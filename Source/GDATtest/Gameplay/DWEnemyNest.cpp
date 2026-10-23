#include "DWEnemyNest.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWEnemyCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "UObject/ConstructorHelpers.h"

namespace DWEnemySpawnRules
{
    static float SafeFloat(float Value, float Minimum, float Maximum, float Fallback)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Fallback;
    }

    static bool UsableEnemyClass(const UClass* Class)
    {
        return IsValid(Class) && Class->IsChildOf(ADWEnemyCharacter::StaticClass()) &&
            !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
    }

    UClass* SelectWeightedClass(const TArray<FDWEnemySpawnEntry>& Entries,
        const TMap<UClass*, int32>& AliveCounts, double UnitSample)
    {
        struct FCandidate { UClass* Class = nullptr; double Weight = 0.; int32 Cap = 0; };
        TArray<FCandidate> Candidates;
        for (const FDWEnemySpawnEntry& Entry : Entries)
        {
            UClass* Class = Entry.EnemyClass.Get();
            if (!UsableEnemyClass(Class) || !FMath::IsFinite(Entry.Weight) || Entry.Weight <= 0.f) continue;
            FCandidate* Existing = Candidates.FindByPredicate([Class](const FCandidate& Value) { return Value.Class == Class; });
            if (!Existing)
            {
                FCandidate NewCandidate;
                NewCandidate.Class = Class;
                Candidates.Add(NewCandidate);
                Existing = &Candidates.Last();
            }
            Existing->Weight += static_cast<double>(Entry.Weight);
            // Duplicating a row must not bypass its type cap through a second unlimited row.
            if (Entry.MaxAliveOfType > 0)
                Existing->Cap = Existing->Cap > 0 ? FMath::Min(Existing->Cap, Entry.MaxAliveOfType) : Entry.MaxAliveOfType;
        }
        double TotalWeight = 0.;
        for (FCandidate& Candidate : Candidates)
        {
            const int32* Alive = AliveCounts.Find(Candidate.Class);
            if (Candidate.Cap > 0 && Alive && *Alive >= Candidate.Cap) Candidate.Weight = 0.;
            TotalWeight += Candidate.Weight;
        }
        if (!FMath::IsFinite(TotalWeight) || TotalWeight <= 0.) return nullptr;
        const double Draw = (FMath::IsFinite(UnitSample) ? FMath::Clamp(UnitSample, 0., 1.) : 0.) * TotalWeight;
        double Cumulative = 0.;
        UClass* LastEligible = nullptr;
        for (const FCandidate& Candidate : Candidates)
        {
            if (Candidate.Weight <= 0.) continue;
            LastEligible = Candidate.Class;
            Cumulative += Candidate.Weight;
            if (Draw < Cumulative) return Candidate.Class;
        }
        // An exact 1.0 sample and final floating point round-off select the last eligible row.
        return LastEligible;
    }

    float SampleInterval(bool bRandom, float FixedSeconds, float MinimumSeconds, float MaximumSeconds, float UnitSample)
    {
        const float Fixed = SafeFloat(FixedSeconds, 0.1f, 86400.f, 10.f);
        if (!bRandom) return Fixed;
        const float A = SafeFloat(MinimumSeconds, 0.1f, 86400.f, Fixed);
        const float B = SafeFloat(MaximumSeconds, 0.1f, 86400.f, Fixed);
        return FMath::Lerp(FMath::Min(A, B), FMath::Max(A, B), SafeFloat(UnitSample, 0.f, 1.f, 0.f));
    }

    FVector2D RadiusRange(bool bUseRange, float FixedRadius, float MinimumRadius, float MaximumRadius)
    {
        // Preserve the original at-least-150cm ring when no new mode is enabled.
        if (!bUseRange)
        {
            const float Fixed = SafeFloat(FixedRadius, 150.f, 1000000.f, 220.f);
            return FVector2D(Fixed, Fixed);
        }
        const float A = SafeFloat(MinimumRadius, 0.f, 1000000.f, 150.f);
        const float B = SafeFloat(MaximumRadius, 0.f, 1000000.f, 1500.f);
        return FVector2D(FMath::Min(A, B), FMath::Max(A, B));
    }
}

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
    AggroRange->SetSphereRadius(DWEnemySpawnRules::SafeFloat(AggroRadius, 1.f, 1000000.f, 1200.f));
    UpdateAmbientPresentation();
}

void ADWEnemyNest::BeginPlay()
{
    Super::BeginPlay();
    if (!bHealthRestoredFromSave) Health = FMath::Max(1.f, MaxHealth);
    UpdateAmbientPresentation();
    SampleNextSpawnInterval();
}

void ADWEnemyNest::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateAmbientPresentation();
    if (!IsAlive()) return;
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    SetPlayerInRange(IsValid(Player) && FVector::DistSquared(Player->GetActorLocation(), GetActorLocation()) <=
        FMath::Square(DWEnemySpawnRules::SafeFloat(AggroRadius, 1.f, 1000000.f, 1200.f)));
    if (!bPlayerInRange)
    {
        SpawnProgress = 0.f;
        return;
    }
    if (!FMath::IsFinite(SpawnProgress) || SpawnProgress < 0.f) SpawnProgress = 0.f;
    SpawnProgress += DWEnemySpawnRules::SafeFloat(DeltaSeconds, 0.f, 86400.f, 0.f);
    if (!bUseRandomSpawnInterval)
        CurrentSpawnInterval = DWEnemySpawnRules::SampleInterval(false, SpawnInterval, MinSpawnInterval, MaxSpawnInterval, 0.f);
    else
    {
        const float Lower = DWEnemySpawnRules::SampleInterval(true, SpawnInterval, MinSpawnInterval, MaxSpawnInterval, 0.f);
        const float Upper = DWEnemySpawnRules::SampleInterval(true, SpawnInterval, MinSpawnInterval, MaxSpawnInterval, 1.f);
        if (!FMath::IsFinite(CurrentSpawnInterval) || CurrentSpawnInterval < Lower || CurrentSpawnInterval > Upper)
            SampleNextSpawnInterval();
    }
    if (SpawnProgress >= CurrentSpawnInterval)
    {
        SpawnProgress = FMath::Fmod(SpawnProgress, CurrentSpawnInterval);
        SpawnEnemy();
        if (bUseRandomSpawnInterval) SampleNextSpawnInterval();
    }
}

ADWEnemyCharacter* ADWEnemyNest::SpawnEnemy()
{
    if (bSpawningEnemy || !IsAlive() || !GetWorld() || IsActorBeingDestroyed()) return nullptr;
    TGuardValue<bool> SpawnGuard(bSpawningEnemy, true);
    SpawnedEnemies.RemoveAll([](const TWeakObjectPtr<ADWEnemyCharacter>& Enemy) {
        return !Enemy.IsValid() || Enemy->IsActorBeingDestroyed() || !Enemy->IsAlive();
    });
    if (MaxAliveEnemies > 0 && SpawnedEnemies.Num() >= MaxAliveEnemies) return nullptr;
    if (!CanSpawnEnemy()) return nullptr;
    // Blueprint callbacks cannot bypass the life or hard total-cap checks.
    if (!IsAlive() || IsActorBeingDestroyed() || (MaxAliveEnemies > 0 && SpawnedEnemies.Num() >= MaxAliveEnemies)) return nullptr;
    TMap<UClass*, int32> AliveCounts;
    for (const TWeakObjectPtr<ADWEnemyCharacter>& Enemy : SpawnedEnemies)
        if (Enemy.IsValid() && Enemy->IsAlive() && !Enemy->IsActorBeingDestroyed()) ++AliveCounts.FindOrAdd(Enemy->GetClass());
    UClass* SelectedClass = WeightedEnemies.IsEmpty() ? EnemyClass.Get()
        : DWEnemySpawnRules::SelectWeightedClass(WeightedEnemies, AliveCounts, FMath::FRand());
    if (!DWEnemySpawnRules::UsableEnemyClass(SelectedClass)) return nullptr;
    const ADWEnemyCharacter* DefaultEnemy = SelectedClass->GetDefaultObject<ADWEnemyCharacter>();
    const float HalfHeight = DefaultEnemy ? DefaultEnemy->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 78.f;
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    const bool bStrictNavigation = bAmbientSpawner || bRequireNavigableSpawn;
    const FVector2D Radii = DWEnemySpawnRules::RadiusRange(bAmbientSpawner || bUseSpawnRadiusRange,
        SpawnRadius, MinSpawnRadius, MaxSpawnRadius);
    const float PlayerClearance = DWEnemySpawnRules::SafeFloat(MinimumPlayerSpawnDistance, 0.f, 1000000.f, 0.f);
    const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (bAmbientSpawner && !IsValid(Player)) return nullptr;
    ANavigationData* NavData = nullptr;
    FNavLocation NavOrigin;
    if (bStrictNavigation)
    {
        if (!Nav || !DefaultEnemy) return nullptr;
        NavData = Nav->GetNavDataForProps(DefaultEnemy->GetNavAgentPropertiesRef(), GetActorLocation());
        if (!NavData || !Nav->ProjectPointToNavigation(GetActorLocation(), NavOrigin, FVector(150.f, 150.f, 1000.f), NavData)) return nullptr;
        if (bAmbientSpawner)
        {
            FNavLocation PlayerNavLocation;
            if (!Nav->ProjectPointToNavigation(Player->GetActorLocation(), PlayerNavLocation, FVector(150.f, 150.f, 1000.f), NavData)) return nullptr;
            FPathFindingQuery PlayerPath(this, *NavData, PlayerNavLocation.Location, NavOrigin.Location);
            PlayerPath.SetAllowPartialPaths(false);
            if (!Nav->TestPathSync(PlayerPath)) return nullptr;
        }
    }
    for (int32 Attempt = 0; Attempt < FMath::Clamp(SpawnAttemptCount, 1, 128); ++Attempt)
    {
        const float Angle = FMath::FRandRange(0.f, 2.f * PI);
        const float Radius = Radii.X == Radii.Y ? Radii.X : FMath::Sqrt(FMath::Lerp(FMath::Square(Radii.X), FMath::Square(Radii.Y), FMath::FRand()));
        FVector Location = GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius;
        FNavLocation NavLocation;
        if (bStrictNavigation)
        {
            // Never fall back to an unprojected/airborne point for scene spawning.
            const bool bFixedRing = FMath::IsNearlyEqual(Radii.X, Radii.Y);
            if (bFixedRing)
            {
                if (!Nav->ProjectPointToNavigation(Location, NavLocation, FVector(150.f, 150.f, 1000.f), NavData)) continue;
                FPathFindingQuery Query(this, *NavData, NavOrigin.Location, NavLocation.Location);
                Query.SetAllowPartialPaths(false);
                if (!Nav->TestPathSync(Query)) continue;
            }
            else if (!Nav->GetRandomReachablePointInRadius(NavOrigin.Location, FMath::Max(1.f, static_cast<float>(Radii.Y)), NavLocation, NavData)) continue;
            const double RadialDistance = FVector::Dist2D(GetActorLocation(), NavLocation.Location);
            const double ProjectionTolerance = bFixedRing ? 150. : 1.;
            if (RadialDistance < Radii.X - ProjectionTolerance || RadialDistance > Radii.Y + ProjectionTolerance) continue;
            FHitResult Ground;
            FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(DWAmbientSpawnGround), false, this);
            if (!GetWorld()->LineTraceSingleByObjectType(Ground, NavLocation.Location + FVector(0.f, 0.f, 50.f),
                NavLocation.Location - FVector(0.f, 0.f, 100.f), FCollisionObjectQueryParams(ECC_WorldStatic), GroundParams)) continue;
            const float WalkableZ = DefaultEnemy->GetCharacterMovement()->GetWalkableFloorZ();
            if (!Ground.bBlockingHit || Ground.ImpactNormal.Z < WalkableZ) continue;
            Location = Ground.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 5.f);
        }
        else if (Nav && Nav->ProjectPointToNavigation(Location, NavLocation, FVector(150.f, 150.f, 1000.f)))
            Location = NavLocation.Location + FVector(0.f, 0.f, HalfHeight + 5.f);
        else
        {
            FHitResult Ground;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(DWNestGround), false, this);
            const bool bGround = GetWorld()->LineTraceSingleByObjectType(Ground, Location + FVector(0.f, 0.f, 1000.f), Location - FVector(0.f, 0.f, 2000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params);
            if (bGround) Location = Ground.ImpactPoint + FVector(0.f, 0.f, HalfHeight + 5.f);
            else Location.Z += HalfHeight;
        }
        if (Location.ContainsNaN() || (IsValid(Player) && FVector::Dist2D(Location, Player->GetActorLocation()) < PlayerClearance)) continue;
        FActorSpawnParameters Params;
        Params.Owner = this;
        // Strict candidates have already passed navigation/ground/clearance checks; do not adjust them off that point.
        Params.SpawnCollisionHandlingOverride = bStrictNavigation ? ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding
            : ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
        if (ADWEnemyCharacter* Spawned = GetWorld()->SpawnActor<ADWEnemyCharacter>(SelectedClass, Location, FRotator::ZeroRotator, Params))
        {
            if (IsValid(Player) && FVector::Dist2D(Spawned->GetActorLocation(), Player->GetActorLocation()) < PlayerClearance)
            { Spawned->Destroy(); continue; }
            Spawned->SourceNest=this;
            SpawnedEnemies.Add(Spawned);
            OnEnemySpawned(Spawned);
            return Spawned;
        }
    }
    return nullptr;
}

void ADWEnemyNest::SampleNextSpawnInterval()
{
    CurrentSpawnInterval = DWEnemySpawnRules::SampleInterval(bUseRandomSpawnInterval, SpawnInterval,
        MinSpawnInterval, MaxSpawnInterval, bUseRandomSpawnInterval ? FMath::FRand() : 0.f);
}

void ADWEnemyNest::UpdateAmbientPresentation()
{
    if (!NestMesh) return;
    if (bAmbientSpawner && !bAmbientPresentationApplied)
    {
        bOriginalNestVisible = NestMesh->IsVisible();
        bOriginalNestHiddenInGame = NestMesh->bHiddenInGame;
        OriginalNestCollision = NestMesh->GetCollisionEnabled();
        bOriginalCanBeDamaged = CanBeDamaged();
        NestMesh->SetVisibility(false);
        NestMesh->SetHiddenInGame(true);
        NestMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SetCanBeDamaged(false);
        bAmbientPresentationApplied = true;
    }
    else if (!bAmbientSpawner && bAmbientPresentationApplied)
    {
        NestMesh->SetVisibility(bOriginalNestVisible);
        NestMesh->SetHiddenInGame(bOriginalNestHiddenInGame);
        NestMesh->SetCollisionEnabled(OriginalNestCollision);
        SetCanBeDamaged(bOriginalCanBeDamaged);
        bAmbientPresentationApplied = false;
    }
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
    if (auto* Authority = UDWCombatAuthoritySubsystem::Get(this))
        return Authority->SubmitLegacyDamage(this, DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    return 0.f;
}

void ADWEnemyNest::SetHealthForLoad(float NewHealth)
{
    if (bCombatDeathTransition || !FMath::IsFinite(NewHealth)) return;
    if (auto* Authority = UDWCombatAuthoritySubsystem::Get(this)) Authority->NotifyActorRestored(this);
    bCombatDeathEffectsPending = false;
    bHealthRestoredFromSave = true;
    SampleNextSpawnInterval();
    Health = FMath::Clamp(NewHealth, 0.f, FMath::Max(1.f, MaxHealth));
    if (Health <= 0.f) DeactivateNest(true);
    else
    {
        SetActorHiddenInGame(false);
        SetActorEnableCollision(true);
        SetActorTickEnabled(true);
        UpdateAmbientPresentation();
    }
}

void ADWEnemyNest::CommitCombatDeathFacts()
{
    if (bCombatDeathEffectsPending) return;
    Health = 0.f;
    bCombatDeathRangeNotice = bPlayerInRange;
    bPlayerInRange = false;
    SpawnProgress = 0.f;
    bCombatDeathEffectsPending = true;
}

void ADWEnemyNest::PublishCombatDeathEffects(bool bFromSave)
{
    const TWeakObjectPtr<UWorld> DeathWorld(GetWorld());
    const bool bHadBegunPlay=HasActorBegunPlay();
    const auto CanContinue=[this,DeathWorld,bHadBegunPlay]()
    {
        return IsValid(this)&&!IsActorBeingDestroyed()&&DeathWorld.IsValid()&&GetWorld()==DeathWorld.Get()
            &&!DeathWorld->bIsTearingDown&&(!bHadBegunPlay||HasActorBegunPlay());
    };
    if(!CanContinue())return;
    if (!bCombatDeathEffectsPending || bCombatDeathTransition || IsActorBeingDestroyed()) return;
    bCombatDeathEffectsPending = false;
    TGuardValue<bool> DeathGuard(bCombatDeathTransition, true);
    if (!bFromSave && bCombatDeathRangeNotice) OnPlayerRangeChanged(false);
    if (!CanContinue()) return;
    SetActorEnableCollision(false);
    if (!CanContinue()) return;
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
    if (!bFromSave) OnNestDestroyed();
}

void ADWEnemyNest::DeactivateNest(bool bFromSave)
{
    if (bCombatDeathTransition) return;
    CommitCombatDeathFacts();
    if (auto* Authority = UDWCombatAuthoritySubsystem::Get(this)) Authority->NotifyActorDeath(this);
    PublishCombatDeathEffects(bFromSave);
}
