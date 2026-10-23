#include "DWCombatBottleTransaction.h"
#include "DWCombatBottleProjectile.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatGroundAreaSubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWCombatItemRegistry.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
bool FDWCombatBottleTransaction::Commit(UDWWeaponAnimationComponent& W,double Offset)
{
    using namespace DWCombatCommit;
    auto* P=W.Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);auto* I=P?P->GetInventory():nullptr;
    const auto Action=W.BottleAction;
    if(!A||!P||!I||!W.IsCurrentFormalBottle(Action)||W.BottlePlan.GetPhase()!=EDWBottlePhase::Release||W.bBottleCommitted||
        !I->CanWriteInventory()||I->GetOwner()!=P||W.CombatWeaponResources.Revision==MAX_int64||!UDWCombatGroundAreaSubsystem::Get(P))return false;
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Alcohol);
    if(!Binding||Binding->ItemRole!=EDWCombatItemRole::ThrowableStock||Binding->ItemId.IsNone())return false;
    FDWPreparedInventoryChange Prepared;if(!I->PrepareRemoveItem(Binding->ItemId,W.BottleConfig.StockPerThrow,Prepared))return false;
    const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch(),Restore=W.CombatWeaponResources.RestoreEpoch;
    const int64 Revision=W.CombatWeaponResources.Revision;const int32 Serial=W.ActionSerial;
    const TWeakObjectPtr<UWorld> World(P->GetWorld());
    const FVector Start=P->GetActorLocation()+P->GetActorTransform().TransformVectorNoScale(P->ThrowOriginOffset);
    if(Start.ContainsNaN())return false;
    const FTransform SpawnTransform(W.BottleForward.Rotation(),Start);
    auto* Bottle=World->SpawnActorDeferred<ADWCombatBottleProjectile>(ADWCombatBottleProjectile::StaticClass(),SpawnTransform,
        P,P,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Bottle)return false;
    Bottle->FinishSpawning(SpawnTransform); // All staging callbacks precede stock adoption.
    const auto Current=[&]()
    {
        auto* CurrentA=UDWCombatAuthoritySubsystem::Get(P);
        if(!World.IsValid()||World->bIsTearingDown||!IsValid(Bottle)||Bottle->IsActorBeingDestroyed()||Bottle->IsReleased()||
            !IsValid(&W)||W.IsBeingDestroyed()||!CurrentA||CurrentA->GetWorldEpoch()!=Epoch||
            !W.IsCurrentFormalBottle(Action)||W.Player()!=P||P->GetWorld()!=World.Get()||W.ActionSerial!=Serial||
            W.BottlePlan.GetPhase()!=EDWBottlePhase::Release||W.bBottleCommitted||
            W.CombatWeaponResources.Revision!=Revision||W.CombatWeaponResources.RestoreEpoch!=Restore||!I->ValidatePreparedForCommit(Prepared))return false;
        const auto L=CurrentA->CaptureActorLife(P);return L.Entity==Life.Entity&&L.Life==Life.Life;
    };
    const auto Discard=[&](){if(IsValid(Bottle)&&!Bottle->IsActorBeingDestroyed()&&World.IsValid()&&!World->bIsTearingDown)Bottle->Destroy();};
    if(!Current()){Discard();return false;}
    Bottle->CopyAuthoredVisual(*P);if(!Current()){Discard();return false;}
    const auto Form=W.ReadFormalSpearForm(Offset);const auto Snapshot=W.BottleConfig.Shot(Form);
    const FVector Clamped=DWBottle::ClampHorizontalTarget(P->GetActorLocation(),W.BottleRequestedTarget,Snapshot.RangeCm);
    const double FloorZ=P->GetCharacterMovement()->GetWalkableFloorZ();FVector Ground;
    FDWBottlePlan Candidate=W.BottlePlan;
    if(!Snapshot.IsValid()||!Candidate.EnterNext(Form)||!ADWCombatBottleProjectile::FindLegalGround(World.Get(),P,Clamped,
        Snapshot.RangeCm+Snapshot.ArcHeightCm,Snapshot.CollisionRadiusCm,FloorZ,Ground)){Discard();return false;}
    A=UDWCombatAuthoritySubsystem::Get(P);auto* Ledger=A?A->GetLedgerForResourceTransaction():nullptr;
    if(!Ledger||!A->ReserveWeaponResourceNoticeSlot()){Discard();return false;}
    const auto Admission=Ledger->AdmitResource({Action,1,EResourceKind::WeaponUse,1});
    if(Admission.Result!=EAdmission::Admitted){Discard();return false;}
    if(!Current()||!A->IsActionOwnedBy(Action,P))
    {Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);Discard();return false;}
    FDWCombatWeaponResourceNotice Notice;Notice.Inventory=I;Notice.Weapon=&W;Notice.Owner=P;Notice.OwnerLife=Life;
    Notice.Receipt=Admission.Receipt.Handle;Notice.Operation=EDWWeaponResourceOperation::InventoryShot;
    Notice.WeaponRevision=Revision+1;Notice.InventoryRevision=Prepared.GetExpectedRevision()+1;Notice.WeaponRestoreEpoch=Restore;
    Notice.LoadedBefore=Notice.LoadedAfter=W.CombatWeaponResources.Snapshot.LoadedShotgunShells;Notice.bInventoryChanged=true;
    const auto Emission=Ledger->CommitWeaponUseWithEmitter(Admission.Receipt.Handle,1);
    if(Emission.Result!=EEmission::Created){Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);Discard();return false;}
    // No spawn, callbacks, transforms or visuals between receipt resolution and all adopted facts.
    I->AdoptPreparedUnchecked(MoveTemp(Prepared));++W.CombatWeaponResources.Revision;W.LastWeaponResourceReceipt=Notice.Receipt;
    W.BottlePlan=Candidate;W.bBottleCommitted=true;
    W.NextAttackTime=FMath::Max(W.BottlePriorAttackLock,W.BottleBoundaryWorldSeconds+Candidate.EstimateRemaining(Form));
    Bottle->ActivateUnchecked(Snapshot,*P,Life,Emission.Handle,Start,Ground,W.BottleBoundaryWorldSeconds,FloorZ);
    A->EnqueueCommittedWeaponResource(Notice);A->DrainNotifications();return true;
}
