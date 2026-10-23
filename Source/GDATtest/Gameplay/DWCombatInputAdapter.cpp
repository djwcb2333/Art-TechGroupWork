#include "DWCombatInputAdapter.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/PlayerController.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Slate/SGameLayerManager.h"
#include "Widgets/SViewport.h"

namespace
{
// Game-thread only. A new controller/PIE session never reuses an earlier input-world identity.
uint64 DWNextInputWorldEpoch = 0;
uint64 DWAllocateInputWorldEpoch()
{
    if (DWNextInputWorldEpoch == MAX_uint64) return 0;
    return ++DWNextInputWorldEpoch;
}
}

FDWCombatInputAdapter::FDWCombatInputAdapter(APlayerController* InOwner)
    : Owner(InOwner), OwnerWorld(InOwner ? InOwner->GetWorld() : nullptr),
      OwnerPawn(InOwner ? InOwner->GetPawn() : nullptr)
{
    StartNewWorldContext();
}

TSharedPtr<FDWCombatInputAdapter> FDWCombatInputAdapter::Create(APlayerController* InOwner)
{
    if (!IsValid(InOwner) || !InOwner->IsLocalPlayerController() || !FSlateApplication::IsInitialized()) return nullptr;
    TSharedPtr<FDWCombatInputAdapter> Result = MakeShareable(new FDWCombatInputAdapter(InOwner));
    if (!Result->Ledger.GetContext().IsValid()) return nullptr;
    FSlateApplication& Slate = FSlateApplication::Get();
    Result->bRegistered = Slate.RegisterInputPreProcessor(Result, 0);
    if (!Result->bRegistered) return nullptr;
    Result->ApplicationActivationHandle = Slate.OnApplicationActivationStateChanged().AddSP(
        Result.ToSharedRef(), &FDWCombatInputAdapter::OnApplicationActivationChanged);
    Result->bHadOwnedKeyboardFocus = Result->HasOwnedKeyboardFocus(Slate);
    if (InOwner->IsPaused()) Result->BeginSuspension();
    return Result;
}

void FDWCombatInputAdapter::Shutdown()
{
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication& Slate = FSlateApplication::Get();
        if (ApplicationActivationHandle.IsValid()) Slate.OnApplicationActivationStateChanged().Remove(ApplicationActivationHandle);
        if (bRegistered) Slate.UnregisterInputPreProcessor(AsShared());
    }
    ApplicationActivationHandle.Reset();
    bRegistered = false;
    InvalidateLife();
    Owner.Reset(); OwnerWorld.Reset(); OwnerPawn.Reset();
}

bool FDWCombatInputAdapter::OwnerIsValid() const
{
    const APlayerController* Controller = Owner.Get();
    return IsValid(Controller) && !Controller->IsActorBeingDestroyed() && Controller->IsLocalPlayerController()
        && Controller->GetWorld() && Controller->GetWorld() == OwnerWorld.Get() && !Controller->GetWorld()->bIsTearingDown;
}

bool FDWCombatInputAdapter::IsCapturing() const { return bRegistered && OwnerIsValid(); }

bool FDWCombatInputAdapter::StartNewWorldContext()
{
    const uint64 Epoch = DWAllocateInputWorldEpoch();
    if (Epoch == 0) return false;
    TArray<FName> Held;
    for (FName Key : ObservedDownKeys) Held.Add(Key);
    const bool bReset = Ledger.ResetContext({Epoch, 1, 1}, Held);
    if (bReset) LastObservedReleases.Reset();
    return bReset;
}

void FDWCombatInputAdapter::SynchronizeOwnerLifecycle()
{
    APlayerController* Controller = Owner.Get();
    if (!IsValid(Controller)) return;
    if (OwnerWorld.Get() != Controller->GetWorld())
    {
        OwnerWorld = Controller->GetWorld(); OwnerPawn = Controller->GetPawn();
        if (StartNewWorldContext()) PendingInvalidations |= EDWCombatInputInvalidation::WorldChanged;
    }
    else if (OwnerPawn.Get() != Controller->GetPawn())
    {
        OwnerPawn = Controller->GetPawn();
        InvalidateLife(EDWCombatInputInvalidation::PawnChanged);
    }
    if (OwnerIsValid() && Controller->IsPaused()) Ledger.BeginSuspension();
}

void FDWCombatInputAdapter::InvalidateLife(EDWCombatInputInvalidation Reason)
{
    FDWCombatInputContext Next = Ledger.GetContext();
    if (!Next.IsValid()) return;
    bool bReset = false;
    if (Next.LifeEpoch == MAX_uint64) bReset = StartNewWorldContext();
    else
    {
        ++Next.LifeEpoch;
        Next.BindingGeneration = 1;
        TArray<FName> Held;
        for (FName Key : ObservedDownKeys) Held.Add(Key);
        bReset = Ledger.ResetContext(Next, Held);
    }
    if (bReset) { LastObservedReleases.Reset(); PendingInvalidations |= Reason; }
}

EDWCombatInputInvalidation FDWCombatInputAdapter::DrainInvalidations()
{
    const EDWCombatInputInvalidation Result = PendingInvalidations;
    PendingInvalidations = EDWCombatInputInvalidation::None;
    return Result;
}

void FDWCombatInputAdapter::RefreshMonitoredKeys(const TArray<FKey>& InKeys)
{
    SynchronizeOwnerLifecycle();
    MonitoredKeys.Reset();
    TArray<FName> Held;
    for (FName Key : ObservedDownKeys) Held.AddUnique(Key);
    for (const FKey& Key : InKeys)
    {
        if (!Key.IsValid()) continue;
        MonitoredKeys.Add(Key.GetFName());
        // A positive engine state is safe for quarantine. A false state is NEVER treated as a physical Up.
        if (const APlayerController* Controller = Owner.Get(); Controller && Controller->IsInputKeyDown(Key)) Held.AddUnique(Key.GetFName());
    }
    FDWCombatInputContext Next = Ledger.GetContext();
    if (!Next.IsValid()) return;
    if (Next.BindingGeneration == MAX_uint64) InvalidateLife();
    else
    {
        ++Next.BindingGeneration;
        if (Ledger.ResetContext(Next, Held))
        {
            LastObservedReleases.Reset();
            PendingInvalidations |= EDWCombatInputInvalidation::BindingsChanged;
        }
    }
}

bool FDWCombatInputAdapter::TryConsumePress(FKey Key, FName Action, FDWCombatInputPress& OutPress)
{
    OutPress = FDWCombatInputPress();
    if (!IsCapturing() || !bApplicationActive || !MonitoredKeys.Contains(Key.GetFName())) return false;
    return Ledger.TryConsumeNextPress(Key.GetFName(), Action, OutPress);
}

bool FDWCombatInputAdapter::ActivateHold(const FDWCombatInputPress& Press)
{ return IsCapturing() && bApplicationActive && Ledger.ActivateHold(Press); }
bool FDWCombatInputAdapter::HasObservedReleaseAfter(const FDWCombatInputPress& Press) const
{
    const FDWCombatInputEdge* Released = LastObservedReleases.Find(Press.Edge.Key);
    return Released && Press.Edge.Context == Ledger.GetContext() && Released->Context == Press.Edge.Context
        && !Released->bDown && Released->Sequence > Press.Edge.Sequence;
}
void FDWCombatInputAdapter::ForgetHolds(FName Action) { Ledger.ForgetHolds(Action); }
void FDWCombatInputAdapter::FinishInputBatch() { Ledger.FinishInputBatch(); }
void FDWCombatInputAdapter::BeginSuspension() { Ledger.BeginSuspension(); }
TArray<FDWCombatInputRelease> FDWCombatInputAdapter::ResumeAndDrainReleases() { return Ledger.ResumeAndDrainReleases(); }
TArray<FDWCombatInputRelease> FDWCombatInputAdapter::DrainReleasedHolds() { return Ledger.DrainReleasedHolds(); }

FString FDWCombatInputAdapter::DescribeState() const
{
    const auto& Context = Ledger.GetContext();
    TArray<FString> DownKeys;
    for (const FName Key : ObservedDownKeys) DownKeys.Add(Key.ToString());
    DownKeys.Sort();
    return FString::Printf(TEXT("Capturing=%d Active=%d World=%llu Life=%llu Binding=%llu Sequence=%llu Suspended=%d PrimaryHold=%d SecondaryHold=%d Invalidations=%u ObservedDown=[%s]"),
        IsCapturing(), bApplicationActive, static_cast<unsigned long long>(Context.WorldEpoch),
        static_cast<unsigned long long>(Context.LifeEpoch), static_cast<unsigned long long>(Context.BindingGeneration),
        static_cast<unsigned long long>(Ledger.GetLastSequence()), Ledger.IsSuspended(),
        Ledger.IsHeld(TEXT("Primary")), Ledger.IsHeld(TEXT("Secondary")), static_cast<uint32>(PendingInvalidations),
        *FString::Join(DownKeys, TEXT(",")));
}

bool FDWCombatInputAdapter::IsMatchingUser(uint32 UserIndex) const
{
    const APlayerController* Controller = Owner.Get();
    const ULocalPlayer* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
    const auto User = Local ? Local->GetSlateUser() : nullptr;
    return User.IsValid() && static_cast<uint32>(User->GetUserIndex()) == UserIndex;
}

TSharedPtr<SWidget> FDWCombatInputAdapter::OwnedGameLayer() const
{
    const APlayerController* Controller = Owner.Get();
    const ULocalPlayer* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
    const UGameViewportClient* Viewport = Local ? Local->ViewportClient : nullptr;
    if (!Viewport) return nullptr;
    const auto Layers = Viewport->GetGameLayerManager();
    if (Layers.IsValid()) return Layers->AsWidget();
    return Viewport->GetGameViewportWidget();
}

bool FDWCombatInputAdapter::HasOwnedKeyboardFocus(FSlateApplication& SlateApp) const
{
    (void)SlateApp;
    const APlayerController* Controller = Owner.Get();
    const ULocalPlayer* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
    const auto User = Local ? Local->GetSlateUser() : nullptr;
    const auto Layer = OwnedGameLayer();
    const auto Viewport = Local && Local->ViewportClient ? Local->ViewportClient->GetGameViewportWidget() : nullptr;
    return User.IsValid() && ((Layer.IsValid() && User->IsWidgetInFocusPath(Layer))
        || (Viewport.IsValid() && User->IsWidgetInFocusPath(Viewport)));
}

bool FDWCombatInputAdapter::IsOwnedPointerPath(FSlateApplication& SlateApp, const FPointerEvent& Event) const
{
    const auto Layer = OwnedGameLayer();
    const APlayerController* Controller = Owner.Get();
    const ULocalPlayer* Local = Controller ? Controller->GetLocalPlayer() : nullptr;
    const auto Viewport = Local && Local->ViewportClient ? Local->ViewportClient->GetGameViewportWidget() : nullptr;
    if (!Layer.IsValid() && !Viewport.IsValid()) return false;
    const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(Event.GetScreenSpacePosition(),
        SlateApp.GetInteractiveTopLevelWindows(), false, static_cast<int32>(Event.GetUserIndex()));
    return (Layer.IsValid() && Path.ContainsWidget(Layer.Get()))
        || (Viewport.IsValid() && Path.ContainsWidget(Viewport.Get()));
}

void FDWCombatInputAdapter::Observe(FKey Key, bool bDown, bool bRepeat, uint32 UserIndex, bool bInsideOwnedUI)
{
    SynchronizeOwnerLifecycle();
    if (!Key.IsValid()) return;
    const FName Name = Key.GetFName();
    const FDWCombatPhysicalInputRoute Route{bRegistered && OwnerIsValid() && (bApplicationActive || !bDown),
        IsMatchingUser(UserIndex), bInsideOwnedUI, ObservedDownKeys.Contains(Name) || Ledger.IsPhysicallyDown(Name)};
    if (!Route.Allows(bDown)) return;
    if (bDown && bRepeat) return;
    if (bDown) ObservedDownKeys.Add(Name); else ObservedDownKeys.Remove(Name);
    if (!MonitoredKeys.Contains(Name) && !Ledger.IsPhysicallyDown(Name)) return;
    FDWCombatInputEdge Edge;
    if (Ledger.ObserveKey(Ledger.GetContext(), Name, bDown, bRepeat, Edge) && !bDown)
        LastObservedReleases.Add(Name, Edge);
}

void FDWCombatInputAdapter::Tick(float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
    SynchronizeOwnerLifecycle();
    if (!OwnerIsValid()) return;
    const bool bOwnFocus = HasOwnedKeyboardFocus(SlateApp);
    if (bHadOwnedKeyboardFocus && !bOwnFocus && bApplicationActive)
        InvalidateLife(EDWCombatInputInvalidation::FocusLost);
    bHadOwnedKeyboardFocus = bOwnFocus;
}

void FDWCombatInputAdapter::OnApplicationActivationChanged(bool bActive)
{
    if (bApplicationActive && !bActive) InvalidateLife(EDWCombatInputInvalidation::FocusLost);
    bApplicationActive = bActive;
    bHadOwnedKeyboardFocus = false;
}

bool FDWCombatInputAdapter::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event)
{ Observe(Event.GetKey(), true, Event.IsRepeat(), Event.GetUserIndex(), HasOwnedKeyboardFocus(SlateApp)); return false; }
bool FDWCombatInputAdapter::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& Event)
{ Observe(Event.GetKey(), false, false, Event.GetUserIndex(), HasOwnedKeyboardFocus(SlateApp)); return false; }
bool FDWCombatInputAdapter::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& Event)
{ Observe(Event.GetEffectingButton(), true, false, Event.GetUserIndex(), IsOwnedPointerPath(SlateApp, Event)); return false; }
bool FDWCombatInputAdapter::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& Event)
{ Observe(Event.GetEffectingButton(), false, false, Event.GetUserIndex(), IsOwnedPointerPath(SlateApp, Event)); return false; }
bool FDWCombatInputAdapter::HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& Event)
{ Observe(Event.GetEffectingButton(), true, false, Event.GetUserIndex(), IsOwnedPointerPath(SlateApp, Event)); return false; }
