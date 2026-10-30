#include "DWBuildingAuthoringLibrary.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.h"
#include "DWPlaceableBuilding.h"
#include "Engine/DataTable.h"
#include "Engine/Blueprint.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/Character.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "IAssetTools.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Framework/Docking/TabManager.h"
#include <type_traits>

namespace DWAuthoring
{
    const FString ActorRoot=TEXT("/Game/DoughWorld/Gameplay/Buildings");
    const FString DataRoot=TEXT("/Game/DoughWorld/Core/Data");
    const FString AuthoritativeConfigPath=TEXT("/Game/DoughWorld/Core/Data/DA_DoughWorldGameplay.DA_DoughWorldGameplay");
    bool ValidStem(const FString& S)
    {
        if(S.IsEmpty() || S.Len()>64) return false;
        for(TCHAR C:S) if(!((C>='a'&&C<='z')||(C>='A'&&C<='Z')||(C>='0'&&C<='9')||C=='_')) return false;
        return true;
    }
    FString BPName(const UDWBuildingAuthoringDraft* D){return TEXT("BP_DWBuilding_")+D->AssetStem;}
    FString DAName(const UDWBuildingAuthoringDraft* D){return TEXT("DA_DWBuilding_")+D->AssetStem;}
    bool Exists(const FString& P){return FindPackage(nullptr,*P)!=nullptr || FPackageName::DoesPackageExist(P);}
    void CopyDefinition(const UDWBuildingAuthoringDraft* D,UDWBuildingDefinition* Def)
    {
        Def->BuildingItemId=D->ItemId; Def->StationId=D->StationId; Def->Parts=D->Parts;
        Def->FootprintHalfExtent=D->FootprintHalfExtent; Def->MaxPlacementDistance=D->MaxPlacementDistance;
        Def->MaxSlopeDegrees=D->MaxSlopeDegrees; Def->MaxSupportHeightDifference=D->MaxSupportHeightDifference;
        Def->InteractionRadius=D->InteractionRadius; Def->RequiredCraftingStation=D->RequiredCraftingStation;
        Def->StackLimit=D->StackLimit; Def->ActorClass=ADWPlaceableBuilding::StaticClass();
    }
    bool SafeExistingPackage(UObject* O,FString& Error)
    {
        if(!O)return true;
        UPackage* P=O->GetOutermost();
        if(P==GetTransientPackage() || !P->GetName().StartsWith(TEXT("/Game/")))
        {Error=TEXT("目标配置/表必须是已保存的项目资产：")+O->GetPathName();return false;}
        if(P->IsDirty()){Error=TEXT("已有目标资产有未保存改动，请先自行保存后再发布：")+P->GetName();return false;}
        FString Filename=FPackageName::LongPackageNameToFilename(P->GetName(),FPackageName::GetAssetPackageExtension());
        if(!IFileManager::Get().FileExists(*Filename)){Error=TEXT("目标资产尚未保存到磁盘：")+P->GetName();return false;}
        if(IFileManager::Get().IsReadOnly(*Filename)){Error=TEXT("目标文件只读，请先检出或解除只读：")+Filename;return false;}
        return true;
    }
    template<class T> bool ReadTable(UDataTable* Table,const TArray<T>& Array,TArray<T>& Out,FString& Error)
    {
        Out=Array;
        if(!Table)return true;
        if(!Table->GetRowStruct() || !Table->GetRowStruct()->IsChildOf(T::StaticStruct()))
        {Error=TEXT("数据表行类型不匹配：")+Table->GetPathName();return false;}
        Out.Reset();
        for(const auto& Pair:Table->GetRowMap())
        {
            T Value=*reinterpret_cast<const T*>(Pair.Value);
            if constexpr(std::is_same_v<T,FDWItemDefinition>){if(Value.ItemId.IsNone())Value.ItemId=Pair.Key;}
            else {if(Value.RecipeId.IsNone())Value.RecipeId=Pair.Key;}
            Out.Add(Value);
        }
        return true;
    }
    FName ItemRow(UDataTable* T,FName Id)
    {
        if(T)for(const auto& P:T->GetRowMap())
        {const auto* V=reinterpret_cast<const FDWItemDefinition*>(P.Value);if((V->ItemId.IsNone()?P.Key:V->ItemId)==Id)return P.Key;}
        return Id;
    }
    FName RecipeRow(UDataTable* T,FName Id)
    {
        if(T)for(const auto& P:T->GetRowMap())
        {const auto* V=reinterpret_cast<const FDWRecipeDefinition*>(P.Value);if((V->RecipeId.IsNone()?P.Key:V->RecipeId)==Id)return P.Key;}
        return Id;
    }
    void AddPackage(TArray<UPackage*>& Packages,UObject* O){if(O)Packages.AddUnique(O->GetOutermost());}
    bool CheckVisualDependencies(UDWBuildingAuthoringDraft* D,FString& Error)
    {
        TSet<FName> Owned,Seen;TArray<FName> Queue;
        for(const FString& Path:D->ImportedAssets)Owned.Add(FName(*FPackageName::ObjectPathToPackageName(Path)));
        const auto Add=[&](UObject* O){if(O)Queue.AddUnique(O->GetOutermost()->GetFName());};
        Add(D->Icon);
        for(const auto& Part:D->Parts)
        {
            if(UStaticMesh* M=Part.Mesh.LoadSynchronous())
            {Add(M);for(const FStaticMaterial& Slot:M->GetStaticMaterials())Add(Slot.MaterialInterface);}
            for(const auto& Material:Part.Materials)if(!Material.IsNull())
            {auto* M=Material.LoadSynchronous();if(!M){Error=TEXT("材质无法载入：")+Material.ToString();return false;}Add(M);}
        }
        auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        for(int32 Index=0;Index<Queue.Num();++Index)
        {
            const FName Name=Queue[Index];const FString Path=Name.ToString();if(Seen.Contains(Name))continue;Seen.Add(Name);
            if(!Path.StartsWith(TEXT("/Game/")))continue;
            if(!Owned.Contains(Name))
            {
                if(UPackage* P=FindPackage(nullptr,*Path);P&&P->IsDirty())
                {Error=TEXT("建筑依赖含用户未保存修改，请先自行处理：")+Path;return false;}
                if(!FPackageName::DoesPackageExist(Path))
                {Error=TEXT("建筑依赖尚未保存到磁盘：")+Path;return false;}
            }
            TArray<FName> Deps;Registry.GetDependencies(Name,Deps);
            for(FName Dep:Deps)if(!Seen.Contains(Dep))Queue.AddUnique(Dep);
        }
        return true;
    }
}

UDWGameplayConfig* UDWBuildingAuthoringLibrary::GetAuthoritativeConfig()
{return LoadObject<UDWGameplayConfig>(nullptr,*DWAuthoring::AuthoritativeConfigPath);}

UDWBuildingAuthoringDraft* UDWBuildingAuthoringLibrary::CreateDraft(UDWGameplayConfig* Config)
{
    auto* D=NewObject<UDWBuildingAuthoringDraft>(GetTransientPackage());D->Config=Config?Config:GetAuthoritativeConfig();
    D->PlayerReferenceClass=TSoftClassPtr<ACharacter>(FSoftObjectPath(TEXT("/Game/DoughWorld/Characters/Hero/Blueprints/BP_DWTopDownCharacter.BP_DWTopDownCharacter_C")));
    return D;
}

TArray<FString> UDWBuildingAuthoringDraft::GetRequiredStationOptions() const
{
    TArray<FString> Options{TEXT("None")};
    if(Config)
    {
        if(const auto* Profile=UDWGameplayConfig::AsGatheringConfig(Config))for(const auto& P:Profile->RecipeStations)if(!P.Value.IsNone())Options.AddUnique(P.Value.ToString());
        for(const auto& Ref:Config->BuildingDefinitions)if(const auto* Def=Ref.LoadSynchronous())if(!Def->StationId.IsNone())Options.AddUnique(Def->StationId.ToString());
    }
    if(!RequiredCraftingStation.IsNone())Options.AddUnique(RequiredCraftingStation.ToString());Options.Sort();return Options;
}

bool UDWBuildingAuthoringLibrary::ReadEffectiveDefinitions(UDWGameplayConfig* Config,TArray<FDWItemDefinition>& Items,TArray<FDWRecipeDefinition>& Recipes,FString& Error)
{
    Error.Reset(); Items.Reset();Recipes.Reset();
    if(!Config){Error=TEXT("请选择游戏配置。");return false;}
    if(!DWAuthoring::ReadTable(Config->ItemTable,Config->Items,Items,Error) || !DWAuthoring::ReadTable(Config->RecipeTable,Config->Recipes,Recipes,Error))return false;
    TSet<FName> ItemIds,RecipeIds;
    for(const auto& I:Items){if(I.ItemId.IsNone()||ItemIds.Contains(I.ItemId)){Error=TEXT("物品来源中存在空或重复 ID。");return false;}ItemIds.Add(I.ItemId);}
    for(const auto& R:Recipes){if(R.RecipeId.IsNone()||RecipeIds.Contains(R.RecipeId)){Error=TEXT("配方来源中存在空或重复 ID。");return false;}RecipeIds.Add(R.RecipeId);}
    return true;
}

TArray<FString> UDWBuildingAuthoringLibrary::GetItemOptions(UDWGameplayConfig* Config)
{
    TArray<FString> Result;TArray<FDWItemDefinition> Items;TArray<FDWRecipeDefinition> Recipes;FString Error;
    if(ReadEffectiveDefinitions(Config,Items,Recipes,Error))for(const auto& I:Items)Result.Add(I.ItemId.ToString());
    Result.Sort();return Result;
}

bool UDWBuildingAuthoringLibrary::SelectExistingItem(UDWBuildingAuthoringDraft* D,FName Id)
{
    if(!D)return false;TArray<FDWItemDefinition> Items;TArray<FDWRecipeDefinition> Recipes;FString Error;
    if(!ReadEffectiveDefinitions(D->Config,Items,Recipes,Error))return false;
    const auto* I=Items.FindByPredicate([Id](const auto& X){return X.ItemId==Id;});if(!I)return false;
    D->RecipeId=NAME_None;D->Ingredients.Reset();D->bUpdateExistingRecipe=false;D->RequiredCraftingStation=NAME_None;D->bCreateRecipe=false;D->bRequiresYeast=false;
    D->ItemId=Id;D->DisplayName=I->DisplayName;D->EnglishDisplayName=I->EnglishDisplayName;D->Icon=I->Icon;
    D->AssetStem=Id.ToString();D->bUpdateExistingItem=true;
    for(const auto& R:Recipes)if(R.Outputs.Num()==1&&R.Outputs[0].ItemId==Id)
    {D->RecipeId=R.RecipeId;D->Ingredients=R.Inputs;D->bRequiresYeast=R.bRequiresYeast;D->bUpdateExistingRecipe=true;D->bCreateRecipe=true;break;}
    if(auto* P=UDWGameplayConfig::AsGatheringConfig(D->Config))
    {D->RequiredCraftingStation=P->GetRequiredStation(D->RecipeId);if(const int32* N=P->StackLimits.Find(Id))D->StackLimit=*N;}
    return true;
}

bool UDWBuildingAuthoringLibrary::LoadDefinitionAsNewDraft(UDWBuildingAuthoringDraft* D,UDWBuildingDefinition* Def)
{
    if(!D||!Def)return false;
    D->ItemId=NAME_None;D->DisplayName=FText::GetEmpty();D->EnglishDisplayName=FText::GetEmpty();D->Icon=nullptr;
    D->RecipeId=NAME_None;D->Ingredients.Reset();D->bUpdateExistingItem=false;D->bUpdateExistingRecipe=false;D->bCreateRecipe=false;D->bRequiresYeast=false;D->RequiredCraftingStation=NAME_None;
    SelectExistingItem(D,Def->BuildingItemId);
    if(D->DisplayName.IsEmpty())D->DisplayName=FText::FromName(Def->BuildingItemId);
    D->Parts=Def->Parts;D->FootprintHalfExtent=Def->FootprintHalfExtent;D->StationId=Def->StationId;
    D->MaxPlacementDistance=Def->MaxPlacementDistance;D->MaxSlopeDegrees=Def->MaxSlopeDegrees;
    D->MaxSupportHeightDifference=Def->MaxSupportHeightDifference;D->InteractionRadius=Def->InteractionRadius;
    D->StackLimit=Def->StackLimit;D->RequiredCraftingStation=Def->RequiredCraftingStation;
    D->AssetStem=Def->BuildingItemId.ToString()+TEXT("_New");D->ItemId=FName(*D->AssetStem);
    D->RecipeId=FName(*(TEXT("Craft_")+D->AssetStem));D->bUpdateExistingItem=false;D->bUpdateExistingRecipe=false;
    D->ImportedAssets.Reset();return true;
}

bool UDWBuildingAuthoringLibrary::ValidateDraft(UDWBuildingAuthoringDraft* D,FString& Report)
{
    using namespace DWAuthoring;
    Report.Reset(); const auto Fail=[&](const FString& E){Report=TEXT("不能发布：")+E;return false;};
    if(!D || !D->Config)return Fail(TEXT("无法读取当前游戏配置 DA_DoughWorldGameplay，请确认该资产存在。"));
    if(D->Config->GetPathName()!=AuthoritativeConfigPath)return Fail(TEXT("发布只能登记当前游戏统一配置 DA_DoughWorldGameplay；其他配置仅可用于隔离检查，不可从此工具发布。"));
    if(!UDWGameplayConfig::AsGatheringConfig(D->Config.Get()))return Fail(TEXT("正式主配置尚未启用采集能力，请先完成统一配置合入。"));
    if(GEditor&&GEditor->PlayWorld)return Fail(TEXT("请先停止试玩。"));
    if(!ValidStem(D->AssetStem))return Fail(TEXT("资源名称须为 1–64 个英文字母、数字或下划线。"));
    if(D->ItemId.IsNone() || D->DisplayName.IsEmpty())return Fail(TEXT("物品 ID 与中文名称不能为空。"));
    if(Exists(ActorRoot/BPName(D)) || Exists(DataRoot/DAName(D)))return Fail(TEXT("目标 BP 或建筑定义已存在；不会覆盖。请使用新资源名称。"));
    FString E;
    if(!SafeExistingPackage(D->Config,E)||!SafeExistingPackage(D->Config->ItemTable,E)||(D->bCreateRecipe&&!SafeExistingPackage(D->Config->RecipeTable,E)))return Fail(E);
    for(const auto& Ref:D->Config->BuildingDefinitions)
    {
        auto* Def=Ref.LoadSynchronous();
        if(!Def)return Fail(TEXT("当前配置包含无法载入的建筑定义：")+Ref.ToString());
        if(Def->BuildingItemId==D->ItemId)return Fail(TEXT("该物品已有建筑定义；本版工具不覆盖已发布建筑。"));
    }
    TArray<FDWItemDefinition> Items;TArray<FDWRecipeDefinition> Recipes;
    if(!ReadEffectiveDefinitions(D->Config,Items,Recipes,E))return Fail(E);
    bool HasItem=Items.ContainsByPredicate([D](const auto& I){return I.ItemId==D->ItemId;});
    if(HasItem&&!D->bUpdateExistingItem)return Fail(TEXT("物品 ID 已存在；如为既有建筑，请明确勾选允许更新同 ID 物品。"));
    if(!HasItem&&D->Config->ItemTable&&D->Config->ItemTable->GetRowMap().Contains(D->ItemId))return Fail(TEXT("物品表存在同名行但 ItemId 不同，拒绝覆盖。"));
    auto* Temp=NewObject<UDWBuildingDefinition>();CopyDefinition(D,Temp);
    if(!Temp->ValidateDefinition(E))return Fail(E);
    for(const auto& P:D->Parts)
    {
        auto* Mesh=P.Mesh.LoadSynchronous();
        if(!Mesh)return Fail(TEXT("模型分件无法载入：")+P.Mesh.ToString());
        if(Mesh->GetOutermost()->IsDirty()&&!D->ImportedAssets.Contains(Mesh->GetPathName()))return Fail(TEXT("引用模型含用户未保存修改：")+Mesh->GetPathName());
        for(const auto& M:P.Materials)if(!M.IsNull()&&!M.LoadSynchronous())return Fail(TEXT("材质引用无法载入：")+M.ToString());
    }
    if(!CheckVisualDependencies(D,E))return Fail(E);
    if(D->bCreateRecipe)
    {
        if(D->RecipeId.IsNone()||D->Ingredients.IsEmpty())return Fail(TEXT("配方需要 ID 和至少一种原料。"));
        bool HasRecipe=Recipes.ContainsByPredicate([D](const auto& R){return R.RecipeId==D->RecipeId;});
        if(HasRecipe&&!D->bUpdateExistingRecipe)return Fail(TEXT("配方 ID 已存在；需要明确允许更新。"));
        if(!HasRecipe&&D->Config->RecipeTable&&D->Config->RecipeTable->GetRowMap().Contains(D->RecipeId))return Fail(TEXT("配方表存在同名行但 RecipeId 不同，拒绝覆盖。"));
        TSet<FName> Seen;
        for(const auto& I:D->Ingredients)
        {
            if(I.ItemId==D->ItemId||I.Quantity<1||Seen.Contains(I.ItemId)||!Items.ContainsByPredicate([&](const auto& X){return X.ItemId==I.ItemId;}))
                return Fail(TEXT("原料必须是当前配置已有物品、数量大于零且不重复，也不能消耗建筑本身。"));
            if(auto* P=UDWGameplayConfig::AsGatheringConfig(D->Config);P&&P->NonInventoryItems.Contains(I.ItemId))return Fail(TEXT("原料不能是现有非背包建筑。"));
            Seen.Add(I.ItemId);
        }
    }
    Report=FString::Printf(TEXT("校验通过。\n新增 Actor：%s\n新增定义：%s\n登记配置：%s\n物品来源：%s\n配方来源：%s\n物品操作：%s %s\n配方操作：%s\n模型分件：%d；背包上限：%d\n默认放置：20 cm 格；Q 逆时针 / E 顺时针，每次 15°\n只有下列目标与本草稿导入包会被保存，现有地图与 UI 不在保存范围。\n"),
        *(ActorRoot/BPName(D)),*(DataRoot/DAName(D)),*D->Config->GetPathName(),
        *(D->Config->ItemTable?D->Config->ItemTable->GetPathName():D->Config->GetPathName()+TEXT(".Items")),
        *(D->Config->RecipeTable?D->Config->RecipeTable->GetPathName():D->Config->GetPathName()+TEXT(".Recipes")),
        HasItem?TEXT("更新"):TEXT("新增"),*D->ItemId.ToString(),D->bCreateRecipe?*D->RecipeId.ToString():TEXT("不发布配方"),D->Parts.Num(),D->StackLimit);
    Report+=TEXT("配置包：")+D->Config->GetOutermost()->GetName()+TEXT("\n");
    if(D->Config->ItemTable)Report+=TEXT("物品表包：")+D->Config->ItemTable->GetOutermost()->GetName()+TEXT("\n");
    if(D->bCreateRecipe&&D->Config->RecipeTable)Report+=TEXT("配方表包：")+D->Config->RecipeTable->GetOutermost()->GetName()+TEXT("\n");
    for(const FString& A:D->ImportedAssets)Report+=TEXT("导入依赖：")+A+TEXT("\n");
    if(!D->RequiredCraftingStation.IsNone())Report+=TEXT("所需工作台：")+D->RequiredCraftingStation.ToString()+TEXT("（请确保目标场景有该站点）\n");
    return true;
}

FDWBuildingAuthoringResult UDWBuildingAuthoringLibrary::PublishDraft(UDWBuildingAuthoringDraft* D,bool bConfirmed)
{
    using namespace DWAuthoring;FDWBuildingAuthoringResult Result;
    if(!bConfirmed){Result.Message=TEXT("尚未确认发布，没有改动。");return Result;}
    if(!ValidateDraft(D,Result.Message))return Result;
    TArray<FDWItemDefinition> CurrentItems;TArray<FDWRecipeDefinition> CurrentRecipes;FString ReadError;
    if(!ReadEffectiveDefinitions(D->Config,CurrentItems,CurrentRecipes,ReadError)){Result.Message=ReadError;return Result;}
    const FString DefPackage=DataRoot/DAName(D),BPPackage=ActorRoot/BPName(D);
    auto* Def=NewObject<UDWBuildingDefinition>(CreatePackage(*DefPackage),*DAName(D),RF_Public|RF_Standalone|RF_Transactional);
    CopyDefinition(D,Def); FAssetRegistryModule::AssetCreated(Def);
    auto* BP=FKismetEditorUtilities::CreateBlueprint(ADWPlaceableBuilding::StaticClass(),CreatePackage(*BPPackage),*BPName(D),BPTYPE_Normal,NAME_None);
    if(!BP){Result.Message=TEXT("创建 Blueprint 失败；配置未修改。新定义保留在内存供检查。");return Result;}
    FAssetRegistryModule::AssetCreated(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    auto* CDO=BP->GeneratedClass?Cast<ADWPlaceableBuilding>(BP->GeneratedClass->GetDefaultObject()):nullptr;
    if(!CDO){Result.Message=TEXT("建筑 Blueprint 编译未产生正确类，配置未修改。");return Result;}
    CDO->Definition=Def;FBlueprintEditorUtils::MarkBlueprintAsModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
    if(BP->Status==BS_Error){Result.Message=TEXT("建筑 Blueprint 编译失败，配置未修改。");return Result;}
    CDO=Cast<ADWPlaceableBuilding>(BP->GeneratedClass->GetDefaultObject());
    if(!CDO){Result.Message=TEXT("重新编译后无法读取建筑默认对象，配置未修改。");return Result;}
    if(CDO->Definition!=Def){CDO->Modify();CDO->Definition=Def;BP->MarkPackageDirty();}
    Def->ActorClass=BP->GeneratedClass;
    Result.BlueprintPath=BP->GetPathName();Result.DefinitionPath=Def->GetPathName();
    const FScopedTransaction Transaction(NSLOCTEXT("DWBuildingAuthoring","Publish","发布背包建筑与配方"));
    D->Config->Modify();
    FDWItemDefinition Item;
    if(const auto* ExistingItem=CurrentItems.FindByPredicate([D](const auto& I){return I.ItemId==D->ItemId;}))Item=*ExistingItem;
    Item.ItemId=D->ItemId;Item.DisplayName=D->DisplayName;Item.EnglishDisplayName=D->EnglishDisplayName;Item.Icon=D->Icon;Item.bUsable=true;
    if(D->Config->ItemTable){D->Config->ItemTable->Modify();D->Config->ItemTable->AddRow(ItemRow(D->Config->ItemTable,D->ItemId),Item);}
    else
    {auto* Existing=D->Config->Items.FindByPredicate([D](const auto& I){return I.ItemId==D->ItemId;});if(Existing)*Existing=Item;else D->Config->Items.Add(Item);}
    if(D->bCreateRecipe)
    {
        FDWRecipeDefinition R;R.RecipeId=D->RecipeId;R.DisplayName=D->DisplayName;R.EnglishDisplayName=D->EnglishDisplayName;R.Inputs=D->Ingredients;R.Outputs={FDWItemStack(D->ItemId,1)};R.bRequiresYeast=D->bRequiresYeast;
        if(D->Config->RecipeTable){D->Config->RecipeTable->Modify();D->Config->RecipeTable->AddRow(RecipeRow(D->Config->RecipeTable,D->RecipeId),R);}
        else {auto* Old=D->Config->Recipes.FindByPredicate([D](const auto& X){return X.RecipeId==D->RecipeId;});if(Old)*Old=R;else D->Config->Recipes.Add(R);}
    }
    D->Config->BuildingDefinitions.Add(Def);
    if(auto* P=UDWGameplayConfig::AsGatheringConfig(D->Config))
    {
        P->NonInventoryItems.Remove(D->ItemId);P->StackLimits.Add(D->ItemId,D->StackLimit);
        if(D->bCreateRecipe)
        {P->RecipeBuildings.Add(D->RecipeId,D->ItemId);if(D->RequiredCraftingStation.IsNone())P->RecipeStations.Remove(D->RecipeId);else P->RecipeStations.Add(D->RecipeId,D->RequiredCraftingStation);}
    }
    D->Config->RefreshDefinitionsFromTables();
    TArray<UPackage*> Packages;AddPackage(Packages,Def);AddPackage(Packages,BP);
    for(const FString& Path:D->ImportedAssets)if(UObject* O=LoadObject<UObject>(nullptr,*Path))AddPackage(Packages,O);
    AddPackage(Packages,D->Config->ItemTable);if(D->bCreateRecipe)AddPackage(Packages,D->Config->RecipeTable);AddPackage(Packages,D->Config);
    for(UPackage* P:Packages)
    {
        P->MarkPackageDirty();Result.ChangedPackages.Add(P->GetName());
        const FString File=FPackageName::LongPackageNameToFilename(P->GetName(),FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(P,nullptr,*File,Args))
        {Result.Message=TEXT("部分发布：保存失败于 ")+P->GetName()+TEXT("。已生成对象和已保存的包均保留；请检查输出日志与 ChangedPackages。未保存其他用户包。");return Result;}
    }
    Result.bSuccess=true;Result.Message=TEXT("已发布建筑 Actor、定义、物品和配方；现有地图与 UI 未保存或修改。\n")+Result.BlueprintPath+TEXT("\n")+Result.DefinitionPath;
    const FString ReportFile=FPaths::ProjectSavedDir()/TEXT("BuildingAuthoring")/(D->AssetStem+TEXT("_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT(".txt"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportFile),true);
    FFileHelper::SaveStringToFile(Result.Message+TEXT("\n")+FString::Join(Result.ChangedPackages,TEXT("\n")),*ReportFile,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return Result;
}

FDWBuildingAuthoringResult UDWBuildingAuthoringLibrary::ImportModels(UDWBuildingAuthoringDraft* D,const TArray<FString>& Filenames,bool bShowImportOptions)
{
    FDWBuildingAuthoringResult R;if(!D||!DWAuthoring::ValidStem(D->AssetStem)){R.Message=TEXT("请先填写有效资源名称。");return R;}
    if(GEditor&&GEditor->PlayWorld){R.Message=TEXT("请先停止试玩。");return R;}
    if(Filenames.IsEmpty()){R.Message=TEXT("未选择文件。");return R;}
    for(const FString& File:Filenames)if(!FPaths::FileExists(File)||!FPaths::GetExtension(File).Equals(TEXT("fbx"),ESearchCase::IgnoreCase))
    {R.Message=TEXT("本版导入接受存在的 FBX 文件。其他格式请先通过 UE 正常导入，再选择已有模型。");return R;}
    TSet<FString> FileStems;
    for(const FString& File:Filenames)
    {FString Stem=FPaths::GetBaseFilename(File).ToLower();if(FileStems.Contains(Stem)){R.Message=TEXT("本批 FBX 有相同文件名，会产生资源冲突。请使用不同文件名或分次以新资源名导入。");return R;}FileStems.Add(Stem);}
    const FString Folder=DWAuthoring::ActorRoot/TEXT("Art")/D->AssetStem;
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FAssetData> ExistingAssets;Registry.GetAssetsByPath(FName(*Folder),ExistingAssets,true);
    FString DiskFolder=FPackageName::LongPackageNameToFilename(Folder);TArray<FString> ExistingFiles;
    IFileManager::Get().FindFilesRecursive(ExistingFiles,*DiskFolder,TEXT("*"),true,false);
    if(!ExistingAssets.IsEmpty()||!ExistingFiles.IsEmpty())
    {R.Message=TEXT("模型目录已存在资产：")+Folder+TEXT("。请选择已有模型复用；若确实要创建副本，请先使用新的资源名称。未导入或覆盖任何文件。");return R;}
    TArray<UAssetImportTask*> Tasks;
    for(const FString& File:Filenames)
    {
        auto* T=NewObject<UAssetImportTask>();T->Filename=File;T->DestinationPath=Folder;T->bReplaceExisting=false;T->bReplaceExistingSettings=false;T->bAutomated=!bShowImportOptions;T->bSave=false;T->bAsync=false;
        auto* Options=NewObject<UFbxImportUI>();Options->MeshTypeToImport=FBXIT_StaticMesh;Options->bImportAsSkeletal=false;Options->bImportMesh=true;Options->bImportMaterials=false;Options->bImportTextures=false;Options->bImportAnimations=false;Options->bAutomatedImportShouldDetectType=false;
        Options->StaticMeshImportData->bCombineMeshes=true;Options->StaticMeshImportData->bConvertScene=true;Options->StaticMeshImportData->bConvertSceneUnit=true;
        Options->StaticMeshImportData->bTransformVertexToAbsolute=true;Options->StaticMeshImportData->bBakePivotInVertex=false;Options->StaticMeshImportData->bAutoGenerateCollision=true;
        T->Options=Options;T->Factory=NewObject<UFbxFactory>();Tasks.Add(T);
    }
    FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().ImportAssetTasks(Tasks);
    TArray<FAssetData> Assets;Registry.GetAssetsByPath(FName(*Folder),Assets,true);
    int32 MeshCount=0;
    for(const auto& A:Assets)
    {
        UObject* O=A.GetAsset();if(!O)continue;D->ImportedAssets.AddUnique(O->GetPathName());R.ChangedPackages.AddUnique(O->GetOutermost()->GetName());
        if(auto* M=Cast<UStaticMesh>(O)){FDWBuildingMeshPart P;P.PartName=M->GetFName();P.Mesh=M;D->Parts.Add(P);++MeshCount;}
    }
    R.bSuccess=MeshCount>0;R.Message=FString::Printf(TEXT("导入 %d 个静态网格分件至 %s。新资源尚未保存，发布时仅保存本草稿依赖；既有资源未覆盖。"),MeshCount,*Folder);return R;
}

bool UDWBuildingAuthoringLibrary::OpenBuildingAuthoringTool()
{return FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("DWBuildingAuthoring"))).IsValid();}

FString UDWBuildingAuthoringLibrary::InspectCurrentEditorWorldStructure()
{
    auto Result=MakeShared<FJsonObject>();
    UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
    const bool Valid=World&&World->WorldType==EWorldType::Editor&&!World->bIsTearingDown&&!(GEditor&&GEditor->PlayWorld);
    Result->SetBoolField(TEXT("valid_editor_world"),Valid);
    if(Valid)
    {
        const bool OnlyPersistent=World->GetLevels().Num()==1&&World->GetLevels()[0]==World->PersistentLevel;
        Result->SetStringField(TEXT("world_package"),World->GetOutermost()->GetName());
        Result->SetBoolField(TEXT("is_partitioned_world"),World->IsPartitionedWorld());
        Result->SetBoolField(TEXT("has_world_composition"),World->WorldComposition!=nullptr);
        Result->SetNumberField(TEXT("streaming_level_count"),World->GetStreamingLevels().Num());
        Result->SetNumberField(TEXT("level_count"),World->GetLevels().Num());
        Result->SetBoolField(TEXT("only_persistent_level"),OnlyPersistent);
        Result->SetBoolField(TEXT("matches_single_resident_structure"),!World->IsPartitionedWorld()&&!World->WorldComposition&&World->GetStreamingLevels().IsEmpty()&&OnlyPersistent);
    }
    FString Json;FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Json));return Json;
}
