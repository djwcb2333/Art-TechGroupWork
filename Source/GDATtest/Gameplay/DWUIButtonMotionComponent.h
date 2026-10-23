#pragma once

// User CSS revision: 2026-10-23.
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWUIButtonMotionComponent.generated.h"

class UButton;
class UWidget;
class SDWUIButtonMotionBox;

/** Base for two independently mountable effects. Never writes the owner's transform,
 * opacity, brush or OnClicked. The separate Slate wrapper composes with existing effects.
 */
UCLASS(Abstract, BlueprintType, EditInlineNew, DefaultToInstanced)
class GDATTEST_API UDWUIButtonMotionComponent : public UUIComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Button Motion|Binding") bool bAutoBindButtons=true;
    /** Non-button hover is opt-in. No pointer button event is captured by the wrapper. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Button Motion|Binding") bool bAnimateNonButtonHover=false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Button Motion|Timing") FVector2D CurveControl1=FVector2D(.25,.1);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Button Motion|Timing") FVector2D CurveControl2=FVector2D(.25,1.);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Button Motion|Timing") FVector2D Pivot=FVector2D(.5,.5);

    UFUNCTION(BlueprintCallable, Category="UI|Button Motion") void SetHovered(bool bHovered);
    UFUNCTION(BlueprintCallable, Category="UI|Button Motion") void SetPressed(bool bPressed);
    UFUNCTION(BlueprintCallable, Category="UI|Button Motion") void RestoreImmediately();
    UFUNCTION(BlueprintPure, Category="UI|Button Motion") float GetCurrentProgress() const {return Current;}
    UFUNCTION(BlueprintPure, Category="UI|Button Motion") float GetCurrentScale() const;
    UFUNCTION(BlueprintPure, Category="UI|Button Motion") FVector2D GetCurrentOffset() const;
    UFUNCTION(BlueprintPure, Category="UI|Button Motion") bool IsAnimating() const {return bTransitioning;}
    UFUNCTION(BlueprintPure, Category="UI|Button Motion") bool IsTickerActive() const {return Handle.IsValid();}

    virtual TSharedRef<SWidget> RebuildWidgetWithContent(TSharedRef<SWidget> Content) override;
    virtual void BeginDestroy() override;

    /** Presentation-only values consumed by the wrapper. Blur uses an approximation. */
    virtual void GetPresentation(float Alpha,float& Scale,FVector2D& Offset,FVector2D& ShadowOffset,
        float& ShadowBlur,float& CornerRadius,FLinearColor& ShadowColor,int32& ShadowLayers) const;
protected:
    virtual void OnPreConstruct(bool bIsDesignTime) override;
    virtual void OnConstruct() override;
    virtual void OnDestruct() override;
    virtual bool WantsActive() const {return bHoveredState;}
    virtual float DurationFor(bool bActive) const {return .25f;}
    bool bHoveredState=false;
    bool bPressedState=false;
private:
    TWeakPtr<SDWUIButtonMotionBox> Wrapper;
    TWeakObjectPtr<UButton> BoundButton;
    FTSTicker::FDelegateHandle Handle;
    bool bConstructed=false;
    bool bDesignTime=true;
    bool bInsideTick=false;
    bool bTransitioning=false;
    float Current=0.f,Start=0.f,Target=0.f,Duration=.25f;
    double StartedAt=0.;
    bool CanAnimate() const;
    bool CanInteract() const;
    void BindButton();void UnbindButton();
    void Retarget();void Sample(double Now);void Apply();
    void EnsureTicker();void StopTicker();bool TickMotion(float UnusedDelta);
    UFUNCTION() void HandleHovered();UFUNCTION() void HandleUnhovered();
    UFUNCTION() void HandlePressed();UFUNCTION() void HandleReleased();
};

/** CSS .lift: .25s ease; hover translateY(-8px), shadow 4/8/.10 -> 16/32/.18. */
UCLASS(BlueprintType,Blueprintable,EditInlineNew,DefaultToInstanced,meta=(DisplayName="DW UIButton Lift"))
class GDATTEST_API UDWUIButtonLiftComponent : public UDWUIButtonMotionComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Motion",meta=(ClampMin="0",Units="s")) float HoverSeconds=.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Motion",meta=(ClampMin="0",Units="s")) float ReturnSeconds=.25f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Motion",meta=(DisplayName="Lift Distance (Pixels)")) float LiftDistance=8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow") FVector2D RestShadowOffset=FVector2D(0,4);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow") FVector2D HoverShadowOffset=FVector2D(0,16);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow",meta=(ClampMin="0")) float RestShadowBlur=8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow",meta=(ClampMin="0")) float HoverShadowBlur=32.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow",meta=(ClampMin="0",ClampMax="1")) float RestShadowOpacity=.10f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow",meta=(ClampMin="0",ClampMax="1")) float HoverShadowOpacity=.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow") FLinearColor ShadowColor=FLinearColor::Black;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow",meta=(ClampMin="0")) float CornerRadius=12.f;
    /** Layered Gaussian approximation, not browser-identical CSS convolution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lift|Shadow",meta=(ClampMin="4",ClampMax="24")) int32 SoftShadowLayers=12;
    virtual void GetPresentation(float Alpha,float& Scale,FVector2D& Offset,FVector2D& ShadowOffset,
        float& ShadowBlur,float& Radius,FLinearColor& Color,int32& Layers) const override;
protected:
    virtual float DurationFor(bool bActive) const override {return bActive?HoverSeconds:ReturnSeconds;}
};

/** CSS .press: .1s ease; scale(.92) translateY(4px), hard shadow y6 -> y2.
 * CSS transform order gives final Y=.92*4=3.68px; shadow is inside the same transform.
 */
UCLASS(BlueprintType,Blueprintable,EditInlineNew,DefaultToInstanced,meta=(DisplayName="DW UIButton Press"))
class GDATTEST_API UDWUIButtonPressComponent : public UDWUIButtonMotionComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Press|Motion",meta=(ClampMin="0",Units="s")) float PressSeconds=.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Press|Motion",meta=(ClampMin="0",Units="s")) float ReleaseSeconds=.1f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Press|Motion",meta=(ClampMin="0.05",ClampMax="3")) float PressScale=.92f;
    /** CSS local translate applied before scaling; final screen offset is scale*translate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Press|Motion") FVector2D PressTranslate=FVector2D(0,4);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Press|Shadow") FVector2D RestShadowOffset=FVector2D(0,6);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Press|Shadow") FVector2D PressShadowOffset=FVector2D(0,2);
    /** Independent editable color. Purple CSS example does not override the C01 palette. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Press|Shadow") FLinearColor ShadowColor=FLinearColor(.18f,.085f,.04f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Press|Shadow",meta=(ClampMin="0")) float CornerRadius=10.f;
    virtual void GetPresentation(float Alpha,float& Scale,FVector2D& Offset,FVector2D& ShadowOffset,
        float& ShadowBlur,float& Radius,FLinearColor& Color,int32& Layers) const override;
protected:
    virtual bool WantsActive() const override {return bPressedState;}
    virtual float DurationFor(bool bActive) const override {return bActive?PressSeconds:ReleaseSeconds;}
};

UCLASS()
class GDATTEST_API UDWUIButtonMotionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Finds the existing instance on this widget's owning WidgetTree; never adds/rebuilds it. */
    UFUNCTION(BlueprintPure, Category="UI|Button Motion") static UDWUIButtonLiftComponent* GetLiftComponent(UWidget* Widget);
    UFUNCTION(BlueprintPure, Category="UI|Button Motion") static UDWUIButtonPressComponent* GetPressComponent(UWidget* Widget);
    /** Inverts the Bezier X coordinate before reading Y, matching CSS timing semantics. */
    UFUNCTION(BlueprintPure, Category="UI|Button Motion") static float EvaluateCSSEase(float Time01,FVector2D Control1,FVector2D Control2);
    /** Pure numerical checks only; not an input, rendering or gameplay acceptance test. */
    UFUNCTION(BlueprintCallable, Category="UI|Button Motion|Diagnostics") static bool RunNumericalSelfTest(FString& Report);
};
