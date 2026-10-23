#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Extensions/UIComponent.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWUIBrushFillComponent.generated.h"

class UButton;
/** Paints behind the original content without capturing input or changing its layout/transform.
 * Add before DW UI Bounce in Components so the spring also carries the painted background. */
UCLASS(BlueprintType,Blueprintable,EditInlineNew,DefaultToInstanced,meta=(DisplayName="DW UI Brush Fill"))
class GDATTEST_API UDWUIBrushFillComponent : public UUIComponent
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Brush Fill",meta=(ClampMin="0",Units="s")) float FillSeconds=.32f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Brush Fill",meta=(ClampMin="0",Units="s")) float ClearSeconds=.18f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Brush Fill") FLinearColor FillColor=FLinearColor(.102f,.082f,.051f,.86f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Brush Fill",meta=(ClampMin="1",ClampMax="12")) int32 StrokeCount=5;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Brush Fill",meta=(ClampMin="0",ClampMax="1")) float StrokeStagger=.45f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Brush Fill",meta=(ClampMin="0",ClampMax="12")) float BristleEdgePixels=4.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Brush Fill") bool bAutoBindButtons=true;
 UFUNCTION(BlueprintCallable,Category="UI|Brush Fill") void SetHovered(bool bHovered);
 UFUNCTION(BlueprintCallable,Category="UI|Brush Fill") void ResetFill();
 UFUNCTION(BlueprintPure,Category="UI|Brush Fill") float GetFillProgress()const{return Progress;}
 UFUNCTION(BlueprintPure,Category="UI|Brush Fill") bool IsAnimating()const{return !FMath::IsNearlyEqual(Progress,TargetProgress);}
 virtual TSharedRef<SWidget> RebuildWidgetWithContent(TSharedRef<SWidget> Content)override;
 virtual void BeginDestroy()override;
protected:
 virtual void OnPreConstruct(bool bIsDesignTime)override;
 virtual void OnConstruct()override;
 virtual void OnDestruct()override;
private:
 TWeakPtr<SWidget> Wrapper;
 TWeakObjectPtr<UButton> BoundButton;
 FTSTicker::FDelegateHandle TickerHandle;
 bool bRuntime=false,bHoveredState=false;
 float Progress=0.f,StartProgress=0.f,TargetProgress=0.f,Duration=0.f;
 double StartedAt=0.;
 bool TickFill(float DeltaSeconds);
 void StopTicker();void UnbindButton();
 UFUNCTION() void HandleHovered(){SetHovered(true);}
 UFUNCTION() void HandleUnhovered(){SetHovered(false);}
};
UCLASS()
class GDATTEST_API UDWUIBrushFillLibrary:public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintPure,Category="UI|Brush Fill") static UDWUIBrushFillComponent* GetBrushFillComponent(UWidget* Widget);
};
