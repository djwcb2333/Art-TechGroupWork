#include "DepthInteractionFocusActor.h"
#include "CameraOccluderFadeComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

ADepthInteractionFocusActor::ADepthInteractionFocusActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
}

void ADepthInteractionFocusActor::BeginPlay()
{
    Super::BeginPlay();
    RefreshNearestFocus();
}

void ADepthInteractionFocusActor::PublishIfChanged(AActor* Target)
{
    const bool bNewNonNullTarget = IsValid(Target);
    if (!bNewNonNullTarget) Target = nullptr;
    // The extra flag detects a previously selected actor becoming invalid/destroyed.
    if (bHasPublishedState && CurrentFocus.Get() == Target && bPublishedNonNullTarget == bNewNonNullTarget) return;
    CurrentFocus = Target;
    bPublishedNonNullTarget = bNewNonNullTarget;
    bHasPublishedState = true;
    LastUpdatedFadeComponentCount = UCameraOccluderFadeComponent::SetInteractionTargetForWorld(
        this, Target, TargetLocalOffset, ProtectedRadiusCm, ProtectedHalfHeightCm);
    ++PublishCount;
}

void ADepthInteractionFocusActor::RefreshNearestFocus()
{
    if (!GetWorld() || !GetWorld()->IsGameWorld() || GetNetMode() == NM_DedicatedServer) return;
    APlayerController* Controller = UGameplayStatics::GetPlayerController(this, LocalPlayerIndex);
    APawn* Pawn = IsValid(Controller) && Controller->IsLocalController() ? Controller->GetPawn() : nullptr;
    if (!IsValid(Pawn))
    {
        PublishIfChanged(nullptr);
        return;
    }
    AActor* Nearest = nullptr;
    double BestDistanceSquared = FMath::Square(FMath::Max(1.f, FocusRadiusCm));
    const FVector PlayerLocation = Pawn->GetActorLocation();
    for (AActor* Anchor : ProtectedInteractionAnchors)
    {
        // Hidden editor-marker actors can still be meaningful anchors. The scene should leave
        // their Actor HiddenInGame flag false so the fade component accepts them as targets.
        if (!IsValid(Anchor) || Anchor == Pawn || Anchor == this || Anchor->GetWorld() != GetWorld()) continue;
        const double DistanceSquared = FVector::DistSquared(PlayerLocation, Anchor->GetActorLocation());
        if (DistanceSquared <= BestDistanceSquared)
        {
            BestDistanceSquared = DistanceSquared;
            Nearest = Anchor;
        }
    }
    PublishIfChanged(Nearest);
}

void ADepthInteractionFocusActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    RefreshNearestFocus();
}

void ADepthInteractionFocusActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld() && GetWorld()->IsGameWorld() && GetNetMode() != NM_DedicatedServer)
        UCameraOccluderFadeComponent::SetInteractionTargetForWorld(this, nullptr, TargetLocalOffset, ProtectedRadiusCm, ProtectedHalfHeightCm);
    CurrentFocus = nullptr;
    bHasPublishedState = false;
    bPublishedNonNullTarget = false;
    Super::EndPlay(EndPlayReason);
}
