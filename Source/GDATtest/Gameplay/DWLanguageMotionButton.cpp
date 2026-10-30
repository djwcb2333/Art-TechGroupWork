#include "DWLanguageMotionButton.h"
#include "DWLanguageUICommon.h"
#include "DWUIBounceComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UDWLanguageActionMotion::RebuildWidget()
{
    if(!Bounce){Bounce=NewObject<UDWUIBounceComponent>(this);Bounce->Initialize(this);}
    Bounce->bPlayOnConstruct=false;Bounce->bAutoBindButtons=false;Bounce->bAnimateNonButtonHover=false;
    Bounce->PreConstruct(IsDesignTime());
    auto Wrapped=Bounce->RebuildWidgetWithContent(Content.IsValid()?Content.ToSharedRef():SNullWidget::NullWidget);
    Bounce->Construct();return Wrapped;
}
void UDWLanguageActionMotion::Hover(bool In){if(Bounce)Bounce->SetHovered(In);}
void UDWLanguageActionMotion::Press(bool In){if(Bounce)Bounce->SetPressed(In);}
void UDWLanguageActionMotion::ReleaseSlateResources(bool Children)
{if(Bounce)Bounce->Destruct();Content.Reset();Super::ReleaseSlateResources(Children);}

namespace
{
class SDWManagedAction : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWManagedAction)
        : _MinWidth(100.f), _Enabled(true), _Selected(false) {}
        SLATE_ARGUMENT(TSharedPtr<FDWLanguageUIContext>, Context)
        SLATE_ARGUMENT(FText, Label)
        SLATE_EVENT(FOnClicked, OnClicked)
        SLATE_ARGUMENT(float, MinWidth)
        SLATE_ARGUMENT(bool, Enabled)
        SLATE_ARGUMENT(bool, Selected)
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs)
    {
        check(InArgs._Context.IsValid());
        const TSharedRef<FDWLanguageUIContext> C = InArgs._Context.ToSharedRef();
        Anchor.Reset(NewObject<UDWLanguageActionMotion>());
        const TWeakObjectPtr<UDWLanguageActionMotion> Weak(Anchor.Get());
        TSharedRef<STextBlock> Text=SNew(STextBlock).Text(InArgs._Label).Font(C->Font(18)).ColorAndOpacity(DWLanguageUI::Ink())
            .AutoWrapText(false).Justification(ETextJustify::Center);
        if(C->Snapshot.bEnglish)Text->SetRenderTransform(FSlateRenderTransform(FVector2D(0,-2.7f)));
        Anchor->SetContent(SNew(SButton).ButtonStyle(&DWLanguageUI::ButtonStyle())
            .ContentPadding(FMargin(18,9)).HAlign(HAlign_Center).VAlign(VAlign_Center)
            .IsEnabled(InArgs._Enabled).OnClicked(InArgs._OnClicked)
            .ButtonColorAndOpacity(InArgs._Selected?DWLanguageUI::Gold()/DWLanguageUI::Paper():FLinearColor::White)
            .OnHovered_Lambda([Weak]{if(Weak.IsValid())Weak->Hover(true);})
            .OnUnhovered_Lambda([Weak]{if(Weak.IsValid()){Weak->Hover(false);Weak->Press(false);}})
            .OnPressed_Lambda([Weak]{if(Weak.IsValid())Weak->Press(true);})
            .OnReleased_Lambda([Weak]{if(Weak.IsValid())Weak->Press(false);})[Text]);
        ChildSlot[SNew(SBox).MinDesiredWidth(InArgs._MinWidth).MinDesiredHeight(46)[Anchor->TakeWidget()]];
    }
    virtual ~SDWManagedAction() override {if(Anchor.IsValid())Anchor->ReleaseSlateResources(true);}
private:
    TStrongObjectPtr<UDWLanguageActionMotion> Anchor;
};
}
TSharedRef<SWidget> DWLanguageUI::MotionButton(const TSharedRef<FDWLanguageUIContext>& C,const FText& Label,
    FOnClicked Click,float MinWidth,bool Enabled,bool Selected)
{return SNew(SDWManagedAction).Context(C).Label(Label).OnClicked(Click).MinWidth(MinWidth).Enabled(Enabled).Selected(Selected);}
