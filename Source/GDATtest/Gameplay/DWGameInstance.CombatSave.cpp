#include "DWGameInstance.h"
#include "DWEditorMapIterationSession.h"
#include "DWWorldMap.h"
#include "DWFormalLanguagePersistence.h"
#include "DWLanguageSubsystem.h"
#include "DWCombatWorldPersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatRules.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayHUD.h"
#include "DWLoadingTransition.h"
#include "DWLocalizationLibrary.h"
#include "DWWorldEvent.h"
#include "DWSaveGame.h"
#include "DWFormalSaveVersionPolicy.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool SameSaveLife(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B)
{return A.Entity==B.Entity&&A.Life==B.Life;}
}

void UDWGameInstance::Shutdown()
{
    bCombatSaveAlive=false;ResetCombatSaveFlow();EditorMapIteration.Reset();Super::Shutdown();
}
void UDWGameInstance::ResetCombatSaveFlow()
{
    // One successful new travel/load selection invalidates every older queued callback.
    ++CombatSaveGeneration;if(!CombatSaveGeneration)++CombatSaveGeneration;
    FTSTicker::RemoveTicker(CombatDeathSaveTicker);CombatDeathSaveTicker.Reset();
    FTSTicker::RemoveTicker(CombatRestoreErrorTicker);CombatRestoreErrorTicker.Reset();
    FTSTicker::RemoveTicker(CombatPendingRestoreTicker);CombatPendingRestoreTicker.Reset();
    CombatDeathSavePlayer.Reset();CombatDeathSaveWorld.Reset();CombatDeathSaveLife={};CombatDeathSaveEpoch=0;
    CombatDeathSaveSlot=INDEX_NONE;CombatDeathSaveGeneration=0;
    bCombatRestoreFailure=false;bCombatRestoreErrorShown=false;CombatRestoreFailureWorld.Reset();
}
void UDWGameInstance::RegisterCombatRestoreFailure(UWorld* World,bool bCommitted,const FString& Detail)
{
    bCombatRestoreFailure=true;bCombatRestoreErrorShown=false;CombatRestoreFailureWorld=World;
    Fail(bCommitted
        ?DWText(this,TEXT("战斗现场已恢复，但显示或场景发布中断。已停止自动重试并保留原存档。请在存档列表点击加载，重新打开关卡。"),TEXT("Combat facts were restored, but scene publication was interrupted. Automatic retries stopped and the original save was preserved. Select Load in the save list to reopen the level."))
        :DWText(this,TEXT("完整战斗存档未能恢复；未写入或覆盖原存档。请在存档列表点击加载，重新打开关卡。"),TEXT("The complete combat save could not be restored. The original save was not overwritten. Select Load in the save list to reopen the level.")));
    if(!Detail.IsEmpty())LastSaveError=FText::FromString(LastSaveError.ToString()+TEXT("\n")+Detail);
    if(!Detail.IsEmpty())UE_LOG(LogTemp,Warning,TEXT("DW_COMBAT_RESTORE_REJECTED: %s"),*Detail);
    ShowPendingCombatRestoreError();
    if(bCombatRestoreErrorShown||CombatRestoreErrorTicker.IsValid())return;
    const TWeakObjectPtr<UDWGameInstance> WeakThis(this);const uint64 Generation=CombatSaveGeneration;
    CombatRestoreErrorTicker=FTSTicker::GetCoreTicker().AddTicker(TEXT("DWCombatRestoreError"),0.f,[WeakThis,Generation](float)
    {
        auto* GI=WeakThis.Get();if(!GI||!GI->bCombatSaveAlive||GI->CombatSaveGeneration!=Generation)return false;
        if(!GI->bCombatRestoreFailure||!GI->CombatRestoreFailureWorld.IsValid()||
            GI->GetWorld()!=GI->CombatRestoreFailureWorld.Get()||GI->CombatRestoreFailureWorld->bIsTearingDown)
        {GI->CombatRestoreErrorTicker.Reset();return false;}
        GI->ShowPendingCombatRestoreError();
        if(GI->CombatSaveGeneration!=Generation)return false;
        if(GI->bCombatRestoreErrorShown){GI->CombatRestoreErrorTicker.Reset();return false;}
        return true; // Core ticker also runs while the menu pauses world time.
    });
}
void UDWGameInstance::ShowPendingCombatRestoreError()
{
    if(!bCombatSaveAlive||!bCombatRestoreFailure||bCombatRestoreErrorShown||!CombatRestoreFailureWorld.IsValid()||
        GetWorld()!=CombatRestoreFailureWorld.Get()||CombatRestoreFailureWorld->bIsTearingDown)return;
    auto* PC=UGameplayStatics::GetPlayerController(this,0);auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
    if(!IsValid(HUD)||!HUD->HasActorBegunPlay()||!HUD->GetGameplayWidget())return;
    if(auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();T&&T->IsTransitionActive())
    {T->RevealCombatRestoreFailure(CombatRestoreFailureWorld.Get(),LastSaveError);return;}
    const uint64 Generation=CombatSaveGeneration;const TWeakObjectPtr<UWorld> World=CombatRestoreFailureWorld;
    const auto Current=[&](){return bCombatSaveAlive&&bCombatRestoreFailure&&CombatSaveGeneration==Generation&&
        World.IsValid()&&GetWorld()==World.Get()&&!World->bIsTearingDown&&IsValid(HUD)&&!HUD->IsActorBeingDestroyed();};
    HUD->SetSessionStarted(false);if(!Current())return;
    HUD->ShowMenu(EDWMenuPage::SaveSlots);if(!Current())return;
    // Existing SaveSlots uses its existing pause/input policy; no generated WBP or new controls.
    if(HUD->GetMenuPage()!=EDWMenuPage::SaveSlots)return;
    HUD->Notify(LastSaveError);if(!Current())return;bCombatRestoreErrorShown=true;
}

void UDWGameInstance::ApplyCompletePendingSave(ADWPlayerCharacter* Player)
{
    if(!bCombatSaveAlive||bApplyingCombatPending||bCombatRestoreFailure||CombatPendingRestoreTicker.IsValid()||
        !IsValid(Player)||Player->IsActorBeingDestroyed()||!PendingSave||!HasActiveSlot())return;
    UWorld* World=Player->GetWorld();
    if(!World||World!=GetWorld()||World->bIsTearingDown||!PendingSaveMatchesWorld(*PendingSave,*World))return;
    const TWeakObjectPtr<UDWGameInstance> WeakThis(this);const TWeakObjectPtr<UWorld> WeakWorld(World);
    const TWeakObjectPtr<ADWPlayerCharacter> WeakPlayer(Player);const TWeakObjectPtr<UDWSaveGame> WeakSave(PendingSave.Get());
    const uint64 Generation=CombatSaveGeneration;const int32 Slot=ActiveSlot;
    // Keep Pending and loading/input/save gates intact. All existing level actors finish BeginPlay first.
    CombatPendingRestoreTicker=FTSTicker::GetCoreTicker().AddTicker(TEXT("DWCompletePendingRestore"),0.f,
        [WeakThis,WeakWorld,WeakPlayer,WeakSave,Generation,Slot](float)
    {
        auto* GI=WeakThis.Get();if(!GI||!GI->bCombatSaveAlive||GI->CombatSaveGeneration!=Generation)return false;
        auto* W=WeakWorld.Get();auto* P=WeakPlayer.Get();
        if(!W||W->bIsTearingDown||GI->GetWorld()!=W||!WeakSave.IsValid()||GI->PendingSave.Get()!=WeakSave.Get()||
            GI->ActiveSlot!=Slot||GI->bCombatRestoreFailure)
        {GI->CombatPendingRestoreTicker.Reset();return false;}
        if(!IsValid(P)||P->IsActorBeingDestroyed()||P->GetWorld()!=W)
        {
            GI->CombatPendingRestoreTicker.Reset();
            GI->RegisterCombatRestoreFailure(W,false,TEXT("Pending player ended before the complete restore boundary."));return false;
        }
        if(!W->HasBegunPlay()||!P->HasActorBegunPlay())return true;
        // Loading cover waits on Pending first, so wait for in-flight scene streaming here as well.
        for(auto* Level:W->GetStreamingLevels())if(Level&&Level->IsStreamingStatePending())return true;
        if(auto* Partition=W->GetSubsystem<UWorldPartitionSubsystem>();Partition&&!Partition->IsAllStreamingCompleted())return true;
        GI->CombatPendingRestoreTicker.Reset(); // Consume the one scheduled attempt before any restore callback.
        GI->ExecuteCompletePendingSave(P);
        return false; // Rejected/committed results are terminal; never loop and replay adoption.
    });
}

void UDWGameInstance::ExecuteCompletePendingSave(ADWPlayerCharacter* Player)
{
    if(!bCombatSaveAlive||bApplyingCombatPending||bCombatRestoreFailure||!IsValid(Player)||Player->IsActorBeingDestroyed()||!PendingSave||!HasActiveSlot())return;
    UWorld* World=Player->GetWorld();if(!World||World!=GetWorld()||World->bIsTearingDown||
        !PendingSaveMatchesWorld(*PendingSave,*World))return;
    const uint64 Generation=CombatSaveGeneration;const int32 Slot=ActiveSlot;
    const TWeakObjectPtr<UWorld> WeakWorld(World);TStrongObjectPtr<UDWSaveGame> Save(PendingSave.Get());
    TGuardValue<bool> Applying(bApplyingCombatPending,true);
    const auto CurrentRequest=[&](){return bCombatSaveAlive&&CombatSaveGeneration==Generation&&ActiveSlot==Slot&&PendingSave==Save.Get();};
    if(!World->HasBegunPlay()||!Player->HasActorBegunPlay())return;
    if(!ValidateSave(Save.Get()))
    {if(CurrentRequest())RegisterCombatRestoreFailure(World,false,LastSaveError.IsEmpty()?TEXT("Complete save validation rejected the pending record."):LastSaveError.ToString());return;}
    auto* DefinitionOwner=GetSubsystem<UDWLanguageSubsystem>();
    DWCharacterA01SaveBridge::FScopedFormalDefinitions DefinitionScope(DefinitionOwner&&DefinitionOwner->GetNativeModel()?&DefinitionOwner->GetNativeModel()->GetCatalog():nullptr,DefinitionOwner?DefinitionOwner->GetTrialConfig():TSharedPtr<FJsonObject>());
    bool bCommitted=false;FString Error;bool bOk=false;
    // New-adventure initialization uses an existing special path rather than RestoreImpl.
    // Prepare marker facts before that path, then adopt only after the same request succeeds.
    TWeakObjectPtr<UDWWorldMapSubsystem> NewMapState;TWeakObjectPtr<UDWGameInstance> WeakMapGI(this);TWeakObjectPtr<ADWPlayerCharacter> WeakMapPlayer(Player);bool bPreparedNewMap=false;FDWWorldMapRestorePlan NewMapPlan;
    if(Save->CombatWorld.bNewAdventure&&Save->AdventureFlow.Phase!=EDWAdventureOpeningPhase::CompletedPendingDestination)
    {
        NewMapState=World->GetSubsystem<UDWWorldMapSubsystem>();bPreparedNewMap=true;
        if(!NewMapState.IsValid()||!NewMapState->PrepareLoadedMarkers(Slot,Save->WorldMapMarkers,NewMapPlan,Error))
        {if(CurrentRequest())RegisterCombatRestoreFailure(World,false,Error);return;}
    }
    if(Save->AdventureFlow.Schema==1&&Save->AdventureFlow.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination)
        bOk=ExecuteOpeningFreshDestination(*Player,*Save,bCommitted,Error);
    else if(Save->CombatWorld.bNewAdventure)
    {
        // Only strict MarkNewAdventure data uses the existing new-character initialization path.
        // Old schema tests may call that path directly; real GI never accepts an incomplete running save.
        bOk=Player->TryApplySaveData(Save.Get());
        if(!bOk)Error=TEXT("Validated new-adventure initialization failed.");
        else if(Save->FormalLanguage.Schema==2)bOk=FDWFormalLanguagePersistence::InitializeAfterLegacyMarker(*this,*Player,*Save,Error);
    }
    else bOk=FDWCombatWorldPersistence::Restore(*Player,*Save,bCommitted,Error);
    if(bOk&&CurrentRequest()&&!FinishOpeningPlayerRestore(*Player,*Save,Error))bOk=false;
    if(bOk&&CurrentRequest()&&bPreparedNewMap)
    {
        const auto MapCurrent=[&](){auto* WorldMapGuardGI=WeakMapGI.Get();auto* WorldMapGuardWorld=WeakWorld.Get();auto* WorldMapGuardPlayer=WeakMapPlayer.Get();auto* WorldMapGuardState=NewMapState.Get();return WorldMapGuardGI==this&&CurrentRequest()&&WorldMapGuardWorld&&!WorldMapGuardWorld->bIsTearingDown&&WorldMapGuardGI->GetWorld()==WorldMapGuardWorld&&WorldMapGuardPlayer&&!WorldMapGuardPlayer->IsActorBeingDestroyed()&&WorldMapGuardPlayer->GetWorld()==WorldMapGuardWorld&&WorldMapGuardState&&WorldMapGuardState->GetWorld()==WorldMapGuardWorld;};if(!MapCurrent()||!NewMapState->CanAdoptLoadedMarkers(NewMapPlan,Error)||!MapCurrent()){if(Error.IsEmpty())Error=TEXT("World-map new-adventure owner changed.");bOk=false;bCommitted=true;}
        else
        {
            NewMapState->AdoptLoadedMarkersUnchecked(MoveTemp(NewMapPlan));
            if(!NewMapState->PublishLoadedMarkers(Slot,MapCurrent,Error)){bOk=false;bCommitted=true;}
        }
    }
    if(!CurrentRequest())return; // A callback explicitly selected another slot/request; do not clear its request.
    if(bOk||bCommitted)PendingSave=nullptr;
    // A committed request is consumed even if a callback tore its world down. Only its own request is cleared.
    if(!bOk)RegisterCombatRestoreFailure(WeakWorld.Get(),bCommitted,Error);
}

bool UDWGameInstance::SaveCombatSnapshot(ADWPlayerCharacter* ExpectedPlayer,bool bAllowCommittedDeath)
{
    FString StorageError;if(!CheckSaveStorageContext(StorageError))return Fail(FText::FromString(StorageError));
    // Covers SaveCurrentGame, save-and-exit and the private committed-death path.
    // Reject before slot reads, capture or WriteSlot, including no-active-slot playtests.
    if(IsOpeningPersistenceBlocked())return Fail(DWText(this,TEXT("开场期间不覆盖起点检查点；中断或死亡后从安全起点重来。"),TEXT("The opening keeps its safe start checkpoint. Restart or death returns to that checkpoint.")));
    if(RejectGatheringPreviewPersistence())return false;
    const auto* Transition=GetSubsystem<UDWLoadingTransitionSubsystem>();
    if(!bCombatSaveAlive||bCombatRestoreFailure||bSaveTravelInFlight||PendingSave||(Transition&&Transition->IsTransitionActive()))
        return Fail(DWText(this,TEXT("请完成读档或重新加载关卡后再保存；原存档已保留。"),TEXT("Finish loading or reopen the level before saving. The original save was preserved.")));
    if(UDWWorldEventSubsystem::IsPlaying(this))return Fail(DWText(this,TEXT("请等待场景演出结束再保存。"),TEXT("Please wait for the cinematic before saving.")));
    LastSaveError=FText::GetEmpty();
    if(!HasActiveSlot())return Fail(DWText(this,TEXT("尚未进入存档，无法保存标题界面。"),TEXT("No save slot is active. You cannot save from the title screen.")));
    UWorld* World=GetWorld();auto* Player=Cast<ADWPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    auto* Authority=UDWCombatAuthoritySubsystem::Get(Player);
    if(!World||World->bIsTearingDown||!IsValid(Player)||Player->IsActorBeingDestroyed()||Player->GetWorld()!=World||
        (ExpectedPlayer&&ExpectedPlayer!=Player)||!Authority||!Player->HasCombatVitalsReady()||Player->IsCombatRestoreInProgress())
        return Fail(DWText(this,TEXT("玩家或战斗现场尚未准备好，原存档已保留。"),TEXT("The player or combat world is not ready. The original save was preserved.")));
    if(Player->IsDead()&&!bAllowCommittedDeath)
        return Fail(DWText(this,TEXT("角色已死亡，普通保存不会覆盖存档；死亡现场由系统统一记录。"),TEXT("The character has died. Manual saving is disabled; the committed death snapshot is recorded by the system.")));
    const auto Life=Authority->FindRegisteredActorLife(Player);const uint64 Epoch=Authority->GetWorldEpoch();
    const uint64 Generation=CombatSaveGeneration;const int32 Slot=ActiveSlot;
    if(bAllowCommittedDeath&&(!ExpectedPlayer||!Player->IsDead()||CombatDeathSavePlayer.Get()!=Player||CombatDeathSaveWorld.Get()!=World||
        CombatDeathSaveGeneration!=Generation||CombatDeathSaveSlot!=Slot||CombatDeathSaveEpoch!=Epoch||!SameSaveLife(CombatDeathSaveLife,Life)))
        return false; // Only the fixed private death ticket may authorize an otherwise-dead snapshot.
    const FString MapPackage=UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
    if(!IsAllowedGameplayMap(MapPackage))return Fail(DWText(this,TEXT("当前关卡不是登记的玩法地图，未覆盖原存档。"),TEXT("This level is not a registered gameplay map. The original save was preserved.")));
    const TWeakObjectPtr<UWorld> WeakWorld(World);const TWeakObjectPtr<ADWPlayerCharacter> WeakPlayer(Player);
    const auto Current=[&]()
    {
        auto* P=WeakPlayer.Get();auto* A=UDWCombatAuthoritySubsystem::Get(P);
        return bCombatSaveAlive&&!bCombatRestoreFailure&&CombatSaveGeneration==Generation&&ActiveSlot==Slot&&!bSaveTravelInFlight&&!PendingSave&&
            WeakWorld.IsValid()&&GetWorld()==WeakWorld.Get()&&!WeakWorld->bIsTearingDown&&IsValid(P)&&!P->IsActorBeingDestroyed()&&
            P->GetWorld()==WeakWorld.Get()&&!P->IsCombatRestoreInProgress()&&P->IsDead()==bAllowCommittedDeath&&
            UGameplayStatics::GetPlayerPawn(this,0)==P&&A&&A->GetWorldEpoch()==Epoch&&SameSaveLife(A->FindRegisteredActorLife(P),Life);
    };
    FString SourceError;const FDWAdventureFlowSave OwnerFlow=AdventureFlow;
    TSharedPtr<const FDWActiveSlotSource> CapturedSource;
    TStrongObjectPtr<UDWSaveGame> Existing(ReadActiveSlotForCapture(CapturedSource,SourceError));
    if(!Existing.Get())return Fail(FText::FromString(SourceError));
    TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());
    if(Existing)
    {
        DWFormalSaveVersionPolicy::EFamily ExistingFamily;FString ExistingError;
        if(!DWFormalSaveVersionPolicy::Classify(*Existing,ExistingFamily,ExistingError))return Fail(FText::FromString(ExistingError));
    }
    Save->DisplayName=Existing->DisplayName;
    FString Error;
    FDWFormalCaptureStamp FormalStamp;const auto* Language=GetSubsystem<UDWLanguageSubsystem>();const bool bFormal=Language&&Language->IsFormalSession();
    if((bFormal&&!FDWFormalLanguagePersistence::MakeCaptureStamp(*Player,FormalStamp,Error))||!FDWCombatWorldPersistence::Capture(*Player,*Save,Error)||!Current())
    {
        UE_LOG(LogTemp,Warning,TEXT("DW_COMBAT_CAPTURE_REJECTED (slot %d, death=%d): %s"),Slot+1,bAllowCommittedDeath?1:0,
            Error.IsEmpty()?TEXT("Player/world/slot/life changed before write."):*Error);
        return Fail(DWText(this,TEXT("完整战斗现场尚未处于可保存边界；未覆盖原存档。"),TEXT("The complete combat world is not at a save boundary. The original save was preserved.")));
    }
    // D22Travel owns the later full per-map scene/identity-bank merge. Keep the existing record here.
    if(bFormal&&(!Existing||Existing->FormalLanguage.Schema!=2||Existing->FormalLanguage.SlotIndex!=Slot||
        !FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&Existing->AdventureFlow,&AdventureFlow,PPF_None)))
        return Fail(DWText(this,TEXT("当前槽的正式归属或阶段已变化，未覆盖原存档。"),TEXT("The formal slot owner or phase changed; its file was preserved.")));
    if(Existing&&Existing->Version!=ActiveSaveVersion)return Fail(FText::FromString(TEXT("Save-container owner changed; original slot preserved.")));
    if(Existing){Save->VisitedMaps=Existing->VisitedMaps;Save->AdventureFlow=Existing->AdventureFlow;}
    UDWWorldMapSubsystem* MapState=World->GetSubsystem<UDWWorldMapSubsystem>();
    const uint64 MapRevision=MapState?MapState->GetMarkerRevision():0;
    if(MapState)
    {
        if(!MapState->MergeMarkersForSave(Slot,Existing->WorldMapMarkers,Save->WorldMapMarkers,Error))return Fail(FText::FromString(Error));
    }
    else Save->WorldMapMarkers=Existing->WorldMapMarkers;
    auto* Map=Save->VisitedMaps.FindByPredicate([&](const FDWMapSnapshot& M){return M.MapPackage==MapPackage;});
    if(!Map)Map=&Save->VisitedMaps.AddDefaulted_GetRef();
    Map->MapPackage=MapPackage;Map->Actors=Save->WorldActors;Map->CompletedWorldEvents=Save->CompletedWorldEvents;Map->PlayerTransform=Save->PlayerTransform;
    Save->Version=Existing->Version;Save->Timestamp=FDateTime::UtcNow();
    if(!ValidateSave(Save.Get())||!Current()||(MapState&&MapState->GetMarkerRevision()!=MapRevision)||(bFormal&&!FDWFormalLanguagePersistence::IsCaptureCurrent(FormalStamp)))return false;
    if(!CommitActiveSlotSave(*Save,CapturedSource,OwnerFlow,Error))return Fail(FText::Format(DWText(this,TEXT("保存已取消，原文件未覆盖。原因：{0}"),TEXT("Saving was cancelled; the original file was not overwritten. Reason: {0}")),FText::FromString(Error)));
    return true;
}

void UDWGameInstance::QueueCommittedCombatDeath(ADWPlayerCharacter* Player,DWCombatCommit::FEntityLife Life)
{
    if(IsOpeningPersistenceBlocked())return; // Never overwrite the start checkpoint or a durable completed handoff.
    if(!bCombatSaveAlive||bCombatRestoreFailure||!HasActiveSlot()||bSaveTravelInFlight||PendingSave||!IsValid(Player)||
        Player->IsActorBeingDestroyed()||Player->GetGameInstance()!=this||Player->GetWorld()!=GetWorld()||!GetWorld()||GetWorld()->bIsTearingDown||
        !Player->IsDead()||Player->IsCombatRestoreInProgress())return;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(Player);
    if(!Authority||!Life.Entity||!Life.Life||!SameSaveLife(Authority->FindRegisteredActorLife(Player),Life))return;
    const uint64 Epoch=Authority->GetWorldEpoch();
    if(CombatDeathSavePlayer.Get()==Player&&CombatDeathSaveGeneration==CombatSaveGeneration&&CombatDeathSaveEpoch==Epoch&&SameSaveLife(CombatDeathSaveLife,Life))return;
    FTSTicker::RemoveTicker(CombatDeathSaveTicker);CombatDeathSaveTicker.Reset();
    CombatDeathSavePlayer=Player;CombatDeathSaveWorld=Player->GetWorld();CombatDeathSaveLife=Life;
    CombatDeathSaveEpoch=Epoch;CombatDeathSaveSlot=ActiveSlot;CombatDeathSaveGeneration=CombatSaveGeneration;
    const TWeakObjectPtr<UDWGameInstance> WeakThis(this);const uint64 Generation=CombatSaveGeneration;
    CombatDeathSaveTicker=FTSTicker::GetCoreTicker().AddTicker(TEXT("DWCommittedDeathSave"),0.f,[WeakThis,Generation](float)
    {
        auto* GI=WeakThis.Get();if(!GI||!GI->bCombatSaveAlive||GI->CombatSaveGeneration!=Generation)return false;
        GI->CombatDeathSaveTicker.Reset(); // One-shot, also safe if a save callback starts another request.
        auto* P=GI->CombatDeathSavePlayer.Get();auto* W=GI->CombatDeathSaveWorld.Get();auto* A=UDWCombatAuthoritySubsystem::Get(P);
        if(!IsValid(P)||P->IsActorBeingDestroyed()||!W||W->bIsTearingDown||GI->GetWorld()!=W||P->GetWorld()!=W||
            !GI->HasActiveSlot()||GI->ActiveSlot!=GI->CombatDeathSaveSlot||GI->bCombatRestoreFailure||GI->PendingSave||GI->bSaveTravelInFlight||
            !P->IsDead()||P->IsCombatRestoreInProgress()||!A||A->GetWorldEpoch()!=GI->CombatDeathSaveEpoch||
            !SameSaveLife(A->FindRegisteredActorLife(P),GI->CombatDeathSaveLife))return false;
        const bool Saved=GI->SaveCombatSnapshot(P,true);
        if(!Saved&&GI->bCombatSaveAlive&&GI->CombatSaveGeneration==Generation&&GI->GetWorld()==W&&!W->bIsTearingDown)
            if(auto* PC=UGameplayStatics::GetPlayerController(GI,0))if(auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD()))HUD->Notify(GI->LastSaveError);
        return false;
    });
}
