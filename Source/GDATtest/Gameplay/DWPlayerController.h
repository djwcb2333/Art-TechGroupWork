#pragma once
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "DWCombatInputLedger.h"
#include "DWPlayerController.generated.h"

class ADWPlayerCharacter;
class UDWWeaponAnimationComponent;
class FDWCombatInputAdapter;
struct FDWInputBindingRelay;

/** Stable action identifiers used by gameplay input and localized UI key labels. */
UENUM(BlueprintType)
enum class EDWInputAction : uint8
{
    MoveForward, MoveBackward, MoveLeft, MoveRight,
    Dash, Sprint, SprintAlternate, Harvest, Transform, Throw, CameraDrag,
    Inventory, Crafting, PauseMenu, PauseAlternate,
    Melee,
    Character, Building, LanguageJournal, PreviousBookTab, NextBookTab,
    Reload, SelectUnarmed, SelectWeapon1, SelectWeapon2, SelectWeapon3, SelectWeapon4, SelectWeapon5, SelectWeapon6, SelectWeapon7, SelectWeapon8,
    Count UMETA(Hidden)
};

UCLASS(Blueprintable)
class GDATTEST_API ADWPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    void RequestFormalDashInput(){Dash();}
public:
    virtual void GetPlayerViewPoint(FVector& Location,FRotator& Rotation)const override;
    ADWPlayerController();
    virtual void PlayerTick(float DeltaSeconds) override;
    bool IsGameplayBlocked() const;
    // Native lifecycle/pause adapter. None of these invents a physical input edge.
    void BeginCombatMenuSuspension();
    void PrepareCombatInputForSimulation();
    void InvalidateCombatInputLife();
    bool IsCombatInputSuspended() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Input|Debug") FString GetCombatInputDebugState() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Camera") bool IsCameraInputAllowed() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Input") TArray<FKey> GetConfiguredKeys() const;
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Input") bool SetActionBinding(EDWInputAction Action,FKey Key,FText& Error,bool bPersist=true);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Input") bool RestoreAuthoredBindings(FText& Error);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Input") FText GetActionDisplayName(EDWInputAction Action) const;
    /** Keep retired enum ids for stored-binding compatibility; expose only current actions. */
    UFUNCTION(BlueprintPure,Category="DoughWorld|Input") static bool IsBindableInputAction(EDWInputAction Action)
    {return Action<EDWInputAction::SelectWeapon4||Action>EDWInputAction::SelectWeapon8;}
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Interaction") FKey KeyMelee = EKeys::RightMouseButton;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Camera") FKey KeyCameraDrag = EKeys::MiddleMouseButton;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyInventory = EKeys::B;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyCrafting = EKeys::Tab;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyPauseMenu = EKeys::Escape;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyPauseAlternate = EKeys::P;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyCharacter=EKeys::C;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyBuilding=EKeys::H;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyLanguageJournal=EKeys::I;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyPreviousBookTab=EKeys::Q;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|UI") FKey KeyNextBookTab=EKeys::E;
    /** Runtime combat shortcuts. New identifiers append after the original21 entries. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeyReload = EKeys::R;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectUnarmed = EKeys::Zero;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectWeapon1 = EKeys::One;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectWeapon2 = EKeys::Two;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectWeapon3 = EKeys::Three;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectWeapon4 = EKeys::Four;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectWeapon5 = EKeys::Five;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectWeapon6 = EKeys::Six;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectWeapon7 = EKeys::Seven;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DoughWorld|Input|Weapons") FKey KeySelectWeapon8 = EKeys::Eight;
    /** Only page navigation is scoped to the handbook; gameplay may reuse those keys. */
    static bool DoBindingContextsOverlap(int32 A,int32 B);

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
    void Transform(); void Throw(); void Melee(); void Inventory(); void Crafting(); void PauseMenu();
    void WeaponPrimaryReleased(); void WeaponSecondaryReleased();
    void Character();void Building();void LanguageJournal();void PreviousBookTab();void NextBookTab();
    bool IsActionDown(EDWInputAction Action) const;

    void RefreshCombatInputKeys();
    void ImportLegacyCombatShortcutDefaults();
    bool ConsumeCombatPress(FKey Key,FName Action,FDWCombatInputPress& OutPress);
    void CompleteCombatHold(UDWWeaponAnimationComponent* Weapons,const FDWCombatInputPress& Press,bool bSecondary);
    TSharedPtr<FDWCombatInputAdapter> CombatInputAdapter;
    TArray<FDWCombatInputRelease> PendingCombatReleases;
    // Gathering-only accepted physical press. Level polling cannot create a new press after UI cancellation.
    FDWCombatInputPress GatheringHarvestPress;
    bool bGatheringHarvestPressActive=false;
    uint64 AppliedObservedReleaseCount=0;
    uint64 LastObservedReleaseSequence=0;
    float LastObservedReleaseWorldTime=-1.f;
    bool bProcessingCombatInputs=false;
    void CommitQueuedBindings();
    void RemoveOwnedKeyBindings();
    void AddOwnedBinding(FKey Key, EInputEvent Event, void(ADWPlayerController::*Method)(), bool bWhenPaused = false);
    TArray<FKey> AppliedKeys;
    TArray<FKey> PendingKeys;
    TArray<TSharedPtr<FDWInputBindingRelay>> OwnedKeyRelays;
    TWeakObjectPtr<UInputComponent> OwnedInputComponent;
};
