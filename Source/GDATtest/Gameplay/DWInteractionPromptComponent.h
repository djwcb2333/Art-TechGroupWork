#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "DWInteractionPromptComponent.generated.h"

class UDWInteractionPromptStyle;
class UDWInteractionPromptWidget;

/** Presentation only. Resource owners use the player's harvest focus; other actors use proximity.
 * Adding this component to a generic actor does not bind F or implement an interaction action. */
UCLASS(Blueprintable, ClassGroup=(DoughWorld), meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWInteractionPromptComponent : public UWidgetComponent
{
    GENERATED_BODY()
public:
    UDWInteractionPromptComponent();
    virtual void OnRegister() override;
    virtual void Deactivate() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt") bool bPromptEnabled = true;
    /** World-space centimeters above the owner's origin, unaffected by owner rotation or scale. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Placement", meta=(Units="cm")) FVector WorldOffset = FVector(0, 0, 145);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Generic") FText GenericPromptTitle = FText::FromString(TEXT("可交互物体"));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Generic") FText GenericPromptAction = FText::FromString(TEXT("交互"));
    /** Optional English overrides; an empty value retains the authored generic text. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Generic") FText GenericEnglishPromptTitle = FText::FromString(TEXT("Interact"));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Generic") FText GenericEnglishAction = FText::FromString(TEXT("Hold to interact"));
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Generic") FText GenericPromptKey = FText::FromString(TEXT("F"));
    /** Generic actors only. Resource owners use their existing InteractionRadius and player focus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Detection", meta=(ClampMin="1", Units="cm")) float GenericDetectionRadius = 220.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Detection", meta=(ClampMin="0", Units="cm")) float HeightTolerance = 300.f;
    /** Empty uses DA_DWInteractionPromptStyle, loaded on BeginPlay. Widget Class is inherited below. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Style") TObjectPtr<UDWInteractionPromptStyle> Style;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Audio") bool bPlayAppearSound = true;
    /** By default both local and shared sound cooldowns use Style.SoundCooldown (0.65 seconds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Audio") bool bOverrideSoundCooldown = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Audio", meta=(ClampMin="0.05", Units="s", EditCondition="bOverrideSoundCooldown")) float SoundCooldownOverride = .65f;

    UFUNCTION(BlueprintCallable, Category="Interaction Prompt") void SetPromptEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="Interaction Prompt|Placement") void RefreshWorldPlacement();
    UFUNCTION(BlueprintPure, Category="Interaction Prompt") UDWInteractionPromptWidget* GetPromptWidget() const;
    UFUNCTION(BlueprintImplementableEvent, Category="Interaction Prompt|Events") void OnPromptShown();
    UFUNCTION(BlueprintImplementableEvent, Category="Interaction Prompt|Events") void OnPromptHidden();

    /** Target state; remains false throughout a fade-out. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Interaction Prompt|Debug") bool bPromptDesiredVisible = false;
    /** Presentation opacity is nonzero, including the fade-out. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Interaction Prompt|Debug") bool bPromptVisible = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Interaction Prompt|Debug") float PresentationOpacity = 0.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Interaction Prompt|Debug") int32 PromptShownCount = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Interaction Prompt|Debug") int32 PromptHiddenCount = 0;
    /** Counts actual sound dispatches, not cooldown-suppressed appearance transitions. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Interaction Prompt|Debug") int32 SoundPlayCount = 0;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
private:
    TWeakObjectPtr<UDWInteractionPromptWidget> InitializedWidget;
    TWeakObjectPtr<UDWInteractionPromptStyle> AppliedStyle;
    double NextLocalSoundTime = 0.0;
    bool bChangingVisibility = false;
    void PrepareWidget();
    void ChangePromptVisibility(bool bVisible, bool bImmediate = false);
    void TryPlayAppearSound();
    void RefreshDebugVisibility();
};
