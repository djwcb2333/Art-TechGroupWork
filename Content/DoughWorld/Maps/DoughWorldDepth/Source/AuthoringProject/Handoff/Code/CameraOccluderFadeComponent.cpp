#include "CameraOccluderFadeComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraTypes.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UCameraOccluderFadeComponent::UCameraOccluderFadeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UCameraOccluderFadeComponent::BeginPlay()
{
    Super::BeginPlay();
    RefreshFadeMeshes();
}

void UCameraOccluderFadeComponent::RestoreOriginals()
{
    for (FCameraOccluderMeshState& State : States)
    {
        if (!IsValid(State.Mesh.Get())) continue;
        for (int32 Slot = 0; Slot < State.Originals.Num(); ++Slot)
        {
            // Do not overwrite another system's later material replacement.
            if (State.Instances.IsValidIndex(Slot) && State.Instances[Slot] &&
                State.Mesh->GetMaterial(Slot) == State.Instances[Slot].Get())
            {
                State.Mesh->SetMaterial(Slot, State.Originals[Slot].Get());
            }
        }
    }
    States.Reset();
}

void UCameraOccluderFadeComponent::RefreshFadeMeshes()
{
    RestoreOriginals();
    if (!GetOwner()) return;
    TArray<UStaticMeshComponent*> Meshes;
    GetOwner()->GetComponents<UStaticMeshComponent>(Meshes);
    for (UStaticMeshComponent* Mesh : Meshes)
    {
        if (!IsValid(Mesh) || !Mesh->ComponentHasTag(ComponentTag)) continue;
        // A single MID here would fade every instance. Never do that silently.
        if (Mesh->IsA<UInstancedStaticMeshComponent>())
        {
            UE_LOG(LogTemp, Warning, TEXT("CameraFade: skipping instanced mesh %s; use per-instance data or a separate hero canopy."), *Mesh->GetName());
            continue;
        }

        FCameraOccluderMeshState State;
        State.Mesh = Mesh;
        const int32 NumSlots = Mesh->GetNumMaterials();
        State.Originals.SetNum(NumSlots);
        State.Instances.SetNum(NumSlots);
        State.OriginalFadeValues.SetNum(NumSlots);
        bool bHasSupportedSlot = false;
        for (int32 Slot = 0; Slot < NumSlots; ++Slot)
        {
            UMaterialInterface* Original = Mesh->GetMaterial(Slot);
            State.Originals[Slot] = Original;
            float Baseline = 1.f;
            if (!Original || !Original->GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterName), Baseline, false))
            {
                UE_LOG(LogTemp, Warning, TEXT("CameraFade: %s slot %d lacks scalar %s."), *Mesh->GetName(), Slot, *ParameterName.ToString());
                continue;
            }
            State.OriginalFadeValues[Slot] = Baseline;
            UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Original, this);
            if (!MID) continue;
            State.Instances[Slot] = MID;
            MID->SetScalarParameterValue(ParameterName, Baseline);
            Mesh->SetMaterial(Slot, MID);
            bHasSupportedSlot = true;
        }
        if (bHasSupportedSlot) States.Add(MoveTemp(State));
    }
    NextScanTime = 0.0;
}

void UCameraOccluderFadeComponent::ScanOcclusion(double Now)
{
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    APawn* Pawn = PC ? PC->GetPawn() : nullptr;
    if (!Pawn || !PC->PlayerCameraManager) return; // Old hits expire; materials restore smoothly.
    APlayerCameraManager* Camera = PC->PlayerCameraManager;
    const FVector CameraPosition = Camera->GetCameraLocation();
    const FVector CameraForward = Camera->GetCameraRotation().Vector();
    const bool bOrthographic = Camera->GetCameraCachePOV().ProjectionMode == ECameraProjectionMode::Orthographic;
    const FVector Center = Pawn->GetActorLocation();
    float HalfHeight = 90.f;
    float Radius = 35.f;
    if (const ACharacter* Character = Cast<ACharacter>(Pawn))
    {
        HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
    }
    const FVector Right = Camera->GetCameraRotation().RotateVector(FVector::RightVector);
    const FVector Targets[] = {
        Center + FVector(0, 0, HalfHeight * 0.55f),
        Center + Right * Radius * 0.7f,
        Center - Right * Radius * 0.7f,
        Center - FVector(0, 0, HalfHeight * 0.5f)
    };

    for (FCameraOccluderMeshState& State : States)
    {
        if (!IsValid(State.Mesh.Get()) || !State.Mesh->IsVisible() || State.Mesh->GetOwner() == Pawn) continue;
        // Conservative visual proxy: works for non-colliding canopy meshes too.
        // Independent tests catch several overlapping occluders, not only the first trace blocker.
        const FBox Box = State.Mesh->Bounds.GetBox().ExpandBy(FMath::Max(0.f, BoundsPaddingCm));
        for (const FVector& Target : Targets)
        {
            const float ForwardDistance = FVector::DotProduct(Target - CameraPosition, CameraForward);
            if (ForwardDistance <= 0.f) continue;
            const FVector Start = bOrthographic ? Target - CameraForward * ForwardDistance : CameraPosition;
            const FVector Direction = Target - Start;
            if (!Direction.IsNearlyZero() && FMath::LineBoxIntersection(Box, Start, Target, Direction))
            {
                State.LastOccludedTime = Now;
                break;
            }
        }
    }
}

void UCameraOccluderFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!GetWorld() || States.IsEmpty()) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now >= NextScanTime)
    {
        ScanOcclusion(Now);
        NextScanTime = Now + FMath::Max(0.03f, ScanInterval);
    }
    for (FCameraOccluderMeshState& State : States)
    {
        if (!IsValid(State.Mesh.Get())) continue;
        const bool bOccluded = Now - State.LastOccludedTime <= FMath::Max(0.f, ClearHoldSeconds);
        const float Duration = bOccluded ? FadeOutSeconds : FadeInSeconds;
        State.Strength = FMath::FInterpConstantTo(State.Strength, bOccluded ? 1.f : 0.f,
            DeltaTime, 1.f / FMath::Max(0.01f, Duration));
        const float Multiplier = FMath::Lerp(1.f, FMath::Clamp(OccludedVisibility, 0.02f, 0.8f), State.Strength);
        for (int32 Slot = 0; Slot < State.Instances.Num(); ++Slot)
        {
            if (State.Instances[Slot] && State.Mesh->GetMaterial(Slot) == State.Instances[Slot].Get())
            {
                State.Instances[Slot]->SetScalarParameterValue(ParameterName, State.OriginalFadeValues[Slot] * Multiplier);
            }
        }
    }
}

void UCameraOccluderFadeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RestoreOriginals();
    Super::EndPlay(EndPlayReason);
}
