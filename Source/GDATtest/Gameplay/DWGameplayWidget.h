#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "Types/SlateEnums.h"
#include "Slate/WidgetTransform.h"
#include "DWGameplayWidget.generated.h"
class UDWGameplayConfig; class UDWGameInstance; class ADWPlayerCharacter; class UDWInventoryComponent;
class UButton; class UTextBlock; class UProgressBar; class UBorder; class UWidgetSwitcher; class UUniformGridPanel;
class UVerticalBox; class UCanvasPanel; class USlider; class UComboBoxString; class UCheckBox; class UImage;
class UDWSettingsPanel;
class UDWProgressRing; class UDWInventorySlotWidget; class UDWRecipeEntryWidget; class UDWSaveSlotWidget;

/** Static label copy authored in a Widget Blueprint; does not replace live gameplay values. */
USTRUCT(BlueprintType)
struct FDWWidgetTextOverride
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Text") FName WidgetName;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Text",meta=(MultiLine="true")) FText Chinese;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Text",meta=(MultiLine="true")) FText English;
};

/** Data/button behavior and configurable page entrances. All layout lives in the authored WBP WidgetTree. */
UCLASS(Blueprintable)
class GDATTEST_API UDWGameplayWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    /** Static TextBlock labels in this WBP only. Match the Hierarchy widget name, e.g. TitleText.
     * These bilingual values take priority over the legacy title and translation catalog.
     * Dynamic health/inventory/input labels are still supplied by their gameplay systems.
     */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Language",meta=(TitleProperty="WidgetName")) TArray<FDWWidgetTextOverride> TextOverrides;
    /** Preview only: never changes the player's saved language. Compile after editing to refresh the Designer. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Language") bool bPreviewLanguageInDesigner=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Language",meta=(EditCondition="bPreviewLanguageInDesigner")) EDWGameLanguage DesignerPreviewLanguage=EDWGameLanguage::English;
    /** Reapply static text after a Blueprint changes Text Overrides at runtime. */
    UFUNCTION(BlueprintCallable,Category="UI|Language") void RefreshTextPresentation();
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="UI|Templates") TSubclassOf<UDWInventorySlotWidget> InventorySlotClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="UI|Templates") TSubclassOf<UDWRecipeEntryWidget> RecipeEntryClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="UI|Templates") TSubclassOf<UDWSaveSlotWidget> SaveSlotClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Layout",meta=(ClampMin="1")) int32 InventoryColumns=6;
    /** Turn off only when replacing the default button graph in the Widget Blueprint. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="UI|Flow") bool bBindDefaultButtonActions=true;
    /** Automatically animate real page changes; list refreshes and same-page calls do not restart it. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Page Transitions") bool bAnimatePageTransitions=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Page Transitions",meta=(ClampMin="0",Units="s")) float PageEnterSeconds=.32f;
    /** Relative multiplier applied to the Designer-authored scale. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Page Transitions",meta=(ClampMin="0.01",ClampMax="2")) float PageEnterStartScale=.94f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Page Transitions") float PageEnterOffsetY=18.f;
    /** Peak relative scale before settling back to the original appearance. 1 means no overshoot. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Page Transitions",meta=(ClampMin="1",ClampMax="1.25")) float PageEnterOvershoot=1.02f;
    /** Explicit replay, restoring any interrupted entrance before capturing the current authored appearance. */
    UFUNCTION(BlueprintCallable,Category="UI|Page Transitions") void PlayCurrentPageEntrance();
    /** Stop and restore the exact opacity and RenderTransform captured before the entrance. */
    UFUNCTION(BlueprintCallable,Category="UI|Page Transitions") void FinishPageTransition();
    UFUNCTION(BlueprintCallable,Category="UI") void InitializeScreen(ADWGameplayHUD* InHUD);
    UFUNCTION(BlueprintCallable,Category="UI") void RefreshFromHUD(bool bForceLists=false);
    UFUNCTION(BlueprintCallable,Category="UI") void ApplyMenuPage(EDWMenuPage Page);
    UFUNCTION(BlueprintPure,Category="UI") ADWPlayerCharacter* GetPlayer() const;
    UFUNCTION(BlueprintPure,Category="UI") UDWGameInstance* GetDWGameInstance() const;
    UFUNCTION(BlueprintPure,Category="UI") UDWGameplayConfig* GetGameplayConfig() const;
    UFUNCTION(BlueprintPure,Category="UI") UDWInventoryComponent* GetInventory() const;
    UFUNCTION(BlueprintPure,Category="UI") ADWGameplayHUD* GetDWHUD() const {return HUD;}
    UFUNCTION(BlueprintCallable,Category="Inventory UI") void SelectInventorySlot(int32 Index);
    UFUNCTION(BlueprintCallable,Category="Crafting UI") void CraftRecipe(FName RecipeId);
    UFUNCTION(BlueprintCallable,Category="Save UI") void LoadSlot(int32 SlotIndex);
    UFUNCTION(BlueprintCallable,Category="Save UI") void NewSlot(int32 SlotIndex);
    UFUNCTION(BlueprintCallable,Category="Save UI") void DeleteSlot(int32 SlotIndex);
    UFUNCTION(BlueprintCallable,Category="UI") void PlayClick();
    /** Retains each Designer-authored label as the translation source and keeps its font size. */
    UFUNCTION(BlueprintCallable,Category="UI|Presentation") void ApplyWidgetPresentation(UUserWidget* Target);
    UFUNCTION(BlueprintImplementableEvent,Category="UI|Events") void OnLanguageChanged(EDWGameLanguage Language);
    UFUNCTION(BlueprintImplementableEvent,Category="UI|Events") void OnMenuPageChanged(EDWMenuPage Page);
    UFUNCTION(BlueprintImplementableEvent,Category="UI|Events") void OnHUDValuesChanged(float HealthFraction,float TransformationFraction,bool bYeast,float SprintProgress);
    UFUNCTION(BlueprintImplementableEvent,Category="UI|Events") void OnInventorySelectionChanged(int32 SlotIndex,FName ItemId);
    UFUNCTION(BlueprintImplementableEvent,Category="UI|Events") void OnItemUseResult(FName ItemId,bool bSuccess);
    UFUNCTION(BlueprintImplementableEvent,Category="UI|Events") void OnRecipeCraftResult(FName RecipeId,bool bSuccess);
    UFUNCTION(BlueprintImplementableEvent,Category="UI|Events") void OnUIAction(FName Action);
protected:
    virtual void NativePreConstruct() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry,float DeltaSeconds) override;
    /** Frontend widgets can exclude their title page when a dedicated UMG timeline already owns it. */
    virtual bool ShouldAnimatePageEntrance(EDWMenuPage Page) const;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UCanvasPanel> HUDLayer;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UBorder> MenuRoot;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UWidgetSwitcher> PageSwitcher;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UProgressBar> HealthBar;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UProgressBar> TransformationBar;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UDWProgressRing> SprintRing;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> HealthValueText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> TransformationValueText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> FormText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> RingValueText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> AlcoholCountText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> InteractionText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UBorder> InteractionContainer;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> ToastText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UBorder> ToastContainer;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> TitleText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> SubtitleText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UImage> TitleBackgroundImage;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UBorder> InventoryCard;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UBorder> CraftingCard;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> StartButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> TitleSettingsButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> QuitButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UVerticalBox> SaveSlotList;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> SlotsBackButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UUniformGridPanel> InventoryGrid;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> InventoryDetailsText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> InventoryCapacityText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> InventoryUseButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> InventoryCloseButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UUniformGridPanel> CraftingInventoryGrid;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> CraftingDetailsText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> CraftingUseButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> CraftingStatusText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UVerticalBox> RecipeList;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> CraftingCloseButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UDWSettingsPanel> ExtendedSettings;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> HUDPauseButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> PauseQuitButton;
    UFUNCTION() void ClickPause(); UFUNCTION() void ClickPauseQuit();
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> ContinueButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> SaveButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> PauseSettingsButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> SaveAndTitleButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> NoSaveTitleButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> DeathLoadButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> DeathTitleButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<USlider> VolumeSlider;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> VolumeValueText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UComboBoxString> ResolutionCombo;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UComboBoxString> WindowModeCombo;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UComboBoxString> QualityCombo;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UComboBoxString> LanguageCombo;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UCheckBox> VSyncCheck;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> ApplySettingsButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> SettingsBackButton;
    UPROPERTY(Transient,BlueprintReadOnly,Category="UI") TObjectPtr<ADWGameplayHUD> HUD;
    UPROPERTY(BlueprintReadOnly,Category="UI") int32 SelectedSlot=INDEX_NONE;
private:
    bool FindTextOverride(const UTextBlock* Text,EDWGameLanguage Language,FText& OutText) const;
    void ApplyDesignerTextPreview();
    EDWMenuPage CurrentPage=EDWMenuPage::None;
    UPROPERTY(Transient) TObjectPtr<UWidget> PageTransitionWidget;
    FWidgetTransform PageTransitionBaseTransform;
    float PageTransitionBaseOpacity=1.f;
    double PageTransitionStartedAt=0.;
    float PageTransitionDuration=0.f;
    float PageTransitionStartScale=1.f;
    float PageTransitionOffsetY=0.f;
    float PageTransitionOvershoot=1.f;
    void TickPageTransition(double Now);
    uint32 InventoryHash=0;
    float MasterVolume=1.f;
    int32 LanguageRevision=INDEX_NONE;
    bool bUpdatingLanguageOptions=false;
    bool bUpdatingSettingsControls=false;
    TMap<TWeakObjectPtr<UTextBlock>,FText> AuthoredLabels;
    void RefreshInventory(); void RefreshRecipes(); void RefreshSaves(); void RefreshSelection(); void LoadSettings();
    void RefreshSettingsLabels();
    void SetToast(const FString& Message); void SaveError(const FString& Fallback); void ApplyArtwork();
    UFUNCTION() void ClickStart(); UFUNCTION() void ClickSettings(); UFUNCTION() void ClickQuit(); UFUNCTION() void ClickSlotsBack();
    UFUNCTION() void ClickUse(); UFUNCTION() void ClickClose(); UFUNCTION() void ClickSave(); UFUNCTION() void ClickSaveTitle();
    UFUNCTION() void ClickNoSaveTitle(); UFUNCTION() void ClickDeathLoad(); UFUNCTION() void ClickApplySettings(); UFUNCTION() void ClickSettingsBack();
    UFUNCTION() void ChangeVolume(float Value);
    UFUNCTION() void ChangeLanguage(FString SelectedItem,ESelectInfo::Type SelectionType);
    UFUNCTION() void ChangeSettingsOption(FString SelectedItem,ESelectInfo::Type SelectionType);
    UFUNCTION() void ChangeVSync(bool bChecked);
    UFUNCTION() void BeginSettingsInteraction();
    UFUNCTION() UWidget* GenerateComboOption(FString Option);
};
