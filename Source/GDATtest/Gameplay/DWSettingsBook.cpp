#include "DWGameplayWidget.h"
#include "DWSettingsPanel.h"
#include "DWLocalizationLibrary.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
const TCHAR* BookTabs[]={TEXT("SettingsBookGraphicsTabButton"),TEXT("SettingsBookAudioTabButton"),
    TEXT("SettingsBookKeysTabButton"),TEXT("SettingsBookControllerTabButton"),TEXT("SettingsBookGeneralTabButton")};
const TCHAR* BookCN[]={TEXT("图像"),TEXT("声音"),TEXT("按键"),TEXT("控制器"),TEXT("常规")};
const TCHAR* BookEN[]={TEXT("Graphics"),TEXT("Audio"),TEXT("Keys"),TEXT("Controls"),TEXT("General")};
const FLinearColor RuntimeBookInk(.18f,.085f,.04f,1.f),RuntimeBookEdge(.54f,.31f,.14f,1.f);
const FLinearColor RuntimeBookPaper(.97f,.89f,.72f,1.f),RuntimeBookCaramel(.85f,.55f,.25f,1.f);
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
            Label->SetColorAndOpacity(FSlateColor(RuntimeBookInk));Label->SetAutoWrapText(false);
        }
        if(auto* Button=Cast<UButton>(GetWidgetFromName(BookTabs[I])))
        {
            FButtonStyle Style=Button->GetStyle();
            const FVector4 Radius(16.f,16.f,5.f,5.f);
            Style.Normal=FSlateRoundedBoxBrush(Selected?RuntimeBookCaramel:RuntimeBookPaper,Radius,RuntimeBookEdge,Selected?2.f:1.f);
            Style.Hovered=FSlateRoundedBoxBrush(FLinearColor(.98f,.78f,.48f,1.f),Radius,RuntimeBookEdge,2.f);
            Style.Pressed=FSlateRoundedBoxBrush(FLinearColor(.76f,.43f,.18f,1.f),Radius,RuntimeBookEdge,2.f);
            Style.NormalForeground=Style.HoveredForeground=Style.PressedForeground=FSlateColor(RuntimeBookInk);
            Button->SetStyle(Style);Button->SetBackgroundColor(FLinearColor::White);
        }
        if(UWidget* Underline=GetWidgetFromName(FName(*(Name+TEXT("_Underline")))))
            Underline->SetVisibility(Selected?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    }
    const TCHAR* DescriptionsCN[]={TEXT("调整显示与画质。修改后点击底部“应用并保存”。"),
        TEXT("主音量控制全部游戏声音；音乐、旁白与音效可分别调整。"),
        TEXT("选择操作并按新的键盘键或鼠标按钮。按键更改自动保存。"),
        TEXT("查看当前键盘与鼠标操作，调整镜头拖拽灵敏度。"),TEXT("选择游戏界面语言。更改即时生效并自动保存。")};
    const TCHAR* DescriptionsEN[]={TEXT("Adjust display and quality, then choose Apply & save below."),
        TEXT("Master volume controls all game audio. Music, voice and effects remain independent."),
        TEXT("Choose an action and press a keyboard key or mouse button. Bindings save automatically."),
        TEXT("Review current keyboard and mouse controls and adjust camera drag sensitivity."),
        TEXT("Choose the interface language. Changes apply immediately and save automatically.")};
    if(auto* Heading=Cast<UTextBlock>(GetWidgetFromName(TEXT("Settings_Heading"))))Heading->SetText(DWText(this,TEXT("设置"),TEXT("Settings")));
    if(auto* Description=Cast<UTextBlock>(GetWidgetFromName(TEXT("Settings_Description"))))
        Description->SetText(DWText(this,DescriptionsCN[SettingsBookTab],DescriptionsEN[SettingsBookTab]));
}

bool UDWGameplayWidget::IsSettingsBookNavigationHit(FVector2D Position) const
{
    if(CurrentPage!=EDWMenuPage::Settings||!GetWidgetFromName(TEXT("SettingsBookPages")))return false;
    for(const TCHAR* Name:{TEXT("SettingsBookTabs"),TEXT("SettingsBookActions")})
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
