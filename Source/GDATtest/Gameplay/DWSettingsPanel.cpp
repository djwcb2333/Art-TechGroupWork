#include "DWSettingsPanel.h"
#include "DWGameplayWidget.h"
#include "DWUserSettings.h"
#include "DWLocalizationLibrary.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Slider.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
void UDWKeyBindingRow::NativeConstruct(){Super::NativeConstruct();if(BindingButton)BindingButton->OnClicked.AddUniqueDynamic(this,&UDWKeyBindingRow::ClickBinding);}
void UDWKeyBindingRow::SetupRow(EDWInputAction A,UDWGameplayWidget* S){BoundAction=A;if(S)S->ApplyWidgetPresentation(this);Refresh();}
void UDWKeyBindingRow::Refresh(){if(auto* PC=Cast<ADWPlayerController>(GetOwningPlayer())){if(ActionLabel)ActionLabel->SetText(PC->GetActionDisplayName(BoundAction));if(BindingLabel){BindingLabel->SetAutoWrapText(false);BindingLabel->SetText(FText::FromName(PC->GetConfiguredKeys()[int32(BoundAction)].GetFName()));}}}
void UDWKeyBindingRow::ClickBinding(){OnRequested.Broadcast(BoundAction);}
void UDWSettingsPanel::NativeConstruct()
{
 Super::NativeConstruct();SetIsFocusable(true);
 if(AudioTabButton)AudioTabButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::AudioTab);
 if(ControlsTabButton)ControlsTabButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::ControlsTab);
 if(ResetKeysButton)ResetKeysButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::ResetKeys);
 if(CancelBindingButton)CancelBindingButton->OnClicked.AddUniqueDynamic(this,&UDWSettingsPanel::CancelCapture);
 if(MusicSlider)MusicSlider->OnValueChanged.AddUniqueDynamic(this,&UDWSettingsPanel::MusicChanged);
 if(VoiceSlider)VoiceSlider->OnValueChanged.AddUniqueDynamic(this,&UDWSettingsPanel::VoiceChanged);
 if(EffectsSlider)EffectsSlider->OnValueChanged.AddUniqueDynamic(this,&UDWSettingsPanel::EffectsChanged);
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
 if(OwnerScreen)OwnerScreen->ApplyWidgetPresentation(this);RefreshVolumes();
 auto Label=[this](const TCHAR* N,const TCHAR* CN,const TCHAR* EN){if(auto* T=Cast<UTextBlock>(GetWidgetFromName(N))){T->SetAutoWrapText(false);T->SetText(DWText(this,CN,EN));}};
 Label(TEXT("AudioTabButton_Label"),TEXT("声音"),TEXT("Audio"));Label(TEXT("ControlsTabButton_Label"),TEXT("按键绑定"),TEXT("Controls"));Label(TEXT("ResetKeysButton_Label"),TEXT("恢复默认按键"),TEXT("Restore default keys"));Label(TEXT("CancelBindingButton_Label"),TEXT("取消改键"),TEXT("Cancel rebinding"));
 if(KeyRows)for(auto* W:KeyRows->GetAllChildren())if(auto* R=Cast<UDWKeyBindingRow>(W))R->Refresh();
 if(CancelBindingButton)CancelBindingButton->SetVisibility(IsCapturing()?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(!IsCapturing())SetStatus(DWText(this,TEXT("点击一个按键后，按新的键盘键或鼠标按钮。更改自动保存。PIE 中 Esc 会停止预览，可用暂停备用键。"),TEXT("Click a key, then press a keyboard key or mouse button. Changes save automatically. Esc stops PIE; use the alternate pause key.")));
}
void UDWSettingsPanel::MusicChanged(float V){if(!bRefreshing)if(auto* S=UDWUserSettings::Resolve(this)){S->SetCategoryVolume(EDWSoundCategory::Music,V);RefreshVolumes();}}
void UDWSettingsPanel::VoiceChanged(float V){if(!bRefreshing)if(auto* S=UDWUserSettings::Resolve(this)){S->SetCategoryVolume(EDWSoundCategory::Voice,V);RefreshVolumes();}}
void UDWSettingsPanel::EffectsChanged(float V){if(!bRefreshing)if(auto* S=UDWUserSettings::Resolve(this)){S->SetCategoryVolume(EDWSoundCategory::SFX,V);RefreshVolumes();}}
void UDWSettingsPanel::AudioTab(){Sound();CancelCapture();if(OptionsSwitcher)OptionsSwitcher->SetActiveWidgetIndex(0);}
void UDWSettingsPanel::ControlsTab(){Sound();if(OptionsSwitcher)OptionsSwitcher->SetActiveWidgetIndex(1);RefreshPanel();}
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
