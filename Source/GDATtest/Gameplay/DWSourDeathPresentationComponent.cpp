#include "DWSourDeathPresentationComponent.h"
#include "DWSourDeathOverrideRules.h"
#include "DWSourDoughEnemy.h"
#include "DWSourDoughAnimInstance.h"
#include "DWCombatAuthoritySubsystem.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"

UDWSourDeathPresentationComponent::UDWSourDeathPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    DeathDustMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/SM_BuildingCloud.SM_BuildingCloud")));
    DeathDustMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/M_BuildingCloud.M_BuildingCloud")));
}
DWSourDeathPresentation::FIdentity UDWSourDeathPresentationComponent::CurrentIdentity()const
{
    const auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
    if(!Owner||!Authority)return {};
    const auto Life=Authority->FindRegisteredActorLife(Owner);
    return {Authority->GetWorldEpoch(),Life.Entity,Life.Life};
}
bool UDWSourDeathPresentationComponent::IsCurrentEpisode()const
{
    const auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());const auto* World=GetWorld();
    return bEpisodeOwned&&IsValid(Owner)&&!Owner->IsActorBeingDestroyed()&&
        World&&!World->bIsTearingDown&&!Owner->IsAlive()&&Owner->State==EDWEnemyState::Dead&&CurrentIdentity()==Identity;
}
bool UDWSourDeathPresentationComponent::OwnsEpisode(uint64 Generation)const
{return Generation&&Generation==EpisodeGeneration&&IsCurrentEpisode();}
bool UDWSourDeathPresentationComponent::BeginCommittedDeath()
{
    auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());auto* World=GetWorld();
    if(!IsValid(Owner)||Owner->IsAlive()||Owner->State!=EDWEnemyState::Dead||!World||World->bIsTearingDown||
       World->IsPaused()||bEpisodeOwned||EpisodeGeneration==MAX_uint64)return false;
    const auto DeathLife=CurrentIdentity();
    const double Now=World->GetTimeSeconds();
    const double Duration=FMath::IsFinite(DeathDissolveDuration)?FMath::Clamp(static_cast<double>(DeathDissolveDuration),.05,3600.):1.15;
    const double Hold=FMath::IsFinite(DeathAfterAnimationHold)?FMath::Clamp(static_cast<double>(DeathAfterAnimationHold),0.,60.):0.;
    if(!Clock.Begin(DeathLife,Now,{Duration,Hold}))return false;
    Identity=DeathLife;StartedAt=Now;bEpisodeOwned=true;bDissolveStarted=false;bDissolveComplete=false;++EpisodeGeneration;
    auto* Mesh=Owner->GetMesh();
    const auto* Sequence=Cast<UAnimSequenceBase>(Owner->DeathAnimation.Get());
    const auto* Skeletal=Mesh?Mesh->GetSkeletalMeshAsset():nullptr;
    const auto* Skeleton=Skeletal?Skeletal->GetSkeleton():nullptr;
    bool Compatible=Skeleton&&Sequence&&Sequence->GetSkeleton()==Skeleton;
    if(Skeleton&&Sequence&&!Compatible)
        for(const auto& Other:Skeleton->GetCompatibleSkeletons())
            if(Other.ToSoftObjectPath()==FSoftObjectPath(Sequence->GetSkeleton())){Compatible=true;break;}
    const double Length=Sequence?Sequence->GetPlayLength():0.;
    const double Rate=Sequence?FMath::Abs(Sequence->RateScale):0.;
    const bool AnimReady=Compatible&&Mesh&&Cast<UDWSourDoughAnimInstance>(Mesh->GetAnimInstance())&&
        FMath::IsFinite(Length)&&Length>UE_SMALL_NUMBER&&FMath::IsFinite(Rate)&&Rate>UE_SMALL_NUMBER;
    SignalTimeoutSeconds=AnimReady?DWSourDeathPresentation::SafeSignalTimeout(DeathAnimationSignalTimeout,Length,Rate):0.;
    if(AnimReady)ForceDeathAnimationEvaluation(Mesh);
    if(!AnimReady)
    {
        UE_LOG(LogTemp,Warning,TEXT("Sour death: animation/compatible graph unavailable; use presentation fallback."));
        Clock.AnimationCompleted(Identity,Now,false);
    }
    return true;
}
bool UDWSourDeathPresentationComponent::NotifyAnimationCompleted(const UAnimInstance* Sender,uint64 Generation)
{
    auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());auto* World=GetWorld();
    if(!OwnsEpisode(Generation)||!World||
       !DWSourDeathPresentation::AcceptToken(Identity,CurrentIdentity(),EpisodeGeneration,Generation,bEpisodeOwned,World->IsPaused())||
       !IsValid(Sender)||!Owner->GetMesh()||
       Owner->GetMesh()->GetAnimInstance()!=Sender||!Sender->IsA<UDWSourDoughAnimInstance>())return false;
    return Clock.AnimationCompleted(Identity,World->GetTimeSeconds(),false);
}
void UDWSourDeathPresentationComponent::ForceDeathAnimationEvaluation(USkeletalMeshComponent* Mesh)
{
    if(!IsValid(Mesh)||bTickSettingsCaptured)return;
    ForcedTickMesh=Mesh;bTickSettingsCaptured=true;
    SavedVisibilityTick=Mesh->VisibilityBasedAnimTickOption;
    bSavedUpdateRateOptimizations=Mesh->bEnableUpdateRateOptimizations;
    bSavedMeshTickEnabled=Mesh->IsComponentTickEnabled();
    Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->bEnableUpdateRateOptimizations=false;
    Mesh->SetComponentTickEnabled(true);
}
void UDWSourDeathPresentationComponent::RestoreDeathAnimationEvaluation()
{
    if(!bTickSettingsCaptured)return;
    bTickSettingsCaptured=false;
    auto* Mesh=ForcedTickMesh.Get();ForcedTickMesh.Reset();
    auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());
    if(!IsValid(Mesh)||Mesh->IsBeingDestroyed()||!IsValid(Owner)||Owner->IsActorBeingDestroyed()||Owner->GetMesh()!=Mesh)return;
    if(Mesh->VisibilityBasedAnimTickOption==EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones)
        Mesh->VisibilityBasedAnimTickOption=SavedVisibilityTick;
    if(!Mesh->bEnableUpdateRateOptimizations)Mesh->bEnableUpdateRateOptimizations=bSavedUpdateRateOptimizations;
    if(Mesh->IsComponentTickEnabled())Mesh->SetComponentTickEnabled(bSavedMeshTickEnabled);
}
void UDWSourDeathPresentationComponent::RestoreMaterialsAndDust()
{
    auto* Mesh=BoundMesh.Get();
    if(IsValid(Mesh)&&!Mesh->IsBeingDestroyed()&&!Bindings.IsEmpty())
    {
        // GetMaterial returns the effective mesh material, not the raw override.
        // Preserve inherited/null slots and only touch slots still owned by this episode.
        const bool AllOwned=DWSourDeathOverrideRules::CanRebuild(OriginalRawOverrides,Mesh->OverrideMaterials,Bindings,
            Mesh->MaterialSlotsOverlayMaterial.Num(),Mesh->GetNumMaterials());
        if(AllOwned)
        {
            // EmptyOverrideMaterials also clears slot overlays in UE 5.8. Snapshot
            // and restore those through public APIs, including explicit null slots.
            SavedSlotOverlays=Mesh->MaterialSlotsOverlayMaterial;
            const TWeakObjectPtr<UDWSourDeathPresentationComponent> Self(this);
            Mesh->EmptyOverrideMaterials();
            if(!Self.IsValid())return;
            for(int32 I=0;I<OriginalRawOverrides.Num()&&IsValid(Mesh)&&!Mesh->IsBeingDestroyed();++I)
            {
                if(Mesh->OverrideMaterials.IsValidIndex(I)&&Mesh->OverrideMaterials[I])continue;
                Mesh->SetMaterial(I,OriginalRawOverrides[I]);
                if(!Self.IsValid())return;
            }
            for(int32 I=0;I<SavedSlotOverlays.Num()&&IsValid(Mesh)&&!Mesh->IsBeingDestroyed();++I)
            {
                if(Mesh->MaterialSlotsOverlayMaterial.IsValidIndex(I)&&Mesh->MaterialSlotsOverlayMaterial[I])continue;
                Mesh->SetOverlayMaterial(SavedSlotOverlays[I],true,I);
                if(!Self.IsValid())return;
            }
            SavedSlotOverlays.Reset();
        }
        else
        {
            // A different system changed a slot or array: restore only our own slots.
            // Public SetMaterial(nullptr) preserves inheritance but may leave trailing
            // null array entries; never erase another system's change to trim them.
            for(const auto& B:Bindings)
                if(Mesh->OverrideMaterials.IsValidIndex(B.Slot)&&Mesh->OverrideMaterials[B.Slot]==B.Instance)
                    Mesh->SetMaterial(B.Slot,OriginalRawOverrides.IsValidIndex(B.Slot)?OriginalRawOverrides[B.Slot].Get():nullptr);
        }
    }
    Bindings.Reset();OriginalRawOverrides.Reset();SavedSlotOverlays.Reset();BoundMesh.Reset();
    auto* Dust=DustComponent.Get();DustComponent=nullptr;
    if(IsValid(Dust)){Dust->DeactivateImmediate();Dust->DestroyComponent();}
    auto Clouds=MoveTemp(NativeDust);NativeDust.Reset();NativeDustMID=nullptr;
    for(const auto& Cloud:Clouds)if(IsValid(Cloud.Get()))Cloud->DestroyComponent();
}
void UDWSourDeathPresentationComponent::CancelPresentation()
{
    Clock.Cancel();bEpisodeOwned=false;bDissolveStarted=false;bDissolveComplete=false;Identity={};
    RestoreDeathAnimationEvaluation();
    RestoreMaterialsAndDust();
}
void UDWSourDeathPresentationComponent::FinishOwnedPresentation()
{
    if(!IsCurrentEpisode()){CancelPresentation();return;}
    auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());const uint64 FinishedGeneration=EpisodeGeneration;
    RestoreDeathAnimationEvaluation();
    RestoreMaterialsAndDust();if(!IsCurrentEpisode())return;
    const TWeakObjectPtr<UDWSourDeathPresentationComponent> Self(this);
    Owner->FinishSourDeathPresentation(FinishedGeneration);
    if(Self.IsValid())CancelPresentation();
}
void UDWSourDeathPresentationComponent::StartDissolve()
{
    if(!IsCurrentEpisode()||bDissolveStarted)return;
    bDissolveStarted=true;
    auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());auto* Mesh=Owner->GetMesh();
    TArray<FDWSourDeathMaterialBinding> Pending;bool AllMapped=IsValid(Mesh)&&Mesh->GetNumMaterials()>0;
    for(int32 Slot=0;AllMapped&&Slot<Mesh->GetNumMaterials();++Slot)
    {
        auto* Original=Mesh->GetMaterial(Slot);UMaterialInterface* Replacement=nullptr;
        for(const auto& Surface:DeathDissolveSurfaces)
        {
            if(Surface.OriginalMaterial.IsNull()||Surface.DissolveMaterial.IsNull())continue;
            auto* Source=Surface.OriginalMaterial.LoadSynchronous();
            if(!IsCurrentEpisode())return;
            if(Source==Original){Replacement=Surface.DissolveMaterial.LoadSynchronous();break;}
        }
        if(!IsCurrentEpisode())return;
        auto* Instance=Replacement?UMaterialInstanceDynamic::Create(Replacement,this):nullptr;
        if(!Original||!Instance){AllMapped=false;break;}
        Instance->SetScalarParameterValue(TEXT("DissolveAmount"),0.f);
        Instance->SetVectorParameterValue(TEXT("EdgeColor"),DeathDissolveEdgeColor);
        Instance->SetScalarParameterValue(TEXT("EdgeWidth"),FMath::IsFinite(DeathDissolveEdgeWidth)?FMath::Clamp(DeathDissolveEdgeWidth,.001f,.3f):.045f);
        Instance->SetScalarParameterValue(TEXT("EdgeEmissiveStrength"),FMath::IsFinite(DeathDissolveEdgeStrength)?FMath::Max(0.f,DeathDissolveEdgeStrength):5.f);
        FDWSourDeathMaterialBinding B;B.Slot=Slot;B.Original=Original;B.Instance=Instance;Pending.Add(MoveTemp(B));
    }
    if(AllMapped&&IsCurrentEpisode())
    {
        for(const auto& B:Pending)if(Mesh->GetMaterial(B.Slot)!=B.Original){AllMapped=false;break;}
    }
    if(AllMapped&&IsCurrentEpisode())
    {
        BoundMesh=Mesh;OriginalRawOverrides=Mesh->OverrideMaterials;
        Bindings=MoveTemp(Pending);
        for(const auto& B:Bindings){Mesh->SetMaterial(B.Slot,B.Instance);if(!IsCurrentEpisode())return;}
    }
    else UE_LOG(LogTemp,Warning,TEXT("Sour death: missing material mapping; keep original body and finish with available dust."));
    if(!IsCurrentEpisode()||DeathDustOffset.ContainsNaN()||DeathDustRotation.ContainsNaN()||DeathDustScale.ContainsNaN())return;
    DustStartedAt=GetWorld()->GetTimeSeconds();
    DustDurationRuntime=FMath::IsFinite(DeathDustDuration)?FMath::Clamp(static_cast<double>(DeathDustDuration),.05,60.):.85;
    if(!DeathDustSystem.IsNull())
    {
        UNiagaraSystem* Dust=DeathDustSystem.LoadSynchronous();if(!IsCurrentEpisode())return;
        if(Dust)DustComponent=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,Dust,Owner->GetActorLocation()+DeathDustOffset,
            DeathDustRotation,DeathDustScale,false,true,ENCPoolMethod::None,true);
        if(!IsCurrentEpisode()){RestoreMaterialsAndDust();return;}
    }
    if(!IsValid(DustComponent))StartNativeDust();
}
void UDWSourDeathPresentationComponent::StartNativeDust()
{
    if(!IsCurrentEpisode()||DeathDustCount<=0)return;
    auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());
    UStaticMesh* Mesh=DeathDustMesh.LoadSynchronous();if(!IsCurrentEpisode())return;
    UMaterialInterface* Material=DeathDustMaterial.LoadSynchronous();if(!IsCurrentEpisode())return;
    if(!Mesh||!Material){UE_LOG(LogTemp,Warning,TEXT("Sour death: native dust assets missing."));return;}
    NativeDustMID=UMaterialInstanceDynamic::Create(Material,this);if(!NativeDustMID)return;
    NativeDustMID->SetVectorParameterValue(TEXT("CloudColor"),DeathDustColor);
    DustMeshDiameter=FMath::Max(1.f,static_cast<float>(Mesh->GetBounds().BoxExtent.GetMax()*2.));
    const int32 Count=FMath::Clamp(DeathDustCount,0,24);
    for(int32 I=0;I<Count;++I)
    {
        auto* Cloud=NewObject<UStaticMeshComponent>(Owner,NAME_None,RF_Transient);
        if(!Cloud)continue;
        NativeDust.Add(Cloud);
        Cloud->SetupAttachment(Owner->GetRootComponent());Cloud->SetMobility(EComponentMobility::Movable);
        Cloud->SetCollisionEnabled(ECollisionEnabled::NoCollision);Cloud->SetGenerateOverlapEvents(false);
        Cloud->SetCanEverAffectNavigation(false);Cloud->SetCastShadow(false);Cloud->SetStaticMesh(Mesh);
        Cloud->SetMaterial(0,NativeDustMID);Cloud->SetWorldScale3D(FVector(.001));Cloud->RegisterComponent();
        if(!IsCurrentEpisode()){RestoreMaterialsAndDust();return;}
    }
    UpdateNativeDust(DustStartedAt);
}
void UDWSourDeathPresentationComponent::UpdateNativeDust(double Now)
{
    auto* Owner=Cast<ADWSourDoughEnemy>(GetOwner());
    if(!IsValid(Owner)||!FMath::IsFinite(Now)||DustDurationRuntime<=0.||DeathDustOffset.ContainsNaN()||
       DeathDustRotation.ContainsNaN()||DeathDustScale.ContainsNaN())return;
    const float T=static_cast<float>(FMath::Clamp((Now-DustStartedAt)/DustDurationRuntime,0.,1.));
    const float Radius=FMath::IsFinite(DeathDustRadius)?FMath::Clamp(DeathDustRadius,0.f,1000.f):28.f;
    const int32 Count=NativeDust.Num();
    for(int32 I=0;I<Count;++I)
    {
        const double Angle=I*2.3999632297;
        const float Size=Radius*2.f*(.65f+FMath::Sin(I*1.71f)*.15f)*(1.f-T)/DustMeshDiameter;
        const float Travel=25.f+85.f*T;
        const FVector Position=Owner->GetActorLocation()+DeathDustOffset+
            DeathDustRotation.RotateVector(FVector(FMath::Cos(Angle)*Travel,FMath::Sin(Angle)*Travel,Radius*.4f+35.f*T));
        const FVector Scale=DeathDustScale*FMath::Max(.001f,Size);
        auto* Cloud=NativeDust[I].Get();if(!IsValid(Cloud))continue;
        Cloud->SetWorldLocationAndRotation(Position,DeathDustRotation);Cloud->SetWorldScale3D(Scale);
        Cloud->SetVisibility(T<1.f,false);
    }
}
void UDWSourDeathPresentationComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if(!bEpisodeOwned)return;
    if(!IsCurrentEpisode()){CancelPresentation();return;}
    auto* World=GetWorld();const double Now=World->GetTimeSeconds();const bool Paused=World->IsPaused();
    auto Sample=Clock.Sample(Identity,Now,Paused);
    if(Sample.Phase==DWSourDeathPresentation::EPhase::Animation&&
       DWSourDeathPresentation::SignalTimedOut(StartedAt,Now,SignalTimeoutSeconds,Paused))
    {
        UE_LOG(LogTemp,Warning,TEXT("Sour death: no animation completion signal after %.2f game seconds (hard maximum 120); use fallback."),SignalTimeoutSeconds);
        Clock.AnimationCompleted(Identity,Now,false);Sample=Clock.Sample(Identity,Now,false);
    }
    if(Sample.Phase==DWSourDeathPresentation::EPhase::Canceled)
    {
        UE_LOG(LogTemp,Warning,TEXT("Sour death: invalid presentation clock; clean up the current corpse."));
        FinishOwnedPresentation();return;
    }
    if(Sample.Phase==DWSourDeathPresentation::EPhase::Dissolve||Sample.Phase==DWSourDeathPresentation::EPhase::Complete)
    {
        if(!bDissolveStarted)StartDissolve();
        if(!IsCurrentEpisode())return;
        for(const auto& B:Bindings)if(IsValid(B.Instance))B.Instance->SetScalarParameterValue(TEXT("DissolveAmount"),static_cast<float>(Sample.Dissolve));
    }
    if(Sample.Phase==DWSourDeathPresentation::EPhase::Complete)bDissolveComplete=true;
    if(bDissolveStarted)UpdateNativeDust(Now);
    const bool bDustPlaying=(IsValid(DustComponent)||NativeDust.Num()>0)&&FMath::IsFinite(Now)&&Now-DustStartedAt<DustDurationRuntime;
    if(bDissolveComplete&&!bDustPlaying)FinishOwnedPresentation();
}
void UDWSourDeathPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{CancelPresentation();Super::EndPlay(Reason);}
