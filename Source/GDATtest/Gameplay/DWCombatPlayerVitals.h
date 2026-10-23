#pragma once
#include "CoreMinimal.h"
#include "DWCombatVitals.h"
#include "DWCombatPlayerVitals.generated.h"

class FDWCombatRules;

/** Normal has no core form or faction. Never infer Mold from !bYeastForm. */
UENUM(BlueprintType)
enum class EDWCombatBaseForm : uint8 { Normal, Mold, Yeast };

/** Resource-only additive save block, not the complete D22 snapshot. */
USTRUCT()
struct FDWCombatPlayerVitalsSnapshot
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 ResourceSchema=0;
    UPROPERTY(SaveGame) int32 RulesCombatSchema=0;
    UPROPERTY(SaveGame) double Health=0.;
    UPROPERTY(SaveGame) double Water=0.;
    UPROPERTY(SaveGame) double Satiety=0.;
    UPROPERTY(SaveGame) double Special=0.;
    UPROPERTY(SaveGame) double WaterEmptyElapsedSeconds=0.;
    UPROPERTY(SaveGame) double SatietyEmptyElapsedSeconds=0.;
    UPROPERTY(SaveGame) double WaterDrainCarry=0.;
    UPROPERTY(SaveGame) double SatietyDrainCarry=0.;
    UPROPERTY(SaveGame) double WaterEmptyTimeCarry=0.;
    UPROPERTY(SaveGame) double SatietyEmptyTimeCarry=0.;
    UPROPERTY(SaveGame) EDWCombatBaseForm BaseForm=EDWCombatBaseForm::Normal;
    UPROPERTY(SaveGame) double FormRemainingSeconds=0.;
    UPROPERTY(SaveGame) double FormTimeCarry=0.;
    UPROPERTY(SaveGame) double SpecialGrowthCarry=0.;
    UPROPERTY(SaveGame) uint64 DehydrationOrdinal=0;
    UPROPERTY(SaveGame) uint64 StarvationOrdinal=0;
};

struct FDWCombatPlayerVitalsConfig
{
    FDWCombatVitalsConfig Vitals;
    int32 RulesCombatSchema=0;
    double FormDurationSeconds=0.,InitialFormSpecial=0.,SpecialGainPerSecond=0.;
    double BaseMoveCentimetersPerSecond=0.,YeastMoveMultiplier=0.;
    double NormalIncomingMultiplier=0.,YeastIncomingMultiplier=0.,MoldIncomingMultiplier=0.;
    bool IsValid() const;
    static bool Build(const FDWCombatRules& Rules,FDWCombatPlayerVitalsConfig& Out,FString& Error);
};

struct FDWCombatPlayerVitalsStep
{
    double AdvancedSeconds=0.;
    FDWCombatVitalsDamageBatch Dehydration,Starvation;
    uint64 DehydrationOrdinal=0,StarvationOrdinal=0;
    bool bFormExpired=false;
};

/** No damage, inventory, UObject calls or notices. Health imports Player.Health as a temporary mirror. */
class GDATTEST_API FDWCombatPlayerVitalsRuntime
{
public:
    static constexpr int32 ResourceSchema=1;
    bool Initialize(const FDWCombatPlayerVitalsConfig& Config,EDWCombatVitalsReset Reset);
    bool Restore(const FDWCombatPlayerVitalsConfig& Config,const FDWCombatPlayerVitalsSnapshot& Snapshot);
    static bool Validate(const FDWCombatPlayerVitalsConfig& Config,const FDWCombatPlayerVitalsSnapshot& Snapshot,FString& Error);
    bool IsReady() const {return bReady;}
    uint64 GetMutationRevision() const {return MutationRevision;}
    const FDWCombatPlayerVitalsConfig& GetConfig() const {return Config;}
    FDWCombatPlayerVitalsSnapshot Capture(double AuthoritativeHealth) const;
    EDWCombatBaseForm GetForm() const {return State.BaseForm;}
    double GetWater() const {return State.Water;}
    double GetSatiety() const {return State.Satiety;}
    double GetSpecial() const {return State.Special-State.SpecialGrowthCarry;}
    double GetFormRemaining() const {return State.FormRemainingSeconds-State.FormTimeCarry;}
    // At most one positive event per channel; caller commits both facts before any notification.
    bool AdvanceNext(double Seconds,double AuthoritativeHealth,FDWCombatPlayerVitalsStep& Out,double SpecialSpendPerSecond=0.);
    // Validated native environmental refill. Returns actual accepted points; full/zero is a no-op.
    bool AddWater(double Amount,double AuthoritativeHealth,double& OutAdded);
    // Future already-committed injection/ability native seams. No Blueprint/player UI grant.
    bool EnterCommittedForm(EDWCombatBaseForm Form);
    bool SetSpecialAfterCommittedCost(double Value);
    bool DebitSpecialAfterCommittedCost(double Cost);
    void EndForm();
private:
    FDWCombatPlayerVitalsConfig Config;
    FDWCombatPlayerVitalsSnapshot State;
    bool bReady=false;
    uint64 MutationRevision=0;
};
