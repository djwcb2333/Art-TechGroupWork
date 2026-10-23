#include "DWCombatInputLedger.h"

bool FDWCombatInputLedger::ResetContext(const FDWCombatInputContext& NewContext,
    const TArray<FName>& AdditionalHeldKeys)
{
    const bool bNewer = !Context.IsValid() || NewContext.WorldEpoch > Context.WorldEpoch
        || (NewContext.WorldEpoch == Context.WorldEpoch && NewContext.LifeEpoch > Context.LifeEpoch)
        || (NewContext.WorldEpoch == Context.WorldEpoch && NewContext.LifeEpoch == Context.LifeEpoch
            && NewContext.BindingGeneration > Context.BindingGeneration);
    if (!NewContext.IsValid() || !bNewer) return false;

    TSet<FName> HeldKeys;
    for (const auto& Entry : Keys)
        if (Entry.Value.bPhysicalDown) HeldKeys.Add(Entry.Key);
    for (FName Key : AdditionalHeldKeys)
        if (!Key.IsNone()) HeldKeys.Add(Key);

    Context = NewContext;
    Keys.Reset();
    PressesThisBatch.Reset();
    HoldsByKey.Reset();
    PendingReleases.Reset();
    for (FName Key : HeldKeys)
    {
        FKeyState& State = Keys.Add(Key);
        State.bPhysicalDown = true;
        State.bRequireRelease = true;
    }
    return true;
}

bool FDWCombatInputLedger::ObserveKey(const FDWCombatInputContext& ObservedContext,
    FName Key, bool bDown, bool bRepeat, FDWCombatInputEdge& OutEdge)
{
    OutEdge = FDWCombatInputEdge();
    if (!Context.IsValid() || ObservedContext != Context || Key.IsNone()
        || (bDown && bRepeat) || LastSequence == MAX_uint64) return false;

    FKeyState* Existing = Keys.Find(Key);
    if ((!Existing && !bDown) || (Existing && Existing->bPhysicalDown == bDown)) return false;
    FKeyState& State = Existing ? *Existing : Keys.Add(Key);

    OutEdge.Context = Context;
    OutEdge.Sequence = ++LastSequence;
    OutEdge.Key = Key;
    OutEdge.bDown = bDown;
    OutEdge.bDuringSuspension = bSuspended;
    State.bPhysicalDown = bDown;
    if (bDown)
    {
        State.LastPress = OutEdge;
        State.bRequireRelease = bSuspended;
        if (!bSuspended)
        {
            FPressRecord& Record = PressesThisBatch.Add(OutEdge.Sequence);
            Record.Edge = OutEdge;
        }
    }
    else
    {
        State.bRequireRelease = false;
        FDWCombatInputPress ReleasedPress;
        if (HoldsByKey.RemoveAndCopyValue(Key, ReleasedPress))
        {
            FDWCombatInputRelease& Released = PendingReleases.AddDefaulted_GetRef();
            Released.Press = ReleasedPress;
            Released.Edge = OutEdge;
            Released.bActionStillHeldAfterRelease = IsHeld(ReleasedPress.Action);
        }
    }
    return true;
}

bool FDWCombatInputLedger::MatchesRecordedPress(const FDWCombatInputEdge& Edge,
    const FPressRecord& Record) const
{
    return Context.IsValid() && Edge.Context == Context && Edge.Sequence != 0
        && Edge.Sequence == Record.Edge.Sequence && Edge.Key == Record.Edge.Key
        && Edge.bDown && Record.Edge.bDown && !Edge.bDuringSuspension
        && !Record.Edge.bDuringSuspension && Record.Edge.Context == Context;
}

bool FDWCombatInputLedger::TryConsumePress(const FDWCombatInputEdge& Edge,
    FName Action, FDWCombatInputPress& OutPress)
{
    OutPress = FDWCombatInputPress();
    if (bSuspended || Action.IsNone()) return false;
    FPressRecord* Record = PressesThisBatch.Find(Edge.Sequence);
    if (!Record || !Record->ConsumedAction.IsNone() || !MatchesRecordedPress(Edge, *Record)) return false;
    Record->ConsumedAction = Action;
    OutPress.Edge = Record->Edge;
    OutPress.Action = Action;
    return true;
}

bool FDWCombatInputLedger::ActivateHold(const FDWCombatInputPress& Press)
{
    if (bSuspended || Press.Action.IsNone() || HoldsByKey.Contains(Press.Edge.Key)) return false;
    const FKeyState* State = Keys.Find(Press.Edge.Key);
    const FPressRecord* Record = PressesThisBatch.Find(Press.Edge.Sequence);
    if (!State || !Record || Record->ConsumedAction != Press.Action
        || !MatchesRecordedPress(Press.Edge, *Record) || !State->bPhysicalDown
        || State->bRequireRelease || State->LastPress.Sequence != Press.Edge.Sequence) return false;
    HoldsByKey.Add(Press.Edge.Key, Press);
    return true;
}

bool FDWCombatInputLedger::TryConsumeNextPress(FName Key, FName Action, FDWCombatInputPress& OutPress)
{
    OutPress = FDWCombatInputPress();
    if (bSuspended || Key.IsNone() || Action.IsNone()) return false;
    const FPressRecord* Next = nullptr;
    for (const auto& Entry : PressesThisBatch)
    {
        const FPressRecord& Record = Entry.Value;
        if (Record.Edge.Key == Key && Record.ConsumedAction.IsNone()
            && (!Next || Record.Edge.Sequence < Next->Edge.Sequence)) Next = &Record;
    }
    if (!Next) return false;
    const FDWCombatInputEdge Edge = Next->Edge;
    return TryConsumePress(Edge, Action, OutPress);
}

void FDWCombatInputLedger::FinishInputBatch()
{
    PressesThisBatch.Reset();
}

void FDWCombatInputLedger::ForgetHolds(FName Action)
{
    if (Action.IsNone()) return;
    for (auto It = HoldsByKey.CreateIterator(); It; ++It)
    {
        if (It.Value().Action != Action) continue;
        if (FKeyState* State = Keys.Find(It.Key()))
        {
            State->bRequireRelease = State->bPhysicalDown;
        }
        It.RemoveCurrent();
    }
}

void FDWCombatInputLedger::BeginSuspension()
{
    if (bSuspended) return;
    bSuspended = true;
    PressesThisBatch.Reset();
    for (auto& Entry : Keys)
    {
        FKeyState& State = Entry.Value;
        if (State.bPhysicalDown && !HoldsByKey.Contains(Entry.Key)) State.bRequireRelease = true;
    }
}

TArray<FDWCombatInputRelease> FDWCombatInputLedger::ResumeAndDrainReleases()
{
    if (!bSuspended) return TArray<FDWCombatInputRelease>();
    bSuspended = false;
    return DrainReleasedHolds();
}

TArray<FDWCombatInputRelease> FDWCombatInputLedger::DrainReleasedHolds()
{
    if (bSuspended) return TArray<FDWCombatInputRelease>();
    TArray<FDWCombatInputRelease> Result = MoveTemp(PendingReleases);
    PendingReleases.Reset();
    return Result;
}

bool FDWCombatInputLedger::IsHeld(FName Action) const
{
    if (Action.IsNone()) return false;
    for (const auto& Entry : HoldsByKey)
        if (Entry.Value.Action == Action) return true;
    return false;
}

bool FDWCombatInputLedger::IsPhysicallyDown(FName Key) const
{
    const FKeyState* State = Keys.Find(Key);
    return State && State->bPhysicalDown;
}
