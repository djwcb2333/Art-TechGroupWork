#include "DWFrontendHUD.h"

#include "DWFrontendWidget.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"

ADWFrontendHUD::ADWFrontendHUD()
{
    bShowTitleOnStart = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    PrimaryActorTick.TickInterval = 0.f;
}

void ADWFrontendHUD::BeginPlay()
{
    bFrontendEnding = false;
    BeginFrontendAudioSequence(); // Block the parent's ShowTitle/UpdateMusic before constructing the intro.
    // A previously authored HUD Blueprint may still serialize the old parent's .15 s interval.
    SetActorTickInterval(0.f);
    SetTickableWhenPaused(true);
    if (!WidgetClass)
        WidgetClass = FrontendWidgetClassPath.TryLoadClass<UDWFrontendWidget>();
    if (!WidgetClass)
    {
        UE_LOG(LogTemp, Error, TEXT("DoughWorld frontend: missing WBP_DWFrontend at %s. Run frontend authoring before opening this map."), *FrontendWidgetClassPath.ToString());
        // Do not silently display the old gameplay UI when the new asset has not been authored.
        WidgetClass = UDWFrontendWidget::StaticClass();
    }
    RefreshMenuCamera();
    Super::BeginPlay(); // Existing title/settings/three-slot workflow; all pawn accesses are null-safe.
    RefreshMenuCamera();
}

void ADWFrontendHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    bFrontendEnding = true;
    CancelFrontendAudioSequence();
    // Only runtime actors from this world are restored; an editor-world camera is never touched.
    RestoreMenuCameraBaseline();
    CameraImpulses.Reset();
    BaselineCamera.Reset();
    bHasCameraBaseline = false;
    Super::EndPlay(Reason);
}

void ADWFrontendHUD::BeginFrontendAudioSequence()
{
    bMenuMusicAllowed = false;
    bMenuMusicPending = false;
    MenuMusicStartsAt = 0.;
    StopMusic();
}

void ADWFrontendHUD::CompleteFrontendAudioSequence(float DelaySeconds)
{
    if (bFrontendEnding || IsActorBeingDestroyed() || bMenuMusicAllowed || bMenuMusicPending) return;
    // This deadline uses wall time, independent of game pause and world timers.
    const float Delay = FMath::IsFinite(DelaySeconds) ? FMath::Max(0.f, DelaySeconds) : 0.f;
    MenuMusicStartsAt = FPlatformTime::Seconds() + Delay;
    bMenuMusicPending = true;
    UpdateFrontendAudio(FPlatformTime::Seconds());
}

void ADWFrontendHUD::CancelFrontendAudioSequence()
{
    BeginFrontendAudioSequence();
}

float ADWFrontendHUD::GetMenuMusicDelayRemaining() const
{
    return bMenuMusicPending ? static_cast<float>(FMath::Max(0., MenuMusicStartsAt - FPlatformTime::Seconds())) : 0.f;
}

void ADWFrontendHUD::UpdateFrontendAudio(double Now)
{
    if (!bMenuMusicPending || bFrontendEnding || IsActorBeingDestroyed() || Now < MenuMusicStartsAt) return;
    bMenuMusicPending = false;
    bMenuMusicAllowed = true;
    UpdateMusic();
}

bool ADWFrontendHUD::IsRuntimeCamera(const ACameraActor* Camera) const
{
    return IsValid(Camera) && GetWorld() && GetWorld()->IsGameWorld() && Camera->GetWorld() == GetWorld();
}

void ADWFrontendHUD::CaptureMenuCameraBaseline(ACameraActor* Camera)
{
    if (!IsRuntimeCamera(Camera)) return;
    BaselineCamera = Camera;
    CameraBaseline = Camera->GetActorTransform();
    LastAppliedCameraTransform = CameraBaseline;
    bHasCameraBaseline = true;
    bHasAppliedCameraMotion = false;
    CameraMotionStartedAt = FPlatformTime::Seconds();
    CurrentLocalOffset = FVector::ZeroVector;
    CurrentLocalRotation = FRotator::ZeroRotator;
    CameraImpulses.Reset();
}

void ADWFrontendHUD::RestoreMenuCameraBaseline()
{
    ACameraActor* Camera = BaselineCamera.Get();
    if (bHasCameraBaseline && bHasAppliedCameraMotion && IsRuntimeCamera(Camera))
    {
        // Do not undo a transform another runtime system has since written to the camera.
        if (Camera->GetActorTransform().Equals(LastAppliedCameraTransform, .001f))
            Camera->SetActorTransform(CameraBaseline, false, nullptr, ETeleportType::TeleportPhysics);
    }
    bHasAppliedCameraMotion = false;
    CurrentLocalOffset = FVector::ZeroVector;
    CurrentLocalRotation = FRotator::ZeroRotator;
}

bool ADWFrontendHUD::RefreshMenuCamera()
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC || !GetWorld() || !GetWorld()->IsGameWorld()) return false;
    const bool bSelectionChanged = !bHaveSelectionInputs || MenuCameraTag != LastRequestedCameraTag || MenuCameraOverride.Get() != LastRequestedOverride.Get();
    LastRequestedCameraTag = MenuCameraTag;
    LastRequestedOverride = MenuCameraOverride.Get();
    bHaveSelectionInputs = true;
    ACameraActor* Found = IsRuntimeCamera(MenuCameraOverride) ? MenuCameraOverride.Get() : nullptr;
    ACameraActor* NamedFallback = nullptr;
    if (!Found)
    {
        for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
        {
            ACameraActor* Camera = *It;
            if (Camera->ActorHasTag(MenuCameraTag)) { Found = Camera; break; }
            bool bNameMatches = Camera->GetFName() == MenuCameraTag;
#if WITH_EDITOR
            bNameMatches |= Camera->GetActorLabel() == MenuCameraTag.ToString();
#endif
            if (bNameMatches && !NamedFallback) NamedFallback = Camera;
        }
        if (!Found) Found = NamedFallback;
    }
    if (bSelectionChanged || Found != BaselineCamera.Get() || !bHasCameraBaseline)
    {
        RestoreMenuCameraBaseline();
        BaselineCamera.Reset();
        bHasCameraBaseline = false;
        CameraImpulses.Reset();
        if (Found) CaptureMenuCameraBaseline(Found);
    }
    SelectedMenuCamera = Found;
    if (!Found)
    {
        if (!bReportedMissingCamera)
            UE_LOG(LogTemp, Warning, TEXT("DoughWorld frontend: place a CameraActor with Actor Tag '%s'; no pawn or camera is spawned by this HUD."), *MenuCameraTag.ToString());
        bReportedMissingCamera = true;
        return false;
    }
    bReportedMissingCamera = false;
    PC->bAutoManageActiveCameraTarget = false;
    // ADWPlayerController enables full paused ticks in its own BeginPlay.
    PC->SetTickableWhenPaused(true);
    GetWorld()->bIsCameraMoveableWhenPaused = true;
    PC->SetViewTarget(Found);
    if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0.f);
    return true;
}

void ADWFrontendHUD::PlayMenuCameraNudge(float Strength)
{
    AddMenuCameraImpulse(TransitionLocationAmplitudeCm, TransitionRotationAmplitudeDegrees, Strength);
}

void ADWFrontendHUD::AddMenuCameraImpulse(FVector LocalLocationAmplitudeCm, FRotator LocalRotationAmplitudeDegrees, float Strength)
{
    if (!bEnableTransitionNudge || TransitionDurationSeconds <= 0.f || !IsRuntimeCamera(SelectedMenuCamera) || !bHasCameraBaseline) return;
    APlayerController* PC = GetOwningPlayerController();
    if (!PC || PC->GetViewTarget() != SelectedMenuCamera) return;
    if (CameraImpulses.Num() >= 8) CameraImpulses.RemoveAt(0);
    const float Amount = FMath::Clamp(Strength, -2.f, 2.f);
    FMenuCameraImpulse& Impulse = CameraImpulses.AddDefaulted_GetRef();
    Impulse.StartedAt = FPlatformTime::Seconds();
    Impulse.LocationAmplitude = LocalLocationAmplitudeCm * Amount;
    Impulse.RotationAmplitude = LocalRotationAmplitudeDegrees * Amount;
}

void ADWFrontendHUD::UpdateMenuCameraMotion(double Now)
{
    ACameraActor* Camera = SelectedMenuCamera;
    APlayerController* PC = GetOwningPlayerController();
    if (!bHasCameraBaseline || BaselineCamera.Get() != Camera || !IsRuntimeCamera(Camera)) return;
    if (!PC || PC->GetViewTarget() != Camera)
    {
        RestoreMenuCameraBaseline();
        CameraImpulses.Reset();
        return;
    }
    FVector LocalOffset = FVector::ZeroVector;
    FRotator LocalRotation = FRotator::ZeroRotator;
    if (bEnableCameraSway)
    {
        const double Elapsed = FMath::Max(0., Now - CameraMotionStartedAt);
        const FVector Wave(
            FMath::Sin(2. * PI * FMath::Clamp(SwayFrequencyHz.X, 0., 10.) * Elapsed),
            FMath::Sin(2. * PI * FMath::Clamp(SwayFrequencyHz.Y, 0., 10.) * Elapsed),
            FMath::Sin(2. * PI * FMath::Clamp(SwayFrequencyHz.Z, 0., 10.) * Elapsed));
        LocalOffset = SwayLocationAmplitudeCm * Wave;
        LocalRotation = FRotator(SwayRotationAmplitudeDegrees.Pitch * Wave.X, SwayRotationAmplitudeDegrees.Yaw * Wave.Y, SwayRotationAmplitudeDegrees.Roll * Wave.Z);
    }
    if (!bEnableTransitionNudge) CameraImpulses.Reset();
    const double Duration = FMath::Clamp(TransitionDurationSeconds, 0.f, 3.f);
    for (int32 Index = CameraImpulses.Num() - 1; Index >= 0; --Index)
    {
        const FMenuCameraImpulse& Impulse = CameraImpulses[Index];
        const double Age = FMath::Max(0., Now - Impulse.StartedAt);
        if (Duration <= 0. || Age >= Duration) { CameraImpulses.RemoveAt(Index); continue; }
        const double Tail = 1. - Age / Duration;
        // Starts and ends at zero; multiple quick page changes add bounded, independent short impulses.
        const double Weight = FMath::Sin(2. * PI * FMath::Clamp(TransitionFrequencyHz, .1f, 20.f) * Age)
            * FMath::Exp(-FMath::Clamp(TransitionDecayPerSecond, 0.f, 50.f) * Age) * Tail * Tail;
        LocalOffset += Impulse.LocationAmplitude * Weight;
        LocalRotation += Impulse.RotationAmplitude * Weight;
    }
    CurrentLocalOffset = LocalOffset;
    CurrentLocalRotation = LocalRotation;
    if (!bEnableCameraSway && CameraImpulses.IsEmpty())
    {
        RestoreMenuCameraBaseline();
        if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0.f);
        return;
    }
    // Always derive from the captured placement. Never accumulate per-frame position/rotation changes.
    const FQuat BaseRotation = CameraBaseline.GetRotation();
    const FTransform Desired(BaseRotation * LocalRotation.Quaternion(),
        CameraBaseline.GetLocation() + BaseRotation.RotateVector(LocalOffset), CameraBaseline.GetScale3D());
    if (Camera->SetActorTransform(Desired, false, nullptr, ETeleportType::TeleportPhysics))
    {
        LastAppliedCameraTransform = Camera->GetActorTransform();
        bHasAppliedCameraMotion = true;
        // The original ADWPlayerController already enables full paused camera updates.
        if (PC->PlayerCameraManager) PC->PlayerCameraManager->UpdateCamera(0.f);
    }
}

void ADWFrontendHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateFrontendAudio(FPlatformTime::Seconds());
    const bool bSelectionChanged = !bHaveSelectionInputs || MenuCameraTag != LastRequestedCameraTag || MenuCameraOverride.Get() != LastRequestedOverride.Get();
    if (bKeepMenuCameraActive || bSelectionChanged)
    {
        APlayerController* PC = GetOwningPlayerController();
        if (bSelectionChanged || !IsValid(SelectedMenuCamera) || (bKeepMenuCameraActive && PC && PC->GetViewTarget() != SelectedMenuCamera)) RefreshMenuCamera();
    }
    const EDWMenuPage Page = GetMenuPage();
    if (bHasObservedMenuPage && Page != LastObservedMenuPage && Page != EDWMenuPage::None) PlayMenuCameraNudge();
    LastObservedMenuPage = Page;
    bHasObservedMenuPage = true;
    UpdateMenuCameraMotion(FPlatformTime::Seconds());
}
