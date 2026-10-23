#include "DWCombatMine.h"
#include "DWCombatMineSubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ADWCombatMine::ADWCombatMine()
{
    PrimaryActorTick.bCanEverTick=false;
    SetActorEnableCollision(false);SetActorHiddenInGame(true); // Both regular and restore staging start inert.
    MineRoot=CreateDefaultSubobject<USceneComponent>(TEXT("MineRoot"));SetRootComponent(MineRoot);
    Placeholder=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Placeholder"));Placeholder->SetupAttachment(MineRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if(Mesh.Succeeded())Placeholder->SetStaticMesh(Mesh.Object);
    Placeholder->SetRelativeScale3D(FVector(.24,.24,.08)); // 24 cm diameter / 8 cm height visual only.
    Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);Placeholder->SetGenerateOverlapEvents(false);
    Placeholder->SetCanEverAffectNavigation(false);SetCanBeDamaged(false);
}
bool ADWCombatMine::TryTrigger()
{auto* M=UDWCombatMineSubsystem::Get(this);return M&&M->TryTrigger(*this);}
void ADWCombatMine::BeginPlay()
{
    // Commit the actual release before Super can invoke component/engine BeginPlay callbacks.
    if(auto* M=UDWCombatMineSubsystem::Get(this))M->PhysicalBegan(*this);
    Super::BeginPlay();
}
void ADWCombatMine::EndPlay(EEndPlayReason::Type Reason)
{if(auto* M=UDWCombatMineSubsystem::Get(this))M->PhysicalEnded(*this);Super::EndPlay(Reason);}
