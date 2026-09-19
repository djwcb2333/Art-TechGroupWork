#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWGameplayTypes.h"
#include "DWInventoryComponent.generated.h"

class UDWGameplayConfig;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWInventoryChanged);

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

    UPROPERTY(BlueprintAssignable, Category="DoughWorld|Inventory")
    FDWInventoryChanged OnChanged;

private:
    UPROPERTY(Transient)
    TObjectPtr<UDWGameplayConfig> Config;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, SaveGame, Category="DoughWorld|Inventory", meta=(AllowPrivateAccess="true"))
    TArray<FDWItemStack> Slots;

    const UDWGameplayConfig* GetConfig() const;
    bool AddToSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const;
    bool RemoveFromSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const;
    bool MakeCraftedSlots(FName RecipeId, bool bYeast, TArray<FDWItemStack>& OutSlots) const;
};
