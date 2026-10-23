#include "DWCombatBowShotTransaction.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWCombatBowProjectile.h"
#include "DWCombatItemRegistry.h"
#include "DWEnemyCharacter.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWPlayerCharacter.h"
#include "Engine/World.h"
bool FDWCombatBowShotTransaction::Commit(UDWWeaponAnimationComponent& W,double Offset)
{
    using namespace DWCombatCommit;
    auto* P=W.Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);auto* I=P?P->GetInventory():nullptr;
    const auto Action=W.BowAction;
    if(!A||!P||!I||!W.IsCurrentFormalBow(Action)||W.BowPlan.Phase!=EDWBowPhase::Release||W.bBowShotCommitted||
        !I->CanWriteInventory()||I->GetOwner()!=P||W.CombatWeaponResources.Revision==MAX_int64)return false;
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::MoldBow);if(!Binding||Binding->AmmoItemId.IsNone())return false;
    FDWPreparedInventoryChange Prepared;if(!I->PrepareRemoveItem(Binding->AmmoItemId,W.BowConfig.AmmoPerShot,Prepared))return false;
    const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch(),Restore=W.CombatWeaponResources.RestoreEpoch;
    const int64 Revision=W.CombatWeaponResources.Revision;const int32 Serial=W.ActionSerial;
    const TWeakObjectPtr<UWorld> World(P->GetWorld());const FTransform SpawnTransform(W.BowForward.Rotation(),P->GetActorLocation());
    auto* Bullet=World->SpawnActorDeferred<ADWCombatBowProjectile>(ADWCombatBowProjectile::StaticClass(),SpawnTransform,
        P,P,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Bullet)return false;
    Bullet->FinishSpawning(SpawnTransform); // May invoke external spawn/BeginPlay listeners; still inert and no cost.
    const auto StillCurrent=[&]()
    {
        auto* CurrentA=UDWCombatAuthoritySubsystem::Get(P);
        if(!World.IsValid()||World->bIsTearingDown||!IsValid(Bullet)||Bullet->IsActorBeingDestroyed()||Bullet->IsReleased()||
            !IsValid(&W)||W.IsBeingDestroyed()||!CurrentA||CurrentA->GetWorldEpoch()!=Epoch||
            !W.IsCurrentFormalBow(Action)||W.Player()!=P||P->GetWorld()!=World.Get()||W.ActionSerial!=Serial||
            W.BowPlan.Phase!=EDWBowPhase::Release||W.bBowShotCommitted||
            W.CombatWeaponResources.Revision!=Revision||W.CombatWeaponResources.RestoreEpoch!=Restore||!I->ValidatePreparedForCommit(Prepared))return false;
        const auto L=CurrentA->CaptureActorLife(P);return L.Entity==Life.Entity&&L.Life==Life.Life;
    };
    const auto Discard=[&](){if(IsValid(Bullet)&&!Bullet->IsActorBeingDestroyed()&&World.IsValid()&&!World->bIsTearingDown)Bullet->Destroy();};
    if(!StillCurrent()){Discard();return false;}
    Bullet->CaptureFlightPresentation(W,true); // Same source geometry/world pose, inert and no delegates.
    if(!StillCurrent()){Discard();return false;}
    A=UDWCombatAuthoritySubsystem::Get(P);auto* Ledger=A->GetLedgerForResourceTransaction();
    const auto Form=W.ReadFormalBowForm(Offset);const auto Snapshot=W.BowConfig.Shot(Form);
    FDWBowPlan Candidate=W.BowPlan;
    if(!Snapshot.IsValid()||!Candidate.EnterNext(Form)||!Ledger||!A->ReserveWeaponResourceNoticeSlot()){Discard();return false;}
    const auto Admission=Ledger->AdmitResource({Action,1,EResourceKind::WeaponUse,1});
    if(Admission.Result!=EAdmission::Admitted){Discard();return false;}
    if(!StillCurrent()||!A->IsActionOwnedBy(Action,P))
    {Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);Discard();return false;}
    FDWCombatWeaponResourceNotice Notice;Notice.Inventory=I;Notice.Weapon=&W;Notice.Owner=P;Notice.OwnerLife=Life;
    Notice.Receipt=Admission.Receipt.Handle;Notice.Operation=EDWWeaponResourceOperation::InventoryShot;
    Notice.WeaponRevision=Revision+1;Notice.InventoryRevision=Prepared.GetExpectedRevision()+1;Notice.WeaponRestoreEpoch=Restore;
    Notice.LoadedBefore=Notice.LoadedAfter=W.CombatWeaponResources.Snapshot.LoadedShotgunShells;Notice.bInventoryChanged=true;
    const auto Emission=Ledger->CommitWeaponUseWithEmitter(Admission.Receipt.Handle,1);
    if(Emission.Result!=EEmission::Created){Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);Discard();return false;}
    // Fixed native fact commit. No delegates, spawning, movement or overridable hooks in this interval.
    I->AdoptPreparedUnchecked(MoveTemp(Prepared));++W.CombatWeaponResources.Revision;W.LastWeaponResourceReceipt=Notice.Receipt;
    W.BowPlan=Candidate;W.bBowShotCommitted=true;
    W.NextAttackTime=FMath::Max(W.BowPriorAttackLock,W.BowBoundaryWorldSeconds+Candidate.EstimateRemaining(Form));
    Bullet->ActivateUnchecked(Snapshot,*P,Life,Emission.Handle,W.BowForward,W.BowBoundaryWorldSeconds);
    A->EnqueueCommittedWeaponResource(Notice);A->DrainNotifications();
    // Publish in the release frame. Cancellation/equipment callbacks cannot undo this paid independent carrier.
    if(IsValid(Bullet)&&!Bullet->IsActorBeingDestroyed())Bullet->PublishFlightPresentation();return true;
}
