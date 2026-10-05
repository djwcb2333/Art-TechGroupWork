#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "DWMinimapWidget.generated.h"

class UDWMinimapComponent;
class UDWMinimapNavigationSubsystem;
class UDWMinimapWidget;
class UTextBlock;

/** Designer-editable circular map surface. Geometry/projection stay native; no gameplay WBP is rebuilt. */
UCLASS(meta=(DisplayName="DW Minimap View"))
class GDATTEST_API UDWMinimapView : public UWidget
{
    GENERATED_BODY()
public:
    UDWMinimapView();
    /** Used only without a live minimap component, for the honest warm-gray Designer placeholder. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|Designer",meta=(ClampMin="160",ClampMax="400"))
    float PreviewDiameter=300.f;

    virtual void SynchronizeProperties() override;
#if WITH_EDITOR
    virtual const FText GetPaletteCategory() override;
#endif
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
};

/** Independent, non-interactive minimap presentation; does not rebuild the existing gameplay WBP. */
UCLASS()
class GDATTEST_API UDWMinimapWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void InitializeMinimap(UDWMinimapComponent* InComponent);

    UFUNCTION(BlueprintPure,Category="DoughWorld|Minimap")
    UDWMinimapComponent* GetMinimapComponent() const;

    /** Brief uniform pulse only on navigation selection/change/clear; no continuous idle animation. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|Motion")
    bool bAnimateNavigationChanges=true;

    /** Updated by the owning minimap component, without requiring UUserWidget NativeTick. */
    void UpdateNavigationFeedback(float DeltaSeconds,bool bVisible);
    virtual void SetVisibility(ESlateVisibility InVisibility) override;

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;

    /** These optional WBP controls keep their Designer-authored font, padding and color. */
    UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Minimap|Navigation",meta=(BindWidgetOptional))
    TObjectPtr<UTextBlock> NavigationInfoText;

    UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Minimap|Navigation",meta=(BindWidgetOptional))
    TObjectPtr<UTextBlock> OutsideInfoText;

private:
    UPROPERTY(Transient)
    TWeakObjectPtr<UDWMinimapComponent> MinimapComponent;

    UPROPERTY(Transient)
    TWeakObjectPtr<UDWMinimapNavigationSubsystem> NavigationSubsystem;

    UFUNCTION()
    void HandleNavigationChanged();

    void FinishNavigationFeedback();
    void UpdateNavigationText(bool bVisible);
    float NavigationPulseElapsed=.34f;
};
