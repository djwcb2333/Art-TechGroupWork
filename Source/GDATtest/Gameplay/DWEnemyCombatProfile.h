#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
#include "DWEnemyCombatProfile.generated.h"

class FDWCombatRules;
enum class EDWCombatBaseForm : uint8;

/** Appended opt-in identities. Existing placed enemies and Blueprint defaults remain Legacy. */
UENUM(BlueprintType)
enum class EDWEnemyCombatProfile : uint8
{
    Legacy=0, MoldGrunt=1, YeastGrunt=2, DoughZombie=3, DoughElite=4
};

/** Immutable validated rule projection; UE distance/speed conversion occurs exactly once here. */
struct FDWEnemyCombatConfig
{
    EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::Legacy;
    double Health=0.,Damage=0.,WindupSeconds=0.,ActiveSeconds=0.,RecoverySeconds=0.,ExtraCooldownSeconds=0.;
    double RangeCm=0.,MoveSpeedCmPerSecond=0.,AttackFullAngleDegrees=0.,AttackHeightCm=0.;
    double KnockbackMultiplier=0.,AttackKnockbackMultiplier=0.,OutgoingKnockbackCm=0.,OutgoingKnockbackSeconds=0.;
    double AlertRadiusCm=0.,DisengageDistanceCm=0.,AlertDelaySeconds=0.,DisengageSeconds=0.;
    double WindupLockFraction=0.,ReturnSpeedMultiplier=0.;
    int32 HitsPerTarget=0;
    double AttackCycleSeconds()const{return WindupSeconds+ActiveSeconds+RecoverySeconds+ExtraCooldownSeconds;}
};

struct FDWEnemyCoreDropConfig
{
    double MoldProbability=0.,YeastProbability=0.,PickupRadiusCm=0.;
    int32 Count=0,StackLimit=0;
};

/** Per-enemy state. No shared aggro group and no dependency on the old bYeastForm mirror. */
struct FDWEnemyCombatMemory
{
    bool bEngaged=false,bReturningHome=false,bDisengageCandidate=false;
    double DisengageElapsedSeconds=0.,LastObservationTime=-1.;
    void Engage();
    void RecordDamage();
    bool ObserveDisengage(double Now,double DistanceCm,bool bHasSight,bool bDamageSinceObservation,const FDWEnemyCombatConfig& Config);
};

UENUM()
enum class EDWEnemyCoreDropDecision : uint8 { Unresolved, Ineligible, Failed, SpawnPending, Spawned, Claimed };
/** Persist success AND failure. Fatal receipt/source identity are facts, not a permanent 'was hit by player' flag. */
struct FDWEnemyCoreDropRecord
{
    FGuid DeathId;
    EDWEnemyCoreDropDecision Decision=EDWEnemyCoreDropDecision::Unresolved;
    DWCombatCommit::FReceiptId FatalReceipt;
    DWCombatCommit::FEntityLife EnemyLife,PlayerLife;
    FGuid PickupId;
    FName CoreItemId;
    int32 Count=0;
};

/** Additive save block. Current transform/health stay in FDWPersistedActorState. */
USTRUCT()
struct FDWEnemyFormalSaveData
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=0;
    UPROPERTY(SaveGame) EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::Legacy;
    UPROPERTY(SaveGame) FGuid StableEnemyId;
    UPROPERTY(SaveGame) FVector BirthLocation=FVector::ZeroVector;
    UPROPERTY(SaveGame) bool bEngaged=false;
    UPROPERTY(SaveGame) bool bReturningHome=false;
    UPROPERTY(SaveGame) bool bPlayerDeathReturn=false;
    UPROPERTY(SaveGame) bool bDisengageCandidate=false;
    UPROPERTY(SaveGame) double DisengageElapsedSeconds=0.;
    UPROPERTY(SaveGame) double AlertRemainingSeconds=0.;
    UPROPERTY(SaveGame) double AttackCooldownRemainingSeconds=0.;
    UPROPERTY(SaveGame) EDWEnemyCoreDropDecision DropDecision=EDWEnemyCoreDropDecision::Unresolved;
    UPROPERTY(SaveGame) uint64 FatalReceiptEpoch=0;
    UPROPERTY(SaveGame) uint64 FatalReceiptId=0;
    UPROPERTY(SaveGame) uint64 DeathEntityId=0;
    UPROPERTY(SaveGame) uint64 DeathLife=0;
    UPROPERTY(SaveGame) uint64 CreditedPlayerEntityId=0;
    UPROPERTY(SaveGame) uint64 CreditedPlayerLife=0;
    UPROPERTY(SaveGame) FGuid CoreDropDeathId;
    UPROPERTY(SaveGame) FGuid PickupId;
    UPROPERTY(SaveGame) FName CoreItemId;
    UPROPERTY(SaveGame) int32 CoreCount=0;
};

/** Captured before any damage notification; no phase-dependent recalculation at publication. */
struct FDWEnemyFormalKnockback
{
    FVector Direction=FVector::ZeroVector;
    double DistanceCm=0.,DurationSeconds=0.;
    bool bValid=false,bInterruptAttack=false,bContinueAttack=false;
};

namespace DWEnemyFormal
{
    bool BuildConfig(const FDWCombatRules& Rules,EDWEnemyCombatProfile Profile,FDWEnemyCombatConfig& Out,FString& Error);
    bool BuildCoreDropConfig(const FDWCombatRules& Rules,FDWEnemyCoreDropConfig& Out,FString& Error);
    bool IsHostile(EDWEnemyCombatProfile Profile,EDWCombatBaseForm PlayerForm,bool bRememberedCombat);
    /** Exact box intersection with a vertical circular sector, including tilted/nonuniformly scaled hurtboxes. */
    bool BoxIntersectsSector(const FTransform& BoxTransform,FVector BoxExtent,FVector Origin,FVector Facing,
        double RadiusCm,double FullHeightCm,double FullAngleDegrees);
    /** One eligible fatal identity, one sample. This function creates no actor/inventory mutation. */
    bool DecideCoreDrop(FDWEnemyCoreDropRecord& Record,EDWEnemyCombatProfile Profile,const FDWEnemyCoreDropConfig& Config,
        DWCombatCommit::FReceiptId FatalReceipt,DWCombatCommit::FEntityLife EnemyLife,
        DWCombatCommit::FEntityLife CreditedPlayerLife,double UnitSample,const FGuid& PreparedPickupId);
}
