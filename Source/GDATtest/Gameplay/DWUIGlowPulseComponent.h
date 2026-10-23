#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWUIGlowPulseComponent.generated.h"

/** A soft white/color halo painted behind the original widget. No texture, material or
 * post-process bloom is required. Add before DW UI Bounce so both move as one object.
 * This wrapper neither changes desired size nor handles input; clipping ancestors must
 * leave enough paint room around their content for the selected HaloExtent. */
UCLASS(BlueprintType,Blueprintable,EditInlineNew,DefaultToInstanced,meta=(DisplayName="DW UI Glow Pulse"))
class GDATTEST_API UDWUIGlowPulseComponent : public UUIComponent
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Glow Pulse",meta=(ClampMin="0",ClampMax="5",Units="s")) float RiseSeconds=.07f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Glow Pulse",meta=(ClampMin="0",ClampMax="5",Units="s")) float HoldSeconds=.04f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Glow Pulse",meta=(ClampMin="0",ClampMax="5",Units="s")) float FadeSeconds=.34f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Glow Pulse",meta=(ClampMin="0",ClampMax="1")) float PeakOpacity=.68f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Glow Pulse",meta=(ClampMin="0",ClampMax="96")) float HaloExtent=24.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Glow Pulse",meta=(ClampMin="0",ClampMax="96")) float CornerRadius=10.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Glow Pulse") FLinearColor GlowColor=FLinearColor::White;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Glow Pulse") bool bPlayOnConstruct=false;
 /** Repeated successful events restart one pulse; they do not accumulate lasting light. */
 UFUNCTION(BlueprintCallable,Category="UI|Glow Pulse") void Pulse(float Strength=1.f);
 UFUNCTION(BlueprintCallable,Category="UI|Glow Pulse") void ResetPulse();
 UFUNCTION(BlueprintPure,Category="UI|Glow Pulse") float GetPulseOpacity() const;
 UFUNCTION(BlueprintPure,Category="UI|Glow Pulse") float GetTotalSeconds() const;
 UFUNCTION(BlueprintPure,Category="UI|Glow Pulse") bool IsAnimating() const{return bPulsing;}
 virtual TSharedRef<SWidget> RebuildWidgetWithContent(TSharedRef<SWidget> Content) override;
 virtual void BeginDestroy() override;
protected:
 virtual void OnPreConstruct(bool bIsDesignTime) override;
 virtual void OnConstruct() override;
 virtual void OnDestruct() override;
private:
 TWeakPtr<SWidget> Wrapper;
 FTSTicker::FDelegateHandle TickerHandle;
 bool bRuntime=false,bPulsing=false;
 float Envelope=0.f,PulseStrength=1.f;
 double StartedAt=0.;
 bool TickPulse(float DeltaSeconds);
 void StopTicker();
 void RefreshPaint();
};

UCLASS()
class GDATTEST_API UDWUIGlowPulseLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintPure,Category="UI|Glow Pulse") static UDWUIGlowPulseComponent* GetGlowPulseComponent(UWidget* Widget);
};
