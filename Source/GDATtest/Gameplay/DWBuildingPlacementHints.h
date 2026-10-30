#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Styling/SlateBrush.h"
#include "DWBuildingPlacementHints.generated.h"
class UDWGameplayWidget;
class UDWInputGlyphWidget;
class UDWUIOffscreenComponent;
class UFont;
class UTexture2D;
class SBox;
class SBorder;
class SImage;
class STextBlock;

/** Read-only build-mode legend. Never binds keys, captures mouse, or commits placement.
 * Runtime child of existing HUDLayer; no saved WBP is rebuilt or reparented. */
UCLASS(BlueprintType)
class GDATTEST_API UDWBuildingPlacementHints : public UWidget
{
 GENERATED_BODY()
public:
 UDWBuildingPlacementHints(const FObjectInitializer& ObjectInitializer);
 /** Right edge follows the minimap inset; narrow maps let the legend extend left. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Layout",meta=(ClampMin="248",ClampMax="400",UIMin="248",UIMax="400")) float MinimumPanelWidth=248.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Layout") FVector2D KeycapSize=FVector2D(48.f,48.f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Layout",meta=(ClampMin="0")) float KeyTextGap=12.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Layout",meta=(ClampMin="0")) float RowGap=10.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Layout",meta=(ClampMin="0")) float MinimapGap=24.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Typography",meta=(ClampMin="12")) int32 FunctionFontSize=22;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Typography",meta=(ClampMin="12")) int32 KeyFontSize=24;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Typography",meta=(ClampMin="0",ClampMax="1")) float FunctionShadowOpacity=.32f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Icons") TSoftObjectPtr<UTexture2D> MouseLeftIcon;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Icons") TSoftObjectPtr<UTexture2D> MouseRightIcon;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Motion",meta=(ClampMin="0.01",Units="s")) float EnterSeconds=.18f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Motion",meta=(ClampMin="0.01",Units="s")) float ExitSeconds=.12f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Hints|Motion",meta=(ClampMin="1")) float SlideDistance=18.f;
 /** Existing screen tick calls this; all authority stays on BuildingPlacement. */
 void RefreshPresentation(UDWGameplayWidget* Screen);
 UFUNCTION(BlueprintPure,Category="Building Hints|Diagnostics") bool IsRequestedVisible()const{return bRequestedVisible;}
 UFUNCTION(BlueprintPure,Category="Building Hints|Diagnostics") UDWUIOffscreenComponent* GetPresentationMotion()const{return Motion;}
 virtual void ReleaseSlateResources(bool bReleaseChildren)override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget()override;
private:
 UPROPERTY(Transient) TObjectPtr<UDWUIOffscreenComponent> Motion;
 FSlateBrush KeycapBrush;
 FSlateBrush MouseBrushes[2];
 UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> LoadedMouseIcons;
 TSharedPtr<SBox> RootBox;
 TSharedPtr<SBox> RowBoxes[4];
 TSharedPtr<SBorder> Keycaps[4];
 TSharedPtr<STextBlock> KeyLabels[4];
 TSharedPtr<STextBlock> FunctionLabels[4];
 UPROPERTY(Transient) TArray<TObjectPtr<UDWInputGlyphWidget>> InputGlyphs;
 bool bRequestedVisible=false,bHasShown=false,bLastEnglish=false,bLastValid=false,bStyleInitialized=false;
 float EffectivePanelWidth=248.f;
 TWeakObjectPtr<UFont> LastFont;
};
