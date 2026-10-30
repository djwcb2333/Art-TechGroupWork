#pragma once

#include "CoreMinimal.h"
#include "Components/WidgetComponent.h"
#include "InputCoreTypes.h"
#include "DWDemolitionPromptLease.h"
#include "DWInteractionPromptComponent.generated.h"

class UDWInteractionPromptStyle;
class UDWInteractionPromptWidget;
class APawn;
class ADWPlayerCharacter;

/** Presentation only. Resource owners use the player's harvest focus; other actors use proximity.
 * Adding this component to a generic actor does not bind F or implement an interaction action. */
UCLASS(Blueprintable, ClassGroup=(DoughWorld), meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWInteractionPromptComponent : public UWidgetComponent
{
    GENERATED_BODY()
public:
    UDWInteractionPromptComponent();
    virtual void OnRegister() override;
    virtual void InitWidget() override;
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
    /** Opt-in only for new gathering-resource placements. Existing instances retain the old behavior. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Performance") bool bBudgetGatheringResourcePrompt = false;
    /** Seconds between hidden-resource proximity checks; 0 keeps per-frame checks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Performance", meta=(ClampMin="0", ClampMax="0.5", Units="s", EditCondition="bBudgetGatheringResourcePrompt")) float GatheringHiddenPollSeconds = .1f;
    /** Empty uses DA_DWInteractionPromptStyle, loaded on BeginPlay. Widget Class is inherited below. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Style") TObjectPtr<UDWInteractionPromptStyle> Style;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Audio") bool bPlayAppearSound = true;
    /** By default both local and shared sound cooldowns use Style.SoundCooldown (0.65 seconds). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Audio") bool bOverrideSoundCooldown = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Prompt|Audio", meta=(ClampMin="0.05", Units="s", EditCondition="bOverrideSoundCooldown")) float SoundCooldownOverride = .65f;

    UFUNCTION(BlueprintCallable, Category="Interaction Prompt") void SetPromptEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category="Interaction Prompt|Placement") void RefreshWorldPlacement();
    UFUNCTION(BlueprintPure, Category="Interaction Prompt") UDWInteractionPromptWidget* GetPromptWidget() const;
    /** Called only by ResourceNode after an accepted inventory harvest has consumed stock. */
    void NotifyHarvestCommitted(int32 ActualAmount);
    /** Call ONCE after a valid tool-hit durability commit, before presentation callbacks.
     * Reads committed progress only; never damages a node, awards items or plays a swing. */
    bool NotifyGatheringHitCommitted(ADWPlayerCharacter* User,float PreviousProgress,float CurrentProgress);
    /** Only a short visual tail; never use this for collision or interaction availability. */
    bool HasPendingGatheringHitFeedback() const;
    /** Call on explicit restore/reset of this same node, before publishing its new state. */
    void ResetGatheringHitFeedback();
    /** Presentation lease: caller owns work validation, transaction and cancellation. */
    bool BeginDemolitionSession(APawn* User,FGuid Session,FKey AppliedKey);
    bool UpdateDemolitionSession(FGuid Session,float AuthoritativeProgress);
    void EndDemolitionSession(FGuid Session);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Interaction Prompt|Demolition",meta=(ClampMin="0.1",ClampMax="2",Units="s")) float DemolitionStaleSeconds=.5f;
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
    bool bAllowBudgetedWidgetInit = false;
    bool bBudgetOwnsTickInterval = false;
    float BudgetTickInterval = 0.f;
    bool UsesGatheringPromptBudget() const;
    bool IsGatheringPromptNearLocalPawn() const;
    void UpdateGatheringPromptBudget(bool bIdle);
    void WakeGatheringPromptBudget();
    double C04CompletionDeadline = 0.;
    double C06GatheringFeedbackDeadline = 0.;
    float C06LastNotifiedGatheringProgress = -1.f;
    TWeakObjectPtr<APawn> C06GatheringFeedbackPawn;
    TWeakObjectPtr<APawn> C04CompletionPawn;
    TWeakObjectPtr<APawn> DemolitionPawn;
    DWDemolitionPromptLease::Lease DemolitionLease;
    FKey DemolitionKey;
    void ResetDemolitionPresentation();
    void PrepareWidget();
    void ChangePromptVisibility(bool bVisible, bool bImmediate = false);
    void TryPlayAppearSound();
    void RefreshDebugVisibility();
};
