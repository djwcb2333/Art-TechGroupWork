#include "DWCombatWorldPersistence.h"
#include "DWCombatOpeningDaggerProjection.h"
#include "DWCombatWorldObjectsPersistence.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWEnemyCharacter.h"
#include "DWSaveGame.h"
#include "Engine/World.h"

namespace
{
bool SameLife(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B)
{return A.Entity&&A.Entity==B.Entity&&A.Life==B.Life;}
bool SameWeapon(const FDWCombatWeaponResourceSnapshot& A,const FDWCombatWeaponResourceSnapshot& B)
{
 if(A.CurrentEquipmentId!=B.CurrentEquipmentId||A.LoadedShotgunShells!=B.LoadedShotgunShells||A.OwnedPermanentWeaponIds.Num()!=B.OwnedPermanentWeaponIds.Num())return false;
 for(FName Id:A.OwnedPermanentWeaponIds)if(!B.OwnedPermanentWeaponIds.Contains(Id))return false;return true;
}
bool SameSavedWeapon(const FDWCombatPlayerResourceSave& A,const FDWCombatWeaponResourceSnapshot& B)
{
 if(A.CurrentEquipmentId!=B.CurrentEquipmentId||A.LoadedShotgunShells!=B.LoadedShotgunShells||A.OwnedPermanentWeaponIds.Num()!=B.OwnedPermanentWeaponIds.Num())return false;
 for(FName Id:B.OwnedPermanentWeaponIds)if(!A.OwnedPermanentWeaponIds.Contains(Id))return false;return true;
}
bool SameAwareness(const FDWDaggerAwarenessSave& A,const FDWDaggerAwarenessSave& B)
{return FDWDaggerAwarenessSave::StaticStruct()->CompareScriptStruct(&A,&B,PPF_None);}
}
bool FDWOpeningDaggerProjectionProof::CanCommit(ADWPlayerCharacter& P,const UDWSaveGame& S,FString& Error)const
{
 const auto Bad=[&](){Error=TEXT("Fresh-opening player/equipment/target-observation projection witness changed.");return false;};
 auto* A=Authority.Get();auto* W=World.Get();auto* Gear=Weapon.Get();
 if(!bPrepared||Player.Get()!=&P||!W||W->bIsTearingDown||P.GetWorld()!=W||P.IsActorBeingDestroyed()||P.IsDead()||
  !A||UDWCombatAuthoritySubsystem::Get(&P)!=A||A->GetWorldEpoch()!=Epoch||W->GetTimeSeconds()!=At||
  !SameLife(A->FindRegisteredActorLife(&P),Life)||!P.GetActorTransform().Equals(PlayerTransform,0.)||
  !Gear||P.WeaponAnimationComponent.Get()!=Gear||Gear->IsBeingDestroyed()||Gear->IsEnabled()!=bWeaponEnabled||
  Gear->GetCombatWeaponResourceRevision()!=WeaponRevision||Gear->GetCombatWeaponRestoreEpoch()!=WeaponRestore||
  !SameWeapon(Gear->GetCombatWeaponStateSnapshot(),CapturedWeapon)||!S.MapPackage.Equals(Map,ESearchCase::CaseSensitive)||
  !SameSavedWeapon(S.CombatPlayerResources,IncomingWeapon)||S.CombatWorld.Player.PlayerEntity!=Life.Entity||
  S.CombatWorld.Player.PlayerLife!=Life.Life||S.CombatWorld.Objects!=ProjectedObjects)return Bad();
 for(const auto& Row:Enemies)
 {
  auto* E=Row.Actor.Get();
  if(!IsValid(E)||E->IsActorBeingDestroyed()||E->GetWorld()!=W||!E->IsAlive()||E->Health!=Row.Health||
   !SameLife(A->FindRegisteredActorLife(E),Row.Life)||!E->GetActorTransform().Equals(Row.Transform,0.)||
   E->GetDaggerAlertRadius()!=Row.AlertRadius||!SameAwareness(E->CaptureDaggerAwarenessSave(),Row.Observed))return Bad();
 }
 Error.Reset();return true;
}
bool FDWCombatWorldPersistence::PrepareOpeningDaggerProjection(ADWPlayerCharacter& P,
 const FDWCombatWeaponResourceState& Incoming,UDWSaveGame& Fresh,FDWOpeningDaggerProjectionProof& Out,FString& Error)
{
 auto* W=P.GetWorld();auto* A=UDWCombatAuthoritySubsystem::Get(&P);auto* Gear=P.WeaponAnimationComponent.Get();
 auto* GI=P.GetGameInstance<UDWGameInstance>();const auto Rules=GI?GI->GetCombatRules():nullptr;
 if(!W||W->bIsTearingDown||!A||!Gear||!Rules||P.IsDead()||P.IsActorBeingDestroyed()||P.IsCombatRestoreInProgress())return false;
 FDWOpeningDaggerProjectionProof Proof;Proof.World=W;Proof.Player=&P;Proof.Weapon=Gear;Proof.Authority=A;
 Proof.At=W->GetTimeSeconds();Proof.Epoch=A->GetWorldEpoch();Proof.Life=A->FindRegisteredActorLife(&P);
 Proof.Map=UWorld::RemovePIEPrefix(W->GetOutermost()->GetName());Proof.PlayerTransform=P.GetActorTransform();
 Proof.CapturedWeapon=Gear->GetCombatWeaponStateSnapshot();Proof.IncomingWeapon=Incoming.Snapshot;
 Proof.WeaponRevision=Gear->GetCombatWeaponResourceRevision();Proof.WeaponRestore=Gear->GetCombatWeaponRestoreEpoch();Proof.bWeaponEnabled=Gear->IsEnabled();
 FDWCombatWeaponResourceState Captured;
 if(!DWCombatPlayerSave::BuildWeaponCandidate(*Rules,Fresh.CombatPlayerResources,Captured,Error)||!Incoming.bRulesReady||
  !SameWeapon(Captured.Snapshot,Proof.CapturedWeapon)||!Fresh.MapPackage.Equals(Proof.Map,ESearchCase::CaseSensitive)||!Fresh.bHasPlayerTransform||
  !Fresh.PlayerTransform.Equals(Proof.PlayerTransform,0.)||Fresh.Health!=P.GetHealth()||
  Fresh.CombatWorld.Player.PlayerEntity!=Proof.Life.Entity||Fresh.CombatWorld.Player.PlayerLife!=Proof.Life.Life)
 {Error=TEXT("Fresh-opening target player capture is not its current live equipment/life/pose.");return false;}
 const bool CapturedDagger=Captured.Snapshot.CurrentEquipmentId==TEXT("DWWeapon_DualDaggers");
 const bool IncomingDagger=Incoming.Snapshot.CurrentEquipmentId==TEXT("DWWeapon_DualDaggers");
 if((CapturedDagger||IncomingDagger)&&!Proof.bWeaponEnabled)
 {Error=TEXT("Fresh-opening dagger projection cannot enable an authored disabled weapon component.");return false;}
 // Reuse the exact existing stable binding producer. The captured bytes and old
 // legacy records must be unchanged; do not reconstruct a looser actor-ID lookup.
 FDWCombatWorldObjectsSave Objects;TArray<FDWPersistedActorState> Legacy;TArray<FDWCombatObjectBinding> Bindings;TArray<uint8> Before;
 const bool Formal=Fresh.FormalLanguage.Schema==2;
 if(!FDWCombatWorldObjectsPersistence::Capture(*W,P,Objects,Legacy,Bindings,Error)||
  !DWCombatWorldObjectsSave::Encode(Objects,Before,Error,Formal)||Before!=Fresh.CombatWorld.Objects||Legacy.Num()!=Fresh.WorldActors.Num())return false;
 for(int32 I=0;I<Legacy.Num();++I)
  if(!FDWPersistedActorState::StaticStruct()->CompareScriptStruct(&Legacy[I],&Fresh.WorldActors[I],PPF_None))
  {Error=TEXT("Fresh-opening target legacy facts changed before equipment projection.");return false;}
 auto Candidate=Objects;
 for(auto& Row:Candidate.Enemies)
 {
  DWCombatOpeningDaggerProjection::FObservation In;In.Awareness=Row.Dagger;In.PlayerLife=Row.DaggerPlayerLife;In.bAlive=Row.Health>0.f;
  if(In.bAlive&&In.Awareness.bInitialized&&SameLife(In.PlayerLife,Proof.Life)&&CapturedDagger!=IncomingDagger)
  {
   const auto* Binding=Bindings.FindByPredicate([&](const auto& X){return X.StableId.Equals(Row.StableId,ESearchCase::CaseSensitive);});
   auto* E=Binding?Cast<ADWEnemyCharacter>(Binding->Actor.Get()):nullptr;
   if(!IsValid(E)||E->IsActorBeingDestroyed()||E->GetWorld()!=W||!E->IsAlive()||E->Health!=Row.Health||
    !SameLife(A->FindRegisteredActorLife(E),Row.Life)||!E->GetActorTransform().Equals(Row.Transform,0.))
   {Error=TEXT("Fresh-opening enemy observation lost its exact captured actor binding.");return false;}
   const double Radius=E->GetDaggerAlertRadius();if(!FMath::IsFinite(Radius)||Radius<0.)return false;
   In.bInsideAtEquipmentCommit=FVector::Dist2D(Row.Transform.GetLocation(),Proof.PlayerTransform.GetLocation())<=Radius;
   FDWOpeningDaggerProjectionProof::FEnemy Expected;Expected.Actor=E;Expected.Life=Row.Life;Expected.Transform=Row.Transform;
   Expected.Observed=Row.Dagger;Expected.AlertRadius=Radius;Expected.Health=Row.Health;Proof.Enemies.Add(MoveTemp(Expected));
  }
  FDWDaggerAwarenessSave Projected;
  if(!DWCombatOpeningDaggerProjection::PrepareAwareness(In,Proof.Life,Captured,Incoming,Proof.At,Projected,Error))return false;
  Row.Dagger=Projected;
 }
 TArray<uint8> Encoded;if(!DWCombatWorldObjectsSave::Encode(Candidate,Encoded,Error,Formal))return false;
 if(P.GetWorld()!=W||W->GetTimeSeconds()!=Proof.At||A->GetWorldEpoch()!=Proof.Epoch||
  !SameLife(A->FindRegisteredActorLife(&P),Proof.Life)||!P.GetActorTransform().Equals(Proof.PlayerTransform,0.)||
  Gear->GetCombatWeaponResourceRevision()!=Proof.WeaponRevision||Gear->GetCombatWeaponRestoreEpoch()!=Proof.WeaponRestore||
  !SameWeapon(Gear->GetCombatWeaponStateSnapshot(),Proof.CapturedWeapon))return false;
 Proof.ProjectedObjects=Encoded;Proof.bPrepared=true;
 Fresh.CombatWorld.Objects=MoveTemp(Encoded);Out=MoveTemp(Proof);Error.Reset();return true;
}
bool FDWCombatWorldPersistence::RestoreOpeningFreshDestination(ADWPlayerCharacter& P,const UDWSaveGame& Source,
 const FString& Destination,UDWSaveGame& Projected,bool& Committed,FString& Error)
{
 Committed=false;FDWOpeningDaggerProjectionProof Proof;
 if(!BuildFreshOpeningDestination(P,Source,Destination,Projected,Proof,Error))return false;
 return RestoreImpl(P,Projected,Committed,Error,&Proof);
}
