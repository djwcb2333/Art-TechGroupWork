#include "DWDashTrailActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
ADWDashTrailActor::ADWDashTrailActor()
{
    PrimaryActorTick.bCanEverTick=true;TrailMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrailMesh"));SetRootComponent(TrailMesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));if(Shape.Succeeded())TrailMesh->SetStaticMesh(Shape.Object);
    TrailMesh->SetRelativeScale3D(FVector(.45f,.3f,.65f));TrailMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);TrailMesh->SetCanEverAffectNavigation(false);TrailMesh->SetCastShadow(false);
}
void ADWDashTrailActor::BeginPlay(){Super::BeginPlay();InitialScale=TrailMesh->GetRelativeScale3D();SetLifeSpan(FMath::Max(.01f,Lifetime));}
void ADWDashTrailActor::Tick(float Dt){Super::Tick(Dt);Elapsed+=Dt;TrailMesh->SetRelativeScale3D(InitialScale*FMath::Max(.001f,1.f-Elapsed/FMath::Max(.01f,Lifetime)));}
