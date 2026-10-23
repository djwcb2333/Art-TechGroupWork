#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatMines.generated.h"

class FDWCombatRules;
struct FDWMoldMineConfig
{
    int32 Count=0,HitsPerTarget=0;
    double RingRadiusCm=0.,TriggerRadiusCm=0.,DamageRadiusCm=0.,Damage=0.;
    double KnockbackCm=0.,KnockbackSeconds=0.,CooldownSeconds=0.,GroundToleranceCm=0.;
    bool IsValid() const;
    static bool Build(const FDWCombatRules& Rules,FDWMoldMineConfig& Out,FString& Error);
    FVector Point(FVector Anchor,int32 Ordinal) const;
};

// Released mine values are saved separately from newly loaded tuning.
USTRUCT()
struct FDWMoldMineConfigSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Count=0;
    UPROPERTY(SaveGame) int32 HitsPerTarget=0;
    UPROPERTY(SaveGame) double RingRadiusCm=0.;
    UPROPERTY(SaveGame) double TriggerRadiusCm=0.;
    UPROPERTY(SaveGame) double DamageRadiusCm=0.;
    UPROPERTY(SaveGame) double Damage=0.;
    UPROPERTY(SaveGame) double KnockbackCm=0.;
    UPROPERTY(SaveGame) double KnockbackSeconds=0.;
    UPROPERTY(SaveGame) double CooldownSeconds=0.;
    UPROPERTY(SaveGame) double GroundToleranceCm=0.;
};

USTRUCT()
struct FDWMoldMineSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FGuid MineId;
    UPROPERTY(SaveGame) int32 Ordinal=0;
    UPROPERTY(SaveGame) FVector Location=FVector::ZeroVector;
    UPROPERTY(SaveGame) bool bConsumed=false;
};
// Runtime receipt/entity IDs are intentionally absent. D22 binds this with root + enemies atomically.
USTRUCT()
struct FDWMoldMineRingSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=1;
    UPROPERTY(SaveGame) double CooldownRemainingSeconds=0.;
    UPROPERTY(SaveGame) bool bActive=false;
    UPROPERTY(SaveGame) FGuid RingId;
    UPROPERTY(SaveGame) FVector Anchor=FVector::ZeroVector;
    UPROPERTY(SaveGame) TArray<FDWMoldMineSave> Mines;
    UPROPERTY(SaveGame) TArray<FGuid> HitFormalEnemies;
    UPROPERTY(SaveGame) TArray<FName> HitLegacyEnemies;
};
namespace DWMoldMines
{
    // Validation only. No independent live-world restore can recreate a paid root/ring.
    bool ValidateSnapshot(const FDWMoldMineRingSave& Snapshot,FString& Error);
}
