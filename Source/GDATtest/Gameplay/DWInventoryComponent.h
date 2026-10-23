#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWGameplayTypes.h"
#include "DWInventoryComponent.generated.h"

class UDWGameplayConfig;
class UDWInventoryComponent;
class FDWCombatInventoryTransaction;
class FDWCombatEquipmentTransaction;
class FDWCombatRules;
class FDWGatheringInventoryTransaction;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWInventoryChanged);

/** A drag/confirmation references one exact inventory revision; it never removes items early. */
USTRUCT(BlueprintType)
struct FDWInventorySlotSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Inventory") int32 SlotIndex=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="Inventory") FName ItemId;
    UPROPERTY(BlueprintReadOnly, Category="Inventory") int32 Quantity=0;
    UPROPERTY(BlueprintReadOnly, Category="Inventory") int64 Revision=INDEX_NONE;
};

/** C++-only, owner-bound, single-use candidate. Preparation never edits the live inventory. */
class GDATTEST_API FDWPreparedInventoryChange final
{
public:
    FDWPreparedInventoryChange() = default;
    FDWPreparedInventoryChange(const FDWPreparedInventoryChange&) = delete;
    FDWPreparedInventoryChange& operator=(const FDWPreparedInventoryChange&) = delete;
    FDWPreparedInventoryChange(FDWPreparedInventoryChange&& Other) noexcept;
    FDWPreparedInventoryChange& operator=(FDWPreparedInventoryChange&& Other) noexcept;
    const TArray<FDWItemStack>& GetCandidateSlots() const { return CandidateSlots; }
    int64 GetExpectedRevision() const { return ExpectedRevision; }
    bool IsConsumed() const { return bConsumed; }
    FName GetCraftRecipeId() const { return RequiredCraftRecipe; }
    FName GetBuildingOutput() const { return RequiredBuildingOutput; }
private:
    friend class UDWInventoryComponent;
    TWeakObjectPtr<const UDWInventoryComponent> Owner;
    TArray<FDWItemStack> CandidateSlots;
    int64 ExpectedRevision=INDEX_NONE;
    FName RequiredUsableItem;
    FName RequiredCraftRecipe,RequiredBuildingOutput;
    bool bConsumed=true;
    void Reset();
};

/** Every inventory mutation succeeds completely or leaves all slots unchanged. */
UCLASS(ClassGroup=(DoughWorld), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    UDWInventoryComponent();
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

    // No Blueprint transaction handles and no public notification-suppression window.
    // A failed preparation invalidates OutPrepared; the live inventory stays unchanged.
    bool PrepareAddItem(FName ItemId, int32 Quantity, FDWPreparedInventoryChange& OutPrepared) const;
    /** A positive partial result is prepared once; caller commits its world remainder atomically. */
    bool PrepareAddItemUpTo(FName ItemId,int32 Requested,int32& OutAccepted,FDWPreparedInventoryChange& OutPrepared) const;
    /** Uses the same station/placement checks as CanCraft. Building candidates remove inputs only. */
    bool PrepareCraftRecipe(FName RecipeId,bool bYeast,FDWPreparedInventoryChange& OutPrepared) const;
    bool PrepareRemoveItem(FName ItemId, int32 Quantity, FDWPreparedInventoryChange& OutPrepared) const;
    bool PrepareRemoveSnapshot(const FDWInventorySlotSnapshot& Snapshot, int32 Quantity, FDWPreparedInventoryChange& OutPrepared) const;
    bool PrepareConsumeSnapshot(const FDWInventorySlotSnapshot& Snapshot, FDWPreparedInventoryChange& OutPrepared) const;
    bool CommitPrepared(FDWPreparedInventoryChange&& Prepared);

    // Explicit rule bridge only. Unknown ids, non-positive caps, or invalidated current slots reject.
    // Success invalidates outstanding prepared changes even when the supplied policy is identical.
    bool TrySetStackLimitOverrides(const TMap<FName,int32>& InOverrides);
    int32 GetEffectiveStackLimit(FName ItemId) const;
    UFUNCTION(BlueprintPure, Category="DoughWorld|Inventory")
    int32 GetAvailableCapacity(FName ItemId) const;
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool TryAddItemUpTo(FName ItemId,int32 Requested,int32& OutAccepted);
    /** Native query only: no mutations/callbacks. Bind a weak gathering component; absent hook rejects requirements. */
    void SetCraftingContextValidator(TFunction<bool(FName,FName,FName)> InValidator);
    // Formal core caps preserve other item policies and reject incompatible existing stacks.
    bool TryApplyCombatStackPolicy(const FDWCombatRules& Rules, FString& OutError);

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    void InitializeInventory(UDWGameplayConfig* InConfig);

    const TArray<FDWItemStack>& GetSlots() const { return Slots; }

    UFUNCTION(BlueprintPure, Category="DoughWorld|Inventory")
    TArray<FDWItemStack> GetInventorySlots() const { return Slots; }

    UFUNCTION(BlueprintPure, Category="DoughWorld|Inventory")
    int32 CountItem(FName ItemId) const;

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool TryAddItem(FName ItemId, int32 Quantity);

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool TryRemoveItem(FName ItemId, int32 Quantity);

    UFUNCTION(BlueprintPure, Category="DoughWorld|Crafting")
    bool CanCraft(FName RecipeId, bool bYeast) const;

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Crafting")
    bool TryCraft(FName RecipeId, bool bYeast);

    /** Consumes exactly one usable item. The player applies its configured effects. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool ConsumeOneAtSlot(int32 SlotIndex);

    /** Rejects unknown items, invalid quantities and over-capacity saves without losing current items. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool SetSlots(const TArray<FDWItemStack>& InSlots);

    UFUNCTION(BlueprintPure, Category="DoughWorld|Inventory")
    bool AreSlotsValid(const TArray<FDWItemStack>& InSlots) const;

    UFUNCTION(BlueprintPure, Category="DoughWorld|Inventory")
    bool CaptureSlotSnapshot(int32 SlotIndex, FDWInventorySlotSnapshot& OutSnapshot) const;

    UFUNCTION(BlueprintPure, Category="DoughWorld|Inventory")
    bool IsSlotSnapshotCurrent(const FDWInventorySlotSnapshot& Snapshot) const;

    /** Permanently removes the selected stack only if nothing changed since confirmation opened. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool TryDiscardSnapshot(const FDWInventorySlotSnapshot& Snapshot);

    /** Splits to the next free compact slot, preserving every item when the bag is full. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool TrySplitSlot(int32 SlotIndex, int32 Quantity, int32& OutNewSlot);

    /** Combines partial stacks of this item within the bag; None combines all item types. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool TryQuickStack(FName ItemId);

    /** Same item merges up to the cap; different item swaps; empty slots move to the compact end. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Inventory")
    bool TryMoveOrMergeSnapshot(const FDWInventorySlotSnapshot& Snapshot, int32 TargetSlot);

    UFUNCTION(BlueprintPure, Category="DoughWorld|Inventory")
    int64 GetInventoryRevision() const { return Revision; }

    UPROPERTY(BlueprintAssignable, Category="DoughWorld|Inventory")
    FDWInventoryChanged OnChanged;


private:
    // Only the concrete typed resource transaction may validate/adopt before its unified notification.
    friend class FDWCombatBowShotTransaction;
    friend class FDWCombatSporeShotTransaction;
    friend class FDWCombatBottleTransaction;
    friend class FDWCombatInventoryTransaction;
    friend class FDWCombatEquipmentTransaction;
    friend class FDWCombatRecoveryTransaction;
    friend class UDWCombatResourceVerificationLibrary;
    friend class FDWCombatPlayerStateTransaction;
    friend class FDWCombatPlayerPersistence;
    friend class FDWCombatCoreInjectionTransaction;
    friend class FDWCombatCorePickupTransaction;
    friend class FDWGatheringInventoryTransaction;
    int64 Revision=0;
    bool bInventoryLifecycleEnded=false;
    UPROPERTY(Transient)
    TMap<FName,int32> StackLimitOverrides;
    UPROPERTY(Transient)
    TObjectPtr<UDWGameplayConfig> Config;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, SaveGame, Category="DoughWorld|Inventory", meta=(AllowPrivateAccess="true"))
    TArray<FDWItemStack> Slots;


    const UDWGameplayConfig* GetConfig() const;
    bool CanWriteInventory() const;
    bool ValidatePreparedForCommit(const FDWPreparedInventoryChange& Prepared) const;
    void AdoptPreparedUnchecked(FDWPreparedInventoryChange&& Prepared);
    bool AreSlotsValidWithPolicy(const TArray<FDWItemStack>& InSlots, const UDWGameplayConfig* Settings,
        const TMap<FName,int32>& Overrides) const;
    bool MakePrepared(TArray<FDWItemStack>&& Candidate, FDWPreparedInventoryChange& OutPrepared) const;
    bool CommitCandidate(TArray<FDWItemStack>&& Candidate);
    bool AddToSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const;
    bool RemoveFromSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const;
    bool MakeCraftedSlots(FName RecipeId, bool bYeast, TArray<FDWItemStack>& OutSlots) const;
    bool CanUseCraftingContext(FName RecipeId,FName& OutBuilding) const;
    TFunction<bool(FName,FName,FName)> CraftingContextValidator;
};
