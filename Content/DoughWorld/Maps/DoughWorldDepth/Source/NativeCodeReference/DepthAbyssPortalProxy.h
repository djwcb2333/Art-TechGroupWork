#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Actor.h"
#include "DepthAbyssPortalProxy.generated.h"

class UTextBlock;
class USceneComponent;
class APlayerController;
class APawn;

/** Small non-interactive screen prompt. Never takes keyboard focus or changes input mode. */
UCLASS()
class GDATTEST_API UDepthAbyssPromptWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void SetPrompt(const FText& Text);
protected:
    virtual void NativeOnInitialized() override;
private:
    UPROPERTY(Transient) TObjectPtr<UTextBlock> PromptText;
};

/** One-way interaction proxy for hole_abyss, not a fall trigger or a finished cinematic. */
UCLASS(Blueprintable)
class GDATTEST_API ADepthAbyssPortalProxy : public AActor
{
    GENERATED_BODY()
public:
    ADepthAbyssPortalProxy();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Abyss Portal") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Abyss Portal", meta=(ClampMin="50", Units="cm")) float InteractionRadiusCm = 350.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Abyss Portal", meta=(ClampMin="0")) int32 LocalPlayerIndex = 0;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Abyss Portal") TObjectPtr<AActor> UndergroundDestinationActor;
    /** World-space centimetres, used only when no destination actor is assigned. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Abyss Portal", meta=(Units="cm")) FVector UndergroundDestination = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Abyss Portal") bool bDestinationConfigured = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Abyss Portal", meta=(ClampMin="0", Units="s")) float EntryCooldownSeconds = 1.f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Abyss Portal|State") bool bPlayerInRange = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Abyss Portal|State") bool bAwaitingConfirmation = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Abyss Portal|State") int32 ConfirmedEntryCount = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Abyss Portal|State") FText LastEntryError;

    /** Same first stage used by E. Returns false outside range, in air/roll, or without a destination. */
    UFUNCTION(BlueprintCallable, Category="Abyss Portal") bool RequestEntryConfirmation();
    /** Same second stage used by Enter. Requires a still-valid first stage; no direct bypass teleport. */
    UFUNCTION(BlueprintCallable, Category="Abyss Portal") bool ConfirmEntry();
    UFUNCTION(BlueprintCallable, Category="Abyss Portal") void CancelEntry();
    UFUNCTION(BlueprintPure, Category="Abyss Portal") bool CanRequestEntry() const;
protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(Transient) TObjectPtr<UDepthAbyssPromptWidget> PromptWidget;
    TWeakObjectPtr<APlayerController> PromptOwner;
    TWeakObjectPtr<APawn> ConfirmingPawn;
    double NextEntryTime = 0.0;
    APawn* GetLocalPawn(APlayerController*& OutController) const;
    void UpdatePrompt(APlayerController* Controller);
};
