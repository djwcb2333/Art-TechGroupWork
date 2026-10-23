#include "DWGatheringWorld.h"
#include "DWGatheringComponent.h"
#include "DWGatheringProfile.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "DWInteractionPromptComponent.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameplayHUD.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADWGatheringInteractable::ADWGatheringInteractable()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    Mesh->SetupAttachment(SceneRoot);
    Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
    Prompt = CreateDefaultSubobject<UDWInteractionPromptComponent>(TEXT("InteractionPrompt"));
    Prompt->SetupAttachment(SceneRoot);
}

void ADWGatheringInteractable::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Prompt->GenericDetectionRadius = FMath::Max(1.f, InteractionRadius);
    Prompt->HeightTolerance = FMath::Max(0.f, HeightTolerance);
}

void ADWGatheringInteractable::BeginPlay()
{
    Super::BeginPlay();
    if (PersistentId.IsNone()) PersistentId = FName(*GetPathName());
    const float Lower = FMath::Clamp(RandomScaleMin, .1f, 2.f);
    const float Upper = FMath::Clamp(RandomScaleMax, Lower, 2.f);
    Mesh->SetRelativeScale3D(Mesh->GetRelativeScale3D() * FMath::FRandRange(Lower, Upper));
    Prompt->GenericDetectionRadius = FMath::Max(1.f, InteractionRadius);
    Prompt->HeightTolerance = FMath::Max(0.f, HeightTolerance);
    PublishInteractionState();
}

bool ADWGatheringInteractable::IsGatheringWorld() const
{
    const UWorld* World = GetWorld();
    auto* GI = World ? World->GetGameInstance<UDWGameInstance>() : nullptr;
    return GI && Cast<UDWGatheringProfile>(GI->GetConfig());
}

bool ADWGatheringInteractable::IsInteractionAvailable() const
{
    return !bStaged && !IsActorBeingDestroyed() && IsGatheringWorld();
}

bool ADWGatheringInteractable::IsWithinReach(const AActor* User, float MaxReach) const
{
    if (!IsValid(User) || User->GetWorld() != GetWorld() || !FMath::IsFinite(MaxReach)) return false;
    const float Radius = MaxReach > 0.f ? FMath::Min(InteractionRadius, MaxReach) : InteractionRadius;
    return Radius > 0.f && FMath::IsFinite(Radius) && FMath::IsFinite(HeightTolerance) &&
        FMath::Abs(User->GetActorLocation().Z - GetActorLocation().Z) <= FMath::Max(0.f, HeightTolerance) &&
        FVector::DistSquared2D(User->GetActorLocation(), GetActorLocation()) <= FMath::Square(double(Radius));
}

bool ADWGatheringInteractable::CanPlayerInteract(const ADWPlayerCharacter* User, float MaxReach) const
{
    const auto* PC = IsValid(User) ? Cast<ADWPlayerController>(User->GetController()) : nullptr;
    return IsInteractionAvailable() && IsValid(User) && !User->IsActorBeingDestroyed() && !User->IsDead() &&
        Cast<UDWGatheringProfile>(User->GetGameplayConfig()) && IsWithinReach(User, MaxReach) &&
        GetWorld() && !GetWorld()->bIsTearingDown && !GetWorld()->IsPaused() && (!PC || !PC->IsGameplayBlocked());
}

void ADWGatheringInteractable::PublishInteractionState()
{
    const bool Available = IsInteractionAvailable();
    SetActorHiddenInGame(!Available);
    SetActorEnableCollision(Available);
    Prompt->SetPromptEnabled(Available);
}

ADWGatheringResource::ADWGatheringResource()
{
    Mesh->SetRelativeScale3D(FVector(.8, .8, 2.));
    Mesh->SetRelativeLocation(FVector(0, 0, 100.));
    Drops = { FDWItemStack(TEXT("SugarStick"), 2), FDWItemStack(TEXT("SugarChunk"), 3) };
    Prompt->GenericPromptTitle = FText::FromString(TEXT("糖树"));
    Prompt->GenericEnglishPromptTitle = FText::FromString(TEXT("Sugar Tree"));
    Prompt->GenericPromptAction = FText::FromString(TEXT("使用工具采集"));
    Prompt->GenericEnglishAction = FText::FromString(TEXT("Gather with tool"));
    Prompt->WorldOffset = FVector(0, 0, 240.);
}

void ADWGatheringResource::BeginPlay()
{
    MaxDurability = FMath::IsFinite(MaxDurability) ? FMath::Max(1., MaxDurability) : 100.;
    Durability = FMath::IsFinite(Durability) ? FMath::Clamp(Durability, 0., MaxDurability) : MaxDurability;
    Super::BeginPlay();
    if (Durability <= 0. && !bDropsReleased && !bStaged) ScheduleDrops(DropDelaySeconds);
}

void ADWGatheringResource::EndPlay(const EEndPlayReason::Type Reason)
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(DropTimer);
    Super::EndPlay(Reason);
}

bool ADWGatheringResource::IsInteractionAvailable() const
{
    return Super::IsInteractionAvailable() && FMath::IsFinite(Durability) && Durability > 0. && !bDropsReleased;
}

bool ADWGatheringResource::ApplyToolHit(ADWPlayerCharacter* User, FName Kind, double Sharpness, float Reach)
{
    if (bApplyingHit || bReleasingDrops || Kind != RequiredTool || !FMath::IsFinite(Sharpness) || Sharpness <= 0. ||
        !FMath::IsFinite(Reach) || Reach <= 0.f || !CanPlayerInteract(User, Reach)) return false;
    bApplyingHit = true;
    const double ActualDamage = FMath::Min(Sharpness, Durability);
    Durability = FMath::Max(0., Durability - ActualDamage);
    const bool Exhausted = Durability <= 0.;
    // Commit depletion and the drop timer before any user-defined presentation callback.
    if (Exhausted) ScheduleDrops(DropDelaySeconds);
    const TWeakObjectPtr<ADWGatheringResource> WeakThis(this);
    OnGatheringHit(ActualDamage, Durability);
    if (!WeakThis.IsValid() || IsActorBeingDestroyed()) return true;
    if (Exhausted && Durability <= 0.) OnGatheringExhausted();
    if (!WeakThis.IsValid() || IsActorBeingDestroyed()) return true;
    bApplyingHit = false;
    PublishInteractionState();
    return true;
}

void ADWGatheringResource::ScheduleDrops(float Delay)
{
    if (!GetWorld() || GetWorld()->bIsTearingDown || bDropsReleased || bStaged || Durability > 0.) return;
    PendingDropDelay = FMath::IsFinite(Delay) ? FMath::Max(.001f, Delay) : .35f;
    GetWorld()->GetTimerManager().SetTimer(DropTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
    {
        PendingDropDelay = -1.f;
        if (!ReleaseDrops() && !bDropsReleased && Durability <= 0. && !bStaged) ScheduleDrops(.5f);
    }), PendingDropDelay, false);
}

bool ADWGatheringResource::ReleaseDrops()
{
    if (bReleasingDrops || bStaged || bDropsReleased || Durability > 0. || !GetWorld() || GetWorld()->bIsTearingDown || !IsGatheringWorld()) return false;
    const auto* Profile = Cast<UDWGatheringProfile>(GetWorld()->GetGameInstance<UDWGameInstance>()->GetConfig());
    const TArray<FDWItemStack> DropSnapshot = Drops;
    for (const auto& Drop : DropSnapshot)
        if (Drop.Quantity <= 0 || !Profile->IsInventoryItem(Drop.ItemId) || !Profile->GetItemDefinition(Drop.ItemId)) return false;
    bReleasingDrops = true;
    TArray<TWeakObjectPtr<ADWGatheringPickup>> Prepared;
    const TWeakObjectPtr<ADWGatheringResource> WeakThis(this);
    UWorld* World = GetWorld();
    bool Ready = true;
    for (int32 Index = 0; Index < DropSnapshot.Num(); ++Index)
    {
        const double Angle = double(Index) * 2.3999632297;
        FVector Position = GetActorLocation() + FVector(FMath::Cos(Angle) * 100., FMath::Sin(Angle) * 100., 30.);
        FHitResult Ground;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(GatheringDropGround), false, this);
        if (World->LineTraceSingleByChannel(Ground, Position + FVector(0, 0, 150.), Position - FVector(0, 0, 450.), ECC_Visibility, Query))
            Position.Z = Ground.ImpactPoint.Z + 20.;
        const FTransform SpawnTransform(Position);
        auto* Pickup = World->SpawnActorDeferred<ADWGatheringPickup>(ADWGatheringPickup::StaticClass(), SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!Pickup) { Ready = false; break; }
        Prepared.Add(Pickup);
        Pickup->SetStaged(true);
        Pickup->bRuntimeSpawned = true;
        Pickup->PersistentId = FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
        Pickup->ItemId = DropSnapshot[Index].ItemId;
        Pickup->Quantity = DropSnapshot[Index].Quantity;
        Pickup->bPopOnSpawn = true;
        Pickup->Prompt->bPromptEnabled = false;
        Pickup->SetActorHiddenInGame(true);
        Pickup->SetActorEnableCollision(false);
        Pickup->FinishSpawning(SpawnTransform);
        if (!WeakThis.IsValid() || IsActorBeingDestroyed() || World->bIsTearingDown || Durability > 0. || bDropsReleased ||
            !IsValid(Pickup) || Pickup->IsActorBeingDestroyed()) { Ready = false; break; }
    }
    for (const auto& Item : Prepared) if (!Item.IsValid() || Item->IsActorBeingDestroyed()) Ready = false;
    if (WeakThis.IsValid() && !IsActorBeingDestroyed())
    {
        if (!IsGatheringWorld() || bStaged || bDropsReleased || Durability > 0. || Drops.Num() != DropSnapshot.Num()) Ready = false;
        if (Drops.Num() == DropSnapshot.Num())
            for (int32 Index = 0; Index < Drops.Num(); ++Index)
                if (Drops[Index].ItemId != DropSnapshot[Index].ItemId || Drops[Index].Quantity != DropSnapshot[Index].Quantity) Ready = false;
    }
    else Ready = false;
    if (!Ready)
    {
        for (auto Item : Prepared) if (Item.IsValid()) Item->Destroy();
        if (WeakThis.IsValid()) bReleasingDrops = false;
        return false;
    }
    // All staged actors are complete. Commit all facts before publishing any presentation.
    bDropsReleased = true;
    bReleasingDrops = false;
    World->GetTimerManager().ClearTimer(DropTimer);
    PendingDropDelay = -1.f;
    for (auto Item : Prepared) Item->SetStaged(false);
    for (auto Item : Prepared) if (Item.IsValid() && !Item->IsActorBeingDestroyed()) Item->RefreshPickup();
    if (WeakThis.IsValid() && !IsActorBeingDestroyed()) PublishInteractionState();
    return true;
}

float ADWGatheringResource::GetPendingDropSeconds() const
{
    if (bDropsReleased || Durability > 0.) return -1.f;
    if (GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(DropTimer))
        return FMath::Max(0.f, GetWorld()->GetTimerManager().GetTimerRemaining(DropTimer));
    return PendingDropDelay;
}

void ADWGatheringResource::AdoptResourceState(double Health, bool Released, float RemainingDelay)
{
    Durability = FMath::IsFinite(Health) ? FMath::Clamp(Health, 0., FMath::Max(1., MaxDurability)) : 0.;
    bDropsReleased = Released;
    if (Released) Durability = 0.;
    PendingDropDelay = FMath::IsFinite(RemainingDelay) ? RemainingDelay : DropDelaySeconds;
}

void ADWGatheringResource::PublishResourceState()
{
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(DropTimer);
    if (!bDropsReleased && Durability <= 0. && !bStaged) ScheduleDrops(FMath::Max(0.f, PendingDropDelay));
    PublishInteractionState();
}

void ADWGatheringResource::RestoreResource(double Health, bool Released)
{
    AdoptResourceState(Health, Released, DropDelaySeconds);
    PublishResourceState();
}

void ADWGatheringResource::PublishInteractionState()
{
    const bool Visible = !bStaged && !bDropsReleased && IsGatheringWorld();
    SetActorHiddenInGame(!Visible);
    SetActorEnableCollision(Visible);
    Prompt->SetPromptEnabled(IsInteractionAvailable());
}

ADWGatheringPickup::ADWGatheringPickup()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    Mesh->SetRelativeScale3D(FVector(.35));
    Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    Prompt->GenericPromptAction = FText::FromString(TEXT("拾取"));
    Prompt->GenericEnglishAction = FText::FromString(TEXT("Pick up"));
    Prompt->WorldOffset = FVector(0, 0, 75.);
}

void ADWGatheringPickup::BeginPlay()
{
    MeshRestLocation = Mesh->GetRelativeLocation();
    Super::BeginPlay();
    RefreshPickup();
}

bool ADWGatheringPickup::IsInteractionAvailable() const
{
    return Super::IsInteractionAvailable() && Quantity > 0 && !ItemId.IsNone();
}

bool ADWGatheringPickup::TryCollect(ADWPlayerCharacter* User)
{
    if (bCollecting || !CanPlayerInteract(User)) return false;
    auto* Inventory = User->GetInventory();
    if (!IsValid(Inventory)) return false;
    FDWPreparedInventoryChange Plan;
    int32 Accepted = 0;
    if (!Inventory->PrepareAddItemUpTo(ItemId, Quantity, Accepted, Plan) || Accepted <= 0)
    {
        UDWGatheringComponent::Notify(User, TEXT("背包已满，物品仍留在地上。"), TEXT("Inventory full. Items remain on the ground."));
        return false;
    }
    bCollecting = true;
    const FName CollectedItem = ItemId;
    const int32 PreviousQuantity = Quantity;
    const TWeakObjectPtr<ADWGatheringPickup> WeakThis(this);
    const TWeakObjectPtr<ADWPlayerCharacter> WeakUser(User);
    // No callback between quantity debit and inventory adoption. OnChanged observes both facts.
    Quantity -= Accepted;
    if (!Inventory->CommitPrepared(MoveTemp(Plan)))
    {
        Quantity = PreviousQuantity;
        bCollecting = false;
        return false;
    }
    if (WeakThis.IsValid() && !IsActorBeingDestroyed())
    {
        bCollecting = false;
        RefreshPickup();
    }
    if (WeakUser.IsValid() && !User->IsActorBeingDestroyed())
        if (auto* PC = Cast<APlayerController>(User->GetController()))
            if (auto* HUD = Cast<ADWGameplayHUD>(PC->GetHUD())) HUD->NotifyItemAcquired(CollectedItem, Accepted);
    return true;
}

void ADWGatheringPickup::RefreshPickup()
{
    auto* GI = GetWorld() ? GetWorld()->GetGameInstance<UDWGameInstance>() : nullptr;
    const auto* Config = GI ? GI->GetConfig() : nullptr;
    const auto* Definition = Config ? Config->GetItemDefinition(ItemId) : nullptr;
    Prompt->GenericPromptTitle = Definition ? Definition->DisplayName : FText::FromName(ItemId);
    Prompt->GenericEnglishPromptTitle = Definition && !Definition->EnglishDisplayName.IsEmpty() ? Definition->EnglishDisplayName : FText::FromName(ItemId);
    PublishInteractionState();
}

void ADWGatheringPickup::PublishInteractionState()
{
    if (IsInteractionAvailable() && bPopOnSpawn && !bPopStarted && HasActorBegunPlay())
    {
        bPopStarted = true;
        PopElapsed = 0.f;
        SetActorTickEnabled(true);
    }
    if (Quantity <= 0 || bStaged)
    {
        SetActorTickEnabled(false);
        Mesh->SetRelativeLocation(MeshRestLocation);
    }
    Super::PublishInteractionState();
}

void ADWGatheringPickup::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f || bStaged || Quantity <= 0) return;
    PopElapsed += DeltaSeconds;
    const float Progress = FMath::Clamp(PopElapsed / .45f, 0.f, 1.f);
    Mesh->SetRelativeLocation(MeshRestLocation + FVector(0, 0, FMath::Sin(Progress * UE_PI) * 55.f));
    if (Progress >= 1.f)
    {
        Mesh->SetRelativeLocation(MeshRestLocation);
        SetActorTickEnabled(false);
    }
}

ADWGatheringStation::ADWGatheringStation()
{
    RandomScaleMin = RandomScaleMax = 1.f;
    Mesh->SetRelativeScale3D(FVector(1.6, 1.2, 1.));
    Prompt->GenericPromptAction = FText::FromString(TEXT("制作"));
    Prompt->GenericEnglishAction = FText::FromString(TEXT("Craft"));
    Prompt->GenericPromptTitle = FText::FromString(TEXT("合成台"));
    Prompt->GenericEnglishPromptTitle = FText::FromString(TEXT("Crafting Table"));
}

bool ADWGatheringStation::IsInteractionAvailable() const
{
    return Super::IsInteractionAvailable() && bStationEnabled && !StationId.IsNone();
}

bool ADWGatheringStation::ConfigureBuilding(FName ItemId)
{
    if (ItemId == TEXT("CraftingTable")) StationId = TEXT("GatherStation_CraftingTable");
    else if (ItemId == TEXT("Furnace")) StationId = TEXT("GatherStation_Furnace");
    else if (ItemId == TEXT("FermentationVat")) StationId = TEXT("GatherStation_FermentationVat");
    else return false;
    BuildingItemId = ItemId;
    return true;
}

void ADWGatheringStation::PublishInteractionState()
{
    auto* GI = GetWorld() ? GetWorld()->GetGameInstance<UDWGameInstance>() : nullptr;
    const auto* Config = GI ? GI->GetConfig() : nullptr;
    const auto* Definition = Config ? Config->GetItemDefinition(BuildingItemId) : nullptr;
    if (Definition)
    {
        Prompt->GenericPromptTitle = Definition->DisplayName;
        Prompt->GenericEnglishPromptTitle = Definition->EnglishDisplayName;
    }
    Super::PublishInteractionState();
}

ADWGatheringRemains::ADWGatheringRemains()
{
    Mesh->SetRelativeScale3D(FVector(1., .7, .3));
    Prompt->GenericPromptTitle = FText::FromString(TEXT("残骸"));
    Prompt->GenericEnglishPromptTitle = FText::FromString(TEXT("Remains"));
    Prompt->GenericPromptAction = FText::FromString(TEXT("玩法待接入"));
    Prompt->GenericEnglishAction = FText::FromString(TEXT("Rules pending"));
}
