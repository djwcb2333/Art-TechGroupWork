#include "DWAlcoholArea.h"
#include "DWDamageVolumeLibrary.h"
#include "DWAudioLibrary.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWPlayerCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ADWAlcoholArea::ADWAlcoholArea()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
    PrimaryActorTick.bTickEvenWhenPaused = false;
    DamageRange = CreateDefaultSubobject<USphereComponent>(TEXT("DamageRange"));
    SetRootComponent(DamageRange);
    DamageRange->SetSphereRadius(Radius);
    DamageRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DamageRange->SetGenerateOverlapEvents(false);
    DamageRange->SetHiddenInGame(true);
    DamageRange->SetVisibility(true);
    DamageRange->ShapeColor = FColor(115, 210, 95);
    DamageVolume = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DamageVolume"));
    DamageVolume->SetupAttachment(RootComponent);
    DamageVolume->SetAbsolute(false, false, true);
    DamageVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    DamageVolume->SetCollisionObjectType(ECC_WorldDynamic);
    DamageVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    DamageVolume->SetGenerateOverlapEvents(false);
    DamageVolume->SetHiddenInGame(true);
    DamageVolume->SetVisibility(false);
    DamageVolume->SetCastShadow(false);
    DamageVolume->SetCanEverAffectNavigation(false);
    PlaceholderPool = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderPool"));
    PlaceholderPool->SetupAttachment(RootComponent);
    PlaceholderPool->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PlaceholderPool->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if (Mesh.Succeeded())
    {
        PlaceholderPool->SetStaticMesh(Mesh.Object);
        DamageVolume->SetStaticMesh(Mesh.Object);
    }
    DamageVolume->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, VerticalTolerance / 50.f));
    PlaceholderPool->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, 0.045f));
    PlaceholderPool->SetRelativeLocation(FVector(0.f, 0.f, 4.f));
    GroundIndicator = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GroundIndicator"));
    GroundIndicator->SetupAttachment(RootComponent);
    GroundIndicator->SetAbsolute(false, false, true);
    GroundIndicator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    GroundIndicator->SetGenerateOverlapEvents(false);
    GroundIndicator->SetCastShadow(false);
    GroundIndicator->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (Plane.Succeeded()) GroundIndicator->SetStaticMesh(Plane.Object);
    GroundIndicator->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, 1.f));
    GroundIndicator->SetHiddenInGame(true);
    GroundIndicator->SetVisibility(false);
}

void ADWAlcoholArea::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    DamageRange->SetSphereRadius(FMath::Max(1.f, Radius));
    DamageVolume->SetWorldScale3D(FVector(FMath::Max(1.f, Radius) / 50.f, FMath::Max(1.f, Radius) / 50.f, FMath::Max(1.f, VerticalTolerance) / 50.f));
    PlaceholderPool->SetRelativeScale3D(FVector(FMath::Max(1.f, Radius) / 50.f, FMath::Max(1.f, Radius) / 50.f, 0.045f));
    FVector SurfaceLocation;
    FQuat SurfaceRotation;
    ResolveGroundSurface(SurfaceLocation, SurfaceRotation);
    SetupGroundIndicator(SurfaceLocation, SurfaceRotation);
}

void ADWAlcoholArea::ResolveGroundSurface(FVector& SurfaceLocation, FQuat& SurfaceRotation) const
{
    SurfaceLocation = GetActorLocation();
    SurfaceRotation = GetActorQuat();
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown) return;
    FHitResult Ground;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWAlcoholVFXGround), false, this);
    Params.AddIgnoredActor(GetOwner());
    Params.AddIgnoredActor(GetInstigator());
    if (World->LineTraceSingleByObjectType(Ground, GetActorLocation() + FVector(0.f, 0.f, 100.f),
        GetActorLocation() - FVector(0.f, 0.f, 3000.f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
    {
        SurfaceLocation = Ground.ImpactPoint;
        SurfaceRotation = FRotationMatrix::MakeFromZ(Ground.ImpactNormal.IsNearlyZero() ? FVector::UpVector : Ground.ImpactNormal.GetSafeNormal()).ToQuat();
    }
}

void ADWAlcoholArea::SetupGroundIndicator(const FVector& SurfaceLocation, const FQuat& SurfaceRotation)
{
    if (!GroundIndicator) return;
    const float IndicatorScale = FMath::Max(1.f, Radius) / 50.f;
    GroundIndicator->SetWorldLocationAndRotation(SurfaceLocation + SurfaceRotation.RotateVector(FVector(0.f, 0.f, IndicatorHeightOffset)), SurfaceRotation);
    GroundIndicator->SetWorldScale3D(FVector(IndicatorScale, IndicatorScale, 1.f));
    if (GroundIndicatorMaterial)
    {
        // Construction also runs when tuning Radius/color in the editor. Reuse the MID
        // until the source material changes instead of allocating one on every update.
        if (!IsValid(GroundIndicatorMID) || GroundIndicatorMIDSource != GroundIndicatorMaterial)
        {
            GroundIndicatorMID = UMaterialInstanceDynamic::Create(GroundIndicatorMaterial, this);
            GroundIndicatorMIDSource = GroundIndicatorMaterial;
        }
        if (GroundIndicatorMID)
        {
            GroundIndicatorMID->SetVectorParameterValue(TEXT("IndicatorColor"), IndicatorColor);
            GroundIndicatorMID->SetScalarParameterValue(TEXT("FillOpacity"), FMath::Clamp(IndicatorFillOpacity, 0.f, 1.f));
            GroundIndicatorMID->SetScalarParameterValue(TEXT("RingOpacity"), FMath::Clamp(IndicatorRingOpacity, 0.f, 1.f));
            GroundIndicatorMID->SetScalarParameterValue(TEXT("RingWidthRatio"), FMath::Clamp(IndicatorRingWidthRatio, 0.f, 0.5f));
            GroundIndicator->SetMaterial(0, GroundIndicatorMID);
        }
    }
    else
    {
        GroundIndicatorMID = nullptr;
        GroundIndicatorMIDSource = nullptr;
        GroundIndicator->SetMaterial(0, nullptr);
    }
    const bool bVisible = bShowGroundIndicator && GroundIndicatorMaterial && GroundIndicatorMID && GroundIndicator->GetStaticMesh();
    GroundIndicator->SetVisibility(bVisible);
    GroundIndicator->SetHiddenInGame(!bVisible);
}

void ADWAlcoholArea::BeginPlay()
{
    Super::BeginPlay();
    DamageVolume->SetWorldScale3D(FVector(FMath::Max(1.f, Radius) / 50.f, FMath::Max(1.f, Radius) / 50.f, FMath::Max(1.f, VerticalTolerance) / 50.f));
    const float ActiveDuration = FMath::IsFinite(Duration) ? FMath::Max(0.05f, Duration) : 0.05f;
    ExpirationTime = GetWorld()->GetTimeSeconds() + ActiveDuration;
    BurnAudioActiveDuration = ActiveDuration;
    BurnAudioFadeDuration = FMath::IsFinite(AreaSoundFadeOutSeconds)
        ? FMath::Clamp(AreaSoundFadeOutSeconds, 0.f, ActiveDuration) : FMath::Min(1.f, ActiveDuration);
    SetLifeSpan(ActiveDuration);
    // The indicator and fire share the same ground hit. Run again here because a
    // deferred spawn can change Radius or the material before gameplay begins.
    FVector GroundLocation;
    FQuat GroundRotation;
    ResolveGroundSurface(GroundLocation, GroundRotation);
    SetupGroundIndicator(GroundLocation, GroundRotation);
    if (AreaVFX)
    {
        const FVector SurfaceLocation = bAlignAreaVFXToGround ? GroundLocation : GetActorLocation();
        const FQuat SurfaceRotation = bAlignAreaVFXToGround ? GroundRotation : GetActorQuat();
        // A real exposed float controls the authored system directly. Only use the
        // component radius fallback when that parameter is absent, preventing a
        // second Radius / ReferenceRadius multiplication inside the same effect.
        const bool bHasRadiusParameter = !AreaVFXRadiusParameter.IsNone() &&
            AreaVFX->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), AreaVFXRadiusParameter)) != INDEX_NONE;
        const float RadiusScale = bScaleAreaVFXToRadius && !bHasRadiusParameter
            ? FMath::Max(1.f, Radius) / FMath::Max(1.f, AreaVFXReferenceRadius) : 1.f;
        const FVector EffectScale = AreaVFXScale * FVector(RadiusScale, RadiusScale, 1.f);
        const FVector EffectLocation = SurfaceLocation + SurfaceRotation.RotateVector(AreaVFXOffset);
        const FRotator EffectRotation = (SurfaceRotation * AreaVFXRotation.Quaternion()).Rotator();
        // A world-owned component can leave a short particle tail after this actor's
        // original gameplay lifetime ends. No damage actor is kept alive for the VFX.
        ActiveAreaVFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, AreaVFX, EffectLocation, EffectRotation,
            EffectScale, true, false, ENCPoolMethod::None, false);
        if (IsValid(ActiveAreaVFX))
        {
            ActiveAreaVFX->SetAbsolute(false, false, true);
            ActiveAreaVFX->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepWorldTransform);
            ActiveAreaVFX->SetWorldLocationAndRotation(EffectLocation, EffectRotation);
            ActiveAreaVFX->SetWorldScale3D(EffectScale);
            if (bHasRadiusParameter) ActiveAreaVFX->SetVariableFloat(AreaVFXRadiusParameter, FMath::Max(1.f, Radius));
            if (!AreaVFXDurationParameter.IsNone()) ActiveAreaVFX->SetVariableFloat(AreaVFXDurationParameter, ActiveDuration);
            ActiveAreaVFX->Activate(true);
            if (bHidePlaceholderWhenVFXActive) PlaceholderPool->SetHiddenInGame(true);
        }
    }
    StartBurnAudio(GroundLocation);
    GetWorldTimerManager().SetTimer(DamageTimer, this, &ADWAlcoholArea::ApplyAreaTick, FMath::Max(0.01f, DamageInterval), true);
    OnAreaActivated();
}

void ADWAlcoholArea::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Revoke restart permission before Stop broadcasts OnAudioFinished, including early destruction.
    StopBurnAudio();
    GetWorldTimerManager().ClearTimer(DamageTimer);
    // The gameplay range ends with the damage actor, even while detached fire particles finish.
    if (GroundIndicator)
    {
        GroundIndicator->SetVisibility(false);
        GroundIndicator->SetHiddenInGame(true);
    }
    if (IsValid(ActiveAreaVFX))
    {
        UNiagaraComponent* Component = ActiveAreaVFX;
        UWorld* World = Component->GetWorld();
        Component->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        if (EndPlayReason == EEndPlayReason::Destroyed && World && !World->bIsTearingDown)
        {
            Component->SetAutoDestroy(true);
            Component->Deactivate();
            if (IsValid(Component))
            {
                if (World->bIsTearingDown || AreaVFXFadeOutTime <= 0.f) Component->DestroyComponent();
                else
                {
                    const TWeakObjectPtr<UNiagaraComponent> WeakComponent(Component);
                    FTimerHandle CleanupTimer;
                    World->GetTimerManager().SetTimer(CleanupTimer, FTimerDelegate::CreateLambda([WeakComponent]()
                    {
                        if (WeakComponent.IsValid()) WeakComponent->DestroyComponent();
                    }), FMath::Max(0.01f, AreaVFXFadeOutTime), false);
                }
            }
        }
        else Component->DestroyComponent();
        ActiveAreaVFX = nullptr;
    }
    Super::EndPlay(EndPlayReason);
}

void ADWAlcoholArea::StartBurnAudio(const FVector& SurfaceLocation)
{
    StopBurnAudio();
    UWorld* World = GetWorld();
    if (!AreaSound || !World || World->bIsTearingDown || World->GetTimeSeconds() >= ExpirationTime) return;
    const float Volume = FMath::IsFinite(AreaSoundVolume) ? FMath::Clamp(AreaSoundVolume, 0.f, 4.f) : 0.f;
    const float Pitch = FMath::IsFinite(AreaSoundPitch) ? FMath::Clamp(AreaSoundPitch, 0.125f, 4.f) : 1.f;
    const FVector RelativeLocation = RootComponent->GetComponentTransform().InverseTransformPosition(SurfaceLocation);
    BurnAudioComponent = UDWAudioLibrary::CreateWorldLoop(this, AreaSound, RootComponent, NAME_None,
        RelativeLocation, Volume, Pitch, AreaSoundAttenuation);
    if (!IsValid(BurnAudioComponent)) return;
    bBurnAudioActive = true;
    BurnAudioComponent->OnAudioFinished.AddUniqueDynamic(this, &ADWAlcoholArea::HandleBurnAudioFinished);
    BurnAudioComponent->SetVolumeMultiplier(Volume * GetBurnAudioGain());
    SetActorTickEnabled(true);
}

float ADWAlcoholArea::GetBurnAudioGain() const
{
    const UWorld* World = GetWorld();
    if (!bBurnAudioActive || !IsValid(BurnAudioComponent) || !World || World->bIsTearingDown ||
        !FMath::IsFinite(BurnAudioActiveDuration) || BurnAudioActiveDuration <= 0.f || !FMath::IsFinite(ExpirationTime)) return 0.f;
    const double Remaining = static_cast<double>(ExpirationTime) - World->GetTimeSeconds();
    if (!FMath::IsFinite(Remaining) || Remaining <= 0.0) return 0.f;
    if (BurnAudioFadeDuration <= 0.f) return 1.f;
    return static_cast<float>(FMath::Clamp(Remaining / static_cast<double>(BurnAudioFadeDuration), 0.0, 1.0));
}

void ADWAlcoholArea::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float Gain = GetBurnAudioGain();
    if (Gain <= 0.f)
    {
        StopBurnAudio();
        return;
    }
    const float Volume = FMath::IsFinite(AreaSoundVolume) ? FMath::Clamp(AreaSoundVolume, 0.f, 4.f) : 0.f;
    const float Pitch = FMath::IsFinite(AreaSoundPitch) ? FMath::Clamp(AreaSoundPitch, 0.125f, 4.f) : 1.f;
    // Audio-thread FadeOut uses a different clock. Set the envelope from game time
    // instead, so a paused game neither consumes the final fade nor restarts clips.
    BurnAudioComponent->SetVolumeMultiplier(Volume * Gain);
    BurnAudioComponent->SetPitchMultiplier(Pitch);
    if (bBurnAudioRestartPending)
    {
        bBurnAudioRestartPending = false;
        BurnAudioComponent->Play();
    }
}

void ADWAlcoholArea::HandleBurnAudioFinished()
{
    // Never recursively Play inside OnAudioFinished. The next game tick restarts
    // a short source only if the same damage deadline still permits the loop.
    if (GetBurnAudioGain() > 0.f) bBurnAudioRestartPending = true;
}

void ADWAlcoholArea::StopBurnAudio()
{
    bBurnAudioActive = false;
    bBurnAudioRestartPending = false;
    SetActorTickEnabled(false);
    if (IsValid(BurnAudioComponent))
    {
        BurnAudioComponent->OnAudioFinished.RemoveDynamic(this, &ADWAlcoholArea::HandleBurnAudioFinished);
        BurnAudioComponent->SetVolumeMultiplier(0.f);
        BurnAudioComponent->Stop();
        BurnAudioComponent->DestroyComponent();
    }
    BurnAudioComponent = nullptr;
}

void ADWAlcoholArea::ApplyAreaTick()
{
    if (bApplyingAreaTick || !GetWorld() || GetWorld()->GetTimeSeconds() >= ExpirationTime) return;
    TGuardValue<bool> TickGuard(bApplyingAreaTick, true);
    const TArray<AActor*> Targets = CollectDamageTargets();
    TSet<AActor*> Seen;
    int32 DamagedTargets = 0;
    float TotalDamage = 0.f;
    for (AActor* Target : Targets)
    {
        if (!IsValid(Target) || Target == this || Target == GetOwner() || Target == GetInstigator() || Seen.Contains(Target)) continue;
        Seen.Add(Target);
        if (!Target->CanBeDamaged() || !Target->GetActorEnableCollision()) continue;
        ADWEnemyCharacter* Enemy = Cast<ADWEnemyCharacter>(Target);
        ADWEnemyNest* Nest = Cast<ADWEnemyNest>(Target);
        if ((Enemy && !Enemy->IsAlive()) || (Nest && !Nest->IsAlive())) continue;
        if (!CanAffectTarget(Target)) continue;
        const float Applied = UGameplayStatics::ApplyDamage(Target, FMath::Max(0.f, GetDamageForTarget(Target)), GetInstigatorController(), this, UDamageType::StaticClass());
        if (Enemy && Enemy->IsAlive()) Enemy->ApplySlow(SlowPercent, FMath::Max(0.01f, DamageInterval) + 0.05f);
        if (Applied > 0.f && (bGrantTransformationOnEveryDamageTick || !bGrantedAttackCharge))
        {
            ADWPlayerCharacter* Player = Cast<ADWPlayerCharacter>(GetInstigator());
            if (!Player) Player = Cast<ADWPlayerCharacter>(GetOwner());
            if (Player)
            {
                Player->NotifyAlcoholHit();
                bGrantedAttackCharge = true;
            }
        }
        if (Applied > 0.f)
        {
            ++DamagedTargets;
            TotalDamage += Applied;
            OnTargetDamaged(Target, Applied);
        }
    }
    OnAreaTickCompleted(DamagedTargets, TotalDamage);
}

TArray<AActor*> ADWAlcoholArea::CollectDamageTargets_Implementation() const
{
    return UDWDamageVolumeLibrary::GetDamageTargetsInVolume(DamageVolume, {GetOwner(), GetInstigator()});
}

bool ADWAlcoholArea::CanAffectTarget_Implementation(AActor* Target) const
{
    return IsValid(Target) && !Target->IsA<ADWPlayerCharacter>();
}

float ADWAlcoholArea::GetDamageForTarget_Implementation(AActor* Target) const
{
    return FMath::Max(0.f, DamagePerTick);
}
