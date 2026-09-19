#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/DataAsset.h"
#include "Extensions/UIComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWTextRevealComponent.generated.h"

class USoundBase;
class UAudioComponent;
class UTextBlock;
class SDWTextReveal;
struct FDWTextRevealState;

UENUM(BlueprintType)
enum class EDWTextVoiceLanguage : uint8 { Auto, English, Chinese };

/** A speaker preset: 36 English character slots and an expandable Chinese blip pool. */
UCLASS(BlueprintType)
class GDATTEST_API UDWTextVoiceProfile : public UDataAsset
{
 GENERATED_BODY()
public:
 UDWTextVoiceProfile();
 /** Keys A-Z and 0-9. Case-insensitive letter lookup. Empty slots fall back to legacy/fallback sounds. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|English", meta=(DisplayName="English Character Sounds (A-Z, 0-9)")) TMap<FName,TObjectPtr<USoundBase>> EnglishCharacterSounds;
 /** Eight slots by default. Add/remove freely; only populated slots participate in deterministic selection. Not spoken Chinese or pinyin. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Chinese", meta=(DisplayName="Chinese Blip Sounds (8 by default)")) TArray<TObjectPtr<USoundBase>> ChineseBlipSounds;
 /** Preview the sound chosen for one character; punctuation/disabled numbers return null. No sound is played. */
 UFUNCTION(BlueprintPure, Category="Voice") USoundBase* ResolveCharacterSound(const FString& Character,EDWTextVoiceLanguage Language=EDWTextVoiceLanguage::Auto) const;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Samples") TObjectPtr<USoundBase> FallbackSound;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Voice|Legacy") TObjectPtr<USoundBase> EnglishA;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Voice|Legacy") TObjectPtr<USoundBase> EnglishB;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Voice|Legacy") TObjectPtr<USoundBase> EnglishC;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Voice|Legacy") TObjectPtr<USoundBase> ChineseA;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Voice|Legacy") TObjectPtr<USoundBase> ChineseB;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category="Voice|Legacy") TObjectPtr<USoundBase> ChineseC;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Mix", meta=(ClampMin="0",ClampMax="1")) float Volume = .45f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Mix", meta=(ClampMin="0.25",ClampMax="4")) float EnglishPitch = 1.35f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Mix", meta=(ClampMin="0.25",ClampMax="4")) float ChinesePitch = 1.15f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Mix", meta=(ClampMin="0",ClampMax="0.5")) float PitchVariation = .12f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Rhythm", meta=(ClampMin="1",ClampMax="12")) int32 EnglishEveryNLetters = 2;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Rhythm", meta=(ClampMin="1",ClampMax="12")) int32 ChineseEveryNCharacters = 1;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Rhythm", meta=(ClampMin="0.02",ClampMax="1",Units="s")) float MinimumBlipInterval = .045f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Rhythm", meta=(ClampMin="0.02",ClampMax="1",Units="s")) float MaximumBlipDuration = .11f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Rhythm", meta=(ClampMin="0",ClampMax="0.1",Units="s")) float BlipFadeOut = .015f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Rhythm") bool bSpeakNumbers = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Voice|Rhythm") int32 VoiceSeed = 17;
};

USTRUCT(BlueprintType)
struct FDWTextRevealCue
{
 GENERATED_BODY()
 /** One-based grapheme count, including spaces but excluding hard line breaks. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue", meta=(ClampMin="1")) int32 AfterCharacter = 1;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cue") FName CueName;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWTextRevealSimpleEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDWTextRevealCharacterEvent,int32,CharacterIndex,FString,Character);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWTextRevealCueEvent,FName,CueName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWTextRevealFinishedEvent,bool,bSkipped);

/** Attach to an ordinary TextBlock using Designer > Add Component. Separate from DW UI Bounce.
 * The full text is laid out before revealing graphemes. Does not truncate TextBlock.Text.
 * Plain left-to-right Chinese/English text; RichTextBlock and editable text are not supported.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced, meta=(DisplayName="DW Text Reveal"))
class GDATTEST_API UDWTextRevealComponent : public UUIComponent
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Playback") bool bPlayOnConstruct = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Playback") bool bReplayOnTextChanged = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Playback") bool bPauseWhenNotPainted = true;
 /** Manual timeline time via SetPlaybackTime; no automatic advance. Scrubbing is always silent. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Playback") bool bExternalClock = false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Playback",meta=(ClampMin="0.1",ClampMax="10")) float PlaybackRate = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Timing",meta=(ClampMin="1",ClampMax="200")) float EnglishCharactersPerSecond = 24.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Timing",meta=(ClampMin="1",ClampMax="100")) float ChineseCharactersPerSecond = 12.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Timing",meta=(ClampMin="0",ClampMax="10",Units="s")) float InitialDelay = .05f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Timing",meta=(ClampMin="0",ClampMax="5",Units="s")) float LinePause = .14f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Timing",meta=(ClampMin="0",ClampMax="5",Units="s")) float CommaPause = .10f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Timing",meta=(ClampMin="0",ClampMax="5",Units="s")) float SentencePause = .22f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Spring",meta=(ClampMin="0.01",ClampMax="1")) float StartScale = .25f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Spring",meta=(ClampMin="0.1",ClampMax="30",Units="Hz")) float FrequencyHz = 5.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Spring",meta=(ClampMin="0.1",ClampMax="3")) float DampingRatio = .55f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Spring") FVector2D EntryOffset = FVector2D(0,8);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Spring",meta=(ClampMin="0",ClampMax="1",Units="s")) float FadeInSeconds = .08f;
 /** Final settling allowance; completion fires after both timing and spring finish. */
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Spring",meta=(ClampMin="0.1",ClampMax="5",Units="s")) float SpringSettleSeconds = .7f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Audio") TObjectPtr<UDWTextVoiceProfile> VoiceProfile;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Audio") EDWTextVoiceLanguage VoiceLanguage = EDWTextVoiceLanguage::Auto;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Audio") bool bEnableBlips = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Audio",meta=(ClampMin="0",ClampMax="1")) float VolumeMultiplier = 1.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Text Reveal|Cues") TArray<FDWTextRevealCue> Cues;
 UPROPERTY(BlueprintAssignable, Category="Text Reveal|Events") FDWTextRevealSimpleEvent OnStarted;
 UPROPERTY(BlueprintAssignable, Category="Text Reveal|Events") FDWTextRevealCharacterEvent OnCharacterRevealed;
 UPROPERTY(BlueprintAssignable, Category="Text Reveal|Events") FDWTextRevealCueEvent OnCue;
 UPROPERTY(BlueprintAssignable, Category="Text Reveal|Events") FDWTextRevealFinishedEvent OnFinished;
 UPROPERTY(BlueprintAssignable, Category="Text Reveal|Events") FDWTextRevealSimpleEvent OnStopped;

 UFUNCTION(BlueprintCallable, Category="UI|Text Reveal") void PlayText(FText NewText);
 UFUNCTION(BlueprintCallable, Category="UI|Text Reveal") void Replay();
 UFUNCTION(BlueprintCallable, Category="UI|Text Reveal") void Pause();
 UFUNCTION(BlueprintCallable, Category="UI|Text Reveal") void Resume();
 /** Reveals everything silently. One OnFinished(true); skipped characters/cues are not dispatched. */
 UFUNCTION(BlueprintCallable, Category="UI|Text Reveal") void SkipToEnd();
 /** Cancel, stop audio, and either keep everything visible or clear the presentation. No OnFinished. */
 UFUNCTION(BlueprintCallable, Category="UI|Text Reveal") void Stop(bool bShowAll = true);
 /** Deterministic, silent seek; does not emit cues or completion. Use External Clock for Sequencer scrubbing. */
 UFUNCTION(BlueprintCallable, Category="UI|Text Reveal") void SetPlaybackTime(float Seconds);
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") bool IsPlaying() const { return bPlaying; }
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") bool IsPaused() const { return bPaused; }
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") bool IsSupportedOwner() const;
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") bool IsPlaybackReady() const { return bConstructed && !bDesignTime; }
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") float GetPlaybackTime() const;
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") float GetDuration() const;
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") int32 GetCharacterCount() const;
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") int32 GetVisibleCharacterCount() const;
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") int32 GetVisualLineCount() const;
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") bool IsBlipPlaying() const;
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") int32 GetBlipsPlayed() const { return BlipsPlayed; }
 UFUNCTION(BlueprintPure, Category="UI|Text Reveal") FString GetPlaybackDebugJson() const;
 virtual TSharedRef<SWidget> RebuildWidgetWithContent(TSharedRef<SWidget> Content) override;
 virtual void BeginDestroy() override;
#if WITH_EDITOR
 virtual EDataValidationResult IsDataValidForOwner(const UWidget* OwnerWidget,FDataValidationContext& Context) const override;
#endif
protected:
 virtual void OnPreConstruct(bool bIsDesignTime) override;
 virtual void OnConstruct() override;
 virtual void OnDestruct() override;
private:
 friend class SDWTextReveal;
 friend bool DWGetGlyphPresentation(const UDWTextRevealComponent*,int32,float&,float&,FVector2D&);
 TSharedPtr<FDWTextRevealState> State;
 TWeakPtr<SDWTextReveal> View;
 FTSTicker::FDelegateHandle TickHandle;
 UPROPERTY(Transient) TObjectPtr<UAudioComponent> BlipAudio;
 bool bConstructed=false,bDesignTime=true,bPlaying=false,bPaused=false,bInsideTick=false,bHasVisiblePaint=false;
 float LastEffectivePaintAlpha=0.f;
 int32 Revision=0,NextCharacter=0,EnglishCounter=0,ChineseCounter=0,BlipsPlayed=0;
 double LastTick=0,LastPaint=0,LastBlip=-100,BlipEnd=0;
 bool bBlipFading=false;
 void EnsureTicker();
 void EndTicker();
 bool TickPlayback(float Delta);
 void SyncSource(bool bForce=false);
 void RebuildSchedule();
 void StopAudio(bool bDestroy=false);
 void PlayBlip(int32 Index);
};

UCLASS()
class GDATTEST_API UDWTextRevealLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintPure,Category="UI|Text Reveal") static UDWTextRevealComponent* GetTextRevealComponent(UWidget* Widget);
 UFUNCTION(BlueprintCallable,Category="UI|Text Reveal") static bool RevealWidgetText(UTextBlock* TextWidget,FText Text);
};
