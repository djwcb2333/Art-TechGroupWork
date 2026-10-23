#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DWInteractionPromptWidget.generated.h"
class UDWInteractionPromptStyle;class UTextBlock;class UProgressBar;class UBorder;class USizeBox;class UDWUIBounceComponent;class UDWUIGlowPulseComponent;

/** The saved WBP provides the layout. Native code supplies a reversible, one-shot spring transition. */
UCLASS(Blueprintable)
class GDATTEST_API UDWInteractionPromptWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    /** Presentation only. Detection, action keys and progress remain in the existing component. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C01") bool bUseC01Presentation=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C01") FLinearColor C01PromptBackground=FLinearColor(.25f,.25f,.25f,.60f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C01",meta=(ClampMin="0",ClampMax="32")) float C01PromptCornerRadius=10.f;
    /** White fill is drawn over the gray surface, below text, and remains translucent. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="0",ClampMax="0.6")) float C04HarvestFillOpacity=.28f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="0",ClampMax="48")) FVector2D C04ContentPadding=FVector2D(16,6);
    /** Local C04 typography only; the shared style asset and saved WBP remain unchanged. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="8",ClampMax="64")) int32 C04TitleFontSize=26;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="8",ClampMax="64")) int32 C04ActionFontSize=18;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="8",ClampMax="64")) int32 C04KeyFontSize=18;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="0",ClampMax="16")) float C04LineGap=4.f;
    /** Minimum target, not a clipping ceiling. Real text metrics plus padding may require more. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="48",ClampMax="240")) float C04TargetPanelHeight=104.f;
    /** Correct font ink's optical center without changing centered layout or the backdrop. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="-24",ClampMax="24")) float C04ContentOpticalOffsetY=-5.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Interaction Prompt|Debug") float C04AppliedContentOpticalOffsetY=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Interaction Prompt|Debug") FVector2D C04MeasuredContentSize=FVector2D::ZeroVector;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Interaction Prompt|Debug") FVector2D C04ResolvedPanelSize=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="0.05",ClampMax="5",Units="s")) float C04CompletionBounceSeconds=.38f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="0",ClampMax="0.3")) float C04CompletionBounceAmount=.08f;
    /** Keep the completed prompt visible long enough to see the last, depleted-node bounce. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="0.05",ClampMax="5",Units="s")) float C04CompletionVisibilitySeconds=.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C04",meta=(ClampMin="0",ClampMax="1",Units="s")) float C04CompletedFillHoldSeconds=.14f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Interaction Prompt|Debug") int32 C04CompletionPulseCount=0;
    /** Completion-only halo. The three phases are editable independently of the spring. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C05 Glow") bool bC05CompletionGlow=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C05 Glow",meta=(ClampMin="0",ClampMax="5",Units="s")) float C05GlowRiseSeconds=.07f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C05 Glow",meta=(ClampMin="0",ClampMax="5",Units="s")) float C05GlowHoldSeconds=.04f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C05 Glow",meta=(ClampMin="0",ClampMax="5",Units="s")) float C05GlowFadeSeconds=.34f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C05 Glow",meta=(ClampMin="0",ClampMax="1")) float C05GlowPeakOpacity=.68f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|C05 Glow",meta=(ClampMin="0",ClampMax="96")) float C05GlowExtent=24.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Interaction Prompt|Debug") int32 C05CompletionGlowCount=0;
    UFUNCTION(BlueprintPure,Category="Interaction Prompt|Debug") UDWUIGlowPulseComponent* GetCompletionGlow()const{return C05CompletionGlow;}
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void InitializePrompt(UDWInteractionPromptStyle* InStyle);
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void SetPromptText(FText Title,FText Action,FText Key);
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void SetProgress(float Progress,bool bShowProgress);
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void SetPromptVisible(bool bVisible,bool bImmediate=false);
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void AdvancePresentation(float DeltaSeconds);
    /** Native commit notification. Progress reaching one alone never dispatches this effect. */
    void NotifyHarvestCompleted();
    float GetCompletionDisplaySeconds() const;
    FVector2D GetMinimumDrawSize() const;
    UFUNCTION(BlueprintPure,Category="Interaction Prompt") bool IsPromptDesiredVisible()const{return bDesiredVisible;}
    UFUNCTION(BlueprintPure,Category="Interaction Prompt") float GetPresentationOpacity()const{return CurrentOpacity;}
    UFUNCTION(BlueprintPure,Category="Interaction Prompt") float GetPresentationScale()const{return CurrentScale;}
    /** UI wall-clock remaining time; a long frame before the commit cannot consume a new hold. */
    UFUNCTION(BlueprintPure,Category="Interaction Prompt|Debug") float GetCompletedFillRemainingSeconds() const;
    UFUNCTION(BlueprintImplementableEvent,Category="Interaction Prompt|Events") void OnPromptAppeared();
    UFUNCTION(BlueprintImplementableEvent,Category="Interaction Prompt|Events") void OnPromptDisappeared();
    UFUNCTION(BlueprintImplementableEvent,Category="Interaction Prompt|Events") void OnProgressChanged(float Progress);
protected:
    virtual void NativeOnInitialized()override;
    virtual void NativeConstruct()override;
    virtual void NativeDestruct()override;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<USizeBox> PromptVisual;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> PromptTitle;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> ActionText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> KeyText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UBorder> Keycap;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UProgressBar> HarvestProgress;
    UPROPERTY(Transient,BlueprintReadOnly) TObjectPtr<UDWInteractionPromptStyle> Style;
private:
    bool bDesiredVisible=false;
    float Clock=0,CurrentOpacity=0,CurrentScale=.55f,StartOpacity=0,StartScale=.55f,CurrentOffset=24,StartOffset=24,LastProgress=-1;
    UPROPERTY(Transient) TObjectPtr<UBorder> C01PromptPaper;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> C04PromptBackdrop;
    UPROPERTY(Transient) TObjectPtr<UDWUIBounceComponent> C04CompletionBounce;
    UPROPERTY(Transient) TObjectPtr<UDWUIGlowPulseComponent> C05CompletionGlow;
    UPROPERTY(Transient) TObjectPtr<UWidget> C04PromptContent;
    bool bC04LayoutDirty=true;
    float C04AuthoredWidth=0.f;
    double C04CompletedFillDeadline=0.;
    bool bC04ShowProgress=false;
    void ApplyC01Artwork(bool bDuringOwnerInitialize=false);
    void MountCompletionBounce(bool bDuringOwnerInitialize);
    void ConfigureCompletionGlow();
    void RefreshGlowPaintBounds();
    void RefreshBackgroundProgress();
    void RefreshC04Layout();
    void ApplyTypography();
    void ApplyTransform(float Scale,float Offset,float Angle,float Opacity);
};
