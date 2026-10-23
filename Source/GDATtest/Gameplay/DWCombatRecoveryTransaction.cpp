#include "DWCombatRecoveryTransaction.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWCombatRecoveryActors.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWWeaponAnimationComponent.h"
#include "DWEnemyCharacter.h"
#include "DWGameplayConfig.h"
#include "DWGameplayHUD.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

bool FDWCombatRecoveryTransaction::PrepareFatal(ADWPlayerCharacter& P,FDWPreparedCombatDeath& Out)
{
    auto* R=UDWCombatRecoverySubsystem::Get(&P);auto* A=UDWCombatAuthoritySubsystem::Get(&P);auto* I=P.GetInventory();
    if(!R||!A||!I||!I->CanWriteInventory()||!R->EnsurePlayer(P)||R->State.bAwaitingRespawn||
        R->State.Revision==MAX_uint64||P.bRestoringSave)return false;
    TArray<FDWItemStack> Kept;TArray<FDWDeathBagStack> Dropped;
    if(!DWCombatRecovery::Partition(I->GetSlots(),Kept,Dropped))return false;
    FDWPreparedCombatDeath Plan;Plan.Player=&P;Plan.Recovery=R;Plan.Candidate=R->State;
    if(!I->MakePrepared(MoveTemp(Kept),Plan.Inventory)||!I->ValidatePreparedForCommit(Plan.Inventory))return false;
    FDWDeathBagSave Bag;Bag.Schema=1;Bag.BagId=FGuid::NewGuid();Bag.PlayerId=R->State.PlayerId;Bag.MapPackage=R->MapPackage(P.GetWorld());
    Bag.Location=P.GetActorLocation();Bag.Contents=MoveTemp(Dropped);Bag.bClaimed=DWCombatRecovery::IsEmpty(Bag);
    Plan.Candidate.Bags.Add(Bag);Plan.Candidate.CurrentDeathBagId=Bag.BagId;Plan.Candidate.bAwaitingRespawn=true;++Plan.Candidate.Revision;
    FString Error;if(!DWCombatRecovery::Validate(Plan.Candidate,Error))return false;
    Plan.Notice.Kind=EDWRecoveryNotice::Death;Plan.Notice.Player=&P;Plan.Notice.Inventory=I;Plan.Notice.Recovery=R;
    Plan.Notice.PlayerId=Bag.PlayerId;Plan.Notice.BagId=Bag.BagId;Plan.Notice.RecoveryRevision=Plan.Candidate.Revision;
    Plan.Notice.WorldEpoch=A->GetWorldEpoch();Plan.Notice.bInventoryChanged=!Bag.Contents.IsEmpty();
    Plan.bValid=true;Out=MoveTemp(Plan);return true;
}
void FDWCombatRecoveryTransaction::CommitFatalUnchecked(FDWPreparedCombatDeath&& Plan)
{
    check(Plan.bValid);auto* P=Plan.Player.Get();auto* R=Plan.Recovery.Get();auto* I=P->GetInventory();
    I->AdoptPreparedUnchecked(MoveTemp(Plan.Inventory));R->State=MoveTemp(Plan.Candidate);Plan.bValid=false;
    P->EndFormalDashFacts(EDWYeastDashEnd::Death);P->PaidCombatWeaponSlice={};
    if(auto* A=UDWCombatAuthoritySubsystem::Get(P))A->EndAction(P->MeleeCommitAction);
    P->MeleeCommitAction={};P->AttackPhase=EDWCombatPhase::None;P->MeleeHitActors.Reset();P->ClearMeleeComboContinuation();
    if(auto* W=P->WeaponAnimationComponent.Get();W&&!W->IsBeingDestroyed())
    {
        // Native action facts only; trail/mesh cleanup remains in the later death presentation.
        W->EndFormalBow();W->EndFormalBottle();W->CancelFormalSporeFacts();W->EndFormalSpear();W->EndFormalUnarmed();
        W->EndFormalAxe();W->CancelFormalSwordFacts();W->EndFormalDagger();W->EndFormalShotgun();W->EndWeaponResourceActions();
        if(W->bActive)P->GetCharacterMovement()->bOrientRotationToMovement=W->bOldOrient;
        W->bActive=W->bPrimaryHeld=W->bSecondaryHeld=W->bCharging=W->bChargeReady=W->bReloading=false;
        W->PrimaryInputOrigin.Reset();W->SecondaryInputOrigin.Reset();
    }
    // Return intent is native only; the AI tick moves actors, never rewinds health/death/drop facts.
    for(TActorIterator<ADWEnemyCharacter> It(P->GetWorld());It;++It)if(It->IsAlive())It->BeginPlayerDeathReturnFacts();
}
bool FDWCombatRecoveryTransaction::Claim(ADWCombatDeathBag& B,ADWPlayerCharacter& P)
{
    using namespace DWCombatCommit;
    auto* R=UDWCombatRecoverySubsystem::Get(&B);auto* A=UDWCombatAuthoritySubsystem::Get(&P);auto* I=P.GetInventory();
    if(!R||!A||!I||!I->CanWriteInventory()||P.GetWorld()!=B.GetWorld()||!P.IsCombatActionAllowed()||
        R->BoundPlayer.Get()!=&P||!R->IsPhysicalCurrent(B)||!B.Touches(P)||R->State.Revision==MAX_uint64)return false;
    FDWDeathBagSave Bag;if(!R->FindBag(B.BagId,Bag))return false;const auto* Config=I->GetConfig();if(!Config)return false;
    TMap<FName,int32> Limits;for(const auto& S:I->GetSlots())Limits.Add(S.ItemId,I->GetEffectiveStackLimit(S.ItemId));
    for(const auto& S:Bag.Contents)Limits.Add(S.ItemId,I->GetEffectiveStackLimit(S.ItemId));
    TArray<FDWItemStack> Slots;FDWDeathBagSave Changed;int64 Transferred=0;
    if(!DWCombatRecovery::PlanClaim(Bag,I->GetSlots(),Config->MaxInventorySlots,Limits,Slots,Changed,Transferred))return false;
    FDWPreparedInventoryChange Inventory;if(!I->MakePrepared(MoveTemp(Slots),Inventory))return false;
    auto Candidate=R->State;const int32 Index=Candidate.Bags.IndexOfByPredicate([&](const auto& Entry){return Entry.BagId==Bag.BagId;});
    if(Index==INDEX_NONE)return false;Candidate.Bags[Index]=Changed;++Candidate.Revision;
    auto* Ledger=A->GetLedgerForResourceTransaction();const auto Action=A->BeginAction(&P);
    if(!Ledger||!Action.Id||!A->ReserveRecoveryNoticeSlot()){A->EndAction(Action);return false;}
    const auto Admission=Ledger->AdmitResource({Action,1,EResourceKind::DeathBagClaim,1});
    if(Admission.Result!=EAdmission::Admitted){A->EndAction(Action);return false;}
    if(!I->ValidatePreparedForCommit(Inventory)||!R->IsPhysicalCurrent(B))
    {Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);A->EndAction(Action);return false;}
    FDWCombatRecoveryNotice N;N.Kind=EDWRecoveryNotice::Claim;N.Player=&P;N.Inventory=I;N.Recovery=R;N.Life=A->CaptureActorLife(&P);
    N.PlayerId=Candidate.PlayerId;N.BagId=Bag.BagId;N.WorldEpoch=A->GetWorldEpoch();N.RecoveryRevision=Candidate.Revision;N.bInventoryChanged=true;
    if(Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved){A->EndAction(Action);return false;}
    I->AdoptPreparedUnchecked(MoveTemp(Inventory));R->State=MoveTemp(Candidate);
    A->EndAction(Action);A->EnqueueCommittedRecovery(N);A->DrainNotifications();return true;
}
bool FDWCombatRecoveryTransaction::Respawn(ADWPlayerCharacter& P)
{
    using namespace DWCombatCommit;
    auto* R=UDWCombatRecoverySubsystem::Get(&P);auto* A=UDWCombatAuthoritySubsystem::Get(&P);auto* I=P.GetInventory();
    auto* W=P.WeaponAnimationComponent.Get();
    if(!R||!A||!I||!W||W->IsBeingDestroyed()||!I->CanWriteInventory()||!R->CanRespawn(P)||
        P.bRestoringSave||P.bGameplayActionInProgress||P.bCombatRecoveryPositionPending||!A->CanRestorePlayerState(&P))return false;
    FDWCombatPlayerVitalsRuntime Vitals;if(!Vitals.Initialize(P.CombatVitals.GetConfig(),EDWCombatVitalsReset::Respawn))return false;
    auto Candidate=R->State;Candidate.bAwaitingRespawn=false;Candidate.CurrentDeathBagId.Invalidate();++Candidate.Revision;
    auto* Ledger=A->GetLedgerForResourceTransaction();const auto Life=A->CaptureActorLife(&P);
    if(!Ledger||!Life.Entity||!A->ReserveRecoveryNoticeSlot())return false;
    // Dedicated fixed recovery commit may be admitted for the dead life; ordinary BeginAction still refuses death.
    const auto Action=Ledger->BeginAction(Life);if(!Action.Id)return false;
    const auto Admission=Ledger->AdmitResource({Action,1,EResourceKind::PlayerRespawn,1});
    if(Admission.Result!=EAdmission::Admitted){Ledger->EndAction(Action);return false;}
    FDWCombatRecoveryNotice N;N.Kind=EDWRecoveryNotice::Respawn;N.Player=&P;N.Inventory=I;N.Recovery=R;N.PlayerId=Candidate.PlayerId;
    N.WorldEpoch=A->GetWorldEpoch();N.RecoveryRevision=Candidate.Revision;N.Destination=Candidate.SafePoint.Transform;
    const auto RespawnValues=Vitals.Capture(P.CombatVitals.GetConfig().Vitals.HealthMax*P.CombatVitals.GetConfig().Vitals.RespawnHealthFraction);
    const float Health=DWCombatPlayerSave::NativeHealth(RespawnValues.Health,P.CombatVitals.GetConfig());
    if(!FMath::IsFinite(Health)||Health<=P.GetCombatHealthFloor())
    {Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);Ledger->EndAction(Action);return false;}
    if(Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved){Ledger->EndAction(Action);return false;}
    Ledger->EndAction(Action);A->NotifyActorRestored(&P);
    P.CombatVitals=MoveTemp(Vitals);P.Health=Health;P.bDeathHandled=false;P.bCombatDeathEffectsPending=false;
    P.bCombatRecoveryPositionPending=true;P.bHasCombatFormFrameSlice=false;P.PaidCombatWeaponSlice={};P.ResetCombatKnockbackFacts();
    P.SyncCombatVitalsCompatibility();R->State=MoveTemp(Candidate);R->bPlacementPending=true;N.Life=A->CaptureActorLife(&P);
    // Permanent ownership, current equipment, loaded shells and all remaining locks/CD are deliberately untouched.
    A->EnqueueCommittedRecovery(N);A->DrainNotifications();return true;
}
void FDWCombatRecoveryTransaction::Publish(const FDWCombatRecoveryNotice& N)
{
    auto Current=[&]()
    {
        auto* P=N.Player.Get();auto* A=UDWCombatAuthoritySubsystem::Get(P);auto* R=UDWCombatRecoverySubsystem::Get(P);
        if(!A||!R||!IsValid(P)||P->IsActorBeingDestroyed()||A->GetWorldEpoch()!=N.WorldEpoch||R->State.PlayerId!=N.PlayerId||
            R->BoundPlayer.Get()!=P)return false;
        const auto L=A->CaptureActorLife(P);return L.Entity==N.Life.Entity&&L.Life==N.Life.Life;
    };
    if(Current())
    {
        auto* P=N.Player.Get();
        if(N.Kind==EDWRecoveryNotice::Respawn)
        {
            if(auto* PC=Cast<ADWPlayerController>(P->GetController()))PC->InvalidateCombatInputLife();if(!Current())return;
            P->ClearHeldActions();if(!Current())return;
            if(!P->SetActorTransform(N.Destination,false,nullptr,ETeleportType::TeleportPhysics)||!Current())return;
            P->GetCharacterMovement()->StopMovementImmediately();P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);if(!Current())return;
            P->bCombatRecoveryPositionPending=false;UDWCombatRecoverySubsystem::Get(P)->bPlacementPending=false;
            P->UpdateFormAppearance(true);if(!Current())return;P->UpdateAnimation();if(!Current())return;
            if(auto* PC=Cast<APlayerController>(P->GetController()))if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))
            {H->SetSessionStarted(true);H->ClosePanels();}
            if(!Current())return;
        }
        if(N.bInventoryChanged){auto* I=N.Inventory.Get();if(I&&I->CanWriteInventory()&&I->GetOwner()==P)I->OnChanged.Broadcast();}
    }
    // Claim facts survive collector destruction/restore. Retire a now-empty marker independently.
    if(N.Kind==EDWRecoveryNotice::Claim)
    {
        auto* R=N.Recovery.Get();auto* A=UDWCombatAuthoritySubsystem::Get(R);FDWDeathBagSave B;
        if(A&&A->GetWorldEpoch()==N.WorldEpoch&&R&&R->State.PlayerId==N.PlayerId&&R->FindBag(N.BagId,B)&&B.bClaimed)
            if(auto* Physical=R->FindPhysicalBag(B.BagId);Physical&&!Physical->IsActorBeingDestroyed())Physical->Destroy();
    }
}
