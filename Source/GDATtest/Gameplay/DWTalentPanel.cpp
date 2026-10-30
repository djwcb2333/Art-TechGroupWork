#include "DWTalentPanel.h"
#include "DWLanguageUICommon.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
// These two reserved IDs have no design definition, not merely unmet prerequisites.
bool IsUndefinedTalent(FName Id)
{
    return Id == TEXT("a5") || Id == TEXT("a6");
}

FText TalentDisplayName(const TSharedRef<FDWLanguageUIContext>& C, const FDWLanguageTalentView& Talent)
{
    if (IsUndefinedTalent(Talent.Id))
        return DWLanguageUI::L(C, TEXT("待定义"), TEXT("Not defined"));
    // SDD-A01 section 3 names only a1. Keep real authored names and do not invent English names.
    if (!C->GetSnapshot().bEnglish && Talent.Id == TEXT("a1")
        && (Talent.Name.IsEmpty() || Talent.Name.ToString().Equals(Talent.Id.ToString(), ESearchCase::IgnoreCase)))
        return FText::FromString(TEXT("第三只手"));
    return Talent.Name;
}

FText UndefinedTalentReason(const TSharedRef<FDWLanguageUIContext>& C)
{
    return DWLanguageUI::L(C, TEXT("暂不可用：该天赋尚未定义，不会扣除点数。"),
        TEXT("Unavailable: this talent is not defined. No points are spent."));
}

struct FDWTalentLink
{
    FVector2f From;
    FVector2f To;
    bool bLearned = false;
};

/** Passive lines are behind hit-testable nodes and cannot eat pointer input. */
class SDWTalentLinks : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SDWTalentLinks) {}
        SLATE_ARGUMENT(TArray<FDWTalentLink>, Links)
        SLATE_ARGUMENT(FVector2D, GraphSize)
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs)
    {
        Links = InArgs._Links;
        GraphSize = InArgs._GraphSize;
        SetVisibility(EVisibility::HitTestInvisible);
    }
    virtual FVector2D ComputeDesiredSize(float) const override { return GraphSize; }
    virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 LayerId, const FWidgetStyle& Style, bool) const override
    {
        for (const FDWTalentLink& Link : Links)
        {
            TArray<FVector2f> Points;
            Points.Add(Link.From);
            Points.Add(Link.To);
            FSlateDrawElement::MakeLines(Elements, LayerId, Geometry.ToPaintGeometry(), MoveTemp(Points),
                ESlateDrawEffect::None, (Link.bLearned ? DWLanguageUI::Gold() : DWLanguageUI::Muted())
                    * Style.GetColorAndOpacityTint(), true, 3.f);
        }
        return LayerId;
    }
private:
    TArray<FDWTalentLink> Links;
    FVector2D GraphSize;
};

const FDWLanguageTalentView* FindTalent(const FDWLanguageUISnapshot& Snapshot, FName Id)
{
    return Snapshot.Talents.FindByPredicate([Id](const FDWLanguageTalentView& Talent) { return Talent.Id == Id; });
}
}

void SDWTalentPanel::Construct(const FArguments& InArgs)
{
    check(InArgs._Context.IsValid());
    Context = InArgs._Context;
    Refresh();
}

void SDWTalentPanel::Select(FName TalentId)
{
    if (!FindTalent(Context->GetSnapshot(), TalentId)) return;
    SelectedId = TalentId;
    Refresh();
}

TSharedRef<SWidget> SDWTalentPanel::MakeGraph()
{
    const auto C = Context.ToSharedRef();
    const FDWLanguageUISnapshot& Snapshot = Context->GetSnapshot();
    const int32 Rows = FMath::Max(1, FMath::DivideAndRoundUp(Snapshot.Talents.Num(), 3));
    const FVector2D GraphSize(560.f, FMath::Max(440.f, Rows * 120.f));
    const FVector2D NodeSize(108.f, 104.f);
    TMap<FName, FVector2D> Centers;
    for (int32 Index = 0; Index < Snapshot.Talents.Num(); ++Index)
    {
        const FDWLanguageTalentView& Node = Snapshot.Talents[Index];
        FVector2D Normalized;
        if (Node.bHasCanvasPosition && FMath::IsFinite(Node.CanvasPosition.X) && FMath::IsFinite(Node.CanvasPosition.Y))
            Normalized = FVector2D(FMath::Clamp(Node.CanvasPosition.X, 0., 1.), FMath::Clamp(Node.CanvasPosition.Y, 0., 1.));
        else
            // Spatial fallback only. No dependency, branch, cost or eligibility is invented here.
            Normalized = FVector2D((Index % 3) / 2., Rows > 1 ? (Index / 3) / double(Rows - 1) : .5);
        Centers.Add(Node.Id, FVector2D(64.f + Normalized.X * (GraphSize.X - 128.f),
            54.f + Normalized.Y * (GraphSize.Y - 108.f)));
    }
    TArray<FDWTalentLink> Links;
    for (const FDWLanguageTalentView& Node : Snapshot.Talents)
    {
        const FVector2D* Destination = Centers.Find(Node.Id);
        if (!Destination) continue;
        for (FName PredecessorId : Node.PrerequisiteIds)
        {
            const FVector2D* Source = Centers.Find(PredecessorId);
            const FDWLanguageTalentView* Predecessor = FindTalent(Snapshot, PredecessorId);
            if (Source && Predecessor && PredecessorId != Node.Id)
                Links.Add({FVector2f(*Source), FVector2f(*Destination), Node.bLearned && Predecessor->bLearned});
        }
    }
    TSharedRef<SCanvas> Nodes = SNew(SCanvas);
    for (const FDWLanguageTalentView& Node : Snapshot.Talents)
    {
        const FVector2D* Position = Centers.Find(Node.Id);
        if (!Position) continue;
        const FText StateLabel = IsUndefinedTalent(Node.Id)
            ? DWLanguageUI::L(C, TEXT("暂不可用"), TEXT("Unavailable"))
            : (Node.bLearned ? DWLanguageUI::L(C, TEXT("已学习"), TEXT("Learned"))
                : (Node.bCanLearn ? FText::Format(DWLanguageUI::L(C, TEXT("{0} 点"), TEXT("{0} points")), FText::AsNumber(Node.Cost))
                    : DWLanguageUI::L(C, TEXT("未解锁"), TEXT("Locked"))));
        TSharedRef<SVerticalBox> Face = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [Context->Glyph(Node.Icon, FVector2D(40, 40))]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 3).HAlign(HAlign_Center)
            [SNew(STextBlock).Text(TalentDisplayName(C, Node)).Font(Context->Font(14)).ColorAndOpacity(DWLanguageUI::Ink())
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis).AutoWrapText(false)]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [DWLanguageUI::Text(C, StateLabel, 12, DWLanguageUI::Ink())];
        Nodes->AddSlot().Position(*Position - NodeSize * .5).Size(NodeSize)
        [SNew(SButton).ButtonStyle(&DWLanguageUI::ButtonStyle()).ContentPadding(FMargin(8))
            .HAlign(HAlign_Fill).VAlign(VAlign_Center)
            .ToolTipText(FText::Format(FText::FromString(TEXT("{0} · {1}")),
                FText::FromName(Node.Id), TalentDisplayName(C, Node)))
            .ButtonColorAndOpacity(Node.Id == SelectedId ? DWLanguageUI::Gold() / DWLanguageUI::Paper()
                : FLinearColor(1.f, 1.f, 1.f, Node.bLearned || Node.bCanLearn ? 1.f : .66f))
            .OnClicked_Lambda([WeakThis = TWeakPtr<SDWTalentPanel>(SharedThis(this)), Id = Node.Id]()
            {
                if (const auto P = WeakThis.Pin()) P->Select(Id);
                return FReply::Handled();
            })[Face]];
    }
    return SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
        [SNew(SBox).WidthOverride(GraphSize.X).HeightOverride(GraphSize.Y)
            [SNew(SOverlay)
                + SOverlay::Slot()[SNew(SDWTalentLinks).Links(Links).GraphSize(GraphSize)]
                + SOverlay::Slot()[Nodes]]];
}

TSharedRef<SWidget> SDWTalentPanel::MakeDetails()
{
    const auto C = Context.ToSharedRef();
    const FDWLanguageUISnapshot& Snapshot = Context->GetSnapshot();
    const FDWLanguageTalentView* Selected = FindTalent(Snapshot, SelectedId);
    if (!Selected)
        return DWLanguageUI::Card(DWLanguageUI::Text(C,
            DWLanguageUI::L(C, TEXT("选择一项天赋查看详情"), TEXT("Select a talent to inspect")), 22, DWLanguageUI::Ink(), true), FMargin(28));

    TSharedRef<SVerticalBox> Requirements = SNew(SVerticalBox);
    if (!Selected->PrerequisiteIds.IsEmpty())
    {
        Requirements->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
        [DWLanguageUI::Text(C, DWLanguageUI::L(C, TEXT("所需前置天赋（全部）"), TEXT("Required talents (all)")), 16, DWLanguageUI::Muted(), true)];
        for (FName Id : Selected->PrerequisiteIds)
        {
            const FDWLanguageTalentView* Predecessor = FindTalent(Snapshot, Id);
            const FText Label = Predecessor ? TalentDisplayName(C, *Predecessor)
                : DWLanguageUI::L(C, TEXT("前置天赋信息不可用"), TEXT("Requirement unavailable"));
            Requirements->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
                [DWLanguageUI::Text(C, FText::FromString(Predecessor && Predecessor->bLearned ? TEXT("✓") : TEXT("·")), 18, DWLanguageUI::Ink())]
                + SHorizontalBox::Slot().FillWidth(1)[DWLanguageUI::Text(C, Label, 17, DWLanguageUI::Ink(), true)]];
        }
    }
    const FText ActionReason = IsUndefinedTalent(Selected->Id) ? UndefinedTalentReason(C)
        : (Selected->bLearned ? Selected->ForgetBlockedReason : Selected->LearnBlockedReason);
    TSharedRef<SHorizontalBox> Actions = SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 6, 0)
            [DWLanguageUI::Button(C, DWLanguageUI::L(C, TEXT("学习"), TEXT("Learn")),
                FOnClicked::CreateLambda([WeakThis = TWeakPtr<SDWTalentPanel>(SharedThis(this))]()
                { const auto P = WeakThis.Pin(); return P ? P->RequestChange(false) : FReply::Handled(); }), 110,
                !bRequestPending && !Selected->Id.IsNone() && !IsUndefinedTalent(Selected->Id) && Selected->bCanLearn && !Selected->bLearned)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(6, 0, 0, 0)
            [DWLanguageUI::Button(C, DWLanguageUI::L(C, TEXT("遗忘"), TEXT("Forget")),
                FOnClicked::CreateLambda([WeakThis = TWeakPtr<SDWTalentPanel>(SharedThis(this))]()
                { const auto P = WeakThis.Pin(); return P ? P->RequestChange(true) : FReply::Handled(); }), 110,
                !bRequestPending && !Selected->Id.IsNone() && !IsUndefinedTalent(Selected->Id) && Selected->bCanForget && Selected->bLearned)];
    TSharedRef<SWidget> Reason = DWLanguageUI::Text(C, ActionReason, 16, DWLanguageUI::Muted(), true);
    Reason->SetVisibility(ActionReason.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
    Requirements->SetVisibility(Selected->PrerequisiteIds.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
    TSharedRef<SVerticalBox> Details = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)
        [Context->Glyph(Selected->Icon, FVector2D(88, 88))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [DWLanguageUI::Text(C, TalentDisplayName(C, *Selected), 26, DWLanguageUI::Ink(), true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
        [DWLanguageUI::Text(C, Selected->Description, 19, DWLanguageUI::Ink(), true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
        [DWLanguageUI::Text(C, FText::Format(DWLanguageUI::L(C, TEXT("所需点数：{0}"), TEXT("Cost: {0} points")),
            FText::AsNumber(Selected->Cost)), 20, DWLanguageUI::Ink())]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, Selected->PrerequisiteIds.IsEmpty() ? 0.f : 14.f)[Requirements]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, ActionReason.IsEmpty() ? 0.f : 16.f)[Reason];
    return DWLanguageUI::Card(SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1)
        [SNew(SScrollBox) + SScrollBox::Slot()[Details]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)[Actions], FMargin(26));
}

FReply SDWTalentPanel::RequestChange(bool bForget)
{
    if (bRequestPending) return FReply::Handled();
    const FDWLanguageTalentView* Selected = FindTalent(Context->GetSnapshot(), SelectedId);
    if (!Selected || Selected->Id.IsNone()) return FReply::Handled();
    if (IsUndefinedTalent(Selected->Id))
    {
        Context->Notice(UndefinedTalentReason(Context.ToSharedRef()));
        return FReply::Handled(); // Reserved definitions cannot submit even if a stale view says eligible.
    }
    const bool bAllowed = bForget ? Selected->bLearned && Selected->bCanForget
        : !Selected->bLearned && Selected->bCanLearn;
    if (!bAllowed)
    {
        Context->Notice(bForget ? Selected->ForgetBlockedReason : Selected->LearnBlockedReason);
        return FReply::Handled();
    }
    FDWLanguageUIRequest Request;
    Request.Kind = bForget ? EDWLanguageUIRequestKind::ForgetTalent : EDWLanguageUIRequestKind::LearnTalent;
    Request.TargetId = Selected->Id;
    const TSharedRef<SDWTalentPanel> KeepAlive = SharedThis(this);
    bRequestPending = true;
    Context->Request(MoveTemp(Request)); // Authority validates, spends/refunds points and publishes effects.
    bRequestPending = false;
    Refresh();
    return FReply::Handled();
}

void SDWTalentPanel::Refresh()
{
    if (!Context) return;
    const auto C = Context.ToSharedRef();
    const FDWLanguageUISnapshot& Snapshot = Context->GetSnapshot();
    if (SessionId != Snapshot.SessionId)
    {
        SessionId = Snapshot.SessionId;
        SelectedId = NAME_None;
    }
    if (!FindTalent(Snapshot, SelectedId))
        SelectedId = Snapshot.Talents.IsEmpty() ? NAME_None : Snapshot.Talents[0].Id;
    ChildSlot
    [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 16)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)
            [DWLanguageUI::Text(C, DWLanguageUI::L(C, TEXT("天赋"), TEXT("Talents")), 28, DWLanguageUI::Ink())]
            + SHorizontalBox::Slot().AutoWidth()
            [DWLanguageUI::Text(C, FText::Format(DWLanguageUI::L(C, TEXT("可用点数：{0}"), TEXT("Available points: {0}")),
                FText::AsNumber(Snapshot.TalentPoints)), 23, DWLanguageUI::Ink())]]
        + SVerticalBox::Slot().FillHeight(1)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(.62f).Padding(0, 0, 12, 0)
            [Snapshot.Talents.IsEmpty()
                ? DWLanguageUI::Card(DWLanguageUI::Text(C, DWLanguageUI::L(C, TEXT("暂无天赋数据"), TEXT("No talent data available")),
                    22, DWLanguageUI::Muted(), true), FMargin(28))
                : MakeGraph()]
            + SHorizontalBox::Slot().FillWidth(.38f).Padding(12, 0, 0, 0)[MakeDetails()]]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 14, 8, 0)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [DWLanguageUI::Button(C, DWLanguageUI::L(C, TEXT("返回"), TEXT("Back")),
                FOnClicked::CreateLambda([WeakContext = TWeakPtr<FDWLanguageUIContext>(C)]()
                { if (const auto P = WeakContext.Pin()) P->CloseLayer(); return FReply::Handled(); }), 136)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0, 24, 0)
            [DWLanguageUI::Text(C, DWLanguageUI::L(C, TEXT("Esc · 返回"), TEXT("Esc · Back")), 16, DWLanguageUI::Muted())]]];
}
