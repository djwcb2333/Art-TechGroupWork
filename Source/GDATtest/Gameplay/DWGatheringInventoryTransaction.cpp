#include "DWGatheringInventoryTransaction.h"
#include "DWInventoryComponent.h"
#include "DWGatheringComponent.h"
#include "DWGatheringWorld.h"
#include "DWGatheringProfile.h"
#include "DWPlayerCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "DWInteractionPromptComponent.h"
#include "Engine/World.h"

bool FDWGatheringInventoryTransaction::TryCraftBuilding(UDWInventoryComponent& Inventory,FName Recipe,bool Yeast)
{
    auto* Context=UDWGatheringComponent::Find(Inventory);auto* User=Cast<ADWPlayerCharacter>(Inventory.GetOwner());
    if(!Context||!User||User->IsDead()||!User->GetWorld()||!Cast<UDWGatheringProfile>(User->GetGameplayConfig()))return false;
    FDWPreparedInventoryChange Plan;
    if(!Inventory.PrepareCraftRecipe(Recipe,Yeast,Plan)||Plan.GetBuildingOutput().IsNone())return false;
    const FName Building=Plan.GetBuildingOutput();FTransform Location;
    if(!Context->FindBuildingPlacement(Building,Location))return false;
    auto* World=User->GetWorld();
    TWeakObjectPtr<UDWInventoryComponent> SafeInventory(&Inventory);
    TWeakObjectPtr<ADWPlayerCharacter> SafeUser(User);
    auto* Station=World->SpawnActorDeferred<ADWGatheringStation>(ADWGatheringStation::StaticClass(),Location,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Station)return false;
    Station->SetStaged(true);Station->bStationEnabled=false;Station->bRuntimeSpawned=true;Station->PersistentId=FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if(!Station->ConfigureBuilding(Building)){Station->Destroy();return false;}
    Station->PublishInteractionState();
    Station->FinishSpawning(Location);
    // Candidate is complete but inactive; revalidate the source revision and location without counting itself.
    FTransform ConfirmedLocation;
    if(!IsValid(Station)||Station->IsActorBeingDestroyed()||!SafeUser.IsValid()||User->IsActorBeingDestroyed()||!SafeInventory.IsValid()||!Inventory.CanWriteInventory()||World->bIsTearingDown||!Inventory.ValidatePreparedForCommit(Plan)||!Context->FindBuildingPlacement(Building,ConfirmedLocation)||!ConfirmedLocation.Equals(Location,.01f))
    {if(IsValid(Station))Station->Destroy();return false;}
    Inventory.AdoptPreparedUnchecked(MoveTemp(Plan));
    Station->bStationEnabled=true;Station->SetStaged(false);Station->PublishInteractionState();
    if(SafeInventory.IsValid()&&SafeInventory->CanWriteInventory()&&SafeUser.IsValid()&&SafeUser->GetWorld()&&!SafeUser->GetWorld()->bIsTearingDown)SafeInventory->OnChanged.Broadcast();return true;
}
bool FDWGatheringInventoryTransaction::TryDiscardToWorld(UDWInventoryComponent& Inventory,const FDWInventorySlotSnapshot& Snapshot)
{
    auto* User=Cast<ADWPlayerCharacter>(Inventory.GetOwner());
    if(!User||User->IsDead()||!User->GetWorld()||!UDWGatheringComponent::Find(Inventory)||!Cast<UDWGatheringProfile>(User->GetGameplayConfig()))return false;
    FDWPreparedInventoryChange Plan;
    if(!Inventory.PrepareRemoveSnapshot(Snapshot,Snapshot.Quantity,Plan))return false;
    auto* World=User->GetWorld();FVector Position=User->GetActorLocation()+User->GetActorForwardVector()*110.;
    TWeakObjectPtr<UDWInventoryComponent> SafeInventory(&Inventory);
    TWeakObjectPtr<ADWPlayerCharacter> SafeUser(User);
    FHitResult Ground;FCollisionQueryParams Query(SCENE_QUERY_STAT(GatherDiscard),false,User);
    if(!World->LineTraceSingleByChannel(Ground,Position+FVector(0,0,100),Position-FVector(0,0,500),ECC_Visibility,Query)||Ground.ImpactNormal.Z<.5)return false;
    Position=Ground.ImpactPoint+FVector(0,0,20.);
    auto* Drop=World->SpawnActorDeferred<ADWGatheringPickup>(ADWGatheringPickup::StaticClass(),FTransform(Position),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Drop)return false;
    Drop->SetStaged(true);Drop->ItemId=Snapshot.ItemId;Drop->Quantity=0;Drop->bRuntimeSpawned=true;Drop->PersistentId=FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
    Drop->SetActorHiddenInGame(true);Drop->SetActorEnableCollision(false);Drop->Prompt->bPromptEnabled=false;
    Drop->FinishSpawning(FTransform(Position));
    if(!IsValid(Drop)||Drop->IsActorBeingDestroyed()||!SafeUser.IsValid()||User->IsActorBeingDestroyed()||!SafeInventory.IsValid()||!Inventory.CanWriteInventory()||World->bIsTearingDown||!Inventory.ValidatePreparedForCommit(Plan))
    {if(IsValid(Drop))Drop->Destroy();return false;}
    Inventory.AdoptPreparedUnchecked(MoveTemp(Plan));Drop->Quantity=Snapshot.Quantity;
    Drop->SetStaged(false);Drop->RefreshPickup();
    if(SafeInventory.IsValid()&&SafeInventory->CanWriteInventory()&&SafeUser.IsValid()&&SafeUser->GetWorld()&&!SafeUser->GetWorld()->bIsTearingDown)SafeInventory->OnChanged.Broadcast();return true;
}
