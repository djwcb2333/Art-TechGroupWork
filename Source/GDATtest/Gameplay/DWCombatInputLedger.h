#pragma once

#include "CoreMinimal.h"

/** Identity of the input session. Nonzero counters advance in World/Life/Binding hierarchy. */
struct GDATTEST_API FDWCombatInputContext
{
    uint64 WorldEpoch = 0;
    uint64 LifeEpoch = 0;
    uint64 BindingGeneration = 0;

    bool IsValid() const { return WorldEpoch != 0 && LifeEpoch != 0 && BindingGeneration != 0; }
    bool operator==(const FDWCombatInputContext& Other) const
    {
        return WorldEpoch == Other.WorldEpoch && LifeEpoch == Other.LifeEpoch
            && BindingGeneration == Other.BindingGeneration;
    }
    bool operator!=(const FDWCombatInputContext& Other) const { return !(*this == Other); }
};

/** A real physical transition, not an action request. Key is FKey::GetFName() at the adapter. */
struct GDATTEST_API FDWCombatInputEdge
{
    FDWCombatInputContext Context;
    uint64 Sequence = 0;
    FName Key = NAME_None;
    bool bDown = false;
    bool bDuringSuspension = false;
};

/** One consumed gameplay press. Consuming an edge does not yet accept an action or create a hold. */
struct GDATTEST_API FDWCombatInputPress
{
    FDWCombatInputEdge Edge;
    FName Action = NAME_None;
};

/** Release of an accepted hold, identified by its original press as well as its release edge. */
struct GDATTEST_API FDWCombatInputRelease
{
    FDWCombatInputPress Press;
    FDWCombatInputEdge Edge;
    bool bActionStillHeldAfterRelease = false;
};

/**
 * Pure input bookkeeping. No UObject, clock, animation, inventory, or gameplay side effects.
 * One owner uses this on the game thread. UI capture must observe physical transitions before
 * Slate can consume them; regular gameplay bindings consume presses only when permitted.
 * This is not a pause implementation and does not define any combo buffer duration.
 */
class GDATTEST_API FDWCombatInputLedger
{
public:
    /**
     * Invalidates old tickets/holds/releases. A strictly newer hierarchical context is required.
     * Known held keys and the supplied physical snapshot require release before a new press.
     * Sequence remains monotonic across contexts. Suspension is preserved across the reset.
     */
    bool ResetContext(const FDWCombatInputContext& NewContext,
        const TArray<FName>& AdditionalHeldKeys = TArray<FName>());

    /** Duplicate Down/Up, repeats, stale contexts, and invalid keys create no edge. */
    bool ObserveKey(const FDWCombatInputContext& ObservedContext, FName Key,
        bool bDown, bool bRepeat, FDWCombatInputEdge& OutEdge);

    /**
     * Exactly one consumer per physical press. A completed quick tap can still be consumed
     * in this input batch, but cannot activate a hold. Paused/stale presses are rejected.
     */
    bool TryConsumePress(const FDWCombatInputEdge& Edge, FName Action, FDWCombatInputPress& OutPress);

    /** For argument-less binding relays: consumes the earliest unconsumed press of this key in this batch. */
    bool TryConsumeNextPress(FName Key, FName Action, FDWCombatInputPress& OutPress);

    /** Call only after the action gate accepted the request. A rejected request creates no hold. */
    bool ActivateHold(const FDWCombatInputPress& Press);

    /** Retire all press tickets after this batch of gameplay input callbacks, including UI-consumed presses. */
    void FinishInputBatch();

    /** Explicit action cancellation: forget its holds, require release, create no release/action. */
    void ForgetHolds(FName Action);

    /** Idempotent. Existing accepted holds survive; unconsumed presses cannot cross this boundary. */
    void BeginSuspension();

    /** Must run before the first resumed gameplay advance. Never synthesizes a press. */
    TArray<FDWCombatInputRelease> ResumeAndDrainReleases();

    /** Empty while suspended. Notifications are returned once, in physical edge order. */
    TArray<FDWCombatInputRelease> DrainReleasedHolds();

    bool IsHeld(FName Action) const;
    bool IsPhysicallyDown(FName Key) const;
    bool IsSuspended() const { return bSuspended; }
    const FDWCombatInputContext& GetContext() const { return Context; }
    uint64 GetLastSequence() const { return LastSequence; }

private:
    struct FKeyState
    {
        FDWCombatInputEdge LastPress;
        bool bPhysicalDown = false;
        bool bRequireRelease = false;
    };

    struct FPressRecord
    {
        FDWCombatInputEdge Edge;
        FName ConsumedAction = NAME_None;
    };

    FDWCombatInputContext Context;
    TMap<FName, FKeyState> Keys;
    TMap<uint64, FPressRecord> PressesThisBatch;
    TMap<FName, FDWCombatInputPress> HoldsByKey;
    TArray<FDWCombatInputRelease> PendingReleases;
    uint64 LastSequence = 0;
    bool bSuspended = false;

    bool MatchesRecordedPress(const FDWCombatInputEdge& Edge, const FPressRecord& Record) const;
};
