#include "DWBuildingEffectsComponent.h"
#include "DWInteractionPromptComponent.h"
#include "CameraOccluderFadeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace DWBuildingEffects
{
    const FName ProxyTag(TEXT("DWBuildingEffectsProxy"));
    float Safe(float V, float Fallback) { return FMath::IsFinite(V) ? V : Fallback; }
    float Ease(float V) { V = FMath::Clamp(V, 0.f, 1.f); return V * V * (3.f - 2.f * V); }
    void PrepareProxy(UStaticMeshComponent* Proxy)
    {
        Proxy->ComponentTags.Add(ProxyTag);
        Proxy->SetMobility(EComponentMobility::Movable);
        Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Proxy->SetGenerateOverlapEvents(false);
        Proxy->SetCanEverAffectNavigation(false);
        Proxy->SetCastShadow(false);
        Proxy->SetRenderCustomDepth(false);
        Proxy->SetVisibility(false, false);
    }
    bool SurfaceAllowsAccent(UStaticMeshComponent* Source)
    {
        if (!IsValid(Source) || !IsValid(Source->GetOwner())) return false;
        const UCameraOccluderFadeComponent* Fade = Source->GetOwner()->FindComponentByClass<UCameraOccluderFadeComponent>();
        if (!Fade || !Fade->IsActive()) return true;
        for (const FCameraFadeSlotStatus& Status : Fade->GetFadeStatus())
        {
            if (Status.Mesh == Source && Status.bOwnsCurrentMaterial &&
                (Status.OriginalFade <= KINDA_SMALL_NUMBER || Status.CurrentFade / Status.OriginalFade < .98f)) return false;
        }
        return true; // Do not fill the player's cleared silhouette with an opaque-looking accent.
    }
}

UDWBuildingEffectsComponent::UDWBuildingEffectsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = true;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    bAutoActivate = true;
    OutlineMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/M_BuildingOutline.M_BuildingOutline")));
    FlashMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/M_BuildingFlash.M_BuildingFlash")));
    CloudMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/M_BuildingCloud.M_BuildingCloud")));
    CloudMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/SM_BuildingCloud.SM_BuildingCloud")));
}

void UDWBuildingEffectsComponent::BeginPlay()
{
    Super::BeginPlay();
    InitializeVisuals(); // No construction playback for placed/restored actors.
}
void UDWBuildingEffectsComponent::Activate(bool bReset)
{
    Super::Activate(bReset);
    if (HasBegunPlay()) InitializeVisuals();
}
void UDWBuildingEffectsComponent::Deactivate()
{
    DestroyAllProxies();
    Super::Deactivate();
}
void UDWBuildingEffectsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    DestroyAllProxies();
    Super::EndPlay(Reason);
}
void UDWBuildingEffectsComponent::OnUnregister()
{
    DestroyAllProxies();
    Super::OnUnregister();
}

void UDWBuildingEffectsComponent::CopySurface(UStaticMeshComponent* Source, UStaticMeshComponent* Proxy)
{
    if (!IsValid(Source) || !IsValid(Proxy)) return;
    const int32 Slots = FMath::Max(1, Source->GetNumMaterials());
    for (int32 Index = 0; Index < Slots; ++Index)
    {
        // Camera fade can create/replace a MID after our initialization. Keep sharing that MID.
        if (Proxy->GetMaterial(Index) != Source->GetMaterial(Index)) Proxy->SetMaterial(Index, Source->GetMaterial(Index));
        if (Proxy->GetOverlayMaterial(true, Index) != Source->GetOverlayMaterial(true, Index))
            Proxy->SetOverlayMaterial(Source->GetOverlayMaterial(true, Index), true, Index);
    }
    if (Proxy->GetOverlayMaterial() != Source->GetOverlayMaterial()) Proxy->SetOverlayMaterial(Source->GetOverlayMaterial());
    Proxy->SetOverlayMaterialMaxDrawDistance(Source->GetOverlayMaterialMaxDrawDistance());
}

UStaticMeshComponent* UDWBuildingEffectsComponent::MakeProxy(UStaticMeshComponent* Source, UMaterialInterface* OverrideMaterial, bool bCopySurface)
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner) || !IsValid(Source) || !Source->GetStaticMesh() || !GetWorld()) return nullptr;
    UStaticMeshComponent* Proxy = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transient);
    if (!Proxy) return nullptr;
    DWBuildingEffects::PrepareProxy(Proxy);
    Proxy->SetStaticMesh(Source->GetStaticMesh());
    // Sibling of the source, never its child: source HiddenInGame must not hide our visuals.
    if (!Cast<UStaticMeshComponent>(Owner->GetRootComponent())) Proxy->SetupAttachment(Owner->GetRootComponent());
    Proxy->SetWorldTransform(Source->GetComponentTransform());
    if (bCopySurface)
    {
        CopySurface(Source, Proxy);
        Proxy->SetCastShadow(Source->CastShadow && !Source->bCastHiddenShadow);
    }
    else for (int32 Index = 0; Index < FMath::Max(1, Source->GetNumMaterials()); ++Index) Proxy->SetMaterial(Index, OverrideMaterial);
    Proxy->RegisterComponent();
    return Proxy;
}

void UDWBuildingEffectsComponent::InitializeVisuals()
{
    AActor* Owner = GetOwner();
    if (bInitialized || bInitializing || !IsActive() || !IsValid(Owner) || Owner->IsActorBeingDestroyed() || !GetWorld() || !GetWorld()->IsGameWorld()) return;
    TGuardValue<bool> Guard(bInitializing, true);
    UMaterialInterface* OM = OutlineMaterial.IsNull() ? nullptr : OutlineMaterial.LoadSynchronous();
    UMaterialInterface* FM = FlashMaterial.IsNull() ? nullptr : FlashMaterial.LoadSynchronous();
    UMaterialInterface* CM = CloudMaterial.IsNull() ? nullptr : CloudMaterial.LoadSynchronous();
    LoadedCloudMesh = CloudMesh.IsNull() ? nullptr : CloudMesh.LoadSynchronous();
    MissingAssetCount = int32(!OM) + int32(!FM) + int32(!CM) + int32(!LoadedCloudMesh);
    OutlineMID = OM ? UMaterialInstanceDynamic::Create(OM, this) : nullptr;
    FlashMID = FM ? UMaterialInstanceDynamic::Create(FM, this) : nullptr;
    CloudMID = CM ? UMaterialInstanceDynamic::Create(CM, this) : nullptr;
    FollowPrompt = Owner->FindComponentByClass<UDWInteractionPromptComponent>();
    if (FollowPrompt.IsValid()) AddTickPrerequisiteComponent(FollowPrompt.Get());
    if (auto* Fade = Owner->FindComponentByClass<UCameraOccluderFadeComponent>()) AddTickPrerequisiteComponent(Fade);
    TInlineComponentArray<UStaticMeshComponent*> Sources;
    Owner->GetComponents(Sources);
    for (UStaticMeshComponent* Source : Sources)
    {
        if (!IsValid(Source) || Source->ComponentHasTag(DWBuildingEffects::ProxyTag) || Cast<UInstancedStaticMeshComponent>(Source) || !Source->GetStaticMesh() ||
            (!SourceComponentTag.IsNone() && !Source->ComponentHasTag(SourceComponentTag))) continue;
        FDWBuildingVisualBinding& B = Bindings.AddDefaulted_GetRef();
        B.Source = Source;
        if (OutlineMID) B.Outline = MakeProxy(Source, OutlineMID, false);
    }
    SourceMeshCount = Bindings.Num();
    bInitialized = true;
    ApplyHighlight();
}

void UDWBuildingEffectsComponent::RefreshVisualSources()
{
    DestroyAllProxies();
    InitializeVisuals();
}
bool UDWBuildingEffectsComponent::IsTemporarilyHiddenSource(const UStaticMeshComponent* Source) const
{
    if (!IsActive() || !bConstructionActive || !IsValid(Source)) return false;
    for (const FDWBuildingVisualBinding& B : Bindings)
        if (B.Source.Get() == Source && B.bSourceHiddenBorrowed && !B.bOriginalHiddenInGame && IsValid(B.Bounce)) return true;
    return false;
}

bool UDWBuildingEffectsComponent::IsSourceRenderable(const FDWBuildingVisualBinding& B) const
{
    const UStaticMeshComponent* S = B.Source.Get();
    const AActor* Owner = GetOwner();
    if (!IsActive() || !IsValid(S) || !S->IsRegistered() || !IsValid(Owner) || Owner->IsHidden() || Owner->IsActorBeingDestroyed()) return false;
    const bool Hidden = B.bSourceHiddenBorrowed ? B.bOriginalHiddenInGame : bool(S->bHiddenInGame);
    return S->GetVisibleFlag() && !Hidden;
}

void UDWBuildingEffectsComponent::SetHighlightAmount(float Amount, bool bDisableAutoFollow)
{
    if (bDisableAutoFollow) bAutoFollowPrompt = false;
    ManualHighlightAmount = FMath::Clamp(DWBuildingEffects::Safe(Amount, 0.f), 0.f, 1.f);
    HighlightAlpha = ManualHighlightAmount;
    InitializeVisuals();
    ApplyHighlight();
}

void UDWBuildingEffectsComponent::ApplyHighlight()
{
    if (OutlineMID)
    {
        OutlineMID->SetScalarParameterValue(TEXT("OutlineAlpha"), HighlightAlpha);
        // Material must normalize VertexNormalWS: width is in world cm, not object scale.
        OutlineMID->SetScalarParameterValue(TEXT("OutlineWidth"), FMath::Max(0.f, DWBuildingEffects::Safe(OutlineWidth, 2.f)));
        OutlineMID->SetVectorParameterValue(TEXT("OutlineColor"), OutlineColor);
    }
    for (FDWBuildingVisualBinding& B : Bindings)
    {
        if (!IsValid(B.Outline)) continue;
        if (B.Source.IsValid()) B.Outline->SetWorldTransform(IsValid(B.Bounce) ? B.Bounce->GetComponentTransform() : B.Source->GetComponentTransform());
        B.Outline->SetVisibility(IsSourceRenderable(B) && DWBuildingEffects::SurfaceAllowsAccent(B.Source.Get()) && HighlightAlpha > KINDA_SMALL_NUMBER, false);
    }
}

bool UDWBuildingEffectsComponent::PlayConstructionEffect()
{
    if (bConstructionPlayed || bConstructionActive || !IsActive() || !GetWorld() || !GetWorld()->IsGameWorld() || GetWorld()->bIsTearingDown || !IsValid(GetOwner()) || GetOwner()->IsActorBeingDestroyed()) return false;
    InitializeVisuals();
    bConstructionPlayed = true; // Only an explicit post-commit request consumes this guard.
    ActiveElapsed = 0.f;
    FBox Bounds(ForceInit);
    int32 ValidClones = 0;
    for (FDWBuildingVisualBinding& B : Bindings)
    {
        UStaticMeshComponent* S = B.Source.Get();
        if (!IsSourceRenderable(B)) continue;
        B.Bounce = MakeProxy(S, nullptr, true);
        if (!IsValid(B.Bounce)) continue;
        Bounds += S->CalcBounds(S->GetComponentTransform()).GetBox();
        B.bOriginalHiddenInGame = S->bHiddenInGame;
        B.bSourceHiddenBorrowed = true;
        // CameraOccluderFade explicitly recognizes our borrowed hide. Preserve source flags/data.
        S->SetHiddenInGame(true, false);
        B.Bounce->SetVisibility(true, false);
        if (FlashMID) B.Flash = MakeProxy(S, FlashMID, false);
        ++ValidClones;
    }
    if (!ValidClones || !Bounds.IsValid)
    {
        FinishConstruction();
        return false;
    }
    const FVector Base(Bounds.GetCenter().X, Bounds.GetCenter().Y, Bounds.Min.Z);
    BaseInOwnerSpace = GetOwner()->GetActorTransform().InverseTransformPosition(Base);
    CloudRingRadii = FVector2D(FMath::Max(10., Bounds.GetExtent().X), FMath::Max(10., Bounds.GetExtent().Y));
    bConstructionActive = true;
    ++ConstructionPlayCount;
    CreateClouds();
    UpdateConstruction();
    ApplyHighlight();
    return true;
}

bool UDWBuildingEffectsComponent::PreviewConstructionEffect()
{
    ResetConstructionEffect(true);
    return PlayConstructionEffect();
}
void UDWBuildingEffectsComponent::ResetConstructionEffect(bool bAllowReplay)
{
    FinishConstruction();
    ActiveElapsed = 0.f;
    if (bAllowReplay) bConstructionPlayed = false;
    ApplyHighlight();
}

void UDWBuildingEffectsComponent::CreateClouds()
{
    const int32 Count = FMath::Clamp(CloudPuffCount, 0, 32);
    if (Count == 0 || !LoadedCloudMesh || !CloudMID || !IsValid(GetOwner())) return;
    Clouds = NewObject<UInstancedStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
    if (!Clouds) return;
    DWBuildingEffects::PrepareProxy(Clouds);
    Clouds->SetStaticMesh(LoadedCloudMesh);
    for (int32 I = 0; I < FMath::Max(1, Clouds->GetNumMaterials()); ++I) Clouds->SetMaterial(I, CloudMID);
    // Remain an owner-owned, unattached component: a generic actor may use its source mesh as root.
    Clouds->RegisterComponent();
    CloudMID->SetVectorParameterValue(TEXT("CloudColor"), CloudColor);
    CloudMeshDiameter = FMath::Max(1.f, float(LoadedCloudMesh->GetBounds().BoxExtent.GetMax() * 2.));
    for (int32 I = 0; I < Count; ++I) Clouds->AddInstance(FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector));
    ActiveCloudCount = Count;
}

float UDWBuildingEffectsComponent::GetEffectDuration() const
{
    return FMath::Max3(FMath::Max(.05f, DWBuildingEffects::Safe(BounceDuration, .65f)),
        FMath::Max(0.f, DWBuildingEffects::Safe(FlashStart, .13f)) + FMath::Max(.01f, DWBuildingEffects::Safe(FlashDuration, .26f)),
        FMath::Max(0.f, DWBuildingEffects::Safe(CloudStart, .14f)) + FMath::Max(.05f, DWBuildingEffects::Safe(CloudDuration, .72f)));
}

void UDWBuildingEffectsComponent::UpdateConstruction()
{
    if (!bConstructionActive || !IsValid(GetOwner())) return;
    const float T = FMath::Clamp(ActiveElapsed / FMath::Max(.05f, DWBuildingEffects::Safe(BounceDuration, .65f)), 0.f, 1.f);
    const float Squash = FMath::Clamp(DWBuildingEffects::Safe(SquashAmount, .20f), 0.f, .5f);
    float ZScale = 1.f;
    if (T < .28f) ZScale = FMath::Lerp(1.f, 1.f - Squash, DWBuildingEffects::Ease(T / .28f));
    else if (T < .52f) ZScale = FMath::Lerp(1.f - Squash, 1.f + Squash * .5f, DWBuildingEffects::Ease((T - .28f) / .24f));
    else ZScale = FMath::Lerp(1.f + Squash * .5f, 1.f, DWBuildingEffects::Ease((T - .52f) / .48f));
    const float XYScale = 1.f / FMath::Sqrt(FMath::Max(.1f, ZScale));
    FVector Scale(XYScale, XYScale, ZScale);
    // A tilted part cannot represent owner-axis anisotropic scale without shear.
    // Use one uniform pulse for the whole assembly so joined pieces stay joined.
    for (const FDWBuildingVisualBinding& B : Bindings)
        if (B.Source.IsValid() && FMath::Abs(FVector::DotProduct(B.Source->GetUpVector(), GetOwner()->GetActorUpVector())) < .999f)
        { Scale = FVector(1.f + (ZScale - 1.f) * .5f); break; }
    const float Lift = FMath::Max(0.f, DWBuildingEffects::Safe(DropHeight, 28.f)) * (1.f - DWBuildingEffects::Ease(T / .28f));
    const FVector Base = GetOwner()->GetActorTransform().TransformPosition(BaseInOwnerSpace);
    const FQuat Axes = GetOwner()->GetActorQuat();
    const float FlashT = (ActiveElapsed - FMath::Max(0.f, DWBuildingEffects::Safe(FlashStart, .13f))) / FMath::Max(.01f, DWBuildingEffects::Safe(FlashDuration, .26f));
    CurrentFlashAmount = FlashT >= 0.f && FlashT < 1.f ? 1.f - DWBuildingEffects::Ease(FlashT) : 0.f;
    if (FlashMID)
    {
        FlashMID->SetScalarParameterValue(TEXT("FlashAmount"), CurrentFlashAmount);
        FlashMID->SetScalarParameterValue(TEXT("FlashStrength"), FMath::Max(0.f, DWBuildingEffects::Safe(FlashStrength, 3.f)));
        FlashMID->SetScalarParameterValue(TEXT("FlashOffset"), FMath::Max(0.f, DWBuildingEffects::Safe(FlashOffset, .05f)));
        FlashMID->SetVectorParameterValue(TEXT("FlashColor"), FlashColor);
    }
    bool bAnyVisible = false;
    for (FDWBuildingVisualBinding& B : Bindings)
    {
        UStaticMeshComponent* S = B.Source.Get();
        if (!IsValid(S) || !IsValid(B.Bounce)) continue;
        const bool Visible = IsSourceRenderable(B);
        bAnyVisible |= Visible;
        CopySurface(S, B.Bounce);
        FTransform Pose = S->GetComponentTransform();
        const FVector LocalOffset = Axes.UnrotateVector(Pose.GetLocation() - Base);
        Pose.SetLocation(Base + Axes.RotateVector(LocalOffset * Scale) + FVector(0, 0, Lift));
        Pose.SetScale3D(Pose.GetScale3D() * Scale);
        B.Bounce->SetWorldTransform(Pose);
        B.Bounce->SetVisibility(Visible, false);
        if (IsValid(B.Flash))
        {
            B.Flash->SetWorldTransform(Pose);
            B.Flash->SetVisibility(Visible && DWBuildingEffects::SurfaceAllowsAccent(S) && CurrentFlashAmount > KINDA_SMALL_NUMBER, false);
        }
    }
    if (Clouds)
    {
        const float U = (ActiveElapsed - FMath::Max(0.f, DWBuildingEffects::Safe(CloudStart, .14f))) / FMath::Max(.05f, DWBuildingEffects::Safe(CloudDuration, .72f));
        const bool Live = U > 0.f && U < 1.f && bAnyVisible;
        Clouds->SetVisibility(Live, false);
        const float Progress = FMath::Clamp(U, 0.f, 1.f);
        const float Envelope = DWBuildingEffects::Ease(Progress / .16f) * (1.f - DWBuildingEffects::Ease((Progress - .62f) / .38f));
        const float Radius = FMath::Max(1.f, DWBuildingEffects::Safe(CloudPuffRadius, 32.f));
        const float Travel = FMath::Max(0.f, DWBuildingEffects::Safe(CloudTravelDistance, 46.f)) * DWBuildingEffects::Ease(Progress);
        const int32 Count = Clouds->GetInstanceCount();
        for (int32 I = 0; I < Count; ++I)
        {
            const float Angle = (float(I) / FMath::Max(1, Count)) * 2.f * PI;
            const float Variation = 1.f + .15f * FMath::Sin(I * 2.17f);
            const FVector Offset(FMath::Cos(Angle) * (CloudRingRadii.X * .75 + Travel), FMath::Sin(Angle) * (CloudRingRadii.Y * .75 + Travel), Radius * .45 * Envelope);
            const FVector Position = Base + Axes.RotateVector(Offset);
            const float Size = Radius * 2.f * Variation * Envelope / CloudMeshDiameter;
            const FTransform Pose(Axes * FRotator(0.f, I * 37.f, 0.f).Quaternion(), Position, FVector(Size, Size * .9f, Size * .75f));
            Clouds->UpdateInstanceTransform(I, Pose, true, I == Count - 1, true);
        }
        ActiveCloudCount = Live ? Count : 0;
    }
    if (ActiveElapsed >= GetEffectDuration()) FinishConstruction();
}

void UDWBuildingEffectsComponent::FinishConstruction()
{
    for (FDWBuildingVisualBinding& B : Bindings)
    {
        if (B.bSourceHiddenBorrowed && B.Source.IsValid() && B.Source->bHiddenInGame)
            B.Source->SetHiddenInGame(B.bOriginalHiddenInGame, false);
        B.bSourceHiddenBorrowed = false;
        if (IsValid(B.Bounce)) B.Bounce->DestroyComponent();
        if (IsValid(B.Flash)) B.Flash->DestroyComponent();
        B.Bounce = nullptr;
        B.Flash = nullptr;
    }
    if (IsValid(Clouds)) Clouds->DestroyComponent();
    Clouds = nullptr;
    ActiveCloudCount = 0;
    CurrentFlashAmount = 0.f;
    bConstructionActive = false;
    if (FlashMID) FlashMID->SetScalarParameterValue(TEXT("FlashAmount"), 0.f);
}

void UDWBuildingEffectsComponent::DestroyAllProxies()
{
    FinishConstruction();
    for (FDWBuildingVisualBinding& B : Bindings) if (IsValid(B.Outline)) B.Outline->DestroyComponent();
    Bindings.Reset();
    if (FollowPrompt.IsValid()) RemoveTickPrerequisiteComponent(FollowPrompt.Get());
    if (IsValid(GetOwner()))
        if (auto* Fade = GetOwner()->FindComponentByClass<UCameraOccluderFadeComponent>()) RemoveTickPrerequisiteComponent(Fade);
    FollowPrompt.Reset();
    OutlineMID = nullptr; FlashMID = nullptr; CloudMID = nullptr; LoadedCloudMesh = nullptr;
    SourceMeshCount = 0;
    bInitialized = false;
}

void UDWBuildingEffectsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    UWorld* World = GetWorld();
    if (!IsActive() || !World || !World->IsGameWorld() || World->bIsTearingDown) return;
    InitializeVisuals();
    HighlightAlpha = bAutoFollowPrompt
        ? (FollowPrompt.IsValid() ? FMath::Clamp(DWBuildingEffects::Safe(FollowPrompt->PresentationOpacity, 0.f), 0.f, 1.f) : 0.f)
        : ManualHighlightAmount;
    if (World->IsPaused()) HighlightAlpha = 0.f;
    if (bConstructionActive)
    {
        if (!World->IsPaused()) ActiveElapsed += FMath::Max(0.f, DWBuildingEffects::Safe(DeltaTime, 0.f));
        UpdateConstruction();
    }
    ApplyHighlight();
}
