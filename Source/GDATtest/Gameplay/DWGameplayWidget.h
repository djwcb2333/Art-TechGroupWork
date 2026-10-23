#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "Types/SlateEnums.h"
#include "Slate/WidgetTransform.h"
#include "DWInventoryInteraction.h"
#include "DWCombatHUDModel.h"
#include "DWC01UIStyle.h"
#include "DWGameplayWidget.generated.h"
class UDWGameplayConfig; class UDWGameInstance; class ADWPlayerCharacter; class UDWInventoryComponent;
class UButton; class UTextBlock; class UProgressBar; class UBorder; class UWidgetSwitcher; class UUniformGridPanel;
class UVerticalBox; class UCanvasPanel; class USlider; class UComboBoxString; class UCheckBox; class UImage;
class UDWSettingsPanel;
class UDWHandbookFrame;
class UDWHandbookButton;
class UDWC01HUDAnchor; class UDWUIBounceComponent; class UHorizontalBox; class USizeBox;
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
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01") bool bUseC01HUD=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01",meta=(ClampMin="160",ClampMax="400")) float C01HUDDiameter=248.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01") FVector2D C01HUDMargin=FVector2D(72,30);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Motion",meta=(ClampMin="0",Units="s")) float C01FocusSeconds=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Motion",meta=(ClampMin="0",Units="s")) float C01HoverSeconds=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Motion",meta=(ClampMin="0",ClampMax="1")) float C01SlotHoverLighten=.38f;
    /** Optical lift as a fraction of font size; only Latin button labels, independent of Bounce. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Typography",meta=(ClampMin="0",ClampMax="0.3")) float C01ButtonLatinOpticalOffsetEm=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Motion",meta=(ClampMin="0",Units="s")) float C01PressSeconds=.08f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Motion",meta=(ClampMin="0",Units="s")) float C01ResourceSeconds=.24f;
    /** Shared by the first three bag cells and the three real inventory quick slots. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Quick Slots",meta=(ClampMin="0",ClampMax="12")) float C01QuickSlotOutlineThickness=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Quick Slots",meta=(ClampMin="64",ClampMax="128")) float C01QuickSlotSize=92.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Quick Slots",meta=(ClampMin="0",ClampMax="32")) float C01QuickSlotGap=16.f;
    void StyleC01Button(UButton*,FLinearColor BaseColor,bool bSelected=false,bool bTransparentRest=false,bool bItemSlot=false);
    void RefreshC01Presentation(float DeltaSeconds);
    UFUNCTION(BlueprintPure,Category="UI|C01|Diagnostics") FVector2D GetC01CinematicOffset() const;
    void InitializeC01Display();void TickC01Buttons();void TickC01Feedback();
    void PrepareSavePageLayout();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Feedback",meta=(ClampMin="0",Units="s")) float C01FeedbackEnterSeconds=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Feedback",meta=(ClampMin="0",Units="s")) float C01FeedbackExitSeconds=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Feedback",meta=(DisplayName="Feedback Float Distance (Pixels)")) float C01FeedbackFloatDistance=12.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Feedback") FVector2D C01FeedbackCurve1=FVector2D(.25,.1);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|C01|Feedback") FVector2D C01FeedbackCurve2=FVector2D(.25,1);
    void TickC01QuickDock();bool IsC01QuickDockPointer(FVector2D ScreenPosition)const;
    UFUNCTION(BlueprintPure,Category="UI|Inventory|Quick slots") bool CanActivateQuickInventorySlot(int32 SlotIndex)const;
    UFUNCTION(BlueprintCallable,Category="UI|Inventory|Quick slots") void ActivateQuickInventorySlot(int32 SlotIndex);
    UFUNCTION(BlueprintPure,Category="UI|Inventory|Equipment") bool CanEquipInventorySlot(int32 SlotIndex)const;
    UFUNCTION(BlueprintCallable,Category="UI|Inventory|Equipment") bool EquipInventorySlot(int32 SlotIndex);
    /** Native additions preserve the saved WBP layout. Position is measured from the bottom-left. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat") bool bShowCombatInfoPanel=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat") FVector2D CombatInfoMargin=FVector2D(24.,24.);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat",meta=(ClampMin="260")) float CombatInfoWidth=480.f;
    UFUNCTION(BlueprintPure,Category="UI|Combat") FDWCombatHUDSnapshot GetCombatHUDSnapshot()const{return CombatHUD;}
    UFUNCTION(BlueprintCallable,Category="UI|Combat") void RefreshCombatHUD();
    /** Local runtime UMG cells reuse DW UI Bounce; native paint retains the read-only reload ring. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="0.05",Units="s")) float ShotgunShellBounceSeconds=.32f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="0.05",ClampMax="1")) float ShotgunShellEntryScale=.72f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="0.05",ClampMax="3")) float ShotgunShellDampingRatio=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="0",ClampMax="16")) float ShotgunShellCornerRadius=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead") bool bShowShotgunOverhead=true;
    /** Added to the current scaled capsule top, in world centimetres. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead") FVector ShotgunHeadWorldOffset=FVector(0.,0.,24.);
    /** DPI-independent screen offset; negative Y moves the row upward. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead") FVector2D ShotgunHeadScreenOffset=FVector2D(0.,-16.);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead") FVector2D ShotgunAmmoSlotSize=FVector2D(11.,17.);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="0")) float ShotgunAmmoSlotGap=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="0.5")) float ShotgunAmmoOutlineThickness=1.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="1")) float ShotgunReloadRingRadius=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="0")) float ShotgunReloadRingGap=7.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead",meta=(ClampMin="0.5")) float ShotgunReloadRingThickness=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead") FLinearColor ShotgunLoadedColor=FLinearColor(1.f,.76f,.28f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead") FLinearColor ShotgunEmptyColor=FLinearColor(.42f,.44f,.47f,.9f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead") FLinearColor ShotgunAmmoOutlineColor=FLinearColor(.03f,.025f,.02f,.9f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Combat|Shotgun Overhead") FLinearColor ShotgunReloadRingColor=FLinearColor(1.f,.88f,.54f,1.f);
    UFUNCTION(BlueprintCallable,Category="UI|Combat") void InitializeCombatSettings();
    UFUNCTION(BlueprintCallable,Category="UI|Handbook") void HandleHandbookAction(int32 Action);
    void RegisterHandbookLabel(UTextBlock* Label,const TCHAR* Chinese,const TCHAR* English);
    void InitializeHandbook();void RefreshHandbook();void RefreshHandbookRecipes();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Handbook",meta=(ClampMin="0.5",ClampMax="1")) float HandbookScreenFraction=.86f;
    UFUNCTION(BlueprintPure,Category="UI|Handbook") bool HasHandbook() const {return HandbookPageIndex!=INDEX_NONE;}
    /** Fixed for players; editors can change combinations in WBP class defaults. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Inventory Controls") FDWInventoryControls InventoryControls;
    UFUNCTION(BlueprintPure,Category="Inventory UI") FText GetInventoryControlHelp() const;
    UFUNCTION(BlueprintPure,Category="Inventory UI") bool IsInventoryDiscardPending() const { return bInventoryDiscardPending; }
    UFUNCTION(BlueprintCallable,Category="Inventory UI") bool RequestDiscardSelectedInventory();
    UFUNCTION(BlueprintCallable,Category="Inventory UI") void ConfirmInventoryDiscard();
    UFUNCTION(BlueprintCallable,Category="Inventory UI") void CancelInventoryDiscard();
    bool HandleInventorySlotPointer(int32 SlotIndex,const FPointerEvent& Event,bool& bStartDrag);
    UDragDropOperation* CreateInventoryDrag(const FDWInventorySlotSnapshot& Snapshot,UDWInventorySlotWidget* Source);
    bool HandleInventorySlotDrop(int32 SlotIndex,UDragDropOperation* Operation);

    /** Shared styling also applies to future sliders discovered in nested widgets. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Sliders",meta=(ClampMin="1",ClampMax="24")) float SliderTrackThickness=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="UI|Sliders",meta=(ClampMin="12",ClampMax="48")) float SliderThumbSize=22.f;
    void ApplySliderPresentation(UUserWidget* Target);
    UFUNCTION(BlueprintCallable,Category="UI|Settings") void SelectSettingsBookTab(int32 Tab);
    UFUNCTION(BlueprintPure,Category="UI|Settings") int32 GetSettingsBookTab() const{return SettingsBookTab;}
    void InitializeSettingsBook();
    void RefreshSettingsBook();
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
    virtual bool NativeOnDrop(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation) override;

    virtual int32 NativePaint(const FPaintArgs&,const FGeometry&,const FSlateRect&,FSlateWindowElementList&,int32,const FWidgetStyle&,bool)const override;
    virtual void NativeOnInitialized() override;
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
    UPROPERTY(Transient) TObjectPtr<UDWC01HUDAnchor> C01HUDAnchor;
    UPROPERTY(Transient) TObjectPtr<UBorder> C01QuickDock;
    UPROPERTY(Transient) TObjectPtr<UBorder> C01PickupPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> C01PickupLabel;
    UPROPERTY(Transient) TObjectPtr<UDWUIBounceComponent> C01PickupBounce;
    UPROPERTY(Transient) TObjectPtr<UBorder> C01ShellDock;
    UPROPERTY(Transient) TObjectPtr<UHorizontalBox> C01ShellRow;
    UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> C01ShellCells;
    UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> C01ShellSizes;
    UPROPERTY(Transient) TArray<TObjectPtr<UDWUIBounceComponent>> C01ShellBounces;
    TWeakObjectPtr<ADWPlayerCharacter> C01ShellPlayer;
    uint64 C01ShellRestoreEpoch=0;
    int32 C01PreviousLoaded=INDEX_NONE;
    double C01PreviousPickupAge=-1.;
    FText C01PreviousPickupText;
    void TickC01RuntimeMeters();
    UPROPERTY(Transient) TArray<TObjectPtr<UDWHandbookButton>> C01QuickButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> C01QuickIcons;
    UPROPERTY(Transient) TArray<TObjectPtr<UImage>> C01QuickOutlines;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> C01QuickNames;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> C01QuickQuantities;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> C01QuickKeys;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> HandbookEquipmentList;
    UPROPERTY(Transient) TObjectPtr<UButton> HandbookRelatedRecipeButton;
    UPROPERTY(Transient) TObjectPtr<UButton> HandbookCraftMoreButton;
    TMap<TWeakObjectPtr<UButton>,FDWC01ButtonVisual> C01Buttons;
    FVector4f C01VisualResources=FVector4f(0,0,0,0);
    bool bC01VisualReady=false;TWeakObjectPtr<ADWPlayerCharacter> C01VisualPlayer;
    UPROPERTY(Transient) FDWCombatHUDSnapshot CombatHUD;
    FDWCombatHUDConfig CombatHUDConfig;
    const FDWCombatRules* CombatHUDRules=nullptr; // Immutable GI-owned rules; refreshed on owner/rules change.
    UPROPERTY(Transient) TObjectPtr<UCheckBox> CombatEnemyHealthCheck;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CombatEnemyHealthLabel;
    void RefreshCombatSettings();
    UFUNCTION() void ChangeCombatEnemyHealth(bool Checked);
    int32 SettingsBookTab=0;
    UFUNCTION() void BookGraphicsTab();
    UFUNCTION() void BookAudioTab();
    UFUNCTION() void BookKeysTab();
    UFUNCTION() void BookControllerTab();
    UFUNCTION() void BookGeneralTab();
    UPROPERTY(Transient,meta=(BindWidgetOptional)) TObjectPtr<UBorder> InventoryDiscardModal;
    UPROPERTY(Transient,meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> InventoryDiscardMessage;
    UPROPERTY(Transient) TWeakObjectPtr<UDWInventoryComponent> PendingDiscardInventory;
    FDWInventorySlotSnapshot PendingDiscardSnapshot;
    bool bInventoryDiscardPending=false;
    bool bInventoryUIInitialized=false;
    bool IsInventoryInteractionPage() const;
    bool HandleInventoryKey(const FKeyEvent& Event);
    bool RequestInventoryDiscard(const FDWInventorySlotSnapshot& Snapshot);
    void ConfigureInventoryDiscardPopup();
    bool PositionInventoryDiscardPopup(const FVector2D& ScreenPosition);
    bool HandleInventoryDiscardPointer(const FPointerEvent& Event);
    bool RequestInventoryDiscardAtScreenPosition(const FDWInventorySlotSnapshot& Snapshot,const FVector2D& ScreenPosition);
    bool IsSettingsBookNavigationHit(FVector2D Position) const;
    void EnsureInventoryInteractionUI();
    void RefreshInventoryInteractionUI();

    bool FindTextOverride(const UTextBlock* Text,EDWGameLanguage Language,FText& OutText) const;
    void ApplyDesignerTextPreview();
    TMap<TWeakObjectPtr<UTextBlock>,TPair<FString,FString>> HandbookLabels;
    int32 HandbookPageIndex=INDEX_NONE;int32 HandbookRecipeIndex=0;
    int64 HandbookInventoryRevision=-1;int32 HandbookLanguageRevision=-1;
    UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> HandbookPages;
    UPROPERTY(Transient) TObjectPtr<UVerticalBox> HandbookRecipeList;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HandbookRecipeDetails;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HandbookStats;
    UPROPERTY(Transient) TObjectPtr<UDWHandbookFrame> HandbookFrame;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> HandbookSelection;
    UPROPERTY(Transient) TObjectPtr<UImage> HandbookItemImage;
    UPROPERTY(Transient) TObjectPtr<UImage> HandbookRecipeImage;
    UPROPERTY(Transient) TObjectPtr<UButton> HandbookCraftButton;
    UPROPERTY(Transient) TArray<TObjectPtr<UButton>> HandbookTabButtons;
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
    FText C01LastFeedbackText;
    double C01FeedbackStarted=0.,C01FeedbackPreviousRemaining=0.;
    bool bC01FeedbackBaselineCaptured=false;
    FWidgetTransform C01FeedbackBaseTransform;
    float C01FeedbackBaseOpacity=1.f;
    uint32 InventoryHash=0;
    float SliderStyleRefreshSeconds=0.f;
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
