#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DWWorldTargetIndicator.generated.h"

class ADWGameplayHUD;
class UDWMinimapMarkerComponent;

/** Read-only, non-interactive display for the current navigation target. */
UCLASS()
class GDATTEST_API UDWWorldTargetIndicator : public UUserWidget
{
    GENERATED_BODY()
public:
    void InitializeIndicator(ADWGameplayHUD* InHUD);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="4",ClampMax="120")) float RingRadius=22.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="25",ClampMax="2000",Units="cm")) float GroundRingRadius=250.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="0",ClampMax="50",Units="cm")) float GroundRingOffset=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="1",ClampMax="12")) float RingThickness=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="0",ClampMax="0.4")) float PulseAmplitude=.055f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="0.1",ClampMax="30")) float PulseFrequency=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="0.05",ClampMax="3")) float PulseDamping=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="0.25",Units="s")) float PulseInterval=1.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Target",meta=(ClampMin="8",ClampMax="150")) float ScreenInset=38.f;
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override;
private:
    TWeakObjectPtr<ADWGameplayHUD> HUD;
    TWeakObjectPtr<UDWMinimapMarkerComponent> TargetActor;
    FVector TargetWorld=FVector::ZeroVector;
    FVector GroundWorld=FVector::ZeroVector,GroundNormal=FVector::UpVector,GroundQueryTarget=FVector::ZeroVector;
    TArray<FVector2f> GroundRingPoints;
    FVector2D DisplayPosition=FVector2D::ZeroVector,DisplayDirection=FVector2D(0.,-1.),PreviousDirection=FVector2D(0.,-1.);
    bool bTargetValid=false,bActorTarget=false,bDisplayInside=false,bDisplayValid=false,bHasGround=false;
    double SelectedAt=0.,LastGroundQuery=0.;
};
