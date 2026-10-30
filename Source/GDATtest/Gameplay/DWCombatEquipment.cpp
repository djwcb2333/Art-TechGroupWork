#include "DWCombatEquipment.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatRules.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWWeaponAnimationComponent.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWLoadingTransition.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"
#include "Misc/Guid.h"

namespace DWEquipmentCraft
{
// Crafting is allowed in the paused crafting menu; combat input suspension is not a craft rejection.
bool OwnerReady(const ADWPlayerCharacter* P)
{
    if(!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||!P->HasCombatVitalsReady()||P->IsCombatRestoreInProgress()||
        !P->GetWorld()||P->GetWorld()->bIsTearingDown||P->IsBuildingPlacementBlockingAttack()||
        UDWWorldEventSubsystem::IsPlaying(P))return false;
    if(const auto* GI=P->GetGameInstance<UDWGameInstance>())
    {
        if(GI->IsPlayerRestorePending())return false;
        if(const auto* Loading=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();Loading&&Loading->IsTransitionActive())return false;
    }
    return true;
}
}

bool DWCombatEquipment::ToPresentation(FName Id,EDWAnimatedWeapon& Out)
{
    if(Id.IsNone()){Out=EDWAnimatedWeapon::Unarmed;return true;}
    if(const auto* Binding=FDWCombatItemRegistry::FindItem(Id)){Out=Binding->Presentation;return true;}
    return false;
}
bool DWCombatEquipment::ToItemId(EDWAnimatedWeapon Weapon,FName& Out)
{
    if(Weapon==EDWAnimatedWeapon::Unarmed){Out=NAME_None;return true;}
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(Weapon);if(!Binding)return false;
    Out=Binding->ItemId;return true;
}
bool DWCombatEquipment::ValidateIdentity(const FDWCombatWeaponResourceSnapshot& S,FString& Error)
{
    TSet<FName> Mechanics;
    for(FName Id:S.OwnedPermanentWeaponIds)
    {
        if(FDWCombatItemRegistry::GetItemRole(Id)!=EDWCombatItemRole::PermanentWeapon||Mechanics.Contains(FDWCombatItemRegistry::CanonicalWeaponItemId(Id)))
        {Error=TEXT("Owned equipment contains an unregistered, consumable or duplicate alias identity.");return false;}
        Mechanics.Add(FDWCombatItemRegistry::CanonicalWeaponItemId(Id));
    }
    EDWAnimatedWeapon Weapon;if(!ToPresentation(S.CurrentEquipmentId,Weapon))
    {Error=TEXT("Current equipment identity is unknown.");return false;}
    if(Weapon!=EDWAnimatedWeapon::Unarmed&&Weapon!=EDWAnimatedWeapon::Alcohol&&
        !OwnsPermanent(S,S.CurrentEquipmentId))
    {Error=TEXT("Current permanent equipment is not owned.");return false;}
    Error.Reset();return true;
}
bool DWCombatEquipment::CanSelect(const FDWCombatWeaponResourceSnapshot& S,EDWAnimatedWeapon Weapon,int32 AlcoholStock)
{
    FString Error;FName Id;
    if(!ValidateIdentity(S,Error)||!ToItemId(Weapon,Id))return false;
    if(Weapon==EDWAnimatedWeapon::Unarmed)return true;
    if(Weapon==EDWAnimatedWeapon::Alcohol)return AlcoholStock>0||S.CurrentEquipmentId==Id;
    return OwnsPermanent(S,Id);
}
bool DWCombatEquipment::ToInventoryItemId(EDWAnimatedWeapon Weapon,FName& Out)
{if(!ToItemId(Weapon,Out))return false;Out=FDWCombatItemRegistry::PreferredInventoryItemId(Out);return true;}
bool DWCombatEquipment::OwnsPermanent(const FDWCombatWeaponResourceSnapshot& S,FName Id)
{for(FName Owned:S.OwnedPermanentWeaponIds)if(FDWCombatItemRegistry::SamePermanentWeapon(Owned,Id))return true;return false;}
bool DWCombatEquipment::BuildSwitchLock(const FDWCombatRules& Rules,double& Out,FString& Error)
{
    double Value=0.;if(!Rules.TryGetNumber(TEXT("weapon.switch_lock_s"),EDWCombatRuleUnit::Seconds,Value,Error))return false;
    if(!FMath::IsFinite(Value)||Value<0.){Error=TEXT("Invalid equipment switch clock.");return false;}
    Out=Value;Error.Reset();return true;
}
bool FDWCombatEquipmentTransaction::CanCraft(const UDWInventoryComponent& Inventory,FName ItemId)
{
    auto* P=Cast<ADWPlayerCharacter>(Inventory.GetOwner());
    auto* W=P?P->WeaponAnimationComponent.Get():nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(P);
    return Inventory.CanWriteInventory()&&P&&IsValid(W)&&!W->IsBeingDestroyed()&&W->Player()==P&&
        P->GetInventory()==&Inventory&&DWEquipmentCraft::OwnerReady(P)&&A&&A->IsOperational()&&
        W->CombatWeaponResources.bRulesReady&&W->CombatWeaponResources.Revision<MAX_int64&&
        FDWCombatItemRegistry::GetItemRole(ItemId)==EDWCombatItemRole::PermanentWeapon&&
        !DWCombatEquipment::OwnsPermanent(W->CombatWeaponResources.Snapshot,ItemId)&&
        !Inventory.GetSlots().ContainsByPredicate([ItemId](const FDWItemStack& S){return FDWCombatItemRegistry::SamePermanentWeapon(S.ItemId,ItemId)&&S.Quantity>0;});
}

bool FDWCombatEquipmentTransaction::TryCraft(UDWWeaponAnimationComponent& W,FName Recipe,bool Yeast)
{
    auto* P=W.Player();auto* Inventory=P?P->GetInventory():nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(P);
    FName Item;
    if(!A||!Inventory||!Inventory->GetCraftPermanentWeaponOutput(Recipe,Item)||Item.IsNone()||!CanCraft(*Inventory,Item))return false;
    const uint64 Epoch=A->GetWorldEpoch();const TWeakObjectPtr<UDWCombatAuthoritySubsystem> SafeAuthority(A);
    const FDWCombatEquipmentGrantRequest Request{A->BeginAction(P),A->CaptureActorLife(P),1,W.GetCombatWeaponRestoreEpoch(),Item};
    const bool Committed=Craft(W,Request,Recipe,Yeast).bCommittedThisCall;
    // A notification may destroy the player or restore the world. No owner/component access here.
    if(SafeAuthority.IsValid()&&SafeAuthority->GetWorldEpoch()==Epoch)SafeAuthority->EndAction(Request.Action);
    return Committed;
}

FDWCombatEquipmentGrantResult FDWCombatEquipmentTransaction::Grant(UDWWeaponAnimationComponent& W,const FDWCombatEquipmentGrantRequest& R)
{return Commit(W,R,NAME_None,false);}

FDWCombatEquipmentGrantResult FDWCombatEquipmentTransaction::Craft(UDWWeaponAnimationComponent& W,const FDWCombatEquipmentGrantRequest& R,FName Recipe,bool Yeast)
{return Recipe.IsNone()?FDWCombatEquipmentGrantResult{}:Commit(W,R,Recipe,Yeast);}

FDWCombatEquipmentGrantResult FDWCombatEquipmentTransaction::Commit(UDWWeaponAnimationComponent& W,const FDWCombatEquipmentGrantRequest& R,FName Recipe,bool Yeast)
{
    using namespace DWCombatCommit;
    FDWCombatEquipmentGrantResult Result;auto* P=W.Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    auto* Inventory=P?P->GetInventory():nullptr;
    const bool bCraft=!Recipe.IsNone();
    if(!A||!IsInGameThread()||!IsValid(&W)||W.IsBeingDestroyed()||!P||
        (bCraft?!DWEquipmentCraft::OwnerReady(P):!P->IsCombatActionAllowed())||
        !Inventory||!Inventory->CanWriteInventory()||Inventory->GetOwner()!=P||
        !W.CombatWeaponResources.bRulesReady||W.CombatWeaponResources.Revision==MAX_int64||
        R.RestoreEpoch!=W.CombatWeaponResources.RestoreEpoch||!R.OperationOrdinal||
        FDWCombatItemRegistry::GetItemRole(R.PermanentItemId)!=EDWCombatItemRole::PermanentWeapon||
        !A->IsActionOwnedBy(R.Action,P))return Result;
    const auto Life=A->CaptureActorLife(P);
    if(!Life.Entity||Life.Entity!=R.OwnerLife.Entity||Life.Life!=R.OwnerLife.Life)return Result;
    const uint64 Epoch=A->GetWorldEpoch();const int64 WeaponRevision=W.CombatWeaponResources.Revision;
    auto* Ledger=A->GetLedgerForResourceTransaction();if(!Ledger)return Result;
    Result.Admission=Ledger->AdmitResource({R.Action,1,EResourceKind::EquipmentGrant,R.OperationOrdinal});
    if(Result.Admission.Result!=EAdmission::Admitted)return Result;
    const auto Reject=[&]()
    {
        Ledger->Resolve(Result.Admission.Receipt.Handle,EOutcome::Rejected);
        if(const auto Receipt=Ledger->FindReceipt(Result.Admission.Receipt.Handle))Result.Admission.Receipt=*Receipt;
        return Result;
    };
    Result.bAlreadyOwned=DWCombatEquipment::OwnsPermanent(W.CombatWeaponResources.Snapshot,R.PermanentItemId);
    if(Result.bAlreadyOwned)return Reject();
    // Prepare only: neither a full bag nor a rejected receipt can consume existing items or grant ownership.
    FDWPreparedInventoryChange InventoryPlan;
    if(Recipe.IsNone())
    {if(!Inventory->PrepareAddItem(R.PermanentItemId,1,InventoryPlan))return Reject();}
    else if(!CanCraft(*Inventory,R.PermanentItemId)||!Inventory->PrepareCraftRecipe(Recipe,Yeast,InventoryPlan)||
        InventoryPlan.GetPermanentWeaponOutput()!=R.PermanentItemId)return Reject();
    auto Candidate=W.CombatWeaponResources.Snapshot;Candidate.OwnedPermanentWeaponIds.Add(FDWCombatItemRegistry::CanonicalWeaponItemId(R.PermanentItemId));
    FString Error;if(!DWCombatEquipment::ValidateIdentity(Candidate,Error)||!A->ReserveWeaponResourceNoticeSlot())return Reject();
    FDWCombatWeaponResourceNotice Notice;Notice.Owner=P;Notice.Weapon=&W;Notice.Inventory=Inventory;
    Notice.OwnerLife=Life;Notice.Receipt=Result.Admission.Receipt.Handle;Notice.Operation=EDWWeaponResourceOperation::EquipmentGrant;
    Notice.bInventoryChanged=true;Notice.LoadedBefore=Notice.LoadedAfter=Candidate.LoadedShotgunShells;
    Notice.InventoryRevision=InventoryPlan.GetExpectedRevision()+1;
    Notice.WeaponRevision=WeaponRevision+1;Notice.WeaponRestoreEpoch=R.RestoreEpoch;
    // The prepared bag and the owned set share one admitted operation and one notification.
    // All native rejection checks and allocations precede the fixed no-callback fact commit.
    if(!Inventory->ValidatePreparedForCommit(InventoryPlan))return Reject();
    // Recheck both revisions after the native station validator, before the no-callback commit.
    if(!IsValid(&W)||W.IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||W.Player()!=P||
        P->GetInventory()!=Inventory||Inventory->GetOwner()!=P||(bCraft?!DWEquipmentCraft::OwnerReady(P):!P->IsCombatActionAllowed())||
        UDWCombatAuthoritySubsystem::Get(P)!=A||A->GetWorldEpoch()!=Epoch||!A->IsActionOwnedBy(R.Action,P)||
        W.CombatWeaponResources.Revision!=WeaponRevision||W.CombatWeaponResources.RestoreEpoch!=R.RestoreEpoch||
        Inventory->GetInventoryRevision()!=InventoryPlan.GetExpectedRevision()||
        (!Recipe.IsNone()&&!CanCraft(*Inventory,R.PermanentItemId)))return Reject();
    const auto CurrentLife=A->CaptureActorLife(P);
    if(CurrentLife.Entity!=Life.Entity||CurrentLife.Life!=Life.Life)return Reject();
    if(Ledger->Resolve(Result.Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved)return Result;
    Inventory->AdoptPreparedUnchecked(MoveTemp(InventoryPlan));
    W.CombatWeaponResources.Snapshot=MoveTemp(Candidate);++W.CombatWeaponResources.Revision;
    W.LastWeaponResourceReceipt=Result.Admission.Receipt.Handle;
    A->EnqueueCommittedWeaponResource(Notice);Result.bCommittedThisCall=true;Result.Admission.Receipt.State=EReceiptState::Committed;
    A->DrainNotifications();return Result; // No owner/component access after callbacks.
}

FDWCombatEquipmentStarterResult FDWCombatEquipmentTransaction::GrantStarterBatch(UDWWeaponAnimationComponent& W,
    const TArray<FDWItemStack>& Items,const FString& Playthrough,FDWCombatStarterGrantState& Receipt)
{
    using namespace DWCombatCommit;FDWCombatEquipmentStarterResult Result;FGuid Guid;
    auto* P=W.Player();auto* Inventory=P?P->GetInventory():nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(P);
    auto Invalid=[&](const TCHAR* Error){Result.Error=Error;return Result;};
    if(!FGuid::ParseExact(Playthrough,EGuidFormats::Digits,Guid)||!Guid.IsValid()||Receipt.Revision<0||Receipt.Revision==MAX_int64||
        (!Receipt.PlaythroughId.IsEmpty()&&Receipt.PlaythroughId!=Playthrough)||Items.IsEmpty())return Invalid(TEXT("Invalid starter playthrough, receipt or empty batch."));
    if(Receipt.bGranted)
    {if(Receipt.PlaythroughId!=Playthrough)return Invalid(TEXT("Starter receipt belongs to another playthrough."));Result.bAlreadyGranted=true;return Result;}
    if(!A||!IsInGameThread()||!IsValid(&W)||W.IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||
        !P->HasCombatVitalsReady()||P->IsCombatRestoreInProgress()||!Inventory||!Inventory->CanWriteInventory()||Inventory->GetOwner()!=P||
        !W.CombatWeaponResources.bRulesReady||W.CombatWeaponResources.Revision==MAX_int64)
        return Invalid(TEXT("Starter acquisition requires the live native trial player and writable resources."));
    const auto Life=A->CaptureActorLife(P);const auto Action=A->BeginAction(P);
    if(!Life.Entity||!Action.Id||!A->IsActionOwnedBy(Action,P)){if(Action.Id)A->EndAction(Action);return Invalid(TEXT("Starter action could not be admitted."));}
    const uint64 WorldEpoch=A->GetWorldEpoch(),RestoreEpoch=W.CombatWeaponResources.RestoreEpoch;
    const int64 WeaponRevision=W.CombatWeaponResources.Revision,ReceiptRevision=Receipt.Revision;
    const FString ExpectedReceiptPlaythrough=Receipt.PlaythroughId;
    const TWeakObjectPtr<UDWCombatAuthoritySubsystem> SafeAuthority(A);
    auto* Ledger=A->GetLedgerForResourceTransaction();
    auto End=[&](){if(SafeAuthority.IsValid()&&SafeAuthority->GetWorldEpoch()==WorldEpoch)SafeAuthority->EndAction(Action);};
    if(!Ledger){End();return Invalid(TEXT("Starter ledger unavailable."));}
    Result.Admission=Ledger->AdmitResource({Action,1,EResourceKind::EquipmentGrant,1});
    if(Result.Admission.Result!=EAdmission::Admitted){End();return Invalid(TEXT("Starter resource admission rejected."));}
    auto Reject=[&](const TCHAR* Error)
    {
        Ledger->Resolve(Result.Admission.Receipt.Handle,EOutcome::Rejected);
        if(const auto Updated=Ledger->FindReceipt(Result.Admission.Receipt.Handle))Result.Admission.Receipt=*Updated;
        End();return Invalid(Error);
    };
    auto Slots=Inventory->GetSlots();auto Candidate=W.CombatWeaponResources.Snapshot;TSet<FName> Seen;
    const auto* SpearBinding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Spear);
    if(!SpearBinding)return Reject(TEXT("Missing spear mechanics binding."));const FName Spear=SpearBinding->ItemId;
    for(const auto& Item:Items)
    {
        if(Item.ItemId.IsNone()||Item.Quantity<1||Seen.Contains(Item.ItemId)||!Inventory->GetConfig()->GetItemDefinition(Item.ItemId))
            return Reject(TEXT("Unknown, duplicate or nonpositive starter item."));
        Seen.Add(Item.ItemId);
        if(FDWCombatItemRegistry::SamePermanentWeapon(Item.ItemId,Spear))return Reject(TEXT("Spear must be crafted; starter batch may not include either spear inventory alias."));
        if(FDWCombatItemRegistry::GetItemRole(Item.ItemId)==EDWCombatItemRole::PermanentWeapon)
        {
            if(DWCombatEquipment::OwnsPermanent(Candidate,Item.ItemId)||
                Inventory->GetSlots().ContainsByPredicate([&](const FDWItemStack& Stack){return FDWCombatItemRegistry::SamePermanentWeapon(Stack.ItemId,Item.ItemId);}))
            {Result.bAlreadyOwned=DWCombatEquipment::OwnsPermanent(Candidate,Item.ItemId);return Reject(TEXT("Starter permanent identity is duplicate, owned or has an existing physical item."));}
            Candidate.OwnedPermanentWeaponIds.Add(FDWCombatItemRegistry::CanonicalWeaponItemId(Item.ItemId));
        }
        if(!Inventory->AddToSlots(Slots,Item.ItemId,Item.Quantity))return Reject(TEXT("Starter batch does not fit in the existing inventory; no partial grant."));
    }
    for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
    {
        if(Binding.Presentation!=EDWAnimatedWeapon::Spear&&!Seen.Contains(Binding.ItemId))return Reject(TEXT("Starter batch omits a required permanent weapon or Alcohol stock."));
        if(!Binding.AmmoItemId.IsNone()&&!Seen.Contains(Binding.AmmoItemId))return Reject(TEXT("Starter batch omits required reserve ammunition."));
    }
    FDWPreparedInventoryChange InventoryPlan;
    if(!Inventory->MakePrepared(MoveTemp(Slots),InventoryPlan))return Reject(TEXT("Starter inventory candidate is invalid."));
    FString Error;if(!DWCombatEquipment::ValidateIdentity(Candidate,Error)||!A->ReserveWeaponResourceNoticeSlot())return Reject(TEXT("Starter ownership candidate or notification allocation failed."));
    FDWCombatStarterGrantState ReceiptCandidate=Receipt;ReceiptCandidate.PlaythroughId=Playthrough;ReceiptCandidate.bGranted=true;++ReceiptCandidate.Revision;
    FDWCombatWeaponResourceNotice Notice;Notice.Owner=P;Notice.Weapon=&W;Notice.Inventory=Inventory;Notice.OwnerLife=Life;
    Notice.Receipt=Result.Admission.Receipt.Handle;Notice.Operation=EDWWeaponResourceOperation::EquipmentGrant;Notice.bInventoryChanged=true;
    Notice.LoadedBefore=Notice.LoadedAfter=Candidate.LoadedShotgunShells;Notice.WeaponRevision=WeaponRevision+1;
    Notice.InventoryRevision=InventoryPlan.GetExpectedRevision()+1;Notice.WeaponRestoreEpoch=RestoreEpoch;
    // Final source checks precede all three facts. No callbacks or allocations occur between adoptions.
    if(!Inventory->ValidatePreparedForCommit(InventoryPlan)||!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||P->IsCombatRestoreInProgress()||
        !IsValid(&W)||W.IsBeingDestroyed()||W.Player()!=P||P->GetInventory()!=Inventory||Inventory->GetOwner()!=P||
        UDWCombatAuthoritySubsystem::Get(P)!=A||A->GetWorldEpoch()!=WorldEpoch||!A->IsActionOwnedBy(Action,P)||
        W.CombatWeaponResources.Revision!=WeaponRevision||W.CombatWeaponResources.RestoreEpoch!=RestoreEpoch||
        Receipt.Revision!=ReceiptRevision||Receipt.bGranted||Receipt.PlaythroughId!=ExpectedReceiptPlaythrough)
        return Reject(TEXT("Starter sources changed while preparing the batch."));
    const auto Current=A->CaptureActorLife(P);if(Current.Entity!=Life.Entity||Current.Life!=Life.Life)return Reject(TEXT("Starter owner life changed."));
    if(Ledger->Resolve(Result.Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved){End();return Invalid(TEXT("Starter commit receipt could not resolve."));}
    Inventory->AdoptPreparedUnchecked(MoveTemp(InventoryPlan));W.CombatWeaponResources.Snapshot=MoveTemp(Candidate);++W.CombatWeaponResources.Revision;
    Receipt=MoveTemp(ReceiptCandidate);W.LastWeaponResourceReceipt=Result.Admission.Receipt.Handle;
    A->EnqueueCommittedWeaponResource(Notice);Result.bCommittedThisCall=true;Result.Admission.Receipt.State=EReceiptState::Committed;
    A->DrainNotifications();End();return Result; // No Weapon/Inventory/player/receipt access after callbacks.
}

bool FDWCombatEquipmentTransaction::ClaimExistingPermanent(UDWWeaponAnimationComponent& W,const FDWInventorySlotSnapshot& Slot)
{
    using namespace DWCombatCommit;auto* P=W.Player();auto* Inventory=P?P->GetInventory():nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(P);
    if(!A||!IsInGameThread()||!IsValid(&W)||W.IsBeingDestroyed()||!DWEquipmentCraft::OwnerReady(P)||!Inventory||
        !Inventory->CanWriteInventory()||Inventory->GetOwner()!=P||!Inventory->IsSlotSnapshotCurrent(Slot)||Slot.Quantity<1||
        FDWCombatItemRegistry::GetItemRole(Slot.ItemId)!=EDWCombatItemRole::PermanentWeapon||!W.CombatWeaponResources.bRulesReady||
        W.CombatWeaponResources.Revision==MAX_int64)return false;
    if(DWCombatEquipment::OwnsPermanent(W.CombatWeaponResources.Snapshot,Slot.ItemId))return true;
    const auto Life=A->CaptureActorLife(P);const auto Action=A->BeginAction(P);const uint64 Epoch=A->GetWorldEpoch(),Restore=W.CombatWeaponResources.RestoreEpoch;
    const int64 Revision=W.CombatWeaponResources.Revision;const TWeakObjectPtr<UDWCombatAuthoritySubsystem> SafeAuthority(A);
    auto End=[&](){if(SafeAuthority.IsValid()&&SafeAuthority->GetWorldEpoch()==Epoch)SafeAuthority->EndAction(Action);};
    auto* Ledger=A->GetLedgerForResourceTransaction();if(!Life.Entity||!Action.Id||!Ledger||!A->IsActionOwnedBy(Action,P)){End();return false;}
    const auto Admission=Ledger->AdmitResource({Action,1,EResourceKind::EquipmentGrant,1});if(Admission.Result!=EAdmission::Admitted){End();return false;}
    auto Reject=[&](){Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);End();return false;};
    auto Candidate=W.CombatWeaponResources.Snapshot;Candidate.OwnedPermanentWeaponIds.Add(FDWCombatItemRegistry::CanonicalWeaponItemId(Slot.ItemId));
    FString Error;if(!DWCombatEquipment::ValidateIdentity(Candidate,Error)||!A->ReserveWeaponResourceNoticeSlot())return Reject();
    FDWCombatWeaponResourceNotice Notice;Notice.Owner=P;Notice.Weapon=&W;Notice.Inventory=Inventory;Notice.OwnerLife=Life;Notice.Receipt=Admission.Receipt.Handle;
    Notice.Operation=EDWWeaponResourceOperation::EquipmentGrant;Notice.LoadedBefore=Notice.LoadedAfter=Candidate.LoadedShotgunShells;
    Notice.WeaponRevision=Revision+1;Notice.InventoryRevision=Inventory->GetInventoryRevision();Notice.WeaponRestoreEpoch=Restore;
    if(!IsValid(&W)||W.IsBeingDestroyed()||!IsValid(P)||W.Player()!=P||!DWEquipmentCraft::OwnerReady(P)||P->GetInventory()!=Inventory||
        UDWCombatAuthoritySubsystem::Get(P)!=A||A->GetWorldEpoch()!=Epoch||!A->IsActionOwnedBy(Action,P)||
        W.CombatWeaponResources.Revision!=Revision||W.CombatWeaponResources.RestoreEpoch!=Restore||!Inventory->IsSlotSnapshotCurrent(Slot))return Reject();
    const auto Current=A->CaptureActorLife(P);if(Current.Entity!=Life.Entity||Current.Life!=Life.Life)return Reject();
    if(Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved){End();return false;}
    W.CombatWeaponResources.Snapshot=MoveTemp(Candidate);++W.CombatWeaponResources.Revision;W.LastWeaponResourceReceipt=Admission.Receipt.Handle;
    A->EnqueueCommittedWeaponResource(Notice);A->DrainNotifications();End();return true;
}

FDWCombatEquipmentStarterResult FDWCombatEquipmentTransaction::GrantFormalStarterBatch(
    UDWWeaponAnimationComponent& W,UDWGameInstance& GI,const TArray<FDWItemStack>& Items,
    const FString& Playthrough)
{
    using namespace DWCombatCommit;
    FDWCombatEquipmentStarterResult Result;FGuid Guid;
    auto Invalid=[&](const TCHAR* Error){Result.Error=Error;return Result;};
    auto* P=W.Player();auto* Inventory=P?P->GetInventory():nullptr;
    auto* A=UDWCombatAuthoritySubsystem::Get(P);
    // CanGrantFormalStarter admits the already-granted no-op for the matching
    // formal owner, but a not-yet-granted owner additionally needs the GI's
    // one Pending opening startup admission (slot/generation/world/life/epoch).
    // A caller cannot manufacture a receipt owner by passing a writable reference.
    const auto ReceiptOwner=GI.FormalStarterGrant;
    if(!IsInGameThread()||!FGuid::ParseExact(Playthrough,EGuidFormats::Digits,Guid)||!Guid.IsValid()||
        !IsValid(&GI)||!IsValid(P)||P->GetGameInstance<UDWGameInstance>()!=&GI||
        !ReceiptOwner.IsValid()||GI.FormalPlaythroughId!=Playthrough||
        ReceiptOwner->PlaythroughId!=Playthrough||ReceiptOwner->Revision<0||
        !GI.CanGrantFormalStarter(P,Playthrough))
        return Invalid(TEXT("Formal starter request does not belong to the admitted new-adventure owner."));
    if(ReceiptOwner->bGranted){Result.bAlreadyGranted=true;return Result;}
    if(ReceiptOwner->Revision==MAX_int64||Items.IsEmpty()||!A||!A->IsOperational()||
        !IsValid(&W)||W.IsBeingDestroyed()||P->IsActorBeingDestroyed()||P->IsDead()||
        !P->HasCombatVitalsReady()||P->IsCombatRestoreInProgress()||!Inventory||
        !Inventory->CanWriteInventory()||Inventory->GetOwner()!=P||!Inventory->GetConfig()||
        !W.CombatWeaponResources.bRulesReady||W.CombatWeaponResources.Revision==MAX_int64)
        return Invalid(TEXT("Formal starter resources are not writable."));

    const auto Life=A->CaptureActorLife(P);const auto Action=A->BeginAction(P);
    if(!Life.Entity||!Life.Life||!Action.Id||!A->IsActionOwnedBy(Action,P))
    {if(Action.Id)A->EndAction(Action);return Invalid(TEXT("Formal starter owner action could not be admitted."));}
    const uint64 WorldEpoch=A->GetWorldEpoch(),RestoreEpoch=W.CombatWeaponResources.RestoreEpoch;
    const int64 WeaponRevision=W.CombatWeaponResources.Revision,ReceiptRevision=ReceiptOwner->Revision;
    const TWeakObjectPtr<UDWCombatAuthoritySubsystem> SafeAuthority(A);
    auto End=[&](){if(SafeAuthority.IsValid()&&SafeAuthority->GetWorldEpoch()==WorldEpoch)SafeAuthority->EndAction(Action);};
    auto* Ledger=A->GetLedgerForResourceTransaction();
    if(!Ledger){End();return Invalid(TEXT("Formal starter ledger is unavailable."));}
    Result.Admission=Ledger->AdmitResource({Action,1,EResourceKind::EquipmentGrant,1});
    if(Result.Admission.Result!=EAdmission::Admitted){End();return Invalid(TEXT("Formal starter resource admission rejected."));}
    auto Reject=[&](const TCHAR* Error)
    {
        Ledger->Resolve(Result.Admission.Receipt.Handle,EOutcome::Rejected);
        if(const auto Updated=Ledger->FindReceipt(Result.Admission.Receipt.Handle))Result.Admission.Receipt=*Updated;
        End();return Invalid(Error);
    };

    auto Slots=Inventory->GetSlots();auto Candidate=W.CombatWeaponResources.Snapshot;
    TSet<FName> Seen;
    for(const auto& Item:Items)
    {
        const auto Role=FDWCombatItemRegistry::GetItemRole(Item.ItemId);
        const bool bPermanent=Role==EDWCombatItemRole::PermanentWeapon;
        const FName Canonical=bPermanent?FDWCombatItemRegistry::CanonicalWeaponItemId(Item.ItemId):Item.ItemId;
        const FName InventoryId=bPermanent?FDWCombatItemRegistry::PreferredInventoryItemId(Canonical):Item.ItemId;
        if(Item.ItemId.IsNone()||Item.Quantity<1||Seen.Contains(Canonical)||
            (!bPermanent&&Role!=EDWCombatItemRole::ReserveAmmo&&Role!=EDWCombatItemRole::ThrowableStock)||
            (bPermanent&&Item.Quantity!=1)||!Inventory->GetConfig()->GetItemDefinition(InventoryId))
            return Reject(TEXT("Formal starter manifest contains an unknown, duplicate or invalid weapon/stock row."));
        Seen.Add(Canonical);
        if(bPermanent)
        {
            if(DWCombatEquipment::OwnsPermanent(Candidate,Canonical)||
                Inventory->GetSlots().ContainsByPredicate([&](const FDWItemStack& S){return FDWCombatItemRegistry::SamePermanentWeapon(S.ItemId,Canonical);}))
            {
                Result.bAlreadyOwned=DWCombatEquipment::OwnsPermanent(Candidate,Canonical);
                return Reject(TEXT("Uncommitted formal starter owner already has a target permanent weapon; no duplicate or repair grant."));
            }
            Candidate.OwnedPermanentWeaponIds.Add(Canonical);
        }
        if(!Inventory->AddToSlots(Slots,InventoryId,Item.Quantity))
            return Reject(TEXT("The complete formal starter batch does not fit; no inventory or ownership changed."));
    }
    for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
    {
        if(!Seen.Contains(FDWCombatItemRegistry::CanonicalWeaponItemId(Binding.ItemId)))
            return Reject(TEXT("Formal starter batch omits a required permanent weapon or Alcohol stock."));
        if(!Binding.AmmoItemId.IsNone()&&!Seen.Contains(Binding.AmmoItemId))
            return Reject(TEXT("Formal starter batch omits required reserve ammunition."));
    }
    FDWPreparedInventoryChange InventoryPlan;
    if(!Inventory->MakePrepared(MoveTemp(Slots),InventoryPlan))return Reject(TEXT("Formal starter inventory candidate is invalid."));
    FString Error;
    if(!DWCombatEquipment::ValidateIdentity(Candidate,Error)||!A->ReserveWeaponResourceNoticeSlot())
        return Reject(TEXT("Formal starter ownership or notification preparation failed."));
    FDWCombatStarterGrantState ReceiptCandidate=*ReceiptOwner;
    ReceiptCandidate.bGranted=true;++ReceiptCandidate.Revision;
    FDWCombatWeaponResourceNotice Notice;Notice.Owner=P;Notice.Weapon=&W;Notice.Inventory=Inventory;Notice.OwnerLife=Life;
    Notice.Receipt=Result.Admission.Receipt.Handle;Notice.Operation=EDWWeaponResourceOperation::EquipmentGrant;Notice.bInventoryChanged=true;
    Notice.LoadedBefore=Notice.LoadedAfter=Candidate.LoadedShotgunShells;
    Notice.WeaponRevision=WeaponRevision+1;Notice.InventoryRevision=InventoryPlan.GetExpectedRevision()+1;Notice.WeaponRestoreEpoch=RestoreEpoch;

    // GI owner identity + its Pending admission are checked again after every
    // native preparation step. No callbacks or allocations separate the three adoptions.
    if(!Inventory->ValidatePreparedForCommit(InventoryPlan)||!IsValid(&GI)||!IsValid(P)||P->IsActorBeingDestroyed()||
        P->IsDead()||P->IsCombatRestoreInProgress()||P->GetGameInstance<UDWGameInstance>()!=&GI||
        !IsValid(&W)||W.IsBeingDestroyed()||W.Player()!=P||P->GetInventory()!=Inventory||Inventory->GetOwner()!=P||
        UDWCombatAuthoritySubsystem::Get(P)!=A||A->GetWorldEpoch()!=WorldEpoch||!A->IsActionOwnedBy(Action,P)||
        W.CombatWeaponResources.Revision!=WeaponRevision||W.CombatWeaponResources.RestoreEpoch!=RestoreEpoch||
        Inventory->GetInventoryRevision()!=InventoryPlan.GetExpectedRevision()||
        GI.FormalStarterGrant!=ReceiptOwner||GI.FormalPlaythroughId!=Playthrough||
        ReceiptOwner->PlaythroughId!=Playthrough||ReceiptOwner->Revision!=ReceiptRevision||ReceiptOwner->bGranted||
        !GI.CanGrantFormalStarter(P,Playthrough))
        return Reject(TEXT("Formal starter owner, admission or resources changed during preparation."));
    const auto CurrentLife=A->CaptureActorLife(P);
    if(CurrentLife.Entity!=Life.Entity||CurrentLife.Life!=Life.Life)return Reject(TEXT("Formal starter owner life changed."));
    if(Ledger->Resolve(Result.Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved)
    {End();return Invalid(TEXT("Formal starter resource receipt could not resolve."));}
    Inventory->AdoptPreparedUnchecked(MoveTemp(InventoryPlan));
    W.CombatWeaponResources.Snapshot=MoveTemp(Candidate);++W.CombatWeaponResources.Revision;
    *ReceiptOwner=MoveTemp(ReceiptCandidate);W.LastWeaponResourceReceipt=Result.Admission.Receipt.Handle;
    A->EnqueueCommittedWeaponResource(Notice);Result.bCommittedThisCall=true;Result.Admission.Receipt.State=EReceiptState::Committed;
    A->DrainNotifications();End();return Result; // No GI/player/component/owner access after callbacks.
}
