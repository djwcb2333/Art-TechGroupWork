#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "DWWorldMapEntry.generated.h"

UCLASS()
class GDATTEST_API UDWWorldMapEntryButton : public UButton
{
    GENERATED_BODY()
public:
    UDWWorldMapEntryButton();
};

class ADWGameplayHUD;
class UTextBlock;

/** Small, separately clickable minimap-adjacent entry; the authored minimap remains untouched. */
UCLASS()
class GDATTEST_API UDWWorldMapEntry : public UUserWidget
{
    GENERATED_BODY()
public:
    void InitializeEntry(ADWGameplayHUD* InHUD);
    void UpdateEntry(bool bShow,float Diameter,FVector2D BottomRightMargin);
protected:
    virtual void NativeOnInitialized() override;
private:
    UFUNCTION() void OpenMap();
    TWeakObjectPtr<ADWGameplayHUD> HUD;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
    FVector2D LastPosition=FVector2D::ZeroVector;
    bool bPositioned=false;
};
