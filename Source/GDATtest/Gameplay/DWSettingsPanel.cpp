#include "DWSettingsPanel.h"
#include "DWGameplayWidget.h"
#include "DWUserSettings.h"
#include "DWLocalizationLibrary.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Brushes/SlateRoundedBoxBrush.h"
void UDWKeyBindingRow::NativeConstruct(){Super::NativeConstruct();if(BindingButton)BindingButton->OnClicked.AddUniqueDynamic(this,&UDWKeyBindingRow::ClickBinding);}
void UDWKeyBindingRow::SetupRow(EDWInputAction A,UDWGameplayWidget* S)
{
 BoundAction=A;if(S)S->ApplyWidgetPresentation(this);
 if(S&&S->GetWidgetFromName(TEXT("SettingsBookPages")))
 {
  const FLinearColor Ink(.18f,.085f,.04f,1.f),Paper(.97f,.89f,.72f,1.f),Edge(.54f,.31f,.14f,1.f);
  for(UTextBlock* Text:{ActionLabel.Get(),BindingLabel.Get()})if(Text)Text->SetColorAndOpacity(FSlateColor(Ink));
  if(BindingButton)
  {
   FButtonStyle Style=BindingButton->GetStyle();
   Style.Normal=FSlateRoundedBoxBrush(Paper,10.f,Edge,1.f);
   Style.Hovered=FSlateRoundedBoxBrush(FLinearColor(1.f,.94f,.82f,1.f),10.f,Edge,2.f);
   Style.Pressed=FSlateRoundedBoxBrush(FLinearColor(.88f,.69f,.43f,1.f),10.f,Edge,2.f);
   Style.NormalForeground=Style.HoveredForeground=Style.PressedForeground=FSlateColor(Ink);
   BindingButton->SetStyle(Style);BindingButton->SetBackgroundColor(FLinearColor::White);
  }
 }
 Refresh();
}
void UDWKeyBindingRow::Refresh(){if(auto* PC=Cast<ADWPlayerController>(GetOwningPlayer())){if(ActionLabel)ActionLabel->SetText(PC->GetActionDisplayName(BoundAction));if(BindingLabel){BindingLabel->SetAutoWrapText(false);BindingLabel->SetText(FText::FromName(PC->GetConfiguredKeys()[int32(BoundAction)].GetFName()));}}}
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
 if(KeyRows){KeyRows->ClearChildren();auto Cl=BindingRowClass;if(!Cl)Cl=LoadClass<UDWKeyBindingRow>(nullptr,TEXT("/Game/DoughWorld/UI/WBP_DWKeyBindingRow.WBP_DWKeyBindingRow_C"));
 if(Cl)for(int32 I=0;I<int32(EDWInputAction::Count);++I){auto* Row=CreateWidget<UDWKeyBindingRow>(GetOwningPlayer(),Cl);KeyRows->AddChild(Row);Row->SetupRow(EDWInputAction(I),S);Row->OnRequested.AddDynamic(this,&UDWSettingsPanel::RequestBinding);}}
 RefreshPanel();
}
void UDWSettingsPanel::Sound(){if(OwnerScreen)OwnerScreen->PlayClick();}
void UDWSettingsPanel::SetStatus(FText T){if(BindingStatus)BindingStatus->SetText(T);}
void UDWSettingsPanel::RefreshVolumes()
{
 auto* S=UDWUserSettings::Resolve(this);if(!S)return;TGuardValue<bool> Guard(bRefreshing,true);
 USlider* Sliders[]={MusicSlider,VoiceSlider,EffectsSlider};UTextBlock* Labels[]={MusicLabel,VoiceLabel,EffectsLabel};
 const TCHAR* CN[]={TEXT("音乐"),TEXT("说话 / 旁白"),TEXT("音效")};const TCHAR* EN[]={TEXT("Music"),TEXT("Voice / Narration"),TEXT("Sound effects")};
 for(int32 I=0;I<3;++I){float V=S->GetCategoryVolume(EDWSoundCategory(I));if(Sliders[I])Sliders[I]->SetValue(V);if(Labels[I])Labels[I]->SetText(FText::Format(FText::FromString(TEXT("{0}  {1}%")),DWText(this,CN[I],EN[I]),FMath::RoundToInt(V*100)));}
}
void UDWSettingsPanel::RefreshPanel()
{
 if(OwnerScreen)OwnerScreen->ApplyWidgetPresentation(this);RefreshVolumes();RefreshCameraSensitivity();RefreshControlsGuide();
 auto Label=[this](const TCHAR* N,const TCHAR* CN,const TCHAR* EN){if(auto* T=Cast<UTextBlock>(GetWidgetFromName(N))){T->SetAutoWrapText(false);T->SetText(DWText(this,CN,EN));}};
 Label(TEXT("AudioTabButton_Label"),TEXT("声音"),TEXT("Audio"));Label(TEXT("ControlsTabButton_Label"),TEXT("按键绑定"),TEXT("Controls"));Label(TEXT("ResetKeysButton_Label"),TEXT("恢复默认按键"),TEXT("Restore default keys"));Label(TEXT("CancelBindingButton_Label"),TEXT("取消改键"),TEXT("Cancel rebinding"));
 Label(TEXT("ControllerTabButton_Label"),TEXT("控制器"),TEXT("Controls guide"));
 Label(TEXT("CameraSensitivityResetButton_Label"),TEXT("恢复镜头灵敏度"),TEXT("Reset camera sensitivity"));
 if(KeyRows)for(auto* W:KeyRows->GetAllChildren())if(auto* R=Cast<UDWKeyBindingRow>(W))R->Refresh();
 if(CancelBindingButton)CancelBindingButton->SetVisibility(IsCapturing()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(!IsCapturing())SetStatus(DWText(this,TEXT("点击一个按键后，按新的键盘键或鼠标按钮。更改自动保存。"),TEXT("Click a key, then press a keyboard key or mouse button. Changes save automatically.")));
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
    const auto Action=EDWInputAction(I);
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
