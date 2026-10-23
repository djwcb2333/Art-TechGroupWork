#include "DWCombatCorePickup.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatCorePickupTransaction.h"
#include "DWPlayerCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

ADWCombatCorePickup::ADWCombatCorePickup()
{
    PrimaryActorTick.bCanEverTick=true;
    PickupRange=CreateDefaultSubobject<USphereComponent>(TEXT("PickupRange"));SetRootComponent(PickupRange);
    PickupRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);PickupRange->SetGenerateOverlapEvents(false);
    PickupRange->SetCanEverAffectNavigation(false);
    Placeholder=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Placeholder"));Placeholder->SetupAttachment(PickupRange);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(Sphere.Succeeded())Placeholder->SetStaticMesh(Sphere.Object);
    Placeholder->SetRelativeScale3D(FVector(.25));Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Placeholder->SetCanEverAffectNavigation(false);
    // No physics impulse, timed lifespan, custom material or replacement of existing art.
}
void ADWCombatCorePickup::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* Drops=UDWCombatCoreDropSubsystem::Get(this);
    if(!Drops||!Drops->IsPhysicalCurrent(*this)||!HasActorBegunPlay()||GetWorld()->IsPaused()||!FMath::IsFinite(Dt)||Dt<=0.f)return;
    const TWeakObjectPtr<ADWCombatCorePickup> Self(this);const TWeakObjectPtr<UWorld> World(GetWorld());
    for(TActorIterator<ADWPlayerCharacter> It(GetWorld());It;++It)
    {
        auto* Player=*It;
        if(FVector::DistSquared(Player->GetActorLocation(),GetActorLocation())>FMath::Square(PickupRadiusCm))
        {LastFullInventoryRevision.Remove(Player);continue;}
        if(TryCollect(Player))return;
        if(!Self.IsValid()||IsActorBeingDestroyed()||!World.IsValid()||GetWorld()!=World.Get()||World->bIsTearingDown||!HasActorBegunPlay()||bClaimed)return;
    }
}
bool ADWCombatCorePickup::TryCollect(ADWPlayerCharacter* Player)
{return IsValid(Player)&&FDWCombatCorePickupTransaction::Commit(*this,*Player);}
void ADWCombatCorePickup::EndPlay(EEndPlayReason::Type Reason)
{
    bBound=false;
    if(auto* Drops=UDWCombatCoreDropSubsystem::Get(this))Drops->PhysicalEnded(*this);
    Super::EndPlay(Reason);
}
