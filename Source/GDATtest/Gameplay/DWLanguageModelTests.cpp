#if WITH_DEV_AUTOMATION_TESTS
#include "DWLanguageModel.h"
#include "Misc/AutomationTest.h"
#include <type_traits>

static_assert(!std::is_copy_constructible_v<DWLanguage::FPreparedChange>);
static_assert(!std::is_copy_assignable_v<DWLanguage::FPreparedChange>);
namespace DWLanguageModelTests
{
using namespace DWLanguage;
const FName Su(TEXT("su")),Ta(TEXT("ta")),Word(TEXT("tasu")),Pu(TEXT("pu")),Bu(TEXT("bu"));
const FName Relic(TEXT("relic.01")),Door(TEXT("door.01")),Trial(TEXT("trial.start"));
struct FFixture
{
    FCatalog C;FModel M;FString Error;bool bReady=false;
    FFixture()
    {
        FEntryDefinition E;E.AllowedClueIds={TEXT("pond.A"),TEXT("pond.B")};C.Entries.Add(Su,E);
        E={};E.AllowedClueIds={TEXT("sign.ta")};C.Entries.Add(Ta,E);
        E={};E.AllowedClueIds={TEXT("marker.pu")};C.Entries.Add(Pu,E);
        E={};E.AllowedClueIds={TEXT("marker.bu")};C.Entries.Add(Bu,E);
        E={};E.Kind=EEntryKind::Compound;E.Components={Su,Ta,Su};E.AllowedClueIds={TEXT("relic.inscription")};C.Entries.Add(Word,E);
        FRelicDefinition R;R.AnswerIds={TEXT("one"),TEXT("take"),TEXT("water"),TEXT("go"),TEXT("two")};
        R.CandidateAnswerIds={TEXT("one"),TEXT("take"),TEXT("water"),TEXT("go"),TEXT("two"),TEXT("guard")};R.RewardPoints=3;C.Relics.Add(Relic,R);
        FTalentDefinition T;T.Cost=1;C.Talents.Add(TEXT("a1"),T);C.Talents.Add(TEXT("b1"),T);
        T.Cost=2;T.Prerequisites={TEXT("a1")};C.Talents.Add(TEXT("a2"),T);
        T.Cost=1;T.Prerequisites={TEXT("a1"),TEXT("b1")};C.Talents.Add(TEXT("b2"),T);
        T={};T.bEnabled=false;C.Talents.Add(TEXT("a5"),T);C.Talents.Add(TEXT("a6"),T);
        C.KnownItemIds={TEXT("Water"),TEXT("Flour")};FDoorDefinition D;D.RequiredItemId=TEXT("Water");D.RewardPoints=2;C.Doors.Add(Door,D);
        C.PointSources.Add(Trial,5);bReady=M.Initialize(C,FState{},Error);
    }
};
FString Snapshot(const FModel& M)
{FString Json,Error;StateToJson(M.GetCatalog(),M.GetState(),Json,Error);return Json;}
bool Discover(FModel& M,FName Entry,FName Clue)
{FPreparedChange P;FOperationResult R;return M.PrepareDiscover(Entry,Clue,true,P,R)&&M.Commit(MoveTemp(P),R);}
bool Note(FModel& M,FName Entry,const FString& Guess)
{FPreparedChange P;FOperationResult R;return M.PrepareSaveGuess(Entry,Guess,P,R)&&M.Commit(MoveTemp(P),R);}
bool Points(FModel& M)
{FPreparedChange P;FOperationResult R;return M.PrepareGrantPoints(Trial,P,R)&&M.Commit(MoveTemp(P),R);}
bool Learn(FModel& M,FName Id)
{FPreparedChange P;FOperationResult R;return M.PrepareLearnTalent(Id,P,R)&&M.Commit(MoveTemp(P),R);}
bool Forget(FModel& M,FName Id)
{FPreparedChange P;FOperationResult R;return M.PrepareForgetTalent(Id,P,R)&&M.Commit(MoveTemp(P),R);}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageDefinitionsTest,"DoughWorld.Language.Model.DefinitionGraphAndUnknownIds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageDefinitionsTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!TestTrue(TEXT("Valid independent definitions"),F.bReady))return false;
    auto C=F.C;C.Talents.FindChecked(TEXT("a1")).Prerequisites={TEXT("unknown")};TestFalse(TEXT("Unknown prerequisite rejected"),ValidateCatalog(C,F.Error));
    C=F.C;C.Talents.FindChecked(TEXT("a1")).Prerequisites={TEXT("a2")};TestFalse(TEXT("Cycle rejected"),ValidateCatalog(C,F.Error));
    C=F.C;C.MaxTalentDepth=1;TestFalse(TEXT("Depth limit enforced without recursion"),ValidateCatalog(C,F.Error));
    C=F.C;C.Talents.FindChecked(TEXT("a5")).bEnabled=true;TestFalse(TEXT("Undefined a5 cannot be enabled"),ValidateCatalog(C,F.Error));
    C=F.C;C.Relics.FindChecked(Relic).CandidateAnswerIds.Remove(TEXT("water"));TestFalse(TEXT("Puzzle pool cannot omit answer"),ValidateCatalog(C,F.Error));
    C=F.C;C.Entries.FindChecked(Word).Components.Add(TEXT("unknown"));TestFalse(TEXT("Unknown component rejected"),ValidateCatalog(C,F.Error));
    FState Invalid;Invalid.AvailableTalentPoints=1;TestFalse(TEXT("Unfunded arbitrary points rejected"),ValidateState(F.C,Invalid,F.Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageDiscoveryTest,"DoughWorld.Language.Model.ExplicitDiscoveryClueDedupeAndOrderedComponents",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageDiscoveryTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!F.bReady)return false;FPreparedChange P;FOperationResult R;const auto Before=Snapshot(F.M);
    TestFalse(TEXT("Proximity does not discover"),F.M.PrepareDiscover(Word,TEXT("relic.inscription"),false,P,R));
    TestTrue(TEXT("Explicit interaction required"),R.Code==ECode::InteractionRequired);TestEqual(TEXT("No proximity mutation"),Snapshot(F.M),Before);
    if(!TestTrue(TEXT("Explicit compound discovery commits"),Discover(F.M,Word,TEXT("relic.inscription"))))return false;
    const auto& S=F.M.GetState();TestEqual(TEXT("Compound first"),S.Entries.FindChecked(Word).FirstDiscoveryOrder,int64(1));
    TestEqual(TEXT("First constituent follows"),S.Entries.FindChecked(Su).FirstDiscoveryOrder,int64(2));TestEqual(TEXT("Second constituent follows"),S.Entries.FindChecked(Ta).FirstDiscoveryOrder,int64(3));
    TestEqual(TEXT("Repeated position is retained in definition"),F.M.GetCatalog().Entries.FindChecked(Word).Components.Num(),3);
    TestEqual(TEXT("Repeated component discovered once"),S.Entries.Num(),3);TestEqual(TEXT("Repeated source deduped"),S.Entries.FindChecked(Su).AppearedInCompounds.Num(),1);
    TestTrue(TEXT("Composition source is not a scene clue"),S.Entries.FindChecked(Su).ClueIds.IsEmpty());
    for(const auto& Pair:S.Entries)TestTrue(TEXT("No correct answer auto-filled into notes"),Pair.Value.Guess.IsEmpty());
    const auto Revision=S.Revision;TestFalse(TEXT("Same Entry+Clue already applied"),F.M.PrepareDiscover(Word,TEXT("relic.inscription"),true,P,R));
    TestEqual(TEXT("Duplicate retains revision"),F.M.GetState().Revision,Revision);
    TestTrue(TEXT("Real constituent location adds clue"),Discover(F.M,Su,TEXT("pond.A")));TestTrue(TEXT("Second location accumulates"),Discover(F.M,Su,TEXT("pond.B")));
    TestEqual(TEXT("Two unique real clues"),F.M.GetState().Entries.FindChecked(Su).ClueIds.Num(),2);
    TestEqual(TEXT("Adding clues never changes first order"),F.M.GetState().Entries.FindChecked(Su).FirstDiscoveryOrder,int64(2));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageNotesTest,"DoughWorld.Language.Model.PlayerNotesTrimLimitAndIndependentWordGuess",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageNotesTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!F.bReady||!Discover(F.M,Word,TEXT("relic.inscription")))return false;
    TestTrue(TEXT("Chinese/punctuation guess accepted and trimmed"),Note(F.M,Su,TEXT("  水、液体？42！  ")));
    TestEqual(TEXT("Trimmed input preserved verbatim"),F.M.GetState().Entries.FindChecked(Su).Guess,FString(TEXT("水、液体？42！")));
    TestTrue(TEXT("Compound independent guess accepted"),Note(F.M,Word,TEXT("朝那边走2步？")));
    TestTrue(TEXT("Single may hold an intentionally wrong personal guess"),Note(F.M,Su,TEXT("火？")));
    TestEqual(TEXT("Single edit does not rewrite word note"),F.M.GetState().Entries.FindChecked(Word).Guess,FString(TEXT("朝那边走2步？")));
    TestTrue(TEXT("Whitespace clears only selected guess"),Note(F.M,Su,TEXT("  \t ")));
    TestTrue(TEXT("Empty note is unrecorded"),F.M.GetEntryStatus(Su)==EEntryStatus::Unrecorded);
    FPreparedChange P;FOperationResult R;const auto Before=Snapshot(F.M);
    TestFalse(TEXT("Undiscovered entry cannot be edited"),F.M.PrepareSaveGuess(Pu,TEXT("一"),P,R));
    TestFalse(TEXT("41 characters fail without truncation"),F.M.PrepareSaveGuess(Su,FString::ChrN(41,TCHAR('x')),P,R));
    TestTrue(TEXT("Explicit over-limit reason"),R.Code==ECode::GuessTooLong);TestEqual(TEXT("Failed saves keep every fact"),Snapshot(F.M),Before);
    TestTrue(TEXT("40 characters accepted"),Note(F.M,Su,FString::ChrN(40,TCHAR('x'))));
    FString Emoji;for(int32 I=0;I<40;++I)Emoji+=TEXT("😀");
    TestTrue(TEXT("Forty Unicode scalars are not split or counted as eighty UTF16 units"),Note(F.M,Su,Emoji));
    TestFalse(TEXT("Forty-one Unicode scalars rejected without splitting"),F.M.PrepareSaveGuess(Su,Emoji+TEXT("😀"),P,R));
    TestTrue(TEXT("Nonempty guess is recorded, not certified correct"),F.M.GetEntryStatus(Su)==EEntryStatus::Recorded);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageSortTest,"DoughWorld.Language.Model.SortUsesThreeStatesAndStableDiscoveryOrder",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageSortTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!F.bReady||!Discover(F.M,Word,TEXT("relic.inscription"))||!Note(F.M,Su,TEXT("猜测")))return false;
    auto Order=F.M.BuildEntryOrder(EEntryFilter::All,EEntrySort::UnrecordedFirst);
    TestTrue(TEXT("Unrecorded first, then stable unknowns, then recorded"),Order==TArray<FName>{Word,Ta,Bu,Pu,Su});
    TestTrue(TEXT("Newest first excludes no entries and keeps unknown last"),F.M.BuildEntryOrder(EEntryFilter::All,EEntrySort::NewestFirst)==TArray<FName>{Ta,Su,Word,Bu,Pu});
    TestTrue(TEXT("Compound filter"),F.M.BuildEntryOrder(EEntryFilter::Compounds,EEntrySort::DiscoveryOrder)==TArray<FName>{Word});
    TestTrue(TEXT("Single filter and first discovery order"),F.M.BuildEntryOrder(EEntryFilter::Singles,EEntrySort::DiscoveryOrder)==TArray<FName>{Su,Ta,Bu,Pu});return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageRelicTest,"DoughWorld.Language.Model.StableRelicAwardsOnceWithoutReplacingExistingPoints",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageRelicTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!F.bReady||!Points(F.M))return false;FPreparedChange P,Q;FOperationResult R;const auto Before=Snapshot(F.M);
    TestFalse(TEXT("Incomplete is not wrong and changes nothing"),F.M.PrepareSolveRelic(Relic,{TEXT("one")},P,R));TestTrue(TEXT("Incomplete reason"),R.Code==ECode::IncompleteSolution);
    auto Wrong=F.C.Relics.FindChecked(Relic).AnswerIds;Wrong[0]=TEXT("guard");TestFalse(TEXT("Wrong order/answer rejected"),F.M.PrepareSolveRelic(Relic,Wrong,P,R));
    TestTrue(TEXT("Incorrect reason"),R.Code==ECode::IncorrectSolution);TestEqual(TEXT("No penalty or note change"),Snapshot(F.M),Before);
    const auto Answers=F.C.Relics.FindChecked(Relic).AnswerIds;
    TestTrue(TEXT("Two simultaneous candidates prepare"),F.M.PrepareSolveRelic(Relic,Answers,P,R)&&F.M.PrepareSolveRelic(Relic,Answers,Q,R));
    TestTrue(TEXT("Preparation keeps live points"),F.M.GetState().AvailableTalentPoints==5&&F.M.GetState().CompletedRelics.IsEmpty());
    TestTrue(TEXT("First commits completion and actual three points"),F.M.Commit(MoveTemp(P),R));TestEqual(TEXT("Existing points plus actual reward"),F.M.GetState().AvailableTalentPoints,8);
    TestEqual(TEXT("Historical receipt contains real amount"),F.M.GetState().CompletedRelics.FindChecked(Relic).AwardedPoints,3);
    TestFalse(TEXT("Double-click stale plan cannot award again"),F.M.Commit(MoveTemp(Q),R));
    TestFalse(TEXT("Completed relic remains read-only"),F.M.PrepareSolveRelic(Relic,Answers,P,R));TestTrue(TEXT("Already complete reason"),R.Code==ECode::AlreadyCompleted);
    TestEqual(TEXT("No second reward"),F.M.GetState().AvailableTalentPoints,8);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageTalentTest,"DoughWorld.Language.Model.AllPrerequisitesLeafRefundAndActualPaidCost",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageTalentTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!F.bReady||!Points(F.M))return false;FPreparedChange P;FOperationResult R;
    TestFalse(TEXT("Missing both prerequisites"),F.M.PrepareLearnTalent(TEXT("b2"),P,R));TestTrue(TEXT("First root learned"),Learn(F.M,TEXT("a1")));
    TestFalse(TEXT("One of two prerequisites is insufficient"),F.M.PrepareLearnTalent(TEXT("b2"),P,R));
    TestTrue(TEXT("Other root learned"),Learn(F.M,TEXT("b1")));TestTrue(TEXT("All prerequisites allow child"),Learn(F.M,TEXT("b2")));
    const auto Before=Snapshot(F.M);TestFalse(TEXT("Cannot forget non-leaf"),F.M.PrepareForgetTalent(TEXT("a1"),P,R));
    TestTrue(TEXT("Dependent reason"),R.Code==ECode::DependentLearned);TestEqual(TEXT("Rejected forget changes nothing"),Snapshot(F.M),Before);
    TestFalse(TEXT("No relearn payment"),F.M.PrepareLearnTalent(TEXT("b2"),P,R));TestFalse(TEXT("a5 unavailable"),F.M.PrepareLearnTalent(TEXT("a5"),P,R));
    TestTrue(TEXT("Leaf forget refunds actual one point"),Forget(F.M,TEXT("b2")));TestEqual(TEXT("Refunded current points"),F.M.GetState().AvailableTalentPoints,3);
    auto Updated=F.C;Updated.Talents.FindChecked(TEXT("a1")).Cost=7;
    const auto Saved=F.M.GetState();TestTrue(TEXT("Validated updated catalog retains historical paid cost"),F.M.Initialize(Updated,Saved,F.Error));
    TestTrue(TEXT("Leaf root refunded"),Forget(F.M,TEXT("a1")));TestEqual(TEXT("Refund is original one, not new seven"),F.M.GetState().AvailableTalentPoints,4);
    TestFalse(TEXT("Refund cannot repeat"),F.M.PrepareForgetTalent(TEXT("a1"),P,R));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageTokenTest,"DoughWorld.Language.Model.PreparedIdentityRevisionMoveAndRestoreEpoch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageTokenTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F,Other;if(!F.bReady||!Other.bReady)return false;FPreparedChange P,Q;FOperationResult R;
    TestTrue(TEXT("Prepared for original owner"),F.M.PrepareDiscover(Su,TEXT("pond.A"),true,P,R));
    TestFalse(TEXT("Foreign model cannot commit"),Other.M.Commit(MoveTemp(P),R));TestTrue(TEXT("Rejected token consumed"),P.IsConsumed());
    TestTrue(TEXT("Parallel preparations"),F.M.PrepareDiscover(Su,TEXT("pond.A"),true,P,R)&&F.M.PrepareDiscover(Ta,TEXT("sign.ta"),true,Q,R));
    FPreparedChange Moved=MoveTemp(P);TestTrue(TEXT("Moved-from consumed"),P.IsConsumed());TestTrue(TEXT("Move preserves candidate"),F.M.Commit(MoveTemp(Moved),R));
    const auto After=Snapshot(F.M);TestFalse(TEXT("Revision stale plan rejected"),F.M.Commit(MoveTemp(Q),R));TestEqual(TEXT("Stale plan leaves facts unchanged"),Snapshot(F.M),After);
    TestTrue(TEXT("Prepare note before restore"),F.M.PrepareSaveGuess(Su,TEXT("pending"),P,R));const auto Saved=F.M.GetState();
    TestTrue(TEXT("Validated restore works"),F.M.RestoreValidated(Saved,F.Error));TestFalse(TEXT("Old epoch plan invalid"),F.M.CanCommit(P));
    TestTrue(TEXT("Prepare fresh note"),F.M.PrepareSaveGuess(Su,TEXT("fresh"),Q,R));
    TestTrue(TEXT("Reinitialize same state changes owner epoch"),F.M.Initialize(F.C,F.M.GetState(),F.Error));TestFalse(TEXT("Same revision cannot resurrect old owner token"),F.M.CanCommit(Q));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageDoorTest,"DoughWorld.Language.Model.DoorWrongPaymentCumulativeProgressAndUniqueCompletion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageDoorTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!F.bReady)return false;FPreparedChange P;FOperationResult R;
    TestTrue(TEXT("Wrong known item prepares a real inventory payment"),F.M.PrepareSubmitDoor(Door,TEXT("request.wrong"),TEXT("Flour"),1,P,R));
    TestTrue(TEXT("Wrong payment explicit and no accepted increment"),R.Code==ECode::WrongDoorItem&&R.bConsumeInventory&&R.InventoryQuantity==1&&R.AcceptedCountDelta==0);
    TestTrue(TEXT("Wrong payment receipt commits"),F.M.Commit(MoveTemp(P),R));TestEqual(TEXT("Wrong does not advance mechanism"),F.M.GetState().DoorProgress.FindChecked(Door).AcceptedCount,0);
    TestFalse(TEXT("Same receipt never charges twice"),F.M.PrepareSubmitDoor(Door,TEXT("request.wrong"),TEXT("Flour"),1,P,R));TestTrue(TEXT("Idempotent receipt reason"),R.Code==ECode::AlreadyApplied);
    TestFalse(TEXT("Receipt cannot be reused with different input"),F.M.PrepareSubmitDoor(Door,TEXT("request.wrong"),TEXT("Water"),1,P,R));
    TestFalse(TEXT("Unknown inventory ID rejected"),F.M.PrepareSubmitDoor(Door,TEXT("unknown"),TEXT("Unknown"),1,P,R));
    TestTrue(TEXT("First correct contribution"),F.M.PrepareSubmitDoor(Door,TEXT("request.1"),TEXT("Water"),1,P,R)&&F.M.Commit(MoveTemp(P),R));
    TestEqual(TEXT("Progress accumulated"),F.M.GetState().DoorProgress.FindChecked(Door).AcceptedCount,1);
    FString Json;FState Loaded;TestTrue(TEXT("Partial progress serializes"),StateToJson(F.C,F.M.GetState(),Json,F.Error)&&StateFromJson(F.C,Json,Loaded,F.Error)&&F.M.RestoreValidated(Loaded,F.Error));
    TestTrue(TEXT("Second correct contribution completes"),F.M.PrepareSubmitDoor(Door,TEXT("request.2"),TEXT("Water"),1,P,R)&&F.M.Commit(MoveTemp(P),R));
    TestTrue(TEXT("Completion and actual award are one fact"),R.bCompletedNow&&R.PointDelta==2&&F.M.GetState().DoorProgress.FindChecked(Door).bCompleted&&F.M.GetState().AvailableTalentPoints==2);
    TestFalse(TEXT("Completed door rejects even wrong further payment"),F.M.PrepareSubmitDoor(Door,TEXT("request.after"),TEXT("Flour"),1,P,R));
    TestTrue(TEXT("No post-completion swallowing"),R.Code==ECode::AlreadyCompleted&&!R.bConsumeInventory);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageJsonTest,"DoughWorld.Language.Model.ValidatedJsonRoundTripAndFailurePreservesDestination",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageJsonTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!F.bReady||!Discover(F.M,Word,TEXT("relic.inscription"))||!Note(F.M,Su,TEXT("个人中文猜测2！"))||!Points(F.M)||!Learn(F.M,TEXT("a1")))return false;
    FPreparedChange P;FOperationResult R;
    TestTrue(TEXT("Complete relic before persistence"),F.M.PrepareSolveRelic(Relic,F.C.Relics.FindChecked(Relic).AnswerIds,P,R)&&F.M.Commit(MoveTemp(P),R));
    FState Large=F.M.GetState();Large.Revision=(int64(1)<<53)+37;TestTrue(TEXT("Exact int64 revision accepted"),F.M.Initialize(F.C,Large,F.Error));
    FString Json;FState Loaded;TestTrue(TEXT("Serialize and validate deserialize"),StateToJson(F.C,F.M.GetState(),Json,F.Error)&&StateFromJson(F.C,Json,Loaded,F.Error));
    TestEqual(TEXT("Revision above double exact range preserved"),Loaded.Revision,Large.Revision);
    FString RoundTrip;TestTrue(TEXT("Round-trip state valid"),StateToJson(F.C,Loaded,RoundTrip,F.Error));TestEqual(TEXT("Deterministic complete state encoding"),RoundTrip,Json);
    FState Destination=Loaded;Destination.Revision=77;
    TestFalse(TEXT("Malformed JSON fails"),StateFromJson(F.C,TEXT("{broken"),Destination,F.Error));TestEqual(TEXT("Failed parse leaves destination intact"),Destination.Revision,int64(77));
    FString Unknown=Json.Replace(TEXT("\"su\""),TEXT("\"unknown.entry\""));
    TestFalse(TEXT("Unknown persisted entry fails"),StateFromJson(F.C,Unknown,Destination,F.Error));TestEqual(TEXT("Failed validation keeps destination"),Destination.Revision,int64(77));
    auto Invalid=Loaded;Invalid.AvailableTalentPoints+=1;TestFalse(TEXT("Unfunded points reject restore"),F.M.RestoreValidated(Invalid,F.Error));
    TestEqual(TEXT("Rejected restore keeps original revision"),F.M.GetState().Revision,Large.Revision);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageBatchDiscoveryTest,"DoughWorld.Language.Model.BatchDiscoveryRejectsAnyInvalidClueWithoutPartialDiscovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageBatchDiscoveryTest::RunTest(const FString&)
{
    using namespace DWLanguageModelTests;FFixture F;if(!F.bReady)return false;FPreparedChange P;FOperationResult R;
    TArray<TPair<FName,FName>> Pairs={{Su,TEXT("pond.A")},{Ta,TEXT("forged")}};
    TestFalse(TEXT("One forged source rejects the whole interaction"),F.M.PrepareDiscoverBatch(Pairs,true,P,R));
    TestTrue(TEXT("No early entry leaked"),F.M.GetState().Entries.IsEmpty());
    Pairs[1].Value=TEXT("sign.ta");TestTrue(TEXT("All real sources prepare"),F.M.PrepareDiscoverBatch(Pairs,true,P,R));
    TestTrue(TEXT("One atomic adoption"),F.M.Commit(MoveTemp(P),R));TestEqual(TEXT("Single revision for whole carrier"),F.M.GetState().Revision,int64(1));
    TestEqual(TEXT("Both entries are discovered"),F.M.GetState().Entries.Num(),2);
    TestFalse(TEXT("Repeated carrier is idempotent"),F.M.PrepareDiscoverBatch(Pairs,true,P,R));return true;
}
#endif
