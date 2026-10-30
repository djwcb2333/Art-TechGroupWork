#include "DWLanguageUICommon.h"
#include "DWLanguageMotionButton.h"
#include "DWC01UIStyle.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SCompoundWidget.h"
#include "Brushes/SlateNoResource.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    /** Reuses the existing Bounce owner; only this journal close control is added. */
    class SDWLanguageCloseAction final : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SDWLanguageCloseAction) {}
            SLATE_ARGUMENT(TSharedPtr<FDWLanguageUIContext>, Context)
            SLATE_EVENT(FOnClicked, OnClicked)
        SLATE_END_ARGS()
        void Construct(const FArguments& InArgs)
        {
            check(InArgs._Context.IsValid());
            const auto C = InArgs._Context.ToSharedRef();
            Anchor.Reset(NewObject<UDWLanguageActionMotion>());
            const TWeakObjectPtr<UDWLanguageActionMotion> WeakMotion(Anchor.Get());
            const FSlateNoResource NoBackground;
            CloseStyle.SetNormal(NoBackground).SetHovered(NoBackground).SetPressed(NoBackground)
                .SetDisabled(NoBackground).SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
            const TSoftObjectPtr<UTexture2D> CloseArt(FSoftObjectPath(TEXT("/Game/DoughWorld/UI/Image/T_DW_C04_CloseBrush.T_DW_C04_CloseBrush")));
            Anchor->SetContent(SNew(SButton).ButtonStyle(&CloseStyle)
                .ContentPadding(FMargin(6)).HAlign(HAlign_Center).VAlign(VAlign_Center)
                .ToolTipText(DWLanguageUI::L(C, TEXT("关闭"), TEXT("Close")))
                .OnClicked(InArgs._OnClicked)
                .OnHovered_Lambda([WeakMotion]() { if (WeakMotion.IsValid()) WeakMotion->Hover(true); })
                .OnUnhovered_Lambda([WeakMotion]() { if (WeakMotion.IsValid()) { WeakMotion->Hover(false); WeakMotion->Press(false); } })
                .OnPressed_Lambda([WeakMotion]() { if (WeakMotion.IsValid()) WeakMotion->Press(true); })
                .OnReleased_Lambda([WeakMotion]() { if (WeakMotion.IsValid()) WeakMotion->Press(false); })
                [C->Glyph(CloseArt, FVector2D(36, 36), DWLanguageUI::Ink())]);
            ChildSlot[SNew(SBox).WidthOverride(48).HeightOverride(48)[Anchor->TakeWidget()]];
        }
        virtual ~SDWLanguageCloseAction() override
        { if (Anchor.IsValid()) Anchor->ReleaseSlateResources(true); }
    private:
        FButtonStyle CloseStyle;
        TStrongObjectPtr<UDWLanguageActionMotion> Anchor;
    };

}

void FDWLanguageUIContext::RefreshSnapshot()
{
    if (Adapter.IsValid()) Snapshot = Adapter->ReadSnapshot();
    else Snapshot = FDWLanguageUISnapshot();
}

FDWLanguageUIReply FDWLanguageUIContext::Request(FDWLanguageUIRequest RequestValue)
{
    FDWLanguageUIReply Reply;
    if (!bAcceptRequests || !Adapter.IsValid() || !Snapshot.bSessionValid)
    {
        Reply.ResultCode = Snapshot.bSessionValid ? FName(TEXT("AdapterUnavailable")) : FName(TEXT("SessionInvalid"));
        Reply.Message = FText::FromString(Snapshot.bEnglish ? TEXT("Interaction unavailable") : TEXT("当前交互不可用"));
        Notice(Reply.Message);
        return Reply;
    }
    RequestValue.SessionId = Snapshot.SessionId;
    RequestValue.ExpectedRevision = Snapshot.Revision;
    if (!RequestValue.RequestId.IsValid()) RequestValue.RequestId = FGuid::NewGuid();
    Reply = Adapter->Request(RequestValue);
    RefreshSnapshot(); // Accepted and rejected requests both re-read authority; no optimistic mutation.
    Notice(Reply.Message);
    if (OnReply) OnReply(RequestValue, Reply);
    return Reply;
}

void FDWLanguageUIContext::OpenJournal(FName EntryId) const { if (OnOpenJournal) OnOpenJournal(EntryId); }
void FDWLanguageUIContext::CloseLayer() const { if (OnCloseLayer) OnCloseLayer(); }
void FDWLanguageUIContext::Notice(const FText& TextValue)
{
    NoticeText = TextValue;
    if (OnNotice) OnNotice(TextValue);
}
FSlateFontInfo FDWLanguageUIContext::Font(int32 Size) const
{
    return FontProvider ? FontProvider(Size) : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
}
TSharedRef<SWidget> FDWLanguageUIContext::Glyph(const TSoftObjectPtr<UTexture2D>& Asset, FVector2D Size, FLinearColor Tint)
{
    const FSoftObjectPath Path = Asset.ToSoftObjectPath();
    TSharedPtr<FSlateBrush>* Cached = GlyphBrushes.Find(Path);
    if (!Cached && !Asset.IsNull())
    {
        if (UTexture2D* Texture = Asset.LoadSynchronous())
        {
            TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
            Brush->SetResourceObject(Texture);
            Brush->ImageSize = FVector2D(Texture->GetSizeX(), Texture->GetSizeY());
            Brush->DrawAs = ESlateBrushDrawType::Image;
            GlyphTextures.Emplace(Texture);
            GlyphBrushes.Add(Path, Brush);
            Cached = GlyphBrushes.Find(Path);
        }
    }
    if (Cached)
    {
        return SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)
            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(SImage).Image(Cached->Get()).ColorAndOpacity(Tint)]];
    }
    // Missing art stays legible and never becomes the old solid-square placeholder.
    return SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y).HAlign(HAlign_Center).VAlign(VAlign_Center)
        [SNew(STextBlock).Text(FText::FromString(TEXT("?"))).Font(Font(FMath::RoundToInt(Size.Y * .55f))).ColorAndOpacity(DWC01UI::Ink())];
}
TSharedRef<SWidget> FDWLanguageUIContext::EntryGlyph(FName EntryId, FVector2D Size)
{
    const FDWLanguageUIEntryView* Entry = Snapshot.FindEntry(EntryId);
    // Check discovery before touching the asset, including defensive handling of unsanitized snapshots.
    return Glyph(Entry && Entry->bDiscovered ? Entry->Glyph : TSoftObjectPtr<UTexture2D>(), Size, DWC01UI::Ink());
}

namespace DWLanguageUI
{
TSharedRef<SWidget> CloseButton(const TSharedRef<FDWLanguageUIContext>& Context, FOnClicked OnClicked)
{ return SNew(SDWLanguageCloseAction).Context(Context).OnClicked(OnClicked); }

    FText L(const TSharedRef<FDWLanguageUIContext>& C, const TCHAR* Chinese, const TCHAR* English)
    { return FText::FromString(C->GetSnapshot().bEnglish ? English : Chinese); }
    FLinearColor Paper() { return DWC01UI::Paper(); }
    FLinearColor Ink() { return DWC01UI::Ink(); }
    FLinearColor Muted() { return DWC01UI::Muted(); }
    FLinearColor Gold() { return DWC01UI::Gold(); }
    const FSlateBrush* PaperBrush() { static FSlateBrush B = DWC01UI::Texture(TEXT("T_DW_C01_Menu_Paper_1160x650"), FVector2D(1160, 650)); return &B; }
    const FSlateBrush* InsetBrush() { static FSlateBrush B = DWC01UI::Surface(Paper(), FVector2D(400, 180)); return &B; }
    const FButtonStyle& ButtonStyle()
    {
        static FButtonStyle Style = FButtonStyle()
            .SetNormal(DWC01UI::Surface(Paper()))
            .SetHovered(DWC01UI::Surface(FLinearColor::LerpUsingHSV(Paper(), FLinearColor::White, .16f)))
            .SetPressed(DWC01UI::Surface(Gold()))
            .SetDisabled(DWC01UI::Surface(Muted()))
            .SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
        return Style;
    }
    const FEditableTextBoxStyle& NoteStyle()
    {
        static FEditableTextBoxStyle Style = FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>(TEXT("NormalEditableTextBox")))
            .SetBackgroundImageNormal(DWC01UI::Surface(Paper(), FVector2D(400, 52)))
            .SetBackgroundImageHovered(DWC01UI::Surface(Paper(), FVector2D(400, 52)))
            .SetBackgroundImageFocused(DWC01UI::Surface(Paper(), FVector2D(400, 52)))
            .SetBackgroundImageReadOnly(DWC01UI::Surface(Paper(), FVector2D(400, 52)))
            .SetForegroundColor(Ink()).SetReadOnlyForegroundColor(Muted()).SetBackgroundColor(FLinearColor::White);
        return Style;
    }
    TSharedRef<SWidget> Text(const TSharedRef<FDWLanguageUIContext>& C, const FText& Value, int32 Size, FLinearColor Color, bool bWrap)
    { return SNew(STextBlock).Text(Value).Font(C->Font(Size)).ColorAndOpacity(Color).AutoWrapText(bWrap); }
    TSharedRef<SWidget> Button(const TSharedRef<FDWLanguageUIContext>& C, const FText& Label,
        FOnClicked OnClicked, float MinWidth, bool bEnabled, bool bSelected)
    {
        return MotionButton(C, Label, OnClicked, MinWidth, bEnabled, bSelected);
    }
    TSharedRef<SWidget> Card(const TSharedRef<SWidget>& Child, FMargin Padding, bool bSelected)
    {
        return SNew(SBorder).BorderImage(InsetBrush()).BorderBackgroundColor(bSelected ? Gold() / Paper() : FLinearColor::White)
            .Padding(Padding)[Child];
    }
    int32 CountCharacters(const FString& Value)
    {
        int32 Count = 0;
        for (int32 I = 0; I < Value.Len(); ++I, ++Count)
        {
            const uint32 C = static_cast<uint32>(Value[I]);
            if (C >= 0xD800 && C <= 0xDBFF && I + 1 < Value.Len())
            {
                const uint32 Next = static_cast<uint32>(Value[I + 1]);
                if (Next >= 0xDC00 && Next <= 0xDFFF) ++I;
            }
        }
        return Count;
    }
    bool IsValidNote(const FString& Value, int32 MaxCharacters)
    {
        if (CountCharacters(Value) > FMath::Max(1, MaxCharacters)) return false;
        for (int32 I = 0; I < Value.Len(); ++I)
        {
            const uint32 C = static_cast<uint32>(Value[I]);
            if (C >= 0xD800 && C <= 0xDBFF)
            {
                if (++I >= Value.Len()) return false;
                const uint32 Next = static_cast<uint32>(Value[I]);
                if (Next < 0xDC00 || Next > 0xDFFF) return false;
            }
            else if (C >= 0xDC00 && C <= 0xDFFF) return false;
        }
        return true;
    }
}
