#include "DWGameplayWidget.h"
#include "DWSettingsPanel.h"
#include "DWLocalizationLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Brushes/SlateNoResource.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Slider.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "DWC01UIStyle.h"
#include "DWC04SettingsPresentation.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Brushes/SlateColorBrush.h"

namespace
{
const TCHAR* BookTabs[]={TEXT("SettingsBookGraphicsTabButton"),TEXT("SettingsBookAudioTabButton"),
    TEXT("SettingsBookKeysTabButton"),TEXT("SettingsBookControllerTabButton"),TEXT("SettingsBookGeneralTabButton")};
const TCHAR* BookCN[]={TEXT("图像"),TEXT("声音"),TEXT("按键"),TEXT("控制器"),TEXT("常规")};
const TCHAR* BookEN[]={TEXT("Graphics"),TEXT("Audio"),TEXT("Keys"),TEXT("Controls"),TEXT("General")};
void LayoutC04Settings(UDWGameplayWidget* Screen)
{
    if(!Screen||!Screen->WidgetTree)return;
    UWidgetTree* Tree=Screen->WidgetTree;
    auto* Paper=Cast<UBorder>(Tree->FindWidget(TEXT("SettingsBookPaper")));
    if(!Paper)return;
    if(!Tree->FindWidget(TEXT("C04SettingsLayout")))
    {
        // Keep every authored interactive widget. Only their runtime parents change.
        UWidget* OriginalRoot=Paper->GetContent();
        UWidget* Heading=Tree->FindWidget(TEXT("Settings_Heading"));
        UWidget* Back=Tree->FindWidget(TEXT("SettingsBookBackWidth"));
        UWidget* Tabs=Tree->FindWidget(TEXT("SettingsBookTabs"));
        UWidget* Content=Tree->FindWidget(TEXT("SettingsBookContentPaper"));
        UWidget* Actions=Tree->FindWidget(TEXT("SettingsBookActions"));
        if(!OriginalRoot||!Heading||!Back||!Tabs||!Content||!Actions)return;
        auto* Canvas=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("C04SettingsLayout"));
        const auto Place=[Canvas](UWidget* Widget,FVector2D Position,FVector2D Size)
        {
            Widget->RemoveFromParent();auto* Cell=Canvas->AddChildToCanvas(Widget);
            Cell->SetAutoSize(false);Cell->SetPosition(Position);Cell->SetSize(Size);
        };
        Place(Heading,FVector2D(40,60),FVector2D(210,48));
        Place(Back,FVector2D(40,122),FVector2D(180,48));
        Place(Tabs,FVector2D(280,60),FVector2D(920,44));
        Place(Content,FVector2D(280,144),FVector2D(920,530));
        Place(Actions,FVector2D(280,704),FVector2D(920,48));
        // Unknown user additions stay connected in the spare sidebar area.
        Place(OriginalRoot,FVector2D(40,194),FVector2D(210,470));
        Paper->SetContent(Canvas);
    }
    for(const TCHAR* Name:{TEXT("SettingsPage"),TEXT("SettingsBookCover"),TEXT("SettingsBookPaper"),TEXT("SettingsBookContentPaper")})
        if(auto* Border=Cast<UBorder>(Tree->FindWidget(Name)))
        {Border->SetBrush(FSlateNoResource());Border->SetBrushColor(FLinearColor::White);Border->SetPadding(FMargin(0));}
    if(auto* Size=Cast<USizeBox>(Tree->FindWidget(TEXT("SettingsPage_Size"))))
    {Size->SetWidthOverride(1240);Size->SetHeightOverride(780);Size->SetMaxDesiredHeight(780);}
    if(UWidget* Description=Tree->FindWidget(TEXT("Settings_Description")))Description->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* Heading=Cast<UTextBlock>(Tree->FindWidget(TEXT("Settings_Heading"))))DWC04Settings::Label(Heading,30);
    for(const TCHAR* Name:{TEXT("SettingsBookBackWidth"),TEXT("SettingsBookApplyWidth")})
        if(auto* Width=Cast<USizeBox>(Tree->FindWidget(Name)))
        {Width->SetWidthOverride(FString(Name)==TEXT("SettingsBookBackWidth")?180:280);Width->SetHeightOverride(48);}
    if(auto* Scroll=Cast<UScrollBox>(Tree->FindWidget(TEXT("SettingsPage_Scroll"))))
    {
        Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        if(UWidget* Pages=Tree->FindWidget(TEXT("SettingsBookPages")))
            if(auto* Cell=Cast<UScrollBoxSlot>(Pages->Slot)){Cell->SetPadding(FMargin(0));Cell->SetHorizontalAlignment(HAlign_Fill);}
    }
    DWC04Settings::ExistingPair(Tree,TEXT("SettingsBookResolutionRow"),TEXT("ResolutionLabel"),TEXT("ResolutionCombo"));
    DWC04Settings::ExistingPair(Tree,TEXT("SettingsBookWindowModeRow"),TEXT("WindowModeLabel"),TEXT("WindowModeCombo"));
    DWC04Settings::ExistingPair(Tree,TEXT("SettingsBookQualityRow"),TEXT("QualityLabel"),TEXT("QualityCombo"));
    DWC04Settings::ExistingPair(Tree,TEXT("SettingsBookLanguageRow"),TEXT("LanguageLabel"),TEXT("LanguageCombo"));
    for(const auto& Spec:{TPair<const TCHAR*,const TCHAR*>(TEXT("VSyncCheck"),TEXT("SettingsPage_Body")),
        TPair<const TCHAR*,const TCHAR*>(TEXT("CombatEnemyHealthCheck"),TEXT("SettingsBookGeneralPage"))})
    {
        auto* Check=Cast<UCheckBox>(Tree->FindWidget(Spec.Key));auto* Column=Cast<UVerticalBox>(Tree->FindWidget(Spec.Value));
        if(Check&&Column)
        {
            auto* Label=Cast<UTextBlock>(Check->GetContent());
            if(Label)DWC04Settings::Pair(Tree,Column,Label,Check,FString(TEXT("C04"))+Spec.Key,44);
        }
    }
    if(auto* Master=Cast<UVerticalBox>(Tree->FindWidget(TEXT("SettingsBookMasterVolume"))))
        DWC04Settings::Pair(Tree,Master,Cast<UTextBlock>(Tree->FindWidget(TEXT("VolumeValueText"))),
            Tree->FindWidget(TEXT("VolumeSlider")),TEXT("C04MasterVolumeRow"));
    for(const TCHAR* Name:{TEXT("ResolutionLabel"),TEXT("WindowModeLabel"),TEXT("QualityLabel"),TEXT("LanguageLabel"),
        TEXT("VSyncLabel"),TEXT("CombatEnemyHealthLabel"),TEXT("VolumeValueText")})
        DWC04Settings::Label(Cast<UTextBlock>(Tree->FindWidget(Name)));
}
void StyleSettingsSlider(USlider* Slider)
{
    if(!Slider)return;
    FSliderStyle Style=Slider->GetWidgetStyle();
    Style.NormalBarImage=Style.HoveredBarImage=Style.DisabledBarImage=
        DWC01UI::Texture(TEXT("T_DW_C01_Slider_Track_2147a771"),FVector2D(264,9));
    Style.NormalThumbImage=Style.HoveredThumbImage=Style.DisabledThumbImage=
        DWC01UI::Texture(TEXT("T_DW_C01_Slider_Ellipse_a436a097"),FVector2D(22,22));
    Slider->SetWidgetStyle(Style);
    Slider->SetSliderBarColor(FLinearColor::White);
    Slider->SetSliderHandleColor(FLinearColor::White);
}

void StyleSettingsCheck(UCheckBox* Check)
{
    if(!Check)return;
    FCheckBoxStyle Style=Check->GetWidgetStyle();
    // Fill-only states: preserve the original checkbox behavior and content.
    Style.UncheckedImage=Style.UncheckedHoveredImage=Style.UncheckedPressedImage=
        DWC01UI::Surface(DWC01UI::Paper(),FVector2D(28,28));
    Style.CheckedImage=Style.CheckedHoveredImage=Style.CheckedPressedImage=
        DWC01UI::Surface(DWC01UI::Gold(),FVector2D(28,28));
    Style.UndeterminedImage=Style.UndeterminedHoveredImage=Style.UndeterminedPressedImage=
        DWC01UI::Surface(DWC01UI::Muted(),FVector2D(28,28));
    Style.BackgroundImage=Style.BackgroundHoveredImage=Style.BackgroundPressedImage=FSlateNoResource();
    Style.SetForegroundColor(FSlateColor(DWC01UI::Ink()));
    Style.SetHoveredForegroundColor(FSlateColor(DWC01UI::Ink()));
    Style.SetPressedForegroundColor(FSlateColor(DWC01UI::Ink()));
    Style.SetCheckedForegroundColor(FSlateColor(DWC01UI::Ink()));
    Style.SetCheckedHoveredForegroundColor(FSlateColor(DWC01UI::Ink()));
    Style.SetCheckedPressedForegroundColor(FSlateColor(DWC01UI::Ink()));
    Style.SetUndeterminedForegroundColor(FSlateColor(DWC01UI::Ink()));
    Check->SetWidgetStyle(Style);
}

void StyleSettingsCombo(UComboBoxString* Combo)
{
    if(!Combo)return;
    FComboBoxStyle Style=Combo->GetWidgetStyle();
    auto& Button=Style.ComboButtonStyle.ButtonStyle;
    Button.Normal=Button.Hovered=Button.Pressed=Button.Disabled=DWC01UI::Surface(DWC01UI::Paper());
    Button.NormalForeground=Button.HoveredForeground=Button.PressedForeground=Button.DisabledForeground=
        FSlateColor(DWC01UI::Ink());
    Button.NormalPadding=Button.PressedPadding=FMargin(0);
    Style.ComboButtonStyle.MenuBorderBrush=DWC01UI::Surface(DWC01UI::Paper());
    Style.ComboButtonStyle.ShadowColorAndOpacity=FLinearColor::Transparent;
    Combo->SetWidgetStyle(Style);
    Combo->SetContentPadding(FMargin(16,0));
    FTableRowStyle Row=Combo->GetItemStyle();
    Row.EvenRowBackgroundBrush=Row.OddRowBackgroundBrush=DWC01UI::Surface(DWC01UI::Paper());
    Row.EvenRowBackgroundHoveredBrush=Row.OddRowBackgroundHoveredBrush=
        Row.ActiveBrush=Row.ActiveHoveredBrush=Row.InactiveBrush=Row.InactiveHoveredBrush=
        DWC01UI::Surface(DWC01UI::Gold());
    Row.TextColor=Row.SelectedTextColor=FSlateColor(DWC01UI::Ink());
    Combo->SetItemStyle(Row);
}
}

void UDWGameplayWidget::InitializeSettingsBook()
{
    if(!GetWidgetFromName(TEXT("SettingsBookPages")))return;
#define DW_BOOK_BIND(Name,Handler) if(auto* Button=Cast<UButton>(GetWidgetFromName(TEXT(Name)))) \
    {Button->OnClicked.RemoveDynamic(this,&UDWGameplayWidget::Handler); \
    if(bBindDefaultButtonActions)Button->OnClicked.AddUniqueDynamic(this,&UDWGameplayWidget::Handler);}
    DW_BOOK_BIND("SettingsBookGraphicsTabButton",BookGraphicsTab)
    DW_BOOK_BIND("SettingsBookAudioTabButton",BookAudioTab)
    DW_BOOK_BIND("SettingsBookKeysTabButton",BookKeysTab)
    DW_BOOK_BIND("SettingsBookControllerTabButton",BookControllerTab)
    DW_BOOK_BIND("SettingsBookGeneralTabButton",BookGeneralTab)
#undef DW_BOOK_BIND
    if(ExtendedSettings)ExtendedSettings->SetBookPage(0);
    RefreshSettingsBook();
}

void UDWGameplayWidget::SelectSettingsBookTab(int32 Tab)
{
    if(!GetWidgetFromName(TEXT("SettingsBookPages")))return;
    const int32 Next=FMath::Clamp(Tab,0,4);
    if(Next!=SettingsBookTab)
    {
        if(ExtendedSettings&&ExtendedSettings->IsCapturing())ExtendedSettings->CancelCapture();
        PlayClick();
        SettingsBookTab=Next;
        if(auto* Scroll=Cast<UScrollBox>(GetWidgetFromName(TEXT("SettingsPage_Scroll"))))Scroll->ScrollToStart();
    }
    RefreshSettingsBook();
}

void UDWGameplayWidget::RefreshSettingsBook()
{
    auto* Pages=Cast<UWidgetSwitcher>(GetWidgetFromName(TEXT("SettingsBookPages")));
    if(!Pages||Pages->GetChildrenCount()!=3)return;
    SettingsBookTab=FMath::Clamp(SettingsBookTab,0,4);
    LayoutC04Settings(this);
    for(const TCHAR* Name:{TEXT("SettingsBackButton"),TEXT("ApplySettingsButton")})
        if(auto* Button=Cast<UButton>(GetWidgetFromName(Name)))
        {
            if(auto* Label=Cast<UTextBlock>(Button->GetContent()))DWC04Settings::Label(Label,20,false);
            StyleC01Button(Button,DWC01UI::Paper());
        }
    StyleSettingsSlider(Cast<USlider>(GetWidgetFromName(TEXT("VolumeSlider"))));
    for(const TCHAR* Name:{TEXT("VSyncCheck"),TEXT("CombatEnemyHealthCheck")})
        StyleSettingsCheck(Cast<UCheckBox>(GetWidgetFromName(Name)));
    for(const TCHAR* Name:{TEXT("ResolutionCombo"),TEXT("WindowModeCombo"),TEXT("QualityCombo"),TEXT("LanguageCombo")})
        StyleSettingsCombo(Cast<UComboBoxString>(GetWidgetFromName(Name)));
    const int32 Page=SettingsBookTab==0?0:SettingsBookTab==4?2:1;
    Pages->SetActiveWidgetIndex(Page);
    if(UWidget* Master=GetWidgetFromName(TEXT("SettingsBookMasterVolume")))
        Master->SetVisibility(SettingsBookTab==1?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(ExtendedSettings&&SettingsBookTab>=1&&SettingsBookTab<=3)ExtendedSettings->SetBookPage(SettingsBookTab-1);

    for(int32 I=0;I<5;++I)
    {
        const bool Selected=I==SettingsBookTab;
        const FString Name(BookTabs[I]);
        if(auto* Label=Cast<UTextBlock>(GetWidgetFromName(FName(*(Name+TEXT("_Label"))))))
        {
            Label->SetText(DWText(this,BookCN[I],BookEN[I]));
            DWC04Settings::Label(Label,18,!Selected);Label->SetJustification(ETextJustify::Center);
        }
        if(auto* Button=Cast<UButton>(GetWidgetFromName(BookTabs[I])))
        {
            // Retain the existing Bounce driver. Selection uses the reference's white tab.
            StyleC01Button(Button,Selected?FLinearColor::White:DWC04Settings::RowColor(),false,false);
            FButtonStyle Style=Button->GetStyle();
            Style.Normal=Style.Hovered=Style.Pressed=Style.Disabled=FSlateColorBrush(FLinearColor::White);
            Style.PressedPadding=Style.NormalPadding;Button->SetStyle(Style);
            if(auto* Cell=Cast<UButtonSlot>(Button->GetContentSlot()))Cell->SetPadding(FMargin(10,4));
        }
        if(auto* Height=Cast<USizeBox>(GetWidgetFromName(FName(*(Name+TEXT("_Height"))))))
        {Height->SetHeightOverride(44);if(auto* Cell=Cast<UHorizontalBoxSlot>(Height->Slot))Cell->SetPadding(FMargin(0,0,I<4?8:0,0));}
        if(UWidget* Underline=GetWidgetFromName(FName(*(Name+TEXT("_Underline")))))
            Underline->SetVisibility(ESlateVisibility::Collapsed);
        if(UWidget* UnderlineHeight=GetWidgetFromName(FName(*(Name+TEXT("_UnderlineHeight")))))
            UnderlineHeight->SetVisibility(ESlateVisibility::Collapsed);
    }
    const TCHAR* DescriptionsCN[]={TEXT("调整显示与画质。修改后点击底部“应用并保存”。"),
        TEXT("主音量控制全部游戏声音；音乐、旁白与音效可分别调整。"),
        TEXT("选择操作并按新的键盘键或鼠标按钮。按键更改自动保存。"),
        TEXT("查看当前键盘与鼠标操作，调整镜头拖拽灵敏度。"),TEXT("选择界面语言与敌人生命显示。更改即时生效并作为用户设置保存。")};
    const TCHAR* DescriptionsEN[]={TEXT("Adjust display and quality, then choose Apply & save below."),
        TEXT("Master volume controls all game audio. Music, voice and effects remain independent."),
        TEXT("Choose an action and press a keyboard key or mouse button. Bindings save automatically."),
        TEXT("Review current keyboard and mouse controls and adjust camera drag sensitivity."),
        TEXT("Choose language and enemy health display. Changes apply immediately and save as local preferences.")};
    if(auto* Heading=Cast<UTextBlock>(GetWidgetFromName(TEXT("Settings_Heading"))))Heading->SetText(DWText(this,TEXT("设置"),TEXT("SETTINGS")));
    if(auto* Description=Cast<UTextBlock>(GetWidgetFromName(TEXT("Settings_Description"))))
        Description->SetText(DWText(this,DescriptionsCN[SettingsBookTab],DescriptionsEN[SettingsBookTab]));
}

bool UDWGameplayWidget::IsSettingsBookNavigationHit(FVector2D Position) const
{
    if(CurrentPage!=EDWMenuPage::Settings||!GetWidgetFromName(TEXT("SettingsBookPages")))return false;
    for(const TCHAR* Name:{TEXT("SettingsBookTabs"),TEXT("SettingsBookActions"),TEXT("SettingsBookBackWidth")})
        if(const UWidget* Widget=GetWidgetFromName(Name))
        {
            const FGeometry& Geometry=Widget->GetTickSpaceGeometry();
            if(Geometry.GetLocalSize().X>0.f&&Geometry.GetLocalSize().Y>0.f&&Geometry.IsUnderLocation(Position))return true;
        }
    return false;
}

void UDWGameplayWidget::BookGraphicsTab(){SelectSettingsBookTab(0);}
void UDWGameplayWidget::BookAudioTab(){SelectSettingsBookTab(1);}
void UDWGameplayWidget::BookKeysTab(){SelectSettingsBookTab(2);}
void UDWGameplayWidget::BookControllerTab(){SelectSettingsBookTab(3);}
void UDWGameplayWidget::BookGeneralTab(){SelectSettingsBookTab(4);}
