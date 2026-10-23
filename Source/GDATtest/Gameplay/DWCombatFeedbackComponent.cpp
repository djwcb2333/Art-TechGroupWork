#include "DWCombatFeedbackComponent.h"
#include "DWGameplayCameraShake.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "Components/MeshComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

UDWCombatFeedbackComponent::UDWCombatFeedbackComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bTickEvenWhenPaused=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flash(TEXT("/Game/DoughWorld/Gameplay/Combat/M_DWCombat_Flash.M_DWCombat_Flash"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Trail(TEXT("/Game/DoughWorld/Gameplay/Combat/M_DWCombat_TrailHighlight.M_DWCombat_TrailHighlight"));
    if(Flash.Succeeded())FlashMaterial=Flash.Object;if(Trail.Succeeded())TrailHighlightMaterial=Trail.Object;
}
void UDWCombatFeedbackComponent::BeginPlay()
{
    Super::BeginPlay();
    // Weapon visuals restore their authored materials in PostUpdateWork. Flash must be applied afterwards.
    if(auto* P=Cast<ADWPlayerCharacter>(GetOwner());P&&P->WeaponAnimationComponent)AddTickPrerequisiteComponent(P->WeaponAnimationComponent);
}
void UDWCombatFeedbackComponent::PlayFeedback(EDWCombatFeedbackCue Cue)
{
    auto* P=Cast<ADWPlayerCharacter>(GetOwner());auto* World=GetWorld();
    if(!bFeedbackEnabled||!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||!World||World->bIsTearingDown||World->IsPaused())return;
    USoundBase* Sound=nullptr;
    switch(Cue)
    {
    case EDWCombatFeedbackCue::ChargeReady:++ChargeReadyCount;Sound=ChargeReadySound;break;
    case EDWCombatFeedbackCue::Block:++BlockCount;Sound=BlockSound;break;
    case EDWCombatFeedbackCue::PerfectBlock:++PerfectBlockCount;Sound=PerfectBlockSound;break;
    case EDWCombatFeedbackCue::SwordSlam:case EDWCombatFeedbackCue::SwordSweep:++SwordAttackCount;break;
    default:return;
    }
    const TWeakObjectPtr<UDWCombatFeedbackComponent> Self(this);const TWeakObjectPtr<ADWPlayerCharacter> Owner(P);
    const auto Current=[&](){return Self.IsValid()&&Owner.IsValid()&&!P->IsActorBeingDestroyed()&&!P->IsDead()&&GetWorld()==World&&!World->bIsTearingDown;};
    if(Cue==EDWCombatFeedbackCue::ChargeReady||Cue==EDWCombatFeedbackCue::PerfectBlock)
    {
        StopFlash();if(!Current())return;
        if(FlashMaterial&&FMath::IsFinite(FlashDurationSeconds)&&FlashDurationSeconds>0.f&&FMath::IsFinite(FlashStrength)&&FlashStrength>0.f)
        {
            FlashInstance=UMaterialInstanceDynamic::Create(FlashMaterial,this);
            if(FlashInstance)
            {
                FlashInstance->SetVectorParameterValue(TEXT("FlashColor"),Cue==EDWCombatFeedbackCue::ChargeReady?ChargeReadyColor:PerfectBlockColor);
                FlashInstance->SetScalarParameterValue(TEXT("FlashStrength"),FlashStrength);
                FlashInstance->SetScalarParameterValue(TEXT("FlashOpacity"),.75f);
                // A cue may be published late in the actor tick, after most of this frame's
                // simulation time has elapsed. Its visual lifetime starts now, not at frame start.
                FlashLastWorldSeconds=World->GetTimeSeconds();
                FlashRemainingSeconds=FlashDurationSeconds;ApplyFlashToMeshes();
            }
        }
    }
    if(!Current())return;
    if(P->GameplayCameraShake)
    {
        if(Cue==EDWCombatFeedbackCue::SwordSlam||Cue==EDWCombatFeedbackCue::SwordSweep)P->GameplayCameraShake->PlayCombatPulse(EDWCombatShakeEvent::SwordAttack);
        else if(Cue==EDWCombatFeedbackCue::Block)P->GameplayCameraShake->PlayCombatPulse(EDWCombatShakeEvent::Block);
        else if(Cue==EDWCombatFeedbackCue::PerfectBlock)P->GameplayCameraShake->PlayCombatPulse(EDWCombatShakeEvent::PerfectBlock);
        else if(Cue==EDWCombatFeedbackCue::ChargeReady)P->GameplayCameraShake->PlayCombatPulse(EDWCombatShakeEvent::ChargeReady);
    }
    if(Current()&&Sound&&FMath::IsFinite(SoundVolume)&&SoundVolume>0.f)
        UGameplayStatics::SpawnSoundAttached(Sound,P->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,SoundVolume);
}

namespace DWMoldRootFeedback
{
bool Same(const FDWMoldRootIdentity& A,const FDWMoldRootIdentity& B)
{return A.WorldEpoch==B.WorldEpoch&&A.RootSerial==B.RootSerial&&A.SourceLife.Entity==B.SourceLife.Entity&&A.SourceLife.Life==B.SourceLife.Life&&A.Action.Id==B.Action.Id&&A.Action.Epoch==B.Action.Epoch;}
FVector FootOffset(const ADWPlayerCharacter& P,FVector Offset)
{return FVector(0,0,-P.GetCapsuleComponent()->GetScaledCapsuleHalfHeight())+Offset;}
}
bool UDWCombatFeedbackComponent::IsMoldRootEpisodeCurrent(bool Rooted)const
{
    const auto* P=Cast<ADWPlayerCharacter>(GetOwner());const auto* World=GetWorld();
    if(IsBeingDestroyed()||!bFeedbackEnabled||!bMoldRootFeedbackEnabled||!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||!World||World->bIsTearingDown||P->IsFormallyRooted()!=Rooted)return false;
    if(Rooted){FDWMoldRootIdentity I;return P->GetFormalRootIdentity(I)&&DWMoldRootFeedback::Same(I,RootFeedbackIdentity);}
    auto* A=UDWCombatAuthoritySubsystem::Get(const_cast<ADWPlayerCharacter*>(P));
    if(!A||A->GetWorldEpoch()!=RootFeedbackIdentity.WorldEpoch)return false;
    const auto Life=A->FindRegisteredActorLife(P);
    return Life.Entity==RootFeedbackIdentity.SourceLife.Entity&&Life.Life==RootFeedbackIdentity.SourceLife.Life;
}
void UDWCombatFeedbackComponent::StopMoldRootLoops()
{
    auto* Audio=RootLoopAudio.Get();auto* VFX=RootLoopVFX.Get();RootLoopAudio=nullptr;RootLoopVFX=nullptr;
    if(IsValid(Audio)){Audio->Stop();Audio->DestroyComponent();}
    if(IsValid(VFX)){VFX->DeactivateImmediate();VFX->DestroyComponent();}
}
void UDWCombatFeedbackComponent::StopMoldRootBursts()
{
    auto Audio=MoveTemp(RootBurstAudio);auto VFX=MoveTemp(RootBurstVFX);RootBurstAudio.Reset();RootBurstVFX.Reset();
    for(UAudioComponent* C:Audio)if(IsValid(C)){C->Stop();C->DestroyComponent();}
    for(UNiagaraComponent* C:VFX)if(IsValid(C)){C->DeactivateImmediate();C->DestroyComponent();}
}
void UDWCombatFeedbackComponent::ClearMoldRootFeedback()
{
    bRootFeedbackBound=bRootEnterPending=bRootExitPending=false;RootFeedbackIdentity={};
    StopMoldRootLoops();StopMoldRootBursts();
}
void UDWCombatFeedbackComponent::EnsureMoldRootLoops()
{
    if(!IsMoldRootEpisodeCurrent(true))return;
    auto* P=Cast<ADWPlayerCharacter>(GetOwner());auto* World=GetWorld();const bool Paused=World->IsPaused();
    if(!IsValid(RootLoopAudio.Get()))RootLoopAudio=nullptr;if(!IsValid(RootLoopVFX.Get()))RootLoopVFX=nullptr;
    if(RootLoopAudio&&RootLoopAudio->GetSound()!=RootLoopSound.Get()){RootLoopAudio->Stop();RootLoopAudio->DestroyComponent();RootLoopAudio=nullptr;}
    if(RootLoopVFX&&RootLoopVFX->GetAsset()!=RootLoopNiagara.Get()){RootLoopVFX->DeactivateImmediate();RootLoopVFX->DestroyComponent();RootLoopVFX=nullptr;}
    if(!RootLoopAudio&&RootLoopSound&&!Paused&&FMath::IsFinite(RootSoundVolume)&&RootSoundVolume>0.f)
    {
        RootLoopAudio=NewObject<UAudioComponent>(P);RootLoopAudio->bAutoActivate=false;RootLoopAudio->bAutoDestroy=false;RootLoopAudio->bStopWhenOwnerDestroyed=true;
        P->AddInstanceComponent(RootLoopAudio);RootLoopAudio->SetupAttachment(P->GetRootComponent());RootLoopAudio->SetSound(RootLoopSound);RootLoopAudio->RegisterComponent();
        if(!IsMoldRootEpisodeCurrent(true)){StopMoldRootLoops();return;}
    }
    if(RootLoopAudio)
    {
        RootLoopAudio->SetVolumeMultiplier(FMath::IsFinite(RootSoundVolume)?FMath::Max(0.f,RootSoundVolume):0.f);RootLoopAudio->SetPaused(Paused);
        if(!Paused&&!RootLoopAudio->IsPlaying())RootLoopAudio->Play();
        if(!IsMoldRootEpisodeCurrent(true)){StopMoldRootLoops();return;}
    }
    if(!RootLoopVFX&&RootLoopNiagara&&!Paused&&!RootEffectFootOffset.ContainsNaN()&&!RootEffectRotation.ContainsNaN()&&!RootEffectScale.ContainsNaN())
    {
        RootLoopVFX=UNiagaraFunctionLibrary::SpawnSystemAttached(RootLoopNiagara,P->GetRootComponent(),NAME_None,DWMoldRootFeedback::FootOffset(*P,RootEffectFootOffset),RootEffectRotation,EAttachLocation::KeepRelativeOffset,false,true,ENCPoolMethod::None,false);
        if(!IsMoldRootEpisodeCurrent(true)){StopMoldRootLoops();return;}
    }
    if(RootLoopVFX)
    {
        RootLoopVFX->SetPaused(Paused);
        if(!RootEffectScale.ContainsNaN())RootLoopVFX->SetRelativeScale3D(RootEffectScale);
        if(!Paused&&!RootLoopVFX->IsActive())RootLoopVFX->Activate(true);
    }
}
void UDWCombatFeedbackComponent::PlayMoldRootBurst(bool Entering)
{
    if(!IsMoldRootEpisodeCurrent(Entering)||GetWorld()->IsPaused())return;
    auto* P=Cast<ADWPlayerCharacter>(GetOwner());USoundBase* Sound=Entering?RootEnterSound.Get():RootExitSound.Get();
    UNiagaraSystem* System=Entering?RootEnterNiagara.Get():RootExitNiagara.Get();
    if(Entering){if(RootEnterCount<MAX_int32)++RootEnterCount;}else if(RootExitCount<MAX_int32)++RootExitCount;
    if(Sound&&FMath::IsFinite(RootSoundVolume)&&RootSoundVolume>0.f)
    {
        auto* Audio=UGameplayStatics::SpawnSoundAttached(Sound,P->GetRootComponent(),NAME_None,FVector::ZeroVector,EAttachLocation::KeepRelativeOffset,true,RootSoundVolume);
        if(IsValid(Audio))RootBurstAudio.Add(Audio);
        if(!IsMoldRootEpisodeCurrent(Entering)){ClearMoldRootFeedback();return;}
    }
    if(System&&!RootEffectFootOffset.ContainsNaN()&&!RootEffectRotation.ContainsNaN()&&!RootEffectScale.ContainsNaN())
    {
        auto* VFX=UNiagaraFunctionLibrary::SpawnSystemAttached(System,P->GetRootComponent(),NAME_None,DWMoldRootFeedback::FootOffset(*P,RootEffectFootOffset),RootEffectRotation,EAttachLocation::KeepRelativeOffset,true,true,ENCPoolMethod::None,false);
        if(IsValid(VFX)){VFX->SetRelativeScale3D(RootEffectScale);RootBurstVFX.Add(VFX);}
        if(!IsMoldRootEpisodeCurrent(Entering))ClearMoldRootFeedback();
    }
}
void UDWCombatFeedbackComponent::PublishMoldRootFeedback(bool Rooted)
{
    auto* P=Cast<ADWPlayerCharacter>(GetOwner());auto* World=GetWorld();
    if(!bFeedbackEnabled||!bMoldRootFeedbackEnabled||!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||!World||World->bIsTearingDown){ClearMoldRootFeedback();return;}
    if(Rooted)
    {
        FDWMoldRootIdentity I;if(!P->GetFormalRootIdentity(I))return;
        if(!bRootFeedbackBound||!DWMoldRootFeedback::Same(I,RootFeedbackIdentity))
        {
            ClearMoldRootFeedback();RootFeedbackIdentity=I;bRootFeedbackBound=bRootEnterPending=true;
            if(RootLoopBindCount<MAX_int32)++RootLoopBindCount;
        }
    }
    else
    {
        const bool HadEpisode=bRootFeedbackBound;
        bRootFeedbackBound=bRootEnterPending=false;StopMoldRootLoops();StopMoldRootBursts();bRootExitPending=HadEpisode;
    }
    TickMoldRootFeedback();
}
void UDWCombatFeedbackComponent::TickMoldRootFeedback()
{
    auto* P=Cast<ADWPlayerCharacter>(GetOwner());auto* World=GetWorld();
    if(!bFeedbackEnabled||!bMoldRootFeedbackEnabled||!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||!World||World->bIsTearingDown){ClearMoldRootFeedback();return;}
    RootBurstAudio.RemoveAll([](const UAudioComponent* C){return !IsValid(C)||C->IsBeingDestroyed();});
    RootBurstVFX.RemoveAll([](const UNiagaraComponent* C){return !IsValid(C)||C->IsBeingDestroyed();});
    if(P->IsFormallyRooted())
    {
        FDWMoldRootIdentity I;
        // Restoring saves deliberately makes this identity unavailable until commit/publication finish.
        if(!P->GetFormalRootIdentity(I)){ClearMoldRootFeedback();return;}
        if(!bRootFeedbackBound||!DWMoldRootFeedback::Same(I,RootFeedbackIdentity))
        {
            ClearMoldRootFeedback();RootFeedbackIdentity=I;bRootFeedbackBound=true;
            if(RootLoopBindCount<MAX_int32)++RootLoopBindCount;
            // Reconstruction from already-committed state never schedules enter (including load/restore).
        }
        EnsureMoldRootLoops();
        if(bRootEnterPending&&!World->IsPaused()&&IsMoldRootEpisodeCurrent(true))
        {bRootEnterPending=false;PlayMoldRootBurst(true);}
    }
    else
    {
        bRootFeedbackBound=bRootEnterPending=false;StopMoldRootLoops();
        if(bRootExitPending)
        {
            if(!IsMoldRootEpisodeCurrent(false)){ClearMoldRootFeedback();return;}
            if(!World->IsPaused()){bRootExitPending=false;PlayMoldRootBurst(false);}
        }
    }
}

void UDWCombatFeedbackComponent::ApplyFlashToMeshes()
{
    auto* P=GetOwner();if(!P||!FlashInstance||FlashRemainingSeconds<=0.f)return;
    TInlineComponentArray<UMeshComponent*> Meshes(P);
    for(auto* Mesh:Meshes)
    {
        if(!IsValid(Mesh)||Mesh->IsBeingDestroyed()||Mesh->GetName().Contains(TEXT("Trail")))continue;
        auto* Binding=FlashBindings.FindByPredicate([Mesh](const FDWCombatFlashBinding& B){return B.Mesh.Get()==Mesh;});
        if(!Binding)
        {
            FDWCombatFlashBinding New;New.Mesh=Mesh;New.bReplaceMaterials=Cast<UProceduralMeshComponent>(Mesh)!=nullptr;
            if(New.bReplaceMaterials)for(int32 I=0;I<Mesh->GetNumMaterials();++I)New.OriginalMaterials.Add(Mesh->GetMaterial(I));
            else New.OriginalOverlay=Mesh->GetOverlayMaterial();
            FlashBindings.Add(MoveTemp(New));Binding=&FlashBindings.Last();
        }
        if(Binding->bReplaceMaterials)
        {
            // ProceduralMesh does not render MeshComponent's overlay. Reapply after normal weapon visuals;
            // keep the current authored material for restoration, including an equipment change during the flash.
            for(int32 I=0;I<Mesh->GetNumMaterials();++I)
            {
                if(I>=Binding->OriginalMaterials.Num())Binding->OriginalMaterials.Add(Mesh->GetMaterial(I));
                else if(Mesh->GetMaterial(I)!=FlashInstance)Binding->OriginalMaterials[I]=Mesh->GetMaterial(I);
                Mesh->SetMaterial(I,FlashInstance);
            }
        }
        else
        {
            if(Mesh->GetOverlayMaterial()!=FlashInstance)Binding->OriginalOverlay=Mesh->GetOverlayMaterial();
            Mesh->SetOverlayMaterial(FlashInstance);
        }
    }
}
void UDWCombatFeedbackComponent::StopFlash()
{
    for(auto& B:FlashBindings)if(auto* Mesh=B.Mesh.Get();IsValid(Mesh)&&!Mesh->IsBeingDestroyed())
    {
        if(B.bReplaceMaterials)
        {for(int32 I=0;I<B.OriginalMaterials.Num();++I)if(Mesh->GetMaterial(I)==FlashInstance)Mesh->SetMaterial(I,B.OriginalMaterials[I]);}
        else if(Mesh->GetOverlayMaterial()==FlashInstance)Mesh->SetOverlayMaterial(B.OriginalOverlay);
    }
    FlashBindings.Reset();FlashInstance=nullptr;FlashRemainingSeconds=0.f;FlashLastWorldSeconds=0.;
}
void UDWCombatFeedbackComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* F)
{
    Super::TickComponent(Dt,Type,F);TickMoldRootFeedback();auto* World=GetWorld();auto* P=Cast<ADWPlayerCharacter>(GetOwner());
    if(!bFeedbackEnabled||!IsValid(P)||P->IsDead()||!World||World->bIsTearingDown){StopFlash();return;}
    if(World->IsPaused()||!FMath::IsFinite(Dt)||Dt<0.f||!IsFlashActive())return;
    const double Now=World->GetTimeSeconds();
    if(!FMath::IsFinite(Now)||Now<FlashLastWorldSeconds){StopFlash();return;}
    const double Elapsed=Now-FlashLastWorldSeconds;FlashLastWorldSeconds=Now;
    FlashRemainingSeconds=static_cast<float>(FMath::Max(0.,static_cast<double>(FlashRemainingSeconds)-Elapsed));
    if(FlashRemainingSeconds<=0.f){StopFlash();return;}
    if(FlashInstance)FlashInstance->SetScalarParameterValue(TEXT("FlashOpacity"),.75f*FMath::Clamp(FlashRemainingSeconds/FMath::Max(FlashDurationSeconds,.001f),0.f,1.f));
    ApplyFlashToMeshes();
}
void UDWCombatFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{ClearMoldRootFeedback();StopFlash();Super::EndPlay(Reason);}
