#pragma once

#include <cstdint>

// D03 pure logic. The rules bridge supplies every value; zero-initialized config is invalid.
// No UObject, world clock, damage call, inventory operation, or gameplay default lives here.
struct FDWCombatVitalsConfig
{
    double HealthMax = 0.0;
    double HealthFloor = 0.0;
    double WaterMax = 0.0;
    double WaterFloor = 0.0;
    double SatietyMax = 0.0;
    double SatietyFloor = 0.0;
    double SpecialMax = 0.0;
    double SpecialFloor = 0.0;
    double InitialHealthFraction = 0.0;
    double InitialWaterFraction = 0.0;
    double InitialSatietyFraction = 0.0;
    double RespawnHealthFraction = 0.0;
    double RespawnWaterFraction = 0.0;
    double RespawnSatietyFraction = 0.0;
    double NormalSpecialValue = 0.0;
    double WaterDrainPerSecond = 0.0;
    double SatietyDrainPerSecond = 0.0;
    double WaterDamagePeriodSeconds = 0.0;
    double SatietyDamagePeriodSeconds = 0.0;
    double WaterDamagePerEvent = 0.0;
    double SatietyDamagePerEvent = 0.0;

    bool IsValid() const;
};

enum class EDWCombatVitalsReset : std::uint8_t { NewCharacter, Respawn };
enum class EDWCombatVitalsDamageSource : std::uint8_t { Dehydration, Starvation };

// Save all fields, including partial periods and numerical carries, as doubles.
// Public data is intentional for a separate validated save-schema bridge. Restore by validation,
// never by Initialize(), which would discard the partially completed damage periods.
struct FDWCombatVitalsState
{
    double Health = 0.0;
    double Water = 0.0;
    double Satiety = 0.0;
    double Special = 0.0;
    double WaterEmptyElapsedSeconds = 0.0;
    double SatietyEmptyElapsedSeconds = 0.0;
    double WaterDrainCarry = 0.0;
    double SatietyDrainCarry = 0.0;
    double WaterEmptyTimeCarry = 0.0;
    double SatietyEmptyTimeCarry = 0.0;

    bool IsValid(const FDWCombatVitalsConfig& Config) const;
};

// Constant-size representation of independent periodic events, including very large Delta.
// Event i occurs at FirstEventOffsetSeconds + i * PeriodSeconds inside this advance.
// The sole damage owner merges the two sequences chronologically, retains simultaneous events
// as two sources, and submits each event once. Do not submit TotalDamage and the events both.
// These events bypass form reduction and blocking. Advance does NOT subtract Health.
struct FDWCombatVitalsDamageBatch
{
    EDWCombatVitalsDamageSource Source = EDWCombatVitalsDamageSource::Dehydration;
    std::uint64_t EventCount = 0;
    double DamagePerEvent = 0.0;
    double FirstEventOffsetSeconds = 0.0;
    double PeriodSeconds = 0.0;
    bool bBypassFormReductionAndBlocking = true;

    double TotalDamage() const;
};

struct FDWCombatVitalsAdvanceResult
{
    bool bAccepted = false;
    double AdvancedSeconds = 0.0;
    FDWCombatVitalsDamageBatch Dehydration;
    FDWCombatVitalsDamageBatch Starvation;
};

namespace DWCombatVitals
{
    bool Initialize(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config,
        EDWCombatVitalsReset Reset);

    // One call per admitted simulation segment. The owner supplies game time (not wall time),
    // freezes this call during pause, and splits segments at external damage/death/form events.
    // No automatic healing, special regeneration, death handling, or form transition occurs here.
    // On invalid inputs or unrepresentable event counts, reject the WHOLE advance unchanged.
    // On success the caller must commit returned damage through its sole authority before saving.
    FDWCombatVitalsAdvanceResult Advance(FDWCombatVitalsState& State,
        const FDWCombatVitalsConfig& Config, double DeltaSeconds, bool bPaused);

    // Internal extension points only: these do not introduce food/drink items or a player UI.
    // A positive refill clears that resource's empty-period progress; a zero refill is a no-op.
    bool AddWater(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Amount);
    bool AddSatiety(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Amount);

    // Explicit, atomic hooks for the form/injection/ability owner. Growth and costs are supplied
    // by that owner; Advance never grows Special and therefore cannot duplicate its form clock.
    bool SetSpecial(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Value);
    bool AddSpecial(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Amount);
    bool TrySpendSpecial(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Cost);
}
