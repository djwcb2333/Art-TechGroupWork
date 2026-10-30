#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Pure A01 model: no Unreal objects, callbacks, inventory grants or damage writes.
// Defaults are the editable LANG-01 trial values, not a final balance approval.
enum class EDWA01Form : std::uint8_t { Normal, Mold, Yeast };

struct FDWCharacterA01Config
{
    // The executor fills these from Design/TrialDefaults.json and derived talent effects.
    double HealthyMax=100., MinimumEffectiveMax=1.;
    double InitialHealth=100., SatietyMax=100., InitialSatiety=100., WaterMax=100., InitialWater=100.;
    double SatietyDrainAmount=1., SatietyDrainIntervalSeconds=4.5;
    double WaterDrainAmount=1., WaterDrainIntervalSeconds=3.;
    double StarvationDamagePerSecond=3., ThirstMovementMultiplier=.75;
    double CapLossAmount=1., CapLossIntervalSeconds=5.;
    double CapRecoveryAmount=1., CapRecoveryIntervalSeconds=2.;
    double NormalRegenAmount=1., NormalRegenIntervalSeconds=5.;
    bool bNormalRegenRequiresCapLoss=true, bRegenWhileStarving=false;
    // b6 supplies .4. Recorded loss is never erased when this limit changes.
    double MaximumLossFraction=1.;
    bool bClampCurrentHealthToEffectiveMax=true;
    double YeastDurationSeconds=180., MoldDurationSeconds=180.;
    double YeastSatietyMultiplier=1.5, YeastWaterMultiplier=.2;
    double MoldSatietyMultiplier=.2, MoldWaterMultiplier=1.5;
    double GasMax=100., CorruptionMax=100., GasEntryFraction=1., CorruptionEntryFraction=1.;
    double GasRegenPerSecond=20., CorruptionRegenPerSecond=1.;
    double CorruptionCost=10., CorruptionDamageBonus=.25, CorruptionHarvestBonus=.5;
    bool IsValid(std::string* Error=nullptr) const;
};

// Persist only this state. Open actions, input holds and invulnerability never resume on load.
struct FDWCharacterA01State
{
    std::uint32_t Schema=1;
    std::uint64_t StarvationOrdinal=0;
    double CurrentHealth=0., Satiety=0., Water=0.;
    double RecordedCapLoss=0., Gas=0., Corruption=0.;
    bool bCorruptionInitialized=false;
    EDWA01Form Form=EDWA01Form::Normal;
    double FormRemainingSeconds=0., EntryDurationSeconds=0.;
    double CapLossElapsedSeconds=0., CapRecoveryElapsedSeconds=0., NormalRegenElapsedSeconds=0.;
};

struct FDWCharacterA01Activity
{
    double GasSpendPerSecond=0.;
    // Also true for a dash that paid its full cost at startup.
    bool bSuppressGasRegeneration=false;
    bool bToolActionActive=false;
};

struct FDWCharacterA01Step
{
    double AdvancedSeconds=0.;
    // Apply damage through the existing authority first, then regen; finally sync actual health.
    // CurrentHealth is an imported mirror and is NOT debited/credited by either amount here.
    double HungerDamageDue=0., CurrentHealthRegenDue=0.;
    double AuthoritativeHealthBeforeAdvance=0.;
    double HealthCeilingBeforeRecovery=0., HealthCeiling=0.;
    double CurrentHealthClamp=0.; // Diagnostic old mirror only. NEVER assign this over post-damage authority HP.
    bool bCurrentHealthClamped=false, bCapLossTick=false, bCapRecoveryTick=false;
    bool bFormExpired=false, bFormChanged=false, bGasExhausted=false;
};

struct FDWA01CorruptionResult
{
    bool bEnhanced=false, bChargedThisCall=false;
    double DamageMultiplier=1., HarvestMultiplier=1.;
};

class FDWCharacterA01Runtime
{
public:
    bool Initialize(const FDWCharacterA01Config&, std::string* Error=nullptr);
    bool Restore(const FDWCharacterA01Config&, const FDWCharacterA01State&, std::string* Error=nullptr);
    static bool ValidateState(const FDWCharacterA01Config&, const FDWCharacterA01State&, std::string* Error=nullptr);
    bool IsReady() const { return bReady; }
    const FDWCharacterA01Config& GetConfig() const { return Config; }
    const FDWCharacterA01State& GetState() const { return State; }
    FDWCharacterA01State Capture() const { return State; }
    double AllowedNewLoss() const;
    double EffectiveLoss() const;
    double EffectiveMaxHealth() const;
    double MovementMultiplier() const;
    // Recompute from a new fully derived config. Increases never heal/refill resources.
    // b3 only affects the next successful essence; the running entry duration is preserved.
    bool ApplyConfig(const FDWCharacterA01Config&, std::string* Error=nullptr);
    bool SyncCurrentHealth(double AuthoritativeHealth);
    bool EnterForm(EDWA01Form); // Caller already committed exactly one essence inventory transaction.
    bool ExitForm();
    bool AdvanceNext(double MaximumSeconds, const FDWCharacterA01Activity&, FDWCharacterA01Step&);
    bool SpendGas(double Cost); // Startup cost: caller commits this candidate with its action receipt.
    double AddSatiety(double Amount);
    double AddWater(double Amount);
    bool BeginToolAction(std::uint64_t ActionId, bool bDamageEligible, bool bHarvestEligible);
    // Invalid/mismatched targets leave the decision open. First valid but unaffordable hit locks plain.
    // Tool identity/range/stock validation and whitelist lookup belong to the executor.
    FDWA01CorruptionResult ResolveToolHit(std::uint64_t ActionId, bool bValidTarget, bool bToolStillCurrent);
    bool EndToolAction(std::uint64_t ActionId);
    void CancelToolActions();
private:
    struct FToolAction
    {
        std::uint64_t Id=0;
        bool bDamageEligible=false, bHarvestEligible=false, bDecided=false, bEnhanced=false, bInvalidated=false;
        double DamageMultiplier=1., HarvestMultiplier=1.;
    };
    FDWCharacterA01Config Config;
    FDWCharacterA01State State;
    bool bReady=false;
    std::uint64_t LastToolActionId=0;
    std::vector<FToolAction> ToolActions;
    void EndFormFacts();
    void InvalidateCorruptionFacts();
    void ClampCurrentHealth();
};
