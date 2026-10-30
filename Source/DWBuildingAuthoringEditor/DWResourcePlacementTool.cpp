#include "DWResourcePlacementTool.h"
#include "DWResourcePlacementSettings.h"
#include "DWResourcePlacementMath.h"
#include "DWPlacementGrid.h"
#include "DWPlacementGuard.h"
#include "DWResourcePlacementRecipe.h"
#include "DWGatheringWorld.h"
#include "DWGatheringPond.h"
#include "DWInteractionPromptComponent.h"
#include "DWPickupPresentationComponent.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "LevelUtils.h"
#include "EngineUtils.h"
#include "GameFramework/Volume.h"
#include "LandscapeProxy.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "CollisionQueryParams.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Framework/Docking/TabManager.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include <cmath>

namespace
{
constexpr int32 MaxCount=500;
constexpr int32 AttemptsPerPoint=50;
const FName TabName(TEXT("DWResourcePlacement"));
const FString GroupPrefix(TEXT("DWPlacementGroup_"));

struct FPlacementPlan
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ULevel> Level;
    TWeakObjectPtr<AActor> Ground;
    TWeakObjectPtr<AVolume> AreaVolume;
    TWeakObjectPtr<UClass> Class;
    TWeakObjectPtr<UDWResourcePlacementRecipe> Recipe;
    FString RecipeFingerprint;
    TArray<FTransform> Transforms;
    FString Diagnostics;
    FVector Center=FVector::ZeroVector,Extent=FVector::ZeroVector;
};


static bool ValidSettings(const UDWResourcePlacementSettings& S,FString& Error)
{
    const auto Finite=[](double V){return std::isfinite(V);};
    if(S.Center.ContainsNaN()||S.Extent.ContainsNaN()||S.Extent.X<=0.||S.Extent.Y<=0.||S.Extent.Z<=0.||
       S.Extent.GetMax()>1000000.||S.Center.GetAbsMax()>10000000.||S.Count<1||S.Count>MaxCount||
       !Finite(S.MinimumDistanceCm)||S.MinimumDistanceCm<0.||S.MinimumDistanceCm>100000.||
       !Finite(S.MaxSlopeDegrees)||S.MaxSlopeDegrees<0.||S.MaxSlopeDegrees>=90.||
       !Finite(S.GroundOffsetCm)||FMath::Abs(S.GroundOffsetCm)>10000.||
       !Finite(S.YawMin)||!Finite(S.YawMax)||S.YawMin>S.YawMax||FMath::Abs(S.YawMin)>36000.||FMath::Abs(S.YawMax)>36000.||
       !Finite(S.ScaleMin)||!Finite(S.ScaleMax)||S.ScaleMin<.05||S.ScaleMax>10.||S.ScaleMin>S.ScaleMax)
    {Error=TEXT("参数无效：检查区域半尺寸、数量、间距、坡度、偏移、角度和缩放范围。");return false;}
    Error.Reset();return true;
}

static UWorld* EditorWorld(FString& Error)
{
    if(!GEditor||GEditor->PlayWorld){Error=TEXT("请先停止游戏预览。工具仅可操作编辑器世界。");return nullptr;}
    UWorld* World=GEditor->GetEditorWorldContext().World();
    if(!World||World->WorldType!=EWorldType::Editor||!World->GetCurrentLevel())
    {Error=TEXT("没有可用的编辑器关卡。");return nullptr;}
    return World;
}

static bool WritableCurrentLevel(UWorld* World,ULevel* Level)
{
    return GEditor&&!GEditor->PlayWorld&&GEditor->GetEditorWorldContext().World()==World&&IsValid(World)&&IsValid(Level)&&World->GetCurrentLevel()==Level&&Level->GetWorld()==World&&
        World->GetLevels().Contains(Level)&&FLevelUtils::IsLevelVisible(Level)&&!FLevelUtils::IsLevelLocked(Level);
}

static bool IsChosenGround(const AActor* Chosen,const AActor* Hit)
{
    if(!IsValid(Chosen)||!IsValid(Hit))return false;
    if(Chosen==Hit)return true;
    const auto* ChosenProxy=Cast<ALandscapeProxy>(Chosen);
    const auto* HitProxy=Cast<ALandscapeProxy>(Hit);
    // Only loaded proxies belonging to the exact same Landscape; never accept a different landscape.
    return ChosenProxy&&HitProxy&&ChosenProxy->GetLandscapeActor()&&
        ChosenProxy->GetLandscapeActor()==HitProxy->GetLandscapeActor();
}

static bool HasAuditedConstruction(const UClass* Class)
{
    // These exact assets were reviewed with their native parent lifecycle and
    // component templates. Edited / unsaved assets must be reviewed again.
    static const TMap<FString,FString> AuditedPackages={
        {TEXT("/Game/DoughWorld/Gameplay/Resources/BP_DWGatherSugarTree_Test.BP_DWGatherSugarTree_Test_C"),TEXT("F3D648630CBF5324D8A9971C2E2DEDF4")},
        {TEXT("/Game/DoughWorld/Gameplay/Resources/BP_DWGatherFlourNode_Test.BP_DWGatherFlourNode_Test_C"),TEXT("1B002D9C9F413E39178BBDF574CFE104")},
        {TEXT("/Game/DoughWorld/Gameplay/Resources/BP_DWGatherSugarChunk_Test.BP_DWGatherSugarChunk_Test_C"),TEXT("668A012C8CB1B04FC322E0A7C69091EA")}
    };
    if(!Class||!Class->GetOutermost()||Class->GetOutermost()->IsDirty())return false;
    const FString* Expected=AuditedPackages.Find(Class->GetPathName());if(!Expected)return false;
    FString Filename;if(!FPackageName::DoesPackageExist(Class->GetOutermost()->GetName(),&Filename))return false;
    const FMD5Hash Actual=FMD5Hash::HashFile(*Filename);
    return Actual.IsValid()&&LexToString(Actual).Equals(*Expected,ESearchCase::IgnoreCase);
}

static bool CheckConvex(UStaticMesh* Mesh)
{
    const auto* Body=Mesh?Mesh->GetBodySetup():nullptr;
    return Body&&Body->GetCollisionTraceFlag()!=CTF_UseComplexAsSimple&&!Body->AggGeom.ConvexElems.IsEmpty();
}
static bool ValidateRecipe(UDWResourcePlacementRecipe* Recipe,UClass* Class,FString& Fingerprint,FString& Error)
{
    if(!IsValid(Recipe)||!Class||Recipe->ActorClass.LoadSynchronous()!=Class||!HasAuditedConstruction(Class))
    {Error=TEXT("配方或目标类未配置、类不匹配或构造未通过审核。");return false;}
    UPackage* Package=Recipe->GetOutermost();FString Filename;
    if(!Package||Package->IsDirty()||!FPackageName::DoesPackageExist(Package->GetName(),&Filename))
    {Error=TEXT("配方尚未保存；请先保存配方再重新预览。");return false;}
    const FMD5Hash Hash=FMD5Hash::HashFile(*Filename);if(!Hash.IsValid()){Error=TEXT("无法校验配方文件。");return false;}Fingerprint=LexToString(Hash);
    UStaticMesh* Mesh=Recipe->Mesh.LoadSynchronous();
    if(!Mesh||!Recipe->MeshRelativeTransform.IsValid()||Recipe->MeshRelativeTransform.GetScale3D().GetMin()<=0.||
       !FMath::IsFinite(Recipe->RandomScaleMin)||!FMath::IsFinite(Recipe->RandomScaleMax)||Recipe->RandomScaleMin<=0.||Recipe->RandomScaleMax<Recipe->RandomScaleMin||
       !FMath::IsFinite(Recipe->MaxDrawDistanceCm)||Recipe->MaxDrawDistanceCm<0.f||
       Recipe->Materials.Num()!=Mesh->GetStaticMaterials().Num())
    {Error=TEXT("配方的网格、正缩放或材质槽数量无效；不会生成占位。");return false;}
    for(const auto& Mat:Recipe->Materials)if(!Mat.LoadSynchronous()){Error=TEXT("配方缺少模型材质。");return false;}
    if(Class->IsChildOf(ADWGatheringResource::StaticClass()))
    {
        if((Recipe->Style!=EDWGatheringDestructionStyle::TreeFall&&Recipe->Style!=EDWGatheringDestructionStyle::RockFracture)||
           !FMath::IsFinite(Recipe->TreePushSpeed)||Recipe->TreePushSpeed<0.||!FMath::IsFinite(Recipe->TreeAngularSpeed)||Recipe->TreeAngularSpeed<0.||
           !FMath::IsFinite(Recipe->UpperTreeMassKg)||Recipe->UpperTreeMassKg<=0.f||!FMath::IsFinite(Recipe->FragmentMassKg)||Recipe->FragmentMassKg<=0.f||
           !FMath::IsFinite(Recipe->CrackSeparation)||Recipe->CrackSeparation<0.f||!FMath::IsFinite(Recipe->FragmentOutwardSpeed)||Recipe->FragmentOutwardSpeed<0.f||
           !FMath::IsFinite(Recipe->FragmentUpwardSpeed)||Recipe->FragmentUpwardSpeed<0.f||!FMath::IsFinite(Recipe->DissolveEdgeWidth)||Recipe->DissolveEdgeWidth<=0.f||
           !FMath::IsFinite(Recipe->LinearDamping)||Recipe->LinearDamping<0.f||!FMath::IsFinite(Recipe->AngularDamping)||Recipe->AngularDamping<0.f||
           !FMath::IsFinite(Recipe->SurfaceFriction)||Recipe->SurfaceFriction<0.f||Recipe->SurfaceFriction>2.f||!FMath::IsFinite(Recipe->Restitution)||Recipe->Restitution<0.f||Recipe->Restitution>1.f||
           !FMath::IsFinite(Recipe->GroundNormalMinZ)||Recipe->GroundNormalMinZ<.1f||Recipe->GroundNormalMinZ>1.f||!FMath::IsFinite(Recipe->SettledContactSeconds)||Recipe->SettledContactSeconds<.05f||
           !FMath::IsFinite(Recipe->DissolveEdgeStrength)||Recipe->DissolveEdgeStrength<0.f||Recipe->DustCount<0||Recipe->DustCount>24||
           !FMath::IsFinite(Recipe->DustRadius)||Recipe->DustRadius<=0.f||!FMath::IsFinite(Recipe->DustDuration)||Recipe->DustDuration<=0.f||
           !FMath::IsFinite(Recipe->DissolveEdgeColor.R)||!FMath::IsFinite(Recipe->DissolveEdgeColor.G)||!FMath::IsFinite(Recipe->DissolveEdgeColor.B)||!FMath::IsFinite(Recipe->DissolveEdgeColor.A)||
           !FMath::IsFinite(Recipe->DustColor.R)||!FMath::IsFinite(Recipe->DustColor.G)||!FMath::IsFinite(Recipe->DustColor.B)||!FMath::IsFinite(Recipe->DustColor.A)||
           !FMath::IsFinite(Recipe->RestBeforeDissolve)||Recipe->RestBeforeDissolve<0.||!FMath::IsFinite(Recipe->DissolveDuration)||Recipe->DissolveDuration<.05f)
        {Error=TEXT("采集表现类型或时间无效。");return false;}
        if(Recipe->DustCount>0&&(!Recipe->DustMesh.LoadSynchronous()||!Recipe->DustMaterial.LoadSynchronous()))
        {Error=TEXT("启用烟尘但网格或材质未配置。");return false;}
        for(const auto& Pair:Recipe->DropMeshes)if(Pair.Key.IsNone()||!Pair.Value.LoadSynchronous()){Error=TEXT("掉落物模型映射无效。");return false;}
        for(const auto& Pair:Recipe->DropMeshTransforms)if(Pair.Key.IsNone()||!Pair.Value.IsValid()){Error=TEXT("掉落物模型变换无效。");return false;}
        if(Recipe->bOverrideGatheringRules)
        {
            if(Recipe->RequiredTool.IsNone()||!FMath::IsFinite(Recipe->MaxDurability)||Recipe->MaxDurability<=0.||Recipe->Drops.IsEmpty())
            {Error=TEXT("采集规则覆盖的工具、耐久或掉落无效。");return false;}
            for(const auto& Item:Recipe->Drops)if(Item.ItemId.IsNone()||Item.Quantity<=0){Error=TEXT("掉落定义无效。");return false;}
        }
        if(Recipe->Style==EDWGatheringDestructionStyle::TreeFall&&(!CheckConvex(Recipe->StumpMesh.LoadSynchronous())||!CheckConvex(Recipe->UpperTreeMesh.LoadSynchronous())||!Recipe->StumpLocalTransform.IsValid()||!Recipe->UpperTreeLocalTransform.IsValid()))
        {Error=TEXT("糖树上下网格、简单凸碰撞或局部变换无效。");return false;}
        if(Recipe->Style==EDWGatheringDestructionStyle::RockFracture)
        {
            if(Recipe->Fragments.Num()<2||Recipe->Fragments.Num()>32){Error=TEXT("碎片数量应为 2–32。");return false;}
            for(const auto& Part:Recipe->Fragments)if(!Part.LocalTransform.IsValid()||!CheckConvex(Part.Mesh.LoadSynchronous())){Error=TEXT("碎片网格、凸碰撞或局部变换无效。");return false;}
        }
        if(Recipe->DissolveSurfaces.IsEmpty()){Error=TEXT("未配置消散材质映射。");return false;}
        for(const auto& Pair:Recipe->DissolveSurfaces)if(!Pair.OriginalMaterial.LoadSynchronous()||!Pair.DissolveMaterial.LoadSynchronous())
        {Error=TEXT("消散材质映射存在空引用。");return false;}
        TArray<UStaticMesh*> Parts;
        if(Recipe->Style==EDWGatheringDestructionStyle::TreeFall){Parts.Add(Recipe->StumpMesh.LoadSynchronous());Parts.Add(Recipe->UpperTreeMesh.LoadSynchronous());}
        else for(const auto& Part:Recipe->Fragments)Parts.Add(Part.Mesh.LoadSynchronous());
        for(const auto* Part:Parts)for(int32 Index=0;Index<Part->GetStaticMaterials().Num();++Index)
        {
            const UMaterialInterface* Original=Part->GetStaticMaterials()[Index].MaterialInterface.Get();bool Found=false;
            for(const auto& Pair:Recipe->DissolveSurfaces)if(Pair.OriginalMaterial.LoadSynchronous()==Original&&Pair.DissolveMaterial.LoadSynchronous()){Found=true;break;}
            if(!Found&&Recipe->Materials.IsValidIndex(Index)&&Recipe->SourceDissolveMaterials.IsValidIndex(Index)&&
               Recipe->Materials[Index].LoadSynchronous()==Original&&Recipe->SourceDissolveMaterials[Index].LoadSynchronous())Found=true;
            if(!Found){Error=FString::Printf(TEXT("网格 %s 的材质槽 %d 没有消散映射。"),*GetNameSafe(Part),Index);return false;}
        }
    }
    else if(Class->IsChildOf(ADWGatheringPickup::StaticClass()))
    {if(Recipe->PickupItem.IsNone()||Recipe->PickupQuantity<1){Error=TEXT("拾取物 ID 或数量无效。");return false;}}
    else {Error=TEXT("目前仅支持已审核资源和拾取物类型。");return false;}
    Error.Reset();return true;
}
static bool ApplyRecipe(ADWGatheringInteractable* Actor,UDWResourcePlacementRecipe* Recipe,bool bExisting,TFunctionRef<bool()> Check)
{
    if(!Check()||!IsValid(Recipe)||!Actor->Mesh)return false;
    UStaticMesh* DesiredMesh=Recipe->Mesh.Get();if(!DesiredMesh)return false;
    Actor->Modify();if(!Check())return false;
    Actor->Mesh->Modify();if(!Check())return false;
    Actor->Mesh->SetStaticMesh(DesiredMesh);if(!Check()||Actor->Mesh->GetStaticMesh()!=DesiredMesh)return false;
    Actor->Mesh->SetRelativeTransform(Recipe->MeshRelativeTransform);if(!Check())return false;
    Actor->Mesh->SetCullDistance(Recipe->MaxDrawDistanceCm);if(!Check())return false;
    Actor->Mesh->SetCastShadow(Recipe->bCastShadow);if(!Check())return false;
    for(int32 I=0;I<Recipe->Materials.Num();++I){auto* Material=Recipe->Materials[I].Get();if(!Material)return false;Actor->Mesh->SetMaterial(I,Material);if(!Check())return false;}
    Actor->RandomScaleMin=Recipe->RandomScaleMin;Actor->RandomScaleMax=Recipe->RandomScaleMax;
    if(auto* R=Cast<ADWGatheringResource>(Actor))
    {
        auto* P=R->GatheringPresentation.Get();if(!P)return false;P->Modify();if(!Check())return false;
        if(!bExisting&&Recipe->bOverrideGatheringRules){R->RequiredTool=Recipe->RequiredTool;R->MaxDurability=R->Durability=Recipe->MaxDurability;R->Drops=Recipe->Drops;}
        for(const auto& Pair:Recipe->DropMeshes)R->DropMeshes.Add(Pair.Key,Pair.Value);
        for(const auto& Pair:Recipe->DropMeshTransforms)R->DropMeshTransforms.Add(Pair.Key,Pair.Value);
        P->Style=Recipe->Style;P->StumpMesh=Recipe->StumpMesh;P->UpperTreeMesh=Recipe->UpperTreeMesh;
        P->StumpLocalTransform=Recipe->StumpLocalTransform;P->UpperTreeLocalTransform=Recipe->UpperTreeLocalTransform;P->Fragments=Recipe->Fragments;
        P->DissolveSurfaces=Recipe->DissolveSurfaces;P->SourceDissolveMaterials=Recipe->SourceDissolveMaterials;
        P->TreePushSpeed=Recipe->TreePushSpeed;P->TreeAngularSpeed=Recipe->TreeAngularSpeed;P->RestBeforeDissolve=Recipe->RestBeforeDissolve;P->DissolveDuration=Recipe->DissolveDuration;
        P->UpperTreeMassKg=Recipe->UpperTreeMassKg;P->CrackSeparation=Recipe->CrackSeparation;P->FragmentMassKg=Recipe->FragmentMassKg;
        P->FragmentOutwardSpeed=Recipe->FragmentOutwardSpeed;P->FragmentUpwardSpeed=Recipe->FragmentUpwardSpeed;
        P->LinearDamping=Recipe->LinearDamping;P->AngularDamping=Recipe->AngularDamping;P->SurfaceFriction=Recipe->SurfaceFriction;P->Restitution=Recipe->Restitution;P->GroundNormalMinZ=Recipe->GroundNormalMinZ;P->SettledContactSeconds=Recipe->SettledContactSeconds;
        P->DissolveEdgeColor=Recipe->DissolveEdgeColor;P->DissolveEdgeWidth=Recipe->DissolveEdgeWidth;P->DissolveEdgeStrength=Recipe->DissolveEdgeStrength;
        P->DustMesh=Recipe->DustMesh;P->DustMaterial=Recipe->DustMaterial;P->DustCount=Recipe->DustCount;P->DustRadius=Recipe->DustRadius;P->DustDuration=Recipe->DustDuration;P->DustColor=Recipe->DustColor;
    }
    else if(auto* Pickup=Cast<ADWGatheringPickup>(Actor)){if(!bExisting){Pickup->ItemId=Recipe->PickupItem;Pickup->Quantity=Recipe->PickupQuantity;}}
    else return false;
    return true;
}
static bool ApplyNewBudget(ADWGatheringInteractable* Actor,const UDWResourcePlacementSettings& Settings,TFunctionRef<bool()> Check)
{
    if(!Check())return false;
    if(auto* Resource=Cast<ADWGatheringResource>(Actor))
        if(Resource->Prompt){Resource->Prompt->Modify();if(!Check())return false;Resource->Prompt->bBudgetGatheringResourcePrompt=Settings.bEnablePromptBudget;Resource->Prompt->GatheringHiddenPollSeconds=Settings.HiddenPromptPollSeconds;}
    if(auto* Pickup=Cast<ADWGatheringPickup>(Actor))
        if(Pickup->PickupPresentation){Pickup->PickupPresentation->Modify();if(!Check())return false;Pickup->PickupPresentation->bBudgetIdlePickupOutline=Settings.bEnablePickupOutlineBudget;Pickup->PickupPresentation->PickupHiddenPollSeconds=Settings.HiddenPromptPollSeconds;}
    return Check();
}

struct FSelectedResourceSnapshot
{
    TWeakObjectPtr<ADWGatheringInteractable> Actor;
    TWeakObjectPtr<UClass> Class;
    TWeakObjectPtr<ULevel> Level;
    TWeakObjectPtr<UStaticMeshComponent> Mesh;
    TWeakObjectPtr<UDWInteractionPromptComponent> Prompt;
    TWeakObjectPtr<UDWGatheringPresentationComponent> GatheringPresentation;
    TWeakObjectPtr<UDWPickupPresentationComponent> PickupPresentation;
    FName Id,RequiredTool,ItemId;FTransform Transform;TArray<FName> Tags;
    double Durability=0.,MaxDurability=0.;int32 Quantity=0;TArray<FDWItemStack> Drops;
};
static bool SameDrops(const TArray<FDWItemStack>& A,const TArray<FDWItemStack>& B)
{
    if(A.Num()!=B.Num())return false;
    for(int32 I=0;I<A.Num();++I)if(A[I].ItemId!=B[I].ItemId||A[I].Quantity!=B[I].Quantity)return false;
    return true;
}
static bool SnapshotResource(ADWGatheringInteractable* A,UWorld* W,ULevel* Level,FSelectedResourceSnapshot& S)
{
    if(!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=W||A->GetLevel()!=Level||A->PersistentId.IsNone()||!IsValid(A->Mesh))return false;
    S.Actor=A;S.Class=A->GetClass();S.Level=Level;S.Mesh=A->Mesh;S.Prompt=A->Prompt;S.Id=A->PersistentId;S.Transform=A->GetActorTransform();S.Tags=A->Tags;
    if(auto* R=Cast<ADWGatheringResource>(A))
    {if(!IsValid(R->GatheringPresentation))return false;S.GatheringPresentation=R->GatheringPresentation;S.RequiredTool=R->RequiredTool;S.Durability=R->Durability;S.MaxDurability=R->MaxDurability;S.Drops=R->Drops;}
    else if(auto* P=Cast<ADWGatheringPickup>(A))
    {if(!IsValid(P->PickupPresentation))return false;S.PickupPresentation=P->PickupPresentation;S.ItemId=P->ItemId;S.Quantity=P->Quantity;}
    else return false;
    return true;
}
static bool ResourceStillMatches(const FSelectedResourceSnapshot& S,UWorld* W,ULevel* Level,bool bCheckTransform=true)
{
    auto* A=S.Actor.Get();
    if(!WritableCurrentLevel(W,Level)||!IsValid(A)||A->IsActorBeingDestroyed()||A->GetWorld()!=W||A->GetLevel()!=Level||S.Level.Get()!=Level||
       A->GetClass()!=S.Class.Get()||A->PersistentId!=S.Id||A->Mesh!=S.Mesh.Get()||A->Prompt!=S.Prompt.Get()||A->Tags!=S.Tags||
       (bCheckTransform&&!A->GetActorTransform().Equals(S.Transform,.001)))return false;
    if(auto* R=Cast<ADWGatheringResource>(A))return R->GatheringPresentation==S.GatheringPresentation.Get()&&R->RequiredTool==S.RequiredTool&&
        R->Durability==S.Durability&&R->MaxDurability==S.MaxDurability&&SameDrops(R->Drops,S.Drops);
    if(auto* P=Cast<ADWGatheringPickup>(A))return P->PickupPresentation==S.PickupPresentation.Get()&&P->ItemId==S.ItemId&&P->Quantity==S.Quantity;
    return false;
}
static bool HoldRecipeAssets(UDWResourcePlacementRecipe* R,TArray<TStrongObjectPtr<UObject>>& Held)
{
    if(!IsValid(R))return false;
    auto Hold=[&Held](UObject* O){if(O)Held.Emplace(O);};
    Hold(R->ActorClass.Get());Hold(R->Mesh.Get());for(const auto& V:R->Materials)Hold(V.Get());
    Hold(R->StumpMesh.Get());Hold(R->UpperTreeMesh.Get());Hold(R->DustMesh.Get());Hold(R->DustMaterial.Get());
    for(const auto& V:R->Fragments)Hold(V.Mesh.Get());
    for(const auto& V:R->DissolveSurfaces){Hold(V.OriginalMaterial.Get());Hold(V.DissolveMaterial.Get());}
    for(const auto& V:R->SourceDissolveMaterials)Hold(V.Get());for(const auto& V:R->DropMeshes)Hold(V.Value.Get());
    return R->ActorClass.Get()&&R->Mesh.Get();
}
static bool CaptureSelection(UWorld* W,ULevel* Level,UClass* RequiredClass,bool bRequireTool,TArray<FSelectedResourceSnapshot>& Out)
{
    if(!WritableCurrentLevel(W,Level)||!GEditor)return false;
    for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)
    {
        auto* A=Cast<ADWGatheringInteractable>(*It);bool Member=false;
        if(A)for(const FName Tag:A->Tags)if(Tag.ToString().StartsWith(GroupPrefix))Member=true;
        FSelectedResourceSnapshot S;
        if(!A||(RequiredClass&&A->GetClass()!=RequiredClass)||(bRequireTool&&!Member)||!HasAuditedConstruction(A->GetClass())||!SnapshotResource(A,W,Level,S))return false;
        Out.Add(MoveTemp(S));
    }
    return Out.Num()>0&&Out.Num()<=MaxCount;
}
static bool RecheckSelection(const TArray<FSelectedResourceSnapshot>& Snapshots,UWorld* W,ULevel* Level)
{
    for(const auto& S:Snapshots)if(!ResourceStillMatches(S,W,Level))return false;
    return true;
}

static bool AuditResourceIdentities(UWorld* World,FString& Error)
{
    TMap<FName,FString> Seen;
    for(TActorIterator<ADWGatheringInteractable> It(World);It;++It)
    {
        const FName Id=It->PersistentId;const FString S=Id.ToString();
        if(Id.IsNone()||S.Len()>160||S.Contains(TEXT("UEDPIE_"))||S.Contains(TEXT("/"))||S.Contains(TEXT(":")))
        {Error=TEXT("现有资源 ID 为空或无效：")+It->GetActorLabel()+TEXT("。工具不会修改它，请先核对旧档和该对象身份。");return false;}
        if(const FString* Previous=Seen.Find(Id))
        {Error=TEXT("现有资源 ID 重复：")+*Previous+TEXT(" 和 ")+It->GetActorLabel()+TEXT("。如后者是刚复制的新对象，可选中它并使用身份修复；不要修改旧对象。");return false;}
        Seen.Add(Id,It->GetActorLabel());
    }
    for(TActorIterator<ADWGatheringPond> It(World);It;++It)if(It->bAuthoredPersistence)
    {
        const FName Id=It->PersistentId;const FString S=Id.ToString();
        if(Id.IsNone()||S.Len()>160||S.Contains(TEXT("UEDPIE_"))||S.Contains(TEXT("/"))||S.Contains(TEXT(":"))||Seen.Contains(Id))
        {Error=TEXT("已登记水塘 ID 为空、无效或重复：")+It->GetActorLabel();return false;}
        Seen.Add(Id,It->GetActorLabel());
    }
    Error.Reset();return true;
}

static bool MakePlan(const UDWResourcePlacementSettings& S,AActor* Ground,AVolume* AreaVolume,FPlacementPlan& Out,FString& Error)
{
    if(!ValidSettings(S,Error))return false;
    if(!FMath::IsFinite(S.HiddenPromptPollSeconds)||S.HiddenPromptPollSeconds<0.f||S.HiddenPromptPollSeconds>.5f)
    {Error=TEXT("隐藏检查间隔必须为 0–0.5 秒，0 表示关闭降频。");return false;}
    UWorld* World=EditorWorld(Error);
    if(!World)return false;
    ULevel* Level=World->GetCurrentLevel();
    if(!WritableCurrentLevel(World,Level)){Error=TEXT("当前关卡不可写、不可见或已卸载；请检查后重新预览。");return false;}
    TWeakObjectPtr<UWorld> WeakWorld=World;TWeakObjectPtr<ULevel> WeakLevel=Level;
    if(!AuditResourceIdentities(World,Error))return false;
    if(!IsValid(Ground)||Ground->GetWorld()!=World||Ground->IsActorBeingDestroyed())
    {Error=TEXT("请先在当前关卡选中一个地面 Actor，再点‘使用选中地面’。");return false;}
    UClass* Class=S.ResourceClass.LoadSynchronous();
    if(!Class||!Class->IsChildOf(ADWGatheringInteractable::StaticClass())||Class->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists))
    {Error=TEXT("请选择有效的 DW Gathering Interactable 子类；例如糖树、面粉或糖块蓝图。");return false;}
    TStrongObjectPtr<UClass> HeldClass(Class);
    UDWResourcePlacementRecipe* Recipe=S.Recipe.LoadSynchronous();TStrongObjectPtr<UDWResourcePlacementRecipe> HeldRecipe(Recipe);FString RecipeFingerprint;
    if(!ValidateRecipe(HeldRecipe.Get(),HeldClass.Get(),RecipeFingerprint,Error))return false;
    if(!WeakWorld.IsValid()||!WeakLevel.IsValid()||EditorWorld(Error)!=WeakWorld.Get()||!WritableCurrentLevel(WeakWorld.Get(),WeakLevel.Get())){Error=TEXT("关卡在读取配方时发生变化，请重新预览。");return false;}
    DWPlacementGrid::FSpacing Occupied(S.MinimumDistanceCm);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWResourcePlacement),true);
    if(IsValid(AreaVolume)&&AreaVolume->GetWorld()==World&&AreaVolume!=Ground)Params.AddIgnoredActor(AreaVolume);
    // Existing resources and ponds never become a substitute ground surface.
    for(TActorIterator<ADWGatheringInteractable> It(World);It;++It)
    {if(!It->IsActorBeingDestroyed()){const FVector P=It->GetActorLocation();Occupied.Add(P.X,P.Y);Params.AddIgnoredActor(*It);}}
    for(TActorIterator<ADWGatheringPond> It(World);It;++It)Params.AddIgnoredActor(*It);
    if(Ground->IsA<ADWGatheringInteractable>()||Ground->IsA<ADWGatheringPond>())
    {Error=TEXT("地面不能是采集物或水塘；请选 Landscape 或实际地面网格。");return false;}
    FPlacementPlan Candidate;Candidate.World=World;Candidate.Level=Level;Candidate.Ground=Ground;Candidate.AreaVolume=AreaVolume;Candidate.Class=Class;Candidate.Recipe=Recipe;Candidate.RecipeFingerprint=RecipeFingerprint;
    Candidate.Center=S.Center;Candidate.Extent=S.Extent;
    FRandomStream Random(S.Seed);
    int32 Traces=0;DWPlacementGrid::FCounts Counts;TArray<FString> OtherExamples;
    for(int32 Try=0;Try<S.Count*AttemptsPerPoint&&Candidate.Transforms.Num()<S.Count;++Try)
    {
        const double X=Random.FRandRange(S.Center.X-S.Extent.X,S.Center.X+S.Extent.X);
        const double Y=Random.FRandRange(S.Center.Y-S.Extent.Y,S.Center.Y+S.Extent.Y);
        const FVector Top(X,Y,S.Center.Z+S.Extent.Z),Bottom(X,Y,S.Center.Z-S.Extent.Z);
        FHitResult Hit;++Traces;
        if(!World->LineTraceSingleByChannel(Hit,Top,Bottom,ECC_Visibility,Params)){Counts.Record(DWPlacementGrid::EResult::NoHit);continue;}
        if(!IsChosenGround(Ground,Hit.GetActor())){Counts.Record(DWPlacementGrid::EResult::OtherGround);if(OtherExamples.Num()<3)OtherExamples.AddUnique(GetNameSafe(Hit.GetActor()));continue;}
        if(Hit.ImpactPoint.ContainsNaN()||Hit.ImpactNormal.ContainsNaN()){Counts.Record(DWPlacementGrid::EResult::Invalid);continue;}
        if(!DWResourcePlacementMath::AcceptSlope(Hit.ImpactNormal.Z,S.MaxSlopeDegrees)){Counts.Record(DWPlacementGrid::EResult::Steep);continue;}
        const FVector Location=Hit.ImpactPoint+FVector(0.,0.,S.GroundOffsetCm);
        if(Occupied.IsTooClose(Location.X,Location.Y)){Counts.Record(DWPlacementGrid::EResult::TooClose);continue;}
        const double Yaw=Random.FRandRange(S.YawMin,S.YawMax),Scale=Random.FRandRange(S.ScaleMin,S.ScaleMax);
        Candidate.Transforms.Emplace(FRotator(0.,Yaw,0.),Location,FVector(Scale));
        Occupied.Add(Location.X,Location.Y);Counts.Record(DWPlacementGrid::EResult::Accepted);
    }
    Candidate.Diagnostics=FString::Printf(TEXT("目标 %d，成功 %d，射线 %d 次（Z %.1f 至 %.1f cm）。未命中 %d（高度或 Visibility 未判定），命中其他对象 %d（最多三例：%s），地面坡度过大 %d，距已有/新资源过近 %d，无效命中 %d。"),S.Count,Counts.Accepted,Traces,S.Center.Z-S.Extent.Z,S.Center.Z+S.Extent.Z,Counts.NoHit,Counts.Other,OtherExamples.IsEmpty()?TEXT("无"):*FString::Join(OtherExamples,TEXT("、")),Counts.Steep,Counts.Near,Counts.Invalid);
    if(const auto* Default=Cast<ADWGatheringResource>(Class->GetDefaultObject()))
    {
        const auto& Drops=Recipe->bOverrideGatheringRules?Recipe->Drops:Default->Drops;
        for(const auto& Item:Drops)if(!Recipe->DropMeshes.Contains(Item.ItemId)&&!Default->DropMeshes.Contains(Item.ItemId))
            Candidate.Diagnostics+=TEXT(" 提醒：掉落物缺少专用模型映射：")+Item.ItemId.ToString()+TEXT("；游戏可能显示占位。");
    }
    if(Candidate.Transforms.Num()!=S.Count)
    {Error=TEXT("预览不足：")+Candidate.Diagnostics+TEXT("没有创建对象。");return false;}
    if(!WritableCurrentLevel(Candidate.World.Get(),Candidate.Level.Get())){Error=TEXT("关卡在生成预览时发生变化，请重新预览。");return false;}
    Out=MoveTemp(Candidate);Error.Reset();return true;
}

static bool SamePlan(const FPlacementPlan& A,const FPlacementPlan& B)
{
    if(!DWPlacementGuard::SameWritableLevel(A.World.Get(),A.Level.Get(),B.World.Get(),B.Level.Get(),WritableCurrentLevel(A.World.Get(),A.Level.Get()),WritableCurrentLevel(B.World.Get(),B.Level.Get()))||
       A.Ground!=B.Ground||A.AreaVolume!=B.AreaVolume||A.Class!=B.Class||A.Recipe!=B.Recipe||A.RecipeFingerprint!=B.RecipeFingerprint||A.Transforms.Num()!=B.Transforms.Num()||
       !A.Center.Equals(B.Center,.001)||!A.Extent.Equals(B.Extent,.001))return false;
    for(int32 I=0;I<A.Transforms.Num();++I)if(!A.Transforms[I].Equals(B.Transforms[I],.001))return false;
    return true;
}

/** In-panel XY diagram: no transient actor, no world writes, no misleading 3D ghost. */
class SDWPlacementPreview final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SDWPlacementPreview){} SLATE_END_ARGS()
    void Construct(const FArguments&){SetCanTick(false);}
    void SetPlan(const FPlacementPlan& In){Plan=In;Invalidate(EInvalidateWidgetReason::Paint);}
    virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(500.,250.);}
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect& Culling,FSlateWindowElementList& Out,
        int32 Layer,const FWidgetStyle&,bool)const override
    {
        const FVector2D Size=G.GetLocalSize();
        FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),FAppStyle::GetBrush("WhiteBrush"),ESlateDrawEffect::None,FLinearColor(.10f,.13f,.16f,1.f));
        if(Plan.Extent.X<=0.||Plan.Extent.Y<=0.)return Layer+1;
        const double Margin=12.;
        for(const FTransform& T:Plan.Transforms)
        {
            const FVector P=T.GetLocation();
            const double X=Margin+(P.X-(Plan.Center.X-Plan.Extent.X))/(2.*Plan.Extent.X)*(Size.X-2.*Margin);
            const double Y=Margin+(1.-(P.Y-(Plan.Center.Y-Plan.Extent.Y))/(2.*Plan.Extent.Y))*(Size.Y-2.*Margin);
            FSlateDrawElement::MakeBox(Out,Layer+1,G.ToPaintGeometry(FVector2D(7.,7.),FSlateLayoutTransform(FVector2D(X-3.5,Y-3.5))),
                FAppStyle::GetBrush("WhiteBrush"),ESlateDrawEffect::None,FLinearColor(.9f,.65f,.15f,1.f));
        }
        return Layer+1;
    }
private:FPlacementPlan Plan;
};

class SDWResourcePlacementPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWResourcePlacementPanel){} SLATE_END_ARGS()
    void Construct(const FArguments&)
    {
        Draft.Reset(NewObject<UDWResourcePlacementSettings>(GetTransientPackage()));
        FDetailsViewArgs Args;Args.bAllowSearch=true;Args.bHideSelectionTip=true;Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
        Details=FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(Args);
        Details->SetObject(Draft.Get());
        Details->OnFinishedChangingProperties().AddSP(this,&SDWResourcePlacementPanel::InvalidatePlan);
        ChildSlot[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("资源批量摆放：PCGVolume 仅用于指定范围，不需要 PCG 图。先选择地面、读取范围，再查看下方二维中心点预览并确认；该预览不是场景中的三维模型。不会自动删除或重建原对象。新地图和水塘的完整存读仍需单独接入。")))]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("使用选中地面"))).OnClicked(this,&SDWResourcePlacementPanel::UseGround)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("使用选中体积作为区域"))).OnClicked(this,&SDWResourcePlacementPanel::UseVolume)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("糖树"))).OnClicked_Lambda([this](){return Preset(TEXT("BP_DWGatherSugarTree_Test"));})]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("面粉"))).OnClicked_Lambda([this](){return Preset(TEXT("BP_DWGatherFlourNode_Test"));})]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("糖块"))).OnClicked_Lambda([this](){return Preset(TEXT("BP_DWGatherSugarChunk_Test"));})]
        ]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("两选：读取范围并校准高度…"))).OnClicked(this,&SDWResourcePlacementPanel::UseBoth)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("撤回区域读取"))).OnClicked(this,&SDWResourcePlacementPanel::UndoRegion)]
        ]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(STextBlock).Text(FText::FromString(TEXT("水深（世界厘米）")))]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SBox).WidthOverride(110)[SNew(SSpinBox<double>).MinValue(0.1).MaxValue(10000.).Value_Lambda([this](){return DepthCm;}).OnValueChanged_Lambda([this](double V){DepthCm=V;})]]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("读取选中水塘"))).OnClicked(this,&SDWResourcePlacementPanel::ReadPond)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("应用水深"))).OnClicked(this,&SDWResourcePlacementPanel::ApplyPond)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("登记新水塘身份…"))).OnClicked(this,&SDWResourcePlacementPanel::AssignPondId)]
        ]
        +SVerticalBox::Slot().FillHeight(1.f).Padding(8)[Details.ToSharedRef()]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(SBox).HeightOverride(220)[SAssignNew(Preview,SDWPlacementPreview)]]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("生成预览"))).OnClicked(this,&SDWResourcePlacementPanel::Generate)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("确认落地"))).OnClicked(this,&SDWResourcePlacementPanel::Commit)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("选择选中对象所属批次"))).OnClicked(this,&SDWResourcePlacementPanel::SelectGroup)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("选中资源重贴地…"))).OnClicked(this,&SDWResourcePlacementPanel::ReprojectSelected)]
        ]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("所选工具资源应用配方…"))).OnClicked(this,&SDWResourcePlacementPanel::ApplyRecipeToSelection)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("所选资源仅更新距离和阴影…"))).OnClicked(this,&SDWResourcePlacementPanel::ApplyRenderTuning)]
        ]
        +SVerticalBox::Slot().AutoHeight().Padding(5)[SNew(SHorizontalBox)
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("检查现有资源身份"))).OnClicked(this,&SDWResourcePlacementPanel::AuditIds)]
          +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("修复选中副本重复 ID…"))).OnClicked(this,&SDWResourcePlacementPanel::RepairDuplicatedId)]
        ]
        +SVerticalBox::Slot().AutoHeight().Padding(8)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(Status);})]
        ];
    }
private:
    void InvalidatePlan(const FPropertyChangedEvent&){Plan=FPlacementPlan();if(Preview)Preview->SetPlan(Plan);Status=TEXT("参数已改变，请重新生成预览。");}
    void ResetPlan(){Plan=FPlacementPlan();if(Preview)Preview->SetPlan(Plan);}
    void SaveRegionForUndo()
    {
        BeforeCenter=Draft->Center;BeforeExtent=Draft->Extent;BeforeVolume=AreaVolume;BeforeGround=Ground;
        bBeforeAreaWasRead=bAreaWasRead;BeforeAreaTransform=AreaTransform;BeforeAreaBoundsCenter=AreaBoundsCenter;BeforeAreaBoundsExtent=AreaBoundsExtent;bCanUndoRegion=true;
    }
    void RememberVolume(AVolume* Volume,const FVector& Center,const FVector& Extent)
    {AreaVolume=Volume;bAreaWasRead=true;AreaTransform=Volume->GetActorTransform();AreaBoundsCenter=Center;AreaBoundsExtent=Extent;}
    bool CheckVolume(FString& Error)const
    {
        if(!bAreaWasRead)return true;
        auto* V=AreaVolume.Get();if(!GEditor||!IsValid(V)||!V->GetWorld()||V->GetWorld()!=GEditor->GetEditorWorldContext().World())
        {Error=TEXT("范围体积已失效或离开当前地图，请重新读取。");return false;}
        FVector Center,Extent;V->GetActorBounds(false,Center,Extent);
        if(!V->GetActorTransform().Equals(AreaTransform,.001)||!Center.Equals(AreaBoundsCenter,.001)||!Extent.Equals(AreaBoundsExtent,.001))
        {Error=TEXT("范围体积的位置、旋转、缩放或边界已变化，请重新读取并生成预览。");return false;}
        return true;
    }
    AActor* OneSelected()const
    {
        if(!GEditor)return nullptr;USelection* Sel=GEditor->GetSelectedActors();AActor* Only=nullptr;
        for(FSelectionIterator It(*Sel);It;++It)if(auto* A=Cast<AActor>(*It)){if(Only)return nullptr;Only=A;}
        return Only;
    }
    FReply UseGround()
    {
        FString E;UWorld* W=EditorWorld(E);AActor* A=OneSelected();
        if(!W||!A||A->GetWorld()!=W||A->IsA<AVolume>()||A->IsA<ADWGatheringInteractable>()||A->IsA<ADWGatheringPond>())
        {Status=TEXT("请选择当前关卡恰好一个实际地面 Actor，不能选择体积、采集物或水塘。");return FReply::Handled();}
        Ground=A;ResetPlan();Status=TEXT("地面：")+A->GetActorLabel()+TEXT("。请设区域并预览。");return FReply::Handled();
    }
    FReply UseVolume()
    {
        FString E;UWorld* W=EditorWorld(E);AVolume* Volume=Cast<AVolume>(OneSelected());
        if(!W||!Volume||Volume->GetWorld()!=W){Status=TEXT("请恰好选择一个现有 Volume。该 Volume 的世界轴对齐包围盒将作为区域。");return FReply::Handled();}
        FVector C,R;Volume->GetActorBounds(false,C,R);
        if(C.ContainsNaN()||R.ContainsNaN()||R.GetMin()<=0.||R.GetMax()>1000000.){Status=TEXT("体积包围盒无效或过大，未改变草稿。");return FReply::Handled();}
        SaveRegionForUndo();Draft->Center=C;Draft->Extent=R;RememberVolume(Volume,C,R);
        Details->ForceRefresh();ResetPlan();Status=TEXT("已读取体积包围盒；体积仅作区域，不会被删除或修改。");return FReply::Handled();
    }
    FReply UseBoth()
    {
        FString E;UWorld* W=EditorWorld(E);if(!W){Status=E;return FReply::Handled();}
        AVolume* Volume=nullptr;AActor* Surface=nullptr;int32 N=0;
        for(FSelectionIterator It(*GEditor->GetSelectedActors());It;++It)if(auto* A=Cast<AActor>(*It))
        {
            ++N;if(auto* V=Cast<AVolume>(A)){if(Volume){Status=TEXT("请选择一个体积和一个非体积的地面。");return FReply::Handled();}Volume=V;}
            else {if(Surface){Status=TEXT("请选择一个体积和一个地面。");return FReply::Handled();}Surface=A;}
        }
        if(N!=2||!Volume||!Surface||Volume->GetWorld()!=W||Surface->GetWorld()!=W||
           Surface->IsA<ADWGatheringInteractable>()||Surface->IsA<ADWGatheringPond>())
        {Status=TEXT("请在大纲 Ctrl 多选一个 PCGVolume（或其他 Volume）与一个实际地面 Actor。");return FReply::Handled();}
        FVector C,R;Volume->GetActorBounds(false,C,R);
        if(C.ContainsNaN()||R.ContainsNaN()||R.GetMin()<=0.||R.GetMax()>1000000.)
        {Status=TEXT("体积包围盒无效或超出允许范围；没有改变草稿。");return FReply::Handled();}
        FVector GroundCenter,GroundExtent;Surface->GetActorBounds(false,GroundCenter,GroundExtent);
        if(GroundCenter.ContainsNaN()||GroundExtent.ContainsNaN()||GroundExtent.X<=0.||GroundExtent.Y<=0.||
           FMath::Abs(C.X-GroundCenter.X)>R.X+GroundExtent.X||FMath::Abs(C.Y-GroundCenter.Y)>R.Y+GroundExtent.Y)
        {Status=TEXT("体积与选中地面的已加载包围盒在 XY 上不相交；请检查所选地面和加载范围。没有改变草稿。");return FReply::Handled();}
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DWPlacementCalibration),true);
        Query.AddIgnoredActor(Volume);
        for(TActorIterator<ADWGatheringInteractable> It(W);It;++It)Query.AddIgnoredActor(*It);
        for(TActorIterator<ADWGatheringPond> It(W);It;++It)Query.AddIgnoredActor(*It);
        const double HalfProbe=FMath::Max(10000.,R.Z);FHitResult Hit;
        if(!W->LineTraceSingleByChannel(Hit,C+FVector(0,0,HalfProbe),C-FVector(0,0,HalfProbe),ECC_Visibility,Query))
        {Status=TEXT("体积中心上下至少 100 米未命中：请检查 Visibility 碰撞、实际地面是否在射线范围内；没有改变草稿。");return FReply::Handled();}
        if(!IsChosenGround(Surface,Hit.GetActor())||Hit.ImpactPoint.ContainsNaN())
        {Status=TEXT("中心射线先命中其他对象：")+GetNameSafe(Hit.GetActor())+TEXT("；不会自动穿过障碍或选择其他地面。没有改变草稿。");return FReply::Handled();}
        const FText Confirm=FText::FromString(FString::Printf(TEXT("预览：区域中心 Z 从 %.1f 调整至选中地面的 %.1f cm，保留体积 XY 与半尺寸；不会移动体积或地面。确认后可用“撤回区域读取”恢复草稿。"),C.Z,Hit.ImpactPoint.Z));
        if(FMessageDialog::Open(EAppMsgType::YesNo,Confirm)!=EAppReturnType::Yes){Status=TEXT("已取消；草稿和场景未变。");return FReply::Handled();}
        SaveRegionForUndo();RememberVolume(Volume,C,R);
        C.Z=Hit.ImpactPoint.Z;Draft->Center=C;Draft->Extent=R;Ground=Surface;
        Details->ForceRefresh();ResetPlan();Status=TEXT("已读取体积与地面并校准中心高度；只修改面板草稿，可撤回。请生成预览。");return FReply::Handled();
    }
    FReply UndoRegion()
    {
        if(!bCanUndoRegion){Status=TEXT("没有可撤回的区域读取。");return FReply::Handled();}
        Draft->Center=BeforeCenter;Draft->Extent=BeforeExtent;AreaVolume=BeforeVolume;Ground=BeforeGround;
        bAreaWasRead=bBeforeAreaWasRead;AreaTransform=BeforeAreaTransform;AreaBoundsCenter=BeforeAreaBoundsCenter;AreaBoundsExtent=BeforeAreaBoundsExtent;bCanUndoRegion=false;
        Details->ForceRefresh();ResetPlan();Status=TEXT("已恢复读取前的中心、半尺寸与地面选择；不会撤销你在场景中的编辑。");return FReply::Handled();
    }
    FReply Preset(const TCHAR* Name)
    {
        const FString Path=FString::Printf(TEXT("/Game/DoughWorld/Gameplay/Resources/%s.%s_C"),Name,Name);
        UClass* Class=LoadObject<UClass>(nullptr,*Path);
        if(!Class||!Class->IsChildOf(ADWGatheringInteractable::StaticClass())){Status=TEXT("当前资源类不存在或不是兼容的采集 Actor：")+Path;return FReply::Handled();}
        const FString RecipeName=FString(Name)==TEXT("BP_DWGatherSugarTree_Test")?TEXT("DA_DWPlacementSugarTree"):
            FString(Name)==TEXT("BP_DWGatherFlourNode_Test")?TEXT("DA_DWPlacementFlour"):TEXT("DA_DWPlacementSugarChunk");
        const FString RecipePath=FString::Printf(TEXT("/Game/DoughWorld/Gameplay/Resources/%s.%s"),*RecipeName,*RecipeName);
        Draft->ResourceClass=Class;Draft->Recipe=LoadObject<UDWResourcePlacementRecipe>(nullptr,*RecipePath);
        Details->ForceRefresh();ResetPlan();Status=Draft->Recipe.IsNull()?TEXT("缺少已安装的美术配方：")+RecipePath:TEXT("已选择资源和美术配方：")+RecipePath;return FReply::Handled();
    }
    FReply Generate()
    {
        ResetPlan();FString E;
        if(!CheckVolume(E)||!MakePlan(*Draft.Get(),Ground.Get(),AreaVolume.Get(),Plan,E)){Status=E;return FReply::Handled();}
        Preview->SetPlan(Plan);Status=TEXT("已生成二维中心点预览；不保证模型边缘不相交。")+Plan.Diagnostics+TEXT("确认前会重新核对地面和现有资源。");
        return FReply::Handled();
    }
    FReply Commit()
    {
        FString E;FPlacementPlan Fresh;
        if(Plan.Transforms.IsEmpty()||!CheckVolume(E)||!MakePlan(*Draft.Get(),Ground.Get(),AreaVolume.Get(),Fresh,E)||!SamePlan(Plan,Fresh))
        {ResetPlan();Status=E.IsEmpty()?TEXT("环境或参数已变化，请重新生成预览。"):E;return FReply::Handled();}
        UWorld* W=Fresh.World.Get();ULevel* Level=Fresh.Level.Get();UClass* Class=Fresh.Class.Get();
        if(!WritableCurrentLevel(W,Level)){ResetPlan();Status=TEXT("预览关卡已切换、锁定或卸载；请重新预览。");return FReply::Handled();}
        if(!HasAuditedConstruction(Class))
        {Status=TEXT("此蓝图未通过当前版本的构造审计，或有未保存修改；禁止落地。请保存并联系工程维护者重新审查。预览不代表审查通过。");return FReply::Handled();}
        TStrongObjectPtr<UDWResourcePlacementRecipe> FrozenRecipe(DuplicateObject<UDWResourcePlacementRecipe>(Fresh.Recipe.Get(),GetTransientPackage()));
        TArray<TStrongObjectPtr<UObject>> HeldAssets;
        if(!FrozenRecipe.IsValid()||!HoldRecipeAssets(FrozenRecipe.Get(),HeldAssets)||!WritableCurrentLevel(W,Level))
        {Status=TEXT("配方或当前关卡已失效；请重新预览。");return FReply::Handled();}
        TSet<FName> ExistingIds;
        for(TActorIterator<ADWGatheringInteractable> It(W);It;++It)if(!It->PersistentId.IsNone())ExistingIds.Add(It->PersistentId);
        FScopedTransaction Transaction(FText::FromString(TEXT("批量放置 Dough World 资源")));
        Level->Modify();
        const FName Group(*(GroupPrefix+FGuid::NewGuid().ToString(EGuidFormats::Digits)));
        TArray<TWeakObjectPtr<AActor>> Created;
        for(const FTransform& Transform:Fresh.Transforms)
        {
            if(!WritableCurrentLevel(W,Level)){Status=TEXT("关卡在生成过程中变化；事务保留，请 Ctrl+Z 检查现场。");ResetPlan();return FReply::Handled();}
            auto* Actor=Cast<ADWGatheringInteractable>(GEditor->AddActor(Level,Class,Transform,true,RF_Transactional,false));
            if(Actor)
            {
                Created.Add(Actor);Actor->Modify();
                FName Id;do{Id=FName(*(TEXT("DWResource_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));}while(ExistingIds.Contains(Id));
                ExistingIds.Add(Id);Actor->PersistentId=Id;Actor->bRuntimeSpawned=false;Actor->Tags.AddUnique(Group);
                TWeakObjectPtr<ADWGatheringInteractable> WeakActor=Actor;
                const auto Check=[&](){auto* A=WeakActor.Get();return WritableCurrentLevel(W,Level)&&IsValid(A)&&!A->IsActorBeingDestroyed()&&A->GetWorld()==W&&A->GetLevel()==Level&&A->GetClass()==Class&&A->PersistentId==Id&&IsValid(A->Mesh);};
                if(!ApplyRecipe(Actor,FrozenRecipe.Get(),false,Check)||!ApplyNewBudget(Actor,*Draft.Get(),Check))Actor=nullptr;
            }
            if(!Actor)
            {
                bool RemovedAll=true;
                for(const TWeakObjectPtr<AActor>& CreatedActor:Created)
                    if(CreatedActor.IsValid()&&!W->EditorDestroyActor(CreatedActor.Get(),true))RemovedAll=false;
                if(RemovedAll)Transaction.Cancel();
                ResetPlan();
                Status=RemovedAll?TEXT("生成失败，已移除本次新建对象；请检查关卡可写状态。"):
                    TEXT("生成失败，部分新对象未能自动移除；事务保留，请先撤销并检查现场，不要保存。");
                return FReply::Handled();
            }
        }
        W->MarkPackageDirty();ResetPlan();
        Status=FString::Printf(TEXT("已放置 %d 个对象，批次 %s。Ctrl+Z 可撤销本批，保存关卡前请检查；当前地图的完整存读仍须先完成正式接入。"),Created.Num(),*Group.ToString());
        return FReply::Handled();
    }
    FReply SelectGroup()
    {
        FString E;UWorld* W=EditorWorld(E);AActor* A=OneSelected();
        if(!W||!A||A->GetWorld()!=W){Status=TEXT("请先选中本工具生成的一个资源。");return FReply::Handled();}
        TArray<FName> Groups;for(const FName GroupTag:A->Tags)if(GroupTag.ToString().StartsWith(GroupPrefix))Groups.Add(GroupTag);
        if(Groups.Num()!=1){Status=TEXT("该对象没有唯一的工具批次标记；未改变选择。");return FReply::Handled();}
        GEditor->SelectNone(false,true,false);int32 N=0;
        for(TActorIterator<ADWGatheringInteractable> It(W);It;++It)if(It->Tags.Contains(Groups[0])){GEditor->SelectActor(*It,true,false);++N;}
        GEditor->NoteSelectionChange();Status=FString::Printf(TEXT("已选择该批次 %d 个资源；没有删除或修改对象。"),N);return FReply::Handled();
    }
    FReply ApplyRecipeToSelection()
    {
        FString E,Fingerprint;UWorld* W=EditorWorld(E);if(!W){Status=E;return FReply::Handled();}TWeakObjectPtr<UWorld> InitialWorld=W;
        UClass* Class=Draft->ResourceClass.LoadSynchronous();TStrongObjectPtr<UClass> HeldClass(Class);
        UDWResourcePlacementRecipe* Recipe=Draft->Recipe.LoadSynchronous();TStrongObjectPtr<UDWResourcePlacementRecipe> HeldRecipe(Recipe);
        if(!InitialWorld.IsValid()||EditorWorld(E)!=InitialWorld.Get()||!ValidateRecipe(HeldRecipe.Get(),HeldClass.Get(),Fingerprint,E)||!InitialWorld.IsValid()) {Status=E.IsEmpty()?TEXT("编辑器世界发生变化。"):E;return FReply::Handled();}
        if(!FMath::IsFinite(Draft->HiddenPromptPollSeconds)||Draft->HiddenPromptPollSeconds<0.f||Draft->HiddenPromptPollSeconds>.5f)
        {Status=TEXT("隐藏检查间隔必须为 0–0.5 秒。");return FReply::Handled();}
        ULevel* Level=W->GetCurrentLevel();TWeakObjectPtr<UWorld> WeakWorld=W;TWeakObjectPtr<ULevel> WeakLevel=Level;
        TStrongObjectPtr<UDWResourcePlacementRecipe> FrozenRecipe(DuplicateObject<UDWResourcePlacementRecipe>(Recipe,GetTransientPackage()));
        TStrongObjectPtr<UDWResourcePlacementSettings> FrozenSettings(DuplicateObject<UDWResourcePlacementSettings>(Draft.Get(),GetTransientPackage()));
        TArray<TStrongObjectPtr<UObject>> HeldAssets;TArray<FSelectedResourceSnapshot> Actors;
        if(!FrozenRecipe.IsValid()||!FrozenSettings.IsValid()||!HoldRecipeAssets(FrozenRecipe.Get(),HeldAssets)||!CaptureSelection(W,Level,Class,true,Actors))
        {Status=TEXT("仅允许选择当前可编辑关卡内 1–500 个同类型、有 ID 的工具资源。");return FReply::Handled();}
        const FText Confirm=FText::FromString(FString::Printf(TEXT("预览：为 %d 个所选对象更新网格、材质、网格局部变换、绘制距离、阴影、随机外观和采集表现/掉落物模型；会覆盖这些手调项。%s 不主动更改位置、ID、耐久、拾取数量和掉落规则。旧存档可恢复旧 Actor 和网格局部变换，拾取物还可能恢复旧网格/材质；本操作不会迁移存档。继续？可 Ctrl+Z 撤销。"),Actors.Num(),FrozenSettings->bApplyBudgetToExisting?TEXT("已勾选：同时更新所选旧对象的性能预算。 "):TEXT("旧对象的性能预算不变。 ")));
        if(FMessageDialog::Open(EAppMsgType::YesNo,Confirm)!=EAppReturnType::Yes){Status=TEXT("已取消；对象未修改。");return FReply::Handled();}
        FString Recheck;
        if(!WeakWorld.IsValid()||!WeakLevel.IsValid()||EditorWorld(E)!=WeakWorld.Get()||!ValidateRecipe(HeldRecipe.Get(),HeldClass.Get(),Recheck,E)||Recheck!=Fingerprint||
           Draft->Recipe.Get()!=HeldRecipe.Get()||Draft->ResourceClass.Get()!=HeldClass.Get()||Draft->bApplyBudgetToExisting!=FrozenSettings->bApplyBudgetToExisting||
           Draft->bEnablePromptBudget!=FrozenSettings->bEnablePromptBudget||Draft->bEnablePickupOutlineBudget!=FrozenSettings->bEnablePickupOutlineBudget||Draft->HiddenPromptPollSeconds!=FrozenSettings->HiddenPromptPollSeconds||
           !RecheckSelection(Actors,WeakWorld.Get(),WeakLevel.Get())){Status=TEXT("关卡、对象、配方或设置发生变化；未修改，请重新选择。");return FReply::Handled();}
        FScopedTransaction Transaction(FText::FromString(TEXT("所选工具资源应用外观配方")));
        for(const auto& S:Actors)
        {
            const auto Check=[&](){return ResourceStillMatches(S,WeakWorld.Get(),WeakLevel.Get());};
            auto* A=S.Actor.Get();
            if(!Check()||!ApplyRecipe(A,FrozenRecipe.Get(),true,Check)||!Check())
            {Status=TEXT("应用中断；事务保留，请立即 Ctrl+Z 并检查现场。");return FReply::Handled();}
            if(FrozenSettings->bApplyBudgetToExisting&&!ApplyNewBudget(A,*FrozenSettings.Get(),Check))
            {Status=TEXT("性能设置中断；事务保留，请立即 Ctrl+Z 并检查现场。");return FReply::Handled();}
        }
        if(!WritableCurrentLevel(WeakWorld.Get(),WeakLevel.Get())){Status=TEXT("关卡已变化；事务保留，请 Ctrl+Z 检查。");return FReply::Handled();}
        WeakWorld->MarkPackageDirty();ResetPlan();Status=FString::Printf(TEXT("已更新 %d 个所选资源；旧档仍可能恢复旧变换和拾取物外观。请检查，可 Ctrl+Z 撤销。"),Actors.Num());
        return FReply::Handled();
    }
    FReply ApplyRenderTuning()
    {
        FString E,Fingerprint;UWorld* W=EditorWorld(E);if(!W){Status=E;return FReply::Handled();}TWeakObjectPtr<UWorld> WeakWorld=W;
        UClass* Class=Draft->ResourceClass.LoadSynchronous();TStrongObjectPtr<UClass> HeldClass(Class);
        auto* Recipe=Draft->Recipe.LoadSynchronous();TStrongObjectPtr<UDWResourcePlacementRecipe> HeldRecipe(Recipe);
        if(!WeakWorld.IsValid()||EditorWorld(E)!=WeakWorld.Get()||!ValidateRecipe(HeldRecipe.Get(),HeldClass.Get(),Fingerprint,E)||!WeakWorld.IsValid()){Status=E;return FReply::Handled();}
        if(!FMath::IsFinite(Draft->HiddenPromptPollSeconds)||Draft->HiddenPromptPollSeconds<0.f||Draft->HiddenPromptPollSeconds>.5f)
        {Status=TEXT("隐藏检查间隔必须为 0–0.5 秒。");return FReply::Handled();}
        ULevel* Level=WeakWorld->GetCurrentLevel();TWeakObjectPtr<ULevel> WeakLevel=Level;
        TStrongObjectPtr<UDWResourcePlacementRecipe> FrozenRecipe(DuplicateObject<UDWResourcePlacementRecipe>(Recipe,GetTransientPackage()));
        TStrongObjectPtr<UDWResourcePlacementSettings> FrozenSettings(DuplicateObject<UDWResourcePlacementSettings>(Draft.Get(),GetTransientPackage()));
        TArray<TStrongObjectPtr<UObject>> HeldAssets;TArray<FSelectedResourceSnapshot> Actors;
        if(!FrozenRecipe.IsValid()||!FrozenSettings.IsValid()||!HoldRecipeAssets(FrozenRecipe.Get(),HeldAssets)||!CaptureSelection(WeakWorld.Get(),WeakLevel.Get(),HeldClass.Get(),true,Actors))
        {Status=TEXT("仅允许选择当前可编辑关卡内 1–500 个同类型、有 ID 的工具资源。");return FReply::Handled();}
        const FText Confirm=FText::FromString(FString::Printf(TEXT("仅更新 %d 个资源的组件绘制距离 %.1f cm 与阴影 %s；0 表示本组件无距离上限，仍受其他裁剪规则影响。%s 不覆盖模型、材质、位置或采集规则。旧档仍可能恢复旧变换及拾取物外观。确认后可 Ctrl+Z 撤销。"),Actors.Num(),FrozenRecipe->MaxDrawDistanceCm,FrozenRecipe->bCastShadow?TEXT("开"):TEXT("关"),FrozenSettings->bApplyBudgetToExisting?TEXT("已勾选同时修改所选对象的性能预算。 "):TEXT("旧对象性能预算不变。 ")));
        if(FMessageDialog::Open(EAppMsgType::YesNo,Confirm)!=EAppReturnType::Yes){Status=TEXT("已取消；未修改任何资源。");return FReply::Handled();}
        FString Recheck;if(!WeakWorld.IsValid()||!WeakLevel.IsValid()||EditorWorld(E)!=WeakWorld.Get()||!ValidateRecipe(HeldRecipe.Get(),HeldClass.Get(),Recheck,E)||Recheck!=Fingerprint||
            Draft->Recipe.Get()!=HeldRecipe.Get()||Draft->ResourceClass.Get()!=HeldClass.Get()||Draft->bApplyBudgetToExisting!=FrozenSettings->bApplyBudgetToExisting||
            Draft->bEnablePromptBudget!=FrozenSettings->bEnablePromptBudget||Draft->bEnablePickupOutlineBudget!=FrozenSettings->bEnablePickupOutlineBudget||Draft->HiddenPromptPollSeconds!=FrozenSettings->HiddenPromptPollSeconds||
            !RecheckSelection(Actors,WeakWorld.Get(),WeakLevel.Get())){Status=TEXT("关卡、对象、配方或设置发生变化；未修改，请重新选择。");return FReply::Handled();}
        FScopedTransaction Transaction(FText::FromString(TEXT("所选资源更新绘制距离和阴影")));
        for(const auto& S:Actors)
        {
            const auto Check=[&](){return ResourceStillMatches(S,WeakWorld.Get(),WeakLevel.Get());};auto* A=S.Actor.Get();
            if(!Check()){Status=TEXT("对象发生变化；事务保留，请 Ctrl+Z 检查。");return FReply::Handled();}
            A->Modify();if(!Check()){Status=TEXT("对象发生变化；事务保留，请 Ctrl+Z 检查。");return FReply::Handled();}
            A->Mesh->Modify();if(!Check()){Status=TEXT("对象发生变化；事务保留，请 Ctrl+Z 检查。");return FReply::Handled();}
            A->Mesh->SetCullDistance(FrozenRecipe->MaxDrawDistanceCm);if(!Check()){Status=TEXT("对象发生变化；事务保留，请 Ctrl+Z 检查。");return FReply::Handled();}
            A->Mesh->SetCastShadow(FrozenRecipe->bCastShadow);if(!Check()||(FrozenSettings->bApplyBudgetToExisting&&!ApplyNewBudget(A,*FrozenSettings.Get(),Check)))
            {Status=TEXT("对象发生变化；事务保留，请 Ctrl+Z 检查。");return FReply::Handled();}
        }
        if(!WritableCurrentLevel(WeakWorld.Get(),WeakLevel.Get())){Status=TEXT("关卡已变化；事务保留，请 Ctrl+Z 检查。");return FReply::Handled();}
        WeakWorld->MarkPackageDirty();Status=FString::Printf(TEXT("已更新 %d 个资源绘制距离和阴影；请检查，可 Ctrl+Z 撤销。"),Actors.Num());
        return FReply::Handled();
    }
    FReply ReprojectSelected()
    {
        FString E;UWorld* W=EditorWorld(E);AActor* Surface=Ground.Get();
        if(!W||!IsValid(Surface)||Surface->GetWorld()!=W){Status=TEXT("先使用“使用选中地面”指定地面，再选择要调整的资源。");return FReply::Handled();}
        if(!CheckVolume(E)){Status=E;return FReply::Handled();}
        if(!std::isfinite(Draft->GroundOffsetCm)||FMath::Abs(Draft->GroundOffsetCm)>10000.||Draft->Extent.ContainsNaN()||Draft->Extent.Z<=0.||Draft->Extent.Z>1000000.)
        {Status=TEXT("竖直偏移或区域 Z 半尺寸无效；未修改对象。");return FReply::Handled();}
        TWeakObjectPtr<UWorld> WeakWorld=W;TWeakObjectPtr<ULevel> WeakLevel=W->GetCurrentLevel();TWeakObjectPtr<AActor> WeakSurface=Surface;
        const FTransform SurfaceTransform=Surface->GetActorTransform();const double Offset=Draft->GroundOffsetCm,HalfProbe=FMath::Max(10000.,Draft->Extent.Z);
        TArray<FSelectedResourceSnapshot> Actors;
        if(!CaptureSelection(WeakWorld.Get(),WeakLevel.Get(),nullptr,false,Actors))
        {Status=TEXT("请选择当前可编辑关卡内 1–500 个已审计、有 ID 的资源。");return FReply::Handled();}
        auto Project=[&](TArray<FVector>& Out)->bool
        {
            UWorld* Current=WeakWorld.Get();AActor* GroundActor=WeakSurface.Get();
            if(!WritableCurrentLevel(Current,WeakLevel.Get())||!IsValid(GroundActor)||Ground.Get()!=GroundActor||GroundActor->GetWorld()!=Current||
               !GroundActor->GetActorTransform().Equals(SurfaceTransform,.001)||Draft->GroundOffsetCm!=Offset||FMath::Max(10000.,Draft->Extent.Z)!=HalfProbe||!CheckVolume(E)||!RecheckSelection(Actors,Current,WeakLevel.Get()))return false;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(DWPlacementReproject),true);
            if(AreaVolume.IsValid()&&AreaVolume->GetWorld()==Current&&AreaVolume.Get()!=GroundActor)Query.AddIgnoredActor(AreaVolume.Get());
            for(TActorIterator<ADWGatheringInteractable> It(Current);It;++It)Query.AddIgnoredActor(*It);
            for(TActorIterator<ADWGatheringPond> It(Current);It;++It)Query.AddIgnoredActor(*It);
            Out.Reset();for(const auto& S:Actors)
            {
                const FVector P=S.Transform.GetLocation();FHitResult Hit;
                if(P.ContainsNaN()||!Current->LineTraceSingleByChannel(Hit,P+FVector(0,0,HalfProbe),P-FVector(0,0,HalfProbe),ECC_Visibility,Query)||
                   !IsChosenGround(GroundActor,Hit.GetActor())||Hit.ImpactPoint.ContainsNaN())return false;
                Out.Add(FVector(P.X,P.Y,Hit.ImpactPoint.Z+Offset));
            }
            return true;
        };
        TArray<FVector> Destinations;if(!Project(Destinations)){Status=TEXT("地面或资源无效，未移动任何对象。");return FReply::Handled();}
        FString ReprojectPreview;
        for(int32 I=0;I<FMath::Min(3,Actors.Num());++I)
            ReprojectPreview+=FString::Printf(TEXT("\n%s：Z %.1f → %.1f cm"),*GetNameSafe(Actors[I].Actor.Get()),Actors[I].Transform.GetLocation().Z,Destinations[I].Z);
        const FText Confirm=FText::FromString(FString::Printf(TEXT("预览 %d 个所选资源，以下显示前 3 个：%s\n竖直偏移 %.1f cm；保留 XY、旋转、缩放、ID 和批次。旧存档可能恢复旧 Actor/网格局部变换，拾取物还可能恢复旧网格/材质；本操作不迁移存档。确认后可 Ctrl+Z 撤销。"),Actors.Num(),*ReprojectPreview,Offset));
        if(FMessageDialog::Open(EAppMsgType::YesNo,Confirm)!=EAppReturnType::Yes){Status=TEXT("已取消；对象未移动。");return FReply::Handled();}
        TArray<FVector> FreshDestinations;
        if(!Project(FreshDestinations)||FreshDestinations.Num()!=Destinations.Num()){Status=TEXT("地面或对象已变化；未移动，请重新预览。");return FReply::Handled();}
        for(int32 I=0;I<Destinations.Num();++I)if(!Destinations[I].Equals(FreshDestinations[I],.01)){Status=TEXT("地面投射结果变化；未移动，请重新预览。");return FReply::Handled();}
        FScopedTransaction Transaction(FText::FromString(TEXT("选中采集资源重贴地")));
        for(int32 I=0;I<Actors.Num();++I)
        {
            UWorld* Current=WeakWorld.Get();AActor* CurrentSurface=WeakSurface.Get();
            if(!WritableCurrentLevel(Current,WeakLevel.Get())||!IsValid(CurrentSurface)||Ground.Get()!=CurrentSurface||CurrentSurface->GetWorld()!=Current||
               !CurrentSurface->GetActorTransform().Equals(SurfaceTransform,.001)||Draft->GroundOffsetCm!=Offset||FMath::Max(10000.,Draft->Extent.Z)!=HalfProbe||!CheckVolume(E))
            {Status=TEXT("地面或关卡已变化；事务保留，请立即 Ctrl+Z 检查。");return FReply::Handled();}
            FCollisionQueryParams CurrentQuery(SCENE_QUERY_STAT(DWPlacementReproject),true);
            if(AreaVolume.IsValid()&&AreaVolume->GetWorld()==Current&&AreaVolume.Get()!=CurrentSurface)CurrentQuery.AddIgnoredActor(AreaVolume.Get());
            for(TActorIterator<ADWGatheringInteractable> It(Current);It;++It)CurrentQuery.AddIgnoredActor(*It);
            for(TActorIterator<ADWGatheringPond> It(Current);It;++It)CurrentQuery.AddIgnoredActor(*It);
            const FVector Start=Actors[I].Transform.GetLocation();FHitResult CurrentHit;
            if(!Current->LineTraceSingleByChannel(CurrentHit,Start+FVector(0,0,HalfProbe),Start-FVector(0,0,HalfProbe),ECC_Visibility,CurrentQuery)||
               !IsChosenGround(CurrentSurface,CurrentHit.GetActor())||CurrentHit.ImpactPoint.ContainsNaN()||
               !FVector(Start.X,Start.Y,CurrentHit.ImpactPoint.Z+Offset).Equals(FreshDestinations[I],.01))
            {Status=TEXT("地面投射发生变化；事务保留，请立即 Ctrl+Z 检查。");return FReply::Handled();}
            const auto& S=Actors[I];auto* A=S.Actor.Get();if(!ResourceStillMatches(S,WeakWorld.Get(),WeakLevel.Get())){Status=TEXT("对象已变化；事务保留，请立即 Ctrl+Z 检查。");return FReply::Handled();}
            A->Modify();if(!ResourceStillMatches(S,WeakWorld.Get(),WeakLevel.Get())){Status=TEXT("对象已变化；事务保留，请立即 Ctrl+Z 检查。");return FReply::Handled();}
            Current=WeakWorld.Get();CurrentSurface=WeakSurface.Get();FHitResult AfterModify;
            if(!IsValid(CurrentSurface)||Ground.Get()!=CurrentSurface||CurrentSurface->GetWorld()!=Current||!CurrentSurface->GetActorTransform().Equals(SurfaceTransform,.001)||
               Draft->GroundOffsetCm!=Offset||FMath::Max(10000.,Draft->Extent.Z)!=HalfProbe||!CheckVolume(E)||
               !Current->LineTraceSingleByChannel(AfterModify,Start+FVector(0,0,HalfProbe),Start-FVector(0,0,HalfProbe),ECC_Visibility,CurrentQuery)||
               !IsChosenGround(CurrentSurface,AfterModify.GetActor())||AfterModify.ImpactPoint.ContainsNaN()||
               !FVector(Start.X,Start.Y,AfterModify.ImpactPoint.Z+Offset).Equals(FreshDestinations[I],.01))
            {Status=TEXT("地面在操作时变化；事务保留，请立即 Ctrl+Z 检查。");return FReply::Handled();}
            if(!A->SetActorLocation(FreshDestinations[I],false,nullptr,ETeleportType::TeleportPhysics))
            {Status=TEXT("重贴地中断；事务保留，请立即 Ctrl+Z 并检查现场。");return FReply::Handled();}
            if(!ResourceStillMatches(S,WeakWorld.Get(),WeakLevel.Get(),false)||!S.Actor->GetActorRotation().Equals(S.Transform.Rotator(),.01)||
               !S.Actor->GetActorScale3D().Equals(S.Transform.GetScale3D(),.001)||!S.Actor->GetActorLocation().Equals(FreshDestinations[I],.01))
            {Status=TEXT("对象身份或变换发生预期外变化；请立即 Ctrl+Z 并检查现场。");return FReply::Handled();}
        }
        if(!WritableCurrentLevel(WeakWorld.Get(),WeakLevel.Get())){Status=TEXT("关卡已变化；事务保留，请 Ctrl+Z 检查。");return FReply::Handled();}
        WeakWorld->MarkPackageDirty();ResetPlan();Status=FString::Printf(TEXT("已按地面调整 %d 个资源 Z；旧档可能恢复旧位置和外观。请检查；Ctrl+Z 可撤销。"),Actors.Num());
        return FReply::Handled();
    }
    FReply ReadPond()
    {
        FString E;UWorld* W=EditorWorld(E);auto* Pond=Cast<ADWGatheringPond>(OneSelected());
        if(!W||!Pond||Pond->GetWorld()!=W||!Pond->bUseSplineBoundary||Pond->GetActorScale3D().ContainsNaN()||
           Pond->GetActorScale3D().GetMin()<=0.||Pond->GetActorUpVector().Z<=.999)
        {Status=TEXT("请选有效样条水塘；各轴缩放须为正，且水塘应保持水平。");return FReply::Handled();}
        DepthCm=(Pond->WaterHeightCm-Pond->BedHeightCm)*Pond->GetActorScale3D().Z*Pond->GetActorUpVector().Z;
        Status=FString::Printf(TEXT("当前实际几何水深 %.2f cm；水面和外缘保持原值。调整数值后点击应用。"),DepthCm);
        return FReply::Handled();
    }
    FReply AssignPondId()
    {
        FString E;UWorld* W=EditorWorld(E);auto* Pond=Cast<ADWGatheringPond>(OneSelected());
        if(!W||!Pond||Pond->GetWorld()!=W||!Pond->PersistentId.IsNone()||Pond->bAuthoredPersistence)
        {Status=TEXT("仅可登记恰好一个尚无 ID 的新水塘；已有身份绝不覆盖。");return FReply::Handled();}
        const FText Q=FText::FromString(TEXT("仅为从未写入任何存档的新水塘登记持久 ID。已有旧水塘/旧档不得自行生成新身份；确认这是新水塘吗？"));
        if(FMessageDialog::Open(EAppMsgType::YesNo,Q)!=EAppReturnType::Yes){Status=TEXT("已取消；水塘身份未改变。");return FReply::Handled();}
        TSet<FName> Used;
        for(TActorIterator<ADWGatheringInteractable> It(W);It;++It)if(!It->PersistentId.IsNone())Used.Add(It->PersistentId);
        for(TActorIterator<ADWGatheringPond> It(W);It;++It)if(!It->PersistentId.IsNone())Used.Add(It->PersistentId);
        FName Id;do{Id=FName(*(TEXT("DWPond_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));}while(Used.Contains(Id));
        FScopedTransaction Transaction(FText::FromString(TEXT("登记新水塘身份")));
        Pond->Modify();Pond->PersistentId=Id;Pond->bAuthoredPersistence=true;Pond->MarkPackageDirty();
        Status=TEXT("已登记新水塘 ID：")+Id.ToString()+TEXT("；Ctrl+Z 可撤销。地图配置和完整存读仍须单独接入验证。");
        return FReply::Handled();
    }
    FReply ApplyPond()
    {
        FString E;UWorld* W=EditorWorld(E);auto* Pond=Cast<ADWGatheringPond>(OneSelected());
        if(!W||!Pond||Pond->GetWorld()!=W||!Pond->bUseSplineBoundary||!std::isfinite(DepthCm)||DepthCm<=0.||
           Pond->GetActorScale3D().ContainsNaN()||Pond->GetActorScale3D().GetMin()<=0.||
           Pond->GetActorUpVector().Z<=.999||!Pond->bBoundaryValid)
        {Status=TEXT("请选择一个边界有效的样条水塘，水深和 Z 缩放必须为正。");return FReply::Handled();}
        double Bed=0.;
        if(!DWResourcePlacementMath::TryBedHeight(Pond->WaterHeightCm,Pond->OuterGroundHeightCm,
           Pond->BankCrestAboveWaterCm,Pond->GetActorScale3D().Z*Pond->GetActorUpVector().Z,DepthCm,Bed))
        {Status=TEXT("超出水塘几何有效范围；没有修改。请检查深度、外缘与岸顶高度。");return FReply::Handled();}
        FString RebuildError;
        if(!Pond->CanRebuildBedInEditor(Pond->BedHeightCm,RebuildError)||!Pond->CanRebuildBedInEditor(Bed,RebuildError))
        {Status=TEXT("未修改：水塘完整预校验失败：")+RebuildError;return FReply::Handled();}
        FScopedTransaction Transaction(FText::FromString(TEXT("调整水塘深度")));
        Pond->Modify();
        if(!Pond->TrySetBedHeightInEditor(Bed,RebuildError))
        {Transaction.Cancel();Status=TEXT("水深未应用：")+RebuildError;return FReply::Handled();}
        Pond->MarkPackageDirty();
        Status=FString::Printf(TEXT("水深 %.2f cm；仅更新塘底。水面高度、外缘、控制点和材质未改。地形不会被挖掉；Ctrl+Z 可撤销。"),DepthCm);
        return FReply::Handled();
    }
    FReply AuditIds()
    {
        FString E;UWorld* W=EditorWorld(E);
        if(!W){Status=E;return FReply::Handled();}
        if(!AuditResourceIdentities(W,E)){Status=E;return FReply::Handled();}
        Status=TEXT("当前已加载采集 Actor 的 ID 非空且不重复；仍需单独检查未加载分区、水塘身份、旧档 path/class 和完整存档能力。");
        return FReply::Handled();
    }
    FReply RepairDuplicatedId()
    {
        FString E;UWorld* W=EditorWorld(E);auto* Chosen=Cast<ADWGatheringInteractable>(OneSelected());
        if(!W||!Chosen||Chosen->GetWorld()!=W||Chosen->PersistentId.IsNone())
        {Status=TEXT("请选择一个有重复 ID 的新复制资源；空 ID 和未明确身份的旧对象不自动更改。");return FReply::Handled();}
        bool IsToolMember=false;
        for(const FName GroupTag:Chosen->Tags)if(GroupTag.ToString().StartsWith(GroupPrefix))IsToolMember=true;
        int32 Matches=0;TSet<FName> Ids;
        for(TActorIterator<ADWGatheringInteractable> It(W);It;++It)
        {if(It->PersistentId==Chosen->PersistentId)++Matches;if(!It->PersistentId.IsNone())Ids.Add(It->PersistentId);}
        if(!IsToolMember||Matches<2)
        {Status=TEXT("只允许修复带工具批次标签且确实重复的选中副本；旧对象及其他情况不自动改 ID。");return FReply::Handled();}
        const FText Question=FText::FromString(TEXT("只给当前选中的对象生成新 ID，其他对象不变。仅当它是刚复制、从未写入任何存档的新对象时才可继续；否则可能导致旧存档无法匹配。确认该对象确实是新副本吗？"));
        if(FMessageDialog::Open(EAppMsgType::YesNo,Question)!=EAppReturnType::Yes){Status=TEXT("已取消，所有 ID 未改变。");return FReply::Handled();}
        FName NewId;do{NewId=FName(*(TEXT("DWResource_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));}while(Ids.Contains(NewId));
        const FName OldId=Chosen->PersistentId;
        FScopedTransaction Transaction(FText::FromString(TEXT("修复新复制资源的 ID")));
        Chosen->Modify();Chosen->PersistentId=NewId;Chosen->MarkPackageDirty();ResetPlan();
        Status=FString::Printf(TEXT("仅修改选中副本 ID：%s → %s。Ctrl+Z 可撤销；尚不代表任意地图存读已支持。"),*OldId.ToString(),*NewId.ToString());
        return FReply::Handled();
    }
    TStrongObjectPtr<UDWResourcePlacementSettings> Draft;
    TWeakObjectPtr<AActor> Ground;
    TWeakObjectPtr<AActor> BeforeGround;
    TWeakObjectPtr<AVolume> AreaVolume,BeforeVolume;
    FVector BeforeCenter=FVector::ZeroVector,BeforeExtent=FVector::ZeroVector;
    bool bCanUndoRegion=false;
    bool bAreaWasRead=false,bBeforeAreaWasRead=false;
    FTransform AreaTransform=FTransform::Identity,BeforeAreaTransform=FTransform::Identity;
    FVector AreaBoundsCenter=FVector::ZeroVector,AreaBoundsExtent=FVector::ZeroVector;
    FVector BeforeAreaBoundsCenter=FVector::ZeroVector,BeforeAreaBoundsExtent=FVector::ZeroVector;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SDWPlacementPreview> Preview;
    FPlacementPlan Plan;
    double DepthCm=50.;
    FString Status=TEXT("请先选择地面。务必保护已有地图和旧存档。");
};
}

void RegisterDWResourcePlacementTool()
{
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName,FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
    {return SNew(SDockTab).TabRole(ETabRole::NomadTab)[SNew(SDWResourcePlacementPanel)];}))
    .SetDisplayName(FText::FromString(TEXT("资源批量摆放")));
}
void UnregisterDWResourcePlacementTool(){FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);}
void RegisterDWResourcePlacementMenu()
{
    UToolMenu* Menu=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
    Menu->FindOrAddSection(TEXT("DoughWorldTools")).AddMenuEntry(TabName,FText::FromString(TEXT("资源批量摆放")),
        FText::FromString(TEXT("预览后批量放置已有可交互资源")),FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([](){FGlobalTabmanager::Get()->TryInvokeTab(TabName);})));
}
