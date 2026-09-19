#pragma once

#include "CoreMinimal.h"
#include "DWGameplayWidget.h"
#include "DWFrontendWidget.generated.h"

class UBackgroundBlur;
class UTexture2D;
class USoundBase;
class UWidgetAnimation;
class UAudioComponent;

UENUM(BlueprintType)
enum class EDWFrontendIntroPhase : uint8
{
    BlackHold, LogoIn, LogoHold, LogoOut, Reveal, Ready
};

/** Presentation for the separate frontend map. Save/settings actions remain in UDWGameplayWidget. */
UCLASS(Blueprintable)
class GDATTEST_API UDWFrontendWidget : public UDWGameplayWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro")
    bool bPlayIntroOnConstruct = true;

    /** True: every new frontend widget/map load plays the logo. False: once per GameInstance.
     * Returning from Settings/SaveSlots within this widget only replays the menu entrance.
     * This session-only choice never reads or writes a save slot.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro")
    bool bReplayIntroWhenReturningToMenu = true;

    /** Sync autoplay DW Text Reveal components under LogoLayer/TitlePage to their actual entrance.
     * Disable for manual Blueprint playback. Components with PlayOnConstruct off or ExternalClock on are not managed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Text Reveal")
    bool bSynchronizeTextRevealWithIntro = true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Frontend|Button Sequence") bool bButtonsAfterTitle=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Frontend|Button Sequence",meta=(ClampMin="0",Units="s")) float ButtonsAfterTitleDelay=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Frontend|Button Sequence",meta=(ClampMin="0",Units="s")) float ButtonStaggerSeconds=.1f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Frontend|Button Sequence",meta=(ClampMin="0.01",Units="s")) float ButtonFadeSeconds=.22f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro", meta=(ClampMin="0", Units="s")) float BlackHoldSeconds = .45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro", meta=(ClampMin="0", Units="s")) float LogoInSeconds = .55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro", meta=(ClampMin="0", Units="s")) float LogoHoldSeconds = 1.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro", meta=(ClampMin="0", Units="s")) float LogoOutSeconds = .4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro", meta=(ClampMin="0", Units="s")) float RevealSeconds = .65f;
    /** Starts with Reveal. Input opens when both Reveal and this entrance have finished. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro", meta=(ClampMin="0", Units="s")) float MenuEnterSeconds = .65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Intro") bool bAllowSkip = true;

    /** Empty texture uses the Designer-authored LogoText, retaining the existing font/language rules. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Artwork") TObjectPtr<UTexture2D> LogoTexture;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Artwork") bool bShowLogoTextWithTexture = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Artwork", meta=(ClampMin="0", ClampMax="100")) float MenuBlurStrength = 5.f;

    /** Plays once as BlackHold begins. Empty means silence; never substitutes another UI cue. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Audio", meta=(DisplayName="Black Screen Sound")) TObjectPtr<USoundBase> BlackScreenSound;
    /** Plays when LogoIn begins. Any unfinished intro sounds stop before the music delay starts. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Audio") TObjectPtr<USoundBase> LogoSound;
    /** Reveal cue during the intro; returning to the title later may play it as ordinary UI feedback. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Audio") TObjectPtr<USoundBase> MenuRevealSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Audio", meta=(ClampMin="0", ClampMax="2")) float SoundVolume = 1.f;
    /** Seconds after the whole intro completes/skips (or is disabled), before DA_DoughWorldGameplay.MenuBGM.
     * Uses real time even while paused. Changing settings/pages does not restart this delay or music.
     * Delay is sampled at intro completion; ReplayIntro cancels and schedules a new sequence.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Frontend|Audio", meta=(ClampMin="0", Units="s", DisplayName="Menu Music Start Delay")) float MenuMusicStartDelay = .5f;
    UFUNCTION(BlueprintPure, Category="Frontend|Audio|Diagnostics") int32 GetPlayingPresentationSoundCount() const;

    UFUNCTION(BlueprintCallable, Category="Frontend|Intro") void ReplayIntro();
    /** Programmatic skip is always permitted; bAllowSkip controls only player input. */
    UFUNCTION(BlueprintCallable, Category="Frontend|Intro") void SkipIntro();
    UFUNCTION(BlueprintCallable, Category="Frontend|Intro") void PlayMenuEntrance();
    UFUNCTION(BlueprintPure, Category="Frontend|Intro") bool IsIntroPlaying() const { return bIntroPlaying; }
    UFUNCTION(BlueprintPure, Category="Frontend|Intro") EDWFrontendIntroPhase GetIntroPhase() const { return IntroPhase; }
    UFUNCTION(BlueprintImplementableEvent, Category="Frontend|Events") void OnIntroPhaseChanged(EDWFrontendIntroPhase Phase);
    UFUNCTION(BlueprintImplementableEvent, Category="Frontend|Events") void OnIntroFinished(bool bSkipped);

protected:
    virtual bool ShouldAnimatePageEntrance(EDWMenuPage Page) const override { return Page != EDWMenuPage::Title && Super::ShouldAnimatePageEntrance(Page); }
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float DeltaSeconds) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;

    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UBorder> BlackLayer;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UBorder> LogoLayer;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UImage> LogoImage;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> LogoText;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UBackgroundBlur> MenuBlur;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UBorder> TitlePage;
    UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional)) TObjectPtr<UButton> SkipIntroButton;

    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> Anim_LogoIn;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> Anim_LogoOut;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> Anim_Reveal;
    UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetAnimOptional)) TObjectPtr<UWidgetAnimation> Anim_MenuEnter;

private:
    EDWFrontendIntroPhase IntroPhase = EDWFrontendIntroPhase::Ready;
    EDWMenuPage LastObservedPage = EDWMenuPage::None;
    bool bIntroPlaying = false;
    bool bMenuEntering = false;
    bool bHasObservedPage = false;
    bool bPendingMenuTextReveal = false;
    bool bButtonSequencePending=false;
    double ButtonsStartedAt=-1.;
    int32 ButtonsShown=0;
    void PrepareButtonSequence(); void TickButtonSequence(double Now);
    double PhaseStartedAt = 0.;
    double MenuEnteredAt = 0.;
    uint32 IntroGeneration = 0;
    int32 FinalPresentationFrames = 0;
    /** Own these components so an interrupted/skipped intro never leaves sound playing into music. */
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> PresentationAudioComponents;

    float GetPhaseSeconds(EDWFrontendIntroPhase Phase) const;
    void EnterPhase(EDWFrontendIntroPhase Phase, double StartedAt);
    void AdvanceIntro(double Now);
    void ApplyPhasePresentation(double Now);
    void FinishIntro(bool bSkipped);
    void ForceFinalPresentation();
    void ResetTitlePresentation();
    void StartMenuEntrance(double StartedAt);
    void TickMenuEntrance(double Now);
    void ApplyLogoArtwork();
    void PlayIntroSound(USoundBase* Sound);
    void StopPresentationSounds();
    void ReleaseFinishedPresentationSounds();
    void ScheduleMenuMusic();
    void StopIntroAnimations();
    void ControlTextRevealIn(UWidget* Root, bool bReplay);
    void TryStartMenuTextReveal();
    bool StartTimedAnimation(UWidgetAnimation* Animation, float DesiredSeconds);
    void SeekTimedAnimation(UWidgetAnimation* Animation, double Elapsed, float DesiredSeconds);
    UFUNCTION() void HandleSkipIntroClicked();
};
