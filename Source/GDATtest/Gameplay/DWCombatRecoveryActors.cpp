#include "DWCombatRecoveryActors.h"
#include "DWLootBeaconSubsystem.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWCombatRecoveryTransaction.h"
#include "DWPlayerCharacter.h"
#include "DWInteractionPromptComponent.h"
#include "DWPickupPresentationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UObject/ConstructorHelpers.h"

ADWCombatSafePoint::ADWCombatSafePoint()
{PrimaryActorTick.bCanEverTick=false;Destination=CreateDefaultSubobject<USceneComponent>(TEXT("RespawnCapsuleCentre"));SetRootComponent(Destination);}
bool ADWCombatSafePoint::ActivateForPlayer(ADWPlayerCharacter* P)
{auto* R=UDWCombatRecoverySubsystem::Get(this);return IsValid(P)&&R&&R->ActivateSafePoint(*P,*this);}
ADWCombatDeathBag::ADWCombatDeathBag()
{
    PickupRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PickupRoot"));
    SetRootComponent(PickupRoot);
    Placeholder = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeathBagPlaceholder"));
    Placeholder->SetupAttachment(PickupRoot);
    Prompt = CreateDefaultSubobject<UDWInteractionPromptComponent>(TEXT("InteractionPrompt"));
    Prompt->SetupAttachment(PickupRoot);
    Prompt->bPromptEnabled = false;
    Prompt->GenericPromptTitle = FText::FromString(TEXT("遗失背包"));
    Prompt->GenericEnglishPromptTitle = FText::FromString(TEXT("Lost Backpack"));
    Prompt->GenericPromptAction = FText::FromString(TEXT("拾取"));
    Prompt->GenericEnglishAction = FText::FromString(TEXT("Pick up"));
    Prompt->WorldOffset = FVector(0, 0, 75.);
    PickupPresentation = CreateDefaultSubobject<UDWPickupPresentationComponent>(TEXT("PickupPresentation"));
    PickupPresentation->bBudgetIdlePickupOutline = true;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(Shape.Succeeded())Placeholder->SetStaticMesh(Shape.Object);
    Placeholder->SetRelativeScale3D(FVector(.35,.35,.25));Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Placeholder->SetGenerateOverlapEvents(false);Placeholder->SetCanEverAffectNavigation(false);SetActorEnableCollision(false);
}
void ADWCombatDeathBag::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* W=GetWorld())if(auto* V=W->GetSubsystem<UDWLootBeaconSubsystem>())V->WithdrawBeacon(this);
    Super::EndPlay(Reason);
}
bool ADWCombatDeathBag::IsPickupAvailable() const
{
    const auto* Recovery = UDWCombatRecoverySubsystem::Get(this);
    return !bPickupVisualPending && !PickupPresentation->IsCollectionActive() && !IsActorBeingDestroyed() &&
        Recovery && Recovery->IsPhysicalCurrent(*this);
}

bool ADWCombatDeathBag::IsPickupInReach(const AActor* User) const
{
    constexpr double ReachCm = 150.;
    return IsValid(User) && User->GetWorld() == GetWorld() &&
        FVector::DistSquared(User->GetActorLocation(), GetActorLocation()) <= FMath::Square(ReachCm);
}

bool ADWCombatDeathBag::TryCollect(ADWPlayerCharacter* Player)
{
    return IsValid(Player) && FDWCombatRecoveryTransaction::Claim(*this, *Player);
}

void ADWCombatDeathBag::PresentCommittedPickup()
{
    const TWeakObjectPtr<ADWCombatDeathBag> Self(this);
    Prompt->SetPromptEnabled(false);
    if (!Self.IsValid() || IsActorBeingDestroyed() || !bBound) return;
    bPickupVisualPending = false;
    if (!GetWorld()->GetFirstPlayerController() || !PickupPresentation->PlayCommittedPickupEffect())
        HandlePickupPresentationFinished();
}

void ADWCombatDeathBag::HandlePickupPresentationFinished()
{
    if (!bBound || !IsPickupAvailable())
    {
        Destroy();
        return;
    }
    if (auto* Beacons = GetWorld()->GetSubsystem<UDWLootBeaconSubsystem>()) Beacons->PublishDeathBag(this, Placeholder);
    if (!IsActorBeingDestroyed() && IsPickupAvailable()) Prompt->SetPromptEnabled(true);
}
