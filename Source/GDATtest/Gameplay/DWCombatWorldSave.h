#pragma once
#include "CoreMinimal.h"
#include "DWCombatMines.h"
#include "DWCombatWorldSave.generated.h"

// Aggregate marker is intentionally absent on old files. Only the complete writer sets it.
USTRUCT()
struct FDWShieldDefenseSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=0;
    UPROPERTY(SaveGame) int32 CounterCharges=0;
    UPROPERTY(SaveGame) double PerfectRemaining=0.;
    UPROPERTY(SaveGame) double ReopenRemaining=0.;
    UPROPERTY(SaveGame) bool bPerfectEligible=false;
    UPROPERTY(SaveGame) uint8 BaseForm=0;
};

USTRUCT()
struct FDWShieldPacketSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) uint64 Receipt=0;
    UPROPERTY(SaveGame) uint64 TargetEntity=0;
    UPROPERTY(SaveGame) uint64 TargetLife=0;
    UPROPERTY(SaveGame) uint64 SourceEntity=0;
    UPROPERTY(SaveGame) uint64 SourceLife=0;
    UPROPERTY(SaveGame) uint64 Action=0;
    UPROPERTY(SaveGame) uint64 Phase=0;
    UPROPERTY(SaveGame) uint64 PacketOrdinal=0;
    UPROPERTY(SaveGame) uint8 ActionKind=0;
    UPROPERTY(SaveGame) uint8 IncomingKind=0;
    UPROPERTY(SaveGame) FString InstigatorId;
    UPROPERTY(SaveGame) FSoftClassPath DamageType;
    UPROPERTY(SaveGame) float Amount=0.f;
    UPROPERTY(SaveGame) float PreparedAmount=0.f;
    UPROPERTY(SaveGame) float NotificationAmount=0.f;
    UPROPERTY(SaveGame) float KnockbackDistance=0.f;
    UPROPERTY(SaveGame) float KnockbackSeconds=0.f;
    UPROPERTY(SaveGame) FVector KnockbackDirection=FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector IncomingDirection=FVector::ZeroVector;
    UPROPERTY(SaveGame) double ImpactAge=0.;
    UPROPERTY(SaveGame) double Remaining=0.;
    UPROPERTY(SaveGame) bool bImpactRooted=false;
    UPROPERTY(SaveGame) bool bEngineNotifications=false;
    UPROPERTY(SaveGame) bool bSourceWasPlayer=false;
};

USTRUCT()
struct FDWMineEmitterSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FGuid MineId;
    UPROPERTY(SaveGame) uint64 Emitter=0;
};

USTRUCT()
struct FDWCombatPlayerExtendedSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=0;
    UPROPERTY(SaveGame) double WeaponLockRemaining=0.;
    UPROPERTY(SaveGame) double LegacyMeleeLockRemaining=0.;
    UPROPERTY(SaveGame) double LegacyThrowLockRemaining=0.;
    UPROPERTY(SaveGame) double DashCooldownRemaining=0.;
    UPROPERTY(SaveGame) uint64 CombatDamageRevision=0;
    UPROPERTY(SaveGame) FDWShieldDefenseSave Defense;
    UPROPERTY(SaveGame) TArray<FDWShieldPacketSave> ShieldPackets;
    UPROPERTY(SaveGame) bool bRooted=false;
    UPROPERTY(SaveGame) FVector RootAnchor=FVector::ZeroVector;
    UPROPERTY(SaveGame) uint64 RootAction=0;
    UPROPERTY(SaveGame) uint64 RootReceipt=0;
    UPROPERTY(SaveGame) uint64 PlayerEntity=0;
    UPROPERTY(SaveGame) uint64 PlayerLife=0;
    UPROPERTY(SaveGame) FDWMoldMineRingSave Ring;
    UPROPERTY(SaveGame) FDWMoldMineConfigSave MineConfig;
    UPROPERTY(SaveGame) TArray<FDWMineEmitterSave> MineEmitters;
    UPROPERTY(SaveGame) TArray<uint64> RingHitEntities;
};

USTRUCT()
struct FDWCombatWorldSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=0;
    // Fresh-slot marker only. Never used for a previously running world.
    UPROPERTY(SaveGame) bool bNewAdventure=false;
    UPROPERTY(SaveGame) FDWCombatPlayerExtendedSave Player;
    UPROPERTY(SaveGame) TArray<uint8> Objects;
    UPROPERTY(SaveGame) TArray<uint8> Effects;
};
