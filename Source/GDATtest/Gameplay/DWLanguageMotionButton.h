#pragma once
#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "Widgets/Input/SButton.h"
#include "DWLanguageMotionButton.generated.h"
class UDWUIBounceComponent;
struct FDWLanguageUIContext;

/** Lifetime anchor for reusing the existing DW UI Bounce component around a native Slate button. */
UCLASS()
class GDATTEST_API UDWLanguageActionMotion : public UWidget
{
    GENERATED_BODY()
public:
    void SetContent(TSharedRef<SWidget> InContent) { Content=InContent; }
    void Hover(bool bIn);
    void Press(bool bIn);
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
private:
    UPROPERTY(Transient) TObjectPtr<UDWUIBounceComponent> Bounce;
    TSharedPtr<SWidget> Content;
};
namespace DWLanguageUI
{
    TSharedRef<SWidget> MotionButton(const TSharedRef<FDWLanguageUIContext>& Context,const FText& Label,
        FOnClicked OnClicked,float MinWidth,bool bEnabled,bool bSelected);
}
