#include "DWCommittedHitFeedbackComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerCharacter.h"
#include "DWUserSettings.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

UDWCommittedHitFeedbackComponent::UDWCommittedHitFeedbackComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Flash(TEXT("/Game/DoughWorld/Gameplay/Combat/M_DWCombat_Flash.M_DWCombat_Flash"));
    if(Flash.Succeeded())EnemyFlashMaterial=Flash.Object;
}
bool UDWCommittedHitFeedbackComponent::IsEpisodeCurrent()const
{
    const auto* Owner=GetOwner();const auto* World=GetWorld();
    if(!IsValid(Owner)||Owner->IsActorBeingDestroyed()||!World||World->bIsTearingDown||StartedAt<0.||EndsAt<0.)return false;
    if(const auto* P=Cast<ADWPlayerCharacter>(Owner))
    {
        const auto* Shake=P->GameplayCameraShake.Get();
        if(P->IsDead()||World->IsPaused()||!Shake||!Shake->CanPlayCombatPulse())return false;
    }
    if(const auto* E=Cast<ADWEnemyCharacter>(Owner);E&&!E->IsAlive())return false;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
    if(!Authority||Authority->GetWorldEpoch()!=EpisodeEpoch)return false;
    const auto Life=Authority->FindRegisteredActorLife(Owner);
    return Life.Entity==EpisodeLife.Entity&&Life.Life==EpisodeLife.Life&&Life.Entity!=0;
}
float UDWCommittedHitFeedbackComponent::GetCombatHitRemainingSeconds()const
{
    if(!IsEpisodeCurrent())return 0.f;
    const double Now=GetWorld()->GetTimeSeconds();
    return FMath::IsFinite(Now)&&Now>=StartedAt?static_cast<float>(FMath::Max(0.,EndsAt-Now)):0.f;
}
float UDWCommittedHitFeedbackComponent::GetCombatHitIntensity()const
{
    const double Duration=EndsAt-StartedAt;
    return Duration>0.?FMath::Clamp(static_cast<float>(GetCombatHitRemainingSeconds()/Duration),0.f,1.f):0.f;
}
void UDWCommittedHitFeedbackComponent::PublishCommittedDamage(float Applied)
{
    auto* Owner=GetOwner();auto* World=GetWorld();
    if(!IsValid(Owner)||Owner->IsActorBeingDestroyed()||!World||World->bIsTearingDown||World->IsPaused()||!FMath::IsFinite(Applied)||Applied<=0.f)return;
    if(const auto* P=Cast<ADWPlayerCharacter>(Owner);P&&
        (P->IsDead()||!P->GameplayCameraShake||!P->GameplayCameraShake->CanPlayCombatPulse()))return;
    if(const auto* E=Cast<ADWEnemyCharacter>(Owner);E&&!E->IsAlive())return;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return;
    const auto Life=Authority->FindRegisteredActorLife(Owner);if(!Life.Entity||!Life.Life)return;
    const double Now=World->GetTimeSeconds();if(!FMath::IsFinite(Now))return;
    if(!IsEpisodeCurrent())ClearFeedback();
    EpisodeEpoch=Authority->GetWorldEpoch();EpisodeLife=Life;
    if(CombatHitSerial<MAX_uint64)++CombatHitSerial;
    const float Duration=FMath::IsFinite(DurationSeconds)?FMath::Clamp(DurationSeconds,0.f,5.f):0.f;
    StartedAt=Now;EndsAt=Now+Duration;
    if(Cast<ADWEnemyCharacter>(Owner)&&Duration>0.f)ApplyEnemyOverlay();
    if(Cast<ADWPlayerCharacter>(Owner))TryPlayDamageShake();
}
void UDWCommittedHitFeedbackComponent::StopDamageShake()
{
    auto* Manager=DamageCameraManager.Get();auto* Pulse=ActiveDamageShake.Get();
    ActiveDamageShake=nullptr;DamageCameraManager.Reset();
    if(IsValid(Manager)&&IsValid(Pulse))Manager->StopCameraShake(Pulse,true);
}
void UDWCommittedHitFeedbackComponent::TryPlayDamageShake()
{
    auto* P=Cast<ADWPlayerCharacter>(GetOwner());auto* World=GetWorld();
    auto* Shake=P?P->GameplayCameraShake.Get():nullptr;
    auto* Settings=UDWUserSettings::Resolve(this);
    if(!World||!Shake||!Shake->bShakeEnabled||!Shake->bCombatPulseEnabled||
        (Settings&&!Settings->GetDamageShakeEnabled())||!Shake->CanPlayCombatPulse()||
        !DamagePulse.IsValid()||(DamagePulse.PositionAmplitudeCm<=0.f&&DamagePulse.RotationAmplitudeDegrees<=0.f))return;
    if(IsValid(ActiveDamageShake)&&ActiveDamageShake->IsActive()&&!ActiveDamageShake->IsFinished())return;
    StopDamageShake();
    const double Now=World->GetTimeSeconds();
    const float Interval=FMath::IsFinite(MinimumShakeIntervalSeconds)?FMath::Clamp(MinimumShakeIntervalSeconds,0.f,5.f):.14f;
    if(!FMath::IsFinite(Now)||(LastShakeAt>=0.&&Now>=LastShakeAt&&Now-LastShakeAt<Interval))return;
    auto* PC=Cast<APlayerController>(P->GetController());auto* Manager=PC?PC->PlayerCameraManager.Get():nullptr;
    if(!Manager)return;
    FAddCameraShakeParams Params(1.f,ECameraShakePlaySpace::CameraLocal);
    const TWeakObjectPtr<UWorld> SafeWorld(World);const FDWGameplayCameraPulseSettings Config=DamagePulse;
    Params.Initializer=FOnInitializeCameraShake::CreateLambda([Config,SafeWorld](UCameraShakeBase* Pulse)
    {if(auto* Pattern=Cast<UDWGameplayCameraPulsePattern>(Pulse->GetRootShakePattern()))Pattern->Configure(Config,SafeWorld.Get());});
    auto* Pulse=Manager->StartCameraShake(UDWGameplayPulseCameraShake::StaticClass(),Params);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||!IsValid(PC)||!IsValid(Manager)||
       !IsValid(Shake)||!Shake->CanPlayCombatPulse()||PC->PlayerCameraManager!=Manager)
    {if(IsValid(Manager)&&IsValid(Pulse))Manager->StopCameraShake(Pulse,true);return;}
    if(IsValid(Pulse)){ActiveDamageShake=Pulse;DamageCameraManager=Manager;LastShakeAt=Now;}
}
void UDWCommittedHitFeedbackComponent::ApplyEnemyOverlay()
{
    auto* Enemy=Cast<ADWEnemyCharacter>(GetOwner());if(!Enemy||!EnemyFlashMaterial||!FMath::IsFinite(EnemyFlashStrength)||EnemyFlashStrength<=0.f)return;
    if(!FlashInstance)FlashInstance=UMaterialInstanceDynamic::Create(EnemyFlashMaterial,this);
    if(!FlashInstance)return;
    FlashInstance->SetVectorParameterValue(TEXT("FlashColor"),EnemyFlashColor);
    FlashInstance->SetScalarParameterValue(TEXT("FlashStrength"),EnemyFlashStrength);
    FlashInstance->SetScalarParameterValue(TEXT("FlashOpacity"),FMath::IsFinite(EnemyFlashOpacity)?FMath::Clamp(EnemyFlashOpacity,0.f,1.f):0.f);
    // Deliberately use the two body meshes only; attached crumbs and other VFX
    // are not part of the enemy body even when they inherit UMeshComponent.
    UMeshComponent* Meshes[]={Enemy->GetMesh(),Enemy->PlaceholderBody.Get()};
    for(auto* Mesh:Meshes)
    {
        if(!IsValid(Mesh)||Mesh->IsBeingDestroyed()||!Mesh->IsVisible())continue;
        auto* Existing=Bindings.FindByPredicate([Mesh](const FDWEnemyHitOverlayBinding& B){return B.Mesh.Get()==Mesh;});
        if(!Existing){FDWEnemyHitOverlayBinding B;B.Mesh=Mesh;B.Original=Mesh->GetOverlayMaterial();Bindings.Add(MoveTemp(B));Mesh->SetOverlayMaterial(FlashInstance);}
        // Do not seize back an overlay replaced by another system while our pulse is active.
    }
}
void UDWCommittedHitFeedbackComponent::ClearFeedback()
{
    StopDamageShake();
    for(const auto& B:Bindings)if(auto* Mesh=B.Mesh.Get();IsValid(Mesh)&&!Mesh->IsBeingDestroyed()&&Mesh->GetOverlayMaterial()==FlashInstance)Mesh->SetOverlayMaterial(B.Original);
    Bindings.Reset();FlashInstance=nullptr;EpisodeLife={};EpisodeEpoch=0;StartedAt=EndsAt=-1.;
}
void UDWCommittedHitFeedbackComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if(ActiveDamageShake)
    {
        const auto* Settings=UDWUserSettings::Resolve(this);
        const auto* P=Cast<ADWPlayerCharacter>(GetOwner());const auto* Shake=P?P->GameplayCameraShake.Get():nullptr;
        if(!IsValid(ActiveDamageShake)||!ActiveDamageShake->IsActive()||ActiveDamageShake->IsFinished()||
           !Shake||!Shake->bShakeEnabled||!Shake->bCombatPulseEnabled||(Settings&&!Settings->GetDamageShakeEnabled())||
           !IsEpisodeCurrent())StopDamageShake();
    }
    if(StartedAt<0.)return;
    if(!IsEpisodeCurrent()||GetCombatHitRemainingSeconds()<=0.f){ClearFeedback();return;}
    if(FlashInstance)FlashInstance->SetScalarParameterValue(TEXT("FlashOpacity"),(FMath::IsFinite(EnemyFlashOpacity)?FMath::Clamp(EnemyFlashOpacity,0.f,1.f):0.f)*GetCombatHitIntensity());
}
void UDWCommittedHitFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{ClearFeedback();Super::EndPlay(Reason);}
