#pragma once

#include "CoreMinimal.h"
#include "DWGameplayHUD.h"
#include "DWFrontendHUD.generated.h"

class ACameraActor;

/** Menu-only HUD: reuses the existing menu actions, but views a placed camera without a player pawn. */
UCLASS(Blueprintable)
class GDATTEST_API ADWFrontendHUD : public ADWGameplayHUD
{
    GENERATED_BODY()
public:
    ADWFrontendHUD();

    /** Keep Niagara, foliage wind, Blueprint ticks and timelines running in this menu-only world. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Frontend|Background", meta=(DisplayName="Animate Menu Background"))
    bool bAnimateMenuBackground = true;

    /** Internal presentation hooks. The WBP owns the editable delay; the DA owns the music asset. */
    void BeginFrontendAudioSequence();
    void CompleteFrontendAudioSequence(float DelaySeconds);
    void CancelFrontendAudioSequence();
    UFUNCTION(BlueprintPure, Category="Frontend|Audio|Diagnostics") bool IsMenuMusicPending() const { return bMenuMusicPending; }
    UFUNCTION(BlueprintPure, Category="Frontend|Audio|Diagnostics") bool IsMenuMusicAllowed() const { return bMenuMusicAllowed && !bFrontendEnding; }
    UFUNCTION(BlueprintPure, Category="Frontend|Audio|Diagnostics") float GetMenuMusicDelayRemaining() const;

    /** WidgetClass in a HUD Blueprint takes precedence over this fallback path. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Frontend|UI")
    FSoftClassPath FrontendWidgetClassPath = FSoftClassPath(TEXT("/Game/DoughWorld/UI/Frontend/WBP_DWFrontend.WBP_DWFrontend_C"));

    /** A placed CameraActor's Actor Tag, object name, or editor label. Tag also works in packaged games. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera") FName MenuCameraTag = TEXT("DWMenuCamera");
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Frontend|Camera") TObjectPtr<ACameraActor> MenuCameraOverride;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera") bool bKeepMenuCameraActive = true;
    UFUNCTION(BlueprintCallable, Category="Frontend|Camera") bool RefreshMenuCamera();
    UFUNCTION(BlueprintPure, Category="Frontend|Camera") ACameraActor* GetMenuCamera() const { return SelectedMenuCamera; }

    /** Only the selected, currently viewed camera in this runtime world is moved. No FOV changes. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Sway") bool bEnableCameraSway = true;
    /** Camera-local axes: X forward, Y right, Z up. These are maximum offsets, not per-frame deltas. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Sway", meta=(Units="cm")) FVector SwayLocationAmplitudeCm = FVector(0.f, 1.5f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Sway", meta=(Units="deg")) FRotator SwayRotationAmplitudeDegrees = FRotator(.15f, .15f, .03f);
    /** XYZ drive local XYZ translation and Pitch/Yaw/Roll respectively. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Sway", meta=(ClampMin="0", ClampMax="10", Units="Hz")) FVector SwayFrequencyHz = FVector(.10f, .08f, .12f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Transition") bool bEnableTransitionNudge = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Transition", meta=(Units="cm")) FVector TransitionLocationAmplitudeCm = FVector(0.f, 1.8f, .8f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Transition", meta=(Units="deg")) FRotator TransitionRotationAmplitudeDegrees = FRotator(.12f, -.18f, .03f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Transition", meta=(ClampMin="0.1", ClampMax="20", Units="Hz")) float TransitionFrequencyHz = 3.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Transition", meta=(ClampMin="0", ClampMax="50")) float TransitionDecayPerSecond = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Camera|Transition", meta=(ClampMin="0", ClampMax="3", Units="s")) float TransitionDurationSeconds = .65f;
    UFUNCTION(BlueprintCallable, Category="Frontend|Camera|Transition") void PlayMenuCameraNudge(float Strength = 1.f);
    /** Adds a short, decaying local impulse. Strength is capped to [-2, 2]; no persistent camera offset. */
    UFUNCTION(BlueprintCallable, Category="Frontend|Camera|Transition") void AddMenuCameraImpulse(FVector LocalLocationAmplitudeCm, FRotator LocalRotationAmplitudeDegrees, float Strength = 1.f);
    UFUNCTION(BlueprintPure, Category="Frontend|Camera|Diagnostics") bool HasMenuCameraBaseline() const { return bHasCameraBaseline; }
    UFUNCTION(BlueprintPure, Category="Frontend|Camera|Diagnostics") FTransform GetMenuCameraBaseline() const { return CameraBaseline; }
    UFUNCTION(BlueprintPure, Category="Frontend|Camera|Diagnostics") FVector GetCurrentMenuCameraOffset() const { return CurrentLocalOffset; }
    UFUNCTION(BlueprintPure, Category="Frontend|Camera|Diagnostics") FRotator GetCurrentMenuCameraRotation() const { return CurrentLocalRotation; }

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldPlayMenuMusic() const override { return IsMenuMusicAllowed(); }
    virtual bool ShouldPauseWorldForMenu(EDWMenuPage Page) const override { return !bAnimateMenuBackground && Super::ShouldPauseWorldForMenu(Page); }
private:
    bool bMenuMusicAllowed = false;
    bool bMenuMusicPending = false;
    bool bFrontendEnding = false;
    double MenuMusicStartsAt = 0.;
    void UpdateFrontendAudio(double Now);
    struct FMenuCameraImpulse
    {
        double StartedAt = 0.;
        FVector LocationAmplitude = FVector::ZeroVector;
        FRotator RotationAmplitude = FRotator::ZeroRotator;
    };
    UPROPERTY(Transient) TObjectPtr<ACameraActor> SelectedMenuCamera;
    TWeakObjectPtr<ACameraActor> BaselineCamera;
    TWeakObjectPtr<ACameraActor> LastRequestedOverride;
    FName LastRequestedCameraTag;
    FTransform CameraBaseline = FTransform::Identity;
    FTransform LastAppliedCameraTransform = FTransform::Identity;
    FVector CurrentLocalOffset = FVector::ZeroVector;
    FRotator CurrentLocalRotation = FRotator::ZeroRotator;
    TArray<FMenuCameraImpulse> CameraImpulses;
    double CameraMotionStartedAt = 0.;
    EDWMenuPage LastObservedMenuPage = EDWMenuPage::None;
    bool bHasObservedMenuPage = false;
    bool bHasCameraBaseline = false;
    bool bHasAppliedCameraMotion = false;
    bool bHaveSelectionInputs = false;
    bool bReportedMissingCamera = false;
    bool IsRuntimeCamera(const ACameraActor* Camera) const;
    void CaptureMenuCameraBaseline(ACameraActor* Camera);
    void RestoreMenuCameraBaseline();
    void UpdateMenuCameraMotion(double Now);
};
