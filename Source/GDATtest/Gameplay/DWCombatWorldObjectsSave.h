#pragma once
#include "CoreMinimal.h"
#include "DWGameplayTypes.h"
#include "DWEnemyCombatProfile.h"
#include "DWCombatDagger.h"
#include "DWEnemyForcedFall.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatRecoveryData.h"
#include "DWCombatEffectsSave.h"

/** Plain values only. All clocks are offsets from the capture's game-time instant. */
struct FDWCombatEnemyObjectSave
{
    FString StableId,ActorPath,ClassPath,SourceNestId;
    FName PersistentId;
    FTransform Transform=FTransform::Identity;
    DWCombatCommit::FEntityLife Life;
    float Health=0.,MaxHealth=0.;
    uint8 State=0,Phase=0,MovementMode=0,CustomMovementMode=0;
    int32 AttackSerial=0,VariantIndex=INDEX_NONE;
    EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::Legacy;
    FDWEnemyFormalSaveData Formal;
    FDWDaggerAwarenessSave Dagger;
    FDWEnemyForcedFallSave ForcedFall;
    DWCombatCommit::FEntityLife DaggerPlayerLife,ObservedPlayerLife,MeleeTargetLife;
    DWCombatCommit::FActionId MeleeAction;
    FString TargetId,MeleeTargetId,AttackAnimationPath;
    FVector Home=FVector::ZeroVector,PatrolGoal=FVector::ZeroVector,LockedDirection=FVector::ForwardVector;
    FVector KnockbackDirection=FVector::ZeroVector,Velocity=FVector::ZeroVector;
    double AttackAge=0.,PhaseRemaining=0.,AttackCooldown=0.,AlertRemaining=0.,PatrolRemaining=0.,PathRemaining=0.;
    double CurrentWindup=0.,CurrentActive=0.,CurrentRecovery=0.,CurrentRange=0.,CurrentDamage=0.;
    double KnockbackDistance=0.,KnockbackSeconds=0.,KnockbackSpeed=0.,KnockbackInitialDistance=0.;
    uint64 KnockbackRevision=0,ObservedPlayerDamageRevision=0;
    double ObservationAge=0.;bool bHasObservation=false;
    double DeathCleanupRemaining=0.,LifeSpanRemaining=0.;bool bDeathCleanupScheduled=false;
    bool bRuntimeSpawned=false,bEnded=false,bAttackResolved=false,bHasAttackSnapshot=false,bFacingLocked=false;
    bool bKnockedBack=false,bAttackContinuesDuringKnockback=false,bNeedsChaseState=false,bHasPatrolGoal=false;
    bool bPlayerDeathReturn=false,bHidden=false,bActorTick=true,bCollision=true,bMeshTick=true;
    bool bHasPreDeathState=false,bPreDeathCollision=true,bPreDeathHidden=false,bPreDeathTick=true,bPreDeathMeshTick=true;
    uint8 PreDeathMovementMode=1,PreDeathCustomMovementMode=0;
    // Percent, remaining seconds; legacy authored slow effects are independent from D17's regions.
    TArray<FVector2D> LegacySlows;
};
struct FDWCombatNestClockSave {FString StableId;double SampledInterval=0.;};
struct FDWCombatWorldObjectsSave
{
    uint32 Schema=1;
    FString MapPackage;
    TArray<FDWCombatEnemyObjectSave> Enemies;
    TArray<FDWCombatNestClockSave> NestClocks;
    TArray<FDWCombatCoreDropSave> CoreDrops;
    FDWCombatRecoverySave Recovery;
};
namespace DWCombatWorldObjectsSave
{
    bool Validate(const FDWCombatWorldObjectsSave&,FString& Error);
    bool Encode(const FDWCombatWorldObjectsSave&,TArray<uint8>& Out,FString& Error);
    bool Decode(const TArray<uint8>&,FDWCombatWorldObjectsSave& Out,FString& Error);
    // Legacy Resource/Nest structure remains in the outer SaveGame, but is validated without Blueprint queries.
    bool ValidateLegacy(const TArray<FDWPersistedActorState>&,FString& Error);
}
