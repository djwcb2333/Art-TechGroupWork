#include "DWInventoryComponent.h"
#include "DWCraftingTypes.h"
#include "DWGameplayConfig.h"
#include "DWBuildingDefinition.h"
#include "DWPlayerCharacter.h"

bool UDWInventoryComponent::PrepareCraftRecipeBatch(FName Id,bool Yeast,int32 Count,FDWPreparedInventoryChange& Out) const
{
    Out.Reset();
    const auto* C=GetConfig();const auto* R=C?C->GetRecipeDefinition(Id):nullptr;
    if(!R||Count<1||Count>DWCrafting::HardBatchLimit)return false;
    if(Count==1)return PrepareCraftRecipe(Id,Yeast,Out); // Exact existing single/cross-state path.
    if(!CanWriteInventory()||!AreSlotsValid(Slots)||(C->bRequireTransformationForCrafting&&R->bRequiresYeast&&!Yeast))return false;
    FName Building,Permanent;
    if(!CanUseCraftingContext(Id,Building)||!GetCraftPermanentWeaponOutput(Id,Permanent)||!Permanent.IsNone()||!Building.IsNone())return false;
    // Both registered bag buildings and permanent equipment keep their single-craft coordinator.
    for(const auto& O:R->Outputs)if(C->GetBuildingDefinition(O.ItemId))return false;
    TArray<FDWItemStack> Inputs,Outputs;
    if(!DWCrafting::Scale(R->Inputs,Count,true,Inputs)||!DWCrafting::Scale(R->Outputs,Count,false,Outputs))return false;
    const int64 BeforeRevision=Revision;
    TArray<FDWItemStack> Candidate=Slots;
    for(const auto& I:Inputs)if(!DWSugarMaterial::Remove(Candidate,I.ItemId,I.Quantity))return false;
    for(const auto& O:Outputs)if(!AddToSlots(Candidate,O.ItemId,O.Quantity))return false;
    if(Revision!=BeforeRevision||!MakePrepared(MoveTemp(Candidate),Out))return false;
    Out.RequiredCraftRecipe=Id;Out.RequiredBuildingOutput=Building;Out.RequiredPermanentWeaponOutput=Permanent;
    Out.RequiredCraftCount=Count;Out.RequiredCraftYeast=Yeast;return true;
}
bool UDWInventoryComponent::CanCraftRecipeBatch(FName Id,bool Yeast,int32 Count) const
{FDWPreparedInventoryChange Plan;return PrepareCraftRecipeBatch(Id,Yeast,Count,Plan);}
bool UDWInventoryComponent::TryCraftRecipeBatch(FName Id,bool Yeast,int32 Count)
{
    if(Count==1)return TryCraft(Id,Yeast);
    FDWPreparedInventoryChange Plan;return PrepareCraftRecipeBatch(Id,Yeast,Count,Plan)&&CommitPrepared(MoveTemp(Plan));
}
bool UDWInventoryComponent::ValidateBatchCandidate(const FDWPreparedInventoryChange& Plan) const
{
    if(Plan.RequiredCraftCount<1||Plan.RequiredCraftCount>DWCrafting::HardBatchLimit)return false;
    if(Plan.RequiredCraftCount==1)return true;
    if(const auto* Player=Cast<ADWPlayerCharacter>(GetOwner());Player&&
       (Player->IsDead()||Player->IsActorBeingDestroyed()||Player->IsCombatRestoreInProgress()||Player->IsYeastForm()!=Plan.RequiredCraftYeast))return false;
    // Configuration/station may have changed without an inventory revision. Reproject the full batch.
    FDWPreparedInventoryChange Current;
    if(!PrepareCraftRecipeBatch(Plan.RequiredCraftRecipe,Plan.RequiredCraftYeast,Plan.RequiredCraftCount,Current)||
       Current.ExpectedRevision!=Plan.ExpectedRevision||Current.CandidateSlots.Num()!=Plan.CandidateSlots.Num())return false;
    for(int32 I=0;I<Current.CandidateSlots.Num();++I)
        if(Current.CandidateSlots[I].ItemId!=Plan.CandidateSlots[I].ItemId||Current.CandidateSlots[I].Quantity!=Plan.CandidateSlots[I].Quantity)return false;
    return true;
}
