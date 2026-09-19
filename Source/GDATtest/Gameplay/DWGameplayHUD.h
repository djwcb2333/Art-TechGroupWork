#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "DWGameplayHUD.generated.h"

class UDWGameplayWidget;
class UTexture2D;
class UFont;
class UAudioComponent;
class USoundBase;

UENUM(BlueprintType)
enum class EDWMenuPage : uint8
{
    None, Title, SaveSlots, Inventory, Crafting, Pause, Settings, Defeat
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

    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ToggleInventory();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ToggleCrafting();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void TogglePause();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowTitle();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowDefeat();
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowMenu(EDWMenuPage Page);
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ShowToast(const FText& Text);
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void Notify(const FText& Text) { ShowToast(Text); }
    UFUNCTION(BlueprintCallable, Category="Dough World|UI") void ClosePanels();
    UFUNCTION(BlueprintPure, Category="Dough World|UI") bool IsBlockingGameplay() const;
    UFUNCTION(BlueprintPure, Category="Dough World|UI") EDWMenuPage GetMenuPage() const { return MenuPage; }

    FText GetToast() const;
    void RefreshPanels();
    void ReturnFromSettings();
    void OpenSettings();
    void SetSessionStarted(bool bStarted);
    bool IsSessionStarted() const { return bSessionStarted; }
    UFUNCTION(BlueprintCallable, Category="Dough World|UI|Audio") void PlayClickSound();
    UFUNCTION(BlueprintPure, Category="Dough World|UI|Audio|Diagnostics") UAudioComponent* GetMusicPlaybackComponent() const { return MusicComponent; }
    UFUNCTION(BlueprintPure, Category="Dough World|UI|Audio|Diagnostics") USoundBase* GetCurrentMusicSound() const { return CurrentMusic; }

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
    EDWMenuPage MenuPage = EDWMenuPage::None;
    EDWMenuPage SettingsReturnPage = EDWMenuPage::Title;
    bool bSessionStarted = false;
    FText Toast;
    double ToastUntil = 0;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;
    UPROPERTY(Transient) TObjectPtr<USoundBase> CurrentMusic;
    void PlayPanelTransitionSound(EDWMenuPage Page, bool bOpening);
    UFUNCTION() void HandleMusicFinished();
};
