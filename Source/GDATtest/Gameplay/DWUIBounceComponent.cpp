#include "DWUIBounceComponent.h"
#include "DWUserSettings.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Widget.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Slate/WidgetTransform.h"
#include "Widgets/Layout/SBox.h"

namespace
{
    // Exact damped-spring solution. A long frame does not destabilize an explicit Euler integrator.
    void AdvanceSpring(double& Position, double& Velocity, double Target, double Omega, double Damping, double Dt)
    {
        const double X = Position - Target;
        if (Damping < .9999)
        {
            const double Wd = Omega * FMath::Sqrt(1. - Damping * Damping);
            const double Decay = FMath::Exp(-Damping * Omega * Dt);
            const double Sin = FMath::Sin(Wd * Dt), Cos = FMath::Cos(Wd * Dt);
            Position = Target + Decay * (X * Cos + (Velocity + Damping * Omega * X) * Sin / Wd);
            Velocity = Decay * (Velocity * Cos - (Damping * Omega * Velocity + Omega * Omega * X) * Sin / Wd);
        }
        else if (Damping <= 1.0001)
        {
            const double B = Velocity + Omega * X;
            const double Decay = FMath::Exp(-Omega * Dt);
            Position = Target + Decay * (X + B * Dt);
            Velocity = Decay * (Velocity - Omega * B * Dt);
        }
        else
        {
            const double Root = FMath::Sqrt(Damping * Damping - 1.);
            const double R1 = -Omega * (Damping - Root), R2 = -Omega * (Damping + Root);
            const double C1 = (Velocity - R2 * X) / (R1 - R2), C2 = X - C1;
            const double A = C1 * FMath::Exp(R1 * Dt), B = C2 * FMath::Exp(R2 * Dt);
            Position = Target + A + B;
            Velocity = R1 * A + R2 * B;
        }
    }

    void AdvanceSpring(float& Position, float& Velocity, double Target, double Omega, double Damping, double Dt)
    {
        double P = Position, V = Velocity;
        AdvanceSpring(P, V, Target, Omega, Damping, Dt);
        Position = static_cast<float>(P); Velocity = static_cast<float>(V);
    }
}

TSharedRef<SWidget> UDWUIBounceComponent::RebuildWidgetWithContent(TSharedRef<SWidget> Content)
{
    const TWeakPtr<SWidget> WeakContent(Content);
    // Mirror collapsed/hidden/hit-test visibility, but leave the owner's transforms on the inner widget.
    TSharedRef<SBox> Box = SNew(SBox)
        .Visibility_Lambda([WeakContent]()
        {
            const TSharedPtr<SWidget> Child = WeakContent.Pin();
            return Child ? Child->GetVisibility() : EVisibility::Collapsed;
        })
        .HAlign(HAlign_Fill).VAlign(VAlign_Fill)
        [Content];
    Wrapper = Box;
    Box->SetRenderTransformPivot(Pivot);
    Box->SetOnMouseEnter(FNoReplyPointerEventHandler::CreateWeakLambda(this,
        [this](const FGeometry&, const FPointerEvent&)
        {
            if (bAnimateNonButtonHover && !Cast<UButton>(GetOwner().Get())) SetHovered(true);
        }));
    Box->SetOnMouseLeave(FSimpleNoReplyPointerEventHandler::CreateWeakLambda(this,
        [this](const FPointerEvent&)
        {
            if (bAnimateNonButtonHover && !Cast<UButton>(GetOwner().Get())) SetHovered(false);
        }));
    // No mouse-down/up/click/focus handler is installed; the original content handles all input.
    ApplyWrapperTransform();
    if (bConstructed && CanAnimate()) EnsureTicker();
    return Box;
}

void UDWUIBounceComponent::OnPreConstruct(bool bIsDesignTime)
{
    Super::OnPreConstruct(bIsDesignTime);
    bDesignTime = bIsDesignTime;
    if (bDesignTime) ResetBounce();
}

void UDWUIBounceComponent::OnConstruct()
{
    Super::OnConstruct();
    bDesignTime = !GetOwner().IsValid() || GetOwner()->IsDesignTime();
    bConstructed = !bDesignTime;
    ResetBounce();
    BindButton();
    if (bPlayOnConstruct && CanAnimate()) PlayEntrance();
}

void UDWUIBounceComponent::OnDestruct()
{
    bConstructed = false;
    UnbindButton();
    ResetBounce();
    // Keep only a weak reference: a removed/re-added UserWidget may reuse its existing Slate tree.
    Super::OnDestruct();
}

void UDWUIBounceComponent::BeginDestroy()
{
    bConstructed = false;
    StopTicker();
    UnbindButton();
    if (TSharedPtr<SBox> Box = Wrapper.Pin()) Box->SetRenderTransform(TOptional<FSlateRenderTransform>());
    Wrapper.Reset();
    Super::BeginDestroy();
}

bool UDWUIBounceComponent::CanAnimate() const
{
    const UWidget* OwnerWidget = GetOwner().Get();
    return bConstructed && !bDesignTime && IsValid(OwnerWidget) && !OwnerWidget->IsDesignTime();
}

void UDWUIBounceComponent::BindButton()
{
    UnbindButton();
    UButton* Button = bAutoBindButtons && CanAnimate() ? Cast<UButton>(GetOwner().Get()) : nullptr;
    if (!Button) return;
    BoundButton = Button;
    Button->OnHovered.AddUniqueDynamic(this, &UDWUIBounceComponent::HandleHovered);
    Button->OnUnhovered.AddUniqueDynamic(this, &UDWUIBounceComponent::HandleUnhovered);
    Button->OnPressed.AddUniqueDynamic(this, &UDWUIBounceComponent::HandlePressed);
    Button->OnReleased.AddUniqueDynamic(this, &UDWUIBounceComponent::HandleReleased);
}

void UDWUIBounceComponent::UnbindButton()
{
    if (UButton* Button = BoundButton.Get())
    {
        Button->OnHovered.RemoveDynamic(this, &UDWUIBounceComponent::HandleHovered);
        Button->OnUnhovered.RemoveDynamic(this, &UDWUIBounceComponent::HandleUnhovered);
        Button->OnPressed.RemoveDynamic(this, &UDWUIBounceComponent::HandlePressed);
        Button->OnReleased.RemoveDynamic(this, &UDWUIBounceComponent::HandleReleased);
    }
    BoundButton.Reset();
}

void UDWUIBounceComponent::GetTarget(float& Scale, FVector2D& Offset, float& Angle) const
{
    Scale = FMath::Clamp(bPressedState ? PressedScale : bHoveredState ? HoverScale : 1.f, .05f, 3.f);
    Offset = bPressedState ? PressedOffset : bHoveredState ? HoverOffset : FVector2D::ZeroVector;
    Angle = bPressedState ? PressedAngle : bHoveredState ? HoverAngle : 0.f;
}

void UDWUIBounceComponent::SetHovered(bool bHovered)
{
    if (!CanAnimate() || bHoveredState == bHovered) return;
    bHoveredState = bHovered;
    if (bHovered) PlayCue(HoverSound);
    EnsureTicker();
}

void UDWUIBounceComponent::SetPressed(bool bPressed)
{
    if (!CanAnimate() || bPressedState == bPressed) return;
    bPressedState = bPressed;
    if (bPressed) PlayCue(PressedSound);
    EnsureTicker();
}

void UDWUIBounceComponent::HandleHovered() { SetHovered(true); }
void UDWUIBounceComponent::HandleUnhovered() { SetHovered(false); }
void UDWUIBounceComponent::HandlePressed() { SetPressed(true); }
void UDWUIBounceComponent::HandleReleased() { SetPressed(false); }

void UDWUIBounceComponent::Pulse(float Strength)
{
    if (!CanAnimate()) return;
    const float Amount = FMath::Clamp(Strength, -4.f, 4.f);
    CurrentScale = FMath::Clamp(CurrentScale + PulseScaleOffset * Amount, .05f, 3.f);
    CurrentOffset += PulseOffset * Amount;
    CurrentAngle += PulseAngle * Amount;
    ApplyWrapperTransform();
    EnsureTicker();
}

void UDWUIBounceComponent::PlayEntrance()
{
    if (!CanAnimate()) return;
    CurrentScale = FMath::Clamp(EntryScale, .05f, 3.f);
    CurrentOffset = EntryOffset;
    CurrentAngle = EntryAngle;
    ScaleVelocity = AngleVelocity = 0.f;
    OffsetVelocity = FVector2D::ZeroVector;
    ApplyWrapperTransform();
    EnsureTicker();
}

void UDWUIBounceComponent::ResetBounce()
{
    StopTicker();
    bHoveredState = bPressedState = false;
    CurrentScale = 1.f;
    CurrentAngle = ScaleVelocity = AngleVelocity = 0.f;
    CurrentOffset = OffsetVelocity = FVector2D::ZeroVector;
    ApplyWrapperTransform();
}

void UDWUIBounceComponent::EnsureTicker()
{
    if (!CanAnimate() || TickerHandle.IsValid() || !Wrapper.IsValid()) return;
    LastTickAt = FPlatformTime::Seconds();
    const TWeakObjectPtr<UDWUIBounceComponent> WeakThis(this);
    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float Dt)
    {
        if (UDWUIBounceComponent* Component = WeakThis.Get()) return Component->TickSpring(Dt);
        return false;
    }));
}

void UDWUIBounceComponent::StopTicker()
{
    if (TickerHandle.IsValid() && !bInsideTicker) FTSTicker::RemoveTicker(TickerHandle);
    TickerHandle.Reset();
}

bool UDWUIBounceComponent::TickSpring(float DeltaSeconds)
{
    // CoreTicker runs while gameplay is paused. Wall-clock delta also handles pause/resume and long frames.
    (void)DeltaSeconds;
    bInsideTicker = true;
    if (!CanAnimate() || !Wrapper.IsValid())
    {
        TickerHandle.Reset(); bInsideTicker = false; return false;
    }
    const double Now = FPlatformTime::Seconds();
    const double Dt = FMath::Max(0., Now - LastTickAt);
    LastTickAt = Now;
    const double Omega = 2. * PI * FMath::Clamp(FrequencyHz, .1f, 30.f);
    const double Damping = FMath::Clamp(DampingRatio, .05f, 3.f);
    float TargetScale, TargetAngle; FVector2D TargetOffset;
    GetTarget(TargetScale, TargetOffset, TargetAngle);
    AdvanceSpring(CurrentScale, ScaleVelocity, TargetScale, Omega, Damping, Dt);
    AdvanceSpring(CurrentAngle, AngleVelocity, TargetAngle, Omega, Damping, Dt);
    AdvanceSpring(CurrentOffset.X, OffsetVelocity.X, TargetOffset.X, Omega, Damping, Dt);
    AdvanceSpring(CurrentOffset.Y, OffsetVelocity.Y, TargetOffset.Y, Omega, Damping, Dt);
    const double ScaleEpsilon = FMath::Max(.00001f, SettleScaleTolerance);
    const double PixelEpsilon = FMath::Max(.001f, SettlePixelTolerance);
    const double AngleEpsilon = FMath::Max(.001f, SettleAngleTolerance);
    const bool bAtRest = FMath::Abs(CurrentScale - TargetScale) <= ScaleEpsilon && FMath::Abs(ScaleVelocity) <= ScaleEpsilon * Omega
        && FMath::Abs(CurrentAngle - TargetAngle) <= AngleEpsilon && FMath::Abs(AngleVelocity) <= AngleEpsilon * Omega
        && (CurrentOffset - TargetOffset).SizeSquared() <= PixelEpsilon * PixelEpsilon
        && OffsetVelocity.SizeSquared() <= PixelEpsilon * PixelEpsilon * Omega * Omega;
    if (bAtRest)
    {
        CurrentScale = TargetScale; CurrentAngle = TargetAngle; CurrentOffset = TargetOffset;
        ScaleVelocity = AngleVelocity = 0.f; OffsetVelocity = FVector2D::ZeroVector;
    }
    ApplyWrapperTransform();
    if (bAtRest) TickerHandle.Reset(); // Returning false removes the ticker without removing it during its callback.
    bInsideTicker = false;
    return !bAtRest && TickerHandle.IsValid();
}

void UDWUIBounceComponent::ApplyWrapperTransform()
{
    if (TSharedPtr<SBox> Box = Wrapper.Pin())
    {
        Box->SetRenderTransformPivot(Pivot);
        if (CurrentScale == 1.f && CurrentAngle == 0.f && CurrentOffset.IsZero())
            Box->SetRenderTransform(TOptional<FSlateRenderTransform>());
        else
        {
            const FWidgetTransform Transform(CurrentOffset, FVector2D(FMath::Max(.05f, CurrentScale)), FVector2D::ZeroVector, CurrentAngle);
            Box->SetRenderTransform(TOptional<FSlateRenderTransform>(Transform.ToSlateRenderTransform()));
        }
    }
}

void UDWUIBounceComponent::PlayCue(USoundBase* Sound) const
{
    // Use the owner as WorldContext; a generic UObject UIComponent does not supply its own world.
    if (Sound && GetOwner().IsValid())
        UDWUserSettings::PlayFeedback(GetOwner().Get(), Sound, FMath::Clamp(SoundVolume, 0.f, 2.f), 1.f);
}

UDWUIBounceComponent* UDWUIBounceLibrary::GetBounceComponent(UWidget* Widget)
{
    if (!IsValid(Widget)) return nullptr;
    const UWidgetTree* Tree = Cast<UWidgetTree>(Widget->GetOuter());
    const UUserWidget* UserWidget = Tree ? Cast<UUserWidget>(Tree->GetOuter()) : Widget->GetTypedOuter<UUserWidget>();
    if (!UserWidget) return nullptr;
    UUIComponentUserWidgetExtension* Extension = UserWidget->GetExtension<UUIComponentUserWidgetExtension>();
    if (!Extension || !Extension->IsContainerInitialized()) return nullptr;
    return Cast<UDWUIBounceComponent>(Extension->GetComponent(UDWUIBounceComponent::StaticClass(), Widget->GetFName()));
}

bool UDWUIBounceLibrary::PulseWidget(UWidget* Widget, float Strength)
{
    if (UDWUIBounceComponent* Component = GetBounceComponent(Widget)) { Component->Pulse(Strength); return true; }
    return false;
}

bool UDWUIBounceLibrary::PlayWidgetEntrance(UWidget* Widget)
{
    if (UDWUIBounceComponent* Component = GetBounceComponent(Widget)) { Component->PlayEntrance(); return true; }
    return false;
}

bool UDWUIBounceLibrary::ResetWidgetBounce(UWidget* Widget)
{
    if (UDWUIBounceComponent* Component = GetBounceComponent(Widget)) { Component->ResetBounce(); return true; }
    return false;
}
