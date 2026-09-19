#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "DWUIOffscreenComponent.generated.h"
class SBox;
class APlayerController;
UENUM(BlueprintType)
enum class EDWUIExitEdge:uint8 { Top,Bottom,Left,Right };

/** Designer > select a HUD group > Add Component > DW UI Offscreen.
 * A separate Slate wrapper preserves the authored widget transform and existing bounce effects. */
UCLASS(BlueprintType,Blueprintable,EditInlineNew,DefaultToInstanced,meta=(DisplayName="DW UI Offscreen"))
class GDATTEST_API UDWUIOffscreenComponent:public UUIComponent
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen") bool bFollowCinematics=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen") EDWUIExitEdge ExitEdge=EDWUIExitEdge::Top;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen",meta=(ClampMin="0",Units="s")) float AnticipationSeconds=.12f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen",meta=(ClampMin="0")) float AnticipationDistance=18;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen",meta=(ClampMin="0.01",Units="s")) float ExitSeconds=.32f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen",meta=(ClampMin="0.01",Units="s")) float ReturnSeconds=.65f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen") bool bAutoExitDistance=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen",meta=(ClampMin="0")) float ExitDistance=700;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen",meta=(ClampMin="0")) float OffscreenPadding=40;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen|Spring",meta=(ClampMin="0.2",ClampMax="1")) float DampingRatio=.65f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Offscreen|Spring",meta=(ClampMin="1",ClampMax="4")) float ReturnOscillations=1.5f;
 UFUNCTION(BlueprintCallable,Category="UI|Offscreen") void HideForCinematic();
 UFUNCTION(BlueprintCallable,Category="UI|Offscreen") void ReturnToScreen();
 UFUNCTION(BlueprintCallable,Category="UI|Offscreen") void RestoreImmediately();
 UFUNCTION(BlueprintPure,Category="UI|Offscreen") FVector2D GetCurrentOffset()const{return Offset;}
 UFUNCTION(BlueprintPure,Category="UI|Offscreen") bool IsOffscreen()const{return bFullyHidden;}
 UFUNCTION(BlueprintPure,Category="UI|Offscreen") bool IsAnimating()const{return Handle.IsValid();}
 static TArray<UDWUIOffscreenComponent*> FindForPlayer(APlayerController* PC);
 static float ReturnFraction(float T,float Damping,float Oscillations);
 virtual TSharedRef<SWidget> RebuildWidgetWithContent(TSharedRef<SWidget> Content)override;
 virtual void BeginDestroy()override;
protected:
 virtual void OnPreConstruct(bool bDesignTime)override;
 virtual void OnConstruct()override;
 virtual void OnDestruct()override;
private:
 TWeakPtr<SBox> Wrapper;
 FTSTicker::FDelegateHandle Handle;
 bool bConstructed=false,bDesign=true,bFullyHidden=false,bExiting=false;
 double Started=0;
 FVector2D Offset=FVector2D::ZeroVector,StartOffset=FVector2D::ZeroVector,EndOffset=FVector2D::ZeroVector;
 FVector2D Direction()const;
 float ComputeDistance()const;
 void StartTicker();void StopTicker();void Apply();bool TickAnimation(float Dt);
};
