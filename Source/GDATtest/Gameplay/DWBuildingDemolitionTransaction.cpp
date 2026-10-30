#include "DWBuildingDemolitionTransaction.h"
#include "DWBuildingDemolitionCost.h"
#include "DWGatheringWorld.h"
#include "DWGatheringProfile.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWInteractionPromptComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

bool FDWBuildingDemolitionTransaction::Commit(ADWPlayerCharacter& Player,ADWGatheringStation& Building,
    EDWBuildingDemolitionReturn Mode,TFunctionRef<bool()> SessionCurrent,TFunctionRef<bool()> ConsumeCompletion,FString& Error)
{
    Error.Reset();UWorld* World=Player.GetWorld();auto* Bag=Player.GetInventory();
    const auto* Profile=UDWGameplayConfig::AsGatheringConfig(Player.GetGameplayConfig());
    if(!World||World->bIsTearingDown||!Bag||!Profile||Building.bDemolished||
       (!Building.bRuntimeSpawned&&!Building.bAllowAuthoredDemolition)||Building.IsStaged()||
       Building.IsActorBeingDestroyed()||Building.GetWorld()!=World||Building.PersistentId.IsNone()||!SessionCurrent())
    {Error=TEXT("The demolition session is no longer current.");return false;}
    const FName ItemId=Building.BuildingItemId,Identity=Building.PersistentId;
    const FTransform Transform=Building.GetActorTransform();
    const uint8 CostOrigin=Building.DemolitionCostOrigin;const FName CostRecipe=Building.DemolitionCostRecipe;
    const TArray<FDWItemStack> OriginalCost=Building.DemolitionCostInputs,AuthoredCost=Building.AuthoredDemolitionReturn;
    const bool bAuthoredAllowed=Building.bAllowAuthoredDemolition,bAllowEmpty=Building.bAllowAuthoredEmptyReturn;
    const bool bWasRuntime=Building.bRuntimeSpawned;
    const auto SameStacks=[](const TArray<FDWItemStack>& A,const TArray<FDWItemStack>& B)
    {if(A.Num()!=B.Num())return false;for(int32 I=0;I<A.Num();++I)if(A[I].ItemId!=B[I].ItemId||A[I].Quantity!=B[I].Quantity)return false;return true;};
    if(!Transform.IsValid()||!Profile->IsInventoryItem(ItemId)||!Profile->GetBuildingDefinition(ItemId))
    {Error=TEXT("This building does not have a registered return item.");return false;}
    TArray<FDWItemStack> Return;
    if(Mode==EDWBuildingDemolitionReturn::BuildingToWorld)Return.Add(FDWItemStack(ItemId,1));
    else if(!Building.bRuntimeSpawned)
    {
        // A map-authored object has no proven player payment. Designers may explicitly configure materials.
        FDWBuildingDemolitionCost Cost;Cost.RecipeId=TEXT("AuthoredDemolition");Cost.Inputs=Building.AuthoredDemolitionReturn;
        if(Cost.Inputs.IsEmpty()&&!Building.bAllowAuthoredEmptyReturn)
        {
            if(!FDWBuildingDemolitionCost::ResolveUnique(*Profile,ItemId,Cost,Error))
            {if(Error.IsEmpty())Error=TEXT("Set authored demolition materials, a unique recipe, or explicit empty return.");return false;}
        }
        if(!Cost.Inputs.IsEmpty()&&!FDWBuildingDemolitionCost::Validate(*Profile,ItemId,Cost,Error))return false;
        Return=MoveTemp(Cost.Inputs);
    }
    else
    {
        FDWBuildingDemolitionCost Cost;Cost.RecipeId=Building.DemolitionCostRecipe;Cost.Inputs=Building.DemolitionCostInputs;
        // Unknown provenance is deliberately rejected, including legacy runtime/debug objects.
        if((Building.DemolitionCostOrigin!=1&&Building.DemolitionCostOrigin!=3&&Building.DemolitionCostOrigin!=4)||
           !FDWBuildingDemolitionCost::Validate(*Profile,ItemId,Cost,Error))
        {if(Error.IsEmpty())Error=TEXT("Building material provenance is unknown; cannot safely return materials.");return false;}
        Return=MoveTemp(Cost.Inputs);
    }
    const TWeakObjectPtr<ADWPlayerCharacter> SafePlayer(&Player);
    const TWeakObjectPtr<ADWGatheringStation> SafeBuilding(&Building);
    const TWeakObjectPtr<UDWInventoryComponent> SafeBag(Bag);
    const TWeakObjectPtr<UWorld> SafeWorld(World);
    FDWPreparedInventoryChange InventoryPlan;
    TArray<TWeakObjectPtr<ADWGatheringPickup>> Staged;
    TArray<FName> StagedIds;TArray<FTransform> StagedTransforms;
    if(Mode==EDWBuildingDemolitionReturn::MaterialsToInventory)
    {
        TArray<FDWItemStack> Candidate=Bag->Slots;
        for(const auto& Stack:Return)if(!Bag->AddToSlots(Candidate,Stack.ItemId,Stack.Quantity))
        {Error=TEXT("Not enough inventory space for all returned materials.");return false;}
        if(!Bag->MakePrepared(MoveTemp(Candidate),InventoryPlan))
        {Error=TEXT("Could not prepare a complete materials return.");return false;}
    }
    else for(int32 Index=0;Index<Return.Num();++Index)
    {
        FVector Position=Transform.GetLocation();
        // Spread multiple material stacks nearby, while remaining at the demolished building.
        const double Angle=double(Index)*2.3999632297;
        Position+=FVector(FMath::Cos(Angle)*35.,FMath::Sin(Angle)*35.,20.);
        FHitResult Ground;FCollisionQueryParams Query(SCENE_QUERY_STAT(DemolitionDrop),false,&Building);
        if(!World->LineTraceSingleByChannel(Ground,Position+FVector(0,0,150.),Position-FVector(0,0,450.),ECC_Visibility,Query)||
           Ground.ImpactPoint.ContainsNaN()||Ground.ImpactNormal.ContainsNaN()||Ground.ImpactNormal.Z<.5||Cast<APawn>(Ground.GetActor())||Cast<ADWGatheringInteractable>(Ground.GetActor()))
        {Error=TEXT("No safe ground for returned materials; building was not removed.");break;}
        Position.Z=Ground.ImpactPoint.Z+20.;
        const FTransform DropTransform(Position);
        auto* Drop=World->SpawnActorDeferred<ADWGatheringPickup>(ADWGatheringPickup::StaticClass(),DropTransform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if(!Drop){Error=TEXT("Could not stage all returned materials.");break;}
        Staged.Add(Drop);Drop->SetStaged(true);Drop->bRuntimeSpawned=true;
        Drop->PersistentId=FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
        StagedIds.Add(Drop->PersistentId);StagedTransforms.Add(DropTransform);
        Drop->ItemId=Return[Index].ItemId;Drop->Quantity=0;Drop->bPopOnSpawn=false;
        Drop->SetActorHiddenInGame(true);Drop->SetActorEnableCollision(false);Drop->Prompt->bPromptEnabled=false;
        Drop->FinishSpawning(DropTransform);
        if(!SafePlayer.IsValid()||!SafeBuilding.IsValid()||!SafeBag.IsValid()||!SafeWorld.IsValid()||
           World->bIsTearingDown||!IsValid(Drop)||Drop->IsActorBeingDestroyed()||!SessionCurrent())
        {Error=TEXT("Demolition was interrupted during return preparation.");break;}
    }
    bool bReady=Error.IsEmpty()&&SafePlayer.IsValid()&&SafeBuilding.IsValid()&&SafeBag.IsValid()&&SafeWorld.IsValid()&&
        !World->bIsTearingDown&&!Building.IsActorBeingDestroyed()&&!Building.IsStaged()&&
        Building.PersistentId==Identity&&Building.BuildingItemId==ItemId&&Building.GetActorTransform().Equals(Transform,.01)&&
        !Building.bDemolished&&Building.bRuntimeSpawned==bWasRuntime&&Building.bAllowAuthoredDemolition==bAuthoredAllowed&&
        Building.bAllowAuthoredEmptyReturn==bAllowEmpty&&Building.DemolitionCostOrigin==CostOrigin&&Building.DemolitionCostRecipe==CostRecipe&&
        SameStacks(Building.DemolitionCostInputs,OriginalCost)&&SameStacks(Building.AuthoredDemolitionReturn,AuthoredCost)&&SessionCurrent();
    if(Mode==EDWBuildingDemolitionReturn::MaterialsToInventory)
        bReady=bReady&&Bag->CanWriteInventory()&&Bag->ValidatePreparedForCommit(InventoryPlan);
    else
    {
        bReady=bReady&&Staged.Num()==Return.Num();
        bReady=bReady&&StagedIds.Num()==Return.Num()&&StagedTransforms.Num()==Return.Num();
        for(int32 I=0;I<Staged.Num();++I)
        {
            const auto& Drop=Staged[I];
            if(!Drop.IsValid()||Drop->IsActorBeingDestroyed()||!Drop->IsStaged()||Drop->GetWorld()!=World||
               Drop->GetClass()!=ADWGatheringPickup::StaticClass()||!Drop->bRuntimeSpawned||Drop->Quantity!=0||
               !StagedIds.IsValidIndex(I)||Drop->PersistentId!=StagedIds[I]||!StagedTransforms.IsValidIndex(I)||
               !Drop->GetActorTransform().Equals(StagedTransforms[I],.01)||!Return.IsValidIndex(I)||Drop->ItemId!=Return[I].ItemId)bReady=false;
        }
    }
    if(!bReady||!ConsumeCompletion())
    {
        for(auto& Drop:Staged)if(Drop.IsValid()&&!Drop->IsActorBeingDestroyed())Drop->Destroy();
        if(Error.IsEmpty())Error=TEXT("Demolition or returned materials could not be committed.");return false;
    }
    // Native fact-only commit barrier: no delegates, Blueprint callbacks or engine publication here.
    const bool bRuntime=bWasRuntime;
    if(bRuntime)Building.SetStaged(true);else Building.bDemolished=true;
    if(Mode==EDWBuildingDemolitionReturn::MaterialsToInventory)Bag->AdoptPreparedUnchecked(MoveTemp(InventoryPlan));
    else for(int32 Index=0;Index<Staged.Num();++Index)
    {Staged[Index]->Quantity=Return[Index].Quantity;Staged[Index]->SetStaged(false);}
    // Once facts are committed, callbacks may run; a failed physical Destroy stays staged/hidden.
    if(SafeBuilding.IsValid()&&!Building.IsActorBeingDestroyed())
    {
        Building.SetActorHiddenInGame(true);Building.SetActorEnableCollision(false);
        if(bRuntime)Building.Destroy();else Building.PublishInteractionState();
    }
    for(auto& Drop:Staged)if(Drop.IsValid()&&!Drop->IsActorBeingDestroyed())Drop->RefreshPickup();
    if(Mode==EDWBuildingDemolitionReturn::MaterialsToInventory&&SafeBag.IsValid()&&SafeBag->CanWriteInventory()&&
       SafePlayer.IsValid()&&SafePlayer->GetWorld()&&!SafePlayer->GetWorld()->bIsTearingDown)SafeBag->OnChanged.Broadcast();
    return true;
}
