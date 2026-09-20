#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/Widget.h"
#include "DWInteractionPromptWidget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWSignpostComponent.generated.h"
class UWidgetComponent;
class UDWInteractionPromptStyle;

/** Vector arrow: independent of font glyph coverage. Zero degrees points right. */
UCLASS()
class GDATTEST_API UDWSignpostArrow : public UWidget
{
 GENERATED_BODY()
public:
 void SetDirection(float Degrees,FLinearColor Color);
 virtual void ReleaseSlateResources(bool bReleaseChildren)override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget()override;
private:
 TSharedPtr<class SDWSignpostArrow> Arrow;
};

UCLASS(Blueprintable)
class GDATTEST_API UDWSignpostPromptWidget : public UDWInteractionPromptWidget
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Signpost") void SetDirection(float ScreenDegrees);
protected:
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UDWSignpostArrow> DirectionArrow;
};

USTRUCT(BlueprintType)
struct FDWSignpostDirection
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost") FText ChineseText;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost") FText EnglishText;
 /** Optional level actor along the road. Its direction takes priority over World Direction. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost") TObjectPtr<AActor> DirectionTarget;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost") FVector WorldDirection=FVector::ForwardVector;
};

/** Display-only road labels; add directly to an existing sign actor. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,Blueprintable,meta=(BlueprintSpawnableComponent,DisplayName="DW Signpost"))
class GDATTEST_API UDWSignpostComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UDWSignpostComponent();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost") TArray<FDWSignpostDirection> Directions;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost",meta=(ClampMin="1",Units="cm")) float DetectionRadius=650;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost",meta=(ClampMin="0",Units="cm")) float ExitHysteresis=80;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost",meta=(ClampMin="0",Units="cm")) float HeightTolerance=600;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost") FVector WorldOffset=FVector(0,0,250);
 /** Distance of each label from sign along its road, in world centimeters. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost",meta=(ClampMin="0",Units="cm")) float LabelWorldRadius=300;
 /** Keep labels below the top HUD and inside the viewport; arrow direction still follows the road. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost|Appearance",meta=(ClampMin="0",ClampMax="0.4")) float TopScreenMarginFraction=.28f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost") bool bPromptEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost|Appearance") TObjectPtr<UDWInteractionPromptStyle> Style;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost|Appearance") TSubclassOf<UDWSignpostPromptWidget> WidgetClass;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Signpost|Appearance") FVector2D DrawSize=FVector2D(420,100);
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Signpost|Diagnostics") bool bPromptsVisible=false;
 UFUNCTION(BlueprintPure,Category="Signpost") TArray<UWidgetComponent*> GetDirectionWidgets()const;
 virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* F)override;
protected:
 virtual void BeginPlay()override;
 virtual void EndPlay(const EEndPlayReason::Type R)override;
private:
 UPROPERTY(Transient) TArray<TObjectPtr<UWidgetComponent>> Widgets;
 void RebuildDirections();
};

UCLASS()
class GDATTEST_API UDWSignpostAuthoringLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Creates only if absent; never rebuilds or replaces an existing WBP. */
 UFUNCTION(BlueprintCallable,Category="Signpost|Authoring") static bool CreateSignpostWidget();
 UFUNCTION(BlueprintCallable,Category="Signpost|Authoring") static UDWSignpostComponent* AddSignpostComponent(AActor* Actor);
};
