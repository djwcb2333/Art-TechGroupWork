#include "DWGatheringWorld.h"
#include "DWGatheringHitAudioSubsystem.h"
#include "DWRetiredItemPolicy.h"
#include "DWLootBeaconSubsystem.h"
#include "DWPickupPresentationComponent.h"
#include "DWGatheringPresentationComponent.h"
#include "DWGatheringPresentationMath.h"
#include "DWBuildingEffectsComponent.h"
#include "DWGatheringComponent.h"
#include "DWGatheringProfile.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "DWInteractionPromptComponent.h"
#include "DWPlayerCharacter.h"
#include "DWCombatAuthoritySubsystem.h"
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
    return GI && UDWGameplayConfig::AsGatheringConfig(GI->GetConfig());
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
        UDWGameplayConfig::AsGatheringConfig(User->GetGameplayConfig()) && IsWithinReach(User, MaxReach) &&
        GetWorld() && !GetWorld()->bIsTearingDown && !GetWorld()->IsPaused() && (!PC || !PC->IsGameplayBlocked());
}

void ADWGatheringInteractable::PublishInteractionState()
{
    const bool Available = IsInteractionAvailable();
    if(!bStaged&&GetWorld()&&GetWorld()->IsGameWorld())
        if(auto* GI=GetWorld()->GetGameInstance<UDWGameInstance>())GI->MarkUnsupportedGatheringPersistence(GetWorld());
    SetActorHiddenInGame(!Available);
    SetActorEnableCollision(Available);
    Prompt->SetPromptEnabled(Available);
}

ADWGatheringResource::ADWGatheringResource()
{
    GatheringPresentation=CreateDefaultSubobject<UDWGatheringPresentationComponent>(TEXT("GatheringPresentation"));
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
    Drops.RemoveAll([](const FDWItemStack& Item){return DWRetiredItemPolicy::IsRetired(Item.ItemId);});
    MaxDurability = FMath::IsFinite(MaxDurability) ? FMath::Max(1., MaxDurability) : 100.;
    Durability = FMath::IsFinite(Durability) ? FMath::Clamp(Durability, 0., MaxDurability) : MaxDurability;
    if(GatheringPresentation)GatheringPresentation->OnExhaustionSettlementReady.AddUniqueDynamic(this,&ADWGatheringResource::HandleGatheringSettlementReady);
    Super::BeginPlay();
    if (Durability <= 0. && !bDropsReleased && !bStaged) ScheduleDrops(DropDelaySeconds);
}

void ADWGatheringResource::EndPlay(const EEndPlayReason::Type Reason)
{
    if(GatheringPresentation)GatheringPresentation->OnExhaustionSettlementReady.RemoveDynamic(this,&ADWGatheringResource::HandleGatheringSettlementReady);
    if (GetWorld()){GetWorld()->GetTimerManager().ClearTimer(DropTimer);GetWorld()->GetTimerManager().ClearTimer(FeedbackHoldTimer);}
    Super::EndPlay(Reason);
}

float ADWGatheringResource::GetGatheringProgress() const
{
    return DWGatheringPresentationMath::GatheredFraction(Durability,MaxDurability);
}

bool ADWGatheringResource::IsInteractionAvailable() const
{
    return Super::IsInteractionAvailable() && FMath::IsFinite(Durability) && Durability > 0. && !bDropsReleased;
}

bool ADWGatheringResource::ApplyToolHit(ADWPlayerCharacter* User, FName Kind, double Sharpness, float Reach,FName ToolItemId,DWCombatCommit::FActionId ParentAction)
{
    if (bApplyingHit || bReleasingDrops || Kind != RequiredTool || !FMath::IsFinite(Sharpness) || Sharpness <= 0. ||
        !FMath::IsFinite(Reach) || Reach <= 0.f || !CanPlayerInteract(User, Reach)) return false;
    FDWCharacterA01Runtime CorruptionCandidate;FDWA01CorruptionResult Enhancement;UDWCharacterA01Component* CorruptionOwner=nullptr;
    const double ExpectedHealth=User?User->GetHealth():0.;uint64 ExpectedRevision=0;double EffectiveSharpness=Sharpness;
    if(User&&User->CharacterA01&&User->CharacterA01->IsEnabled()&&ParentAction.Id)
    {
        auto* A=UDWCombatAuthoritySubsystem::Get(this);
        if(!A||!A->IsActionOwnedBy(ParentAction,User)||!User->GetInventory()||User->GetInventory()->CountItem(ToolItemId)<1)return false;
        ExpectedRevision=User->CharacterA01->GetResourceRevision();
        if(User->CharacterA01->PrepareCorruption(ParentAction.Id,ToolItemId,true,CorruptionCandidate,Enhancement))
        {CorruptionOwner=User->CharacterA01.Get();EffectiveSharpness*=Enhancement.HarvestMultiplier;}
    }
    if(!FMath::IsFinite(EffectiveSharpness)||EffectiveSharpness<=0.||(CorruptionOwner&&!CorruptionOwner->CanAdoptRuntime(ExpectedRevision,ExpectedHealth)))return false;
    bApplyingHit = true;
    const float PreviousProgress=GetGatheringProgress();
    const double ActualDamage = FMath::Min(EffectiveSharpness, Durability);
    Durability = FMath::Max(0., Durability - ActualDamage);
    if(CorruptionOwner)CorruptionOwner->AdoptRuntimeUnchecked(MoveTemp(CorruptionCandidate)); // Joint native facts, before any hit/Blueprint callback.
    const bool Exhausted = Durability <= 0.;
    // Cosmetic notification follows committed facts, once, before external callbacks.
    if(auto* GI=GetGameInstance())if(auto* Audio=GI->GetSubsystem<UDWGatheringHitAudioSubsystem>())
        Audio->PlayCommittedHit(this,Kind,GetActorLocation());
    // Commit HP first. UI feedback sees exactly this real debit, before external presentation callbacks.
    const TWeakObjectPtr<ADWGatheringResource> WeakThis(this);
    if(Prompt)Prompt->NotifyGatheringHitCommitted(User,PreviousProgress,GetGatheringProgress());
    if (!WeakThis.IsValid() || IsActorBeingDestroyed()) return true;
    if(GatheringPresentation)GatheringPresentation->PlayAcceptedHit(User->GetActorLocation(),GetGatheringProgress(),Exhausted);
    if (!WeakThis.IsValid() || IsActorBeingDestroyed()) return true;
    // Real contact or bounded readiness starts the existing exact-once reward timer.
    // Missing/invalid physics assets remain an explicit setup failure; preserve legacy reward safety.
    const bool Physical=GatheringPresentation&&GatheringPresentation->bExhaustionStarted&&!GatheringPresentation->bPhysicsSetupFailed;
    if(Exhausted&&!Physical)ScheduleDrops(DropDelaySeconds);
    OnGatheringHit(ActualDamage, Durability);
    if (!WeakThis.IsValid() || IsActorBeingDestroyed()) return true;
    if (Exhausted && Durability <= 0.) OnGatheringExhausted();
    if (!WeakThis.IsValid() || IsActorBeingDestroyed()) return true;
    bApplyingHit = false;
    PublishInteractionState();
    return true;
}

void ADWGatheringResource::HandleGatheringPhysicalGroundContact()
{
    if(GatheringPresentation&&GatheringPresentation->bContactedGround)HandleGatheringSettlementReady();
}
void ADWGatheringResource::HandleGatheringSettlementReady()
{
    if(!IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown&&!bStaged&&!bDropsReleased&&Durability<=0.&&
        GatheringPresentation&&GatheringPresentation->HasExhaustionSettlementReady()&&!GatheringPresentation->bPhysicsSetupFailed)
        ScheduleDrops(DropDelaySeconds);
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
    if (bReleasingDrops || bStaged || bDropsReleased || Durability > 0. || !GetWorld() || GetWorld()->bIsTearingDown || !IsGatheringWorld() ||
        (GatheringPresentation&&GatheringPresentation->IsAwaitingExhaustionSettlement())) return false;
    const auto* Profile = UDWGameplayConfig::AsGatheringConfig(GetWorld()->GetGameInstance<UDWGameInstance>()->GetConfig());
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
        Pickup->SetPopOrigin(GetActorLocation()+FVector(0,0,45.));
        if(const auto* Visual=DropMeshes.Find(Pickup->ItemId))
            if(auto* Asset=Visual->LoadSynchronous())
            {
                Pickup->Mesh->SetStaticMesh(Asset);
                Pickup->Mesh->SetRelativeTransform(DropMeshTransforms.Contains(Pickup->ItemId)?DropMeshTransforms[Pickup->ItemId]:FTransform::Identity);
                Pickup->RandomScaleMin=Pickup->RandomScaleMax=1.f;
            }
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
    bRestorePresentationPending = true;
}

void ADWGatheringResource::PublishResourceState()
{
    if(bRestorePresentationPending)
    {
        ResetGatheringPersistencePresentation();
        FString RestoreError;
        if(!PublishGatheringPersistencePresentation(RestoreError))UE_LOG(LogTemp,Warning,TEXT("Gathering restore presentation: %s"),*RestoreError);
    }
    if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(DropTimer);
    if (!bDropsReleased && Durability <= 0. && !bStaged && !(GatheringPresentation&&GatheringPresentation->IsAwaitingExhaustionSettlement())) ScheduleDrops(FMath::Max(0.f, PendingDropDelay));
    PublishInteractionState();
}

void ADWGatheringResource::RestoreResource(double Health, bool Released)
{
    AdoptResourceState(Health, Released, DropDelaySeconds);
    PublishResourceState();
}

void ADWGatheringResource::PublishInteractionState()
{
    const bool Playing=GatheringPresentation&&GatheringPresentation->IsExhaustionVisible();
    const bool Finished=GatheringPresentation&&GatheringPresentation->bPresentationFinished;
    const bool StumpRemains=GatheringPresentation&&GatheringPresentation->HasPersistentStump();
    const bool PhysicalBodies=GatheringPresentation&&GatheringPresentation->GetActivePhysicsBodyCount()>0;
    const bool PendingFeedback=Prompt&&Prompt->HasPendingGatheringHitFeedback();
    const bool Available=IsInteractionAvailable();
    const bool Visible=!bStaged&&IsGatheringWorld()&&(StumpRemains||PendingFeedback||(!Finished&&(!bDropsReleased||Playing)));
    SetActorHiddenInGame(!Visible);
    // Disabling the Actor would disable ALL detached-but-owned Chaos components.
    // Disable only the intact resource collider; keep live physics and stump collision.
    SetActorEnableCollision(Visible&&(Available||PhysicalBodies||StumpRemains));
    if((Playing||PhysicalBodies||StumpRemains)&&Mesh)Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if(Prompt)Prompt->SetPromptEnabled(Available||PendingFeedback);
    if(GetWorld())
    {
        if(PendingFeedback&&!Playing&&!StumpRemains&&(bDropsReleased||Finished))
            GetWorld()->GetTimerManager().SetTimer(FeedbackHoldTimer,FTimerDelegate::CreateWeakLambda(this,[this](){PublishInteractionState();}),.05f,false);
        else GetWorld()->GetTimerManager().ClearTimer(FeedbackHoldTimer);
    }
}

ADWGatheringPickup::ADWGatheringPickup()
{
    PickupPresentation=CreateDefaultSubobject<UDWPickupPresentationComponent>(TEXT("PickupPresentation"));
    RandomScaleMin=RandomScaleMax=1.f;
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

void ADWGatheringPickup::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if(PickupPresentation) PickupPresentation->NormalizePickupVisual();
}

void ADWGatheringPickup::BeginPlay()
{
    if(DWRetiredItemPolicy::IsRetired(ItemId))Quantity=0;
    // Old placed instances may still serialize the legacy .7..1 random scale range.
    RandomScaleMin=RandomScaleMax=1.f;
    MeshRestLocation=Mesh->GetRelativeLocation();
    Super::BeginPlay();
    if(PickupPresentation) PickupPresentation->NormalizePickupVisual();
    RefreshPickup();
    // Actor BeginPlay has not yet returned; defer the first published visual until
    // the actor is fully begun, preserving the original world epoch and identity.
    if(UWorld* W=GetWorld();W&&W->IsGameWorld()&&!W->bIsTearingDown)
    {
        const TWeakObjectPtr<ADWGatheringPickup> Self(this);const TWeakObjectPtr<UWorld> World(W);
        const TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority=UDWCombatAuthoritySubsystem::Get(this);
        const bool bHadAuthority=Authority.IsValid();const uint64 Epoch=bHadAuthority?Authority->GetWorldEpoch():0;
        W->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[Self,World,Authority,Epoch,bHadAuthority]()
        {
            if(!Self.IsValid()||Self->IsActorBeingDestroyed()||!World.IsValid()||World->bIsTearingDown||Self->GetWorld()!=World.Get()||Self->IsStaged())return;
            auto* Current=UDWCombatAuthoritySubsystem::Get(Self.Get());
            if(bHadAuthority&&(!Authority.IsValid()||Current!=Authority.Get()||Current->GetWorldEpoch()!=Epoch))return;
            Self->RefreshPickup();
        }));
    }
}

void ADWGatheringPickup::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* W=GetWorld())if(auto* V=W->GetSubsystem<UDWLootBeaconSubsystem>())V->WithdrawBeacon(this);
    Super::EndPlay(Reason);
}
bool ADWGatheringPickup::IsInteractionAvailable() const
{
    return Super::IsInteractionAvailable() && Quantity > 0 && !ItemId.IsNone() && !bCollecting &&
        !(PickupPresentation && PickupPresentation->IsCollectionActive());
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
    if(auto* W=GetWorld())if(auto* V=W->GetSubsystem<UDWLootBeaconSubsystem>())V->SuspendBeacon(this);
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
        RefreshPickup();
        return false;
    }
    if (WeakThis.IsValid() && !IsActorBeingDestroyed())
    {
        // Guard stays set until the successful-commit visual is armed. A rejected commit
        // never enters this block. Existing pop motion yields to the cosmetic proxy.
        if(PickupPresentation && PickupPresentation->PlayCommittedPickupEffect()) SetActorTickEnabled(false);
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

void ADWGatheringPickup::HandlePickupPresentationFinished()
{
    // The source never stretched; restore the end of its scatter motion before showing
    // a partial stack again. A fully collected stack remains hidden by Publish below.
    SetActorTickEnabled(false);
    Mesh->SetRelativeLocation(MeshRestLocation);
    RefreshPickup();
}

void ADWGatheringPickup::PublishInteractionState()
{
    const bool Playing=PickupPresentation && PickupPresentation->IsCollectionActive();
    const TWeakObjectPtr<ADWGatheringPickup> SafeThis(this);
    const TWeakObjectPtr<UDWCombatAuthoritySubsystem> SafeAuthority=UDWCombatAuthoritySubsystem::Get(this);
    const bool bHasAuthority=SafeAuthority.IsValid();const uint64 Epoch=bHasAuthority?SafeAuthority->GetWorldEpoch():0;
    auto StillCurrent=[&]()
    {if(!SafeThis.IsValid()||IsActorBeingDestroyed())return false;
     auto* A=UDWCombatAuthoritySubsystem::Get(this);return !bHasAuthority||(SafeAuthority.IsValid()&&A==SafeAuthority.Get()&&A->GetWorldEpoch()==Epoch);};
    Super::PublishInteractionState();
    if(!StillCurrent())return;
    if(UWorld* W=GetWorld();W&&W->IsGameWorld())if(auto* V=W->GetSubsystem<UDWLootBeaconSubsystem>())
    {
        if(Playing)V->SuspendBeacon(this);
        else if(IsInteractionAvailable()&&!IsHidden())
        {bool Changed=false;V->PublishBeacon(this,Mesh,ItemId,&Changed);if(!StillCurrent())return;
         if(Changed){MeshRestLocation=Mesh->GetRelativeLocation();if(PickupPresentation)PickupPresentation->OnCatalogVisualChanged();if(!StillCurrent())return;}}
        else V->WithdrawBeacon(this);
    }
    if(!StillCurrent())return;
    if (!Playing && IsInteractionAvailable() && bPopOnSpawn && !bPopStarted && HasActorBegunPlay())
    {
        bPopStarted = true;
        PopElapsed = 0.f;
        if(bHasPopOrigin)Mesh->SetWorldLocation(PopOriginWorld);
        SetActorTickEnabled(true);
    }
    if (Playing || Quantity <= 0 || bStaged)
    {
        SetActorTickEnabled(false);
        if(!Playing) Mesh->SetRelativeLocation(MeshRestLocation);
    }
    if(Playing && !bStaged && IsGatheringWorld())
    {
        // Keep only the visual proxy alive. The logical pickup is unavailable already.
        SetActorHiddenInGame(false);
        SetActorEnableCollision(false);
        Prompt->SetPromptEnabled(false);
    }
}

void ADWGatheringPickup::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f || bStaged || Quantity <= 0 ||
        (PickupPresentation && PickupPresentation->IsCollectionActive())) return;
    PopElapsed += DeltaSeconds;
    const float Duration=FMath::IsFinite(PopDuration)?FMath::Max(.05f,PopDuration):.55f;
    const float Progress = FMath::Clamp(PopElapsed / Duration, 0.f, 1.f);
    const FVector End=SceneRoot->GetComponentTransform().TransformPosition(MeshRestLocation);
    const FVector Start=bHasPopOrigin?PopOriginWorld:End;
    Mesh->SetWorldLocation(DWGatheringPresentationMath::ScatterPoint(Start,End,Progress,PopHeight));
    if (Progress >= 1.f)
    {
        Mesh->SetRelativeLocation(MeshRestLocation);
        SetActorTickEnabled(false);
    }
}

ADWGatheringStation::ADWGatheringStation()
{
    BuildingEffects = CreateDefaultSubobject<UDWBuildingEffectsComponent>(TEXT("BuildingEffects"));
    RandomScaleMin = RandomScaleMax = 1.f;
    Mesh->SetRelativeScale3D(FVector(1.6, 1.2, 1.));
    Prompt->GenericPromptAction = FText::FromString(TEXT("制作"));
    Prompt->GenericEnglishAction = FText::FromString(TEXT("Craft"));
    Prompt->GenericPromptTitle = FText::FromString(TEXT("合成台"));
    Prompt->GenericEnglishPromptTitle = FText::FromString(TEXT("Crafting Table"));
}

bool ADWGatheringStation::IsInteractionAvailable() const
{
    return Super::IsInteractionAvailable() && !bDemolished && bStationEnabled && !StationId.IsNone();
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

// LANG-01 complete-save helpers. No damage, drops, construction effect or reward is replayed.
bool ADWGatheringResource::CanCaptureGatheringPersistence(FString& Error) const
{
    if(bApplyingHit||bReleasingDrops||(Durability<=0.&&!bDropsReleased)||
        (GatheringPresentation&&(GatheringPresentation->IsAwaitingExhaustionSettlement()||GatheringPresentation->GetActivePhysicsBodyCount()>0||
            (GatheringPresentation->Style==EDWGatheringDestructionStyle::TreeFall&&GatheringPresentation->IsExhaustionVisible()))))
    {Error=TEXT("Wait for this gathering hit, physical contact and pending drops to finish before saving.");return false;}
    Error.Reset();return true;
}
bool ADWGatheringResource::CanPrepareGatheringPersistence(bool /*SavedReleased*/,FString& Error) const
{
    Error.Reset();return true;
}
void ADWGatheringResource::ResetGatheringPersistencePresentation()
{
    if(GetWorld()){GetWorld()->GetTimerManager().ClearTimer(DropTimer);GetWorld()->GetTimerManager().ClearTimer(FeedbackHoldTimer);}
    if(GatheringPresentation)GatheringPresentation->ResetPresentation();
    // The ordinary presentation first restores its own source, if it ever captured one.
    // Cache that baseline before the FIRST persistence override; later restores use OUR
    // baseline even when no attack animation/source cache has ever existed.
    if(Mesh)
    {
        if(!bPersistenceSourceDefaultsCaptured)
        {
            bPersistenceSourceVisible=Mesh->GetVisibleFlag();bPersistenceSourceHiddenInGame=Mesh->bHiddenInGame;
            PersistenceSourceCollision=Mesh->GetCollisionEnabled();bPersistenceSourceDefaultsCaptured=true;
        }
        Mesh->SetVisibility(bPersistenceSourceVisible,false);Mesh->SetHiddenInGame(bPersistenceSourceHiddenInGame,false);
        Mesh->SetCollisionEnabled(PersistenceSourceCollision);
    }
    if(Prompt)Prompt->ResetGatheringHitFeedback();
    if(IsValid(RestoredStump))RestoredStump->DestroyComponent();RestoredStump=nullptr;
    bRestorePresentationPending=false;
}
bool ADWGatheringResource::PublishGatheringPersistencePresentation(FString& Error)
{
    if(IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown){Error=TEXT("Resource ended during saved-state publication.");return false;}
    if(bDropsReleased&&GatheringPresentation&&GatheringPresentation->Style==EDWGatheringDestructionStyle::TreeFall)
    {
        // Exhausted saves retain their actor and reward facts, but never recreate
        // the former persistent stump or replay its effect/drop transaction.
        Mesh->SetVisibility(false,false);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    }
    bRestorePresentationPending=false;PublishInteractionState();Error.Reset();return true;
}
bool ADWGatheringPickup::CanCaptureGatheringPersistence(FString& Error) const
{
    if(bCollecting||(PickupPresentation&&PickupPresentation->IsCollectionActive())||
        (bPopStarted&&IsActorTickEnabled()&&PopElapsed<FMath::Max(.05f,PopDuration)))
    {Error=TEXT("Wait for this pickup commit/presentation to finish before saving.");return false;}
    Error.Reset();return true;
}
void ADWGatheringPickup::AdoptGatheringPersistenceFacts(FName SavedItem,int32 SavedQuantity)
{
    ItemId=SavedItem;Quantity=SavedQuantity;bCollecting=false;bPopOnSpawn=false;
    bPopStarted=true;bHasPopOrigin=false;PopElapsed=0.f;
}
void ADWGatheringPickup::ResetGatheringPersistencePresentation()
{
    if(auto* W=GetWorld())if(auto* V=W->GetSubsystem<UDWLootBeaconSubsystem>())V->WithdrawBeacon(this);
    if(PickupPresentation){PickupPresentation->Deactivate();PickupPresentation->Activate(true);}
    SetActorTickEnabled(false);Mesh->SetRelativeLocation(MeshRestLocation);
}
