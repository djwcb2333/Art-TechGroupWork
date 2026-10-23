#include "DWCombatVitalsRules.h"
#include "DWCombatRules.h"

bool DWCombatVitals::BuildConfig(const FDWCombatRules& Rules, FDWCombatVitalsConfig& OutConfig, FString& OutError)
{
    FDWCombatVitalsConfig Candidate;
    struct FBinding
    {
        const TCHAR* Id;
        EDWCombatRuleUnit Unit;
        double FDWCombatVitalsConfig::* Field;
    };
    const FBinding Bindings[] = {
        {TEXT("player.health.max"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::HealthMax},
        {TEXT("player.health.floor"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::HealthFloor},
        {TEXT("player.hydration.max"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::WaterMax},
        {TEXT("player.hydration.floor"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::WaterFloor},
        {TEXT("player.satiety.max"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::SatietyMax},
        {TEXT("player.satiety.floor"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::SatietyFloor},
        {TEXT("special.max"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::SpecialMax},
        {TEXT("special.floor"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::SpecialFloor},
        {TEXT("special.normal"), EDWCombatRuleUnit::Points, &FDWCombatVitalsConfig::NormalSpecialValue},
        {TEXT("player.health.initial_fraction"), EDWCombatRuleUnit::Fraction, &FDWCombatVitalsConfig::InitialHealthFraction},
        {TEXT("player.hydration.initial_fraction"), EDWCombatRuleUnit::Fraction, &FDWCombatVitalsConfig::InitialWaterFraction},
        {TEXT("player.satiety.initial_fraction"), EDWCombatRuleUnit::Fraction, &FDWCombatVitalsConfig::InitialSatietyFraction},
        {TEXT("respawn.health.fraction"), EDWCombatRuleUnit::Fraction, &FDWCombatVitalsConfig::RespawnHealthFraction},
        {TEXT("respawn.hydration.fraction"), EDWCombatRuleUnit::Fraction, &FDWCombatVitalsConfig::RespawnWaterFraction},
        {TEXT("respawn.satiety.fraction"), EDWCombatRuleUnit::Fraction, &FDWCombatVitalsConfig::RespawnSatietyFraction},
        {TEXT("player.hydration.drain_per_second"), EDWCombatRuleUnit::PointsPerSecond, &FDWCombatVitalsConfig::WaterDrainPerSecond},
        {TEXT("player.satiety.drain_per_second"), EDWCombatRuleUnit::PointsPerSecond, &FDWCombatVitalsConfig::SatietyDrainPerSecond},
        {TEXT("player.hydration.empty_interval_s"), EDWCombatRuleUnit::Seconds, &FDWCombatVitalsConfig::WaterDamagePeriodSeconds},
        {TEXT("player.satiety.empty_interval_s"), EDWCombatRuleUnit::Seconds, &FDWCombatVitalsConfig::SatietyDamagePeriodSeconds},
        {TEXT("player.hydration.empty_damage"), EDWCombatRuleUnit::PointsPerEvent, &FDWCombatVitalsConfig::WaterDamagePerEvent},
        {TEXT("player.satiety.empty_damage"), EDWCombatRuleUnit::PointsPerEvent, &FDWCombatVitalsConfig::SatietyDamagePerEvent}
    };
    for (const FBinding& Binding : Bindings)
    {
        if (!Rules.TryGetNumber(FName(Binding.Id), Binding.Unit, Candidate.*Binding.Field, OutError)) { return false; }
    }
    double HealthRegeneration = 0.0;
    if (!Rules.TryGetNumber(TEXT("player.health.regen_per_s"), EDWCombatRuleUnit::PointsPerSecond,
        HealthRegeneration, OutError)) { return false; }
    if (HealthRegeneration != 0.0)
    {
        OutError = TEXT("D03 does not permit automatic health regeneration; player.health.regen_per_s must be zero.");
        return false;
    }
    if (!Candidate.IsValid())
    {
        OutError = TEXT("D03 vitals configuration has invalid ranges, ratios, periods, or derived values below their floors.");
        return false;
    }

    // Read all six authoritative derived entries as well. They must agree with their validated
    // max * fraction inputs; never silently ignore a mismatch or replace it with a legacy value.
    const TCHAR* DerivedIds[] = {TEXT("player.health.initial"), TEXT("player.hydration.initial"), TEXT("player.satiety.initial"),
        TEXT("respawn.health"), TEXT("respawn.hydration"), TEXT("respawn.satiety")};
    const double Expected[] = {Candidate.HealthMax * Candidate.InitialHealthFraction,
        Candidate.WaterMax * Candidate.InitialWaterFraction, Candidate.SatietyMax * Candidate.InitialSatietyFraction,
        Candidate.HealthMax * Candidate.RespawnHealthFraction, Candidate.WaterMax * Candidate.RespawnWaterFraction,
        Candidate.SatietyMax * Candidate.RespawnSatietyFraction};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(DerivedIds); ++Index)
    {
        double Actual = 0.0;
        if (!Rules.TryGetNumber(FName(DerivedIds[Index]), EDWCombatRuleUnit::Points, Actual, OutError)) { return false; }
        if (Actual != Expected[Index])
        {
            OutError = FString::Printf(TEXT("D03 derived parameter %s disagrees with its max * fraction inputs."), DerivedIds[Index]);
            return false;
        }
    }
    OutConfig = Candidate;
    OutError.Reset();
    return true;
}
