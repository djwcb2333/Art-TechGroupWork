#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "DWPlayerController.generated.h"

class ADWPlayerCharacter;
struct FDWInputBindingRelay;

/** Stable action identifiers used by gameplay input and localized UI key labels. */
UENUM(BlueprintType)
enum class EDWInputAction : uint8
{
    MoveForward, MoveBackward, MoveLeft, MoveRight,
    Dash, Sprint, SprintAlternate, Harvest, Transform, Throw, CameraDrag,
    Inventory, Crafting, PauseMenu, PauseAlternate,
    Count UMETA(Hidden)
};

UCLASS(Blueprintable)
class GDATTEST_API ADWPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual void GetPlayerViewPoint(FVector& Location,FRotator& Rotation)const override;
    ADWPlayerController();
    virtual void PlayerTick(float DeltaSeconds) override;
    bool IsGameplayBlocked() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Input") TArray<FKey> GetConfiguredKeys() const;
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Input") bool SetActionBinding(EDWInputAction Action,FKey Key,FText& Error,bool bPersist=true);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Input") bool RestoreAuthoredBindings(FText& Error);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Input") FText GetActionDisplayName(EDWInputAction Action) const;
    void LoadSavedBindings(); void SaveBindings() const;
    void AssignKeys(const TArray<FKey>& Keys);

    /** Edit in BP_DWPlayerController Class Defaults; at runtime call ApplyInputBindings after changing fields. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Movement") FKey KeyMoveForward = EKeys::W;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Movement") FKey KeyMoveBackward = EKeys::S;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Movement") FKey KeyMoveLeft = EKeys::A;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Movement") FKey KeyMoveRight = EKeys::D;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Movement") FKey KeyDash = EKeys::SpaceBar;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Movement") FKey KeySprint = EKeys::LeftShift;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Movement") FKey KeySprintAlternate = EKeys::RightShift;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Interaction") FKey KeyHarvest = EKeys::F;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Interaction") FKey KeyTransform = EKeys::E;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Interaction") FKey KeyThrow = EKeys::LeftMouseButton;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Camera") FKey KeyCameraDrag = EKeys::RightMouseButton;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyInventory = EKeys::B;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyCrafting = EKeys::Tab;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyPauseMenu = EKeys::Escape;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyPauseAlternate = EKeys::P;

    /** Validates the editable fields only. None disables an action except PauseMenu, which must stay assigned. */
    UFUNCTION(BlueprintPure, Category="DoughWorld|Input") bool ValidateInputBindings(FText& OutError) const;
    /** Returns whether the request was accepted. Commits next PlayerTick, outside input delegate iteration. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Input") bool ApplyInputBindings(FText& OutError);
    /** Restores the original keyboard/mouse layout and queues a safe application. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Input") bool ResetInputBindingsToDefaults(FText& OutError);
    /** Returns the active key, never an unvalidated edit or an unapplied queued key. */
    UFUNCTION(BlueprintPure, Category="DoughWorld|Input") FKey GetAppliedActionKey(EDWInputAction Action) const;
    UFUNCTION(BlueprintPure, Category="DoughWorld|Input") FText GetActionKeyLabel(EDWInputAction Action) const;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="DoughWorld|Input") bool bInputBindingsPending = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="DoughWorld|Input") int32 InputBindingsRevision = 0;

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    ADWPlayerCharacter* Player() const;
    void Dash(); void SprintDown(); void SprintUp(); void HarvestDown(); void HarvestUp();
    void Transform(); void Throw(); void Inventory(); void Crafting(); void PauseMenu();
    bool IsActionDown(EDWInputAction Action) const;

    void CommitQueuedBindings();
    void RemoveOwnedKeyBindings();
    void AddOwnedBinding(FKey Key, EInputEvent Event, void(ADWPlayerController::*Method)(), bool bWhenPaused = false);
    TArray<FKey> AppliedKeys;
    TArray<FKey> PendingKeys;
    TArray<TSharedPtr<FDWInputBindingRelay>> OwnedKeyRelays;
    TWeakObjectPtr<UInputComponent> OwnedInputComponent;
};
