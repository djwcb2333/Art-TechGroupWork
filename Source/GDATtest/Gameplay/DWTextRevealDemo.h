#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/HUD.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWTextRevealDemo.generated.h"
class UTextBlock;
class UButton;
class UDWTextRevealComponent;

/** Optional isolated test screen; never injected into the existing frontend. */
UCLASS()
class GDATTEST_API UDWTextRevealDemoWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Demo",meta=(MultiLine=true)) FText EnglishLine=FText::FromString(TEXT("Welcome to Dough World!\nGather water, shape your dough, and explore together."));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Demo",meta=(MultiLine=true)) FText ChineseLine=FText::FromString(TEXT("欢迎来到面团世界！\n收集清水，揉好面团，一起出发吧。"));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Demo",meta=(MultiLine=true)) FText MixedLine=FText::FromString(TEXT("Hello，面团伙伴！\nLet's explore 麦谷 together. 水 × 2 + 面粉 × 2。"));
 UFUNCTION(BlueprintPure,Category="Demo") UDWTextRevealComponent* GetReveal() const;
 UFUNCTION(BlueprintCallable,Category="Demo") void PlayEnglish();
 UFUNCTION(BlueprintCallable,Category="Demo") void PlayChinese();
 UFUNCTION(BlueprintCallable,Category="Demo") void PlayMixed();
protected:
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> DialogueText;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> StatusText;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> EnglishButton;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ChineseButton;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> MixedButton;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ReplayButton;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> PauseButton;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> SkipButton;
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ClearButton;
 virtual void NativeConstruct() override;
 virtual void NativeTick(const FGeometry&,float) override;
private:
 UFUNCTION() void ReplayLine();
 UFUNCTION() void TogglePause();
 UFUNCTION() void SkipLine();
 UFUNCTION() void ClearLine();
};
UCLASS()
class GDATTEST_API ADWTextRevealDemoHUD : public AHUD
{
 GENERATED_BODY()
public:
 UPROPERTY(Transient,BlueprintReadOnly,Category="Demo") TObjectPtr<UDWTextRevealDemoWidget> DemoWidget;
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type) override;
};
UCLASS()
class GDATTEST_API ADWTextRevealDemoGameMode : public AGameModeBase
{
 GENERATED_BODY()
public:
 ADWTextRevealDemoGameMode();
};
UCLASS()
class GDATTEST_API UDWTextRevealAuthoringLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Editor-only, creates only missing demo assets. Never overwrites existing user examples. */
 UFUNCTION(BlueprintCallable,Category="UI|Text Reveal|Editor") static bool CreateDemoAssets();
 /** Editor-only repair: adds missing LogoText/TitleText reveal components; retains existing component settings. */
 UFUNCTION(BlueprintCallable,Category="UI|Text Reveal|Editor") static bool EnsureFrontendRevealComponents();
};
