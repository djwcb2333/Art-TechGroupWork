#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DWGameplayHUD.generated.h"

class UDWGameplayWidget;
class UDWWorldTargetIndicator;
class UDWWorldMapScreen;
class UDWWorldMapEntry;
class UTexture2D;
class UFont;
class UAudioComponent;
class USoundBase;

UENUM(BlueprintType)
enum class EDWMenuPage : uint8
{
    None, Title, SaveSlots, Inventory, Crafting, Pause, Settings, Defeat, Character, Building, LanguageJournal, WorldMap
};

/** Owns game UI state; layout is supplied by a real, editable Widget Blueprint. */
UCLASS(Blueprintable)
class GDATTEST_API ADWGameplayHUD : public AHUD
{
    GENERATED_BODY()
public:
    ADWGameplayHUD();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|UI")
    FText GameTitle = FText::FromString(TEXT("面团世界"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|UI")
    FText GameSubtitle = FText::FromString(TEXT("DOUGH WORLD"));

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|UI")
    bool bShowTitleOnStart = false;

    /** Compatibility only: interactions now show over the resource actor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|UI")
    bool bUseLegacyInteractionPrompt = false;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dough World|UI") TSubclassOf<UDWGameplayWidget> WidgetClass;
    UFUNCTION(BlueprintPure, Category="Dough World|UI") UDWGameplayWidget* GetGameplayWidget() const { return GameplayWidget; }

    /** Empty texture fields use the built-in readable placeholder artwork. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dough World|UI|Artwork") TObjectPtr<UTexture2D> HealthFrameTexture;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dough World|UI|Artwork") TObjectPtr<UTexture2D> TransformationFrameTexture;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dough World|UI|Artwork") TObjectPtr<UTexture2D> InventoryPanelTexture;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dough World|UI|Artwork") TObjectPtr<UTexture2D> CraftingPanelTexture;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dough World|UI|Artwork") TObjectPtr<UTexture2D> TitleBackgroundTexture;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dough World|UI|Artwork") TObjectPtr<UFont> GameUIFont;
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Dough World|UI|Artwork") FLinearColor UIAccentColor = FLinearColor(.88f, .64f, .28f, 1.f);

    static bool IsHandbookPage(EDWMenuPage Page);
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ToggleHandbook(EDWMenuPage Page);
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void CycleHandbook(int32 Direction);
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ToggleInventory();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ToggleCrafting();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void TogglePause();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ToggleWorldMap();
    void FinishWorldMapClose(UDWWorldMapScreen* Screen);
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowTitle();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowDefeat();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowMenu(EDWMenuPage Page);
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowToast(const FText& Text);
    /** Combat hints use the rule table duration; menu/save notices retain their original duration. */
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowCombatToast(const FText& Text);
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void Notify(const FText& Text) { ShowToast(Text); }
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ClosePanels();
    UFUNCTION(BlueprintPure, Category="Dough World|UI") bool IsBlockingGameplay() const;
    UFUNCTION(BlueprintPure, Category="Dough World|UI") EDWMenuPage GetMenuPage() const { return MenuPage; }

    FText GetToast() const;
    UFUNCTION(BlueprintCallable,Category="Dough World|UI") void NotifyItemAcquired(FName ItemId,int32 Amount);
    UFUNCTION(BlueprintPure,Category="Dough World|UI") FText GetPickupToast() const;

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Pickup",meta=(ClampMin="0",Units="s")) float C01PickupEnterSeconds=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Pickup",meta=(ClampMin="0",Units="s")) float C01PickupHoldSeconds=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Pickup",meta=(ClampMin="0",Units="s")) float C01PickupExitSeconds=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Pickup",meta=(ClampMin="0.05",Units="s")) float C01PickupBounceSeconds=.38f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Pickup",meta=(ClampMin="0.05",ClampMax="3")) float C01PickupDampingRatio=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Pickup",meta=(ClampMin="0.05",ClampMax="1")) float C01PickupEntryScale=.82f;
    UFUNCTION(BlueprintPure,Category="UI|C01|Pickup") double GetPickupToastAge()const;
    UFUNCTION(BlueprintPure,Category="UI|C01|Pickup") double GetPickupToastRemaining()const;
    UFUNCTION(BlueprintPure,Category="UI|C01|Feedback") double GetToastRemaining()const;
    void RefreshPanels();
    void ReturnFromSettings();
    void OpenSettings();
    void SetSessionStarted(bool bStarted);
    bool IsSessionStarted() const { return bSessionStarted; }
    UFUNCTION(BlueprintCallable, Category="Dough World|UI|Audio") void PlayClickSound();
    UFUNCTION(BlueprintPure, Category="Dough World|UI|Audio|Diagnostics") UAudioComponent* GetMusicPlaybackComponent() const { return MusicComponent; }
    UFUNCTION(BlueprintPure, Category="Dough World|UI|Audio|Diagnostics") USoundBase* GetCurrentMusicSound() const { return CurrentMusic; }

    /** Same MusicComponent and Music sound category. A null sound intentionally silences background music during the lease. */
    FGuid AcquireOpeningMusic(AActor* InOwner,USoundBase* Sound,float VolumeMultiplier=1.f,float PitchMultiplier=1.f);
    /** A stale token cannot release a later director. Opening worlds remain silent until leaving the map; other worlds restore their normal music. */
    bool ReleaseOpeningMusic(const FGuid& Lease);
    /** Retain the opening lease while fading its music to silence; stale tokens are rejected. */
    bool FadeOpeningMusicToSilence(const FGuid& Lease);

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    /** Frontend subclasses can defer only menu music; gameplay music keeps its existing behavior. */
    virtual bool ShouldPlayMenuMusic() const { return true; }
    /** Menu-only worlds keep their scenery simulating; gameplay menus retain pause behavior. */
    virtual bool ShouldPauseWorldForMenu(EDWMenuPage Page) const;
    void UpdateMusic();
    void StopMusic();

private:
    UPROPERTY(Transient) TObjectPtr<UDWGameplayWidget> GameplayWidget;
    UPROPERTY(Transient) TObjectPtr<UDWWorldTargetIndicator> WorldTargetIndicator;
    UPROPERTY(Transient) TObjectPtr<UDWWorldMapScreen> WorldMapScreen;
    UPROPERTY(Transient) TObjectPtr<UDWWorldMapEntry> WorldMapEntry;
    EDWMenuPage MenuPage = EDWMenuPage::None;
    EDWMenuPage SettingsReturnPage = EDWMenuPage::Title;
    bool bSessionStarted = false;
    FText Toast;
    double ToastUntil = 0;
    FText PickupToast;double PickupToastStarted=0.,PickupToastUntil=0.;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;
    UPROPERTY(Transient) TObjectPtr<USoundBase> CurrentMusic;
    TWeakObjectPtr<AActor> OpeningMusicOwner;
    FGuid OpeningMusicLease;
    UPROPERTY(Transient) TObjectPtr<USoundBase> OpeningMusicSound;
    float OpeningMusicVolumeMultiplier=1.f,OpeningMusicPitchMultiplier=1.f;
    bool bMusicEnding=false;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> CombatMusicComponent;
    bool bCombatMusicFading=false,bCombatRestartPending=false,bCombatFinishedDuringFade=false,bCombatStartBlocked=false,bCombatFailurePending=false,bBackgroundPausedForCombat=false,bBackgroundNotStarted=false;
    bool bOpeningMusicFading=false,bOpeningMusicFinishedPending=false;
    float CombatMusicGain=1.f,CombatFadeFrom=1.f,CombatFadeTo=1.f,OpeningMusicGain=1.f;
    double CombatFadeStart=0.,CombatFadeDuration=0.,CombatLastPlayStart=0.,OpeningFadeStart=0.,OpeningFadeDuration=0.;
    void TickCombatMusic();
    void TickOpeningMusicFade();
    void StopCombatMusic(bool bResumeBackground);
    void BeginCombatFade(float Target,float Seconds);
    float GetMusicFadeSeconds() const;
    UFUNCTION() void HandleCombatMusicFinished();
    bool IsOpeningMusicWorld()const;
    bool HasOpeningMusicOwner()const;
    void ResetOpeningMusicLease();
    void PlayPanelTransitionSound(EDWMenuPage Page, bool bOpening);
    UFUNCTION() void HandleMusicFinished();
};
