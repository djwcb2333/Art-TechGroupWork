#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWUIBounceComponent.generated.h"

class UButton;
class USoundBase;
class SBox;

/** Add to a widget in UMG Designer's Components panel. The separate Slate wrapper composes with
 * existing UMG animations; this component never writes its owner's RenderTransform or opacity.
 * Construction is the entrance trigger, not a later Visibility change. Button clicks are untouched.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced, meta=(DisplayName="DW UI Bounce"))
class GDATTEST_API UDWUIBounceComponent : public UUIComponent
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Spring", meta=(ClampMin="0.1", ClampMax="30", Units="Hz")) float FrequencyHz = 5.f;
    /** Less than one overshoots, one is critically damped, greater than one settles without a bounce. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Spring", meta=(ClampMin="0.05", ClampMax="3")) float DampingRatio = .55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Spring") FVector2D Pivot = FVector2D(.5f, .5f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Spring", meta=(ClampMin="0.00001")) float SettleScaleTolerance = .001f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Spring", meta=(ClampMin="0.001")) float SettlePixelTolerance = .05f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Spring", meta=(ClampMin="0.001", Units="deg")) float SettleAngleTolerance = .05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Interaction") bool bAutoBindButtons = true;
    /** A non-button must permit hit testing for pointer enter/leave. It never captures the mouse. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Interaction") bool bAnimateNonButtonHover = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Interaction", meta=(ClampMin="0.05", ClampMax="3")) float HoverScale = 1.06f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Interaction", meta=(ClampMin="0.05", ClampMax="3")) float PressedScale = .94f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Interaction", meta=(Units="deg")) float HoverAngle = -1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Interaction", meta=(Units="deg")) float PressedAngle = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Interaction") FVector2D HoverOffset = FVector2D(0.f, -2.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Interaction") FVector2D PressedOffset = FVector2D(0.f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Entrance") bool bPlayOnConstruct = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Entrance", meta=(ClampMin="0.05", ClampMax="3")) float EntryScale = .82f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Entrance") FVector2D EntryOffset = FVector2D(0.f, 30.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Entrance", meta=(Units="deg")) float EntryAngle = -3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Pulse") float PulseScaleOffset = .08f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Pulse") FVector2D PulseOffset = FVector2D(0.f, -8.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Pulse", meta=(Units="deg")) float PulseAngle = 3.f;

    /** Optional additional hover/press cues. Empty by default; OnClicked audio remains in the old UI. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Audio") TObjectPtr<USoundBase> HoverSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Audio") TObjectPtr<USoundBase> PressedSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Bounce|Audio", meta=(ClampMin="0", ClampMax="2")) float SoundVolume = .35f;

    UFUNCTION(BlueprintCallable, Category="UI|Bounce") void Pulse(float Strength = 1.f);
    UFUNCTION(BlueprintCallable, Category="UI|Bounce") void PlayEntrance();
    UFUNCTION(BlueprintCallable, Category="UI|Bounce") void SetHovered(bool bHovered);
    UFUNCTION(BlueprintCallable, Category="UI|Bounce") void SetPressed(bool bPressed);
    /** Stops this effect and restores its wrapper only; the original UMG animation is not stopped. */
    UFUNCTION(BlueprintCallable, Category="UI|Bounce") void ResetBounce();
    UFUNCTION(BlueprintPure, Category="UI|Bounce") bool IsAnimating() const { return TickerHandle.IsValid(); }
    /** These values describe the additional wrapper, not the combined transform of the UMG animation. */
    UFUNCTION(BlueprintPure, Category="UI|Bounce") float GetCurrentScale() const { return CurrentScale; }
    UFUNCTION(BlueprintPure, Category="UI|Bounce") float GetCurrentAngle() const { return CurrentAngle; }
    UFUNCTION(BlueprintPure, Category="UI|Bounce") FVector2D GetCurrentOffset() const { return CurrentOffset; }

    virtual TSharedRef<SWidget> RebuildWidgetWithContent(TSharedRef<SWidget> Content) override;
    virtual void BeginDestroy() override;
protected:
    virtual void OnPreConstruct(bool bIsDesignTime) override;
    virtual void OnConstruct() override;
    virtual void OnDestruct() override;
private:
    TWeakPtr<SBox> Wrapper;
    TWeakObjectPtr<UButton> BoundButton;
    FTSTicker::FDelegateHandle TickerHandle;
    bool bDesignTime = true;
    bool bConstructed = false;
    bool bHoveredState = false;
    bool bPressedState = false;
    bool bInsideTicker = false;
    double LastTickAt = 0.;
    float CurrentScale = 1.f;
    float ScaleVelocity = 0.f;
    float CurrentAngle = 0.f;
    float AngleVelocity = 0.f;
    FVector2D CurrentOffset = FVector2D::ZeroVector;
    FVector2D OffsetVelocity = FVector2D::ZeroVector;

    bool CanAnimate() const;
    void EnsureTicker();
    void StopTicker();
    bool TickSpring(float DeltaSeconds);
    void ApplyWrapperTransform();
    void GetTarget(float& Scale, FVector2D& Offset, float& Angle) const;
    void BindButton();
    void UnbindButton();
    void PlayCue(USoundBase* Sound) const;
    UFUNCTION() void HandleHovered();
    UFUNCTION() void HandleUnhovered();
    UFUNCTION() void HandlePressed();
    UFUNCTION() void HandleReleased();
};

/** Finds the Designer-authored runtime component. These helpers never add components or change assets. */
UCLASS()
class GDATTEST_API UDWUIBounceLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure, Category="UI|Bounce") static UDWUIBounceComponent* GetBounceComponent(UWidget* Widget);
    UFUNCTION(BlueprintCallable, Category="UI|Bounce") static bool PulseWidget(UWidget* Widget, float Strength = 1.f);
    UFUNCTION(BlueprintCallable, Category="UI|Bounce") static bool PlayWidgetEntrance(UWidget* Widget);
    UFUNCTION(BlueprintCallable, Category="UI|Bounce") static bool ResetWidgetBounce(UWidget* Widget);
};
