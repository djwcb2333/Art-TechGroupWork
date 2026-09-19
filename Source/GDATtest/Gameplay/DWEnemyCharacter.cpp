#include "DWEnemyCharacter.h"
#include "DWDamageVolumeLibrary.h"
#include "DWAudioLibrary.h"
#include "DWUserSettings.h"
#include "DWPlayerCharacter.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADWEnemyCharacter::ADWEnemyCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    GetCapsuleComponent()->InitCapsuleSize(38.f, 78.f);
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    AIControllerClass = AAIController::StaticClass();
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
    GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;
    GetCharacterMovement()->bUseRVOAvoidance = true;
    PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
    PlaceholderBody->SetupAttachment(RootComponent);
    PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (BodyMeshFinder.Succeeded()) PlaceholderBody->SetStaticMesh(BodyMeshFinder.Object);
    PlaceholderBody->SetRelativeScale3D(FVector(0.75f, 0.75f, 1.35f));
    DetectionRange = CreateDefaultSubobject<USphereComponent>(TEXT("DetectionRange"));
    DetectionRange->SetupAttachment(RootComponent);
    DetectionRange->SetUsingAbsoluteScale(true);
    DetectionRange->SetSphereRadius(DetectionRadius);
    DetectionRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DetectionRange->SetGenerateOverlapEvents(false);
    DetectionRange->SetHiddenInGame(true);
    DetectionRange->SetVisibility(true);
    DetectionRange->ShapeColor = FColor::Red;
    Tags.AddUnique(TEXT("DoughWorldEnemy"));
}

void ADWEnemyCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    DetectionRange->SetSphereRadius(FMath::Max(1.f, DetectionRadius));
    GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.f, MoveSpeed);
}

void ADWEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (!bHealthRestoredFromSave) Health = FMath::Max(1.f, MaxHealth);
    HomeLocation = GetActorLocation();
    NextPatrolTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(0.f, FMath::Max(0.f, PatrolWaitTime));
}

bool ADWEnemyCharacter::HasSightTo(const AActor* Target) const
{
    if (!Target || !GetWorld()) return false;
    if (!bRequireLineOfSight) return true;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWEnemySight), false, this);
    const FVector Start = GetActorLocation() + FVector(0.f, 0.f, 30.f);
    const FVector End = Target->GetActorLocation() + FVector(0.f, 0.f, 25.f);
    const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    return !bBlocked || Hit.GetActor() == Target;
}

bool ADWEnemyCharacter::CanDetectTarget_Implementation(AActor* Target) const
{
    return IsValid(Target) && FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) <= DetectionRadius && HasSightTo(Target);
}

bool ADWEnemyCharacter::CanAttackTarget_Implementation(AActor* Target) const
{
    if (!IsValid(Target)) return false;
    return UDWDamageVolumeLibrary::IsTargetInDamageSphere(Target, GetActorLocation(), AttackRange) && HasSightTo(Target);
}

float ADWEnemyCharacter::GetAttackDamage_Implementation(AActor* Target) const
{
    return FMath::Max(0.f, AttackDamage);
}

void ADWEnemyCharacter::SetAIState(EDWEnemyState NewState)
{
    if (State == NewState) return;
    const EDWEnemyState PreviousState = State;
    State = NewState;
    OnStateChanged(PreviousState, NewState);
}

void ADWEnemyCharacter::ChoosePatrolGoal()
{
    FNavLocation NavPoint;
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    if (Nav && Nav->GetRandomReachablePointInRadius(HomeLocation, FMath::Max(0.f, PatrolRadius), NavPoint))
        PatrolGoal = NavPoint.Location;
    else
    {
        const float Angle = FMath::FRandRange(0.f, 2.f * PI);
        const float Distance = FMath::FRandRange(0.25f, 1.f) * FMath::Max(0.f, PatrolRadius);
        PatrolGoal = HomeLocation + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
    }
    bHasPatrolGoal = true;
}

void ADWEnemyCharacter::StopAIMovement()
{
    if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
    GetCharacterMovement()->StopMovementImmediately();
}

void ADWEnemyCharacter::MoveToward(const FVector& Goal, float DeltaSeconds)
{
    AAIController* AI = Cast<AAIController>(GetController());
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation OnNav;
    const bool bHasNav = Nav && Nav->ProjectPointToNavigation(GetActorLocation(), OnNav, FVector(100.f, 100.f, 250.f));
    if (AI && bHasNav)
    {
        if (GetWorld()->GetTimeSeconds() >= NextPathRequestTime)
        {
            AI->MoveToLocation(Goal, 35.f, true, true, true, false, nullptr, true);
            NextPathRequestTime = GetWorld()->GetTimeSeconds() + 0.35f;
        }
    }
    else
    {
        // Movement still uses CharacterMovement sweeps when no nav data is present.
        AddMovementInput((Goal - GetActorLocation()).GetSafeNormal2D(), 1.f);
    }
}

void ADWEnemyCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsAlive()) return;
    UpdateSlowEffects();
    const float Now = GetWorld()->GetTimeSeconds();
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (const ADWPlayerCharacter* DWPlayer = Cast<ADWPlayerCharacter>(Player))
        if (DWPlayer->IsDead()) Player = nullptr;
    if (Player && !Player->IsActorBeingDestroyed())
    {
        const float Distance = FVector::Dist2D(GetActorLocation(), Player->GetActorLocation());
        if (!TargetPawn.IsValid() && CanDetectTarget(Player)) TargetPawn = Player;
        else if (TargetPawn.IsValid() && Distance > FMath::Max(DetectionRadius, LoseTargetRadius)) TargetPawn.Reset();
    }
    else TargetPawn.Reset();

    if (APawn* Target = TargetPawn.Get())
    {
        const FVector Delta = Target->GetActorLocation() - GetActorLocation();
        if (CanAttackTarget(Target))
        {
            SetAIState(EDWEnemyState::Attack);
            StopAIMovement();
            if (!Delta.IsNearlyZero()) SetActorRotation(FRotator(0.f, Delta.Rotation().Yaw, 0.f));
            if (Now >= NextAttackTime)
            {
                NextAttackTime = Now + FMath::Max(0.05f, AttackInterval);
                const float AppliedDamage = UGameplayStatics::ApplyDamage(Target, FMath::Max(0.f, GetAttackDamage(Target)), GetController(), this, UDamageType::StaticClass());
                if (AttackSound) UDWAudioLibrary::PlayWorldSound(this, AttackSound, GetActorLocation());
                OnEnemyAttack(Target);
                OnAttackResolved(Target, AppliedDamage);
            }
        }
        else
        {
            SetAIState(EDWEnemyState::Chase);
            MoveToward(Target->GetActorLocation(), DeltaSeconds);
        }
        return;
    }

    SetAIState(EDWEnemyState::Patrol);
    if (!bHasPatrolGoal && Now >= NextPatrolTime) ChoosePatrolGoal();
    if (bHasPatrolGoal)
    {
        if (FVector::Dist2D(GetActorLocation(), PatrolGoal) < 80.f)
        {
            StopAIMovement();
            bHasPatrolGoal = false;
            NextPatrolTime = Now + FMath::Max(0.f, PatrolWaitTime);
        }
        else MoveToward(PatrolGoal, DeltaSeconds);
    }
}

void ADWEnemyCharacter::ApplySlow(float Percent, float Duration)
{
    if (!IsAlive() || Duration <= 0.f) return;
    const float Clamped = FMath::Clamp(Percent, 0.f, 100.f);
    const float Until = GetWorld()->GetTimeSeconds() + Duration;
    // Coalesce identical strengths so a long-lived pool does not grow this array.
    for (FVector2D& Effect : SlowEffects)
    {
        if (FMath::IsNearlyEqual(static_cast<float>(Effect.X), Clamped))
        {
            Effect.Y = FMath::Max(Effect.Y, static_cast<double>(Until));
            UpdateSlowEffects();
            return;
        }
    }
    SlowEffects.Emplace(Clamped, Until);
    UpdateSlowEffects();
}

void ADWEnemyCharacter::UpdateSlowEffects()
{
    const float Now = GetWorld()->GetTimeSeconds();
    SlowEffects.RemoveAll([Now](const FVector2D& Effect) { return Effect.Y <= Now; });
    float Strongest = 0.f;
    for (const FVector2D& Effect : SlowEffects) Strongest = FMath::Max(Strongest, static_cast<float>(Effect.X));
    GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.f, MoveSpeed) * (1.f - Strongest / 100.f);
}

float ADWEnemyCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    if (!IsAlive() || DamageAmount <= 0.f) return 0.f;
    Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    const float Applied = FMath::Min(Health, DamageAmount);
    Health -= Applied;
    if (EventInstigator && EventInstigator->IsPlayerController()) TargetPawn = EventInstigator->GetPawn();
    if (Health <= 0.f) Die();
    return Applied;
}

void ADWEnemyCharacter::SetHealthForLoad(float NewHealth)
{
    bHealthRestoredFromSave = true;
    Health = FMath::Clamp(NewHealth, 0.f, FMath::Max(1.f, MaxHealth));
    if (Health <= 0.f) Die(true);
}

void ADWEnemyCharacter::Die(bool bFromSave)
{
    if (State == EDWEnemyState::Dead) return;
    // Load restores presentation silently; ordinary deaths notify the state graph.
    if (bFromSave) State = EDWEnemyState::Dead;
    else SetAIState(EDWEnemyState::Dead);
    Health = 0.f;
    StopAIMovement();
    GetCharacterMovement()->DisableMovement();
    SetActorEnableCollision(false);
    TargetPawn.Reset();
    if (!bFromSave)
    {
        if (DeathSound) UDWAudioLibrary::PlayWorldSound(this, DeathSound, GetActorLocation());
        OnEnemyDied();
    }
    if (bFromSave && !PersistentId.IsNone()) FinishPersistentDeath();
    else if (PersistentId.IsNone()) SetLifeSpan(FMath::Max(0.1f, DeathDestroyDelay));
    else GetWorldTimerManager().SetTimer(DeathCleanupTimer, this, &ADWEnemyCharacter::FinishPersistentDeath, FMath::Max(0.1f, DeathDestroyDelay), false);
}

void ADWEnemyCharacter::FinishPersistentDeath()
{
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
    // Authored IDs remain queryable by save capture after the death visual expires.
}
