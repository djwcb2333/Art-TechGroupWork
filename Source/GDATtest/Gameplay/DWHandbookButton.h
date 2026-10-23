#pragma once
#include "CoreMinimal.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "DWHandbookButton.generated.h"
class UDWGameplayWidget;
class UDWUIBounceComponent;
/** A per-button action relay keeps runtime book controls independent of authored WBP graphs. */
UCLASS()
class GDATTEST_API UDWHandbookButton:public UButton
{
 GENERATED_BODY()
public:
 void Configure(UDWGameplayWidget* InScreen,int32 InAction,bool bItemSlot=false);
 /** Item choices only change selection color; execution buttons keep the Press component. */
 bool bC01ItemSlot=false;
 bool bBookTab=false;
 float Selection=0.f;
 FLinearColor C01BaseColor=FLinearColor::White;
 bool bC01TransparentRest=false;
 void UpdateSelection(bool bSelected);
 UFUNCTION(BlueprintPure,Category="UI|Handbook") float GetSelectionBlend()const{return Selection;}
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
private:
 double LastSelectionTick=0.;
 UPROPERTY(Transient) TObjectPtr<UDWGameplayWidget> Screen;
 int32 Action=0;
 UFUNCTION() void Dispatch();
};

/** Runtime-only wrapper reuses the existing spring component without modifying WBP assets. */
UCLASS()
class GDATTEST_API UDWHandbookFrame:public UBorder
{
 GENERATED_BODY()
public:
 UPROPERTY(Transient) TObjectPtr<UDWUIBounceComponent> Bounce;
 void Pulse();
 virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
};
