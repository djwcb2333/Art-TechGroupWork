#include "DWCombatVitals.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    bool FiniteNonNegative(double Value) { return std::isfinite(Value) && Value >= 0.0; }
    bool InRange(double Value, double Maximum)
    {
        return FiniteNonNegative(Value) && Value <= Maximum;
    }

    bool InResourceRange(double Value, double Floor, double Maximum)
    {
        return std::isfinite(Value) && Value >= Floor && Value <= Maximum;
    }

    // Kahan accumulation prevents ordinary frame subdivision from losing small deltas.
    // The carries are numerical state, not a gameplay tolerance; there is no epsilon that
    // awards a damage event before the actual period boundary.
    void AddCompensated(double& Sum, double& Carry, double Value)
    {
        const double Adjusted = Value - Carry;
        const double Next = Sum + Adjusted;
        Carry = (Next - Sum) - Adjusted;
        Sum = Next;
    }

    bool ValidCarry(double Carry, double Scale)
    {
        return std::isfinite(Carry)
            && std::abs(Carry) <= std::numeric_limits<double>::epsilon() * std::max(1.0, Scale);
    }

    bool ValidResource(double Value, double Floor, double Maximum, double DrainCarry,
        double EmptyElapsed, double TimeCarry, double Period)
    {
        const bool bHasResource = Value > Floor || (Value == Floor && DrainCarry < 0.0);
        return InResourceRange(Value, Floor, Maximum) && ValidCarry(DrainCarry, Maximum)
            // Validate the represented two-part value, not only its rounded high component.
            && DrainCarry <= Value - Floor && -DrainCarry <= Maximum - Value
            && (Value != Floor || DrainCarry <= 0.0)
            && (Value != Maximum || DrainCarry >= 0.0)
            && FiniteNonNegative(EmptyElapsed) && EmptyElapsed <= Period
            && ValidCarry(TimeCarry, Period)
            && TimeCarry <= EmptyElapsed && (Period - EmptyElapsed) + TimeCarry > 0.0
            // A compensated value may round up to Period while its positive carry places
            // the represented time strictly below Period. Likewise zero cannot represent negative time.
            && (EmptyElapsed != Period || TimeCarry > 0.0)
            && (EmptyElapsed != 0.0 || TimeCarry <= 0.0)
            && (!bHasResource || (EmptyElapsed == 0.0 && TimeCarry == 0.0));
    }

    bool AdvanceResource(double& Value, double& DrainCarry, double& EmptyElapsed,
        double& TimeCarry, double Floor, double DrainPerSecond, double Period, double DamagePerEvent,
        double DeltaSeconds, FDWCombatVitalsDamageBatch& Batch)
    {
        Batch.DamagePerEvent = DamagePerEvent;
        Batch.PeriodSeconds = Period;
        double EmptyOffset = 0.0;
        if (Value > Floor || (Value == Floor && DrainCarry < 0.0))
        {
            EmptyElapsed = 0.0;
            TimeCarry = 0.0;
            if (DrainPerSecond == 0.0) { return true; }
            // Value - Carry reconstructs the compensated amount before this segment.
            const double TimeToEmpty = ((Value - Floor) - DrainCarry) / DrainPerSecond;
            if (DeltaSeconds < TimeToEmpty)
            {
                const double Drain = DrainPerSecond * DeltaSeconds;
                AddCompensated(Value, DrainCarry, -Drain);
                // Recover product rounding as well as accumulation rounding.
                AddCompensated(Value, DrainCarry, -std::fma(DrainPerSecond, DeltaSeconds, -Drain));
                if (Value < Floor) { return false; }
                return true;
            }
            EmptyOffset = TimeToEmpty;
            Value = Floor;
            DrainCarry = 0.0;
        }

        const double SecondsUntilEvent = (Period - EmptyElapsed) + TimeCarry;
        const double EmptyDelta = DeltaSeconds - EmptyOffset;
        AddCompensated(EmptyElapsed, TimeCarry, EmptyDelta);
        if (!std::isfinite(EmptyElapsed)) { return false; }

        double Count = std::floor((EmptyElapsed - TimeCarry) / Period);
        // Above 2^53-1, double cannot represent every event index. Reject atomically instead
        // of saturating the count, discarding elapsed time, looping, or silently under-damaging.
        constexpr double LargestExactEventCount = 9007199254740991.0;
        if (!FiniteNonNegative(Count) || Count > LargestExactEventCount) { return false; }

        double Remainder = std::fma(-Count, Period, EmptyElapsed) - TimeCarry;
        // A quotient can round to the neighboring integer at a representable boundary.
        // Correct against the residual, with no gameplay epsilon and at most one correction.
        if (Remainder < 0.0 && Count > 0.0)
        {
            Count -= 1.0;
        }
        else if (Remainder >= Period)
        {
            Count += 1.0;
        }
        if (Count > LargestExactEventCount) { return false; }

        Batch.EventCount = static_cast<std::uint64_t>(Count);
        if (Batch.EventCount > 0)
        {
            Batch.FirstEventOffsetSeconds = EmptyOffset + SecondsUntilEvent;
            if (!std::isfinite(Batch.TotalDamage())) { return false; }
        }
        // Keep compensation across frames that did not consume a whole period. After removing
        // full periods, use TwoSum to preserve a sub-ulp distance below the next boundary.
        if (Batch.EventCount > 0)
        {
            const double ResidualHigh = std::fma(-Count, Period, EmptyElapsed);
            const double ResidualLow = -TimeCarry;
            EmptyElapsed = ResidualHigh + ResidualLow;
            const double AddedLow = EmptyElapsed - ResidualHigh;
            TimeCarry = -((ResidualHigh - (EmptyElapsed - AddedLow)) + (ResidualLow - AddedLow));
        }
        return true;
    }

    bool AddResource(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config,
        double Amount, bool bWater)
    {
        if (!State.IsValid(Config) || !FiniteNonNegative(Amount)) { return false; }
        if (Amount == 0.0) { return true; }
        double& Value = bWater ? State.Water : State.Satiety;
        double& Carry = bWater ? State.WaterDrainCarry : State.SatietyDrainCarry;
        const double Maximum = bWater ? Config.WaterMax : Config.SatietyMax;
        const double Floor = bWater ? Config.WaterFloor : Config.SatietyFloor;
        // Compare to headroom before addition so even a finite enormous refill cannot overflow.
        Value = Amount >= Maximum - Value ? Maximum : Value + Amount;
        Carry = 0.0;
        if (Value > Floor)
        {
            (bWater ? State.WaterEmptyElapsedSeconds : State.SatietyEmptyElapsedSeconds) = 0.0;
            (bWater ? State.WaterEmptyTimeCarry : State.SatietyEmptyTimeCarry) = 0.0;
        }
        return true;
    }
}

bool FDWCombatVitalsConfig::IsValid() const
{
    return FiniteNonNegative(HealthFloor) && std::isfinite(HealthMax) && HealthMax > HealthFloor
        && FiniteNonNegative(WaterFloor) && std::isfinite(WaterMax) && WaterMax > WaterFloor
        && FiniteNonNegative(SatietyFloor) && std::isfinite(SatietyMax) && SatietyMax > SatietyFloor
        && FiniteNonNegative(SpecialFloor) && std::isfinite(SpecialMax) && SpecialMax > SpecialFloor
        && InRange(InitialHealthFraction, 1.0) && InRange(InitialWaterFraction, 1.0)
        && InRange(InitialSatietyFraction, 1.0) && InRange(RespawnHealthFraction, 1.0)
        && InRange(RespawnWaterFraction, 1.0) && InRange(RespawnSatietyFraction, 1.0)
        && InResourceRange(HealthMax * InitialHealthFraction, HealthFloor, HealthMax)
        && InResourceRange(HealthMax * RespawnHealthFraction, HealthFloor, HealthMax)
        && InResourceRange(WaterMax * InitialWaterFraction, WaterFloor, WaterMax)
        && InResourceRange(WaterMax * RespawnWaterFraction, WaterFloor, WaterMax)
        && InResourceRange(SatietyMax * InitialSatietyFraction, SatietyFloor, SatietyMax)
        && InResourceRange(SatietyMax * RespawnSatietyFraction, SatietyFloor, SatietyMax)
        && InResourceRange(NormalSpecialValue, SpecialFloor, SpecialMax)
        && FiniteNonNegative(WaterDrainPerSecond) && FiniteNonNegative(SatietyDrainPerSecond)
        && std::isfinite(WaterDamagePeriodSeconds) && WaterDamagePeriodSeconds > 0.0
        && std::isfinite(SatietyDamagePeriodSeconds) && SatietyDamagePeriodSeconds > 0.0
        && FiniteNonNegative(WaterDamagePerEvent) && FiniteNonNegative(SatietyDamagePerEvent);
}

bool FDWCombatVitalsState::IsValid(const FDWCombatVitalsConfig& Config) const
{
    return Config.IsValid() && InResourceRange(Health, Config.HealthFloor, Config.HealthMax)
        && InResourceRange(Special, Config.SpecialFloor, Config.SpecialMax)
        && ValidResource(Water, Config.WaterFloor, Config.WaterMax, WaterDrainCarry, WaterEmptyElapsedSeconds,
            WaterEmptyTimeCarry, Config.WaterDamagePeriodSeconds)
        && ValidResource(Satiety, Config.SatietyFloor, Config.SatietyMax, SatietyDrainCarry, SatietyEmptyElapsedSeconds,
            SatietyEmptyTimeCarry, Config.SatietyDamagePeriodSeconds);
}

double FDWCombatVitalsDamageBatch::TotalDamage() const
{
    return static_cast<double>(EventCount) * DamagePerEvent;
}

bool DWCombatVitals::Initialize(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config,
    EDWCombatVitalsReset Reset)
{
    if (!Config.IsValid()
        || (Reset != EDWCombatVitalsReset::NewCharacter && Reset != EDWCombatVitalsReset::Respawn))
    {
        return false;
    }
    const bool bRespawn = Reset == EDWCombatVitalsReset::Respawn;
    FDWCombatVitalsState Candidate;
    Candidate.Health = Config.HealthMax * (bRespawn ? Config.RespawnHealthFraction : Config.InitialHealthFraction);
    Candidate.Water = Config.WaterMax * (bRespawn ? Config.RespawnWaterFraction : Config.InitialWaterFraction);
    Candidate.Satiety = Config.SatietyMax * (bRespawn ? Config.RespawnSatietyFraction : Config.InitialSatietyFraction);
    Candidate.Special = Config.NormalSpecialValue;
    State = Candidate;
    return true;
}

FDWCombatVitalsAdvanceResult DWCombatVitals::Advance(FDWCombatVitalsState& State,
    const FDWCombatVitalsConfig& Config, double DeltaSeconds, bool bPaused)
{
    FDWCombatVitalsAdvanceResult Result;
    Result.Starvation.Source = EDWCombatVitalsDamageSource::Starvation;
    if (!State.IsValid(Config)) { return Result; }
    if (bPaused)
    {
        Result.bAccepted = true;
        return Result;
    }
    if (!FiniteNonNegative(DeltaSeconds)) { return Result; }
    FDWCombatVitalsState Candidate = State;
    if (!AdvanceResource(Candidate.Water, Candidate.WaterDrainCarry, Candidate.WaterEmptyElapsedSeconds,
            Candidate.WaterEmptyTimeCarry, Config.WaterFloor, Config.WaterDrainPerSecond, Config.WaterDamagePeriodSeconds,
            Config.WaterDamagePerEvent, DeltaSeconds, Result.Dehydration)
        || !AdvanceResource(Candidate.Satiety, Candidate.SatietyDrainCarry, Candidate.SatietyEmptyElapsedSeconds,
            Candidate.SatietyEmptyTimeCarry, Config.SatietyFloor, Config.SatietyDrainPerSecond, Config.SatietyDamagePeriodSeconds,
            Config.SatietyDamagePerEvent, DeltaSeconds, Result.Starvation)
        || !Candidate.IsValid(Config))
    {
        // Do not expose the first resource's events if the second resource rejected the segment.
        Result = FDWCombatVitalsAdvanceResult{};
        Result.Starvation.Source = EDWCombatVitalsDamageSource::Starvation;
        return Result;
    }
    State = Candidate;
    Result.bAccepted = true;
    Result.AdvancedSeconds = DeltaSeconds;
    return Result;
}

bool DWCombatVitals::AddWater(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Amount)
{
    return AddResource(State, Config, Amount, true);
}

bool DWCombatVitals::AddSatiety(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Amount)
{
    return AddResource(State, Config, Amount, false);
}

bool DWCombatVitals::SetSpecial(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Value)
{
    if (!State.IsValid(Config) || !InResourceRange(Value, Config.SpecialFloor, Config.SpecialMax)) { return false; }
    State.Special = Value;
    return true;
}

bool DWCombatVitals::AddSpecial(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Amount)
{
    if (!State.IsValid(Config) || !FiniteNonNegative(Amount)) { return false; }
    State.Special = Amount >= Config.SpecialMax - State.Special ? Config.SpecialMax : State.Special + Amount;
    return true;
}

bool DWCombatVitals::TrySpendSpecial(FDWCombatVitalsState& State, const FDWCombatVitalsConfig& Config, double Cost)
{
    if (!State.IsValid(Config) || !FiniteNonNegative(Cost) || State.Special - Config.SpecialFloor < Cost) { return false; }
    State.Special = std::max(Config.SpecialFloor, State.Special - Cost);
    return true;
}
