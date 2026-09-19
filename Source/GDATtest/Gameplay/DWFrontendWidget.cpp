#include "DWFrontendWidget.h"
#include "DWUIBounceComponent.h"
#include "DWUserSettings.h"
#include "DWFrontendHUD.h"
#include "DWTextRevealComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"

#include "Animation/WidgetAnimation.h"
#include "Components/BackgroundBlur.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Slate/WidgetTransform.h"

namespace
{
    // Weak, per-session history: separate PIE sessions and game instances never share progress.
    TSet<TWeakObjectPtr<UGameInstance>> FrontendIntroSeen;

    float AnimationLength(const UWidgetAnimation* Animation)
    {
        return Animation ? FMath::Max(0.f, Animation->GetEndTime() - Animation->GetStartTime()) : 0.f;
    }

    float Progress(double Elapsed, float Duration)
    {
        return Duration <= KINDA_SMALL_NUMBER ? 1.f : FMath::Clamp(static_cast<float>(Elapsed / Duration), 0.f, 1.f);
    }

    float Smooth(float Alpha) { return Alpha * Alpha * (3.f - 2.f * Alpha); }

    void ResetTransform(UWidget* Widget)
    {
        if (Widget) Widget->SetRenderTransform(FWidgetTransform());
    }
}

void UDWFrontendWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetIsFocusable(true);
    bHasObservedPage = false;
    if (SkipIntroButton)
    {
        SkipIntroButton->OnClicked.RemoveDynamic(this, &UDWFrontendWidget::HandleSkipIntroClicked);
        SkipIntroButton->OnClicked.AddDynamic(this, &UDWFrontendWidget::HandleSkipIntroClicked);
    }
    ApplyLogoArtwork();
    if (MenuBlur) MenuBlur->SetBlurStrength(FMath::Clamp(MenuBlurStrength, 0.f, 100.f));
    for (auto It = FrontendIntroSeen.CreateIterator(); It; ++It)
        if (!It->IsValid()) It.RemoveCurrent();
    const TWeakObjectPtr<UGameInstance> Instance(GetGameInstance());
    const bool bAlreadySeen = Instance.IsValid() && FrontendIntroSeen.Contains(Instance);
    if (bPlayIntroOnConstruct && (bReplayIntroWhenReturningToMenu || !bAlreadySeen))
        ReplayIntro();
    else
    {
        if (ADWFrontendHUD* FrontendHUD = Cast<ADWFrontendHUD>(HUD)) FrontendHUD->BeginFrontendAudioSequence();
        StopPresentationSounds();
        bIntroPlaying = false;
        IntroPhase = EDWFrontendIntroPhase::Ready;
        ForceFinalPresentation();
        ScheduleMenuMusic();
    }
}

void UDWFrontendWidget::NativeDestruct()
{
    ControlTextRevealIn(LogoLayer, false);
    ControlTextRevealIn(TitlePage, false);
    bPendingMenuTextReveal = false;
    ++IntroGeneration;
    bIntroPlaying = false;
    bMenuEntering = false;
    if (ADWFrontendHUD* FrontendHUD = Cast<ADWFrontendHUD>(HUD)) FrontendHUD->CancelFrontendAudioSequence();
    StopPresentationSounds();
    StopIntroAnimations();
    if (SkipIntroButton) SkipIntroButton->OnClicked.RemoveDynamic(this, &UDWFrontendWidget::HandleSkipIntroClicked);
    Super::NativeDestruct();
}

void UDWFrontendWidget::ApplyLogoArtwork()
{
    if (LogoImage)
    {
        if (LogoTexture) LogoImage->SetBrushFromTexture(LogoTexture);
        LogoImage->SetVisibility(LogoTexture ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    if (LogoText)
        LogoText->SetVisibility(LogoTexture && !bShowLogoTextWithTexture ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UDWFrontendWidget::ReplayIntro()
{
    ++IntroGeneration;
    if (ADWFrontendHUD* FrontendHUD = Cast<ADWFrontendHUD>(HUD)) FrontendHUD->BeginFrontendAudioSequence();
    StopPresentationSounds();
    StopIntroAnimations();
    bMenuEntering = false;
    bIntroPlaying = true;
    FinalPresentationFrames = 0;
    ApplyLogoArtwork();
    ResetTitlePresentation();
    if (UGameInstance* Instance = GetGameInstance()) FrontendIntroSeen.Add(TWeakObjectPtr<UGameInstance>(Instance));
    // Explicit replay is a presentation action. It does not navigate menus or touch saves.
    EnterPhase(EDWFrontendIntroPhase::BlackHold, FPlatformTime::Seconds());
    SetFocus();
}

float UDWFrontendWidget::GetPhaseSeconds(EDWFrontendIntroPhase Phase) const
{
    switch (Phase)
    {
        case EDWFrontendIntroPhase::BlackHold: return FMath::Max(0.f, BlackHoldSeconds);
        case EDWFrontendIntroPhase::LogoIn: return FMath::Max(0.f, LogoInSeconds);
        case EDWFrontendIntroPhase::LogoHold: return FMath::Max(0.f, LogoHoldSeconds);
        case EDWFrontendIntroPhase::LogoOut: return FMath::Max(0.f, LogoOutSeconds);
        case EDWFrontendIntroPhase::Reveal: return FMath::Max(0.f, FMath::Max(RevealSeconds, MenuEnterSeconds));
        default: return 0.f;
    }
}

bool UDWFrontendWidget::StartTimedAnimation(UWidgetAnimation* Animation, float DesiredSeconds)
{
    const float Length = AnimationLength(Animation);
    if (Length <= KINDA_SMALL_NUMBER || DesiredSeconds <= KINDA_SMALL_NUMBER) return false;
    StopAnimation(Animation);
    PlayAnimation(Animation, 0.f, 1, EUMGSequencePlayMode::Forward, Length / DesiredSeconds, false);
    return true;
}

void UDWFrontendWidget::SeekTimedAnimation(UWidgetAnimation* Animation, double Elapsed, float DesiredSeconds)
{
    if (AnimationLength(Animation) > KINDA_SMALL_NUMBER && DesiredSeconds > KINDA_SMALL_NUMBER)
        SetAnimationCurrentTime(Animation, Progress(Elapsed, DesiredSeconds) * AnimationLength(Animation));
}

void UDWFrontendWidget::EnterPhase(EDWFrontendIntroPhase Phase, double StartedAt)
{
    IntroPhase = Phase;
    PhaseStartedAt = StartedAt;
    switch (Phase)
    {
        case EDWFrontendIntroPhase::BlackHold:
            bPendingMenuTextReveal = false;
            ControlTextRevealIn(LogoLayer, false);
            ControlTextRevealIn(TitlePage, false);
            PlayIntroSound(BlackScreenSound);
            if (BlackLayer) { BlackLayer->SetRenderOpacity(1.f); BlackLayer->SetVisibility(ESlateVisibility::Visible); }
            if (LogoLayer) { LogoLayer->SetRenderOpacity(0.f); ResetTransform(LogoLayer); LogoLayer->SetVisibility(ESlateVisibility::HitTestInvisible); }
            if (MenuRoot) { MenuRoot->SetRenderOpacity(0.f); MenuRoot->SetIsEnabled(true); MenuRoot->SetVisibility(ESlateVisibility::HitTestInvisible); }
            if (SkipIntroButton) SkipIntroButton->SetVisibility(bAllowSkip ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
            break;
        case EDWFrontendIntroPhase::LogoIn:
            StartTimedAnimation(Anim_LogoIn, LogoInSeconds);
            ControlTextRevealIn(LogoLayer, true);
            PlayIntroSound(LogoSound);
            break;
        case EDWFrontendIntroPhase::LogoHold:
            if (Anim_LogoIn) StopAnimation(Anim_LogoIn);
            if (LogoLayer) { LogoLayer->SetRenderOpacity(1.f); ResetTransform(LogoLayer); }
            break;
        case EDWFrontendIntroPhase::LogoOut:
            // The logo is leaving: keep the displayed letters, but do not leave dialogue audio running.
            if (bSynchronizeTextRevealWithIntro && WidgetTree)
                WidgetTree->ForEachWidget([this](UWidget* W)
                {
                    for (UWidget* P=W; P; P=P->GetParent()) if (P==LogoLayer)
                    {
                        if (auto* E=UDWTextRevealLibrary::GetTextRevealComponent(W); E && E->bPlayOnConstruct && !E->bExternalClock) E->Stop(true);
                        break;
                    }
                });
            StartTimedAnimation(Anim_LogoOut, LogoOutSeconds);
            break;
        case EDWFrontendIntroPhase::Reveal:
            if (Anim_LogoOut) StopAnimation(Anim_LogoOut);
            if (LogoLayer) { LogoLayer->SetRenderOpacity(0.f); LogoLayer->SetVisibility(ESlateVisibility::Collapsed); }
            if (MenuRoot) MenuRoot->SetRenderOpacity(1.f);
            StartTimedAnimation(Anim_Reveal, RevealSeconds);
            StartMenuEntrance(StartedAt);
            break;
        default: break;
    }
    OnIntroPhaseChanged(Phase);
}

void UDWFrontendWidget::AdvanceIntro(double Now)
{
    const uint32 Generation = IntroGeneration;
    // A hitch can cross several (including zero-length) phases. Retain the absolute timeline.
    for (int32 Transition = 0; bIntroPlaying && Transition < 6; ++Transition)
    {
        const double Duration = GetPhaseSeconds(IntroPhase);
        if (Now - PhaseStartedAt < Duration) break;
        if (IntroPhase == EDWFrontendIntroPhase::Reveal)
        {
            FinishIntro(false);
            return;
        }
        const auto Next = static_cast<EDWFrontendIntroPhase>(static_cast<uint8>(IntroPhase) + 1);
        EnterPhase(Next, PhaseStartedAt + Duration);
        // Blueprint callbacks may request another intro or skip it immediately.
        if (IntroGeneration != Generation || !bIntroPlaying) return;
    }
    if (bIntroPlaying) ApplyPhasePresentation(Now);
}

void UDWFrontendWidget::ApplyPhasePresentation(double Now)
{
    const double Elapsed = FMath::Max(0., Now - PhaseStartedAt);
    // Block hit testing without Slate disabled tint: enabling at the end must not change the artwork.
    if (MenuRoot) { MenuRoot->SetIsEnabled(true); MenuRoot->SetVisibility(ESlateVisibility::HitTestInvisible); }
    if (SkipIntroButton) SkipIntroButton->SetVisibility(bAllowSkip ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    switch (IntroPhase)
    {
        case EDWFrontendIntroPhase::LogoIn:
            if (AnimationLength(Anim_LogoIn) > KINDA_SMALL_NUMBER && LogoInSeconds > KINDA_SMALL_NUMBER)
                SeekTimedAnimation(Anim_LogoIn, Elapsed, LogoInSeconds);
            else if (LogoLayer)
            {
                const float Alpha = Smooth(Progress(Elapsed, LogoInSeconds));
                LogoLayer->SetRenderOpacity(Alpha);
                LogoLayer->SetRenderScale(FVector2D(FMath::Lerp(.85f, 1.f, Alpha)));
            }
            break;
        case EDWFrontendIntroPhase::LogoOut:
            if (AnimationLength(Anim_LogoOut) > KINDA_SMALL_NUMBER && LogoOutSeconds > KINDA_SMALL_NUMBER)
                SeekTimedAnimation(Anim_LogoOut, Elapsed, LogoOutSeconds);
            else if (LogoLayer) LogoLayer->SetRenderOpacity(1.f - Smooth(Progress(Elapsed, LogoOutSeconds)));
            break;
        case EDWFrontendIntroPhase::Reveal:
            if (Elapsed >= FMath::Max(0.f, RevealSeconds))
            {
                if (Anim_Reveal) StopAnimation(Anim_Reveal);
                if (BlackLayer) { BlackLayer->SetRenderOpacity(0.f); BlackLayer->SetVisibility(ESlateVisibility::Collapsed); }
            }
            else if (AnimationLength(Anim_Reveal) > KINDA_SMALL_NUMBER)
                SeekTimedAnimation(Anim_Reveal, Elapsed, RevealSeconds);
            else if (BlackLayer) BlackLayer->SetRenderOpacity(1.f - Smooth(Progress(Elapsed, RevealSeconds)));
            break;
        default: break;
    }
}

void UDWFrontendWidget::ResetTitlePresentation()
{
    if (TitlePage) { TitlePage->SetRenderOpacity(1.f); ResetTransform(TitlePage); }
    // These three controls may have optional stagger tracks in Anim_MenuEnter.
    for (UButton* Button : {StartButton.Get(), TitleSettingsButton.Get(), QuitButton.Get()})
        if (Button) { Button->SetRenderOpacity(1.f); ResetTransform(Button); }
}

void UDWFrontendWidget::ForceFinalPresentation()
{
    if (BlackLayer) { BlackLayer->SetRenderOpacity(0.f); BlackLayer->SetVisibility(ESlateVisibility::Collapsed); }
    if (LogoLayer) { LogoLayer->SetRenderOpacity(0.f); ResetTransform(LogoLayer); LogoLayer->SetVisibility(ESlateVisibility::Collapsed); }
    if (SkipIntroButton) SkipIntroButton->SetVisibility(ESlateVisibility::Collapsed);
    if (MenuRoot) { MenuRoot->SetRenderOpacity(1.f); MenuRoot->SetIsEnabled(true); MenuRoot->SetVisibility(HUD && HUD->GetMenuPage() == EDWMenuPage::None ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); ResetTransform(MenuRoot); }
    ResetTitlePresentation();
}

void UDWFrontendWidget::StopIntroAnimations()
{
    for (UWidgetAnimation* Animation : {Anim_LogoIn.Get(), Anim_LogoOut.Get(), Anim_Reveal.Get(), Anim_MenuEnter.Get()})
        if (Animation) StopAnimation(Animation);
}

void UDWFrontendWidget::FinishIntro(bool bSkipped)
{
    if (!bIntroPlaying) return;
    ++IntroGeneration;
    const uint32 Generation = IntroGeneration;
    bIntroPlaying = false;
    bMenuEntering = false;
    StopIntroAnimations();
    IntroPhase = EDWFrontendIntroPhase::Ready;
    ForceFinalPresentation();
    ControlTextRevealIn(LogoLayer, false);
    if (bSkipped) { ControlTextRevealIn(TitlePage, false); bPendingMenuTextReveal = true; PrepareButtonSequence(); }
    // Re-assert after queued UMG stop evaluations so skipping never leaves a staggered button hidden.
    FinalPresentationFrames = 2;
    // Strict sequence: stop owned black/logo/reveal cues, then delay, then allow menu music.
    // Skipping never launches another intro cue after this boundary.
    StopPresentationSounds();
    ScheduleMenuMusic();
    OnIntroPhaseChanged(IntroPhase);
    if (IntroGeneration == Generation && !bIntroPlaying) OnIntroFinished(bSkipped);
}

void UDWFrontendWidget::SkipIntro() { FinishIntro(true); }

void UDWFrontendWidget::HandleSkipIntroClicked()
{
    if (bAllowSkip && bIntroPlaying) { PlayClick(); SkipIntro(); }
}

void UDWFrontendWidget::StartMenuEntrance(double StartedAt)
{
    ControlTextRevealIn(TitlePage, false);
    bPendingMenuTextReveal = true;
    if (Anim_MenuEnter) StopAnimation(Anim_MenuEnter);
    ResetTitlePresentation();
    FinalPresentationFrames = 0;
    MenuEnteredAt = StartedAt;
    bMenuEntering = true;
    PrepareButtonSequence();
    if (TitlePage)
    {
        TitlePage->SetRenderOpacity(0.f);
        TitlePage->SetRenderScale(FVector2D(.9f));
        TitlePage->SetRenderTranslation(FVector2D(0.f, 30.f));
    }
    StartTimedAnimation(Anim_MenuEnter, MenuEnterSeconds);
    const ADWFrontendHUD* FrontendHUD = Cast<ADWFrontendHUD>(HUD);
    // A page refresh/return during the post-intro delay must not launch another intro cue.
    if (bIntroPlaying || !FrontendHUD || FrontendHUD->IsMenuMusicAllowed()) PlayIntroSound(MenuRevealSound);
}

void UDWFrontendWidget::PlayMenuEntrance()
{
    if (!bIntroPlaying) StartMenuEntrance(FPlatformTime::Seconds());
}

void UDWFrontendWidget::ControlTextRevealIn(UWidget* Root, bool bReplay)
{
    if (!bSynchronizeTextRevealWithIntro || !Root || !WidgetTree) return;
    WidgetTree->ForEachWidget([Root,bReplay](UWidget* W)
    {
        for (UWidget* P=W; P; P=P->GetParent()) if (P==Root)
        {
            if (auto* E=UDWTextRevealLibrary::GetTextRevealComponent(W); E && E->bPlayOnConstruct && !E->bExternalClock)
            {
                if (bReplay) E->Replay(); else E->Stop(false);
            }
            break;
        }
    });
}

void UDWFrontendWidget::TryStartMenuTextReveal()
{
    if (!bPendingMenuTextReveal || !TitlePage || (HUD && HUD->GetMenuPage()!=EDWMenuPage::Title)) return;
    // A fully visible text can still be covered by the independent black overlay.
    if (bIntroPlaying && IntroPhase!=EDWFrontendIntroPhase::Reveal) return;
    if (BlackLayer && BlackLayer->IsVisible() && BlackLayer->GetRenderOpacity()>.02f) return;
    if (!TitlePage->IsVisible() || TitlePage->GetRenderOpacity()<=.01f) return;
    bPendingMenuTextReveal = false;
    ControlTextRevealIn(TitlePage, true);
}

void UDWFrontendWidget::TickMenuEntrance(double Now)
{
    if (!bMenuEntering) return;
    const double Elapsed = FMath::Max(0., Now - MenuEnteredAt);
    const float Alpha = Progress(Elapsed, MenuEnterSeconds);
    if (Alpha >= 1.f)
    {
        if (Anim_MenuEnter) StopAnimation(Anim_MenuEnter);
        bMenuEntering = false;
        ResetTitlePresentation();
        return;
    }
    if (AnimationLength(Anim_MenuEnter) > KINDA_SMALL_NUMBER)
        SeekTimedAnimation(Anim_MenuEnter, Elapsed, MenuEnterSeconds);
    else if (TitlePage)
    {
        const float X = Alpha - 1.f;
        const float Bounce = 1.f + 2.70158f * X * X * X + 1.70158f * X * X;
        TitlePage->SetRenderOpacity(Smooth(FMath::Min(1.f, Alpha * 2.f)));
        TitlePage->SetRenderScale(FVector2D(FMath::Lerp(.9f, 1.f, Bounce)));
        TitlePage->SetRenderTranslation(FVector2D(0.f, (1.f - Bounce) * 30.f));
    }
}

void UDWFrontendWidget::PlayIntroSound(USoundBase* Sound)
{
    ReleaseFinishedPresentationSounds();
    if (!Sound) return;
    UAudioComponent* Audio = UGameplayStatics::CreateSound2D(this, Sound, FMath::Clamp(SoundVolume, 0.f, 2.f), 1.f, 0.f, nullptr, false, false);
    if (!Audio) return;
    if(auto* S=UDWUserSettings::Resolve(this))S->RouteAudio(Audio,EDWSoundCategory::SFX);
    Audio->SetUISound(true); // Set before Play, because the frontend intentionally pauses the world.
    PresentationAudioComponents.Add(Audio);
    Audio->Play();
}

void UDWFrontendWidget::StopPresentationSounds()
{
    for (UAudioComponent* Audio : PresentationAudioComponents)
        if (IsValid(Audio)) { Audio->Stop(); Audio->DestroyComponent(); }
    PresentationAudioComponents.Reset();
}

void UDWFrontendWidget::ReleaseFinishedPresentationSounds()
{
    for (int32 Index = PresentationAudioComponents.Num() - 1; Index >= 0; --Index)
    {
        UAudioComponent* Audio = PresentationAudioComponents[Index];
        if (!IsValid(Audio) || !Audio->IsPlaying())
        {
            if (IsValid(Audio)) Audio->DestroyComponent();
            PresentationAudioComponents.RemoveAt(Index);
        }
    }
}

int32 UDWFrontendWidget::GetPlayingPresentationSoundCount() const
{
    int32 Count = 0;
    for (const UAudioComponent* Audio : PresentationAudioComponents)
        if (IsValid(Audio) && Audio->IsPlaying()) ++Count;
    return Count;
}

void UDWFrontendWidget::ScheduleMenuMusic()
{
    if (ADWFrontendHUD* FrontendHUD = Cast<ADWFrontendHUD>(HUD))
        FrontendHUD->CompleteFrontendAudioSequence(MenuMusicStartDelay);
}

void UDWFrontendWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds); // Existing localization, font, save-slot and settings behavior.
    ReleaseFinishedPresentationSounds();
    const double Now = FPlatformTime::Seconds(); // Independent of the paused world's game time.
    const EDWMenuPage Page = HUD ? HUD->GetMenuPage() : EDWMenuPage::None;
    const bool bPageChanged = !bHasObservedPage || Page != LastObservedPage;
    LastObservedPage = Page;
    bHasObservedPage = true;
    if (bPageChanged)
    {
        if (Page != EDWMenuPage::Title)
        {
            bPendingMenuTextReveal = false;
            ControlTextRevealIn(TitlePage, false);
        }
        if (bIntroPlaying && Page != EDWMenuPage::Title && Page != EDWMenuPage::None)
            FinishIntro(true); // A Blueprint-requested settings/slots page remains usable; never navigate back.
        if (!bIntroPlaying && Page == EDWMenuPage::Title)
            PlayMenuEntrance();
        else if (!bIntroPlaying && bMenuEntering)
        {
            if (Anim_MenuEnter) StopAnimation(Anim_MenuEnter);
            bMenuEntering = false;
            ResetTitlePresentation();
        }
    }
    if (MenuBlur)
    {
        MenuBlur->SetBlurStrength(FMath::Clamp(MenuBlurStrength, 0.f, 100.f));
        // Also works when the Designer nests MenuRoot inside the blur widget.
        MenuBlur->SetVisibility(Page == EDWMenuPage::None ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
    }
    if (bIntroPlaying) AdvanceIntro(Now);
    TickMenuEntrance(Now);
    TryStartMenuTextReveal();
    if (!bIntroPlaying && !bMenuEntering && FinalPresentationFrames > 0)
    {
        ForceFinalPresentation();
        --FinalPresentationFrames;
    }
    TickButtonSequence(Now);
}

FReply UDWFrontendWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (bIntroPlaying)
    {
        if (bAllowSkip && !Event.IsRepeat() && (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::SpaceBar)) SkipIntro();
        return FReply::Handled(); // Do not let a skip also activate a focused Start button.
    }
    return Super::NativeOnPreviewKeyDown(Geometry, Event);
}

FReply UDWFrontendWidget::NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event)
{
    if (bIntroPlaying)
    {
        if (bAllowSkip && !Event.IsRepeat() && (Event.GetKey() == EKeys::Escape || Event.GetKey() == EKeys::SpaceBar)) SkipIntro();
        return FReply::Handled();
    }
    return Super::NativeOnKeyDown(Geometry, Event);
}

FReply UDWFrontendWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    // Let the topmost Skip button receive the click, without running remapped gameplay/menu shortcuts.
    return bIntroPlaying ? FReply::Unhandled() : Super::NativeOnPreviewMouseButtonDown(Geometry, Event);
}

FReply UDWFrontendWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
    return bIntroPlaying ? FReply::Handled() : Super::NativeOnMouseButtonDown(Geometry, Event);
}

void UDWFrontendWidget::PrepareButtonSequence()
{
 bButtonSequencePending=bButtonsAfterTitle;ButtonsStartedAt=-1.;ButtonsShown=0;
 for(auto* B:{StartButton.Get(),TitleSettingsButton.Get(),QuitButton.Get()})if(B){UDWUIBounceLibrary::ResetWidgetBounce(B);B->SetVisibility(bButtonsAfterTitle?ESlateVisibility::Hidden:ESlateVisibility::Visible);B->SetIsEnabled(!bButtonsAfterTitle);}
}
void UDWFrontendWidget::TickButtonSequence(double Now)
{
 if(!bButtonSequencePending||!HUD||HUD->GetMenuPage()!=EDWMenuPage::Title)return;
 UButton* Buttons[]={StartButton,TitleSettingsButton,QuitButton};
 if(ButtonsStartedAt<0)
 {
  for(auto* B:Buttons)if(B){B->SetVisibility(ESlateVisibility::Hidden);B->SetIsEnabled(false);}
  if(bPendingMenuTextReveal||bIntroPlaying||bMenuEntering)return;
  if(auto* R=UDWTextRevealLibrary::GetTextRevealComponent(TitleText);R&&R->IsPlaying())return;
  ButtonsStartedAt=Now+FMath::Max(0.f,ButtonsAfterTitleDelay);
 }
 for(int32 I=0;I<3;++I)if(auto* B=Buttons[I])
 {
  const float Age=Now-ButtonsStartedAt-I*FMath::Max(0.f,ButtonStaggerSeconds);
  if(Age<0){B->SetVisibility(ESlateVisibility::Hidden);continue;}
  if(ButtonsShown<=I){ButtonsShown=I+1;B->SetVisibility(ESlateVisibility::Visible);UDWUIBounceLibrary::PlayWidgetEntrance(B);}
  const float A=FMath::Clamp(Age/FMath::Max(.01f,ButtonFadeSeconds),0.f,1.f);B->SetRenderOpacity(A*A*(3-2*A));B->SetIsEnabled(A>=1.f);
 }
 if(Now>=ButtonsStartedAt+2*FMath::Max(0.f,ButtonStaggerSeconds)+FMath::Max(.01f,ButtonFadeSeconds))bButtonSequencePending=false;
}
