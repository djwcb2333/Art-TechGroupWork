#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DWInteractionPromptWidget.generated.h"
class UDWInteractionPromptStyle;class UTextBlock;class UProgressBar;class UBorder;class USizeBox;

/** The saved WBP provides the layout. Native code supplies a reversible, one-shot spring transition. */
UCLASS(Blueprintable)
class GDATTEST_API UDWInteractionPromptWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void InitializePrompt(UDWInteractionPromptStyle* InStyle);
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void SetPromptText(FText Title,FText Action,FText Key);
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void SetProgress(float Progress,bool bShowProgress);
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void SetPromptVisible(bool bVisible,bool bImmediate=false);
    UFUNCTION(BlueprintCallable,Category="Interaction Prompt") void AdvancePresentation(float DeltaSeconds);
    UFUNCTION(BlueprintPure,Category="Interaction Prompt") bool IsPromptDesiredVisible()const{return bDesiredVisible;}
    UFUNCTION(BlueprintPure,Category="Interaction Prompt") float GetPresentationOpacity()const{return CurrentOpacity;}
    UFUNCTION(BlueprintPure,Category="Interaction Prompt") float GetPresentationScale()const{return CurrentScale;}
    UFUNCTION(BlueprintImplementableEvent,Category="Interaction Prompt|Events") void OnPromptAppeared();
    UFUNCTION(BlueprintImplementableEvent,Category="Interaction Prompt|Events") void OnPromptDisappeared();
    UFUNCTION(BlueprintImplementableEvent,Category="Interaction Prompt|Events") void OnProgressChanged(float Progress);
protected:
    virtual void NativeConstruct()override;
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
    void ApplyTypography();
    void ApplyTransform(float Scale,float Offset,float Angle,float Opacity);
};
