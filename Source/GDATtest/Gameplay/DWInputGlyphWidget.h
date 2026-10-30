#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "InputCoreTypes.h"
#include "Styling/SlateBrush.h"
#include "DWInputGlyphWidget.generated.h"
class UFont;class UTexture2D;class SBox;class SBorder;class SImage;class STextBlock;class SHorizontalBox;
/** Diagrams are presentation only, not applied bindings. */
UENUM(BlueprintType)
enum class EDWInputGlyphDiagram:uint8 {None,Mouse,MouseMove,MouseDrag,WASD};
/** Display only. Owner supplies the APPLIED FKey; no polling/input/gameplay/tree rebuilds. */
UCLASS(BlueprintType)
class GDATTEST_API UDWInputGlyphWidget:public UWidget
{
 GENERATED_BODY()
public:
 UDWInputGlyphWidget(const FObjectInitializer& ObjectInitializer);
 UFUNCTION(BlueprintCallable,Category="Input Glyph") void SetInputKey(FKey Key,FText FallbackLabel);
 UFUNCTION(BlueprintCallable,Category="Input Glyph") void SetTextFallback(FText Label);
 UFUNCTION(BlueprintCallable,Category="Input Glyph") void SetDiagram(EDWInputGlyphDiagram InDiagram);
 UFUNCTION(BlueprintCallable,Category="Input Glyph") void SetPresentationStyle(UFont* ExistingFont,bool bInEnglish,FVector2D Size,int32 FontSize,bool bDrawKeycap=true);
 /** Preserve existing authored texture overrides. Null uses the standard mapping. */
 UFUNCTION(BlueprintCallable,Category="Input Glyph") void SetTextureOverride(UTexture2D* Texture);
 /** Explicit retry after import; missing assets are not loaded every tick. */
 UFUNCTION(BlueprintCallable,Category="Input Glyph") void RefreshGlyphAssets();
 UFUNCTION(BlueprintPure,Category="Input Glyph") FKey GetPresentedKey()const{return PresentedKey;}
 UFUNCTION(BlueprintPure,Category="Input Glyph") bool IsShowingGraphic()const{return bShowingGraphic;}
 virtual void ReleaseSlateResources(bool bReleaseChildren)override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget()override;
private:
 UPROPERTY(Transient) FKey PresentedKey;
 UPROPERTY(Transient) FText ExplicitLabel;
 UPROPERTY(Transient) TObjectPtr<UFont> PresentationFont;
 UPROPERTY(Transient) TObjectPtr<UTexture2D> TextureOverride;
 // Reflected ownership protects resource pointers held by the persistent brush members.
 UPROPERTY(Transient) TMap<FName,TObjectPtr<UTexture2D>> LoadedTextures;
 FSlateBrush CapBrush,IconBrushes[2];
 TSharedPtr<SBox> RootBox;
 TSharedPtr<SBorder> Cap;
 TSharedPtr<SImage> Images[2];
 TSharedPtr<SBox> IconSlots[2];
 TSharedPtr<SHorizontalBox> ImageRow;
 TSharedPtr<STextBlock> Label;
 EDWInputGlyphDiagram Diagram=EDWInputGlyphDiagram::None;
 FVector2D DisplaySize=FVector2D(48,48);
 int32 LabelSize=24;
 bool bEnglish=true,bDrawCap=true,bTextOnly=false,bShowingGraphic=false;
 UTexture2D* ResolveTexture(FName AssetName);
 void UpdatePresentation();
};
