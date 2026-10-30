#include "DWLootBeaconSubsystem.h"
#include "DWLootVisualCatalog.h"
#include "DWLootVisualRules.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "DWGatheringWorld.h"
#include "DWPickupPresentationComponent.h"
#include "DWCombatCorePickup.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatRecoveryActors.h"
#include "DWCombatRecoverySubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "DWCombatAuthoritySubsystem.h"
#include "Misc/ScopeExit.h"

bool UDWLootBeaconSubsystem::LoadCatalog()
{
    if(IsValid(Catalog))return true;
    const TSoftObjectPtr<UDWLootVisualCatalog> Path(FSoftObjectPath(TEXT("/Game/DoughWorld/Core/Data/DA_DWLootVisualCatalog.DA_DWLootVisualCatalog")));
    const TWeakObjectPtr<UDWLootBeaconSubsystem> Self(this);
    auto* Loaded=Path.LoadSynchronous();
    if(!Self.IsValid()||HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed))return false;
    Catalog=Loaded;
    if(!IsValid(Catalog))return false;
    TSet<FName> Seen;
    for(const auto& Entry:Catalog->Entries)
    {
        if(Entry.ItemId.IsNone()||Seen.Contains(Entry.ItemId))
        {
            UE_LOG(LogTemp,Warning,TEXT("Loot visual catalog has empty or duplicated ItemId: %s"),*Entry.ItemId.ToString());
        }
        else Seen.Add(Entry.ItemId);
    }
    return true;
}
bool UDWLootBeaconSubsystem::IsCurrent(AActor* Owner,FName ItemId,UStaticMeshComponent* Anchor)const
{
    UWorld* World=GetWorld();
    if(!World||World->bIsTearingDown||!IsValid(Owner)||Owner->IsActorBeingDestroyed()||Owner->GetWorld()!=World||
       !IsValid(Anchor)||Anchor->GetOwner()!=Owner||Owner->IsHidden()||!Owner->HasActorBegunPlay())return false;
    if(auto* Pickup=Cast<ADWGatheringPickup>(Owner))return Pickup->Mesh==Anchor&&Pickup->ItemId==ItemId&&Pickup->IsInteractionAvailable();
    if(auto* Core=Cast<ADWCombatCorePickup>(Owner))
    {auto* Drops=UDWCombatCoreDropSubsystem::Get(Owner);return Core->GetCoreItemId()==ItemId&&!Core->IsClaimed()&&Drops&&Drops->FindPhysicalPickup(Core->GetPickupId())==Core;}
    if(auto* Bag=Cast<ADWCombatDeathBag>(Owner))
    {auto* R=UDWCombatRecoverySubsystem::Get(Owner);FDWDeathBagSave Saved;return ItemId.IsNone()&&R&&R->FindPhysicalBag(Bag->GetBagId())==Bag&&R->FindBag(Bag->GetBagId(),Saved)&&!Saved.bClaimed;}
    return false;
}
void UDWLootBeaconSubsystem::SuspendBeacon(AActor* Owner)
{for(auto& R:Records)if(R.Owner.Get()==Owner&&IsValid(R.Beacon))R.Beacon->SetVisibility(false);}
bool UDWLootBeaconSubsystem::PublishBeacon(AActor* Owner,UStaticMeshComponent* Anchor,FName ItemId,bool* bMeshChanged)
{return PublishInternal(Owner,Anchor,ItemId,false,bMeshChanged);}
bool UDWLootBeaconSubsystem::PublishDeathBag(AActor* Owner,UStaticMeshComponent* Anchor)
{return PublishInternal(Owner,Anchor,NAME_None,true,nullptr);}
bool UDWLootBeaconSubsystem::PublishInternal(AActor* Owner,UStaticMeshComponent* Anchor,FName ItemId,bool bDeathBag,bool* bMeshChanged)
{
    if(bMeshChanged)*bMeshChanged=false;
    UWorld* World=GetWorld();
    if(!World||!World->IsGameWorld()||(!bDeathBag&&ItemId.IsNone())||!IsCurrent(Owner,ItemId,Anchor))return false;
    const TWeakObjectPtr<AActor> BuildingOwner(Owner);const TWeakObjectPtr<UDWLootBeaconSubsystem> Self(this);
    if(Building.Contains(BuildingOwner))return false;
    Building.Add(BuildingOwner);ON_SCOPE_EXIT{if(Self.IsValid())Self->Building.Remove(BuildingOwner);};
    for(auto& R:Records)if(R.Owner.Get()==Owner&&R.Anchor.Get()==Anchor&&R.ItemId==ItemId&&R.OriginalMesh.Get()==Anchor->GetStaticMesh())
    {Rebalance();return true;}
    const TWeakObjectPtr<AActor> PreOwner(Owner);const TWeakObjectPtr<UStaticMeshComponent> PreAnchor(Anchor);
    WithdrawBeacon(Owner);
    if(!PreOwner.IsValid()||!PreAnchor.IsValid()||!IsCurrent(PreOwner.Get(),ItemId,PreAnchor.Get()))return false;
    Owner=PreOwner.Get();Anchor=PreAnchor.Get();
    if(Anchor->bHiddenInGame||!Anchor->IsVisible())return false;
    TWeakObjectPtr<AActor> SafeOwner(Owner);TWeakObjectPtr<UStaticMeshComponent> SafeAnchor(Anchor);
    TWeakObjectPtr<UWorld> SafeWorld(World);const auto OriginalMesh=Anchor->GetStaticMesh();
    const auto OriginalOverrides=Anchor->OverrideMaterials;const FTransform OriginalTransform=Anchor->GetRelativeTransform();
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority=UDWCombatAuthoritySubsystem::Get(Owner);
    const uint64 Epoch=Authority.IsValid()?Authority->GetWorldEpoch():0;
    auto StillPublished=[&]()
    {
        if(!Self.IsValid()||HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed)||!SafeWorld.IsValid()||!SafeOwner.IsValid()||!SafeAnchor.IsValid()||GetWorld()!=SafeWorld.Get()||
           !IsCurrent(SafeOwner.Get(),ItemId,SafeAnchor.Get()))return false;
        auto* Now=UDWCombatAuthoritySubsystem::Get(SafeOwner.Get());
        return Now==Authority.Get()&&(!Now||Now->GetWorldEpoch()==Epoch);
    };
    auto StillCurrent=[&]()
    {return StillPublished()&&SafeAnchor->GetStaticMesh()==OriginalMesh&&SafeAnchor->OverrideMaterials==OriginalOverrides&&SafeAnchor->GetRelativeTransform().Equals(OriginalTransform);};
    if(!LoadCatalog()||!StillCurrent())return false;
    const FDWLootVisualEntry* Found=bDeathBag?nullptr:Catalog->FindUnique(ItemId);
    if(!bDeathBag&&!Found)return false;
    FDWLootVisualEntry Row=Found?*Found:FDWLootVisualEntry{};
    if(bDeathBag){Row.Mesh=Catalog->DeathBagMesh;Row.RelativeTransform=Catalog->DeathBagTransform;}
    const auto BeaconShapeRef=Catalog->BeaconMesh;const auto BeaconMaterialRef=Catalog->BeaconMaterial;
    TStrongObjectPtr<UStaticMesh> Shape(Row.Mesh.IsNull()?nullptr:Row.Mesh.LoadSynchronous());
    if(!StillCurrent())return false;
    TStrongObjectPtr<UStaticMesh> BeamShape(BeaconShapeRef.IsNull()?nullptr:BeaconShapeRef.LoadSynchronous());
    if(!StillCurrent())return false;
    TStrongObjectPtr<UMaterialInterface> BeamMaterial(BeaconMaterialRef.IsNull()?nullptr:BeaconMaterialRef.LoadSynchronous());
    if(!StillCurrent())return false;
    Owner=SafeOwner.Get();Anchor=SafeAnchor.Get();
    // Replacing only literal default placeholder geometry or exact per-ID legacy assets.
    // A user tag or custom material prevents fallback, as does any other mesh.
    bool DefaultMesh=OriginalMesh&&(OriginalMesh->GetPathName()==TEXT("/Engine/BasicShapes/Cube.Cube")||OriginalMesh->GetPathName()==TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    for(const auto& Legacy:Row.LegacyPlaceholderMeshes)if(OriginalMesh&&Legacy.ToSoftObjectPath()==FSoftObjectPath(OriginalMesh))DefaultMesh=true;
    // The old native sugar fallback can run during construction before this service.
    // Recognize only the unchanged component default, never a user-authored replacement.
    const auto* Pickup=Cast<ADWGatheringPickup>(Owner);
    const auto* SugarDefaults=GetDefault<UDWPickupPresentationComponent>();
    if(Pickup&&ItemId==TEXT("SugarChunk")&&Pickup->PickupPresentation&&SugarDefaults&&OriginalMesh&&
       Pickup->PickupPresentation->SugarChunkMesh.ToSoftObjectPath()==SugarDefaults->SugarChunkMesh.ToSoftObjectPath()&&
       SugarDefaults->SugarChunkMesh.ToSoftObjectPath()==FSoftObjectPath(OriginalMesh))DefaultMesh=true;
    bool CustomMaterial=false;
    if(OriginalMesh)for(int32 I=0;I<Anchor->GetNumMaterials();++I)if(Anchor->GetMaterial(I)!=OriginalMesh->GetMaterial(I)){CustomMaterial=true;break;}
    const auto Appearance=DefaultMesh?DWLootVisualRules::ExistingAppearance::KnownPlaceholder:DWLootVisualRules::ExistingAppearance::Authored;
    const bool Replace=(!Pickup||Pickup->bUseItemCatalogVisuals)&&
        DWLootVisualRules::CanReplace(Appearance,CustomMaterial||Anchor->ComponentHasTag(TEXT("DWLootKeepAuthored")),Shape.IsValid())&&Row.RelativeTransform.IsValid();
    TMap<int32,TStrongObjectPtr<UMaterialInterface>> Materials;
    if(Replace)for(const auto& Pair:Row.MaterialOverrides)
    {
        if(Pair.Key<0||Pair.Key>=Shape->GetStaticMaterials().Num()||Pair.Value.IsNull())return false;
        auto* M=Pair.Value.LoadSynchronous();
        if(!IsValid(M)||!StillCurrent())return false;
        Materials.Add(Pair.Key,TStrongObjectPtr<UMaterialInterface>(M));
    }
    FDWLootBeaconRecord Record;Record.Owner=Owner;Record.Anchor=Anchor;Record.ItemId=ItemId;
    if(Replace&&bDeathBag)
    {
        // The death bag's mesh is its root: changing its relative transform would
        // change the saved bag location and invalidate the claim. Use a visual child.
        auto* Visual=NewObject<UStaticMeshComponent>(Owner,NAME_None,RF_Transient);if(!Visual)return false;
        Visual->SetupAttachment(Anchor);Visual->SetMobility(EComponentMobility::Movable);
        Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetGenerateOverlapEvents(false);
        Visual->SetCanEverAffectNavigation(false);Visual->SetStaticMesh(Shape.Get());Visual->SetRelativeTransform(Row.RelativeTransform);
        Visual->RegisterComponent();
        if(!StillCurrent()){if(IsValid(Visual))Visual->DestroyComponent();return false;}
        Record.Visual=Visual;Record.bOriginalHidden=Anchor->bHiddenInGame;Record.bBorrowedHidden=true;
        Anchor->SetHiddenInGame(true,false);
        if(!StillCurrent())
        {if(SafeAnchor.IsValid()&&SafeAnchor->bHiddenInGame)SafeAnchor->SetHiddenInGame(Record.bOriginalHidden,false);if(IsValid(Visual))Visual->DestroyComponent();return false;}
    }
    else if(Replace)
    {
        // Saved placeholder materials may be explicit copies of the mesh defaults.
        // Clear only slot overrides; EmptyOverrideMaterials would also clear overlays.
        Anchor->SetStaticMesh(Shape.Get());
        if(!StillPublished()||SafeAnchor->GetStaticMesh()!=Shape.Get())return false;
        const int32 Slots=Anchor->GetNumOverrideMaterials();
        for(int32 I=0;I<Slots;++I)
        {Anchor->SetMaterial(I,nullptr);if(!StillPublished()||SafeAnchor->GetStaticMesh()!=Shape.Get())return false;}
        Anchor->SetRelativeTransform(Row.RelativeTransform);
        if(!StillPublished()||SafeAnchor->GetStaticMesh()!=Shape.Get())return false;
        for(const auto& Pair:Materials)
        {Anchor->SetMaterial(Pair.Key,Pair.Value.Get());if(!StillPublished()||SafeAnchor->GetStaticMesh()!=Shape.Get())return false;}
        if(bMeshChanged)*bMeshChanged=Anchor->GetStaticMesh()!=OriginalMesh||Anchor->OverrideMaterials!=OriginalOverrides||!Anchor->GetRelativeTransform().Equals(OriginalTransform);
    }
    Record.OriginalMesh=Anchor->GetStaticMesh();
    Records.Add(Record);
    if(!(bDeathBag?Catalog->bDeathBagBeacon:Row.bUseBeacon)||!BeamShape.IsValid()||!BeamMaterial.IsValid())return true;
    auto* Beam=NewObject<UStaticMeshComponent>(Owner,NAME_None,RF_Transient);if(!Beam)return false;
    Beam->ComponentTags.Add(TEXT("DWWorldMapExclude"));
    Beam->SetupAttachment(Record.Visual?Record.Visual.Get():Anchor);Beam->SetMobility(EComponentMobility::Movable);
    // Follow the item position, but do not inherit its rotation or scale.
    Beam->SetAbsolute(false,true,true);
    Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);Beam->SetGenerateOverlapEvents(false);
    Beam->SetCanEverAffectNavigation(false);Beam->SetCastShadow(false);Beam->SetComponentTickEnabled(false);
    Beam->SetStaticMesh(BeamShape.Get());Beam->SetMaterial(0,BeamMaterial.Get());Beam->SetRelativeTransform(Catalog->BeaconRelativeTransform);
    Beam->SetVisibility(false);Beam->RegisterComponent();
    if(!StillPublished())
    {if(IsValid(Beam))Beam->DestroyComponent();return false;}
    auto* Added=Records.FindByPredicate([Owner](const FDWLootBeaconRecord& R){return R.Owner.Get()==Owner;});
    if(!Added){Beam->DestroyComponent();return false;}Added->Beacon=Beam;
    const float Period=FMath::IsFinite(Catalog->BeaconRefreshSeconds)?FMath::Clamp(Catalog->BeaconRefreshSeconds,.05f,2.f):.25f;
    if(!World->GetTimerManager().IsTimerActive(BudgetTimer))World->GetTimerManager().SetTimer(BudgetTimer,this,&UDWLootBeaconSubsystem::Rebalance,Period,true);
    Rebalance();return true;
}
void UDWLootBeaconSubsystem::ClearRecord(int32 Index)
{
    if(!Records.IsValidIndex(Index))return;
    const FDWLootBeaconRecord Record=Records[Index];Records.RemoveAtSwap(Index);
    if(IsValid(Record.Beacon))Record.Beacon->DestroyComponent();
    if(auto* Source=Record.Anchor.Get();IsValid(Source)&&Record.bBorrowedHidden&&Source->bHiddenInGame)
        Source->SetHiddenInGame(Record.bOriginalHidden,false);
    if(IsValid(Record.Visual))Record.Visual->DestroyComponent();
}
void UDWLootBeaconSubsystem::WithdrawBeacon(AActor* Owner)
{
    for(int32 I=Records.Num()-1;I>=0;--I)
        if(!Records[I].Owner.IsValid()||Records[I].Owner.Get()==Owner)
            ClearRecord(I);
    if(Records.IsEmpty())if(auto* World=GetWorld())World->GetTimerManager().ClearTimer(BudgetTimer);
}
void UDWLootBeaconSubsystem::Rebalance()
{
    UWorld* World=GetWorld();if(!World||World->bIsTearingDown||!IsValid(Catalog))return;
    APlayerController* PC=World->GetFirstPlayerController();FVector Reference=FVector::ZeroVector;
    bool HasView=IsValid(PC)&&PC->IsLocalController();
    if(HasView)
    {
        const APawn* Pawn=PC->GetPawn();
        if(IsValid(Pawn)&&!Pawn->IsActorBeingDestroyed()&&Pawn->GetWorld()==World)Reference=Pawn->GetActorLocation();
        else{FRotator Rotation;PC->GetPlayerViewPoint(Reference,Rotation);}
        if(Reference.ContainsNaN())HasView=false;
    }
    std::vector<DWLootVisualRules::FViewCandidate> Choices;
    const double Max=FMath::IsFinite(Catalog->BeaconMaxDistanceCm)?FMath::Max(0.f,Catalog->BeaconMaxDistanceCm):0.;
    for(int32 I=Records.Num()-1;I>=0;--I)
    {
        auto& R=Records[I];auto* Owner=R.Owner.Get();auto* Anchor=R.Anchor.Get();
        if(!IsValid(Owner)||Owner->IsActorBeingDestroyed()||!IsValid(Anchor)||Owner->GetWorld()!=World||Anchor->GetStaticMesh()!=R.OriginalMesh.Get())
            ClearRecord(I);
    }
    for(int32 I=0;I<Records.Num();++I)
    {
        auto& R=Records[I];auto* Owner=R.Owner.Get();auto* Anchor=R.Anchor.Get();auto* Beam=R.Beacon.Get();
        if(!IsValid(Beam))continue;
        Beam->SetVisibility(false);
        if(!DWLootVisualRules::CanShow(HasView&&IsCurrent(Owner,R.ItemId,Anchor),Owner->IsHidden()||!Anchor->IsVisible()||(Anchor->bHiddenInGame&&!R.bBorrowedHidden),false,false,Owner->IsActorBeingDestroyed(),true))continue;
        const double D=FVector::DistSquared2D(Reference,Anchor->GetComponentLocation());
        Choices.push_back({D,static_cast<std::size_t>(I)});
    }
    const auto Selected=DWLootVisualRules::Nearest(Choices,Max,static_cast<std::size_t>(FMath::Clamp(Catalog->MaxVisibleBeacons,0,256)));
    for(const auto Index:Selected)if(Records.IsValidIndex(static_cast<int32>(Index))&&IsValid(Records[static_cast<int32>(Index)].Beacon))Records[static_cast<int32>(Index)].Beacon->SetVisibility(true);
    if(Records.IsEmpty())World->GetTimerManager().ClearTimer(BudgetTimer);
}
void UDWLootBeaconSubsystem::Deinitialize()
{
    if(auto* World=GetWorld())World->GetTimerManager().ClearTimer(BudgetTimer);
    while(!Records.IsEmpty())ClearRecord(Records.Num()-1);
    Catalog=nullptr;Super::Deinitialize();
}
