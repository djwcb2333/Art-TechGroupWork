#pragma once

#include "CoreMinimal.h"
#include "DWCombatInputLedger.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"

class APlayerController;
class APawn;
class UWorld;
class SWidget;

enum class EDWCombatInputInvalidation : uint8
{
    None = 0,
    FocusLost = 1,
    PawnChanged = 2,
    ExplicitLifeChange = 4,
    WorldChanged = 8,
    BindingsChanged = 16
};
ENUM_CLASS_FLAGS(EDWCombatInputInvalidation);

/** Testable routing policy. Outside-domain releases are accepted only for an already tracked key. */
struct GDATTEST_API FDWCombatPhysicalInputRoute
{
    bool bOwnerValid = false;
    bool bMatchingSlateUser = false;
    bool bInsideOwnedGameUI = false;
    bool bAlreadyTrackedDown = false;
    bool Allows(bool bDown) const
    {
        return bOwnerValid && bMatchingSlateUser
            && (bInsideOwnedGameUI || (!bDown && bAlreadyTrackedDown));
    }
};

/**
 * Passive Slate observer and lifecycle adapter; never calls gameplay from Slate callbacks.
 * The owning controller must call Shutdown() in EndPlay, finish each input batch, and
 * process release notifications before the first gameplay advance after unpausing.
 */
class GDATTEST_API FDWCombatInputAdapter final
    : public IInputProcessor, public TSharedFromThis<FDWCombatInputAdapter>
{
public:
    static TSharedPtr<FDWCombatInputAdapter> Create(APlayerController* InOwner);
    void Shutdown();
    bool IsCapturing() const;

    /** Called after committed binding changes, never while iterating InputComponent bindings. */
    void RefreshMonitoredKeys(const TArray<FKey>& InKeys);
    /** Detects pawn/world replacement. Explicit same-pawn death/restore must call InvalidateLife. */
    void SynchronizeOwnerLifecycle();
    void InvalidateLife(EDWCombatInputInvalidation Reason = EDWCombatInputInvalidation::ExplicitLifeChange);
    EDWCombatInputInvalidation DrainInvalidations();

    bool TryConsumePress(FKey Key, FName Action, FDWCombatInputPress& OutPress);
    bool ActivateHold(const FDWCombatInputPress& Press);
    bool HasObservedReleaseAfter(const FDWCombatInputPress& Press) const;
    void ForgetHolds(FName Action);
    void FinishInputBatch();
    void BeginSuspension();
    bool IsSuspended() const { return Ledger.IsSuspended(); }
    TArray<FDWCombatInputRelease> ResumeAndDrainReleases();
    TArray<FDWCombatInputRelease> DrainReleasedHolds();
    const FDWCombatInputContext& GetContext() const { return Ledger.GetContext(); }
    FString DescribeState() const;

    virtual void Tick(float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
    virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override;
    virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override;
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override;
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override;
    virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override;
    virtual const TCHAR* GetDebugName() const override { return TEXT("DoughWorld physical combat input observer"); }

private:
    explicit FDWCombatInputAdapter(APlayerController* InOwner);
    TWeakObjectPtr<APlayerController> Owner;
    TWeakObjectPtr<UWorld> OwnerWorld;
    TWeakObjectPtr<APawn> OwnerPawn;
    FDWCombatInputLedger Ledger;
    TSet<FName> MonitoredKeys;
    TSet<FName> ObservedDownKeys;
    TMap<FName, FDWCombatInputEdge> LastObservedReleases;
    FDelegateHandle ApplicationActivationHandle;
    EDWCombatInputInvalidation PendingInvalidations = EDWCombatInputInvalidation::None;
    bool bRegistered = false;
    bool bApplicationActive = true;
    bool bHadOwnedKeyboardFocus = false;

    bool OwnerIsValid() const;
    bool IsMatchingUser(uint32 UserIndex) const;
    TSharedPtr<SWidget> OwnedGameLayer() const;
    bool HasOwnedKeyboardFocus(FSlateApplication& SlateApp) const;
    bool IsOwnedPointerPath(FSlateApplication& SlateApp, const FPointerEvent& Event) const;
    void Observe(FKey Key, bool bDown, bool bRepeat, uint32 UserIndex, bool bInsideOwnedUI);
    void OnApplicationActivationChanged(bool bActive);
    bool StartNewWorldContext();
};
