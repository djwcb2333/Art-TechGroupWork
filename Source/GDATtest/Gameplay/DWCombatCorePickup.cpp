#include "DWCombatCorePickup.h"

#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatCorePickupTransaction.h"
#include "DWInteractionPromptComponent.h"
#include "DWLootBeaconSubsystem.h"
#include "DWPickupPresentationComponent.h"
#include "DWPlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

ADWCombatCorePickup::ADWCombatCorePickup()
{
    PickupRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PickupRoot"));
    SetRootComponent(PickupRoot);
    Placeholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Placeholder"));
    Placeholder->SetupAttachment(PickupRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (Sphere.Succeeded()) Placeholder->SetStaticMesh(Sphere.Object);
    Placeholder->SetRelativeScale3D(FVector(.25));
    Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Placeholder->SetCanEverAffectNavigation(false);
    Prompt = CreateDefaultSubobject<UDWInteractionPromptComponent>(TEXT("InteractionPrompt"));
    Prompt->SetupAttachment(PickupRoot);
    Prompt->bPromptEnabled = false;
    Prompt->GenericPromptAction = FText::FromString(TEXT("拾取"));
    Prompt->GenericEnglishAction = FText::FromString(TEXT("Pick up"));
    Prompt->WorldOffset = FVector(0, 0, 75.);
    PickupPresentation = CreateDefaultSubobject<UDWPickupPresentationComponent>(TEXT("PickupPresentation"));
    PickupPresentation->bBudgetIdlePickupOutline = true;
}

bool ADWCombatCorePickup::IsPickupAvailable() const
{
    const auto* Drops = UDWCombatCoreDropSubsystem::Get(this);
    return !IsActorBeingDestroyed() && Drops && Drops->IsPhysicalCurrent(*this);
}

bool ADWCombatCorePickup::IsPickupInReach(const AActor* User) const
{
    return IsValid(User) && User->GetWorld() == GetWorld() && PickupRadiusCm > 0. &&
        FVector::DistSquared(User->GetActorLocation(), GetActorLocation()) <= FMath::Square(PickupRadiusCm);
}

bool ADWCombatCorePickup::TryCollect(ADWPlayerCharacter* Player)
{
    return IsValid(Player) && FDWCombatCorePickupTransaction::Commit(*this, *Player);
}

void ADWCombatCorePickup::BeginDropPresentation(const FVector* PopOrigin)
{
    const TWeakObjectPtr<ADWCombatCorePickup> Self(this);
    if (auto* Beacons = GetWorld()->GetSubsystem<UDWLootBeaconSubsystem>())
        Beacons->PublishBeacon(this, Placeholder, ItemId);
    if (!Self.IsValid() || !IsPickupAvailable()) return;
    if (PopOrigin) PickupPresentation->StartScatter(*PopOrigin);
    Prompt->SetPromptEnabled(true);
}

void ADWCombatCorePickup::PresentCommittedPickup()
{
    const TWeakObjectPtr<ADWCombatCorePickup> Self(this);
    Prompt->SetPromptEnabled(false);
    if (!Self.IsValid() || IsActorBeingDestroyed()) return;
    if (!GetWorld()->GetFirstPlayerController() || !PickupPresentation->PlayCommittedPickupEffect()) Destroy();
}

void ADWCombatCorePickup::HandlePickupPresentationFinished()
{
    if (bClaimed) Destroy();
}

void ADWCombatCorePickup::EndPlay(EEndPlayReason::Type Reason)
{
    if (auto* W = GetWorld())
        if (auto* Beacons = W->GetSubsystem<UDWLootBeaconSubsystem>()) Beacons->WithdrawBeacon(this);
    bBound = false;
    if (auto* Drops = UDWCombatCoreDropSubsystem::Get(this)) Drops->PhysicalEnded(*this);
    Super::EndPlay(Reason);
}
