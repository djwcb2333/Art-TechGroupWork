#include "DWAlcoholProjectile.h"
#include "DWAlcoholArea.h"
#include "DWAudioLibrary.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    // SpawnSystemAtLocation gives these components a world owner instead of the bottle.
    // Weak timer captures let finite particles finish after the gameplay actor is destroyed.
    void FinishBottleVFX(UNiagaraComponent* Component, float FadeOutTime)
    {
        if (!IsValid(Component)) return;
        UWorld* World = Component->GetWorld();
        Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        Component->SetAutoDestroy(true);
        Component->Deactivate();
        if (!IsValid(Component)) return;
        // Never enqueue cleanup work into a world that is already shutting down.
        if (!World || World->bIsTearingDown || FadeOutTime <= 0.f)
        {
            Component->DestroyComponent();
            return;
        }
        const TWeakObjectPtr<UNiagaraComponent> WeakComponent(Component);
        FTimerHandle CleanupTimer;
        World->GetTimerManager().SetTimer(CleanupTimer, FTimerDelegate::CreateLambda([WeakComponent]()
        {
            if (WeakComponent.IsValid()) WeakComponent->DestroyComponent();
        }), FMath::Max(0.01f, FadeOutTime), false);
    }
}

ADWAlcoholProjectile::ADWAlcoholProjectile()
{
    PrimaryActorTick.bCanEverTick = false;
    CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
    SetRootComponent(CollisionSphere);
    CollisionSphere->InitSphereRadius(10.f);
    CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CollisionSphere->SetCollisionObjectType(ECC_WorldDynamic);
    CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
    CollisionSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    CollisionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    CollisionSphere->SetGenerateOverlapEvents(false);
    CollisionSphere->SetNotifyRigidBodyCollision(true);
    CollisionSphere->SetCanEverAffectNavigation(false);
    CollisionSphere->OnComponentHit.AddDynamic(this, &ADWAlcoholProjectile::OnProjectileHit);
    BottleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleMesh"));
    BottleMesh->SetupAttachment(RootComponent);
    BottleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BottleMesh->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (Mesh.Succeeded()) BottleMesh->SetStaticMesh(Mesh.Object);
    BottleMesh->SetRelativeScale3D(FVector(0.16f, 0.16f, 0.32f));
    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    ProjectileMovement->SetUpdatedComponent(CollisionSphere);
    ProjectileMovement->InitialSpeed = 0.f;
    ProjectileMovement->MaxSpeed = 0.f;
    ProjectileMovement->ProjectileGravityScale = 1.f;
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->bShouldBounce = false;
    AreaClass = ADWAlcoholArea::StaticClass();
}

void ADWAlcoholProjectile::BeginPlay()
{
    Super::BeginPlay();
    if (GetOwner()) CollisionSphere->IgnoreActorWhenMoving(GetOwner(), true);
    if (GetInstigator()) CollisionSphere->IgnoreActorWhenMoving(GetInstigator(), true);
    if (TrailVFX)
    {
        ActiveTrailVFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, TrailVFX, GetActorLocation(), GetActorRotation(), TrailVFXScale, true, false, ENCPoolMethod::None, false);
        if (IsValid(ActiveTrailVFX))
        {
            ActiveTrailVFX->SetAbsolute(false, false, true);
            ActiveTrailVFX->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
            ActiveTrailVFX->SetRelativeLocationAndRotation(TrailVFXOffset, TrailVFXRotation);
            ActiveTrailVFX->SetWorldScale3D(TrailVFXScale);
            ActiveTrailVFX->Activate(true);
            if (TrailEmissionDuration > 0.f)
                GetWorldTimerManager().SetTimer(TrailEmissionTimer, this, &ADWAlcoholProjectile::FinishTrail, FMath::Max(0.01f, TrailEmissionDuration), false);
        }
    }
    GetWorldTimerManager().SetTimer(FlightTimer, this, &ADWAlcoholProjectile::ExpireInFlight, FMath::Max(0.1f, MaxFlightLifetime), false);
}

void ADWAlcoholProjectile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    GetWorldTimerManager().ClearTimer(FlightTimer);
    GetWorldTimerManager().ClearTimer(TrailEmissionTimer);
    if (EndPlayReason == EEndPlayReason::Destroyed) FinishTrail();
    else if (IsValid(ActiveTrailVFX)) ActiveTrailVFX->DestroyComponent();
    ActiveTrailVFX = nullptr;
    Super::EndPlay(EndPlayReason);
}

void ADWAlcoholProjectile::FinishTrail()
{
    GetWorldTimerManager().ClearTimer(TrailEmissionTimer);
    FinishBottleVFX(ActiveTrailVFX, FMath::Max(0.f, TrailFadeOutTime));
    ActiveTrailVFX = nullptr;
}

void ADWAlcoholProjectile::LaunchAtTarget(const FVector& Target)
{
    if (!GetWorld() || bHasBurst) return;
    FVector Velocity = CalculateLaunchVelocity(Target);
    if (Velocity.ContainsNaN()) Velocity = CalculateLaunchVelocity_Implementation(Target);
    ProjectileMovement->Velocity = Velocity;
    ProjectileMovement->Activate(true);
    OnProjectileLaunched(Target, Velocity);
}

FVector ADWAlcoholProjectile::CalculateLaunchVelocity_Implementation(const FVector& Target) const
{
    if (!GetWorld()) return FVector::ZeroVector;
    FVector Delta = Target - GetActorLocation();
    const float Distance2D = Delta.Size2D();
    if (Distance2D > FMath::Max(1.f, MaxThrowDistance))
    {
        const float Ratio = FMath::Max(1.f, MaxThrowDistance) / Distance2D;
        Delta.X *= Ratio; Delta.Y *= Ratio;
    }
    const float Time = FMath::Max(0.05f, FlightTime);
    FVector Velocity = Delta / Time;
    Velocity.Z -= 0.5f * GetWorld()->GetGravityZ() * ProjectileMovement->ProjectileGravityScale * Time;
    return Velocity;
}

void ADWAlcoholProjectile::OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit)
{
    if (OtherActor == GetOwner() || OtherActor == GetInstigator()) return;
    Burst(Hit.ImpactPoint, Hit.ImpactNormal);
}

void ADWAlcoholProjectile::ExpireInFlight()
{
    Burst(GetActorLocation());
}

void ADWAlcoholProjectile::Burst(const FVector& Location, const FVector& ImpactNormal)
{
    if (bHasBurst || !GetWorld() || GetWorld()->bIsTearingDown) return;
    bHasBurst = true;
    GetWorldTimerManager().ClearTimer(FlightTimer);
    ProjectileMovement->StopMovementImmediately();
    SetActorEnableCollision(false);
    FinishTrail();
    FVector AreaLocation = Location;
    FHitResult Ground;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWAlcoholGround), false, this);
    Params.AddIgnoredActor(GetOwner());
    if (GetWorld()->LineTraceSingleByObjectType(Ground, Location + FVector(0.f, 0.f, 100.f), Location - FVector(0.f, 0.f, 3000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
        AreaLocation = Ground.ImpactPoint + FVector(0.f, 0.f, 2.f);
    // Keep the ignition at the actual collision (including walls). The ground trace
    // only chooses the damaging pool position; it must not move the collision flash.
    if (ImpactVFX)
    {
        const FQuat SurfaceRotation = bAlignImpactToSurface && !ImpactNormal.IsNearlyZero()
            ? FRotationMatrix::MakeFromZ(ImpactNormal.GetSafeNormal()).ToQuat() : FQuat::Identity;
        UNiagaraComponent* ImpactComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, ImpactVFX,
            Location + SurfaceRotation.RotateVector(ImpactVFXOffset),
            (SurfaceRotation * ImpactVFXRotation.Quaternion()).Rotator(), ImpactVFXScale, true, true, ENCPoolMethod::None, false);
        if (IsValid(ImpactComponent))
        {
            const TWeakObjectPtr<UNiagaraComponent> WeakImpact(ImpactComponent);
            const float TailTime = FMath::Max(0.f, ImpactFadeOutTime);
            FTimerHandle ImpactEmissionTimer;
            GetWorldTimerManager().SetTimer(ImpactEmissionTimer, FTimerDelegate::CreateLambda([WeakImpact, TailTime]()
            {
                if (WeakImpact.IsValid()) FinishBottleVFX(WeakImpact.Get(), TailTime);
            }), FMath::Max(0.01f, ImpactEmissionDuration), false);
        }
    }
    if (ImpactSound)
    {
        const float Volume = FMath::IsFinite(ImpactSoundVolume) ? FMath::Clamp(ImpactSoundVolume, 0.f, 4.f) : 0.f;
        const float Pitch = FMath::IsFinite(ImpactSoundPitch) ? FMath::Clamp(ImpactSoundPitch, 0.125f, 4.f) : 1.f;
        UDWAudioLibrary::PlayWorldSound(this, ImpactSound, Location, Volume, Pitch, ImpactSoundAttenuation);
    }
    ADWAlcoholArea* SpawnedArea = nullptr;
    if (AreaClass)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.Owner = GetOwner();
        SpawnParams.Instigator = GetInstigator();
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnedArea = GetWorld()->SpawnActor<ADWAlcoholArea>(AreaClass, AreaLocation, FRotator::ZeroRotator, SpawnParams);
    }
    OnProjectileBurst(SpawnedArea, AreaLocation);
    Destroy();
}
