#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWGameplayTypes.h"
#include "DWInventoryComponent.generated.h"

class UDWGameplayConfig;

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

/** Every inventory mutation succeeds completely or leaves all slots unchanged. */
UCLASS(ClassGroup=(DoughWorld), BlueprintType, Blueprintable, meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWInventoryComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDWInventoryComponent();

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
    int64 Revision=0;
    UPROPERTY(Transient)
    TObjectPtr<UDWGameplayConfig> Config;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, SaveGame, Category="DoughWorld|Inventory", meta=(AllowPrivateAccess="true"))
    TArray<FDWItemStack> Slots;

    const UDWGameplayConfig* GetConfig() const;
    bool AddToSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const;
    bool RemoveFromSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const;
    bool MakeCraftedSlots(FName RecipeId, bool bYeast, TArray<FDWItemStack>& OutSlots) const;
};
