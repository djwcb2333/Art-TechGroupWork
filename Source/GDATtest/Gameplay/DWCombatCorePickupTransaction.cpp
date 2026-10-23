#include "DWCombatCorePickupTransaction.h"
#include "DWCombatCorePickup.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWEnemyCharacter.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool FDWCombatCorePickupTransaction::Commit(ADWCombatCorePickup& P,ADWPlayerCharacter& Player)
{
    using namespace DWCombatCommit;
    auto* Drops=UDWCombatCoreDropSubsystem::Get(&P);auto* Authority=UDWCombatAuthoritySubsystem::Get(&Player);
    auto* Inventory=Player.GetInventory();
    if(!Drops||!Authority||!Inventory||!Inventory->CanWriteInventory()||Inventory->GetOwner()!=&Player||
        Player.GetWorld()!=P.GetWorld()||!Player.IsCombatActionAllowed()||P.IsActorBeingDestroyed()||!P.HasActorBegunPlay()||
        !Drops->IsPhysicalCurrent(P)||FVector::DistSquared(Player.GetActorLocation(),P.GetActorLocation())>FMath::Square(P.PickupRadiusCm))return false;
    FDWPreparedInventoryChange Plan;
    if(!Inventory->PrepareAddItem(P.ItemId,P.Count,Plan))
    {
        const int64 Revision=Inventory->GetInventoryRevision();
        if(const auto* Previous=P.LastFullInventoryRevision.Find(&Player);Previous&&*Previous==Revision)return false;
        P.LastFullInventoryRevision.Add(&Player,Revision);
        if(auto* PC=Cast<APlayerController>(Player.GetController()))if(auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD()))
            HUD->ShowCombatToast(DWText(&Player,TEXT("背包空间不足，核心留在地面"),TEXT("Inventory full. The core remains on the ground.")));
        return false; // HUD notification may destroy/restore actors; never continue the old request.
    }
    auto* Ledger=Authority->GetLedgerForResourceTransaction();
    const auto Life=Authority->CaptureActorLife(&Player);const uint64 ExpectedDropRevision=Drops->Revision;
    if(!Ledger||!Life.Entity||ExpectedDropRevision==MAX_uint64||!Authority->ReserveCorePickupNoticeSlot())return false;
    const auto Action=Authority->BeginAction(&Player);if(!Action.Id)return false;
    const auto Admission=Ledger->AdmitResource({Action,1,EResourceKind::CorePickup,1});
    if(Admission.Result!=EAdmission::Admitted){Authority->EndAction(Action);return false;}
    auto* Record=Drops->Records.Find(P.DeathId);
    if(!Record||!Drops->IsPhysicalCurrent(P)||Drops->Revision!=ExpectedDropRevision||!Authority->IsActionOwnedBy(Action,&Player)||
        !Inventory->ValidatePreparedForCommit(Plan))
    {Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);Authority->EndAction(Action);return false;}
    FDWCombatCorePickupNotice Notice;Notice.Player=&Player;Notice.Inventory=Inventory;Notice.Pickup=&P;Notice.PlayerLife=Life;
    Notice.ItemId=P.ItemId;Notice.Count=P.Count;
    Notice.Receipt=Admission.Receipt.Handle;Notice.PickupId=P.PickupId;Notice.InventoryRevision=Plan.GetExpectedRevision()+1;
    if(Ledger->Resolve(Notice.Receipt,EOutcome::Committed)!=EResolve::Resolved){Authority->EndAction(Action);return false;}
    // Fixed fact commit: inventory, persistent claim and physical availability precede any callback.
    Inventory->AdoptPreparedUnchecked(MoveTemp(Plan));
    Record->Save.Decision=EDWEnemyCoreDropDecision::Claimed;Record->bMayMaterialize=false;P.bClaimed=true;++Drops->Revision;
    if(auto* E=Record->Origin.Get();IsValid(E)&&!E->IsActorBeingDestroyed()&&E->GetWorld()==P.GetWorld()&&E->FormalCoreDrop.DeathId==P.DeathId)E->FormalCoreDrop.Decision=EDWEnemyCoreDropDecision::Claimed;
    Authority->EndAction(Action);Authority->EnqueueCommittedCorePickup(Notice);Authority->DrainNotifications();
    return true; // No old object accesses after notification reentry.
}
void FDWCombatCorePickupTransaction::Publish(const FDWCombatCorePickupNotice& Notice)
{
    auto* Player=Notice.Player.Get();auto* Inventory=Notice.Inventory.Get();auto* Authority=UDWCombatAuthoritySubsystem::Get(Player);
    if(Authority&&IsValid(Player)&&!Player->IsActorBeingDestroyed()&&Inventory&&Inventory->CanWriteInventory()&&Inventory->GetOwner()==Player)
    {
        const auto Life=Authority->CaptureActorLife(Player);
        if(Life.Entity==Notice.PlayerLife.Entity&&Life.Life==Notice.PlayerLife.Life)Inventory->OnChanged.Broadcast();
    }
    // A committed claim never reappears merely because a listener restored/destroyed the collector.
    auto* Pickup=Notice.Pickup.Get();
    if(IsValid(Pickup)&&!Pickup->IsActorBeingDestroyed()&&Pickup->GetWorld()&&!Pickup->GetWorld()->bIsTearingDown&&
        Pickup->PickupId==Notice.PickupId&&Pickup->bClaimed)Pickup->Destroy();
    // Reacquire after inventory listeners and destruction. A restore cannot emit an old-life toast.
    Player=Notice.Player.Get();Inventory=Notice.Inventory.Get();Authority=UDWCombatAuthoritySubsystem::Get(Player);
    if(!Authority||!IsValid(Player)||Player->IsActorBeingDestroyed()||!Inventory||
        Inventory->GetOwner()!=Player||!Inventory->CanWriteInventory()||Notice.ItemId.IsNone()||Notice.Count<=0)return;
    const auto CurrentLife=Authority->CaptureActorLife(Player);
    if(CurrentLife.Entity!=Notice.PlayerLife.Entity||CurrentLife.Life!=Notice.PlayerLife.Life)return;
    if(auto* PC=Cast<APlayerController>(Player->GetController()))if(auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD()))
        HUD->NotifyItemAcquired(Notice.ItemId,Notice.Count); // Final call: no access after external UI reentry.
}
