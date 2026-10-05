#include "DWMinimapNavigation.h"

#include "Components/SceneComponent.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWResourceNode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
    FText GetMinimapLabel(const FText& Label)
    {
        return Label.IsEmpty() ? NSLOCTEXT("DWMinimap", "UnnamedMarker", "标记点") : Label;
    }
}

UDWMinimapMarkerComponent::UDWMinimapMarkerComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    Label = NSLOCTEXT("DWMinimap", "DefaultMarker", "标记点");
    MarkerColor = DWMinimapMath::CategoryColor(EDWMinimapMarkerKind::Landmark);
}

bool UDWMinimapMarkerComponent::IsMarkerValid() const
{
    const AActor* Owner = GetOwner();
    if (!bEnabled || !IsValid(this) || !IsRegistered() || !IsValid(Owner)
        || Owner->IsActorBeingDestroyed() || !GetWorld() || Owner->GetWorld() != GetWorld()
        || (bRespectOwnerVisibility && Owner->IsHidden()) || Owner->GetActorLocation().ContainsNaN()
        || GetMarkerWorldPosition().ContainsNaN())
    {
        return false;
    }

    if (bRespectOwnerGameplayState)
    {
        if (const ADWEnemyCharacter* Enemy = Cast<ADWEnemyCharacter>(Owner)) return Enemy->IsAlive();
        if (const ADWEnemyNest* Nest = Cast<ADWEnemyNest>(Owner)) return Nest->IsAlive();
        if (const ADWResourceNode* Resource = Cast<ADWResourceNode>(Owner)) return Resource->IsAvailable();
    }
    return true;
}

FVector UDWMinimapMarkerComponent::GetMarkerWorldPosition() const
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner)) return FVector::ZeroVector;
    const USceneComponent* Anchor = Cast<USceneComponent>(DisplayAnchor.GetComponent(Owner));
    // A wrongly chosen external component must not couple annotations to another actor or World.
    const FVector Base = IsValid(Anchor) && Anchor->GetOwner() == Owner
        ? Anchor->GetComponentLocation() : Owner->GetActorLocation();
    return Base + WorldOffset;
}

FLinearColor UDWMinimapMarkerComponent::GetMarkerColor() const
{
    return bUseCategoryColor ? DWMinimapMath::CategoryColor(Kind) : MarkerColor;
}

UDWMinimapNavigationSubsystem* UDWMinimapNavigationSubsystem::GetNavigationSubsystem(const UObject* WorldContextObject)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    return World ? World->GetSubsystem<UDWMinimapNavigationSubsystem>() : nullptr;
}

bool UDWMinimapNavigationSubsystem::SetWaypoint(FVector WorldPosition, FText Label)
{
    if (!GetWorld() || WorldPosition.ContainsNaN()) return false;
    ClearTargetWithoutNotification();
    StaticTarget.bValid = true;
    StaticTarget.WorldPosition = WorldPosition;
    StaticTarget.NavigationPosition = WorldPosition;
    StaticTarget.Label = GetMinimapLabel(Label);
    StaticTarget.Color = DWMinimapMath::CategoryColor(EDWMinimapMarkerKind::Landmark);
    OnNavigationTargetChanged.Broadcast();
    return true;
}

bool UDWMinimapNavigationSubsystem::SetActorTarget(UDWMinimapMarkerComponent* Marker)
{
    if (!IsValid(Marker) || Marker->GetWorld() != GetWorld() || !Marker->IsMarkerValid()) return false;
    ClearTargetWithoutNotification();
    ActorTarget = Marker;
    bHasActorTarget = true;
    OnNavigationTargetChanged.Broadcast();
    return true;
}

void UDWMinimapNavigationSubsystem::ClearTargetWithoutNotification()
{
    ActorTarget.Reset();
    bHasActorTarget = false;
    StaticTarget = FDWMinimapNavigationTarget();
}

void UDWMinimapNavigationSubsystem::ClearWaypoint()
{
    const bool bHadTarget = bHasActorTarget || StaticTarget.bValid;
    ClearTargetWithoutNotification();
    if (bHadTarget) OnNavigationTargetChanged.Broadcast();
}

bool UDWMinimapNavigationSubsystem::ValidateActorTarget()
{
    if (!bHasActorTarget) return false;
    UDWMinimapMarkerComponent* Marker = ActorTarget.Get();
    if (!IsValid(Marker) || Marker->GetWorld() != GetWorld() || !Marker->IsMarkerValid())
    {
        ClearWaypoint();
        return false;
    }
    return true;
}

FDWMinimapNavigationTarget UDWMinimapNavigationSubsystem::GetNavigationTarget()
{
    if (!bHasActorTarget) return StaticTarget;
    if (!ValidateActorTarget()) return FDWMinimapNavigationTarget();
    const UDWMinimapMarkerComponent* Marker = ActorTarget.Get();
    FDWMinimapNavigationTarget Result;
    Result.bValid = true;
    Result.bIsActorTarget = true;
    Result.WorldPosition = Marker->GetMarkerWorldPosition();
    Result.NavigationPosition = Marker->GetOwner()->GetActorLocation();
    Result.Label = GetMinimapLabel(Marker->Label);
    Result.Kind = Marker->Kind;
    Result.Shape = Marker->Shape;
    Result.Color = Marker->GetMarkerColor();
    Result.Priority = Marker->Priority;
    return Result;
}

UDWMinimapMarkerComponent* UDWMinimapNavigationSubsystem::GetTargetMarker()
{
    return ValidateActorTarget() ? ActorTarget.Get() : nullptr;
}

void UDWMinimapNavigationSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    ValidateActorTarget();
}

TStatId UDWMinimapNavigationSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UDWMinimapNavigationSubsystem, STATGROUP_Tickables);
}

bool UDWMinimapNavigationSubsystem::IsTickable() const
{
    return !IsTemplate() && IsInitialized() && bHasActorTarget;
}

bool UDWMinimapNavigationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UDWMinimapNavigationSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
    // Clear before old HUD objects are discarded. The next World receives its own fresh subsystem.
    ClearWaypoint();
    Super::OnWorldEndPlay(InWorld);
}

void UDWMinimapNavigationSubsystem::Deinitialize()
{
    // Teardown must not invoke gameplay listeners. EndPlay handles the normal visible reset.
    ClearTargetWithoutNotification();
    OnNavigationTargetChanged.Clear();
    Super::Deinitialize();
}

float DWMinimapMath::DistanceXYMeters(const FVector& From, const FVector& To)
{
    if (From.ContainsNaN() || To.ContainsNaN()) return 0.f;
    return static_cast<float>(FVector::Dist2D(From, To) / 100.0);
}

FVector2D DWMinimapMath::WorldToNorthUp(const FVector& From, const FVector& To, float NorthYaw)
{
    if (From.ContainsNaN() || To.ContainsNaN() || !FMath::IsFinite(NorthYaw)) return FVector2D::ZeroVector;
    const double Angle = FMath::DegreesToRadians(static_cast<double>(NorthYaw));
    const double C = FMath::Cos(Angle);
    const double S = FMath::Sin(Angle);
    const FVector Delta = To - From;
    return FVector2D(-Delta.X * S + Delta.Y * C, Delta.X * C + Delta.Y * S);
}

float DWMinimapMath::BearingDegrees(const FVector& From, const FVector& To, float NorthYaw)
{
    const FVector2D Compass = WorldToNorthUp(From, To, NorthYaw);
    if (Compass.IsNearlyZero()) return 0.f;
    const float Degrees = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Compass.X, Compass.Y)));
    return FMath::Fmod(Degrees + 360.f, 360.f);
}

FLinearColor DWMinimapMath::CategoryColor(EDWMinimapMarkerKind Kind)
{
    // FColor is sRGB; Slate/material consumers receive linear colors exactly once.
    switch (Kind)
    {
        case EDWMinimapMarkerKind::Enemy: return FLinearColor(FColor(237, 87, 97));
        case EDWMinimapMarkerKind::Resource: return FLinearColor(FColor(223, 168, 57));
        case EDWMinimapMarkerKind::Landmark:
        default: return FLinearColor(FColor(223, 168, 57));
    }
}
