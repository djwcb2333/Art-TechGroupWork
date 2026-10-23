#pragma once
#include "CoreMinimal.h"
#include "Components/Border.h"
#include "DWC01HUDAnchor.generated.h"
class UDWUIOffscreenComponent;
/** Runtime display anchor: independent menu/cinematic requests reuse the existing offscreen effect. */
UCLASS()
class GDATTEST_API UDWC01HUDAnchor:public UBorder
{
 GENERATED_BODY()
public:
 UPROPERTY(Transient,BlueprintReadOnly,Category="UI|Cinematic") TObjectPtr<UDWUIOffscreenComponent> Offscreen;
 /** Menu motion never joins the cinematic component registry, so cinematic cleanup
  * cannot return a HUD which is still hidden by an open menu. */
 UPROPERTY(Transient,BlueprintReadOnly,Category="UI|Menu") TObjectPtr<UDWUIOffscreenComponent> MenuOffscreen;
 void SetMenuHidden(bool bHidden);
 FVector2D GetMenuOffset()const;
 FVector2D GetPresentationOffset()const;
 bool IsMenuMotionActive()const;
 /** The original and menu channels share an exit direction. Use the outermost
  * current displacement rather than adding two simultaneous slide distances. */
 static FVector2D ComposePresentationOffsets(FVector2D Cinematic,FVector2D Menu);
 virtual void ReleaseSlateResources(bool ReleaseChildren)override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget()override;
private:
 bool bMenuHidden=false;
 double MenuReturnStarted=0.;
 FVector2D MenuExitFloor=FVector2D::ZeroVector,MenuReturnCorrection=FVector2D::ZeroVector;
};
