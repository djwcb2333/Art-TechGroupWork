#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DWPlayerController.h"
#include "DWSettingsPanel.generated.h"
class UTextBlock;class UButton;class USlider;class UVerticalBox;class UWidgetSwitcher;class UDWGameplayWidget;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWBindingRequest,EDWInputAction,Action);
UCLASS(Blueprintable)
class GDATTEST_API UDWKeyBindingRow:public UUserWidget
{
 GENERATED_BODY()
public:
 UPROPERTY(BlueprintAssignable) FDWBindingRequest OnRequested;
 UFUNCTION(BlueprintCallable,Category="Settings") void SetupRow(EDWInputAction Action,UDWGameplayWidget* Screen);
 void Refresh();
protected:
 virtual void NativeConstruct()override;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> ActionLabel;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> BindingLabel;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> BindingButton;
private:
 EDWInputAction BoundAction=EDWInputAction::MoveForward;
 UFUNCTION()void ClickBinding();
};
UCLASS(Blueprintable)
class GDATTEST_API UDWSettingsPanel:public UUserWidget
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Settings") void InitializePanel(UDWGameplayWidget* Screen);
 UFUNCTION(BlueprintCallable,Category="Settings") void RefreshPanel();
 /** The outer settings book owns navigation: 0 audio, 1 key bindings, 2 controls guide. */
 UFUNCTION(BlueprintCallable,Category="Settings") void SetBookPage(int32 OptionsIndex);
 UFUNCTION(BlueprintPure,Category="Settings") bool IsCapturing()const{return Capturing!=EDWInputAction::Count;}
 UFUNCTION(BlueprintCallable,Category="Settings") void RequestBinding(EDWInputAction Action);
 UFUNCTION(BlueprintCallable,Category="Settings") void CancelCapture();
 bool CaptureKey(FKey Key);bool IsCancelHit(FVector2D Position)const;
 UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Settings") TSubclassOf<UDWKeyBindingRow> BindingRowClass;
protected:
 virtual void NativeConstruct()override;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> AudioTabButton;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> ControlsTabButton;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> ControllerTabButton;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UWidgetSwitcher> OptionsSwitcher;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<USlider> MusicSlider;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<USlider> VoiceSlider;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<USlider> EffectsSlider;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> MusicLabel;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> VoiceLabel;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> EffectsLabel;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UVerticalBox> KeyRows;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> BindingStatus;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> ResetKeysButton;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> CancelBindingButton;
 /** Read-only keyboard/mouse guide; the existing rebinding page remains separate. */
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> ControlsGuideText;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> InventoryShortcutsText;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<USlider> CameraSensitivitySlider;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> CameraSensitivityLabel;
 UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> CameraSensitivityResetButton;
private:
 UPROPERTY(Transient) TObjectPtr<UDWGameplayWidget> OwnerScreen;
 EDWInputAction Capturing=EDWInputAction::Count;bool bRefreshing=false;bool bBookNavigation=false;
 UFUNCTION()void AudioTab();UFUNCTION()void ControlsTab();UFUNCTION()void ControllerTab();UFUNCTION()void ResetKeys();
 UFUNCTION()void MusicChanged(float V);UFUNCTION()void VoiceChanged(float V);UFUNCTION()void EffectsChanged(float V);
 UFUNCTION()void CameraSensitivityChanged(float Value);UFUNCTION()void ResetCameraSensitivity();
 void SetStatus(FText Text);void Sound();void RefreshVolumes();void RefreshControlsGuide();void RefreshCameraSensitivity();
};
