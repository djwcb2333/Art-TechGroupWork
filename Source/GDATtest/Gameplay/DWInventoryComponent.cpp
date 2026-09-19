#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"

UDWInventoryComponent::UDWInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

const UDWGameplayConfig* UDWInventoryComponent::GetConfig() const
{
    return Config ? Config.Get() : GetDefault<UDWGameplayConfig>();
}

void UDWInventoryComponent::InitializeInventory(UDWGameplayConfig* InConfig)
{
    UDWGameplayConfig* Previous = Config;
    Config = InConfig;
    if (!AreSlotsValid(Slots))
    {
        Config = Previous;
        UE_LOG(LogTemp, Warning, TEXT("DW inventory rejected configuration that would invalidate its existing contents."));
        return;
    }
    OnChanged.Broadcast();
}

int32 UDWInventoryComponent::CountItem(FName ItemId) const
{
    int64 Total = 0;
    for (const FDWItemStack& Stack : Slots)
        if (Stack.ItemId == ItemId) Total += Stack.Quantity;
    return static_cast<int32>(FMath::Min<int64>(Total, MAX_int32));
}

bool UDWInventoryComponent::AreSlotsValid(const TArray<FDWItemStack>& InSlots) const
{
    const UDWGameplayConfig* Settings = GetConfig();
    if (Settings->MaxInventorySlots < 1 || Settings->MaxStackSize < 1 || InSlots.Num() > Settings->MaxInventorySlots)
        return false;
    for (const FDWItemStack& Stack : InSlots)
        if (Stack.ItemId.IsNone() || !Settings->GetItemDefinition(Stack.ItemId) ||
            Stack.Quantity < 1 || Stack.Quantity > Settings->MaxStackSize)
            return false;
    return true;
}

bool UDWInventoryComponent::AddToSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const
{
    const UDWGameplayConfig* Settings = GetConfig();
    if (Quantity <= 0 || ItemId.IsNone() || !Settings->GetItemDefinition(ItemId) || !AreSlotsValid(InOutSlots)) return false;

    int64 Capacity = static_cast<int64>(Settings->MaxInventorySlots - InOutSlots.Num()) * Settings->MaxStackSize;
    for (const FDWItemStack& Stack : InOutSlots)
        if (Stack.ItemId == ItemId) Capacity += Settings->MaxStackSize - Stack.Quantity;
    if (Capacity < Quantity) return false;

    int32 Remaining = Quantity;
    for (FDWItemStack& Stack : InOutSlots)
    {
        if (Stack.ItemId != ItemId) continue;
        const int32 Added = FMath::Min(Remaining, Settings->MaxStackSize - Stack.Quantity);
        Stack.Quantity += Added;
        Remaining -= Added;
        if (Remaining == 0) return true;
    }
    while (Remaining > 0)
    {
        const int32 Added = FMath::Min(Remaining, Settings->MaxStackSize);
        InOutSlots.Emplace(ItemId, Added);
        Remaining -= Added;
    }
    return true;
}

bool UDWInventoryComponent::RemoveFromSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const
{
    if (Quantity <= 0 || ItemId.IsNone() || !GetConfig()->GetItemDefinition(ItemId) || !AreSlotsValid(InOutSlots)) return false;
    int64 Available = 0;
    for (const FDWItemStack& Stack : InOutSlots)
        if (Stack.ItemId == ItemId) Available += Stack.Quantity;
    if (Available < Quantity) return false;

    int32 Remaining = Quantity;
    for (FDWItemStack& Stack : InOutSlots)
    {
        if (Stack.ItemId != ItemId) continue;
        const int32 Removed = FMath::Min(Remaining, Stack.Quantity);
        Stack.Quantity -= Removed;
        Remaining -= Removed;
        if (Remaining == 0) break;
    }
    InOutSlots.RemoveAll([](const FDWItemStack& Stack) { return Stack.Quantity == 0; });
    return true;
}

bool UDWInventoryComponent::TryAddItem(FName ItemId, int32 Quantity)
{
    TArray<FDWItemStack> Candidate = Slots;
    if (!AddToSlots(Candidate, ItemId, Quantity)) return false;
    Slots = MoveTemp(Candidate);
    OnChanged.Broadcast();
    return true;
}

bool UDWInventoryComponent::TryRemoveItem(FName ItemId, int32 Quantity)
{
    TArray<FDWItemStack> Candidate = Slots;
    if (!RemoveFromSlots(Candidate, ItemId, Quantity)) return false;
    Slots = MoveTemp(Candidate);
    OnChanged.Broadcast();
    return true;
}

bool UDWInventoryComponent::MakeCraftedSlots(FName RecipeId, bool bYeast, TArray<FDWItemStack>& OutSlots) const
{
    const FDWRecipeDefinition* Recipe = GetConfig()->GetRecipeDefinition(RecipeId);
    if (!Recipe || (Recipe->bRequiresYeast && !bYeast) || Recipe->Inputs.IsEmpty() || Recipe->Outputs.IsEmpty()) return false;
    OutSlots = Slots;
    // Inputs are removed first, so their emptied slots can hold outputs. The source never changes on failure.
    for (const FDWItemStack& Input : Recipe->Inputs)
        if (!RemoveFromSlots(OutSlots, Input.ItemId, Input.Quantity)) return false;
    for (const FDWItemStack& Output : Recipe->Outputs)
        if (!AddToSlots(OutSlots, Output.ItemId, Output.Quantity)) return false;
    return true;
}

bool UDWInventoryComponent::CanCraft(FName RecipeId, bool bYeast) const
{
    TArray<FDWItemStack> Candidate;
    return MakeCraftedSlots(RecipeId, bYeast, Candidate);
}

bool UDWInventoryComponent::TryCraft(FName RecipeId, bool bYeast)
{
    TArray<FDWItemStack> Candidate;
    if (!MakeCraftedSlots(RecipeId, bYeast, Candidate)) return false;
    Slots = MoveTemp(Candidate);
    OnChanged.Broadcast();
    return true;
}

bool UDWInventoryComponent::ConsumeOneAtSlot(int32 SlotIndex)
{
    if (!Slots.IsValidIndex(SlotIndex) || !AreSlotsValid(Slots)) return false;
    const FDWItemDefinition* Item = GetConfig()->GetItemDefinition(Slots[SlotIndex].ItemId);
    if (!Item || !Item->bUsable) return false;
    if (--Slots[SlotIndex].Quantity == 0) Slots.RemoveAt(SlotIndex);
    OnChanged.Broadcast();
    return true;
}

bool UDWInventoryComponent::SetSlots(const TArray<FDWItemStack>& InSlots)
{
    if (!AreSlotsValid(InSlots)) return false;
    Slots = InSlots;
    OnChanged.Broadcast();
    return true;
}
