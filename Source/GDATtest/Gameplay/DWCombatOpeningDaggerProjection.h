#pragma once
#include "CoreMinimal.h"
#include "DWCombatDagger.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatWeaponResourceState.h"

class ADWPlayerCharacter;
class ADWEnemyCharacter;
class UDWWeaponAnimationComponent;
class UDWCombatAuthoritySubsystem;
class UDWSaveGame;
class UWorld;
class FDWCombatWorldPersistence;

namespace DWCombatOpeningDaggerProjection
{
 // Values captured from the target world, not observations borrowed from source enemies.
 struct FObservation
 {
  FDWDaggerAwarenessSave Awareness;
  DWCombatCommit::FEntityLife PlayerLife;
  bool bAlive=false,bInsideAtEquipmentCommit=false;
 };
 bool PrepareAwareness(const FObservation&,DWCombatCommit::FEntityLife TargetPlayerLife,
  const FDWCombatWeaponResourceState& CapturedTargetWeapon,const FDWCombatWeaponResourceState& IncomingWeapon,
  double TargetTime,FDWDaggerAwarenessSave& Out,FString& Error);
}

// Never serialized or accepted by ordinary Restore. Only the private fresh-opening
// builder creates this witness; the aggregate rechecks it with PlayerPlan's final
// CanCommit immediately before the original no-callback adoption barrier.
class FDWOpeningDaggerProjectionProof final
{
public:
 bool CanCommit(ADWPlayerCharacter&,const UDWSaveGame&,FString& Error)const;
private:
 friend class FDWCombatWorldPersistence;
 struct FEnemy
 {
  TWeakObjectPtr<ADWEnemyCharacter> Actor;
  DWCombatCommit::FEntityLife Life;
  FTransform Transform;
  FDWDaggerAwarenessSave Observed;
  double AlertRadius=0.;float Health=0.f;
 };
 TWeakObjectPtr<UWorld> World;
 TWeakObjectPtr<ADWPlayerCharacter> Player;
 TWeakObjectPtr<UDWWeaponAnimationComponent> Weapon;
 TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority;
 DWCombatCommit::FEntityLife Life;
 FDWCombatWeaponResourceSnapshot CapturedWeapon,IncomingWeapon;
 TArray<FEnemy> Enemies;
 TArray<uint8> ProjectedObjects;
 FString Map;
 FTransform PlayerTransform;
 double At=0.;uint64 Epoch=0,WeaponRestore=0;int64 WeaponRevision=0;
 bool bWeaponEnabled=false,bPrepared=false;
};
