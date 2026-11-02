#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipment.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWCombatItemRegistry.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameplayHUD.h"
#include "DWGameplayWidget.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWCharacterA01Component.h"
#include "DWLanguageUIBridgeComponent.h"
#include "DWLoadingTransition.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace DWInventoryEquipment
{
// This is one real inventory-menu context, not an alternate combat-action gate.
bool Context(const ADWPlayerCharacter* P,const UDWWeaponAnimationComponent* W)
{
    if(!IsValid(P)||P->IsActorBeingDestroyed()||!P->HasActorBegunPlay()||P->IsDead()||!P->HasCombatVitalsReady()||
        P->IsCombatRestoreInProgress()||P->IsBuildingPlacementBlockingAttack()||!P->GetWorld()||P->GetWorld()->bIsTearingDown||
        !IsValid(W)||W->IsBeingDestroyed()||P->WeaponAnimationComponent.Get()!=W||W->GetOwner()!=P||W->GetWorld()!=P->GetWorld()||UDWWorldEventSubsystem::IsPlaying(P))return false;
    const auto* PC=Cast<ADWPlayerController>(P->GetController());const auto* H=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
    const auto* Screen=H?H->GetGameplayWidget():nullptr;const auto* GI=P->GetGameInstance<UDWGameInstance>();
    if(!PC||!PC->IsLocalPlayerController()||PC->GetPawn()!=P||PC->GetWorld()!=P->GetWorld()||!H||!H->IsSessionStarted()||
        H->GetMenuPage()!=EDWMenuPage::Inventory||!Screen||Screen->GetPlayer()!=P||Screen->IsInventoryDiscardPending()||
        !GI||GI->GetWorld()!=P->GetWorld()||GI->IsPlayerRestorePending())return false;
    if(const auto* Loading=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();Loading&&Loading->IsTransitionActive())return false;
    if(const auto* Bridge=UDWLanguageUIBridgeComponent::Find(PC);Bridge&&Bridge->HasModalLease())return false;
    return !P->CharacterA01||!P->CharacterA01->IsEnabled()||P->CharacterA01->CanOpenMenus();
}
}

bool UDWWeaponAnimationComponent::CanEquipInventoryItem(const FDWInventorySlotSnapshot& Slot)const
{
    const auto* P=Player();const auto* Bag=P?P->GetInventory():nullptr;
    if(!IsInGameThread()||!DWInventoryEquipment::Context(P,this)||P->bGameplayActionInProgress||P->bCombatRecoveryPositionPending||
        !bEnabled||!bEquipmentRulesReady||!CombatWeaponResources.bRulesReady||CombatWeaponResources.Revision==MAX_int64||
        !HasCurrentEquipmentIdentity()||!IsValid(Bag)||Bag->GetOwner()!=P||Slot.Quantity<1||!Bag->IsSlotSnapshotCurrent(Slot))return false;
    const auto* Binding=FDWCombatItemRegistry::FindItem(Slot.ItemId);if(!Binding)return false;
    const auto Role=FDWCombatItemRegistry::GetItemRole(Slot.ItemId);
    if(Role!=EDWCombatItemRole::PermanentWeapon&&Role!=EDWCombatItemRole::ThrowableStock)return false;
    auto Candidate=CombatWeaponResources.Snapshot;
    if(Role==EDWCombatItemRole::PermanentWeapon&&!DWCombatEquipment::OwnsPermanent(Candidate,Slot.ItemId))Candidate.OwnedPermanentWeaponIds.Add(FDWCombatItemRegistry::CanonicalWeaponItemId(Slot.ItemId));
    FString Error;return DWCombatEquipment::ValidateIdentity(Candidate,Error)&&DWCombatEquipment::CanSelect(Candidate,Binding->Presentation,Bag->CountItem(TEXT("Alcohol")));
}

bool UDWWeaponAnimationComponent::EquipInventoryItem(const FDWInventorySlotSnapshot& Slot)
{
    using namespace DWCombatCommit;
    if(!CanEquipInventoryItem(Slot))return false;
    auto* P=Player();auto* Bag=P->GetInventory();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    const auto* Binding=FDWCombatItemRegistry::FindItem(Slot.ItemId);if(!A||!A->IsOperational()||!Binding)return false;
    FName Id;if(!DWCombatEquipment::ToItemId(Binding->Presentation,Id))return false;
    const bool Claim=FDWCombatItemRegistry::GetItemRole(Slot.ItemId)==EDWCombatItemRole::PermanentWeapon&&
        !DWCombatEquipment::OwnsPermanent(CombatWeaponResources.Snapshot,Slot.ItemId);
    if(!Claim&&CombatWeaponResources.Snapshot.CurrentEquipmentId==Id&&SelectedWeapon==Binding->Presentation)return true;
    const auto Life=A->CaptureActorLife(P);if(!Life.Entity||!Life.Life)return false;
    const uint64 Epoch=A->GetWorldEpoch(),Restore=CombatWeaponResources.RestoreEpoch;const int64 Revision=CombatWeaponResources.Revision;
    const auto Previous=SelectedWeapon;const FName PreviousId=CombatWeaponResources.Snapshot.CurrentEquipmentId;
    const TWeakObjectPtr<UWorld> World(GetWorld());const TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority(A);
    const TWeakObjectPtr<UDWInventoryComponent> Inventory(Bag);const double Now=GetCombatActionWorldSeconds();
    if(!FMath::IsFinite(Now))return false;
    auto Candidate=CombatWeaponResources.Snapshot;
    if(Claim)Candidate.OwnedPermanentWeaponIds.Add(FDWCombatItemRegistry::CanonicalWeaponItemId(Slot.ItemId));
    Candidate.CurrentEquipmentId=Id;FString Error;if(!DWCombatEquipment::ValidateIdentity(Candidate,Error))return false;
    TStrongObjectPtr<ADWPlayerCharacter> KeepPlayer(P);TStrongObjectPtr<UDWWeaponAnimationComponent> KeepWeapon(this);
    TGuardValue<bool> ActionGuard(P->bGameplayActionInProgress,true);
    // Preserve selection's established cancellation semantics. No acquired item or
    // ownership fact is committed before callbacks have completed and been revalidated.
    P->CancelCombatActions();
    const auto Current=[&]()
    {
        if(!DWInventoryEquipment::Context(P,this)||P->bCombatRecoveryPositionPending||!World.IsValid()||GetWorld()!=World.Get()||
            !Authority.IsValid()||UDWCombatAuthoritySubsystem::Get(P)!=Authority.Get()||A->GetWorldEpoch()!=Epoch||
            !Inventory.IsValid()||P->GetInventory()!=Bag||Bag->GetOwner()!=P||!Bag->IsSlotSnapshotCurrent(Slot)||
            CombatWeaponResources.Revision!=Revision||CombatWeaponResources.RestoreEpoch!=Restore||SelectedWeapon!=Previous||
            CombatWeaponResources.Snapshot.CurrentEquipmentId!=PreviousId||!bEnabled||!bEquipmentRulesReady||!CombatWeaponResources.bRulesReady)return false;
        const auto Live=A->CaptureActorLife(P);return Live.Entity==Life.Entity&&Live.Life==Life.Life;
    };
    if(!Current())return false;
    FActionId Action;FLedger* Ledger=nullptr;FAdmission Admission;FDWCombatWeaponResourceNotice Notice;
    const auto End=[&](){if(Action.Id&&Authority.IsValid()&&A->GetWorldEpoch()==Epoch)A->EndAction(Action);};
    const auto Reject=[&](){if(Ledger&&Admission.Result==EAdmission::Admitted)Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);End();return false;};
    if(Claim)
    {
        Action=A->BeginAction(P);Ledger=A->GetLedgerForResourceTransaction();
        if(!Action.Id||!Ledger||!A->IsActionOwnedBy(Action,P)){End();return false;}
        Admission=Ledger->AdmitResource({Action,1,EResourceKind::EquipmentGrant,1});
        if(Admission.Result!=EAdmission::Admitted){End();return false;}
        if(!A->ReserveWeaponResourceNoticeSlot())return Reject();
        Notice.Owner=P;Notice.Weapon=this;Notice.Inventory=Bag;Notice.OwnerLife=Life;Notice.Receipt=Admission.Receipt.Handle;
        Notice.Operation=EDWWeaponResourceOperation::EquipmentGrant;Notice.LoadedBefore=Notice.LoadedAfter=Candidate.LoadedShotgunShells;
        Notice.WeaponRevision=Revision+1;Notice.InventoryRevision=Bag->GetInventoryRevision();Notice.WeaponRestoreEpoch=Restore;
    }
    if(!Current()||(Claim&&!A->IsActionOwnedBy(Action,P)))return Reject();
    if(Claim&&Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved){End();return false;}
    // Fixed native fact boundary: existing slots/loaded shells stay unchanged;
    // compatibility ownership and selected identity are one snapshot/revision.
    ClearShieldCounterFacts();CombatWeaponResources.Snapshot=MoveTemp(Candidate);++CombatWeaponResources.Revision;
    SelectedWeapon=Binding->Presentation;AmmoInMagazine=SelectedWeapon==EDWAnimatedWeapon::Shotgun?CombatWeaponResources.Snapshot.LoadedShotgunShells:0;
    NextAttackTime=FMath::Max(NextAttackTime,Now+EquipmentSwitchLockSeconds);
    bPrimaryHeld=bSecondaryHeld=false;PrimaryInputOrigin.Reset();SecondaryInputOrigin.Reset();ActiveSet=nullptr;ActiveClip={};
    if(Claim){LastWeaponResourceReceipt=Admission.Receipt.Handle;A->EnqueueCommittedWeaponResource(Notice);}End();
    ClearSimpleSpearToolSelection();RefreshDaggerEquipmentFacts();UpdateVisuals();
    if(Claim&&Authority.IsValid()&&A->GetWorldEpoch()==Epoch)A->DrainNotifications();
    return true;
}

bool UDWWeaponAnimationComponent::EquipSimpleSpearInventory(const FDWInventorySlotSnapshot& Slot)
{
 auto* P=Player();auto* Bag=P?P->GetInventory():nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(P);
 const auto* Config=P?P->GetGameplayConfig():nullptr;
 if(!P||!Bag||!A||!A->IsOperational()||!Config||Slot.ItemId!=TEXT("SimpleSugarSpear")||Slot.Quantity<1||
    !Bag->IsSlotSnapshotCurrent(Slot)||!Config->Tools.Contains(Slot.ItemId)||!bEnabled||!HasCurrentEquipmentIdentity()||
    CombatWeaponResources.Revision==MAX_int64||(!DWInventoryEquipment::Context(P,this)&&!P->IsCombatActionAllowed()))return false;
 if(HasSimpleSpearSelected())return true; // No free cooldown/action reset on selecting the same tool.
 const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch(),Restore=CombatWeaponResources.RestoreEpoch;
 const int64 Revision=CombatWeaponResources.Revision;const TWeakObjectPtr<UWorld> World(GetWorld());
 const TWeakObjectPtr<UDWInventoryComponent> KeepBag(Bag);const double Now=GetCombatActionWorldSeconds();if(!FMath::IsFinite(Now))return false;
 P->CancelCombatActions();A=UDWCombatAuthoritySubsystem::Get(P);
 if(!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||Player()!=P||!IsValid(this)||IsBeingDestroyed()||
    !A||A->GetWorldEpoch()!=Epoch||!World.IsValid()||GetWorld()!=World.Get()||World->bIsTearingDown||
    !KeepBag.IsValid()||P->GetInventory()!=Bag||!Bag->IsSlotSnapshotCurrent(Slot)||
    CombatWeaponResources.Revision!=Revision||CombatWeaponResources.RestoreEpoch!=Restore||P->GetGameplayConfig()!=Config||
    (!DWInventoryEquipment::Context(P,this)&&!P->IsCombatActionAllowed()))return false;
 const auto Live=A->CaptureActorLife(P);if(Live.Entity!=Life.Entity||Live.Life!=Life.Life)return false;
 ClearShieldCounterFacts();CombatWeaponResources.Snapshot.CurrentEquipmentId=NAME_None;++CombatWeaponResources.Revision;
 SelectedWeapon=EDWAnimatedWeapon::Unarmed;AmmoInMagazine=0;NextAttackTime=FMath::Max(NextAttackTime,Now+EquipmentSwitchLockSeconds);
 bPrimaryHeld=bSecondaryHeld=false;PrimaryInputOrigin.Reset();SecondaryInputOrigin.Reset();ActiveSet=nullptr;ActiveClip={};
 RefreshDaggerEquipmentFacts();UpdateVisuals();return true;
}
