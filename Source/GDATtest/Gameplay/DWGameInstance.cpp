#include "DWGameInstance.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWWorldEvent.h"
#include "DWLoadingTransition.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.h"
#include "DWLocalizationLibrary.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWSaveGame.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatWorldPersistence.h"
#include "DWCombatRecoverySubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "DWSaveSettings.h"
#include "DWSaveStorage.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void UDWGameInstance::Init()
{
 bCombatSaveAlive=true;Super::Init();GetSubsystem<UDWLoadingTransitionSubsystem>()->OnFailed.AddDynamic(this,&UDWGameInstance::HandleTravelFailure);
 if(!FDWCombatRules::LoadProjectPayload(CombatRules,CombatRulesError))
 {UE_LOG(LogTemp,Error,TEXT("DW_COMBAT_RULES_INVALID: %s"),*CombatRulesError);}
 else
 {UE_LOG(LogTemp,Display,TEXT("DW_COMBAT_RULES_NUMERIC_READY: %d base, %d derived, %d equipment bindings pending; %s"),CombatRules->GetBaseParameterCount(),CombatRules->GetDerivedParameterCount(),CombatRules->GetUnboundEquipmentCount(),*CombatRules->GetDesignVersion());}
#if WITH_DEV_AUTOMATION_TESTS
 FString Prefix;if(FParse::Value(FCommandLine::Get(),TEXT("DWVerificationSavePrefix="),Prefix)&&Prefix.StartsWith(TEXT("DW_QA_"))&&Prefix.Len()<80&&!Prefix.Contains(TEXT("/"))&&!Prefix.Contains(TEXT("\\")))VerificationSavePrefix=Prefix;
#endif
 InitializeSaveStorage();
 GetSubsystem<UDWLoadingTransitionSubsystem>()->OnFinished.AddDynamic(this,&UDWGameInstance::HandleTravelFinished);
}
void UDWGameInstance::HandleTravelFailure(FText Reason)
{
 if(bSaveTravelInFlight){PendingSave=nullptr;ActiveSlot=PreviousTravelSlot;bSaveTravelInFlight=false;}LastSaveError=Reason;
}

void UDWGameInstance::HandleTravelFinished(){bSaveTravelInFlight=false;ShowPendingCombatRestoreError();}

UDWGameplayConfig* UDWGameInstance::GetDefaultGameplayConfig()
{
    if (!Config)
    {
        if (!SoftConfigPath.IsNull()) Config = Cast<UDWGameplayConfig>(SoftConfigPath.TryLoad());
        if (!Config) Config = NewObject<UDWGameplayConfig>(this);
        Config->RefreshDefinitionsFromTables();
    }
    return Config;
}

UDWGameplayConfig* UDWGameInstance::GetConfigForMap(const FString& MapPackage)
{
    UDWGameplayConfig* Default=GetDefaultGameplayConfig();
    if(!Default)return nullptr;
    const FName Key(*UWorld::RemovePIEPrefix(MapPackage));
    const TSoftObjectPtr<UDWGameplayConfig>* Override=Default->MapConfigOverrides.Find(Key);
    if(!Override)return Default;
    if(Override->IsNull())return nullptr; // An explicitly registered but missing profile must not validate a save.
    if(auto* Cached=MapConfigs.Find(Key);Cached&&IsValid(Cached->Get()))return Cached->Get();
    UDWGameplayConfig* Loaded=Override->LoadSynchronous();
    if(!IsValid(Loaded))return nullptr;
    Loaded->RefreshDefinitionsFromTables();
    if(const auto* Gathering=Cast<UDWGatheringProfile>(Loaded))
    {
        FString Error;
        if(!Gathering->ValidateGatheringDefinitions(Error))
        {UE_LOG(LogTemp,Error,TEXT("DW gathering map profile rejected: %s"),*Error);return nullptr;}
    }
    MapConfigs.Add(Key,Loaded);
    return Loaded;
}

UDWGameplayConfig* UDWGameInstance::GetConfig()
{
    const UWorld* World=GetWorld();
    const FString Map=World?World->GetOutermost()->GetName():FString();
    if(UDWGameplayConfig* Effective=GetConfigForMap(Map))return Effective;
    // Existing callers require a non-null fallback, but save/profile admission still fails closed.
    UE_LOG(LogTemp,Error,TEXT("DW current map profile unavailable for %s; default data is read-only fallback."),*Map);
    return GetDefaultGameplayConfig();
}

bool UDWGameInstance::AreGameplayMapProfilesCompatible(const FString& FirstMap,const FString& SecondMap)
{
    UDWGameplayConfig* First=GetConfigForMap(FirstMap);
    UDWGameplayConfig* Second=GetConfigForMap(SecondMap);
    return First&&Second&&First==Second;
}

bool UDWGameInstance::ValidateCombatItemRegistration(FString& OutError)
{
    return FDWCombatItemRegistry::ValidateItems(GetConfig(),OutError);
}

bool UDWGameInstance::IsValidSlot(int32 SlotIndex)
{
    return SlotIndex >= 0 && SlotIndex < SaveSlotCount;
}

FString UDWGameInstance::MakeSlotName(int32 SlotIndex)const
{
    if(!VerificationSavePrefix.IsEmpty())return VerificationSavePrefix+FString::FromInt(SlotIndex+1);
    return FString::Printf(TEXT("DoughWorld_Final_Slot_%d"), SlotIndex + 1);
}

bool UDWGameInstance::Fail(const FText& Reason)
{
    LastSaveError = Reason;
    UE_LOG(LogTemp, Warning, TEXT("DoughWorld save: %s"), *Reason.ToString());
    return false;
}

TArray<FDWSlotSummary> UDWGameInstance::GetSlotSummaries() const
{
    TArray<FDWSlotSummary> Result;
    Result.Reserve(SaveSlotCount);
    for (int32 Index = 0; Index < SaveSlotCount; ++Index)
    {
        FDWSlotSummary Summary;
        Summary.SlotIndex = Index;
        const FString SlotName = MakeSlotName(Index);
        Summary.bExists = SlotExists(SlotName);
        if (Summary.bExists)
        {
            const UDWSaveGame* Save = Cast<UDWSaveGame>(ReadSlot(SlotName));
            if (Save)
            {
                Summary.DisplayName = Save->DisplayName;
                Summary.Timestamp = Save->Timestamp;
                Summary.MapPackage = Save->MapPackage;
            }
            else
            {
                Summary.DisplayName = DWText(this, TEXT("无法读取的存档（可删除）"), TEXT("Unreadable save (can be deleted)")).ToString();
            }
        }
        Result.Add(Summary);
    }
    return Result;
}

bool UDWGameInstance::ValidateSave(const UDWSaveGame* Save)
{
    if (!Save || (Save->Version < 1 || Save->Version > UDWSaveGame::CurrentVersion))
        return Fail(DWText(this, TEXT("存档格式无法读取或版本不兼容。"), TEXT("The save format is unreadable or its version is incompatible.")));
    if(RejectGatheringPreviewPersistence(Save->MapPackage))return false;
    if (!Save->MapPackage.StartsWith(TEXT("/Game/")) ||
        !FPackageName::IsValidLongPackageName(Save->MapPackage) ||
        !FPackageName::DoesPackageExist(Save->MapPackage) ||
        !IsAllowedGameplayMap(Save->MapPackage))
        return Fail(DWText(this, TEXT("存档地图不存在或不是当前玩法地图。"), TEXT("The saved map is missing or is not the current gameplay map.")));
    if (!FMath::IsFinite(Save->Health) || Save->Health < 0.f ||
        !FMath::IsFinite(Save->Transformation) || Save->Transformation < 0.f ||
        !FMath::IsFinite(Save->SprintAlcoholElapsed) || Save->SprintAlcoholElapsed < 0.f ||
        !FMath::IsFinite(Save->TransformationDecayElapsed) || Save->TransformationDecayElapsed < 0.f ||
        (Save->bHasPlayerTransform && !Save->PlayerTransform.IsValid()))
        return Fail(DWText(this, TEXT("存档包含无效的玩家属性或位置，已停止加载。"), TEXT("Loading stopped: the save contains invalid player attributes or a position.")));

    FString CombatError;
    if(!CombatRules||!DWCombatPlayerSave::Validate(*CombatRules,*Save,CombatError))
        return Fail(DWText(this,TEXT("战斗资源存档缺失或不兼容，已停止加载；原文件已保留。"),TEXT("Combat resource data is missing or incompatible. Loading stopped; the original file was preserved.")));
    if(!FDWCombatWorldPersistence::Validate(*CombatRules,*Save,CombatError))
        return Fail(DWText(this,TEXT("完整战斗存档缺失或不兼容，原文件已保留。"),TEXT("The complete combat save is missing or incompatible. The original file was preserved.")));
    UDWInventoryComponent* Validator = NewObject<UDWInventoryComponent>(this);
    UDWGameplayConfig* SavedConfig=GetConfigForMap(Save->MapPackage);
    if(!SavedConfig)return Fail(DWText(this,TEXT("存档地图的玩法配置不可用；原文件已保留。"),TEXT("The saved map gameplay profile is unavailable. The original save was preserved.")));
    Validator->InitializeInventory(SavedConfig);
    if(!Validator->TryApplyCombatStackPolicy(*CombatRules,CombatError))return Fail(FText::FromString(CombatError));
    if (!Validator->AreSlotsValid(Save->Inventory))
        return Fail(DWText(this, TEXT("存档背包与当前物品或容量配置不兼容；原存档已保留。"), TEXT("The saved inventory is incompatible with the current items or capacity. The original save has been preserved.")));

    TSet<FString> ActorIds;
    for (const FDWPersistedActorState& ActorState : Save->WorldActors)
    {
        if (ActorState.ActorId.IsEmpty() || ActorIds.Contains(ActorState.ActorId) ||
            !FMath::IsFinite(ActorState.Health) || ActorState.Health < 0.f ||
            (ActorState.bHasTransform&&!ActorState.Transform.IsValid()) || !FMath::IsFinite(ActorState.SpawnProgress) || ActorState.SpawnProgress<0)
            return Fail(DWText(this, TEXT("存档包含无效或重复的场景物件记录。"), TEXT("The save contains invalid or duplicate world object records.")));
        ActorIds.Add(ActorState.ActorId);
    }
    return true;
}

bool UDWGameInstance::NewGame(int32 SlotIndex, const FString& Name)
{
    if(RejectGatheringPreviewPersistence())return false;
    if(UDWWorldEventSubsystem::IsPlaying(this))return Fail(DWText(this,TEXT("请等待场景演出结束。"),TEXT("Please wait for the cinematic.")));
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return Fail(DWText(this,TEXT("请等待当前过渡完成。"),TEXT("Please wait for the current transition.")));
    LastSaveError = FText::GetEmpty();
    if (!IsValidSlot(SlotIndex)) return Fail(DWText(this, TEXT("只能使用存档槽 1 至 3。"), TEXT("Only save slots 1 through 3 are available.")));
    if (!GetWorld() || !GetSubsystem<UDWLoadingTransitionSubsystem>()) return Fail(DWText(this, TEXT("游戏世界尚未准备好。"), TEXT("The game world is not ready yet.")));
    if (SlotExists(MakeSlotName(SlotIndex)))
        return Fail(DWText(this, TEXT("该槽位已有存档，请先删除或选择空槽位。"), TEXT("This slot already contains a save. Delete it first or choose an empty slot.")));

    UDWSaveGame* Save = Cast<UDWSaveGame>(UGameplayStatics::CreateSaveGameObject(UDWSaveGame::StaticClass()));
    if (!Save) return Fail(DWText(this, TEXT("无法创建存档对象。"), TEXT("Unable to create the save object.")));
    Save->DisplayName = Name.TrimStartAndEnd();
    if (Save->DisplayName.IsEmpty()) Save->DisplayName = FText::Format(DWText(this, TEXT("冒险 {0}"), TEXT("Adventure {0}")), FText::AsNumber(SlotIndex + 1)).ToString();
    Save->Timestamp = FDateTime::UtcNow();
    Save->MapPackage = GetConfig()->GameplayMap;
    FString CombatError;
    if(!CombatRules||!DWCombatPlayerSave::MakeNew(*CombatRules,*Save,CombatError))return Fail(FText::FromString(CombatError));
    Save->bHasPlayerTransform = false;
    if(!FDWCombatWorldPersistence::MarkNewAdventure(*CombatRules,*Save,CombatError))return Fail(FText::FromString(CombatError));
    if (!ValidateSave(Save)) return false;
    if (!WriteSlot(Save, MakeSlotName(SlotIndex)))
        return Fail(DWText(this, TEXT("写入新存档失败，请检查磁盘空间与写入权限。"), TEXT("Unable to write the new save. Check disk space and write permissions.")));

    ResetCombatSaveFlow();
    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = SlotIndex;
    PendingSave = Save;
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(Save->MapPackage)){HandleTravelFailure(Transition->LastError);return false;}
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
    return true;
}

bool UDWGameInstance::LoadGameSlot(int32 SlotIndex)
{
    if(RejectGatheringPreviewPersistence())return false;
    if(UDWWorldEventSubsystem::IsPlaying(this))return Fail(DWText(this,TEXT("请等待场景演出结束。"),TEXT("Please wait for the cinematic.")));
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return Fail(DWText(this,TEXT("请等待当前过渡完成。"),TEXT("Please wait for the current transition.")));
    LastSaveError = FText::GetEmpty();
    if (!IsValidSlot(SlotIndex)) return Fail(DWText(this, TEXT("只能使用存档槽 1 至 3。"), TEXT("Only save slots 1 through 3 are available.")));
    if (!GetWorld() || !GetSubsystem<UDWLoadingTransitionSubsystem>()) return Fail(DWText(this, TEXT("游戏世界尚未准备好。"), TEXT("The game world is not ready yet.")));
    if (!SlotExists(MakeSlotName(SlotIndex)))
        return Fail(DWText(this, TEXT("该存档槽为空。"), TEXT("This save slot is empty.")));
    UDWSaveGame* Save = Cast<UDWSaveGame>(ReadSlot(MakeSlotName(SlotIndex)));
    if (!ValidateSave(Save)) return false;

    ResetCombatSaveFlow();
    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = SlotIndex;
    PendingSave = Save;
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(Save->MapPackage)){HandleTravelFailure(Transition->LastError);return false;}
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
    return true;
}

bool UDWGameInstance::DeleteGameSlot(int32 SlotIndex)
{
    // Preview slot lists must not edit formal saves. Other maps keep their old behavior.
    if(RejectGatheringPreviewPersistence())return false;
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return Fail(DWText(this,TEXT("请等待当前过渡完成。"),TEXT("Please wait for the current transition.")));
    LastSaveError = FText::GetEmpty();
    if (!IsValidSlot(SlotIndex)) return Fail(DWText(this, TEXT("只能使用存档槽 1 至 3。"), TEXT("Only save slots 1 through 3 are available.")));
    const FString SlotName = MakeSlotName(SlotIndex);
    if (!SlotExists(SlotName)) return true;
    if (!RemoveSlot(SlotName))
        return Fail(DWText(this, TEXT("删除存档失败，文件可能被占用。"), TEXT("Unable to delete the save. The file may be in use.")));
    if (ActiveSlot == SlotIndex)
    {
        ResetCombatSaveFlow();ActiveSlot = INDEX_NONE;
        PendingSave = nullptr;
    }
    return true;
}

bool UDWGameInstance::SaveCurrentGame()
{
    return SaveCombatSnapshot(nullptr,false);
}

void UDWGameInstance::ApplyPendingSave(ADWPlayerCharacter* Player)
{
    ApplyCompletePendingSave(Player);
}

void UDWGameInstance::ReturnToTitle()
{
    if(UDWWorldEventSubsystem::IsPlaying(this))return;
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return;
    const FString MenuMap = GetConfig()->MainMenuMap;
    if (!GetWorld() || !FPackageName::IsValidLongPackageName(MenuMap) || !FPackageName::DoesPackageExist(MenuMap))
    {
        Fail(DWText(this, TEXT("标题地图不存在，已留在当前关卡。"), TEXT("The title map is missing. You have remained in the current level.")));
        return;
    }
    ResetCombatSaveFlow();
    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = INDEX_NONE;
    PendingSave = nullptr;
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(MenuMap))HandleTravelFailure(Transition->LastError);
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
}

bool UDWGameInstance::IsAllowedGameplayMap(const FString& Map)
{
 auto* C=GetDefaultGameplayConfig();if(!C)return false;
 if(Map==C->GameplayMap)return true;
 if(Map==C->MainMenuMap)return false;
 for(const auto& M:C->AdditionalGameplayMaps)if(M.ToSoftObjectPath().GetLongPackageName()==Map)return true;
 if(C->MapConfigOverrides.Contains(FName(*Map)))return GetConfigForMap(Map)!=nullptr;
 return false;
}
bool UDWGameInstance::TravelToGameplayMap(TSoftObjectPtr<UWorld> Destination)
{
 const FString Map=Destination.ToSoftObjectPath().GetLongPackageName();
 if(RejectGatheringPreviewPersistence()||RejectGatheringPreviewPersistence(Map))return false;
 auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();
 if(T->IsTransitionActive())return false;
 if(!IsAllowedGameplayMap(Map)||!FPackageName::DoesPackageExist(Map))return Fail(DWText(this,TEXT("请在 Additional Gameplay Maps 中登记目标地图。"),TEXT("Register the destination in Additional Gameplay Maps first.")));
 const FString CurrentMap=GetWorld()?UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()):FString();
 if(!AreGameplayMapProfilesCompatible(CurrentMap,Map))return Fail(DWText(this,TEXT("不同资源配置的地图暂不支持携带背包互相旅行，请分别开始或加载该地图的存档。"),TEXT("Travel with inventory between different gameplay profiles is not supported. Start or load that map separately.")));
 if(!SaveCurrentGame())return false;
 auto* Save=Cast<UDWSaveGame>(ReadSlot(MakeSlotName(ActiveSlot)));if(!Save)return false;
 Save->MapPackage=Map;Save->WorldActors.Reset();Save->CompletedWorldEvents.Reset();Save->bHasPlayerTransform=false;
 for(const auto& M:Save->VisitedMaps)if(M.MapPackage==Map){Save->WorldActors=M.Actors;Save->CompletedWorldEvents=M.CompletedWorldEvents;Save->PlayerTransform=M.PlayerTransform;Save->bHasPlayerTransform=true;break;}
 if(!ValidateSave(Save))return false;
 PendingSave=Save;PreviousTravelSlot=ActiveSlot;bSaveTravelInFlight=true;
 if(!T->RequestMap(Map)){HandleTravelFailure(T->LastError);return false;}
 if(!T->IsTransitionActive())bSaveTravelInFlight=false;
 return true;
}


void UDWGameInstance::InitializeSaveStorage()
{
    bUseDocumentsStorage = !GIsEditor;
    SaveDirectory = bUseDocumentsStorage ? DWSaveStorage::DocumentsDirectory(GetDefault<UDWSaveSettings>()->DocumentsFolderName) : FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames"));
#if WITH_DEV_AUTOMATION_TESTS
    if (!VerificationSavePrefix.IsEmpty())
    {
        // QA must never import or replace a player's normal slots.
        FString TestDirectory;
        if (FParse::Value(FCommandLine::Get(), TEXT("DWSaveTestDirectory="), TestDirectory) && !FPaths::IsRelative(TestDirectory))
        { SaveDirectory = TestDirectory; bUseDocumentsStorage = true; }
        else { SaveDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames")); bUseDocumentsStorage = true; }
    }
#endif
    if (SaveDirectory.IsEmpty() || !IFileManager::Get().MakeDirectory(*SaveDirectory, true))
    { UE_LOG(LogTemp, Error, TEXT("DoughWorld: cannot create save directory: %s"), *SaveDirectory); return; }
    UE_LOG(LogTemp, Display, TEXT("DoughWorld save directory: %s"), *SaveDirectory);
    // One-time copy of slots visible to this installation. Originals are never moved/deleted.
    // The marker prevents an explicitly deleted slot reappearing from the legacy location.
    const FString Marker = SaveDirectory / TEXT(".legacy-import-v1");
    if (bUseDocumentsStorage && VerificationSavePrefix.IsEmpty() && !IFileManager::Get().FileExists(*Marker))
    {
        bool bComplete = true;
        for (int32 Index=0; Index<SaveSlotCount; ++Index)
        {
            const FString Slot = MakeSlotName(Index);
            if (!DWSaveStorage::Exists(SaveDirectory, Slot) && UGameplayStatics::DoesSaveGameExist(Slot, 0))
            {
                TArray<uint8> Bytes;
                if (!UGameplayStatics::LoadDataFromSlot(Bytes, Slot, 0) || !DWSaveStorage::WriteBytes(SaveDirectory / (Slot + TEXT(".sav")), Bytes)) bComplete = false;
            }
        }
        if (bComplete) { TArray<uint8> MarkerBytes; MarkerBytes.Add(1); DWSaveStorage::WriteBytes(Marker, MarkerBytes); }
    }
}
bool UDWGameInstance::SlotExists(const FString& Slot) const
{ return bUseDocumentsStorage ? DWSaveStorage::Exists(SaveDirectory, Slot) : UGameplayStatics::DoesSaveGameExist(Slot, 0); }
USaveGame* UDWGameInstance::ReadSlot(const FString& Slot) const
{ return bUseDocumentsStorage ? DWSaveStorage::Read(SaveDirectory, Slot) : UGameplayStatics::LoadGameFromSlot(Slot, 0); }
bool UDWGameInstance::WriteSlot(USaveGame* Save, const FString& Slot) const
{ return DWSaveStorage::Write(SaveDirectory, Slot, Save); }
bool UDWGameInstance::RemoveSlot(const FString& Slot) const
{ return bUseDocumentsStorage ? DWSaveStorage::Delete(SaveDirectory, Slot) : UGameplayStatics::DeleteGameInSlot(Slot, 0); }
