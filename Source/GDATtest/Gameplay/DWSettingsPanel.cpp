#include "DWSettingsPanel.h"
#include "DWGameplayWidget.h"
#include "DWUserSettings.h"
#include "DWLocalizationLibrary.h"
#include "Components/Button.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Blueprint/WidgetTree.h"
#include "DWC01UIStyle.h"
#include "DWC04SettingsPresentation.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Spacer.h"
#include "Components/ButtonSlot.h"

namespace
{
void PrepareC04OptionsTree(UDWSettingsPanel* Panel)
{
 if(!Panel||!Panel->WidgetTree)return;
 UWidgetTree* Tree=Panel->WidgetTree;
 // UVerticalBox::OnSlotRemoved removes GetCachedWidget(), while a component-wrapped
 // button was inserted as GetCachedWrappedWidget(). Reparenting after Slate builds
 // can therefore leave an old painted Bounce wrapper behind. Prepare the UObject
 // hierarchy exactly once during owner initialization, before any Slate is built.
 bool bSlateBuilt=Panel->GetCachedWidget().IsValid();
 Tree->ForEachWidget([&bSlateBuilt](UWidget* Widget){bSlateBuilt|=Widget->GetCachedWidget().IsValid();});
 if(bSlateBuilt)
 {
  UE_LOG(LogTemp,Warning,TEXT("%s: C04 settings tree preparation skipped because Slate already exists; no live reparent attempted."),*Panel->GetPathName());
  return;
 }
 // Older authored books flattened these two scrolls. Reconnect their original widgets
 // at runtime so all 26 actions remain reachable inside the new bounded row area.
 const auto RestoreScroll=[Tree](const TCHAR* HeightName,const TCHAR* ScrollName,const TCHAR* ContentName)
 {
  auto* Height=Cast<USizeBox>(Tree->FindWidget(HeightName));
  auto* Scroll=Cast<UScrollBox>(Tree->FindWidget(ScrollName));UWidget* Content=Tree->FindWidget(ContentName);
  if(!Height||!Scroll||!Content)return;
  if(Height->GetContent()==Content)
  {Content->RemoveFromParent();Scroll->RemoveFromParent();Scroll->AddChild(Content);Height->SetContent(Scroll);}
  Scroll->SetVisibility(ESlateVisibility::Visible);Scroll->SetIsEnabled(true);
 };
 RestoreScroll(TEXT("KeyRowsHeight"),TEXT("KeyRowsScroll"),TEXT("KeyRows"));
 RestoreScroll(TEXT("ControlsGuideHeight"),TEXT("ControlsGuideScroll"),TEXT("ControlsGuideColumn"));
 if(auto* Audio=Cast<UVerticalBox>(Tree->FindWidget(TEXT("AudioOptions"))))
  for(const TCHAR* Prefix:{TEXT("Music"),TEXT("Voice"),TEXT("Effects")})
  {
   const FString Name(Prefix);
   DWC04Settings::Pair(Tree,Audio,Cast<UTextBlock>(Tree->FindWidget(FName(*(Name+TEXT("Label"))))),
    Tree->FindWidget(FName(*(Name+TEXT("Slider")))),TEXT("C04")+Name+TEXT("Row"));
  }
 if(auto* Guide=Cast<UVerticalBox>(Tree->FindWidget(TEXT("ControllerOptions"))))
 {
  DWC04Settings::Pair(Tree,Guide,Cast<UTextBlock>(Tree->FindWidget(TEXT("CameraSensitivityLabel"))),
   Tree->FindWidget(TEXT("CameraSensitivitySlider")),TEXT("C04SensitivityRow"));
  // Keep the existing read-only guide and fixed-combination explanations in their own scroll.
  if(auto* Height=Cast<USizeBox>(Tree->FindWidget(TEXT("ControlsGuideHeight"))))
  {
   Height->SetHeightOverride(340);
   if(auto* Scroll=Cast<UScrollBox>(Height->GetContent()))
   {
    Scroll->RemoveFromParent();auto* Background=DWC04Settings::Card(Tree,TEXT("C04ControlsGuideBackground"),Scroll);
    // Bound the scrolling viewport inside the paper; long help remains reachable by scrolling.
    Background->SetPadding(FMargin(18));Background->SetVerticalAlignment(VAlign_Fill);
    Scroll->SetClipping(EWidgetClipping::ClipToBounds);
    if(UWidget* Column=Tree->FindWidget(TEXT("ControlsGuideColumn")))
     if(auto* Cell=Cast<UScrollBoxSlot>(Column->Slot))
     {Cell->SetPadding(FMargin(0,0,8,12));Cell->SetHorizontalAlignment(HAlign_Fill);}
    Height->SetContent(Background);
    UWidget* Sensitivity=Tree->FindWidget(TEXT("C04SensitivityRow_Height"));
    UWidget* Reset=Tree->FindWidget(TEXT("CameraSensitivityResetButton"));
    if(Sensitivity&&Reset)
    {
     Sensitivity->RemoveFromParent();Height->RemoveFromParent();Reset->RemoveFromParent();
     Guide->AddChildToVerticalBox(Sensitivity)->SetPadding(FMargin(0,0,0,12));
     Guide->AddChildToVerticalBox(Height)->SetPadding(FMargin(0,0,0,12));
     auto* ResetWidth=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("C04SensitivityResetWidth"));
     ResetWidth->SetWidthOverride(310);ResetWidth->SetHeightOverride(46);ResetWidth->SetContent(Reset);
     Guide->AddChildToVerticalBox(ResetWidth)->SetHorizontalAlignment(HAlign_Right);
    }
   }
  }
 }
 if(auto* Keys=Cast<UVerticalBox>(Tree->FindWidget(TEXT("ControlsOptions"))))
 {
  if(!Tree->FindWidget(TEXT("C04KeyFooter")))
  {
   UWidget* Status=Tree->FindWidget(TEXT("BindingStatus"));UWidget* Reset=Tree->FindWidget(TEXT("ResetKeysButton"));
   UWidget* Cancel=Tree->FindWidget(TEXT("CancelBindingButton"));
   if(Status&&Reset&&Cancel)
   {
    Status->RemoveFromParent();Reset->RemoveFromParent();Cancel->RemoveFromParent();
    auto* Footer=Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("C04KeyFooter"));
    auto AddButton=[Tree,Footer](UWidget* Button,const TCHAR* Name)
    {
     auto* Width=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);Width->SetWidthOverride(270);Width->SetHeightOverride(46);Width->SetContent(Button);
     Footer->AddChildToHorizontalBox(Width);
    };
    AddButton(Reset,TEXT("C04ResetKeyWidth"));
    auto* Spacer=Tree->ConstructWidget<USpacer>();Footer->AddChildToHorizontalBox(Spacer)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    AddButton(Cancel,TEXT("C04CancelKeyWidth"));
    Keys->AddChildToVerticalBox(Footer)->SetPadding(FMargin(0,4,0,4));
    auto* StatusFill=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C04BindingStatusFill"));
    StatusFill->SetBrush(DWC01UI::Surface(FLinearColor::FromSRGBColor(FColor(145,157,109)),FVector2D(680,36)));
    StatusFill->SetBrushColor(FLinearColor::White);StatusFill->SetPadding(FMargin(12,5));
    StatusFill->SetContent(Status);Keys->AddChildToVerticalBox(StatusFill)->SetPadding(FMargin(60,0));
   }
  }
 }
}
void StyleC04OptionsTree(UDWSettingsPanel* Panel)
{
 if(!Panel||!Panel->WidgetTree)return;
 UWidgetTree* Tree=Panel->WidgetTree;
 // Refresh changes only properties on the prepared tree. Never remove/re-add controls here.
 if(auto* Height=Cast<USizeBox>(Tree->FindWidget(TEXT("KeyRowsHeight"))))Height->SetHeightOverride(432);
 if(auto* Scroll=Cast<UScrollBox>(Tree->FindWidget(TEXT("KeyRowsScroll"))))
 {
  Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
  if(UWidget* Rows=Tree->FindWidget(TEXT("KeyRows")))if(auto* Cell=Cast<UScrollBoxSlot>(Rows->Slot))
  {Cell->SetPadding(FMargin(0,0,8,0));Cell->SetHorizontalAlignment(HAlign_Fill);}
 }
 // Reassert the final guide slot style after initialization and later presentation refreshes.
 if(UWidget* Column=Tree->FindWidget(TEXT("ControlsGuideColumn")))if(auto* Cell=Cast<UScrollBoxSlot>(Column->Slot))
 {if(Cell->GetPadding()!=FMargin(0,0,8,12))Cell->SetPadding(FMargin(0,0,8,12));
  if(Cell->GetHorizontalAlignment()!=HAlign_Fill)Cell->SetHorizontalAlignment(HAlign_Fill);}
 for(const TCHAR* Name:{TEXT("MusicLabel"),TEXT("VoiceLabel"),TEXT("EffectsLabel"),TEXT("CameraSensitivityLabel")})
  DWC04Settings::Label(Cast<UTextBlock>(Tree->FindWidget(Name)));
 for(const TCHAR* Name:{TEXT("ControlsGuideText"),TEXT("InventoryShortcutsText")})
  if(auto* Text=Cast<UTextBlock>(Tree->FindWidget(Name))){DWC04Settings::Label(Text,18);Text->SetAutoWrapText(true);}
 if(auto* Status=Cast<UTextBlock>(Tree->FindWidget(TEXT("BindingStatus"))))
 {DWC04Settings::Label(Status,16);Status->SetAutoWrapText(true);Status->SetJustification(ETextJustify::Center);}
 for(const TCHAR* Name:{TEXT("ResetKeysButton_Label"),TEXT("CancelBindingButton_Label"),TEXT("CameraSensitivityResetButton_Label")})
  if(auto* Text=Cast<UTextBlock>(Tree->FindWidget(Name))){DWC04Settings::Label(Text,18,false);Text->SetJustification(ETextJustify::Center);}
}
void StyleC01Options(UDWSettingsPanel* Panel,UDWGameplayWidget* Owner)
{
 if(!Panel||!Panel->WidgetTree||!Owner)return;
 Panel->WidgetTree->ForEachWidget([Owner](UWidget* Widget)
 {
  if(auto* Button=Cast<UButton>(Widget))Owner->StyleC01Button(Button,DWC01UI::Paper());
  if(auto* Slider=Cast<USlider>(Widget))
  {
   FSliderStyle Style=Slider->GetWidgetStyle();
   Style.NormalBarImage=Style.HoveredBarImage=Style.DisabledBarImage=
    DWC01UI::Texture(TEXT("T_DW_C01_Slider_Track_2147a771"),FVector2D(264,9));
   Style.NormalThumbImage=Style.HoveredThumbImage=Style.DisabledThumbImage=
    DWC01UI::Texture(TEXT("T_DW_C01_Slider_Ellipse_a436a097"),FVector2D(22,22));
   Slider->SetWidgetStyle(Style);Slider->SetSliderBarColor(FLinearColor::White);Slider->SetSliderHandleColor(FLinearColor::White);
  }
 });
 StyleC04OptionsTree(Panel);
}
}
void UDWKeyBindingRow::NativeConstruct(){Super::NativeConstruct();if(BindingButton)BindingButton->OnClicked.AddUniqueDynamic(this,&UDWKeyBindingRow::ClickBinding);}
void UDWKeyBindingRow::SetupRow(EDWInputAction A,UDWGameplayWidget* S)
{
 BoundAction=A;if(S)S->ApplyWidgetPresentation(this);
 if(WidgetTree&&!GetWidgetFromName(TEXT("C04BindingRowBackground")))
 {
  UWidget* Original=WidgetTree->RootWidget;Original->RemoveFromParent();
  auto* Background=DWC04Settings::Card(WidgetTree,TEXT("C04BindingRowBackground"),Original);
  Background->SetPadding(FMargin(18,6));WidgetTree->RootWidget=Background;
 }
 if(ActionLabel){DWC04Settings::Label(ActionLabel,20);if(auto* RowSlot=Cast<UHorizontalBoxSlot>(ActionLabel->Slot)){RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));RowSlot->SetHorizontalAlignment(HAlign_Fill);RowSlot->SetVerticalAlignment(VAlign_Center);RowSlot->SetPadding(FMargin(0,0,28,0));}}
 if(auto* Width=Cast<USizeBox>(GetWidgetFromName(TEXT("KeyButtonWidth")))){Width->SetWidthOverride(200);Width->SetMinDesiredHeight(48);Width->SetHeightOverride(48);if(auto* RowSlot=Cast<UHorizontalBoxSlot>(Width->Slot)){RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));RowSlot->SetHorizontalAlignment(HAlign_Right);RowSlot->SetVerticalAlignment(VAlign_Center);RowSlot->SetPadding(FMargin(0));}}
 if(BindingLabel){BindingLabel->SetJustification(ETextJustify::Center);BindingLabel->SetAutoWrapText(false);}
 DWC04Settings::Label(BindingLabel,20,false);if(BindingLabel)BindingLabel->SetJustification(ETextJustify::Center);
 if(S&&BindingButton)S->StyleC01Button(BindingButton,DWC01UI::Paper());
 Refresh();
}
void UDWKeyBindingRow::Refresh()
{
 if(auto* PC=Cast<ADWPlayerController>(GetOwningPlayer()))
 {
  if(ActionLabel)ActionLabel->SetText(PC->GetActionDisplayName(BoundAction));
  if(BindingLabel)
  {
   BindingLabel->SetAutoWrapText(false);
   BindingLabel->SetText(PC->GetAppliedActionKey(BoundAction).IsValid()?PC->GetActionKeyLabel(BoundAction):
    DWText(this,TEXT("未绑定"),TEXT("Unbound")));
  }
 }
}
void UDWKeyBindingRow::ClickBinding(){OnRequested.Broadcast(BoundAction);}
void UDWSettingsPanel::NativeConstruct()
{
 Super::NativeConstruct();SetIsFocusable(true);
 if(AudioTabButton)AudioTabButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::AudioTab);
 if(ControlsTabButton)ControlsTabButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::ControlsTab);
 if(ControllerTabButton)ControllerTabButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::ControllerTab);
 if(ResetKeysButton)ResetKeysButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::ResetKeys);
 if(CancelBindingButton)CancelBindingButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::CancelCapture);
 if(MusicSlider)MusicSlider->OnValueChanged.AddUniqueDynamic(this,&UDWSettingsPanel::MusicChanged);
 if(VoiceSlider)VoiceSlider->OnValueChanged.AddUniqueDynamic(this,&UDWSettingsPanel::VoiceChanged);
 if(EffectsSlider)EffectsSlider->OnValueChanged.AddUniqueDynamic(this,&UDWSettingsPanel::EffectsChanged);
 if(CameraSensitivitySlider)CameraSensitivitySlider->OnValueChanged.AddUniqueDynamic(this,&UDWSettingsPanel::CameraSensitivityChanged);
 if(CameraSensitivityResetButton)CameraSensitivityResetButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::ResetCameraSensitivity);
}
void UDWSettingsPanel::InitializePanel(UDWGameplayWidget* S)
{
 OwnerScreen=S;if(OwnerScreen)OwnerScreen->ApplyWidgetPresentation(this);
 if(KeyRows){DWC01UI::ReleaseMotionTree(KeyRows);KeyRows->ClearChildren();auto Cl=BindingRowClass;if(!Cl)Cl=LoadClass<UDWKeyBindingRow>(nullptr,TEXT("/Game/DoughWorld/UI/WBP_DWKeyBindingRow.WBP_DWKeyBindingRow_C"));
 if(Cl)for(int32 I=0;I<int32(EDWInputAction::Count);++I){if(!ADWPlayerController::IsBindableInputAction(EDWInputAction(I)))continue;auto* Row=CreateWidget<UDWKeyBindingRow>(GetOwningPlayer(),Cl);if(!Row)continue;Row->SetupRow(EDWInputAction(I),S);if(auto* RowSlot=KeyRows->AddChildToVerticalBox(Row)){RowSlot->SetPadding(FMargin(0,0,0,12));RowSlot->SetHorizontalAlignment(HAlign_Fill);}Row->OnRequested.AddDynamic(this,&UDWSettingsPanel::RequestBinding);}}
 RefreshPanel();
}
void UDWSettingsPanel::Sound(){if(OwnerScreen)OwnerScreen->PlayClick();}
void UDWSettingsPanel::SetStatus(FText T)
{
 const auto StatusVisibility=T.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible;
 if(BindingStatus){BindingStatus->SetText(T);BindingStatus->SetVisibility(StatusVisibility);}
 if(UWidget* Fill=GetWidgetFromName(TEXT("C04BindingStatusFill")))Fill->SetVisibility(StatusVisibility);
}
void UDWSettingsPanel::RefreshVolumes()
{
 auto* S=UDWUserSettings::Resolve(this);if(!S)return;TGuardValue<bool> Guard(bRefreshing,true);
 USlider* Sliders[]={MusicSlider,VoiceSlider,EffectsSlider};UTextBlock* Labels[]={MusicLabel,VoiceLabel,EffectsLabel};
 const TCHAR* CN[]={TEXT("音乐"),TEXT("说话 / 旁白"),TEXT("音效")};const TCHAR* EN[]={TEXT("Music"),TEXT("Voice / Narration"),TEXT("Sound effects")};
 for(int32 I=0;I<3;++I){float V=S->GetCategoryVolume(EDWSoundCategory(I));if(Sliders[I])Sliders[I]->SetValue(V);if(Labels[I])Labels[I]->SetText(FText::Format(FText::FromString(TEXT("{0}  {1}%")),DWText(this,CN[I],EN[I]),FMath::RoundToInt(V*100)));}
}
void UDWSettingsPanel::RefreshPanel()
{
 if(OwnerScreen)OwnerScreen->ApplyWidgetPresentation(this);
 StyleC01Options(this,OwnerScreen);
 RefreshVolumes();RefreshCameraSensitivity();RefreshControlsGuide();
 auto Label=[this](const TCHAR* N,const TCHAR* CN,const TCHAR* EN){if(auto* T=Cast<UTextBlock>(GetWidgetFromName(N))){T->SetAutoWrapText(false);T->SetText(DWText(this,CN,EN));}};
 Label(TEXT("AudioTabButton_Label"),TEXT("声音"),TEXT("Audio"));Label(TEXT("ControlsTabButton_Label"),TEXT("按键绑定"),TEXT("Controls"));Label(TEXT("ResetKeysButton_Label"),TEXT("恢复默认按键"),TEXT("Restore default keys"));Label(TEXT("CancelBindingButton_Label"),TEXT("取消改键"),TEXT("Cancel rebinding"));
 Label(TEXT("ControllerTabButton_Label"),TEXT("控制器"),TEXT("Controls guide"));
 Label(TEXT("CameraSensitivityResetButton_Label"),TEXT("恢复镜头灵敏度"),TEXT("Reset camera sensitivity"));
 if(KeyRows)for(auto* W:KeyRows->GetAllChildren())if(auto* R=Cast<UDWKeyBindingRow>(W))R->Refresh();
 if(CancelBindingButton)CancelBindingButton->SetVisibility(IsCapturing()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(!IsCapturing())SetStatus(FText::GetEmpty());
 if(bBookNavigation)if(UWidget* Tabs=GetWidgetFromName(TEXT("SettingsTabs"))){Tabs->SetVisibility(ESlateVisibility::Collapsed);Tabs->SetIsEnabled(false);}
}
void UDWSettingsPanel::SetBookPage(int32 OptionsIndex)
{
 bBookNavigation=true;
 const int32 Index=FMath::Clamp(OptionsIndex,0,2);
 if(OptionsSwitcher&&OptionsSwitcher->GetChildrenCount()>Index)
 {
  if(IsCapturing()&&OptionsSwitcher->GetActiveWidgetIndex()!=Index)CancelCapture();
  OptionsSwitcher->SetActiveWidgetIndex(Index);
 }
 RefreshPanel();
}
void UDWSettingsPanel::MusicChanged(float V){if(!bRefreshing)if(auto* S=UDWUserSettings::Resolve(this)){S->SetCategoryVolume(EDWSoundCategory::Music,V);RefreshVolumes();}}
void UDWSettingsPanel::VoiceChanged(float V){if(!bRefreshing)if(auto* S=UDWUserSettings::Resolve(this)){S->SetCategoryVolume(EDWSoundCategory::Voice,V);RefreshVolumes();}}
void UDWSettingsPanel::EffectsChanged(float V){if(!bRefreshing)if(auto* S=UDWUserSettings::Resolve(this)){S->SetCategoryVolume(EDWSoundCategory::SFX,V);RefreshVolumes();}}
void UDWSettingsPanel::AudioTab(){Sound();CancelCapture();if(OptionsSwitcher)OptionsSwitcher->SetActiveWidgetIndex(0);}
void UDWSettingsPanel::ControlsTab(){Sound();if(OptionsSwitcher)OptionsSwitcher->SetActiveWidgetIndex(1);RefreshPanel();}
void UDWSettingsPanel::ControllerTab(){Sound();CancelCapture();if(OptionsSwitcher&&OptionsSwitcher->GetChildrenCount()>2)OptionsSwitcher->SetActiveWidgetIndex(2);RefreshPanel();}
void UDWSettingsPanel::RefreshControlsGuide()
{
 if(ControlsGuideText)
 {
  FString Guide=DWText(this,TEXT("键盘与鼠标操作（当前生效的按键）："),TEXT("Keyboard and mouse controls (current bindings):")).ToString();
  if(const auto* PC=Cast<ADWPlayerController>(GetOwningPlayer()))
  {
   for(int32 I=0;I<int32(EDWInputAction::Count);++I)
   {
    const auto Action=EDWInputAction(I);if(!ADWPlayerController::IsBindableInputAction(Action))continue;
    FText Key=PC->GetActionKeyLabel(Action);
    if(!PC->GetAppliedActionKey(Action).IsValid())Key=DWText(this,TEXT("未绑定"),TEXT("Unbound"));
    else if(Action==EDWInputAction::CameraDrag)Key=FText::Format(
     DWText(this,TEXT("按住 {0} 并移动鼠标"),TEXT("Hold {0} and move the mouse")),Key);
    Guide+=FText::Format(FText::FromString(TEXT("\n{0}  —  {1}")),PC->GetActionDisplayName(Action),Key).ToString();
   }
  }
  // MouseWheelAxis is a fixed axis binding, outside EDWInputAction's rebinding list.
  Guide+=DWText(this,TEXT("\n鼠标滚轮向上  —  拉近视角\n鼠标滚轮向下  —  拉远视角"),
   TEXT("\nMouse wheel up  —  Zoom in\nMouse wheel down  —  Zoom out")).ToString();
  Guide+=(bBookNavigation?DWText(this,TEXT("\n\n这里仅说明操作；可改绑的按键请到“按键”页调整。滚轮缩放和背包组合快捷键使用固定规则，不在改绑列表中。"),
   TEXT("\n\nThis page explains controls. Rebind supported actions on the Keys tab. Wheel zoom and inventory combinations use fixed rules and are not part of that rebinding list.")):
   DWText(this,TEXT("\n\n这里仅说明操作；可改绑的按键请到“按键绑定”页调整。滚轮缩放和背包组合快捷键使用固定规则，不在改绑列表中。"),
   TEXT("\n\nThis page explains controls. Rebind supported actions on the Controls tab. Wheel zoom and inventory combinations use fixed rules and are not part of that rebinding list."))).ToString();
  const FText Text=FText::FromString(Guide);
  if(!ControlsGuideText->GetText().EqualTo(Text))ControlsGuideText->SetText(Text);
 }
 if(InventoryShortcutsText)
 {
  const FText Help=OwnerScreen?OwnerScreen->GetInventoryControlHelp():FText::GetEmpty();
  if(!InventoryShortcutsText->GetText().EqualTo(Help))InventoryShortcutsText->SetText(Help);
 }
}
void UDWSettingsPanel::RefreshCameraSensitivity()
{
 const auto* Settings=UDWUserSettings::Resolve(this);if(!Settings)return;
 TGuardValue<bool> Guard(bRefreshing,true);
 const float Value=Settings->GetCameraSensitivityMultiplier();
 if(CameraSensitivitySlider)CameraSensitivitySlider->SetValue((Value-UDWUserSettings::CameraSensitivityMinimum)
  /(UDWUserSettings::CameraSensitivityMaximum-UDWUserSettings::CameraSensitivityMinimum));
 if(CameraSensitivityLabel)CameraSensitivityLabel->SetText(FText::Format(
  DWText(this,TEXT("镜头灵敏度  {0}%"),TEXT("Camera sensitivity  {0}%")),FMath::RoundToInt(Value*100.f)));
}
void UDWSettingsPanel::CameraSensitivityChanged(float Value)
{
 if(bRefreshing)return;
 if(auto* Settings=UDWUserSettings::Resolve(this))
 {
  const float Normalized=FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):
   (1.f-UDWUserSettings::CameraSensitivityMinimum)/(UDWUserSettings::CameraSensitivityMaximum-UDWUserSettings::CameraSensitivityMinimum);
  Settings->SetCameraSensitivityMultiplier(FMath::Lerp(UDWUserSettings::CameraSensitivityMinimum,UDWUserSettings::CameraSensitivityMaximum,Normalized));
  RefreshCameraSensitivity();
 }
}
void UDWSettingsPanel::ResetCameraSensitivity(){Sound();if(auto* Settings=UDWUserSettings::Resolve(this)){Settings->SetCameraSensitivityMultiplier(1.f);RefreshCameraSensitivity();}}
void UDWSettingsPanel::RequestBinding(EDWInputAction A)
{
 Sound();Capturing=A;if(OwnerScreen)OwnerScreen->SetKeyboardFocus();if(CancelBindingButton)CancelBindingButton->SetVisibility(ESlateVisibility::Visible);
 if(auto* PC=Cast<ADWPlayerController>(GetOwningPlayer()))SetStatus(FText::Format(DWText(this,TEXT("为“{0}”按下一个键；点取消可退出。"),TEXT("Press a key for {0}, or click Cancel.")),PC->GetActionDisplayName(A)));
}
void UDWSettingsPanel::CancelCapture(){Capturing=EDWInputAction::Count;RefreshPanel();}
bool UDWSettingsPanel::IsCancelHit(FVector2D P)const{return CancelBindingButton&&CancelBindingButton->GetCachedGeometry().IsUnderLocation(P);}
bool UDWSettingsPanel::CaptureKey(FKey K)
{
 if(!IsCapturing())return false;auto* PC=Cast<ADWPlayerController>(GetOwningPlayer());if(!PC)return true;FText Error;
 if(PC->SetActionBinding(Capturing,K,Error)){CancelCapture();SetStatus(DWText(this,TEXT("按键已保存。"),TEXT("Binding saved.")));}else SetStatus(Error);return true;
}
void UDWSettingsPanel::ResetKeys(){Sound();auto* PC=Cast<ADWPlayerController>(GetOwningPlayer());if(!PC)return;FText Error;if(PC->RestoreAuthoredBindings(Error)){CancelCapture();SetStatus(DWText(this,TEXT("已恢复蓝图默认按键。"),TEXT("Blueprint default bindings restored.")));}else SetStatus(Error);}

void UDWKeyBindingRow::NativeOnInitialized(){Super::NativeOnInitialized();if(BindingButton)DWC01UI::AttachMotion(BindingButton,false,true);}
void UDWSettingsPanel::NativeOnInitialized()
{
 // BindWidget references and WidgetTree already exist here, but OwnerScreen may still
 // be null. This preparation only uses this widget's own tree and never reads OwnerScreen.
 PrepareC04OptionsTree(this);
 StyleC04OptionsTree(this);
 Super::NativeOnInitialized();
 if(WidgetTree)WidgetTree->ForEachWidget([](UWidget* W){if(auto* B=Cast<UButton>(W))DWC01UI::AttachMotion(B,false,true);});
}
