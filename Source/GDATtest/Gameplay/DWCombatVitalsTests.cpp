// Same deterministic cases can run as UE automation or a tiny standalone native executable.
// Standalone: compile this file and DWCombatVitals.cpp with DW_COMBAT_VITALS_STANDALONE defined.
#if WITH_DEV_AUTOMATION_TESTS || defined(DW_COMBAT_VITALS_STANDALONE)

#include "DWCombatVitals.h"
#include <cmath>
#include <initializer_list>
#include <limits>

#if defined(DW_COMBAT_VITALS_STANDALONE)
#include <cstdio>
#else
#include "Misc/AutomationTest.h"
#endif

namespace DWCombatVitalsTests
{
    struct FContext
    {
        void* Owner;
        void (*Sink)(void*, const char*, bool);
        void Check(bool Condition, const char* Message) { Sink(Owner, Message, Condition); }
        void Near(double Actual, double Expected, const char* Message, double Tolerance = 1.e-9)
        {
            Check(std::isfinite(Actual) && std::abs(Actual - Expected) <= Tolerance, Message);
        }
    };

    // An explicit revision105 test fixture, not a fallback or an independent runtime data source.
    FDWCombatVitalsConfig Config()
    {
        FDWCombatVitalsConfig C;
        C.HealthMax = C.WaterMax = C.SatietyMax = C.SpecialMax = 100.0;
        C.InitialHealthFraction = C.RespawnHealthFraction = 1.0;
        C.InitialWaterFraction = C.InitialSatietyFraction = 0.5;
        C.RespawnWaterFraction = C.RespawnSatietyFraction = 0.5;
        C.NormalSpecialValue = 0.0;
        C.WaterDrainPerSecond = 0.1;
        C.SatietyDrainPerSecond = 0.05;
        C.WaterDamagePeriodSeconds = C.SatietyDamagePeriodSeconds = 5.0;
        C.WaterDamagePerEvent = C.SatietyDamagePerEvent = 1.0;
        return C;
    }

    FDWCombatVitalsState NewState(const FDWCombatVitalsConfig& C)
    {
        FDWCombatVitalsState S;
        DWCombatVitals::Initialize(S, C, EDWCombatVitalsReset::NewCharacter);
        return S;
    }

    void InitializationAndExplicitSpecial(FContext& T)
    {
        auto C = Config(); auto S = NewState(C);
        T.Near(S.Health, 100.0, "New health"); T.Near(S.Water, 50.0, "New water");
        T.Near(S.Satiety, 50.0, "New satiety"); T.Near(S.Special, 0.0, "Normal special");
        C.HealthMax = 240.0; C.WaterMax = 80.0; C.SatietyMax = 160.0;
        C.RespawnHealthFraction = 0.75; C.RespawnWaterFraction = 0.25; C.RespawnSatietyFraction = 0.125;
        T.Check(DWCombatVitals::Initialize(S, C, EDWCombatVitalsReset::NewCharacter), "New config accepted");
        T.Near(S.Health, 240.0, "New maximum scales health"); T.Near(S.Water, 40.0, "Water initial ratio");
        T.Check(DWCombatVitals::Initialize(S, C, EDWCombatVitalsReset::Respawn), "Respawn config accepted");
        T.Near(S.Health, 180.0, "Independent respawn health ratio");
        T.Near(S.Water, 20.0, "Independent respawn water ratio"); T.Near(S.Satiety, 20.0, "Independent respawn satiety ratio");
        T.Check(DWCombatVitals::SetSpecial(S, C, 25.0), "Explicit injection special");
        S.Health = 17.0;
        T.Check(DWCombatVitals::Advance(S, C, 10.0, false).bAccepted, "Normal advance");
        T.Near(S.Health, 17.0, "Never auto-heals"); T.Near(S.Special, 25.0, "No second special clock");
        T.Check(DWCombatVitals::AddSpecial(S, C, 500.0), "Explicit growth"); T.Near(S.Special, 100.0, "Special capped");
        T.Check(!DWCombatVitals::TrySpendSpecial(S, C, 101.0), "Insufficient cost rejects atomically");
        T.Near(S.Special, 100.0, "Rejected cost preserves value");
        T.Check(DWCombatVitals::TrySpendSpecial(S, C, 100.0), "Explicit affordable cost");
        T.Near(S.Special, 0.0, "Cost does not trigger or end a form here");
    }

    void ExactDepletionAndIndependentPackets(FContext& T)
    {
        const auto C = Config(); auto S = NewState(C); S.Water = S.Satiety = 0.2;
        const auto R = DWCombatVitals::Advance(S, C, 12.0, false);
        T.Check(R.bAccepted, "Mixed depletion advance accepted");
        T.Check(R.Dehydration.EventCount == 2, "Water empties at 2, events at 7 and 12");
        T.Check(R.Starvation.EventCount == 1, "Satiety empties at 4, event at 9");
        T.Near(R.Dehydration.FirstEventOffsetSeconds, 7.0, "Water first damage offset");
        T.Near(R.Starvation.FirstEventOffsetSeconds, 9.0, "Satiety first damage offset");
        T.Near(S.WaterEmptyElapsedSeconds, 0.0, "Water period remainder");
        T.Near(S.SatietyEmptyElapsedSeconds, 3.0, "Satiety independent remainder");
        T.Check(R.Dehydration.Source == EDWCombatVitalsDamageSource::Dehydration, "Water source tag");
        T.Check(R.Starvation.Source == EDWCombatVitalsDamageSource::Starvation, "Satiety source tag");
        T.Check(R.Dehydration.bBypassFormReductionAndBlocking && R.Starvation.bBypassFormReductionAndBlocking,
            "Both sources bypass blocking and form reductions");
        T.Near(R.Dehydration.TotalDamage() + R.Starvation.TotalDamage(), 3.0, "Separate damage total");
        T.Near(S.Health, 100.0, "Only the external damage authority may subtract health");
    }

    void TrueBoundaryNoPrematureEvent(FContext& T)
    {
        const auto C = Config(); auto S = NewState(C); S.Water = 0.0;
        const double Before = std::nextafter(5.0, 0.0);
        auto R = DWCombatVitals::Advance(S, C, Before, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 0, "One representable step before period never damages");
        R = DWCombatVitals::Advance(S, C, 5.0 - Before, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 1, "Exact period emits once");
        T.Near(S.WaterEmptyElapsedSeconds, 0.0, "Exact boundary has no carry period");
        R = DWCombatVitals::Advance(S, C, 0.0, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 0, "Zero delta cannot replay an event");
        S = NewState(C); S.Water = 0.1;
        R = DWCombatVitals::Advance(S, C, 1.0, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 0, "Depletion itself does not immediately damage");
        T.Near(S.Water, 0.0, "Exact depletion clamps to zero");
        R = DWCombatVitals::Advance(S, C, 5.0, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 1, "Full empty period follows depletion");
        S = NewState(C); S.Water = 0.0;
        S.WaterEmptyElapsedSeconds = 5.0; S.WaterEmptyTimeCarry = 1.e-16;
        T.Check(S.IsValid(C), "High component may round to period while compensated value is still below it");
        R = DWCombatVitals::Advance(S, C, 0.0, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 0, "Valid compensated boundary cannot emit at zero delta");
        R = DWCombatVitals::Advance(S, C, 1.e-16, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 1, "Sub-ulp remaining time finishes exactly");
        T.Check(R.Dehydration.FirstEventOffsetSeconds == 1.e-16, "Compensated boundary keeps positive exact event offset");
    }

    void CrossFrameIntegration(FContext& T)
    {
        const auto C = Config(); const double Duration = 1012.25;
        auto One = NewState(C); const auto Expected = DWCombatVitals::Advance(One, C, Duration, false);
        T.Check(Expected.bAccepted, "Single long segment accepted");
        T.Check(Expected.Dehydration.EventCount == 102 && Expected.Starvation.EventCount == 2, "Fixture event counts");
        for (const int Rate : { 15, 30, 60, 144 })
        {
            auto Split = NewState(C); std::uint64_t WaterCount = 0, SatietyCount = 0;
            const double Step = 1.0 / Rate; const int Frames = static_cast<int>(Duration / Step);
            bool bAccepted = true;
            for (int Frame = 0; Frame < Frames; ++Frame)
            {
                const auto R = DWCombatVitals::Advance(Split, C, Step, false);
#if defined(DW_COMBAT_VITALS_STANDALONE)
                if (!R.bAccepted && bAccepted)
                {
                    std::fprintf(stderr, "First rejected subdivision: rate=%d frame=%d water=%.17g waterCarry=%.17g empty=%.17g timeCarry=%.17g satiety=%.17g satietyCarry=%.17g\n",
                        Rate, Frame, Split.Water, Split.WaterDrainCarry, Split.WaterEmptyElapsedSeconds,
                        Split.WaterEmptyTimeCarry, Split.Satiety, Split.SatietyDrainCarry);
                }
#endif
                bAccepted = bAccepted && R.bAccepted;
                WaterCount += R.Dehydration.EventCount; SatietyCount += R.Starvation.EventCount;
            }
            const auto Tail = DWCombatVitals::Advance(Split, C, Duration - Frames * Step, false);
            WaterCount += Tail.Dehydration.EventCount; SatietyCount += Tail.Starvation.EventCount;
            T.Check(bAccepted && Tail.bAccepted, "Every subdivided frame accepted");
            T.Check(WaterCount == Expected.Dehydration.EventCount, "Water event count independent of frame rate");
            T.Check(SatietyCount == Expected.Starvation.EventCount, "Satiety event count independent of frame rate");
            T.Near(Split.WaterEmptyElapsedSeconds, One.WaterEmptyElapsedSeconds, "Water partial period preserved", 1.e-8);
            T.Near(Split.SatietyEmptyElapsedSeconds, One.SatietyEmptyElapsedSeconds, "Satiety partial period preserved", 1.e-8);
        }
    }

    void SimultaneousAndLargeDelta(FContext& T)
    {
        const auto C = Config(); auto S = NewState(C); S.Water = S.Satiety = 0.0;
        const auto R = DWCombatVitals::Advance(S, C, 1000000001.25, false);
        T.Check(R.bAccepted, "Huge finite delta accepted without per-event allocation or loop");
        T.Check(R.Dehydration.EventCount == 200000000 && R.Starvation.EventCount == 200000000,
            "Two simultaneous sources retain all independent events");
        T.Near(R.Dehydration.FirstEventOffsetSeconds, 5.0, "First huge-segment event");
        T.Near(R.Starvation.FirstEventOffsetSeconds, 5.0, "Simultaneous event has independent source");
        T.Near(S.WaterEmptyElapsedSeconds, 1.25, "Huge delta water remainder");
        T.Near(S.SatietyEmptyElapsedSeconds, 1.25, "Huge delta satiety remainder");
    }

    void PauseAndSnapshotContinuation(FContext& T)
    {
        const auto C = Config(); auto Original = NewState(C); Original.Water = Original.Satiety = 0.0;
        T.Check(DWCombatVitals::Advance(Original, C, 3.125, false).bAccepted, "Build a partial saved period");
        auto Restored = Original;
        T.Check(Restored.IsValid(C), "Full value snapshot validates without reinitialization");
        auto Paused = DWCombatVitals::Advance(Restored, C, 3600.0, true);
        T.Check(Paused.bAccepted && Paused.AdvancedSeconds == 0.0, "Pause discards delta, never banks it");
        T.Check(Paused.Dehydration.EventCount == 0 && Paused.Starvation.EventCount == 0, "Pause emits no damage");
        T.Near(Restored.WaterEmptyElapsedSeconds, 3.125, "Pause preserves partial water period exactly");
        const auto A = DWCombatVitals::Advance(Original, C, 1.875, false);
        const auto B = DWCombatVitals::Advance(Restored, C, 1.875, false);
        T.Check(A.Dehydration.EventCount == 1 && B.Dehydration.EventCount == 1, "Restored partial period finishes once");
        T.Check(A.Starvation.EventCount == B.Starvation.EventCount, "Independent saved satiety phase preserved");
        T.Near(B.Dehydration.FirstEventOffsetSeconds, 1.875, "Restored remaining time is exact");
        T.Near(Restored.Health, 100.0, "Pause/load never heals or directly damages");
    }

    void RefillsAndZeroDrain(FContext& T)
    {
        auto C = Config(); auto S = NewState(C); S.Water = S.Satiety = 0.0;
        DWCombatVitals::Advance(S, C, 3.5, false);
        T.Check(DWCombatVitals::AddWater(S, C, 0.0), "Zero refill accepted as no-op");
        T.Near(S.WaterEmptyElapsedSeconds, 3.5, "Zero refill cannot erase accumulated empty time");
        T.Check(DWCombatVitals::AddWater(S, C, 0.1), "Positive internal refill accepted");
        T.Near(S.WaterEmptyElapsedSeconds, 0.0, "Positive refill clears only its resource empty period");
        T.Near(S.SatietyEmptyElapsedSeconds, 3.5, "Refill leaves other resource period intact");
        const auto R = DWCombatVitals::Advance(S, C, 4.0, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 0 && R.Starvation.EventCount == 1,
            "Refilled resource must deplete again before starting a full empty period");
        T.Near(S.WaterEmptyElapsedSeconds, 3.0, "New empty period starts at exact depletion point");
        T.Check(DWCombatVitals::AddSatiety(S, C, std::numeric_limits<double>::max()), "Huge finite refill safely saturates");
        T.Near(S.Satiety, C.SatietyMax, "Refill clamps to configured maximum without overflow");
        C.WaterDrainPerSecond = C.SatietyDrainPerSecond = 0.0;
        S = NewState(C); const auto NoDrain = DWCombatVitals::Advance(S, C, 1000000.0, false);
        T.Check(NoDrain.bAccepted && NoDrain.Dehydration.EventCount == 0, "Positive zero-drain resource never depletes");
        T.Near(S.Water, 50.0, "Zero drain preserves amount");
        S.Water = 0.0;
        T.Check(DWCombatVitals::Advance(S, C, 5.0, false).Dehydration.EventCount == 1,
            "Already empty resource still uses its independent damage period when drain is zero");
    }

    void InvalidInputsAreAtomic(FContext& T)
    {
        const auto C = Config(); auto S = NewState(C); S.Water = S.Satiety = 0.0;
        DWCombatVitals::Advance(S, C, 2.0, false);
        for (const double BadDelta : { -1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1.e30 })
        {
            const auto R = DWCombatVitals::Advance(S, C, BadDelta, false);
            T.Check(!R.bAccepted && R.Dehydration.EventCount == 0 && R.Starvation.EventCount == 0,
                "Invalid or unrepresentable advance exposes no partial damage");
            T.Near(S.WaterEmptyElapsedSeconds, 2.0, "Rejected advance preserves water clock");
            T.Near(S.SatietyEmptyElapsedSeconds, 2.0, "Rejected advance preserves satiety clock");
        }
        auto BadConfig = C; BadConfig.SatietyDamagePerEvent = std::numeric_limits<double>::max();
        const auto R = DWCombatVitals::Advance(S, BadConfig, 20.0, false);
        T.Check(!R.bAccepted && R.Dehydration.EventCount == 0, "Second-source overflow rolls back first-source work");
        T.Near(S.WaterEmptyElapsedSeconds, 2.0, "Atomic rejection preserves state");
        BadConfig = C; BadConfig.WaterDamagePeriodSeconds = 0.0;
        T.Check(!DWCombatVitals::Initialize(S, BadConfig, EDWCombatVitalsReset::NewCharacter), "Invalid reset never overwrites state");
        T.Near(S.WaterEmptyElapsedSeconds, 2.0, "Rejected reset preserves saved phase");
        T.Check(!DWCombatVitals::AddWater(S, C, -1.0), "Negative refill rejected");
        T.Check(!DWCombatVitals::SetSpecial(S, C, std::numeric_limits<double>::quiet_NaN()), "NaN special rejected");
        T.Check(!FDWCombatVitalsConfig{}.IsValid(), "Missing rules bridge cannot silently activate fallback parameters");
        auto Forged = NewState(C); Forged.Water = C.WaterFloor;
        Forged.WaterEmptyElapsedSeconds = std::nextafter(C.WaterDamagePeriodSeconds, 0.0);
        Forged.WaterEmptyTimeCarry = -std::numeric_limits<double>::epsilon() * C.WaterDamagePeriodSeconds;
        const double SavedElapsed = Forged.WaterEmptyElapsedSeconds, SavedCarry = Forged.WaterEmptyTimeCarry;
        T.Check(!Forged.IsValid(C), "Two-component saved time above period rejected even if high component is below period");
        const auto ForgedResult = DWCombatVitals::Advance(Forged, C, 0.0, false);
        T.Check(!ForgedResult.bAccepted && ForgedResult.Dehydration.EventCount == 0,
            "Corrupt compensated snapshot cannot create a zero-delta or negative-offset event");
        T.Check(Forged.WaterEmptyElapsedSeconds == SavedElapsed && Forged.WaterEmptyTimeCarry == SavedCarry,
            "Rejected compensated snapshot remains unchanged for diagnosis");
        Forged = NewState(C); Forged.Water = std::nextafter(C.WaterFloor, C.WaterMax);
        Forged.WaterDrainCarry = std::numeric_limits<double>::epsilon();
        T.Check(!Forged.IsValid(C), "Two-component resource below floor rejected");
        Forged = NewState(C); Forged.Water = std::nextafter(C.WaterMax, C.WaterFloor);
        Forged.WaterDrainCarry = -std::numeric_limits<double>::epsilon() * C.WaterMax;
        T.Check(!Forged.IsValid(C), "Two-component resource above maximum rejected");
        Forged = NewState(C); Forged.Water = C.WaterFloor;
        Forged.WaterEmptyElapsedSeconds = std::nextafter(0.0, 1.0);
        Forged.WaterEmptyTimeCarry = std::numeric_limits<double>::epsilon();
        T.Check(!Forged.IsValid(C), "Two-component empty time below zero rejected");
    }

    void ConfigurableFloors(FContext& T)
    {
        auto C = Config();
        C.HealthFloor = 1.0; C.WaterFloor = 10.0; C.SatietyFloor = 20.0; C.SpecialFloor = 5.0;
        C.NormalSpecialValue = 5.0; C.WaterDrainPerSecond = 1.0; C.SatietyDrainPerSecond = 2.0;
        auto S = NewState(C);
        T.Check(C.IsValid() && S.IsValid(C), "All four nonzero floors supported");
        T.Near(S.Water, 50.0, "Initialization stays ratio times maximum, not floor interpolation");
        S.Water = 11.0; S.Satiety = 22.0;
        const auto R = DWCombatVitals::Advance(S, C, 6.0, false);
        T.Check(R.bAccepted && R.Dehydration.EventCount == 1 && R.Starvation.EventCount == 1,
            "Both empty timers begin at configured floors");
        T.Near(S.Water, 10.0, "Water clamps to floor"); T.Near(S.Satiety, 20.0, "Satiety clamps to floor");
        T.Check(!DWCombatVitals::SetSpecial(S, C, 4.0), "Special setter respects floor");
        T.Check(!DWCombatVitals::TrySpendSpecial(S, C, 0.1), "Cannot spend below special floor");
        T.Check(DWCombatVitals::AddSpecial(S, C, 2.0) && DWCombatVitals::TrySpendSpecial(S, C, 2.0),
            "Spend returns exactly to configured floor");
        auto Invalid = C; Invalid.InitialHealthFraction = 0.005;
        T.Check(!Invalid.IsValid(), "Ratio-derived initial value below floor rejected, never silently clamped");
        Invalid = C; Invalid.HealthFloor = Invalid.HealthMax;
        T.Check(!Invalid.IsValid(), "Empty resource range rejected");
        C.WaterDrainPerSecond = 0.1; C.SatietyDrainPerSecond = 0.05;
        S = NewState(C); bool bAccepted = true; std::uint64_t WaterCount = 0;
        for (int Frame = 0; Frame < 144 * 406; ++Frame)
        {
            const auto FrameResult = DWCombatVitals::Advance(S, C, 1.0 / 144.0, false);
            bAccepted = bAccepted && FrameResult.bAccepted; WaterCount += FrameResult.Dehydration.EventCount;
        }
        T.Check(bAccepted && WaterCount == 1, "Compensated integration also crosses a nonzero floor at high frame rates");
    }
}

#if defined(DW_COMBAT_VITALS_STANDALONE)
int main()
{
    int Failures = 0;
    DWCombatVitalsTests::FContext T{ &Failures, [](void* Owner, const char* Message, bool bPassed)
    {
        if (!bPassed) { ++*static_cast<int*>(Owner); std::fprintf(stderr, "FAIL: %s\n", Message); }
    } };
    using namespace DWCombatVitalsTests;
    InitializationAndExplicitSpecial(T); ExactDepletionAndIndependentPackets(T); TrueBoundaryNoPrematureEvent(T);
    CrossFrameIntegration(T); SimultaneousAndLargeDelta(T); PauseAndSnapshotContinuation(T);
    RefillsAndZeroDrain(T); InvalidInputsAreAtomic(T); ConfigurableFloors(T);
    std::printf("DWCombatVitals: 9 deterministic scenarios, %d failures\n", Failures);
    return Failures == 0 ? 0 : 1;
}
#else
namespace DWCombatVitalsTests
{
    void AutomationSink(void* Owner, const char* Message, bool bPassed)
    {
        static_cast<FAutomationTestBase*>(Owner)->TestTrue(UTF8_TO_TCHAR(Message), bPassed);
    }
}
#define DW_VITALS_AUTOMATION(Name) \
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatVitals##Name, "DoughWorld.Combat.Vitals." #Name, \
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
    bool FDWCombatVitals##Name::RunTest(const FString& Parameters) \
    { \
        DWCombatVitalsTests::FContext Context{this, DWCombatVitalsTests::AutomationSink}; \
        DWCombatVitalsTests::Name(Context); \
        return true; \
    }
DW_VITALS_AUTOMATION(InitializationAndExplicitSpecial)
DW_VITALS_AUTOMATION(ExactDepletionAndIndependentPackets)
DW_VITALS_AUTOMATION(TrueBoundaryNoPrematureEvent)
DW_VITALS_AUTOMATION(CrossFrameIntegration)
DW_VITALS_AUTOMATION(SimultaneousAndLargeDelta)
DW_VITALS_AUTOMATION(PauseAndSnapshotContinuation)
DW_VITALS_AUTOMATION(RefillsAndZeroDrain)
DW_VITALS_AUTOMATION(InvalidInputsAreAtomic)
DW_VITALS_AUTOMATION(ConfigurableFloors)
#undef DW_VITALS_AUTOMATION
#endif
#endif
