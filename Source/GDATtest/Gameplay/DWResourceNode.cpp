#include "DWResourceNode.h"
#include "DWInteractionPromptComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ADWResourceNode::ADWResourceNode()
{
    PrimaryActorTick.bCanEverTick = false;
    ResourceMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ResourceMesh"));
    SetRootComponent(ResourceMesh);
    ResourceMesh->SetCollisionProfileName(TEXT("BlockAll"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (Mesh.Succeeded()) ResourceMesh->SetStaticMesh(Mesh.Object);
    ResourceMesh->SetRelativeScale3D(FVector(1.1f, 1.1f, 0.7f));
    InteractionRange = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionRange"));
    InteractionRange->SetupAttachment(RootComponent);
    InteractionRange->SetUsingAbsoluteScale(true);
    InteractionRange->SetSphereRadius(InteractionRadius);
    InteractionRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    InteractionRange->SetGenerateOverlapEvents(false);
    InteractionRange->SetHiddenInGame(true);
    InteractionRange->SetVisibility(true);
    InteractionRange->ShapeColor = FColor(245, 195, 75);
    InteractionPrompt = CreateDefaultSubobject<UDWInteractionPromptComponent>(TEXT("InteractionPrompt"));
    InteractionPrompt->SetupAttachment(RootComponent);
    Tags.AddUnique(TEXT("DoughWorldResource"));
}

void ADWResourceNode::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    InteractionRange->SetSphereRadius(FMath::Max(1.f, InteractionRadius));
    HarvestInterval = FMath::Max(0.05f, HarvestInterval);
    HarvestAmount = FMath::Max(1, HarvestAmount);
    RemainingAmount = FMath::Max(0, RemainingAmount);
    if (InteractionPrompt) InteractionPrompt->RefreshWorldPlacement();
}

void ADWResourceNode::BeginPlay()
{
    Super::BeginPlay();
    RefreshAvailability();
}

bool ADWResourceNode::IsAvailable() const
{
    return !ItemId.IsNone() && HarvestAmount > 0 && (bInfinite || RemainingAmount > 0) && CanHarvest();
}

bool ADWResourceNode::CanHarvest_Implementation() const
{
    return true;
}

void ADWResourceNode::CommitHarvest(int32 ActualAmount)
{
    // Eligibility was checked before the player's inventory transaction. Never rerun
    // designer rules here: adding the item may itself have changed those rules.
    if (bCommittingHarvest || ActualAmount <= 0 || ItemId.IsNone() || (!bInfinite && RemainingAmount <= 0)) return;
    TGuardValue<bool> CommitGuard(bCommittingHarvest, true);
    const int32 Committed = bInfinite ? ActualAmount : FMath::Min(ActualAmount, RemainingAmount);
    if (!bInfinite) RemainingAmount -= Committed;
    RefreshAvailability();
    OnHarvestCommitted(Committed);
}

void ADWResourceNode::RestoreRemainingAmount(int32 NewAmount)
{
    RemainingAmount = FMath::Max(0, NewAmount);
    RefreshAvailability();
}

void ADWResourceNode::RefreshAvailability()
{
    const bool bDepleted = !bInfinite && RemainingAmount <= 0;
    ResourceMesh->SetHiddenInGame(bHideWhenDepleted && bDepleted);
    ResourceMesh->SetCollisionEnabled(bDepleted ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
    const bool bNowAvailable = IsAvailable();
    if (!bAvailabilityInitialized || bLastAvailable != bNowAvailable)
    {
        bAvailabilityInitialized = true;
        bLastAvailable = bNowAvailable;
        OnAvailabilityChanged(bNowAvailable);
    }
}

ADWWaterResourceNode::ADWWaterResourceNode()
{
    ItemId = TEXT("Water"); HarvestInterval = 1.f; HarvestAmount = 1; bInfinite = true;
    ResourceMesh->SetRelativeScale3D(FVector(1.5f, 1.5f, 0.3f));
    InteractionRange->ShapeColor = FColor(55, 175, 255);
}

ADWYeastResourceNode::ADWYeastResourceNode()
{
    ItemId = TEXT("Yeast"); HarvestInterval = 3.f; HarvestAmount = 5; bInfinite = true;
    ResourceMesh->SetRelativeScale3D(FVector(0.8f, 0.8f, 1.25f));
    InteractionRange->ShapeColor = FColor(155, 95, 225);
}

ADWDoughResourceNode::ADWDoughResourceNode()
{
    ItemId = TEXT("Dough"); HarvestInterval = 3.f; HarvestAmount = 3; RemainingAmount = 3;
}

ADWFlourResourceNode::ADWFlourResourceNode()
{
    ItemId = TEXT("Flour"); HarvestInterval = 3.f; HarvestAmount = 2; RemainingAmount = 20;
    ResourceMesh->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.8f));
}
