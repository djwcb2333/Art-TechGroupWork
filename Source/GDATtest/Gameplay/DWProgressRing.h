#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "DWProgressRing.generated.h"
class SDWRingProgress;

/** A normal UMG Designer control: its size, color and progress are editable. */
UCLASS(BlueprintType, meta=(DisplayName="Dough World Progress Ring"))
class GDATTEST_API UDWProgressRing : public UWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Progress", meta=(ClampMin="0",ClampMax="1")) float Fraction = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance") FLinearColor Tint = FLinearColor(.34f,.78f,.57f,1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Appearance", meta=(ClampMin="16")) float Diameter = 92.f;
    UFUNCTION(BlueprintCallable, Category="Progress") void SetFraction(float InFraction);
    virtual void SynchronizeProperties() override;
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
#if WITH_EDITOR
    virtual const FText GetPaletteCategory() override { return FText::FromString(TEXT("Dough World")); }
#endif
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    TSharedPtr<SDWRingProgress> Ring;
};
