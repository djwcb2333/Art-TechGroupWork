#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatInputLedger.h"
#include "Misc/AutomationTest.h"

namespace DWCombatInputTests
{
const FName Primary(TEXT("Primary"));
const FName Secondary(TEXT("Secondary"));
const FName Dash(TEXT("Dash"));
const FName Left(TEXT("LeftMouseButton"));
const FName Right(TEXT("RightMouseButton"));
const FName Space(TEXT("SpaceBar"));

struct FFixture
{
    FDWCombatInputContext Context{1, 1, 1};
    FDWCombatInputLedger Ledger;
    FFixture() { Ledger.ResetContext(Context); }

    bool Down(FName Key, FDWCombatInputEdge& Edge)
    { return Ledger.ObserveKey(Context, Key, true, false, Edge); }
    bool Up(FName Key, FDWCombatInputEdge& Edge)
    { return Ledger.ObserveKey(Context, Key, false, false, Edge); }
    bool Hold(FName Key, FName Action, FDWCombatInputPress& Press)
    {
        FDWCombatInputEdge Edge;
        return Down(Key, Edge) && Ledger.TryConsumePress(Edge, Action, Press) && Ledger.ActivateHold(Press);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputOneConsumerTest,
    "DoughWorld.CombatInput.OneEdgeOneConsumer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputOneConsumerTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInputTests;
    FFixture F;
    FDWCombatInputEdge Down, Ignored, Up;
    FDWCombatInputPress Press, Duplicate;
    TestTrue(TEXT("A real Down produces an edge"), F.Down(Left, Down));
    TestTrue(TEXT("Edge carries owning world, life, and binding generation"), Down.Context == F.Context);
    TestEqual(TEXT("First physical transition has a nonzero sequence"), Down.Sequence, uint64(1));
    TestFalse(TEXT("Duplicate Down does not manufacture another edge"), F.Down(Left, Ignored));
    TestFalse(TEXT("OS key repeat does not manufacture another edge"),
        F.Ledger.ObserveKey(F.Context, Left, true, true, Ignored));
    TestEqual(TEXT("Suppressed duplicates do not advance sequence"), F.Ledger.GetLastSequence(), Down.Sequence);
    TestTrue(TEXT("One gameplay consumer receives the press"), F.Ledger.TryConsumePress(Down, Primary, Press));
    TestFalse(TEXT("Consuming is not yet an accepted hold"), F.Ledger.IsHeld(Primary));
    TestFalse(TEXT("Same action cannot consume a ticket twice"), F.Ledger.TryConsumePress(Down, Primary, Duplicate));
    TestFalse(TEXT("Another mapped action cannot take the same ticket"), F.Ledger.TryConsumePress(Down, Secondary, Duplicate));
    FDWCombatInputPress Forged = Press;
    Forged.Action = Secondary;
    TestFalse(TEXT("Changing Action on a consumed ticket cannot activate a different hold"), F.Ledger.ActivateHold(Forged));
    TestTrue(TEXT("Accepted request may activate its own hold"), F.Ledger.ActivateHold(Press));
    TestFalse(TEXT("Hold activation is also once-only"), F.Ledger.ActivateHold(Press));
    F.Ledger.FinishInputBatch();
    TestTrue(TEXT("Ending the batch does not release an accepted hold"), F.Ledger.IsHeld(Primary));
    TestTrue(TEXT("Real Up produces a physical release"), F.Up(Left, Up));
    TestFalse(TEXT("Duplicate UI/controller release is ignored"), F.Up(Left, Ignored));
    const auto Released = F.Ledger.DrainReleasedHolds();
    if (TestEqual(TEXT("Exactly one accepted-hold release"), Released.Num(), 1))
    {
        TestEqual(TEXT("Release retains original press identity"), Released[0].Press.Edge.Sequence, Press.Edge.Sequence);
        TestEqual(TEXT("Release has its own later identity"), Released[0].Edge.Sequence, Up.Sequence);
        TestTrue(TEXT("Release retains current epoch"), Released[0].Edge.Context == F.Context);
        TestFalse(TEXT("No other key holds this action"), Released[0].bActionStillHeldAfterRelease);
    }
    TestEqual(TEXT("Notifications drain once"), F.Ledger.DrainReleasedHolds().Num(), 0);
    TestFalse(TEXT("Release cannot itself be consumed as a new action"), F.Ledger.TryConsumePress(Up, Primary, Duplicate));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputQuickTapTest,
    "DoughWorld.CombatInput.QuickTapsAndBatchRetirement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputQuickTapTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInputTests;
    FFixture F;
    FDWCombatInputEdge First, Second, Up, UIOnly;
    FDWCombatInputPress FirstPress, SecondPress, Late;
    TestTrue(TEXT("First rapid Down"), F.Down(Left, First));
    TestTrue(TEXT("First rapid Up before input callbacks"), F.Up(Left, Up));
    TestTrue(TEXT("Second rapid Down in same input batch"), F.Down(Left, Second));
    TestTrue(TEXT("Second rapid Up before input callbacks"), F.Up(Left, Up));
    TestTrue(TEXT("Argument-less relay consumes first completed tap in order"), F.Ledger.TryConsumeNextPress(Left, Primary, FirstPress));
    TestEqual(TEXT("First relay receives first physical Down"), FirstPress.Edge.Sequence, First.Sequence);
    TestTrue(TEXT("Next relay consumes second completed tap in order"), F.Ledger.TryConsumeNextPress(Left, Primary, SecondPress));
    TestEqual(TEXT("Second relay receives second physical Down"), SecondPress.Edge.Sequence, Second.Sequence);
    TestFalse(TEXT("No third tap can be invented by another callback"), F.Ledger.TryConsumeNextPress(Left, Primary, Late));
    TestTrue(TEXT("Both taps retain different sequence identities"), FirstPress.Edge.Sequence < SecondPress.Edge.Sequence);
    TestFalse(TEXT("Completed tap cannot become a late hold"), F.Ledger.ActivateHold(FirstPress));
    TestFalse(TEXT("Other completed tap also cannot become a late hold"), F.Ledger.ActivateHold(SecondPress));
    TestEqual(TEXT("No accepted hold means no fabricated release callback"), F.Ledger.DrainReleasedHolds().Num(), 0);

    TestTrue(TEXT("A UI-only physical press is observed"), F.Down(Right, UIOnly));
    F.Ledger.FinishInputBatch();
    TestFalse(TEXT("A UI-consumed press cannot become a later gameplay action"), F.Ledger.TryConsumePress(UIOnly, Secondary, Late));
    TestFalse(TEXT("Retired press cannot be consumed again"), F.Ledger.TryConsumePress(Second, Primary, Late));
    TestTrue(TEXT("Retiring input candidates does not falsify physical state"), F.Ledger.IsPhysicallyDown(Right));
    TestTrue(TEXT("UI-only release is recorded without creating an action"), F.Up(Right, Up));
    TestEqual(TEXT("UI-only press/release has no gameplay release"), F.Ledger.DrainReleasedHolds().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputPauseReleaseTest,
    "DoughWorld.CombatInput.PausedReleaseAndRepress", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputPauseReleaseTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInputTests;
    FFixture F;
    FDWCombatInputPress Shot, Block, Run, Rejected, NewShot;
    TestTrue(TEXT("Gun-style held input begins"), F.Hold(Left, Primary, Shot));
    TestTrue(TEXT("Shield-style held input begins"), F.Hold(Right, Secondary, Block));
    TestTrue(TEXT("Dash-style held input begins"), F.Hold(Space, Dash, Run));
    F.Ledger.FinishInputBatch();
    F.Ledger.BeginSuspension();
    FDWCombatInputEdge ReleaseShot, RepressedShot, PausedNew, Ignored;
    TestTrue(TEXT("UI-consumed mouse release can still be observed while paused"), F.Up(Left, ReleaseShot));
    TestTrue(TEXT("Release explicitly identifies suspended capture"), ReleaseShot.bDuringSuspension);
    TestFalse(TEXT("Old gun hold clears immediately in input ledger"), F.Ledger.IsHeld(Primary));
    TestTrue(TEXT("Unreleased block remains held"), F.Ledger.IsHeld(Secondary));
    TestEqual(TEXT("Gameplay cannot drain notifications during suspension"), F.Ledger.DrainReleasedHolds().Num(), 0);
    TestTrue(TEXT("Same mouse is pressed again in paused UI"), F.Down(Left, RepressedShot));
    TestFalse(TEXT("Repress during pause is not an action"), F.Ledger.TryConsumePress(RepressedShot, Primary, Rejected));
    TestTrue(TEXT("A different key can be observed during pause"), F.Down(FName(TEXT("R")), PausedNew));
    TestFalse(TEXT("New paused key is not an action"), F.Ledger.TryConsumePress(PausedNew, FName(TEXT("Reload")), Rejected));

    const auto Released = F.Ledger.ResumeAndDrainReleases();
    if (TestEqual(TEXT("Resume returns the original release exactly once"), Released.Num(), 1))
    {
        TestEqual(TEXT("Resume does not attribute release to paused repress"), Released[0].Press.Edge.Sequence, Shot.Edge.Sequence);
        TestEqual(TEXT("Resume retains actual physical release sequence"), Released[0].Edge.Sequence, ReleaseShot.Sequence);
    }
    TestFalse(TEXT("Paused repress cannot be consumed after resume"), F.Ledger.TryConsumePress(RepressedShot, Primary, Rejected));
    TestFalse(TEXT("Paused new key cannot be consumed after resume"), F.Ledger.TryConsumePress(PausedNew, FName(TEXT("Reload")), Rejected));
    TestFalse(TEXT("A still-down paused key is not a new edge"), F.Down(Left, Ignored));
    TestEqual(TEXT("Repeated resume does not duplicate release"), F.Ledger.ResumeAndDrainReleases().Num(), 0);
    TestTrue(TEXT("Previously unreleased dash survives pause"), F.Ledger.IsHeld(Dash));
    TestTrue(TEXT("Physical release exits quarantine"), F.Up(Left, Ignored));
    TestEqual(TEXT("Quarantined repress had no accepted hold"), F.Ledger.DrainReleasedHolds().Num(), 0);
    TestTrue(TEXT("Fresh post-resume press may start a new hold"), F.Hold(Left, Primary, NewShot));
    TestTrue(TEXT("Fresh shot is a new sequence"), NewShot.Edge.Sequence > RepressedShot.Sequence);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputSuspensionBoundaryTest,
    "DoughWorld.CombatInput.SuspensionKeepsOnlyAcceptedHolds", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputSuspensionBoundaryTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInputTests;
    FFixture F;
    FDWCombatInputPress Held, ConsumedButNotAccepted, IgnoredPress;
    FDWCombatInputEdge Unconsumed, ConsumedEdge, Ignored;
    TestTrue(TEXT("One hold is genuinely accepted"), F.Hold(Left, Primary, Held));
    TestTrue(TEXT("Another physical Down is still unconsumed"), F.Down(Right, Unconsumed));
    TestTrue(TEXT("A request candidate exists"), F.Down(Space, ConsumedEdge));
    TestTrue(TEXT("Candidate is consumed but its action gate has not accepted it"), F.Ledger.TryConsumePress(ConsumedEdge, Dash, ConsumedButNotAccepted));
    F.Ledger.BeginSuspension();
    F.Ledger.BeginSuspension();
    TestFalse(TEXT("OS repeat while paused does not add an edge"), F.Ledger.ObserveKey(F.Context, Left, true, true, Ignored));
    TestTrue(TEXT("Existing hold stays present while paused"), F.Ledger.IsHeld(Primary));
    TestFalse(TEXT("A consumed candidate cannot activate during pause"), F.Ledger.ActivateHold(ConsumedButNotAccepted));
    TestEqual(TEXT("Unreleased pause returns no release"), F.Ledger.ResumeAndDrainReleases().Num(), 0);
    TestTrue(TEXT("Previously accepted hold resumes unchanged"), F.Ledger.IsHeld(Primary));
    TestFalse(TEXT("Unconsumed pre-pause press cannot be replayed after resume"), F.Ledger.TryConsumePress(Unconsumed, Secondary, IgnoredPress));
    TestFalse(TEXT("Consumed but unaccepted pre-pause candidate cannot activate late"), F.Ledger.ActivateHold(ConsumedButNotAccepted));
    TestFalse(TEXT("Physical hold without new Down cannot start a second action"), F.Down(Left, Ignored));
    TestTrue(TEXT("Releasing the genuinely accepted hold still works"), F.Up(Left, Ignored));
    TestEqual(TEXT("It releases once after resume"), F.Ledger.DrainReleasedHolds().Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputEpochTest,
    "DoughWorld.CombatInput.EpochAndBindingIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputEpochTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInputTests;
    FFixture F;
    FDWCombatInputPress OldPress, Replayed, NewPress;
    TestTrue(TEXT("Initial held input"), F.Hold(Left, Primary, OldPress));
    TestFalse(TEXT("Identical reset cannot wipe consumed identities"), F.Ledger.ResetContext(F.Context));
    TestFalse(TEXT("Zero epoch cannot reset a live session"), F.Ledger.ResetContext({0, 9, 9}));
    TestTrue(TEXT("Rejected reset leaves existing hold intact"), F.Ledger.IsHeld(Primary));
    TestFalse(TEXT("Ticket remains consumed after rejected reset"), F.Ledger.TryConsumePress(OldPress.Edge, Primary, Replayed));

    FDWCombatInputContext NewBinding{1, 1, 2};
    TestTrue(TEXT("A strictly newer binding generation resets the context"), F.Ledger.ResetContext(NewBinding, TArray<FName>{Right}));
    TestFalse(TEXT("Rebinding invalidates old action holds"), F.Ledger.IsHeld(Primary));
    TestTrue(TEXT("Known physical Down survives only as quarantine"), F.Ledger.IsPhysicallyDown(Left));
    TestTrue(TEXT("External physical snapshot seeds quarantine, not a new press"), F.Ledger.IsPhysicallyDown(Right));
    TestEqual(TEXT("Reset does not manufacture an input sequence"), F.Ledger.GetLastSequence(), OldPress.Edge.Sequence);
    TestFalse(TEXT("Old binding ticket cannot activate"), F.Ledger.ActivateHold(OldPress));
    TestFalse(TEXT("Old binding reset cannot roll context backward"), F.Ledger.ResetContext(F.Context));
    FDWCombatInputEdge Edge;
    TestFalse(TEXT("Stale-context release cannot clear current physical state"), F.Ledger.ObserveKey(F.Context, Left, false, false, Edge));
    TestTrue(TEXT("Stale release left the current Down intact"), F.Ledger.IsPhysicallyDown(Left));
    TestFalse(TEXT("Already-held snapshot does not produce Down"), F.Ledger.ObserveKey(NewBinding, Right, true, false, Edge));
    TestTrue(TEXT("Current-context release unlocks the old key"), F.Ledger.ObserveKey(NewBinding, Left, false, false, Edge));
    TestEqual(TEXT("Invalidated hold has no ghost release"), F.Ledger.DrainReleasedHolds().Num(), 0);
    TestTrue(TEXT("New-context fresh press creates its own edge"), F.Ledger.ObserveKey(NewBinding, Left, true, false, Edge));
    TestTrue(TEXT("New edge sequence stays monotonic"), Edge.Sequence > OldPress.Edge.Sequence);
    TestTrue(TEXT("New binding press is eligible"), F.Ledger.TryConsumePress(Edge, Primary, NewPress));
    TestTrue(TEXT("New binding hold activates"), F.Ledger.ActivateHold(NewPress));

    TestTrue(TEXT("New life may start its binding counter anew"), F.Ledger.ResetContext({1, 2, 1}));
    TestFalse(TEXT("Old life with huge binding cannot roll life backward"), F.Ledger.ResetContext({1, 1, 999}));
    TestFalse(TEXT("Previous life ticket cannot activate"), F.Ledger.ActivateHold(NewPress));
    TestTrue(TEXT("New world may start its life counter anew"), F.Ledger.ResetContext({2, 1, 1}));
    TestFalse(TEXT("Old world with huge life cannot roll world backward"), F.Ledger.ResetContext({1, 999, 999}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputLateActivationTest,
    "DoughWorld.CombatInput.NoLateHoldActivation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputLateActivationTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInputTests;
    FFixture F;
    FDWCombatInputEdge First, Up, Second;
    FDWCombatInputPress Pending, Replacement;
    TestTrue(TEXT("Request press"), F.Down(Left, First));
    TestTrue(TEXT("Request consumes its input identity"), F.Ledger.TryConsumePress(First, Primary, Pending));
    TestTrue(TEXT("Release occurs before delayed action acceptance"), F.Up(Left, Up));
    TestFalse(TEXT("Released press cannot activate late"), F.Ledger.ActivateHold(Pending));
    TestTrue(TEXT("A different press is now physically Down"), F.Down(Left, Second));
    TestFalse(TEXT("Down from a later press cannot revive the first press"), F.Ledger.ActivateHold(Pending));
    TestTrue(TEXT("Only the replacement consumes its own identity"), F.Ledger.TryConsumePress(Second, Primary, Replacement));
    F.Ledger.FinishInputBatch();
    TestFalse(TEXT("Acceptance delayed past input batch cannot revive a candidate"), F.Ledger.ActivateHold(Replacement));
    TestEqual(TEXT("Rejected/cancelled action requests created no hold release events"), F.Ledger.DrainReleasedHolds().Num(), 0);
    TestFalse(TEXT("No candidate became an action hold"), F.Ledger.IsHeld(Primary));

    FDWCombatInputEdge Invalid = First;
    Invalid.Sequence = 0;
    TestFalse(TEXT("Default/forged sequence is rejected"), F.Ledger.TryConsumePress(Invalid, Primary, Replacement));
    TestFalse(TEXT("Invalid key is not a physical edge"), F.Ledger.ObserveKey(F.Context, NAME_None, true, false, Up));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputCancelTest,
    "DoughWorld.CombatInput.CancellationAndAlternateKeys", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputCancelTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInputTests;
    FFixture F;
    FDWCombatInputPress First, Alternate, Unrelated, Fresh;
    TestTrue(TEXT("First accepted key for primary action"), F.Hold(Left, Primary, First));
    TestTrue(TEXT("Another separately accepted key may hold the same action"), F.Hold(Space, Primary, Alternate));
    TestTrue(TEXT("Independent shield action is held"), F.Hold(Right, Secondary, Unrelated));
    FDWCombatInputEdge Up;
    TestTrue(TEXT("First primary key releases"), F.Up(Left, Up));
    const auto Released = F.Ledger.DrainReleasedHolds();
    if (TestEqual(TEXT("One release edge is reported"), Released.Num(), 1))
        TestTrue(TEXT("Release says another accepted key still holds this action"), Released[0].bActionStillHeldAfterRelease);
    TestTrue(TEXT("Other primary key remains held"), F.Ledger.IsHeld(Primary));

    F.Ledger.ForgetHolds(Primary);
    F.Ledger.ForgetHolds(Primary);
    TestFalse(TEXT("Explicit action cancellation removes its remaining hold"), F.Ledger.IsHeld(Primary));
    TestTrue(TEXT("Action cancellation does not touch unrelated hold"), F.Ledger.IsHeld(Secondary));
    TestFalse(TEXT("Cancelled ticket cannot reactivate while its key remains Down"), F.Ledger.ActivateHold(Alternate));
    TestEqual(TEXT("Explicit cancellation creates no synthetic physical release"), F.Ledger.DrainReleasedHolds().Num(), 0);
    TestTrue(TEXT("Physical release after cancellation exits quarantine"), F.Up(Space, Up));
    TestEqual(TEXT("Cancelled hold does not release a second time"), F.Ledger.DrainReleasedHolds().Num(), 0);
    TestTrue(TEXT("A fresh press is allowed after that real release"), F.Hold(Space, Primary, Fresh));
    TestTrue(TEXT("Fresh press has a distinct identity"), Fresh.Edge.Sequence > Alternate.Edge.Sequence);
    return true;
}
#endif
