#include "CameraOccluderFadeComponent.h"
#include "Camera/CameraTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogDoughCameraFade, Log, All);

UCameraOccluderFadeComponent::UCameraOccluderFadeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    bAutoActivate = true;
}

void UCameraOccluderFadeComponent::BeginPlay()
{
    Super::BeginPlay();
    if (IsActive() && GetNetMode() != NM_DedicatedServer) RefreshFadeMeshes();
    else SetComponentTickEnabled(false);
}

void UCameraOccluderFadeComponent::Activate(bool bReset)
{
    Super::Activate(bReset);
    if (HasBegunPlay() && IsActive() && GetNetMode() != NM_DedicatedServer) RefreshFadeMeshes();
}

void UCameraOccluderFadeComponent::Deactivate()
{
    RestoreOriginals();
    Super::Deactivate();
}

void UCameraOccluderFadeComponent::RestoreOriginals()
{
    for (FCameraOccluderMeshState& State : States)
    {
        if (!IsValid(State.Mesh.Get())) continue;
        for (int32 Slot = 0; Slot < State.Originals.Num(); ++Slot)
        {
            // Do not take ownership back from another system which has replaced this slot.
            if (State.Instances.IsValidIndex(Slot) && IsValid(State.Instances[Slot].Get()) &&
                State.Mesh->GetMaterial(Slot) == State.Instances[Slot].Get())
            {
                State.Instances[Slot]->SetScalarParameterValue(State.BoundParameterName, State.OriginalFadeValues[Slot]);
                State.Mesh->SetMaterial(Slot, State.Originals[Slot].Get());
            }
        }
    }
    States.Reset();
}

void UCameraOccluderFadeComponent::RefreshFadeMeshes()
{
    RestoreOriginals();
    SkippedSlotCount = 0;
    if (!IsValid(GetOwner()) || !GetWorld() || !GetWorld()->IsGameWorld() || !IsActive()) return;
    if (GetNetMode() == NM_DedicatedServer) return;

    TArray<UStaticMeshComponent*> Meshes;
    GetOwner()->GetComponents<UStaticMeshComponent>(Meshes);
    for (UStaticMeshComponent* Mesh : Meshes)
    {
        if (!IsValid(Mesh) || !Mesh->ComponentHasTag(ComponentTag)) continue;
        if (Mesh->IsA<UInstancedStaticMeshComponent>()) // Includes HISM.
        {
            UE_LOG(LogDoughCameraFade, Warning, TEXT("Skipping ISM/HISM %s. Use separate canopy components or a per-instance implementation."), *Mesh->GetPathName());
            SkippedSlotCount += Mesh->GetNumMaterials();
            continue;
        }
        if (!Mesh->GetStaticMesh()) continue;

        FCameraOccluderMeshState State;
        State.Mesh = Mesh;
        State.BoundParameterName = ParameterName;
        const int32 NumSlots = Mesh->GetNumMaterials();
        State.Originals.SetNum(NumSlots);
        State.Instances.SetNum(NumSlots);
        State.OriginalFadeValues.SetNum(NumSlots);
        bool bSupported = false;
        for (int32 Slot = 0; Slot < NumSlots; ++Slot)
        {
            UMaterialInterface* Original = Mesh->GetMaterial(Slot);
            State.Originals[Slot] = Original;
            float Baseline = 1.f;
            if (!IsValid(Original) || !Original->GetScalarParameterValue(FHashedMaterialParameterInfo(ParameterName), Baseline, false))
            {
                ++SkippedSlotCount;
                UE_LOG(LogDoughCameraFade, Warning, TEXT("%s slot %d lacks scalar %s; slot left unchanged."), *Mesh->GetPathName(), Slot, *ParameterName.ToString());
                continue;
            }
            UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Original, this);
            if (!MID) { ++SkippedSlotCount; continue; }
            State.OriginalFadeValues[Slot] = Baseline;
            State.Instances[Slot] = MID;
            MID->SetScalarParameterValue(State.BoundParameterName, Baseline);
            Mesh->SetMaterial(Slot, MID);
            bSupported = true;
        }
        if (bSupported) States.Add(MoveTemp(State));
    }
    NextScanTime = 0.0;
    SetComponentTickEnabled(!States.IsEmpty());
}

void UCameraOccluderFadeComponent::AddProtectedTarget(AActor* Target, FVector LocalOffset, float RadiusCm, float HalfHeightCm)
{
    if (!IsValid(Target)) return;
    RemoveProtectedTarget(Target);
    FCameraFadeProtectedTarget Entry;
    Entry.Actor = Target;
    Entry.LocalOffset = LocalOffset;
    Entry.RadiusCm = FMath::Max(0.f, RadiusCm);
    Entry.HalfHeightCm = FMath::Max(0.f, HalfHeightCm);
    AdditionalTargets.Add(Entry);
    NextScanTime = 0.0;
}

void UCameraOccluderFadeComponent::RemoveProtectedTarget(AActor* Target)
{
    AdditionalTargets.RemoveAll([Target](const FCameraFadeProtectedTarget& Entry)
    {
        return !IsValid(Entry.Actor.Get()) || Entry.Actor.Get() == Target;
    });
    NextScanTime = 0.0;
}

void UCameraOccluderFadeComponent::ClearProtectedTargets()
{
    AdditionalTargets.Reset();
    NextScanTime = 0.0;
}

int32 UCameraOccluderFadeComponent::SetInteractionTargetForWorld(UObject* WorldContextObject, AActor* Target,
    FVector LocalOffset, float RadiusCm, float HalfHeightCm)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World || !World->IsGameWorld()) return 0;
    int32 Count = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        TArray<UCameraOccluderFadeComponent*> Components;
        It->GetComponents<UCameraOccluderFadeComponent>(Components);
        for (UCameraOccluderFadeComponent* Component : Components)
        {
            Component->ClearProtectedTargets();
            Component->AddProtectedTarget(Target, LocalOffset, RadiusCm, HalfHeightCm);
            ++Count;
        }
    }
    return Count;
}

int32 UCameraOccluderFadeComponent::ScanOcclusion(double Now)
{
    for (FCameraOccluderMeshState& State : States) State.bOccludedLastScan = false;
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, LocalPlayerIndex);
    if (!IsValid(PC) || !PC->IsLocalController() || !IsValid(PC->PlayerCameraManager)) return 0;
    APawn* Pawn = PC->GetPawn();
    const APlayerCameraManager* Camera = PC->PlayerCameraManager;
    // UE 5.8 uses GetCameraCacheView (the handoff's GetCameraCachePOV is not present).
    const FMinimalViewInfo& View = Camera->GetCameraCacheView();
    const FVector CameraPosition = View.Location;
    const FVector CameraForward = View.Rotation.Vector();
    const FVector CameraRight = View.Rotation.RotateVector(FVector::RightVector);
    const bool bOrthographic = View.ProjectionMode == ECameraProjectionMode::Orthographic;
    TArray<FVector, TInlineAllocator<24>> Targets;
    auto AddSamples = [&Targets, &CameraRight](const FVector& Center, float Radius, float HalfHeight)
    {
        Targets.Add(Center);
        Targets.Add(Center + FVector(0.f, 0.f, HalfHeight * 0.65f));
        Targets.Add(Center + CameraRight * Radius * 0.7f);
        Targets.Add(Center - CameraRight * Radius * 0.7f);
        Targets.Add(Center - FVector(0.f, 0.f, HalfHeight * 0.5f));
    };
    if (bProtectPlayer && IsValid(Pawn))
    {
        float HalfHeight = 90.f;
        float Radius = 35.f;
        if (const ACharacter* Character = Cast<ACharacter>(Pawn))
        {
            if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
            {
                HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
                Radius = Capsule->GetScaledCapsuleRadius();
            }
        }
        AddSamples(Pawn->GetActorLocation(), Radius, HalfHeight);
    }
    AdditionalTargets.RemoveAll([](const FCameraFadeProtectedTarget& Entry) { return !IsValid(Entry.Actor.Get()); });
    for (const FCameraFadeProtectedTarget& Entry : AdditionalTargets)
    {
        if (Entry.Actor->IsHidden() || Entry.Actor.Get() == GetOwner()) continue;
        const FVector Center = Entry.Actor->GetActorTransform().TransformPosition(Entry.LocalOffset);
        if (IsValid(Pawn) && AdditionalTargetMaxDistanceCm > 0.f &&
            FVector::DistSquared(Center, Pawn->GetActorLocation()) > FMath::Square(AdditionalTargetMaxDistanceCm)) continue;
        AddSamples(Center, FMath::Max(0.f, Entry.RadiusCm), FMath::Max(0.f, Entry.HalfHeightCm));
    }
    int32 OccludedCount = 0;
    for (FCameraOccluderMeshState& State : States)
    {
        UStaticMeshComponent* Mesh = State.Mesh.Get();
        if (!IsValid(Mesh) || !Mesh->IsVisible() || Mesh->GetOwner()->IsHidden() || Mesh->GetOwner() == Pawn) continue;
        // Independent visual AABB tests do not stop at the first collision blocker.
        // Large irregular meshes should be split into local canopy/roof/upper-wall pieces.
        const FBox Box = Mesh->Bounds.GetBox().ExpandBy(FMath::Max(0.f, BoundsPaddingCm));
        for (const FVector& Target : Targets)
        {
            const double Depth = FVector::DotProduct(Target - CameraPosition, CameraForward);
            if (Depth <= 0.0) continue;
            const FVector Start = bOrthographic ? Target - CameraForward * Depth : CameraPosition;
            const FVector ToTarget = Target - Start;
            const double Length = ToTarget.Length();
            const double Inset = FMath::Max(0.f, TargetEndInsetCm);
            if (Length <= Inset + UE_SMALL_NUMBER) continue;
            const FVector End = Target - ToTarget * (Inset / Length);
            if (FMath::LineBoxIntersection(Box, Start, End, End - Start))
            {
                State.bOccludedLastScan = true;
                State.LastOccludedTime = Now;
                ++OccludedCount;
                break;
            }
        }
    }
    return OccludedCount;
}

int32 UCameraOccluderFadeComponent::EvaluateOcclusionNow()
{
    if (!GetWorld() || !GetWorld()->IsGameWorld() || !IsActive()) return 0;
    const double Now = GetWorld()->GetTimeSeconds();
    const int32 Result = ScanOcclusion(Now);
    NextScanTime = Now + FMath::Max(0.03f, ScanInterval);
    return Result;
}

void UCameraOccluderFadeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!GetWorld() || !IsActive() || States.IsEmpty()) return;
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now >= NextScanTime) EvaluateOcclusionNow();
    for (FCameraOccluderMeshState& State : States)
    {
        if (!IsValid(State.Mesh.Get())) continue;
        const bool bOccluded = State.bOccludedLastScan || Now - State.LastOccludedTime <= FMath::Max(0.f, ClearHoldSeconds);
        const float Duration = bOccluded ? FadeOutSeconds : FadeInSeconds;
        State.Strength = FMath::FInterpConstantTo(State.Strength, bOccluded ? 1.f : 0.f,
            DeltaTime, 1.f / FMath::Max(0.01f, Duration));
        const float Multiplier = FMath::Lerp(1.f, FMath::Clamp(OccludedVisibility, 0.f, 1.f), State.Strength);
        for (int32 Slot = 0; Slot < State.Instances.Num(); ++Slot)
        {
            UMaterialInstanceDynamic* MID = State.Instances[Slot].Get();
            if (IsValid(MID) && State.Mesh->GetMaterial(Slot) == MID)
                MID->SetScalarParameterValue(State.BoundParameterName, State.OriginalFadeValues[Slot] * Multiplier);
        }
    }
}

TArray<FCameraFadeSlotStatus> UCameraOccluderFadeComponent::GetFadeStatus() const
{
    TArray<FCameraFadeSlotStatus> Result;
    for (const FCameraOccluderMeshState& State : States)
    {
        if (!IsValid(State.Mesh.Get())) continue;
        for (int32 Slot = 0; Slot < State.Instances.Num(); ++Slot)
        {
            UMaterialInstanceDynamic* MID = State.Instances[Slot].Get();
            if (!IsValid(MID)) continue;
            FCameraFadeSlotStatus Status;
            Status.Mesh = State.Mesh;
            Status.SlotIndex = Slot;
            Status.OriginalMaterial = State.Originals[Slot];
            Status.DynamicMaterial = MID;
            Status.OriginalFade = State.OriginalFadeValues[Slot];
            Status.CurrentFade = MID->K2_GetScalarParameterValue(State.BoundParameterName);
            Status.FadeStrength = State.Strength;
            Status.bOwnsCurrentMaterial = State.Mesh->GetMaterial(Slot) == MID;
            Status.bOccludedLastScan = State.bOccludedLastScan;
            Result.Add(Status);
        }
    }
    return Result;
}

FCameraFadeViewSnapshot UCameraOccluderFadeComponent::GetViewSnapshot() const
{
    FCameraFadeViewSnapshot Result;
    const APlayerController* PC = UGameplayStatics::GetPlayerController(this, LocalPlayerIndex);
    if (!IsValid(PC) || !IsValid(PC->PlayerCameraManager)) return Result;
    const FMinimalViewInfo& View = PC->PlayerCameraManager->GetCameraCacheView();
    Result.bValid = true;
    Result.Location = View.Location;
    Result.Rotation = View.Rotation;
    Result.FOV = View.FOV;
    Result.OrthoWidth = View.OrthoWidth;
    Result.AspectRatio = View.AspectRatio;
    Result.bOrthographic = View.ProjectionMode == ECameraProjectionMode::Orthographic;
    if (IsValid(PC->GetPawn())) Result.DistanceToPawn = FVector::Dist(View.Location, PC->GetPawn()->GetActorLocation());
    return Result;
}

void UCameraOccluderFadeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RestoreOriginals();
    Super::EndPlay(EndPlayReason);
}

void UCameraOccluderFadeComponent::OnUnregister()
{
    RestoreOriginals();
    Super::OnUnregister();
}
