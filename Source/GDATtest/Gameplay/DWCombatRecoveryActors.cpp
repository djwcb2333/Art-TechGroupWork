#include "DWCombatRecoveryActors.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWCombatRecoveryTransaction.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ADWCombatSafePoint::ADWCombatSafePoint()
{PrimaryActorTick.bCanEverTick=false;Destination=CreateDefaultSubobject<USceneComponent>(TEXT("RespawnCapsuleCentre"));SetRootComponent(Destination);}
bool ADWCombatSafePoint::ActivateForPlayer(ADWPlayerCharacter* P)
{auto* R=UDWCombatRecoverySubsystem::Get(this);return IsValid(P)&&R&&R->ActivateSafePoint(*P,*this);}
ADWCombatDeathBag::ADWCombatDeathBag()
{
    PrimaryActorTick.bCanEverTick=true;Placeholder=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DeathBagPlaceholder"));SetRootComponent(Placeholder);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(Shape.Succeeded())Placeholder->SetStaticMesh(Shape.Object);
    Placeholder->SetRelativeScale3D(FVector(.35,.35,.25));Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Placeholder->SetGenerateOverlapEvents(false);Placeholder->SetCanEverAffectNavigation(false);SetActorEnableCollision(false);
}
bool ADWCombatDeathBag::Touches(const ADWPlayerCharacter& P)const
{
    const auto* C=P.GetCapsuleComponent();if(!C||P.GetWorld()!=GetWorld())return false;
    const FVector Point=C->GetComponentTransform().InverseTransformPosition(GetActorLocation());
    const double Radius=C->GetUnscaledCapsuleRadius(),Segment=FMath::Max(0.,static_cast<double>(C->GetUnscaledCapsuleHalfHeight())-Radius);
    const double Z=FMath::Max(0.,FMath::Abs(Point.Z)-Segment);return Point.X*Point.X+Point.Y*Point.Y+Z*Z<=Radius*Radius;
}
bool ADWCombatDeathBag::TryCollect(ADWPlayerCharacter* P)
{return IsValid(P)&&FDWCombatRecoveryTransaction::Claim(*this,*P);}
void ADWCombatDeathBag::Tick(float Dt)
{
    Super::Tick(Dt);auto* R=UDWCombatRecoverySubsystem::Get(this);if(!R||!R->IsPhysicalCurrent(*this)||GetWorld()->IsPaused())return;
    auto* P=R->BoundPlayer.Get();if(!P||!P->IsCombatActionAllowed()||!Touches(*P)||!P->GetInventory())return;
    const int64 Revision=P->GetInventory()->GetInventoryRevision();if(LastAttemptInventoryRevision==Revision)return;
    LastAttemptInventoryRevision=Revision;TryCollect(P);
}
