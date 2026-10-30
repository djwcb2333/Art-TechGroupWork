#include "DWItemTooltip.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SGameLayerManager.h"
#include "Styling/StyleDefaults.h"

namespace DWItemTooltip
{
// The tooltip can outlive a temporary inventory projection. Keep its glyph brushes,
// loaded texture references and injected font provider alive with the Slate owner.
class SDWOwnedItemToolTip final : public SToolTip
{
public:
 void Construct(const SToolTip::FArguments& Args,TSharedRef<FDWLanguageUIContext> C)
 {Context=C;SToolTip::Construct(Args);}
 void SetView(TSharedRef<FDWLanguageUIContext> C,const TSharedRef<SWidget>& Content)
 {Context=C;SetContentWidget(Content);}
private:
 TSharedPtr<FDWLanguageUIContext> Context;
};
// Budget in the same DPI-scaled local units used by the game layer manager.
static FVector2D ContentBudget()
{
    constexpr float OuterInset = 16.f;
    constexpr float PaperPadding = 20.f;
    // WidgetHost is inside the same DPI scaler as the native tooltip presenter.
    // Its title-bar row may consume height, so keep a conservative allowance.
    FVector2D Size(480.f, 320.f);
    if (GEngine && GEngine->GameViewport)
    {
        const TSharedPtr<IGameLayerManager> Layer = GEngine->GameViewport->GetGameLayerManager();
        if (Layer.IsValid())
        {
            const FVector2D Host = Layer->GetViewportWidgetHostGeometry().GetLocalSize();
            if (Host.X > 1.f && Host.Y > 1.f) Size = Host;
        }
    }
    const float Edge = 2.f * (OuterInset + PaperPadding);
    constexpr float TitleBarAllowance = 48.f;
    return FVector2D(FMath::Clamp(Size.X - Edge, 1.f, 360.f), FMath::Max(1.f, Size.Y - Edge - TitleBarAllowance));
}
static TSharedRef<SWidget> Content(const TSharedRef<FDWLanguageUIContext>& C,const FDWItemTooltipView& V)
{
    auto Lines=SNew(SVerticalBox);
    Lines->AddSlot().AutoHeight()[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth().Padding(0,0,16,0)[C->Glyph(V.Icon,FVector2D(60,60))]
        +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[DWLanguageUI::Text(C,V.Name,24,DWLanguageUI::Ink(),true)]];
    auto Add=[&](const FText& T,FLinearColor Color)
    {if(!T.IsEmpty()) Lines->AddSlot().AutoHeight().Padding(0,8,0,0)[DWLanguageUI::Text(C,T,18,Color,true)];};
    Add(V.Use,DWLanguageUI::Ink());
    if(V.StackLimit>0) Add(FText::Format(DWLanguageUI::L(C,TEXT("数量 {0} / {1}"),TEXT("Quantity {0} / {1}")),FText::AsNumber(V.Quantity),FText::AsNumber(V.StackLimit)),DWLanguageUI::Muted());
    for(const FText& T:V.Attributes) Add(T,DWLanguageUI::Ink());
    Add(V.Product,DWLanguageUI::Ink());
    for(const FText& T:V.Materials) Add(T,DWLanguageUI::Ink());
    for(const auto& Condition:V.Conditions)
    {
        Add(FText::Format(DWLanguageUI::L(C,TEXT("需要：{0} · {1}"),TEXT("Needs: {0} · {1}")),Condition.Name,
            Condition.bSatisfied?DWLanguageUI::L(C,TEXT("已满足"),TEXT("Ready")):DWLanguageUI::L(C,TEXT("未满足"),TEXT("Missing"))),DWLanguageUI::Ink());
        Add(Condition.Description,DWLanguageUI::Ink()); Add(Condition.MissingReason,DWLanguageUI::Ink());
    }
    Add(V.BlockedReason,DWLanguageUI::Ink());
    // Outer transparent inset participates in the native tooltip presenter's fit;
    // the inner border retains the paper brush and text padding across updates.
    return SNew(SBorder).BorderImage(DWLanguageUI::PaperBrush()).Padding(FMargin(20))
        [SNew(SBox).MaxDesiredHeight_Lambda([] { return FOptionalSize(ContentBudget().Y); })
            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                [SNew(SBox).WidthOverride_Lambda([] { return FOptionalSize(ContentBudget().X); })[Lines]]]];
}
TSharedPtr<SToolTip> Make(const TSharedRef<FDWLanguageUIContext>& C,const FDWItemTooltipView& V)
{
    // A handcraft condition has no item ID, but is still a real condition tooltip.
    if(!V.bOccupied || (V.ItemId.IsNone() && V.Conditions.IsEmpty())) return nullptr;
    return SNew(SDWOwnedItemToolTip,C).IsInteractive(false).BorderImage(FStyleDefaults::GetNoBrush()).TextMargin(FMargin(16))[Content(C,V)];
}
void Update(const TSharedRef<SToolTip>& Tooltip,const TSharedRef<FDWLanguageUIContext>& C,const FDWItemTooltipView& V)
{StaticCastSharedRef<SDWOwnedItemToolTip>(Tooltip)->SetView(C,Content(C,V));}
// The row owns its context even for a hand-crafting condition whose id is None.
// Its glyph brushes and textures must outlive every SImage in the contained row.
class SDWOwnedNeeds final : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SDWOwnedNeeds) {}
  SLATE_DEFAULT_SLOT(FArguments, Content)
 SLATE_END_ARGS()
 void Construct(const FArguments& Args,TSharedRef<FDWLanguageUIContext> InContext)
 { Context=InContext; ChildSlot[Args._Content.Widget]; }
private:
 TSharedPtr<FDWLanguageUIContext> Context;
};
TSharedRef<SWidget> Needs(const TSharedRef<FDWLanguageUIContext>& C,const TArray<FDWCraftingConditionView>& Conditions)
{
    auto Row=SNew(SHorizontalBox);
    if(Conditions.IsEmpty())return SNew(SDWOwnedNeeds,C)[Row];
    Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
        [DWLanguageUI::Text(C,DWLanguageUI::L(C,TEXT("需要："),TEXT("Needs:")),20,DWLanguageUI::Ink())];
    auto Icons=SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(12,8));
    for(const auto& Condition:Conditions)
    {
        FDWItemTooltipView Info;Info.ItemId=Condition.ConditionId;Info.bOccupied=true;Info.Name=Condition.Name;
        Info.Icon=Condition.Icon;Info.Conditions.Add(Condition);
        Icons->AddSlot()[SNew(SBox).ToolTip(Make(C,Info))
            [SNew(SVerticalBox)
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[C->Glyph(Condition.Icon,FVector2D(40,40))]
                +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [DWLanguageUI::Text(C,Condition.bSatisfied?DWLanguageUI::L(C,TEXT("已满足"),TEXT("Ready")):DWLanguageUI::L(C,TEXT("缺少"),TEXT("Missing")),16,DWLanguageUI::Ink())]]];
    }
    Row->AddSlot().FillWidth(1)[Icons];return SNew(SDWOwnedNeeds,C)[Row];
}
}
