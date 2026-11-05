#include "DWGameInstance.h"
#include "DWPlayerCombatTuningSubsystem.h"
#include "DWRetiredSaveProjection.h"
#include "DWFormalLanguagePersistence.h"
#include "DWLanguageSubsystem.h"
#include "DWOpeningMapCompatibility.h"
#include "UObject/StrongObjectPtr.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWWorldEvent.h"
#include "DWLoadingTransition.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.h"
#include "DWGatheringWorldPersistence.h"
#include "DWLocalizationLibrary.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWSaveGame.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatWorldPersistence.h"
#include "DWGatheringPersistenceOptIn.h"
#include "DWCombatRecoverySubsystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "DWSaveSettings.h"
#include "DWSaveStorage.h"
#include "DWEditorMapIterationSession.h"
#include "DWCombatSandboxContext.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

void UDWGameInstance::Init()
{
 bCombatSaveAlive=true;Super::Init();GetSubsystem<UDWLoadingTransitionSubsystem>()->OnFailed.AddDynamic(this,&UDWGameInstance::HandleTravelFailure);
 if(!GetSubsystem<UDWPlayerCombatTuningSubsystem>()->ResolveCombatRules(CombatRules,CombatRulesError))
 {CombatRules.Reset();UE_LOG(LogTemp,Error,TEXT("DW_COMBAT_RULES_INVALID: %s"),*CombatRulesError);}
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
 if(bSaveTravelInFlight)
 {
  PendingSave=nullptr;ActiveSlot=PreviousTravelSlot;bSaveTravelInFlight=false;
  const auto PriorSource=PreviousTravelSource;PreviousTravelSource.Reset();
  // Restore only the previously admitted owner. A failed travel must never admit
  // an arbitrary same-version replacement that now occupies that filename.
  const bool OwnerRestored=SetAdventureFlowOwner(nullptr,PriorSource);
  FString SourceError;
  TStrongObjectPtr<UDWSaveGame> Previous(HasActiveSlot()&&OwnerRestored?ReadSlotMatchingSource(ActiveSlot,PriorSource,SourceError):nullptr);
  if(HasActiveSlot()&&(!OwnerRestored||!Previous.Get()||!ValidateSave(Previous.Get())))
   RegisterCombatRestoreFailure(GetWorld(),false,TEXT("Previous admitted slot source is unavailable/changed; its token and file were retained, no overwrite is allowed."));
  else if(!FDWFormalLanguagePersistence::ResumeAfterTravelFailure(*this,Previous.Get()))
   RegisterCombatRestoreFailure(GetWorld(),false,TEXT("Formal travel did not leave the prior live owner resumable; reload the retained slot."));
 }
 LastSaveError=Reason;
}

void UDWGameInstance::HandleTravelFinished(){bSaveTravelInFlight=false;PreviousTravelSource.Reset();ShowPendingCombatRestoreError();}

UDWGameplayConfig* UDWGameInstance::GetDefaultGameplayConfig()
{
    if (!Config)
    {
        if (!SoftConfigPath.IsNull()) Config = Cast<UDWGameplayConfig>(SoftConfigPath.TryLoad());
        if (!Config) Config = NewObject<UDWGameplayConfig>(this);
        Config->RefreshDefinitionsFromTables();
        if(const auto* Profile=UDWGameplayConfig::AsGatheringConfig(Config))
        {
            FString Error;
            if(!Profile->ValidateGatheringDefinitions(Error))
            {
                UE_LOG(LogTemp,Error,TEXT("DW default gathering profile rejected: %s"),*Error);
                bDefaultConfigRejected=true;
                // UI callers retain a non-null read-only fallback. Map/save admission fails closed.
                Config=NewObject<UDWGameplayConfig>(this);
            }
        }
    }
    return Config;
}

UDWGameplayConfig* UDWGameInstance::GetConfigForMap(const FString& MapPackage)
{
    UDWGameplayConfig* Default=GetDefaultGameplayConfig();
    if(!Default||bDefaultConfigRejected)return nullptr;
    const FString CanonicalMap=DWOpeningMapCompatibility::CanonicalPackage(UWorld::RemovePIEPrefix(MapPackage));
    // Test profiles are explicit isolated-map overrides, never the formal adventure source.
    if(CanonicalMap!=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GatheringTest")&&
       CanonicalMap!=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest"))return Default;
    const FName Key(*CanonicalMap);
    const TSoftObjectPtr<UDWGameplayConfig>* Override=nullptr;FString AliasError;
    if(!DWOpeningMapCompatibility::FindProfileOverride(*Default,CanonicalMap,Override,AliasError))
    {UE_LOG(LogTemp,Error,TEXT("DW opening profile alias rejected: %s"),*AliasError);return nullptr;}
    if(!Override)return Default;
    if(Override->IsNull())return nullptr; // An explicitly registered but missing profile must not validate a save.
    if(auto* Cached=MapConfigs.Find(Key);Cached&&IsValid(Cached->Get()))return Cached->Get();
    UDWGameplayConfig* Loaded=Override->LoadSynchronous();
    if(!IsValid(Loaded))return nullptr;
    Loaded->RefreshDefinitionsFromTables();
    if(const auto* Gathering=UDWGameplayConfig::AsGatheringConfig(Loaded))
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
    // Refresh-driven diagnostics only. No validation/projection/restore or slot writes.
    if(bEditorIterationRequired)
        UE_LOG(LogTemp,Display,TEXT("DW_PIE_SLOT_LIST initialized=%d directory=%s identity=%s slots=%d init_error=%s"),
            EditorMapIteration.IsValid()?1:0,*SaveDirectory,*GetSaveStorageIdentity(),SaveSlotCount,*SaveStorageInitializationError);
    for (int32 Index = 0; Index < SaveSlotCount; ++Index)
    {
        FDWSlotSummary Summary;
        Summary.SlotIndex = Index;
        const FString SlotName = MakeSlotName(Index);
        Summary.bExists = SlotExists(SlotName);
        if (Summary.bExists)
        {
            // Metadata only: never run compatibility projection merely to list a slot.
            const UDWSaveGame* Save=nullptr;
            if(bEditorIterationRequired)
            {
                // Same single raw read + memory decode as DWSaveStorage::Read, with stage reporting.
                // bExists stays the existence result even if read/decode fails.
                TArray<uint8> Bytes;
                const bool bRead=!SaveDirectory.IsEmpty()&&FFileHelper::LoadFileToArray(Bytes,*(SaveDirectory/(SlotName+TEXT(".sav"))));
                if(bRead)Save=Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
                UE_LOG(LogTemp,Display,TEXT("DW_PIE_SLOT_METADATA slot=%s exists=1 status=%s bytes=%d"),*SlotName,
                    bRead?(Save?TEXT("decoded_metadata"):TEXT("decode_or_type_failed")):TEXT("raw_read_failed"),Bytes.Num());
            }
            else Save=Cast<UDWSaveGame>(bUseDocumentsStorage?DWSaveStorage::Read(SaveDirectory,SlotName):UGameplayStatics::LoadGameFromSlot(SlotName,0));
            if (Save)
            {
                Summary.DisplayName = Save->DisplayName;
                Summary.Timestamp = Save->Timestamp;
                Summary.MapPackage = Save->MapPackage;
            }
            else
            {
                Summary.DisplayName = DWText(this, TEXT("无法读取存档（原文件保留）"), TEXT("Unreadable save (original file preserved)")).ToString();
            }
        }
        else if(bEditorIterationRequired)
            UE_LOG(LogTemp,Display,TEXT("DW_PIE_SLOT_METADATA slot=%s exists=0 status=%s"),*SlotName,
                EditorMapIteration.IsValid()?TEXT("absent_in_bound_directory"):TEXT("storage_initialization_failed"));
        Result.Add(Summary);
    }
    return Result;
}

bool UDWGameInstance::ValidateSave(const UDWSaveGame* Save,int32 ExpectedSlot)
{
    if (!Save || (Save->Version < 1 || Save->Version > UDWSaveGame::CurrentVersion))
        return Fail(DWText(this, TEXT("存档格式无法读取或版本不兼容。"), TEXT("The save format is unreadable or its version is incompatible.")));
    // Validate a temporary retirement projection; never mutate the source slot or file.
    TStrongObjectPtr<UDWSaveGame> RetirementCopy(DuplicateObject<UDWSaveGame>(Save,GetTransientPackage()));
    FString RetirementError;
    const auto* RetirementConfig=GetConfigForMap(Save->MapPackage);
    if(!RetirementCopy.IsValid()||!RetirementConfig||!DWRetiredSaveProjection::ApplyToDuplicate(*RetirementCopy,RetirementError,RetirementConfig->MaxInventorySlots))
        return Fail(FText::FromString(RetirementError.IsEmpty()?TEXT("Could not prepare legacy item compatibility."):RetirementError));
    Save=RetirementCopy.Get();
    FString MarkerError;
    if(!DWWorldMapData::ValidateSave(Save->WorldMapMarkers,MarkerError))return Fail(FText::FromString(MarkerError));
    FString FormalError;
    if(!FDWFormalLanguagePersistence::Validate(*this,*Save,ExpectedSlot==INDEX_NONE?ActiveSlot:ExpectedSlot,FormalError))return Fail(FText::FromString(FormalError));
    auto* DefinitionOwner=GetSubsystem<UDWLanguageSubsystem>();
    DWCharacterA01SaveBridge::FScopedFormalDefinitions DefinitionScope(DefinitionOwner&&DefinitionOwner->GetNativeModel()?&DefinitionOwner->GetNativeModel()->GetCatalog():nullptr,DefinitionOwner?DefinitionOwner->GetTrialConfig():TSharedPtr<FJsonObject>());
    if(!ValidateAdventureFlow(*Save))return Fail(DWText(this,TEXT("开场进度字段不兼容；原文件保持。"),TEXT("Opening progress is incompatible; the original file is preserved.")));
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
    if(Save->CombatWorld.Gathering.Schema==1||Save->CombatWorld.Gathering.Schema==2)
    {
        auto* GatheringProfile=UDWGameplayConfig::AsGatheringConfig(SavedConfig);
        if(!GatheringProfile||!FDWGatheringWorldPersistence::ValidateForConfig(*GatheringProfile,Save->CombatWorld.Gathering,Save->Inventory,CombatError))return Fail(FText::FromString(CombatError));
    }
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
    FString StorageError;if(!CheckSaveStorageContext(StorageError))return Fail(FText::FromString(StorageError));
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
    Save->Version=UDWSaveGame::CurrentVersion; // Only a genuinely new formal slot chooses v5.
    Save->Timestamp = FDateTime::UtcNow();
    Save->MapPackage = GetConfig()->GameplayMap;
    FString CombatError;
    if(!CombatRules)return Fail(FText::Format(DWText(this,
        TEXT("战斗数值尚未通过校验，无法新建存档。请修正战斗配置后重新开始：{0}"),
        TEXT("Combat settings are not valid, so a new save cannot be created. Correct the combat settings and start again: {0}")),
        FText::FromString(CombatRulesError.IsEmpty()?TEXT("战斗配置未初始化 / Combat configuration is not initialized"):CombatRulesError)));
    if(!ConfigureOpeningNewGame(*Save,CombatError))return Fail(FText::FromString(CombatError));
    if(!DWCombatPlayerSave::MakeNewFullSurvival(*CombatRules,*Save,CombatError))return Fail(FText::FromString(CombatError));
    Save->bHasPlayerTransform = false;
    if(!FDWCombatWorldPersistence::PrepareNewAdventureMarker(*CombatRules,*Save,CombatError)||
        !FDWFormalLanguagePersistence::InitializeNew(*this,*Save,SlotIndex,CombatError))return Fail(FText::FromString(CombatError));
    if (!ValidateSave(Save,SlotIndex)) return false;
    TSharedPtr<const FDWActiveSlotSource> NewSource;
    if (!WriteNewSlot(*Save,SlotIndex,NewSource,CombatError))return Fail(FText::FromString(CombatError));

    PreviousTravelSource=ActiveSlotSource;ResetCombatSaveFlow();
    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = SlotIndex;
    PendingSave = Save;
    if(!SetAdventureFlowOwner(Save,NewSource))
    {HandleTravelFailure(FText::FromString(TEXT("The new slot source owner could not be admitted.")));return false;}
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(Save->MapPackage)){HandleTravelFailure(Transition->LastError);return false;}
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
    return true;
}

bool UDWGameInstance::LoadGameSlot(int32 SlotIndex)
{
    FString StorageError;if(!CheckSaveStorageContext(StorageError))return Fail(FText::FromString(StorageError));
    if(RejectGatheringPreviewPersistence())return false;
    if(UDWWorldEventSubsystem::IsPlaying(this))return Fail(DWText(this,TEXT("请等待场景演出结束。"),TEXT("Please wait for the cinematic.")));
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())return Fail(DWText(this,TEXT("请等待当前过渡完成。"),TEXT("Please wait for the current transition.")));
    LastSaveError = FText::GetEmpty();
    if (!IsValidSlot(SlotIndex)) return Fail(DWText(this, TEXT("只能使用存档槽 1 至 3。"), TEXT("Only save slots 1 through 3 are available.")));
    if (!GetWorld() || !GetSubsystem<UDWLoadingTransitionSubsystem>()) return Fail(DWText(this, TEXT("游戏世界尚未准备好。"), TEXT("The game world is not ready yet.")));
    if (!SlotExists(MakeSlotName(SlotIndex)))
        return Fail(DWText(this, TEXT("该存档槽为空。"), TEXT("This save slot is empty.")));
    TSharedPtr<const FDWActiveSlotSource> NewSource;FString SourceError;
    UDWSaveGame* Save=ReadSlotWithSource(MakeSlotName(SlotIndex),NewSource,SourceError);
    TStrongObjectPtr<UDWSaveGame> KeepSave(Save);
    FString TravelMap,OpeningError;
    if(!Save)return Fail(DWText(this,TEXT("无法读取存档或地图兼容检查未通过；原文件已保留。"),TEXT("The save could not be read or its map compatibility check failed. The original file was preserved.")));
    if(!ResolveAdventureTravelMap(*Save,TravelMap,OpeningError))return Fail(FText::FromString(OpeningError));
    if (!ValidateSave(Save,SlotIndex)) return false;
    if(!ReadSlotMatchingSource(SlotIndex,NewSource,SourceError))return Fail(FText::FromString(SourceError));

    PreviousTravelSource=ActiveSlotSource;ResetCombatSaveFlow();
    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = SlotIndex;
    PendingSave = Save;
    if(!SetAdventureFlowOwner(Save,NewSource))
    {HandleTravelFailure(FText::FromString(TEXT("The new slot source owner could not be admitted.")));return false;}
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(TravelMap)){HandleTravelFailure(Transition->LastError);return false;}
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
    return true;
}

bool UDWGameInstance::DeleteGameSlot(int32 SlotIndex)
{
    FString StorageError;if(!CheckSaveStorageContext(StorageError))return Fail(FText::FromString(StorageError));
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
        PendingSave = nullptr;SetAdventureFlowOwner(nullptr);
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
    const UDWGameplayConfig* MenuConfig=GetDefaultGameplayConfig();
    const FString MenuMap=MenuConfig&&!bDefaultConfigRejected?MenuConfig->MainMenuMap:FString();
    if (!GetWorld() || !FPackageName::IsValidLongPackageName(MenuMap) || !FPackageName::DoesPackageExist(MenuMap))
    {
        Fail(DWText(this, TEXT("标题地图不存在，已留在当前关卡。"), TEXT("The title map is missing. You have remained in the current level.")));
        return;
    }
    ResetCombatSaveFlow();
    PreviousTravelSource=ActiveSlotSource;
    bSaveTravelInFlight=true;PreviousTravelSlot=ActiveSlot;ActiveSlot = INDEX_NONE;
    PendingSave = nullptr;SetAdventureFlowOwner(nullptr);
    auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!Transition->RequestMap(MenuMap))HandleTravelFailure(Transition->LastError);
    if(!Transition->IsTransitionActive())bSaveTravelInFlight=false;
}

bool UDWGameInstance::IsAllowedGameplayMap(const FString& Map)
{
 auto* C=GetDefaultGameplayConfig();if(!C)return false;
 const FString CanonicalMap=DWOpeningMapCompatibility::CanonicalPackage(Map);
 if(CanonicalMap==DWOpeningMapCompatibility::CanonicalPackage(C->GameplayMap))return true;
 if(CanonicalMap==DWOpeningMapCompatibility::CanonicalPackage(C->MainMenuMap))return false;
 if(!C->NewGameOpeningMap.IsNull()&&CanonicalMap==DWOpeningMapCompatibility::CanonicalPackage(C->NewGameOpeningMap.ToSoftObjectPath().GetLongPackageName()))return true;
 for(const auto& M:C->AdditionalGameplayMaps)if(DWOpeningMapCompatibility::CanonicalPackage(M.ToSoftObjectPath().GetLongPackageName())==CanonicalMap)return true;
 const TSoftObjectPtr<UDWGameplayConfig>* Override=nullptr;FString AliasError;
 if(!DWOpeningMapCompatibility::FindProfileOverride(*C,CanonicalMap,Override,AliasError))return false;
 return Override&&GetConfigForMap(CanonicalMap)!=nullptr;
}
bool UDWGameInstance::TravelToGameplayMap(TSoftObjectPtr<UWorld> Destination)
{
 const FString Map=Destination.ToSoftObjectPath().GetLongPackageName();
 if(RejectGatheringPreviewPersistence()||RejectGatheringPreviewPersistence(Map))return false;
 auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();
 if(T->IsTransitionActive())return false;
 if(!IsAllowedGameplayMap(Map)||!FPackageName::DoesPackageExist(Map))return Fail(DWText(this,TEXT("请在 Additional Gameplay Maps 中登记目标地图。"),TEXT("Register the destination in Additional Gameplay Maps first.")));
 const FString CurrentMap=GetWorld()?UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()):FString();
 if(CurrentMap!=Map&&(UDWGatheringPersistenceOptIn::Find(CurrentMap)||UDWGatheringPersistenceOptIn::Find(Map)))
     return Fail(DWText(this,TEXT("采集地图的跨地图进度尚未接入；请先保存并返回标题，在同一地图加载。"),
         TEXT("Cross-map gathering persistence is not available. Save and return to title, then load the same map.")));
 if(!AreGameplayMapProfilesCompatible(CurrentMap,Map))return Fail(DWText(this,TEXT("不同资源配置的地图暂不支持携带背包互相旅行，请分别开始或加载该地图的存档。"),TEXT("Travel with inventory between different gameplay profiles is not supported. Start or load that map separately.")));
 if(!SaveCurrentGame())return false;
 TSharedPtr<const FDWActiveSlotSource> TravelSource;FString SourceError;
 TStrongObjectPtr<UDWSaveGame> KeepSave(ReadActiveSlotForCapture(TravelSource,SourceError));auto* Save=KeepSave.Get();
 if(!Save)return Fail(FText::FromString(SourceError));
 Save->MapPackage=Map;Save->WorldActors.Reset();Save->CompletedWorldEvents.Reset();Save->bHasPlayerTransform=false;
 for(const auto& M:Save->VisitedMaps)if(M.MapPackage==Map){Save->WorldActors=M.Actors;Save->CompletedWorldEvents=M.CompletedWorldEvents;Save->PlayerTransform=M.PlayerTransform;Save->bHasPlayerTransform=true;break;}
 if(!ValidateSave(Save))return false;
 PreviousTravelSource=ActiveSlotSource;PendingSave=Save;PreviousTravelSlot=ActiveSlot;bSaveTravelInFlight=true;
 if(!T->RequestMap(Map)){HandleTravelFailure(T->LastError);return false;}
 if(!T->IsTransitionActive())bSaveTravelInFlight=false;
 return true;
}


bool UDWGameInstance::IsNonPersistentCombatSandboxContext()const
{return DWCombatSandbox::IsWorld(GetWorld())&&VerificationSavePrefix.IsEmpty()&&!HasActiveSlot()&&!PendingSave&&!ActiveSlotSource&&!bSaveTravelInFlight;}
void UDWGameInstance::InitializeSaveStorage()
{
    if(DWCombatSandbox::IsWorld(GetWorld())&&VerificationSavePrefix.IsEmpty())
    {bEditorIterationRequired=true;bUseDocumentsStorage=true;SaveDirectory.Empty();SaveStorageInitializationError=TEXT("CombatSandbox is non-persistent; save/load/new-game are disabled.");return;}
    bEditorIterationRequired=FDWEditorMapIterationSession::ShouldUse(*this,!VerificationSavePrefix.IsEmpty());
    if(bEditorIterationRequired)
    {
        bUseDocumentsStorage=true;SaveDirectory.Empty();
        const auto* Main=GetDefaultGameplayConfig();
        if(Main)EditorMapIteration=FDWEditorMapIterationSession::Start(*this,*Main,SaveStorageInitializationError);
        if(!EditorMapIteration)
        {
            if(SaveStorageInitializationError.IsEmpty())SaveStorageInitializationError=TEXT("请保存地图后再试玩：编辑器测试存储未能初始化，未读取正式存档。");
            UE_LOG(LogTemp,Warning,TEXT("DW_PIE_STORAGE_INIT_FAILED error=%s; formal_fallback=0"),*SaveStorageInitializationError);
            Fail(FText::FromString(SaveStorageInitializationError));return;
        }
        SaveDirectory=EditorMapIteration->Directory();return; // Never imports formal or legacy slots.
    }
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
bool UDWGameInstance::RemoveSlot(const FString& Slot) const
{ FString Error;if(!CheckSaveStorageContext(Error))return false;return bUseDocumentsStorage ? DWSaveStorage::Delete(SaveDirectory, Slot) : UGameplayStatics::DeleteGameInSlot(Slot, 0); }
bool UDWGameInstance::CheckSaveStorageContext(FString& Error)const
{
    if(DWCombatSandbox::IsWorld(GetWorld())){Error=TEXT("CombatSandbox does not support save/load/new-game.");return false;}
    if(!bEditorIterationRequired){Error.Reset();return true;}
    if(!EditorMapIteration){Error=SaveStorageInitializationError;return false;}
    return EditorMapIteration->CheckCurrent(Error);
}
FString UDWGameInstance::GetSaveStorageIdentity()const
{
    if(EditorMapIteration)return EditorMapIteration->Identity();
    return FString(bUseDocumentsStorage?TEXT("file:"):TEXT("engine:"))+FPaths::ConvertRelativePathToFull(SaveDirectory)+TEXT(":")+VerificationSavePrefix;
}
