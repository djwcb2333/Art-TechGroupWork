#include "DWFrontendAuthoringLibrary.h"
#include "DWFrontendWidget.h"

namespace
{
    FString FrontendBuildReport;
}

FString UDWFrontendAuthoringLibrary::GetLastBuildReport()
{
    return FrontendBuildReport;
}

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Font.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/MovieScene2DTransformSection.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Evaluation/MovieSceneCompletionMode.h"

namespace
{
    constexpr int32 AnimationTicksPerSecond = 6000;
    const FString FrontendFolder = TEXT("/Game/DoughWorld/Maps/Gameplay/Frontend/UI");
    const FString FrontendName = TEXT("WBP_DWFrontend");
    const FLinearColor BreadInk(.19f, .085f, .024f, 1.f);
    const FLinearColor BreadGold(.96f, .59f, .14f, 1.f);
    const FLinearColor Cream(1.f, .95f, .80f, 1.f);

    struct FCurveKey
    {
        float Time;
        float Value;
    };

    FFrameNumber Frame(float Seconds)
    {
        return FFrameNumber(FMath::RoundToInt(Seconds * AnimationTicksPerSecond));
    }

    void Curve(FMovieSceneFloatChannel& Channel, std::initializer_list<FCurveKey> Keys, float Default)
    {
        Channel.SetDefault(Default);
        for (const FCurveKey& Key : Keys)
        {
            Channel.AddCubicKey(Frame(Key.Time), Key.Value, RCTM_Auto);
        }
        Channel.AutoSetTangents();
    }

    /** Native UMG animation data, as authored by the Designer timeline. */
    struct FTimeline
    {
        UWidgetAnimation* Animation;
        UMovieScene* Scene;
        float Duration;

        FTimeline(UWidgetBlueprint* BP, const TCHAR* Name, float InDuration)
            : Animation(NewObject<UWidgetAnimation>(BP, FName(Name), RF_Transactional))
            , Scene(NewObject<UMovieScene>(Animation, FName(Name), RF_Transactional))
            , Duration(InDuration)
        {
            Animation->MovieScene = Scene;
            Animation->SetDisplayLabel(Name);
            Scene->SetTickResolutionDirectly(FFrameRate(AnimationTicksPerSecond, 1));
            Scene->SetDisplayRate(FFrameRate(60, 1));
            Scene->SetPlaybackRange(FFrameNumber(0), Frame(Duration).Value + 1);
            Scene->GetEditorData().WorkStart = 0;
            Scene->GetEditorData().WorkEnd = Duration + .15;
            Scene->GetEditorData().ViewStart = -.05;
            Scene->GetEditorData().ViewEnd = Duration + .15;
            BP->Animations.Add(Animation);
        }

        FGuid Binding(UWidget* Widget)
        {
            for (const FWidgetAnimationBinding& Existing : Animation->AnimationBindings)
            {
                if (Existing.WidgetName == Widget->GetFName())
                {
                    return Existing.AnimationGuid;
                }
            }
            const FGuid Id = Scene->AddPossessable(Widget->GetName(), Widget->GetClass());
            FWidgetAnimationBinding Binding;
            Binding.WidgetName = Widget->GetFName();
            Binding.SlotWidgetName = NAME_None;
            Binding.AnimationGuid = Id;
            Binding.bIsRootWidget = false;
            Animation->AnimationBindings.Add(Binding);
            return Id;
        }

        void Opacity(UWidget* Widget, std::initializer_list<FCurveKey> Keys)
        {
            UMovieSceneFloatTrack* Track = Scene->AddTrack<UMovieSceneFloatTrack>(Binding(Widget));
            Track->SetPropertyNameAndPath(TEXT("RenderOpacity"), TEXT("RenderOpacity"));
            UMovieSceneFloatSection* Section = CastChecked<UMovieSceneFloatSection>(Track->CreateNewSection());
            Section->SetRange(TRange<FFrameNumber>(Frame(0), Frame(Duration) + 1));
            Section->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
            Curve(Section->GetChannel(), Keys, 1.f);
            Track->AddSection(*Section);
        }

        void Transform(UWidget* Widget, std::initializer_list<FCurveKey> Y,
                       std::initializer_list<FCurveKey> Scale)
        {
            UMovieScene2DTransformTrack* Track = Scene->AddTrack<UMovieScene2DTransformTrack>(Binding(Widget));
            Track->SetPropertyNameAndPath(TEXT("RenderTransform"), TEXT("RenderTransform"));
            UMovieScene2DTransformSection* Section = CastChecked<UMovieScene2DTransformSection>(Track->CreateNewSection());
            Section->SetRange(TRange<FFrameNumber>(Frame(0), Frame(Duration) + 1));
            Section->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
            Section->SetMask(FMovieScene2DTransformMask(EMovieScene2DTransformChannel::Translation | EMovieScene2DTransformChannel::Scale));
            Section->Translation[0].SetDefault(0.f);
            Curve(Section->Translation[1], Y, 0.f);
            Curve(Section->Scale[0], Scale, 1.f);
            Curve(Section->Scale[1], Scale, 1.f);
            Section->Rotation.SetDefault(0.f);
            Section->Shear[0].SetDefault(0.f);
            Section->Shear[1].SetDefault(0.f);
            Track->AddSection(*Section);
        }
    };

    template<class T>
    T* Make(UWidgetTree* Tree, const TCHAR* Name)
    {
        T* Widget = Tree->ConstructWidget<T>(T::StaticClass(), FName(Name));
        Widget->bIsVariable = true;
        return Widget;
    }

    UFont* FrontendFont()
    {
        UFont* Font = LoadObject<UFont>(nullptr, TEXT("/Game/DoughWorld/Maps/Gameplay/UI/Interaction/Fonts/F_DWHandDrawn.F_DWHandDrawn"));
        return Font ? Font : LoadObject<UFont>(nullptr, TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    }

    void StyleText(UTextBlock* Text, int32 Size, const FLinearColor& Color, bool bShadow = false)
    {
        if (!Text) return;
        FSlateFontInfo Font = Text->GetFont();
        if (UFont* FontAsset = FrontendFont())
        {
            Font.FontObject = FontAsset;
            Font.CompositeFont.Reset();
            Font.TypefaceFontName = TEXT("Default");
        }
        Font.Size = Size;
        Font.OutlineSettings.OutlineSize = bShadow ? 2 : 0;
        Font.OutlineSettings.OutlineColor = FLinearColor(.19f, .075f, .014f, .88f);
        Text->SetFont(Font);
        Text->SetColorAndOpacity(FSlateColor(Color));
        Text->SetAutoWrapText(false);
        Text->SetJustification(ETextJustify::Center);
        Text->SetShadowOffset(bShadow ? FVector2D(0, 3) : FVector2D::ZeroVector);
        Text->SetShadowColorAndOpacity(FLinearColor(.15f, .075f, .025f, .2f));
    }

    UTextBlock* MakeText(UWidgetTree* Tree, const TCHAR* Name, const TCHAR* Value,
                         int32 Size, const FLinearColor& Color)
    {
        UTextBlock* Text = Make<UTextBlock>(Tree, Name);
        Text->SetText(FText::FromString(Value));
        StyleText(Text, Size, Color);
        Text->SetVisibility(ESlateVisibility::HitTestInvisible);
        return Text;
    }

    UCanvasPanelSlot* Position(UCanvasPanel* Root, UWidget* Widget, FAnchors Anchors,
                              FMargin Offsets, FVector2D Alignment, int32 Z)
    {
        UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Widget);
        Slot->SetAnchors(Anchors);
        Slot->SetAlignment(Alignment);
        Slot->SetOffsets(Offsets);
        Slot->SetZOrder(Z);
        return Slot;
    }

    void StyleButton(UButton* Button, bool bPrimary, bool bSkip = false)
    {
        if (!Button) return;
        FButtonStyle Style = Button->GetStyle();
        const FLinearColor Fill = bSkip ? FLinearColor(1, 1, 1, .035f) :
            (bPrimary ? BreadGold : FLinearColor(1.f, .94f, .80f, .91f));
        const FLinearColor Edge = bSkip ? FLinearColor(1, 1, 1, .30f) :
            (bPrimary ? FLinearColor(.47f, .21f, .05f, .65f) : FLinearColor(.65f, .39f, .14f, .45f));
        Style.SetNormal(FSlateRoundedBoxBrush(Fill, 13.f, Edge, 1.25f));
        Style.SetHovered(FSlateRoundedBoxBrush(bSkip ? FLinearColor(1, 1, 1, .13f) : FLinearColor(1.f, .76f, .34f, 1.f), 13.f, Edge, 1.5f));
        Style.SetPressed(FSlateRoundedBoxBrush(bSkip ? FLinearColor(1, 1, 1, .20f) : FLinearColor(.84f, .46f, .11f, 1.f), 13.f, Edge, 1.25f));
        Style.SetNormalPadding(FMargin(20, 11));
        Style.SetPressedPadding(FMargin(20, 13, 20, 9));
        Button->SetStyle(Style);
        Button->SetBackgroundColor(FLinearColor::White);
        Button->SetColorAndOpacity(FLinearColor::White);
        Button->SetRenderTransformPivot(FVector2D(.5f, .5f));
        if (UTextBlock* Label = Cast<UTextBlock>(Button->GetContent()))
        {
            StyleText(Label, bSkip ? 15 : (bPrimary ? 24 : 21), bSkip ? Cream : BreadInk);
        }
        if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Button->Slot))
        {
            Slot->SetHorizontalAlignment(HAlign_Fill);
            Slot->SetPadding(FMargin(10, 6, 10, 6));
        }
    }

    void RestyleTitle(UWidgetTree* Tree)
    {
        UBorder* MenuRoot = CastChecked<UBorder>(Tree->FindWidget(TEXT("MenuRoot")));
        MenuRoot->SetBrushColor(FLinearColor::Transparent);
        MenuRoot->SetPadding(FMargin(0));
        UBorder* TitlePage = CastChecked<UBorder>(Tree->FindWidget(TEXT("TitlePage")));
        TitlePage->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 0.f));
        TitlePage->SetBrushColor(FLinearColor::Transparent);
        TitlePage->SetPadding(FMargin(0));
        TitlePage->SetRenderTransformPivot(FVector2D(.5f, .5f));
        if (USizeBox* Size = Cast<USizeBox>(Tree->FindWidget(TEXT("TitlePage_Size"))))
        {
            Size->SetWidthOverride(470.f);
            Size->SetMaxDesiredHeight(690.f);
        }
        if (UScrollBox* Scroll = Cast<UScrollBox>(Tree->FindWidget(TEXT("TitlePage_Scroll"))))
        {
            Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
            Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::Never);
        }
        if (UOverlaySlot* Slot = Cast<UOverlaySlot>(TitlePage->Slot))
        {
            Slot->SetHorizontalAlignment(HAlign_Left);
            Slot->SetVerticalAlignment(VAlign_Center);
            Slot->SetPadding(FMargin(90.f, 15.f, 40.f, 15.f));
        }
        UWidget* Overlay = Tree->FindWidget(TEXT("TitlePageOverlay"));
        if (Overlay)
        {
            if (UWidgetSwitcherSlot* Slot = Cast<UWidgetSwitcherSlot>(Overlay->Slot))
            {
                Slot->SetHorizontalAlignment(HAlign_Fill);
                Slot->SetVerticalAlignment(VAlign_Fill);
            }
        }
        StyleText(Cast<UTextBlock>(Tree->FindWidget(TEXT("SubtitleText"))), 18, BreadInk);
        StyleText(Cast<UTextBlock>(Tree->FindWidget(TEXT("TitleText"))), 66, FLinearColor(1.f, .76f, .29f, 1.f), true);
        StyleText(Cast<UTextBlock>(Tree->FindWidget(TEXT("Tagline"))), 18, BreadInk);
        for (const TCHAR* Name : {TEXT("SubtitleText"), TEXT("TitleText"), TEXT("Tagline")})
        {
            if (UWidget* Widget = Tree->FindWidget(Name))
            {
                if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Widget->Slot))
                {
                    Slot->SetHorizontalAlignment(HAlign_Center);
                    Slot->SetPadding(FMargin(0, FCString::Strcmp(Name, TEXT("TitleText")) == 0 ? 11.f : 7.f));
                }
            }
        }
        UVerticalBox* Body = CastChecked<UVerticalBox>(Tree->FindWidget(TEXT("TitlePage_Body")));
        USizeBox* AccentSize = Make<USizeBox>(Tree, TEXT("FrontendTitleAccentSize"));
        AccentSize->SetWidthOverride(68.f);
        AccentSize->SetHeightOverride(4.f);
        UBorder* Accent = Make<UBorder>(Tree, TEXT("FrontendTitleAccent"));
        Accent->SetBrush(FSlateRoundedBoxBrush(BreadGold, 2.f));
        Accent->SetPadding(FMargin(0));
        Accent->SetVisibility(ESlateVisibility::HitTestInvisible);
        AccentSize->SetContent(Accent);
        if (UVerticalBoxSlot* Slot = Cast<UVerticalBoxSlot>(Body->InsertChildAt(3, AccentSize)))
        {
            Slot->SetHorizontalAlignment(HAlign_Center);
            Slot->SetPadding(FMargin(0, 12, 0, 22));
        }
        StyleButton(Cast<UButton>(Tree->FindWidget(TEXT("StartButton"))), true);
        StyleButton(Cast<UButton>(Tree->FindWidget(TEXT("TitleSettingsButton"))), false);
        StyleButton(Cast<UButton>(Tree->FindWidget(TEXT("QuitButton"))), false);
    }

    void AddFrontendLayers(UWidgetTree* Tree, UCanvasPanel* Root)
    {
        UBackgroundBlur* Blur = Make<UBackgroundBlur>(Tree, TEXT("MenuBlur"));
        Blur->SetBlurStrength(0.f);
        Blur->SetApplyAlphaToBlur(true);
        Blur->SetPadding(FMargin(0));
        Blur->SetVisibility(ESlateVisibility::HitTestInvisible);
        Blur->SetLowQualityFallbackBrush(FSlateRoundedBoxBrush(FLinearColor(1.f, .94f, .83f, .07f), 0.f));
        // Canvas z-order is independent of construction order: blur stays below
        // every original HUD/menu child, so it can never blur the new text.
        Position(Root, Blur, FAnchors(0, 0, 1, 1), FMargin(0), FVector2D::ZeroVector, -20);

        UBorder* Black = Make<UBorder>(Tree, TEXT("BlackLayer"));
        Black->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Black, 0.f));
        Black->SetBrushColor(FLinearColor::Black);
        Black->SetPadding(FMargin(0));
        Black->SetVisibility(ESlateVisibility::Collapsed);
        Position(Root, Black, FAnchors(0, 0, 1, 1), FMargin(0), FVector2D::ZeroVector, 1000);

        UBorder* Logo = Make<UBorder>(Tree, TEXT("LogoLayer"));
        Logo->SetBrushColor(FLinearColor::Transparent);
        Logo->SetPadding(FMargin(0));
        Logo->SetRenderTransformPivot(FVector2D(.5f, .5f));
        Logo->SetVisibility(ESlateVisibility::Collapsed);
        Position(Root, Logo, FAnchors(.5f, .5f), FMargin(0, -14, 720, 240), FVector2D(.5f, .5f), 1010);
        UCanvasPanel* LogoContent = Make<UCanvasPanel>(Tree, TEXT("FrontendLogoContent"));
        Logo->SetContent(LogoContent);
        UImage* LogoImage = Make<UImage>(Tree, TEXT("LogoImage"));
        LogoImage->SetVisibility(ESlateVisibility::Collapsed);
        Position(LogoContent, LogoImage, FAnchors(.5f, .5f), FMargin(0, -20, 500, 152), FVector2D(.5f, .5f), 0);
        UTextBlock* LogoText = MakeText(Tree, TEXT("LogoText"), TEXT("YOUR TEAM"), 62, Cream);
        Position(LogoContent, LogoText, FAnchors(.5f, .5f), FMargin(0, -18, 700, 90), FVector2D(.5f, .5f), 1);
        UTextBlock* Presents = MakeText(Tree, TEXT("FrontendPresentsText"), TEXT("P R E S E N T S"), 15, FLinearColor(.71f, .56f, .34f, 1.f));
        Position(LogoContent, Presents, FAnchors(.5f, .5f), FMargin(0, 59, 700, 28), FVector2D(.5f, .5f), 2);

        UButton* Skip = Make<UButton>(Tree, TEXT("SkipIntroButton"));
        UTextBlock* SkipText = MakeText(Tree, TEXT("SkipIntroButton_Label"), TEXT("跳过 / Skip"), 15, Cream);
        Skip->SetContent(SkipText);
        StyleButton(Skip, false, true);
        Skip->SetVisibility(ESlateVisibility::Collapsed);
        Position(Root, Skip, FAnchors(1, 1), FMargin(-36, -30, 166, 45), FVector2D(1, 1), 1020);
    }

    void AuthorTimelines(UWidgetBlueprint* BP)
    {
        UWidgetTree* Tree = BP->WidgetTree;
        UWidget* Logo = Tree->FindWidget(TEXT("LogoLayer"));
        UWidget* Black = Tree->FindWidget(TEXT("BlackLayer"));
        UWidget* Title = Tree->FindWidget(TEXT("TitlePage"));
        FTimeline In(BP, TEXT("Anim_LogoIn"), .55f);
        In.Opacity(Logo, {{0, 0}, {.36f, 1}, {.55f, 1}});
        In.Transform(Logo, {{0, 12}, {.36f, -2}, {.55f, 0}}, {{0, .85f}, {.36f, 1.04f}, {.55f, 1}});
        FTimeline Out(BP, TEXT("Anim_LogoOut"), .4f);
        Out.Opacity(Logo, {{0, 1}, {.4f, 0}});
        FTimeline Reveal(BP, TEXT("Anim_Reveal"), .65f);
        Reveal.Opacity(Black, {{0, 1}, {.65f, 0}});
        FTimeline Menu(BP, TEXT("Anim_MenuEnter"), .9f);
        Menu.Opacity(Title, {{0, 0}, {.24f, 1}, {.9f, 1}});
        Menu.Transform(Title, {{0, 48}, {.40f, -8}, {.64f, 3}, {.9f, 0}},
                             {{0, .95f}, {.40f, 1.018f}, {.64f, .996f}, {.9f, 1}});
        const TCHAR* ButtonNames[] = {TEXT("StartButton"), TEXT("TitleSettingsButton"), TEXT("QuitButton")};
        for (int32 Index = 0; Index < 3; ++Index)
        {
            UWidget* Button = Tree->FindWidget(ButtonNames[Index]);
            const float Delay = .08f + Index * .08f;
            Menu.Opacity(Button, {{0, 0}, {Delay, 0}, {Delay + .23f, 1}, {.9f, 1}});
            Menu.Transform(Button, {{0, 18}, {Delay, 18}, {Delay + .29f, -3}, {Delay + .46f, 0}, {.9f, 0}},
                                   {{0, .97f}, {Delay, .97f}, {Delay + .29f, 1.012f}, {Delay + .46f, 1}, {.9f, 1}});
        }
    }

    bool SaveFrontend(UWidgetBlueprint* BP)
    {
        FBlueprintEditorUtils::RefreshAllNodes(BP);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        FKismetEditorUtilities::CompileBlueprint(BP);
        if (BP->Status == BS_Error || !BP->GeneratedClass)
        {
            FrontendBuildReport += TEXT("ERROR: frontend Widget Blueprint compile failed. Original WBP was not modified.\n");
            return false;
        }
        BP->MarkPackageDirty();
        const FString Filename = FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(BP->GetOutermost(), BP, *Filename, Args);
    }
}
#endif

bool UDWFrontendAuthoringLibrary::CreateFrontendUIAssets()
{
    FrontendBuildReport.Reset();
#if WITH_EDITOR
    const FString Destination = FrontendFolder + TEXT("/") + FrontendName;
    const FString DestinationObject = Destination + TEXT(".") + FrontendName;
    if (LoadObject<UWidgetBlueprint>(nullptr, *DestinationObject))
    {
        FrontendBuildReport = TEXT("PRESERVED: existing frontend Designer asset and timelines were left unchanged: ") + Destination;
        return true;
    }
    // A failed LoadObject can leave an empty UPackage in memory. That is not an
    // authored asset and is safe for DuplicateAsset to reuse. Only an existing
    // on-disk package needs this additional no-overwrite guard; an existing
    // Widget Blueprint, including an unsaved one, was handled above.
    if (FPackageName::DoesPackageExist(Destination))
    {
        FrontendBuildReport = TEXT("PRESERVED: destination package already exists; no overwrite attempted: ") + Destination;
        return false;
    }
    UWidgetBlueprint* Source = LoadObject<UWidgetBlueprint>(nullptr, TEXT("/Game/DoughWorld/Maps/Gameplay/UI/WBP_DWGameplay.WBP_DWGameplay"));
    if (!Source || !Source->WidgetTree || !Source->GeneratedClass || !Source->ParentClass ||
        !UDWFrontendWidget::StaticClass()->IsChildOf(Source->ParentClass))
    {
        FrontendBuildReport = TEXT("ERROR: missing or incompatible original WBP_DWGameplay. Nothing was replaced.");
        return false;
    }
    for (const TCHAR* Required : {TEXT("RootCanvas"), TEXT("HUDLayer"), TEXT("MenuRoot"), TEXT("PageSwitcher"),
        TEXT("TitlePage"), TEXT("TitlePageOverlay"), TEXT("TitlePage_Body"), TEXT("StartButton"), TEXT("TitleSettingsButton"), TEXT("QuitButton")})
    {
        if (!Source->WidgetTree->FindWidget(Required))
        {
            FrontendBuildReport = FString::Printf(TEXT("ERROR: source Designer widget %s is absent; original UI preserved."), Required);
            return false;
        }
    }
    TSet<FName> SourceWidgets;
    Source->WidgetTree->ForEachWidget([&](UWidget* Widget) { SourceWidgets.Add(Widget->GetFName()); });
    UWidgetSwitcher* SourceSwitcher = Cast<UWidgetSwitcher>(Source->WidgetTree->FindWidget(TEXT("PageSwitcher")));
    TArray<FName> SourcePages;
    if (!SourceSwitcher || !Cast<UCanvasPanel>(Source->WidgetTree->RootWidget))
    {
        FrontendBuildReport = TEXT("ERROR: source Canvas/Switcher types are incompatible; original UI preserved.");
        return false;
    }
    for (int32 Index = 0; Index < SourceSwitcher->GetChildrenCount(); ++Index)
    {
        SourcePages.Add(SourceSwitcher->GetChildAt(Index)->GetFName());
    }
    for (const TCHAR* NewName : {TEXT("MenuBlur"), TEXT("BlackLayer"), TEXT("LogoLayer"), TEXT("LogoImage"), TEXT("LogoText"), TEXT("SkipIntroButton")})
    {
        if (SourceWidgets.Contains(FName(NewName)))
        {
            FrontendBuildReport = FString::Printf(TEXT("ERROR: source already contains %s; resolve the naming collision in the new example explicitly."), NewName);
            return false;
        }
    }
    const int32 OriginalAnimationCount = Source->Animations.Num();
    UWidgetBlueprint* BP = Cast<UWidgetBlueprint>(FAssetToolsModule::GetModule().Get().DuplicateAsset(FrontendName, FrontendFolder, Source));
    if (!BP || !BP->WidgetTree)
    {
        FrontendBuildReport = TEXT("ERROR: frontend duplication failed; original UI was not changed.");
        return false;
    }
    BP->Modify();
    // Both parents share UDWGameplayWidget. The duplicated graph, bindings,
    // authored defaults and entire Designer tree stay in the same hierarchy.
    BP->ParentClass = UDWFrontendWidget::StaticClass();
    UWidgetTree* Tree = BP->WidgetTree;
    RestyleTitle(Tree);
    AddFrontendLayers(Tree, CastChecked<UCanvasPanel>(Tree->RootWidget));
    AuthorTimelines(BP);
    for (const FName& Name : SourceWidgets)
    {
        if (!Tree->FindWidget(Name))
        {
            FrontendBuildReport = TEXT("ERROR: an original widget was not retained: ") + Name.ToString();
            return false;
        }
    }
    UWidgetSwitcher* Switcher = CastChecked<UWidgetSwitcher>(Tree->FindWidget(TEXT("PageSwitcher")));
    if (Switcher->GetChildrenCount() != SourcePages.Num()) return false;
    for (int32 Index = 0; Index < SourcePages.Num(); ++Index)
    {
        if (Switcher->GetChildAt(Index)->GetFName() != SourcePages[Index])
        {
            FrontendBuildReport = TEXT("ERROR: original menu page order was not retained.");
            return false;
        }
    }
    if (!SaveFrontend(BP))
    {
        FrontendBuildReport += TEXT("ERROR: failed to compile/save the new frontend asset.");
        return false;
    }
    int32 WidgetCount = 0;
    Tree->ForEachWidget([&](UWidget*) { ++WidgetCount; });
    FrontendBuildReport = FString::Printf(TEXT("SAVED %s\nSource preserved: %s\nOriginal widgets retained: %d; final widgets: %d; original menu pages retained: %d.\nAdded 4 editable UMG Designer animations (existing animations: %d); 3D background is supplied by the frontend example level.\n"),
        *BP->GetPathName(), *Source->GetPathName(), SourceWidgets.Num(), WidgetCount, SourcePages.Num(), OriginalAnimationCount);
    return true;
#else
    FrontendBuildReport = TEXT("Frontend asset authoring is only available in the Unreal Editor.");
    return false;
#endif
}
