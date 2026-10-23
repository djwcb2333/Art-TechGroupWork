#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.h"
#include "DWGatheringInventoryTransaction.h"
#include "GameFramework/Actor.h"

void FDWPreparedInventoryChange::Reset()
{
    Owner.Reset(); CandidateSlots.Reset(); ExpectedRevision=INDEX_NONE;
    RequiredUsableItem=NAME_None; bConsumed=true;
    RequiredCraftRecipe=RequiredBuildingOutput=NAME_None;
}

FDWPreparedInventoryChange::FDWPreparedInventoryChange(FDWPreparedInventoryChange&& Other) noexcept
{
    *this=MoveTemp(Other);
}

FDWPreparedInventoryChange& FDWPreparedInventoryChange::operator=(FDWPreparedInventoryChange&& Other) noexcept
{
    if(this!=&Other)
    {
        Owner=Other.Owner; CandidateSlots=MoveTemp(Other.CandidateSlots);
        ExpectedRevision=Other.ExpectedRevision; RequiredUsableItem=Other.RequiredUsableItem; bConsumed=Other.bConsumed;
        RequiredCraftRecipe=Other.RequiredCraftRecipe;RequiredBuildingOutput=Other.RequiredBuildingOutput;
        Other.Reset(); // A moved-from token must never become an empty-inventory commit.
    }
    return *this;
}

UDWInventoryComponent::UDWInventoryComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

const UDWGameplayConfig* UDWInventoryComponent::GetConfig() const
{
    return Config ? Config.Get() : GetDefault<UDWGameplayConfig>();
}

void UDWInventoryComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
    // Actor hierarchy teardown may call this before MarkAsGarbage, without DestroyComponent.
    bInventoryLifecycleEnded=true;
    CraftingContextValidator=nullptr;
    Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UDWInventoryComponent::CanWriteInventory() const
{
    if(!IsInGameThread()||!IsValid(this)||IsBeingDestroyed()||bInventoryLifecycleEnded)return false;
    const AActor* OwnerActor=GetOwner();
    // Ownerless transient inventory remains supported. Destruction callbacks are still UObject-valid.
    return !OwnerActor||(IsValid(OwnerActor)&&!OwnerActor->IsActorBeingDestroyed());
}

void UDWInventoryComponent::InitializeInventory(UDWGameplayConfig* InConfig)
{
    if(!CanWriteInventory()||Revision==MAX_int64)return;
    const UDWGameplayConfig* CandidateConfig=InConfig?InConfig:GetDefault<UDWGameplayConfig>();
    if(!AreSlotsValidWithPolicy(Slots,CandidateConfig,StackLimitOverrides))
    {
        UE_LOG(LogTemp,Warning,TEXT("DW inventory rejected configuration that would invalidate its existing contents or stack policy."));
        return;
    }
    Config=InConfig;
    ++Revision;
    OnChanged.Broadcast(); // No owner/member access after synchronous external callbacks.
}

int32 UDWInventoryComponent::CountItem(FName ItemId) const
{
    int64 Total = 0;
    for (const FDWItemStack& Stack : Slots)
        if (Stack.ItemId == ItemId) Total += Stack.Quantity;
    return static_cast<int32>(FMath::Min<int64>(Total, MAX_int32));
}

bool UDWInventoryComponent::AreSlotsValidWithPolicy(const TArray<FDWItemStack>& InSlots,
    const UDWGameplayConfig* Settings,const TMap<FName,int32>& Overrides) const
{
    if(!Settings||Settings->MaxInventorySlots<1||Settings->MaxStackSize<1||InSlots.Num()>Settings->MaxInventorySlots)return false;
    const auto* Profile=Cast<UDWGatheringProfile>(Settings);
    if(Profile)
    {
        for(const auto& Limit:Profile->StackLimits)
            if(!Profile->IsInventoryItem(Limit.Key)||Limit.Value<1)return false;
    }
    for(const auto& Entry:Overrides)
        if(Entry.Key.IsNone()||Entry.Value<1||!Settings->GetItemDefinition(Entry.Key)||(Profile&&!Profile->IsInventoryItem(Entry.Key)))return false;
    for(const FDWItemStack& Stack:InSlots)
    {
        if(Stack.ItemId.IsNone()||!Settings->GetItemDefinition(Stack.ItemId)||Stack.Quantity<1)return false;
        if(Profile&&!Profile->IsInventoryItem(Stack.ItemId))return false;
        const int32* Override=Overrides.Find(Stack.ItemId);
        const int32 Limit=Override?*Override:(Profile?Profile->GetConfiguredStackLimit(Stack.ItemId):Settings->MaxStackSize);
        if(Limit<1)return false;
        if(Stack.Quantity>Limit)return false;
    }
    return true;
}

bool UDWInventoryComponent::AreSlotsValid(const TArray<FDWItemStack>& InSlots) const
{
    return AreSlotsValidWithPolicy(InSlots,GetConfig(),StackLimitOverrides);
}

int32 UDWInventoryComponent::GetEffectiveStackLimit(FName ItemId) const
{
    const UDWGameplayConfig* Settings=GetConfig();
    if(ItemId.IsNone()||!Settings->GetItemDefinition(ItemId))return 0;
    const auto* Profile=Cast<UDWGatheringProfile>(Settings);
    if(Profile&&!Profile->IsInventoryItem(ItemId))return 0;
    const int32* Override=StackLimitOverrides.Find(ItemId);
    return Override?*Override:(Profile?Profile->GetConfiguredStackLimit(ItemId):Settings->MaxStackSize);
}

int32 UDWInventoryComponent::GetAvailableCapacity(FName ItemId) const
{
    if(!CanWriteInventory()||!AreSlotsValid(Slots))return 0;
    const int32 Limit=GetEffectiveStackLimit(ItemId);
    if(Limit<1)return 0;
    int64 Capacity=static_cast<int64>(GetConfig()->MaxInventorySlots-Slots.Num())*Limit;
    for(const auto& Stack:Slots)if(Stack.ItemId==ItemId)Capacity+=Limit-Stack.Quantity;
    return static_cast<int32>(FMath::Clamp<int64>(Capacity,0,MAX_int32));
}

bool UDWInventoryComponent::PrepareAddItemUpTo(FName ItemId,int32 Requested,int32& OutAccepted,FDWPreparedInventoryChange& Prepared) const
{
    OutAccepted=0;Prepared.Reset();
    if(Requested<1)return false;
    const int32 Accepted=FMath::Min(Requested,GetAvailableCapacity(ItemId));
    if(Accepted<1||!PrepareAddItem(ItemId,Accepted,Prepared))return false;
    OutAccepted=Accepted;return true;
}

bool UDWInventoryComponent::TryAddItemUpTo(FName ItemId,int32 Requested,int32& OutAccepted)
{
    OutAccepted=0;int32 Accepted=0;FDWPreparedInventoryChange Prepared;
    if(!PrepareAddItemUpTo(ItemId,Requested,Accepted,Prepared)||!ValidatePreparedForCommit(Prepared))return false;
    // Complete the output before broadcasting; callbacks may destroy this inventory.
    AdoptPreparedUnchecked(MoveTemp(Prepared));OutAccepted=Accepted;OnChanged.Broadcast();return true;
}

void UDWInventoryComponent::SetCraftingContextValidator(TFunction<bool(FName,FName,FName)> InValidator)
{
    if(!CanWriteInventory()||Revision==MAX_int64)return;
    CraftingContextValidator=MoveTemp(InValidator);++Revision;
}

bool UDWInventoryComponent::TrySetStackLimitOverrides(const TMap<FName,int32>& InOverrides)
{
    if(!CanWriteInventory()||Revision==MAX_int64||!AreSlotsValidWithPolicy(Slots,GetConfig(),InOverrides))return false;
    TMap<FName,int32> Candidate=InOverrides;
    StackLimitOverrides=MoveTemp(Candidate);
    ++Revision;
    OnChanged.Broadcast();
    return true;
}

bool UDWInventoryComponent::MakePrepared(TArray<FDWItemStack>&& Candidate,FDWPreparedInventoryChange& OutPrepared) const
{
    OutPrepared.Reset();
    if(!CanWriteInventory()||Revision==MAX_int64||!AreSlotsValid(Candidate))return false;
    OutPrepared.Owner=this; OutPrepared.ExpectedRevision=Revision;
    OutPrepared.CandidateSlots=MoveTemp(Candidate); OutPrepared.bConsumed=false;
    return true;
}

bool UDWInventoryComponent::PrepareAddItem(FName ItemId,int32 Quantity,FDWPreparedInventoryChange& OutPrepared) const
{
    OutPrepared.Reset();
    if(!CanWriteInventory())return false;
    TArray<FDWItemStack> Candidate=Slots;
    return AddToSlots(Candidate,ItemId,Quantity)&&MakePrepared(MoveTemp(Candidate),OutPrepared);
}

bool UDWInventoryComponent::PrepareRemoveItem(FName ItemId,int32 Quantity,FDWPreparedInventoryChange& OutPrepared) const
{
    OutPrepared.Reset();
    if(!CanWriteInventory())return false;
    TArray<FDWItemStack> Candidate=Slots;
    return RemoveFromSlots(Candidate,ItemId,Quantity)&&MakePrepared(MoveTemp(Candidate),OutPrepared);
}

bool UDWInventoryComponent::PrepareRemoveSnapshot(const FDWInventorySlotSnapshot& Snapshot,int32 Quantity,
    FDWPreparedInventoryChange& OutPrepared) const
{
    OutPrepared.Reset();
    if(!CanWriteInventory()||!IsSlotSnapshotCurrent(Snapshot)||Quantity<1||Quantity>Snapshot.Quantity)return false;
    TArray<FDWItemStack> Candidate=Slots;
    Candidate[Snapshot.SlotIndex].Quantity-=Quantity;
    if(Candidate[Snapshot.SlotIndex].Quantity==0)Candidate.RemoveAt(Snapshot.SlotIndex);
    return MakePrepared(MoveTemp(Candidate),OutPrepared);
}

bool UDWInventoryComponent::PrepareConsumeSnapshot(const FDWInventorySlotSnapshot& Snapshot,
    FDWPreparedInventoryChange& OutPrepared) const
{
    OutPrepared.Reset();
    if(!CanWriteInventory()||!IsSlotSnapshotCurrent(Snapshot))return false;
    const FDWItemDefinition* Item=GetConfig()->GetItemDefinition(Snapshot.ItemId);
    if(!Item||!Item->bUsable||!PrepareRemoveSnapshot(Snapshot,1,OutPrepared))return false;
    OutPrepared.RequiredUsableItem=Snapshot.ItemId;
    return true;
}

bool UDWInventoryComponent::ValidatePreparedForCommit(const FDWPreparedInventoryChange& Prepared) const
{
    if(!CanWriteInventory()||Prepared.bConsumed||Prepared.Owner.Get()!=this||
        Prepared.ExpectedRevision!=Revision||Revision==MAX_int64||!AreSlotsValid(Prepared.CandidateSlots))return false;
    if(!Prepared.RequiredUsableItem.IsNone())
    {
        const FDWItemDefinition* Item=GetConfig()->GetItemDefinition(Prepared.RequiredUsableItem);
        if(!Item||!Item->bUsable)return false;
    }
    if(!Prepared.RequiredCraftRecipe.IsNone())
    {
        FName Building;
        if(!CanUseCraftingContext(Prepared.RequiredCraftRecipe,Building)||Building!=Prepared.RequiredBuildingOutput)return false;
    }
    return true;
}
void UDWInventoryComponent::AdoptPreparedUnchecked(FDWPreparedInventoryChange&& Prepared)
{
    Slots=MoveTemp(Prepared.CandidateSlots);++Revision;Prepared.Reset();
}
bool UDWInventoryComponent::CommitPrepared(FDWPreparedInventoryChange&& Prepared)
{
    // Building world facts must use their typed coordinator, never an inventory-only public commit.
    if(!Prepared.RequiredBuildingOutput.IsNone())return false;
    if(!ValidatePreparedForCommit(Prepared))return false;
    AdoptPreparedUnchecked(MoveTemp(Prepared));
    OnChanged.Broadcast();
    return true;
}

bool UDWInventoryComponent::CommitCandidate(TArray<FDWItemStack>&& Candidate)
{
    FDWPreparedInventoryChange Prepared;
    return MakePrepared(MoveTemp(Candidate),Prepared)&&CommitPrepared(MoveTemp(Prepared));
}

bool UDWInventoryComponent::AddToSlots(TArray<FDWItemStack>& InOutSlots, FName ItemId, int32 Quantity) const
{
    const UDWGameplayConfig* Settings = GetConfig();
    if (Quantity <= 0 || ItemId.IsNone() || !Settings->GetItemDefinition(ItemId) || !AreSlotsValid(InOutSlots)) return false;

    const int32 StackLimit=GetEffectiveStackLimit(ItemId);
    if(StackLimit<1)return false;
    int64 Capacity = static_cast<int64>(Settings->MaxInventorySlots - InOutSlots.Num()) * StackLimit;
    for (const FDWItemStack& Stack : InOutSlots)
        if (Stack.ItemId == ItemId) Capacity += StackLimit - Stack.Quantity;
    if (Capacity < Quantity) return false;

    int32 Remaining = Quantity;
    for (FDWItemStack& Stack : InOutSlots)
    {
        if (Stack.ItemId != ItemId) continue;
        const int32 Added = FMath::Min(Remaining, StackLimit - Stack.Quantity);
        Stack.Quantity += Added;
        Remaining -= Added;
        if (Remaining == 0) return true;
    }
    while (Remaining > 0)
    {
        const int32 Added = FMath::Min(Remaining, StackLimit);
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

bool UDWInventoryComponent::TryAddItem(FName ItemId,int32 Quantity)
{
    FDWPreparedInventoryChange Prepared;
    return PrepareAddItem(ItemId,Quantity,Prepared)&&CommitPrepared(MoveTemp(Prepared));
}

bool UDWInventoryComponent::TryRemoveItem(FName ItemId,int32 Quantity)
{
    FDWPreparedInventoryChange Prepared;
    return PrepareRemoveItem(ItemId,Quantity,Prepared)&&CommitPrepared(MoveTemp(Prepared));
}

bool UDWInventoryComponent::MakeCraftedSlots(FName RecipeId, bool bYeast, TArray<FDWItemStack>& OutSlots) const
{
    const FDWRecipeDefinition* Recipe = GetConfig()->GetRecipeDefinition(RecipeId);
    if (!Recipe || (GetConfig()->bRequireTransformationForCrafting && Recipe->bRequiresYeast && !bYeast) || Recipe->Inputs.IsEmpty() || Recipe->Outputs.IsEmpty()) return false;
    FName Building;
    if(!CanUseCraftingContext(RecipeId,Building))return false;
    OutSlots = Slots;
    // Inputs are removed first, so their emptied slots can hold outputs. The source never changes on failure.
    for (const FDWItemStack& Input : Recipe->Inputs)
        if (!RemoveFromSlots(OutSlots, Input.ItemId, Input.Quantity)) return false;
    if(Building.IsNone())
    {
        for (const FDWItemStack& Output : Recipe->Outputs)
            if (!AddToSlots(OutSlots, Output.ItemId, Output.Quantity)) return false;
    }
    else if(Recipe->Outputs.Num()!=1||Recipe->Outputs[0].ItemId!=Building||Recipe->Outputs[0].Quantity!=1)return false;
    return true;
}

bool UDWInventoryComponent::CanUseCraftingContext(FName RecipeId,FName& OutBuilding) const
{
    OutBuilding=NAME_None;
    const auto* Profile=Cast<UDWGatheringProfile>(GetConfig());
    if(!Profile)return true;
    const FName Station=Profile->GetRequiredStation(RecipeId);OutBuilding=Profile->GetBuildingOutput(RecipeId);
    if(!OutBuilding.IsNone()&&!Profile->NonInventoryItems.Contains(OutBuilding))return false;
    if(Station.IsNone()&&OutBuilding.IsNone())return true;
    return CraftingContextValidator&&CraftingContextValidator(RecipeId,Station,OutBuilding);
}

bool UDWInventoryComponent::PrepareCraftRecipe(FName RecipeId,bool bYeast,FDWPreparedInventoryChange& Prepared) const
{
    Prepared.Reset();TArray<FDWItemStack> Candidate;FName Building;
    if(!CanUseCraftingContext(RecipeId,Building)||!MakeCraftedSlots(RecipeId,bYeast,Candidate)||!MakePrepared(MoveTemp(Candidate),Prepared))return false;
    Prepared.RequiredCraftRecipe=RecipeId;Prepared.RequiredBuildingOutput=Building;return true;
}

bool UDWInventoryComponent::CanCraft(FName RecipeId, bool bYeast) const
{
    TArray<FDWItemStack> Candidate;
    return MakeCraftedSlots(RecipeId, bYeast, Candidate);
}

bool UDWInventoryComponent::TryCraft(FName RecipeId, bool bYeast)
{
    const auto* Profile=Cast<UDWGatheringProfile>(GetConfig());
    if(Profile&&!Profile->GetBuildingOutput(RecipeId).IsNone())
        return FDWGatheringInventoryTransaction::TryCraftBuilding(*this,RecipeId,bYeast);
    FDWPreparedInventoryChange Prepared;
    return PrepareCraftRecipe(RecipeId,bYeast,Prepared)&&CommitPrepared(MoveTemp(Prepared));
}

bool UDWInventoryComponent::ConsumeOneAtSlot(int32 SlotIndex)
{
    FDWInventorySlotSnapshot Snapshot;
    FDWPreparedInventoryChange Prepared;
    return CaptureSlotSnapshot(SlotIndex,Snapshot)&&PrepareConsumeSnapshot(Snapshot,Prepared)&&CommitPrepared(MoveTemp(Prepared));
}

bool UDWInventoryComponent::SetSlots(const TArray<FDWItemStack>& InSlots)
{
    TArray<FDWItemStack> Candidate=InSlots;
    return CommitCandidate(MoveTemp(Candidate));
}

bool UDWInventoryComponent::CaptureSlotSnapshot(int32 SlotIndex, FDWInventorySlotSnapshot& OutSnapshot) const
{
    OutSnapshot=FDWInventorySlotSnapshot();
    if(!Slots.IsValidIndex(SlotIndex)||!AreSlotsValid(Slots))return false;
    OutSnapshot.SlotIndex=SlotIndex;
    OutSnapshot.ItemId=Slots[SlotIndex].ItemId;
    OutSnapshot.Quantity=Slots[SlotIndex].Quantity;
    OutSnapshot.Revision=Revision;
    return true;
}

bool UDWInventoryComponent::IsSlotSnapshotCurrent(const FDWInventorySlotSnapshot& Snapshot) const
{
    return Snapshot.Revision==Revision&&Slots.IsValidIndex(Snapshot.SlotIndex)&&
        Snapshot.ItemId==Slots[Snapshot.SlotIndex].ItemId&&Snapshot.Quantity==Slots[Snapshot.SlotIndex].Quantity&&
        AreSlotsValid(Slots);
}

bool UDWInventoryComponent::TryDiscardSnapshot(const FDWInventorySlotSnapshot& Snapshot)
{
    if(Cast<UDWGatheringProfile>(GetConfig()))return FDWGatheringInventoryTransaction::TryDiscardToWorld(*this,Snapshot);
    if(!IsSlotSnapshotCurrent(Snapshot))return false;
    TArray<FDWItemStack> Candidate=Slots;
    Candidate.RemoveAt(Snapshot.SlotIndex);
    if(!AreSlotsValid(Candidate))return false;
    return CommitCandidate(MoveTemp(Candidate));
}

bool UDWInventoryComponent::TrySplitSlot(int32 SlotIndex, int32 Quantity, int32& OutNewSlot)
{
    OutNewSlot=INDEX_NONE;
    if(!AreSlotsValid(Slots)||!Slots.IsValidIndex(SlotIndex)||Quantity<1||Quantity>=Slots[SlotIndex].Quantity||
        Slots.Num()>=GetConfig()->MaxInventorySlots)return false;
    TArray<FDWItemStack> Candidate=Slots;
    const FName ItemId=Candidate[SlotIndex].ItemId;
    Candidate[SlotIndex].Quantity-=Quantity;
    Candidate.Emplace(ItemId,Quantity);
    if(!AreSlotsValid(Candidate))return false;

    OutNewSlot=Candidate.Num()-1;
    if(CommitCandidate(MoveTemp(Candidate)))return true;
    OutNewSlot=INDEX_NONE;
    return false;
}

bool UDWInventoryComponent::TryQuickStack(FName ItemId)
{
    if(!AreSlotsValid(Slots))return false;
    TArray<FDWItemStack> Candidate=Slots;
    bool bChanged=false;
    for(int32 I=0;I<Candidate.Num();++I)
    {
        if(!ItemId.IsNone()&&Candidate[I].ItemId!=ItemId)continue;
        const int32 StackLimit=GetEffectiveStackLimit(Candidate[I].ItemId);
        for(int32 J=I+1;J<Candidate.Num()&&Candidate[I].Quantity<StackLimit;++J)
        {
            if(Candidate[J].ItemId!=Candidate[I].ItemId||Candidate[J].Quantity<1)continue;
            const int32 Move=FMath::Min(Candidate[J].Quantity,StackLimit-Candidate[I].Quantity);
            Candidate[I].Quantity+=Move;Candidate[J].Quantity-=Move;bChanged|=Move>0;
        }
    }
    if(!bChanged)return false;
    Candidate.RemoveAll([](const FDWItemStack& Stack){return Stack.Quantity==0;});
    if(!AreSlotsValid(Candidate))return false;
    return CommitCandidate(MoveTemp(Candidate));
}

bool UDWInventoryComponent::TryMoveOrMergeSnapshot(const FDWInventorySlotSnapshot& Snapshot,int32 TargetSlot)
{
    if(!IsSlotSnapshotCurrent(Snapshot)||TargetSlot<0||TargetSlot>=GetConfig()->MaxInventorySlots||TargetSlot==Snapshot.SlotIndex)return false;
    TArray<FDWItemStack> Candidate=Slots;
    if(!Candidate.IsValidIndex(TargetSlot))
    {
        if(Snapshot.SlotIndex==Candidate.Num()-1)return false;
        const FDWItemStack Moved=Candidate[Snapshot.SlotIndex];
        Candidate.RemoveAt(Snapshot.SlotIndex);Candidate.Add(Moved);
    }
    else if(Candidate[TargetSlot].ItemId!=Snapshot.ItemId)Candidate.Swap(Snapshot.SlotIndex,TargetSlot);
    else
    {
        const int32 Move=FMath::Min(Candidate[Snapshot.SlotIndex].Quantity,GetEffectiveStackLimit(Snapshot.ItemId)-Candidate[TargetSlot].Quantity);
        if(Move<1)return false;
        Candidate[TargetSlot].Quantity+=Move;Candidate[Snapshot.SlotIndex].Quantity-=Move;
        if(Candidate[Snapshot.SlotIndex].Quantity==0)Candidate.RemoveAt(Snapshot.SlotIndex);
    }
    if(!AreSlotsValid(Candidate))return false;
    return CommitCandidate(MoveTemp(Candidate));
}
