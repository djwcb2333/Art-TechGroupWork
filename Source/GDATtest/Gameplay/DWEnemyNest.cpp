#include "DWEnemyNest.h"
#include "DWEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "UObject/ConstructorHelpers.h"

ADWEnemyNest::ADWEnemyNest()
{
    PrimaryActorTick.bCanEverTick = true;
    NestMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NestMesh"));
    SetRootComponent(NestMesh);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cone.Cone"));
    if (Mesh.Succeeded()) NestMesh->SetStaticMesh(Mesh.Object);
    NestMesh->SetRelativeScale3D(FVector(2.5f, 2.5f, 2.f));
    NestMesh->SetCollisionProfileName(TEXT("BlockAll"));
    AggroRange = CreateDefaultSubobject<USphereComponent>(TEXT("AggroRange"));
    AggroRange->SetupAttachment(RootComponent);
    AggroRange->SetUsingAbsoluteScale(true);
    AggroRange->SetSphereRadius(AggroRadius);
    AggroRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    AggroRange->SetGenerateOverlapEvents(false);
    AggroRange->SetHiddenInGame(true);
    AggroRange->SetVisibility(true);
    AggroRange->ShapeColor = FColor(230, 50, 100);
    EnemyClass = ADWEnemyCharacter::StaticClass();
    Tags.AddUnique(TEXT("DoughWorldNest"));
}

void ADWEnemyNest::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    AggroRange->SetSphereRadius(FMath::Max(1.f, AggroRadius));
}

void ADWEnemyNest::BeginPlay()
{
    Super::BeginPlay();
    if (!bHealthRestoredFromSave) Health = FMath::Max(1.f, MaxHealth);
}

void ADWEnemyNest::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsAlive()) return;
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    SetPlayerInRange(IsValid(Player) && FVector::DistSquared(Player->GetActorLocation(), GetActorLocation()) <= FMath::Square(FMath::Max(1.f, AggroRadius)));
    if (!bPlayerInRange)
    {
        SpawnProgress = 0.f;
        return;
    }
    SpawnProgress += DeltaSeconds;
    if (SpawnProgress >= FMath::Max(0.1f, SpawnInterval))
    {
        SpawnProgress = FMath::Fmod(SpawnProgress, FMath::Max(0.1f, SpawnInterval));
        SpawnEnemy();
    }
}

ADWEnemyCharacter* ADWEnemyNest::SpawnEnemy()
{
    if (bSpawningEnemy || !IsAlive() || !EnemyClass || !GetWorld()) return nullptr;
    TGuardValue<bool> SpawnGuard(bSpawningEnemy, true);
    SpawnedEnemies.RemoveAll([](const TWeakObjectPtr<ADWEnemyCharacter>& Enemy) { return !Enemy.IsValid() || !Enemy->IsAlive(); });
    if (MaxAliveEnemies > 0 && SpawnedEnemies.Num() >= MaxAliveEnemies) return nullptr;
    if (!CanSpawnEnemy()) return nullptr;
    const ADWEnemyCharacter* DefaultEnemy = EnemyClass->GetDefaultObject<ADWEnemyCharacter>();
    const float HalfHeight = DefaultEnemy ? DefaultEnemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 78.f;
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    for (int32 Attempt = 0; Attempt < 8; ++Attempt)
    {
        const float Angle = FMath::FRandRange(0.f, 2.f * PI);
        FVector Location = GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * FMath::Max(150.f, SpawnRadius);
        FNavLocation NavLocation;
        if (Nav && Nav->ProjectPointToNavigation(Location, NavLocation, FVector(150.f, 150.f, 1000.f)))
            Location = NavLocation.Location + FVector(0.f, 0.f, HalfHeight + 5.f);
        else
        {
            FHitResult Ground;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(DWNestGround), false, this);
            const bool bGround = GetWorld()->LineTraceSingleByObjectType(Ground, Location + FVector(0.f, 0.f, 1000.f), Location - FVector(0.f, 0.f, 2000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
            if (bGround) Location = Ground.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 5.f);
            else Location.Z += HalfHeight;
        }
        FActorSpawnParameters Params;
        Params.Owner = this;
        Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
        if (ADWEnemyCharacter* Spawned = GetWorld()->SpawnActor<ADWEnemyCharacter>(EnemyClass, Location, FRotator::ZeroRotator, Params))
        {
            Spawned->SourceNest=this;
            SpawnedEnemies.Add(Spawned);
            OnEnemySpawned(Spawned);
            return Spawned;
        }
    }
    return nullptr;
}

bool ADWEnemyNest::CanSpawnEnemy_Implementation() const
{
    return true;
}

void ADWEnemyNest::SetPlayerInRange(bool bNewInRange, bool bNotify)
{
    if (bPlayerInRange == bNewInRange) return;
    bPlayerInRange = bNewInRange;
    if (bNotify) OnPlayerRangeChanged(bPlayerInRange);
}

float ADWEnemyNest::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    if (!IsAlive() || DamageAmount <= 0.f) return 0.f;
    Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    const float Applied = FMath::Min(Health, DamageAmount);
    Health -= Applied;
    if (Health <= 0.f) DeactivateNest();
    return Applied;
}

void ADWEnemyNest::SetHealthForLoad(float NewHealth)
{
    bHealthRestoredFromSave = true;
    Health = FMath::Clamp(NewHealth, 0.f, FMath::Max(1.f, MaxHealth));
    if (Health <= 0.f) DeactivateNest(true);
    else
    {
        SetActorHiddenInGame(false);
        SetActorEnableCollision(true);
        SetActorTickEnabled(true);
    }
}

void ADWEnemyNest::DeactivateNest(bool bFromSave)
{
    Health = 0.f;
    SetPlayerInRange(false, !bFromSave);
    SpawnProgress = 0.f;
    SetActorEnableCollision(false);
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
    // Keep this placed actor so saves can preserve a destroyed nest by PersistentId.
    if (!bFromSave) OnNestDestroyed();
}
