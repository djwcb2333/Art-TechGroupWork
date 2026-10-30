#include "DWPickupPresentationComponent.h"
#include "DWPickupPresentationMath.h"
#include "DWGatheringWorld.h"
#include "DWCombatAuthoritySubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UDWPickupPresentationComponent::UDWPickupPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;bAutoActivate=true;
    SugarChunkMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Resources/Art/Models/LooseSugar/SM_LooseSugar_Regular_R03.SM_LooseSugar_Regular_R03")));
    OutlineMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/M_BuildingOutline.M_BuildingOutline")));
    FlashMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/M_BuildingFlash.M_BuildingFlash")));
}
void UDWPickupPresentationComponent::NormalizePickupVisual()
{
    auto* Pickup=Cast<ADWGatheringPickup>(GetOwner());
    if(!IsValid(Pickup)||!IsValid(Pickup->Mesh)||bCollectionActive)return;
    auto* Mesh=Pickup->Mesh.Get();
    // Preserve authored mesh/materials. Only the exact default cube gets legacy sugar art.
    const UStaticMesh* Existing=Mesh->GetStaticMesh();
    const bool DefaultCube=Existing&&Existing->GetPathName()==TEXT("/Engine/BasicShapes/Cube.Cube")&&Mesh->GetNumOverrideMaterials()==0;
    if(Pickup->bUseItemCatalogVisuals&&!Mesh->ComponentHasTag(TEXT("DWLootKeepAuthored"))&&Pickup->ItemId==TEXT("SugarChunk")&&DefaultCube&&!SugarChunkMesh.IsNull())
    {
        const TWeakObjectPtr<UDWPickupPresentationComponent> Self(this);
        const TWeakObjectPtr<ADWGatheringPickup> SafePickup(Pickup);
        const TWeakObjectPtr<UStaticMeshComponent> SafeMesh(Mesh);
        UWorld* InitialWorld=GetWorld();const TWeakObjectPtr<UWorld> SafeWorld(InitialWorld);const bool HadWorld=InitialWorld!=nullptr;
        const TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority=InitialWorld?InitialWorld->GetSubsystem<UDWCombatAuthoritySubsystem>():nullptr;
        const bool HadAuthority=Authority.IsValid();const uint64 Epoch=HadAuthority?Authority->GetWorldEpoch():0;
        const FName Item=Pickup->ItemId;const bool UseCatalog=Pickup->bUseItemCatalogVisuals;
        const bool KeepAuthored=Mesh->ComponentHasTag(TEXT("DWLootKeepAuthored"));
        const auto Overrides=Mesh->OverrideMaterials;const FTransform Transform=Mesh->GetRelativeTransform();
        const TSoftObjectPtr<UStaticMesh> Requested=SugarChunkMesh;
        auto StillOwned=[&](const UStaticMesh* ExpectedMesh)
        {
            if(!Self.IsValid()||IsBeingDestroyed()||!SafePickup.IsValid()||SafePickup->IsActorBeingDestroyed()||!SafeMesh.IsValid())return false;
            if(HadWorld?(!SafeWorld.IsValid()||SafeWorld->bIsTearingDown||GetWorld()!=SafeWorld.Get()||SafePickup->GetWorld()!=SafeWorld.Get()):
                (GetWorld()!=nullptr||SafePickup->GetWorld()!=nullptr))return false;
            auto* Current=SafeWorld.IsValid()?SafeWorld->GetSubsystem<UDWCombatAuthoritySubsystem>():nullptr;
            if(HadAuthority?(!Authority.IsValid()||Current!=Authority.Get()||Current->GetWorldEpoch()!=Epoch):(Current!=nullptr))return false;
            return GetOwner()==SafePickup.Get()&&SafePickup->PickupPresentation.Get()==this&&SafePickup->Mesh.Get()==SafeMesh.Get()&&
                SafeMesh->GetOwner()==SafePickup.Get()&&SafePickup->ItemId==Item&&SafePickup->bUseItemCatalogVisuals==UseCatalog&&
                SafeMesh->ComponentHasTag(TEXT("DWLootKeepAuthored"))==KeepAuthored&&!bCollectionActive&&
                SugarChunkMesh.ToSoftObjectPath()==Requested.ToSoftObjectPath()&&SafeMesh->GetStaticMesh()==ExpectedMesh&&
                SafeMesh->OverrideMaterials==Overrides&&SafeMesh->GetRelativeTransform().Equals(Transform);
        };
        UStaticMesh* Art=Requested.LoadSynchronous();
        if(!StillOwned(Existing))return;
        if(IsValid(Art))
        {
            SafeMesh->SetStaticMesh(Art);
            if(!StillOwned(Art))return;
        }
    }
    if(!Mesh->GetStaticMesh())return;
    if(bNormalizeSize)
    {
        FVector Scale=Mesh->GetRelativeScale3D();
        // An invalid or completely collapsed mesh has no usable width ratio. Restore only
        // that component's scale; a collapsed Actor transform is never edited here.
        if(Scale.ContainsNaN()||Scale.IsNearlyZero())
        {Scale=FVector::OneVector;Mesh->SetRelativeScale3D(Scale);}
        const FVector Extent=Mesh->CalcBounds(Mesh->GetComponentTransform()).BoxExtent;
        const float Width=float(2.*FMath::Max(Extent.X,Extent.Y));
        if(FMath::IsFinite(Width)&&Width>KINDA_SMALL_NUMBER)
            Mesh->SetRelativeScale3D(Scale*DWPickupPresentationMath::NormalizeScale(Width,TargetWidthCm));
    }
    const FVector Extent=Mesh->CalcBounds(Mesh->GetComponentTransform()).BoxExtent;
    MeasuredWidthCm=float(2.*FMath::Max(Extent.X,Extent.Y));
    if(IsValid(OutlineProxy)&&OutlineProxy->GetStaticMesh()!=Mesh->GetStaticMesh())
    {OutlineProxy->DestroyComponent();OutlineProxy=nullptr;}
}
void UDWPickupPresentationComponent::OnCatalogVisualChanged()
{
    auto* Pickup=Cast<ADWGatheringPickup>(GetOwner());
    if(!IsValid(Pickup)||!IsValid(Pickup->Mesh)||bCollectionActive)return;
    SourceMesh=Pickup->Mesh;
    auto* OldOutline=OutlineProxy.Get();OutlineProxy=nullptr;
    if(IsValid(OldOutline))OldOutline->DestroyComponent();
}
UStaticMeshComponent* UDWPickupPresentationComponent::MakeProxy(UStaticMeshComponent* Source,UMaterialInterface* Material,bool bCopySurface)
{
    if(!IsValid(GetOwner())||!IsValid(Source)||!Source->GetStaticMesh())return nullptr;
    auto* Proxy=NewObject<UStaticMeshComponent>(GetOwner(),NAME_None,RF_Transient);if(!Proxy)return nullptr;
    Proxy->ComponentTags.Add(TEXT("DWPickupPresentationProxy"));Proxy->SetMobility(EComponentMobility::Movable);
    Proxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);Proxy->SetGenerateOverlapEvents(false);
    Proxy->SetCanEverAffectNavigation(false);Proxy->SetCastShadow(false);Proxy->SetRenderCustomDepth(false);
    Proxy->SetupAttachment(GetOwner()->GetRootComponent());Proxy->SetStaticMesh(Source->GetStaticMesh());
    Proxy->SetWorldTransform(Source->GetComponentTransform());
    for(int32 I=0;I<FMath::Max(1,Source->GetNumMaterials());++I)Proxy->SetMaterial(I,bCopySurface?Source->GetMaterial(I):Material);
    Proxy->SetVisibility(false,false);Proxy->RegisterComponent();return Proxy;
}
void UDWPickupPresentationComponent::EnsureOutline()
{
    auto* Pickup=Cast<ADWGatheringPickup>(GetOwner());
    if(!IsActive()||!IsValid(Pickup)||!GetWorld()||!GetWorld()->IsGameWorld()||!IsValid(Pickup->Mesh))return;
    SourceMesh=Pickup->Mesh;
    if(!OutlineMID&&!OutlineMaterial.IsNull())
        if(auto* M=OutlineMaterial.LoadSynchronous())OutlineMID=UMaterialInstanceDynamic::Create(M,this);
    if(!OutlineProxy&&OutlineMID)OutlineProxy=MakeProxy(SourceMesh.Get(),OutlineMID,false);
}
bool UDWPickupPresentationComponent::IsBudgetedOutlineIdle() const
{
    const auto* Pickup=Cast<ADWGatheringPickup>(GetOwner());
    const UWorld* World=GetWorld();
    if(!bBudgetIdlePickupOutline||!bOutlineOnlyInPickupRange||bCollectionActive||!IsValid(Pickup)||!World)return false;
    const auto* Mesh=Pickup->Mesh.Get();
    if(!bShowOutline||World->IsPaused()||Pickup->IsHidden()||!IsValid(Mesh)||!Mesh->GetVisibleFlag()||Mesh->bHiddenInGame||!Pickup->IsInteractionAvailable())return true;
    const APlayerController* PC=World->GetFirstPlayerController();
    const APawn* Player=PC?PC->GetPawn():nullptr;
    return !Player||!Pickup->IsWithinReach(Player);
}
void UDWPickupPresentationComponent::WakePickupBudget()
{
    if(!bBudgetOwnsTickInterval)return;
    if(FMath::IsNearlyEqual(GetComponentTickInterval(),BudgetTickInterval))
        SetComponentTickIntervalAndCooldown(0.f);
    bBudgetOwnsTickInterval=false;
}
void UDWPickupPresentationComponent::UpdatePickupBudget(bool bIdle)
{
    const float Current=GetComponentTickInterval();
    if(bBudgetOwnsTickInterval&&!FMath::IsNearlyEqual(Current,BudgetTickInterval))
    {bBudgetOwnsTickInterval=false;return;} // Do not overwrite an external interval.
    const float Interval=FMath::IsFinite(PickupHiddenPollSeconds)?FMath::Clamp(PickupHiddenPollSeconds,0.f,.5f):0.f;
    if(!bBudgetIdlePickupOutline||!bIdle||Interval<=KINDA_SMALL_NUMBER){WakePickupBudget();return;}
    if(!bBudgetOwnsTickInterval&&!FMath::IsNearlyZero(Current))return;
    if(!bBudgetOwnsTickInterval||!FMath::IsNearlyEqual(Current,Interval))
    {SetComponentTickInterval(Interval);BudgetTickInterval=Interval;bBudgetOwnsTickInterval=true;}
}
void UDWPickupPresentationComponent::UpdateOutline()
{
    auto* Pickup=Cast<ADWGatheringPickup>(GetOwner());if(!IsValid(Pickup)||!SourceMesh.IsValid())return;
    CurrentHighlight=0.f;
    const bool bRenderable=IsActive()&&!Pickup->IsHidden()&&SourceMesh->GetVisibleFlag()&&(!SourceMesh->bHiddenInGame||bSourceHiddenBorrowed);
    if(bRenderable&&bShowOutline&&GetWorld()&&!GetWorld()->IsPaused())
    {
        if(bCollectionActive)CurrentHighlight=DWPickupPresentationMath::Evaluate(CollectionElapsed/FMath::Max(.05f,DWPickupPresentationMath::Safe(CollectionDuration,.55f))).Opacity;
        else if(Pickup->IsInteractionAvailable())
        {
            const APlayerController* PC=GetWorld()->GetFirstPlayerController();const APawn* Player=PC?PC->GetPawn():nullptr;
            CurrentHighlight=(!bOutlineOnlyInPickupRange||(Player&&Pickup->IsWithinReach(Player)))?1.f:0.f;
        }
    }
    if(OutlineMID)
    {
        OutlineMID->SetScalarParameterValue(TEXT("OutlineAlpha"),CurrentHighlight);
        OutlineMID->SetScalarParameterValue(TEXT("OutlineWidth"),FMath::Max(0.f,DWPickupPresentationMath::Safe(OutlineWidth,1.25f)));
        OutlineMID->SetVectorParameterValue(TEXT("OutlineColor"),OutlineColor);
    }
    if(IsValid(OutlineProxy))
    {
        OutlineProxy->SetWorldTransform(IsValid(CollectionProxy)?CollectionProxy->GetComponentTransform():SourceMesh->GetComponentTransform());
        OutlineProxy->SetVisibility(CurrentHighlight>KINDA_SMALL_NUMBER,false);
    }
}
bool UDWPickupPresentationComponent::PlayCommittedPickupEffect()
{
    auto* Pickup=Cast<ADWGatheringPickup>(GetOwner());
    if(!bEnableCollectionEffect||bCollectionActive||!IsActive()||!IsValid(Pickup)||Pickup->IsActorBeingDestroyed()||Pickup->IsStaged()||!GetWorld()||!GetWorld()->IsGameWorld()||GetWorld()->bIsTearingDown)return false;
    const bool bWasBudgetIdle=bBudgetOwnsTickInterval;
    WakePickupBudget();
    EnsureOutline();if(!SourceMesh.IsValid()||!SourceMesh->GetStaticMesh()||!SourceMesh->GetVisibleFlag()||SourceMesh->bHiddenInGame)return false;
    CollectionProxy=MakeProxy(SourceMesh.Get(),nullptr,true);if(!IsValid(CollectionProxy))return false;
    if(!FlashMID&&!FlashMaterial.IsNull())
        if(auto* M=FlashMaterial.LoadSynchronous())FlashMID=UMaterialInstanceDynamic::Create(M,this);
    if(FlashMID)FlashProxy=MakeProxy(SourceMesh.Get(),FlashMID,false);
    CollectionStart=SourceMesh->GetComponentTransform();const FBox Bounds=SourceMesh->GetStaticMesh()->GetBoundingBox();
    AnchorLocal=FVector(Bounds.GetCenter().X,Bounds.GetCenter().Y,Bounds.Min.Z);CollectionItemId=Pickup->ItemId;
    bOriginalSourceHidden=SourceMesh->bHiddenInGame;bSourceHiddenBorrowed=true;SourceMesh->SetHiddenInGame(true,false);
    CollectionElapsed=0.f;bCollectionActive=true;bDiscardNextCollectionTick=bWasBudgetIdle;++CollectionPlayCount;UpdateCollection();return true;
}
void UDWPickupPresentationComponent::UpdateCollection()
{
    if(!bCollectionActive||!IsValid(CollectionProxy))return;
    const float Duration=FMath::Max(.05f,DWPickupPresentationMath::Safe(CollectionDuration,.55f));
    const auto S=DWPickupPresentationMath::Evaluate(CollectionElapsed/Duration,StretchHeight,SquashAmount,RiseDistance);
    FTransform Pose=CollectionStart;Pose.SetScale3D(CollectionStart.GetScale3D()*FVector(S.XY,S.XY,S.Z));
    const FVector Anchor=CollectionStart.TransformPosition(AnchorLocal);
    Pose.SetLocation(Anchor+FVector(0,0,S.Lift)-Pose.GetRotation().RotateVector(AnchorLocal*Pose.GetScale3D()));
    CollectionProxy->SetWorldTransform(Pose);CollectionProxy->SetVisibility(S.Opacity>KINDA_SMALL_NUMBER,false);CurrentFlash=S.Flash;
    if(FlashMID)
    {
        FlashMID->SetScalarParameterValue(TEXT("FlashAmount"),CurrentFlash);
        FlashMID->SetScalarParameterValue(TEXT("FlashStrength"),FMath::Max(0.f,DWPickupPresentationMath::Safe(FlashStrength,3.f)));
        FlashMID->SetScalarParameterValue(TEXT("FlashOffset"),.05f);FlashMID->SetVectorParameterValue(TEXT("FlashColor"),FlashColor);
    }
    if(IsValid(FlashProxy)){FlashProxy->SetWorldTransform(Pose);FlashProxy->SetVisibility(CurrentFlash>KINDA_SMALL_NUMBER,false);}
    if(CollectionElapsed>=Duration)FinishCollection(true);
}
void UDWPickupPresentationComponent::FinishCollection(bool bNotifyOwner)
{
    const bool bWasActive=bCollectionActive;bCollectionActive=false;bDiscardNextCollectionTick=false;CurrentFlash=0.f;
    if(bSourceHiddenBorrowed&&SourceMesh.IsValid()&&SourceMesh->bHiddenInGame)SourceMesh->SetHiddenInGame(bOriginalSourceHidden,false);
    bSourceHiddenBorrowed=false;
    if(IsValid(CollectionProxy))CollectionProxy->DestroyComponent();if(IsValid(FlashProxy))FlashProxy->DestroyComponent();
    CollectionProxy=nullptr;FlashProxy=nullptr;if(FlashMID)FlashMID->SetScalarParameterValue(TEXT("FlashAmount"),0.f);
    if(bNotifyOwner&&bWasActive)
        if(auto* Pickup=Cast<ADWGatheringPickup>(GetOwner());IsValid(Pickup)&&!Pickup->IsActorBeingDestroyed())Pickup->HandlePickupPresentationFinished();
}
void UDWPickupPresentationComponent::DestroyVisuals(bool bNotifyOwner)
{
    FinishCollection(bNotifyOwner);if(IsValid(OutlineProxy))OutlineProxy->DestroyComponent();
    OutlineProxy=nullptr;OutlineMID=nullptr;FlashMID=nullptr;SourceMesh.Reset();CurrentHighlight=0.f;
}
void UDWPickupPresentationComponent::Deactivate(){DestroyVisuals(true);Super::Deactivate();}
void UDWPickupPresentationComponent::BeginPlay(){bEndingPlay=false;Super::BeginPlay();}
void UDWPickupPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    bEndingPlay=true;
    DestroyVisuals(false);
    Super::EndPlay(Reason);
}
void UDWPickupPresentationComponent::OnUnregister()
{
    // UnregisterComponent does not call EndPlay. In a live game it can be temporary
    // (for example component re-registration) while the logical pickup survives.
    UWorld* World=GetWorld();
    const TWeakObjectPtr<ADWGatheringPickup> Pickup=Cast<ADWGatheringPickup>(GetOwner());
    const bool bRestoreLiveOwner=bCollectionActive&&!bEndingPlay&&HasBegunPlay()&&
        World&&World->IsGameWorld()&&!World->bIsTearingDown&&Pickup.IsValid()&&
        Pickup->HasActorBegunPlay()&&!Pickup->IsActorBeingDestroyed();
    DestroyVisuals(false);
    Super::OnUnregister(); // finish proxy cleanup and registration-state transition first
    if(bRestoreLiveOwner&&Pickup.IsValid()&&Pickup->HasActorBegunPlay()&&!Pickup->IsActorBeingDestroyed())
        Pickup->HandlePickupPresentationFinished();
}
void UDWPickupPresentationComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);UWorld* World=GetWorld();
    if(!IsActive()||!World||!World->IsGameWorld()||World->bIsTearingDown)return;
    auto* Pickup=Cast<ADWGatheringPickup>(GetOwner());if(!IsValid(Pickup)||Pickup->IsActorBeingDestroyed())return;
    if(IsBudgetedOutlineIdle())
    {
        // Keep a previously created outline correctly hidden; do not create one for a distant pickup.
        if(IsValid(OutlineProxy)&&SourceMesh.IsValid())UpdateOutline();
        UpdatePickupBudget(true);
        return;
    }
    UpdatePickupBudget(false);
    EnsureOutline();
    if(bCollectionActive)
    {
        if(Pickup->IsStaged()||Pickup->ItemId!=CollectionItemId)FinishCollection(true);
        else if(!World->IsPaused())
        {
            const float ActiveDt=bDiscardNextCollectionTick?0.f:FMath::Max(0.f,DWPickupPresentationMath::Safe(DeltaTime,0.f));
            bDiscardNextCollectionTick=false;CollectionElapsed+=ActiveDt;UpdateCollection();
        }
    }
    UpdateOutline();
}
