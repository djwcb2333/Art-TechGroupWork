#include "DWCombatOpeningDaggerProjection.h"
#include "DWCombatEquipment.h"

bool DWCombatOpeningDaggerProjection::PrepareAwareness(const FObservation& In,DWCombatCommit::FEntityLife TargetLife,
 const FDWCombatWeaponResourceState& Captured,const FDWCombatWeaponResourceState& Incoming,double At,
 FDWDaggerAwarenessSave& Out,FString& Error)
{
 if(!TargetLife.Entity||!TargetLife.Life||!FMath::IsFinite(At)||At<0.||!FDWDaggerAwareness::Validate(In.Awareness)||
  !Captured.bRulesReady||!Incoming.bRulesReady||!DWCombatEquipment::ValidateIdentity(Captured.Snapshot,Error)||
  !DWCombatEquipment::ValidateIdentity(Incoming.Snapshot,Error))
 {Error=TEXT("Invalid fresh-opening dagger projection input.");return false;}
 if(!In.bAlive||!In.Awareness.bInitialized||In.PlayerLife.Entity!=TargetLife.Entity||In.PlayerLife.Life!=TargetLife.Life)
 {Out=In.Awareness;Error.Reset();return true;} // Preserve corpses, unobserved and historical lives exactly.
 const bool Before=Captured.Snapshot.CurrentEquipmentId==TEXT("DWWeapon_DualDaggers");
 const bool After=Incoming.Snapshot.CurrentEquipmentId==TEXT("DWWeapon_DualDaggers");
 if(In.Awareness.bEquipped!=Before)
 {Error=TEXT("Target observation already disagrees with its captured player's actual equipment.");return false;}
 if(Before==After){Out=In.Awareness;Error.Reset();return true;} // Preserve positive grace without a deadline round-trip.
 FDWDaggerAwareness Candidate;
 if(!Candidate.Restore(In.Awareness,At)){Error=TEXT("Target dagger observation cannot be prepared.");return false;}
 // The player candidate really changes equipment in this same adoption. This is
 // the existing equip rule: standing inside is not a geometric entry. No Observe,
 // Refresh, BreakProtection, manual flag assignment, or new grace is fabricated.
 Candidate.EquipmentChanged(In.bInsideAtEquipmentCommit,After);
 const auto Saved=Candidate.Capture(At);
 if(!FDWDaggerAwareness::Validate(Saved)){Error=TEXT("Projected dagger observation is invalid.");return false;}
 Out=Saved;Error.Reset();return true;
}
