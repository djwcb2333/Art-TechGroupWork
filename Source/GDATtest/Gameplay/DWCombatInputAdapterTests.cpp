#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatInputAdapter.h"
#include "DWCombatInputOrigin.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputOwnedRouteTest,
    "DoughWorld.CombatInputAdapter.OwnedWindowAndReleaseRoute", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputOwnedRouteTest::RunTest(const FString& Parameters)
{
    FDWCombatPhysicalInputRoute Route{true, true, true, false};
    TestTrue(TEXT("Own viewport/UI Down is observable"), Route.Allows(true));
    TestTrue(TEXT("Own viewport/UI Up is observable before UI consumes it"), Route.Allows(false));
    Route.bInsideOwnedGameUI = false;
    TestFalse(TEXT("Click in another editor/PIE window cannot start an action"), Route.Allows(true));
    TestFalse(TEXT("Untracked Up outside owned UI cannot affect this player"), Route.Allows(false));
    Route.bAlreadyTrackedDown = true;
    TestTrue(TEXT("A tracked press may finish outside its viewport"), Route.Allows(false));
    TestFalse(TEXT("Tracked key does not license a new outside Down"), Route.Allows(true));
    Route.bMatchingSlateUser = false;
    TestFalse(TEXT("Another user's Up cannot release this user's hold"), Route.Allows(false));
    Route.bInsideOwnedGameUI = true;
    TestFalse(TEXT("Another user's Down is rejected even over this viewport"), Route.Allows(true));
    Route.bMatchingSlateUser = true;
    Route.bOwnerValid = false;
    TestFalse(TEXT("Destroyed/old-world observer cannot accept Down"), Route.Allows(true));
    TestFalse(TEXT("Destroyed/old-world observer cannot accept Up"), Route.Allows(false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputSyntheticReleaseTest,
    "DoughWorld.CombatInputAdapter.TrustedReleaseVersusFocusInvalidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputSyntheticReleaseTest::RunTest(const FString& Parameters)
{
    FDWCombatInputLedger Ledger;
    const FDWCombatInputContext Context{11, 1, 1};
    const FName Key(TEXT("LeftMouseButton")), Action(TEXT("Primary"));
    FDWCombatInputEdge Down, Up;
    FDWCombatInputPress Press;
    TestTrue(TEXT("Create input context"), Ledger.ResetContext(Context));
    TestTrue(TEXT("Trusted observed Down"), Ledger.ObserveKey(Context, Key, true, false, Down));
    TestTrue(TEXT("Gameplay consumes the observed Down"), Ledger.TryConsumePress(Down, Action, Press));
    TestTrue(TEXT("Action gate accepts the hold"), Ledger.ActivateHold(Press));
    Ledger.FinishInputBatch();
    // A gameplay Release relay/FlushPressedKeys is deliberately NOT passed to ObserveKey.
    TestEqual(TEXT("Synthetic relay alone creates no physical release notification"), Ledger.DrainReleasedHolds().Num(), 0);
    TestTrue(TEXT("Synthetic relay alone cannot falsify physical held state"), Ledger.IsPhysicallyDown(Key));
    Ledger.BeginSuspension();
    TestTrue(TEXT("Real UI-observed Up is recorded during pause"), Ledger.ObserveKey(Context, Key, false, false, Up));
    TestEqual(TEXT("Paused notification cannot yet mutate gameplay"), Ledger.DrainReleasedHolds().Num(), 0);
    const auto Released = Ledger.ResumeAndDrainReleases();
    if (TestEqual(TEXT("Resume returns one real release"), Released.Num(), 1))
        TestEqual(TEXT("Resume carries actual observed Up identity"), Released[0].Edge.Sequence, Up.Sequence);

    TestTrue(TEXT("Another real Down"), Ledger.ObserveKey(Context, Key, true, false, Down));
    TestTrue(TEXT("Consume new press"), Ledger.TryConsumePress(Down, Action, Press));
    TestTrue(TEXT("Activate new accepted hold"), Ledger.ActivateHold(Press));
    const uint64 BeforeInvalidation = Ledger.GetLastSequence();
    TestTrue(TEXT("Focus loss explicitly invalidates context"), Ledger.ResetContext({11, 2, 1}));
    TestEqual(TEXT("Focus invalidation manufactures no physical edge"), Ledger.GetLastSequence(), BeforeInvalidation);
    TestEqual(TEXT("Focus invalidation manufactures no physical Up notification"), Ledger.DrainReleasedHolds().Num(), 0);
    TestTrue(TEXT("Unknown held key stays quarantined until trusted release"), Ledger.IsPhysicallyDown(Key));
    TestFalse(TEXT("Old held ticket cannot survive explicit invalidation"), Ledger.ActivateHold(Press));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatInputOriginTest,
    "DoughWorld.CombatInputAdapter.ReleaseCannotCrossActionOrigin", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWCombatInputOriginTest::RunTest(const FString& Parameters)
{
    FDWCombatInputPress Original;
    Original.Edge.Context = {31, 8, 4}; Original.Edge.Sequence = 99;
    Original.Edge.Key = TEXT("LeftMouseButton"); Original.Edge.bDown = true; Original.Action = TEXT("Primary");
    TestTrue(TEXT("The same physical hold survives internal role/round transitions"), DWCombatInputOriginsMatch(Original, Original));
    auto Changed = Original; ++Changed.Edge.Sequence;
    TestFalse(TEXT("New press cannot receive an older release"), DWCombatInputOriginsMatch(Original, Changed));
    Changed = Original; ++Changed.Edge.Context.WorldEpoch;
    TestFalse(TEXT("Next PIE/map cannot inherit a release"), DWCombatInputOriginsMatch(Original, Changed));
    Changed = Original; ++Changed.Edge.Context.LifeEpoch;
    TestFalse(TEXT("Death/restore invalidates old release identity"), DWCombatInputOriginsMatch(Original, Changed));
    Changed = Original; ++Changed.Edge.Context.BindingGeneration;
    TestFalse(TEXT("Rebinding invalidates old release identity"), DWCombatInputOriginsMatch(Original, Changed));
    Changed = Original; Changed.Action = TEXT("Secondary");
    TestFalse(TEXT("A primary release cannot end a secondary action"), DWCombatInputOriginsMatch(Original, Changed));
    Changed = Original; Changed.Edge.Key = TEXT("RightMouseButton");
    TestFalse(TEXT("Another physical key cannot supply the release"), DWCombatInputOriginsMatch(Original, Changed));
    Changed = Original; Changed.Edge.bDuringSuspension = true;
    TestFalse(TEXT("Paused press cannot be installed as a gameplay origin"), DWCombatInputOriginsMatch(Original, Changed));
    Changed = Original; Changed.Edge.bDown = false;
    TestFalse(TEXT("An Up edge cannot masquerade as the original press"), DWCombatInputOriginsMatch(Original, Changed));
    TestFalse(TEXT("Unbound BP/new action cannot inherit an old origin"), DWCombatInputOriginsMatch(Original, FDWCombatInputPress()));
    TestFalse(TEXT("Two empty origins never authorize release"), DWCombatInputOriginsMatch(FDWCombatInputPress(), FDWCombatInputPress()));
    return true;
}
#endif
