#include "DWGameplayCameraShake.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGameplayHUD.h"
#include "DWWorldEvent.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"


bool FDWGameplayCameraHoldSettings::IsValid()const
{
 return FMath::IsFinite(PositionAmplitudeCm)&&PositionAmplitudeCm>=0.f&&FMath::IsFinite(RotationAmplitudeDegrees)&&
  RotationAmplitudeDegrees>=0.f&&FMath::IsFinite(FrequencyHz)&&FrequencyHz>0.f;
}
void UDWGameplayCameraHoldPattern::Configure(const FDWGameplayCameraHoldSettings& Value,UDWGameplayCameraShakeComponent* Owner)
{Settings=Value;HoldOwner=Owner;PhaseCycles=0.;bFinished=!Settings.IsValid()||!IsValid(Owner);}
void UDWGameplayCameraHoldPattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo)const
{OutInfo.Duration=FCameraShakeDuration::Infinite();OutInfo.BlendIn=OutInfo.BlendOut=0.f;}
void UDWGameplayCameraHoldPattern::StartShakePatternImpl(const FCameraShakePatternStartParams&)
{PhaseCycles=0.;bFinished=!Settings.IsValid()||!HoldOwner.IsValid();}
void UDWGameplayCameraHoldPattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& Out)
{
 Out.Location=FVector::ZeroVector;Out.Rotation=FRotator::ZeroRotator;
 auto* Owner=HoldOwner.Get();auto* World=Owner?Owner->GetWorld():nullptr;
 if(bFinished||!Owner||!Owner->ShouldPlaySwordReadyHold()){bFinished=true;return;}
 // A paused instance stays owned, with exactly zero offset and no phase advance.
 if(!World||World->IsPaused()||!FMath::IsFinite(Params.DeltaTime)||Params.DeltaTime<=0.f)return;
 // All frequency ratios have two decimal places. Wrapping every 100 base cycles
 // bounds long-held numerical phase without a discontinuity in any axis.
 PhaseCycles=FMath::Fmod(PhaseCycles+static_cast<double>(Params.DeltaTime)*Settings.FrequencyHz,100.);
 const double Phase=PhaseCycles*2.*PI;
 Out.Location=FVector(0.,FMath::Sin(Phase),FMath::Sin(Phase*1.31))*Settings.PositionAmplitudeCm;
 Out.Rotation=FRotator(FMath::Sin(Phase*1.17),FMath::Sin(Phase*1.41),FMath::Sin(Phase*.83))*Settings.RotationAmplitudeDegrees;
}
bool UDWGameplayCameraHoldPattern::IsFinishedImpl()const
{return bFinished||!HoldOwner.IsValid()||!HoldOwner->ShouldPlaySwordReadyHold();}
void UDWGameplayCameraHoldPattern::StopShakePatternImpl(const FCameraShakePatternStopParams&)
{bFinished=true;}
UDWGameplayHoldCameraShake::UDWGameplayHoldCameraShake(const FObjectInitializer& Init):Super(Init)
{bSingleInstance=false;SetRootShakePattern(CreateDefaultSubobject<UDWGameplayCameraHoldPattern>(TEXT("NativeSwordReadyHold")));}

bool UDWGameplayCameraShakeComponent::ShouldPlaySwordReadyHold()const
{
 // Continuous ready shake was replaced by one committed ChargeReady pulse.
 // Ignore legacy Blueprint true values; old native hold instances also finish here.
 return false;
}
bool UDWGameplayCameraShakeComponent::IsSwordReadyHoldActive()const
{return IsValid(ActiveSwordReadyHold)&&ActiveSwordReadyHold->IsActive()&&!ActiveSwordReadyHold->IsFinished();}
void UDWGameplayCameraShakeComponent::StopSwordReadyHold()
{
 if(auto* Manager=SwordReadyCameraManager.Get();Manager&&IsValid(ActiveSwordReadyHold))Manager->StopCameraShake(ActiveSwordReadyHold,true);
 ActiveSwordReadyHold=nullptr;SwordReadyCameraManager.Reset();
}
void UDWGameplayCameraShakeComponent::UpdateSwordReadyHold()
{
 // Keep old APIs/serialized fields, but never start a continuous ready instance.
 StopSwordReadyHold();
}

bool FDWGameplayCameraPulseSettings::IsValid()const
{
 return FMath::IsFinite(DurationSeconds)&&DurationSeconds>0.f&&FMath::IsFinite(PositionAmplitudeCm)&&PositionAmplitudeCm>=0.f&&
  FMath::IsFinite(RotationAmplitudeDegrees)&&RotationAmplitudeDegrees>=0.f&&FMath::IsFinite(FrequencyHz)&&FrequencyHz>0.f;
}
UDWGameplayCameraPulsePattern::UDWGameplayCameraPulsePattern(const FObjectInitializer& Init):Super(Init){}
void UDWGameplayCameraPulsePattern::Configure(const FDWGameplayCameraPulseSettings& Value,UWorld* World)
{Settings=Value;GameWorld=World;Elapsed=0.;bHasEvaluatedSample=false;bFinished=!Settings.IsValid()||!World;}
void UDWGameplayCameraPulsePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo)const
{OutInfo.Duration=FCameraShakeDuration::Custom(Settings.DurationSeconds);OutInfo.BlendIn=OutInfo.BlendOut=0.f;}
void UDWGameplayCameraPulsePattern::StartShakePatternImpl(const FCameraShakePatternStartParams&)
{Elapsed=0.;bHasEvaluatedSample=false;bFinished=!Settings.IsValid()||!GameWorld.IsValid();}
void UDWGameplayCameraPulsePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& Out)
{
 Out.Location=FVector::ZeroVector;Out.Rotation=FRotator::ZeroRotator;
 auto* World=GameWorld.Get();
 if(bFinished||!World||World->bIsTearingDown){bFinished=true;return;}
 // Camera managers may be evaluated while paused: retain progress and output no offset.
 if(World->IsPaused()||!FMath::IsFinite(Params.DeltaTime)||Params.DeltaTime<=0.f)return;
 // A new pulse may be born late in the current game frame. The camera modifier passes
 // that frame's full DeltaTime, which must not erase it before its first visible sample.
 // Quantize only the first evaluated sample to at most a quarter of the configured duration;
 // subsequent samples retain the real camera DeltaTime. No gameplay/animation clock changes.
 const double Step=bHasEvaluatedSample?static_cast<double>(Params.DeltaTime):
  FMath::Min(static_cast<double>(Params.DeltaTime),static_cast<double>(Settings.DurationSeconds)*.25);
 bHasEvaluatedSample=true;
 Elapsed=FMath::Min(Elapsed+Step,static_cast<double>(Settings.DurationSeconds));
 if(Elapsed>=Settings.DurationSeconds){bFinished=true;return;}
 const double Alpha=Elapsed/Settings.DurationSeconds;
 // A single camera-local kick, followed by a critically damped return.
 // Legacy FrequencyHz is now the inverse-second return stiffness, not cycles.
 // Both factors are nonnegative and nonincreasing on [0,Duration], so
 // the displacement never reverses or crosses the resting camera position.
 const double DampedTime=Elapsed*static_cast<double>(Settings.FrequencyHz);
 const double EndCap=(1.-Alpha)*(1.-Alpha)*(1.+2.*Alpha);
 const double Envelope=(1.+DampedTime)*FMath::Exp(-DampedTime)*EndCap;
 const FVector ImpactDirection=FVector(0.,.25,-1.).GetSafeNormal();
 Out.Location=ImpactDirection*(Settings.PositionAmplitudeCm*Envelope);
 Out.Rotation=FRotator(-1.,0.,.2)*(Settings.RotationAmplitudeDegrees*Envelope);
}
bool UDWGameplayCameraPulsePattern::IsFinishedImpl()const
{return bFinished||!GameWorld.IsValid()||GameWorld->bIsTearingDown;}
void UDWGameplayCameraPulsePattern::StopShakePatternImpl(const FCameraShakePatternStopParams&)
{bFinished=true;}
UDWGameplayPulseCameraShake::UDWGameplayPulseCameraShake(const FObjectInitializer& Init):Super(Init)
{bSingleInstance=false;SetRootShakePattern(CreateDefaultSubobject<UDWGameplayCameraPulsePattern>(TEXT("NativeCombatPulse")));}

bool UDWGameplayCameraShakeComponent::CanPlayCombatPulse()const
{
 const auto* World=GetWorld();const auto* Hero=Cast<ADWPlayerCharacter>(GetOwner());
 const auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
 const auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 const auto* GI=World?World->GetGameInstance<UDWGameInstance>():nullptr;
 return World&&!World->bIsTearingDown&&Hero&&!Hero->IsActorBeingDestroyed()&&!Hero->IsDead()&&PC&&PC->IsLocalController()&&
  PC->GetPawn()==Hero&&PC->GetViewTarget()==Hero&&!PC->IsPaused()&&!UDWWorldEventSubsystem::IsPlaying(this)&&
  (!HUD||(HUD->IsSessionStarted()&&!HUD->IsBlockingGameplay()))&&
  (!GI||(!GI->IsPlayerRestorePending()&&!GI->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive()));
}
void UDWGameplayCameraShakeComponent::PruneCombatPulses()
{ActiveCombatPulses.RemoveAll([](const UCameraShakeBase* P){return !IsValid(P)||!P->IsActive()||P->IsFinished();});}
int32 UDWGameplayCameraShakeComponent::GetActiveCombatPulseCount()const
{int32 Count=0;for(const UCameraShakeBase* P:ActiveCombatPulses)if(IsValid(P)&&P->IsActive()&&!P->IsFinished())++Count;return Count;}
void UDWGameplayCameraShakeComponent::StopCombatPulses()
{
 if(auto* Manager=CombatCameraManager.Get())for(UCameraShakeBase* P:ActiveCombatPulses)if(IsValid(P))Manager->StopCameraShake(P,true);
 ActiveCombatPulses.Reset();CombatCameraManager.Reset();
}
bool UDWGameplayCameraShakeComponent::PlayCombatPulse(EDWCombatShakeEvent Event,float Scale)
{
 switch(Event)
 {
 case EDWCombatShakeEvent::SwordAttack:return PlayConfiguredCombatPulse(SwordAttackPulse,Scale);
 case EDWCombatShakeEvent::Block:return PlayConfiguredCombatPulse(BlockPulse,Scale);
 case EDWCombatShakeEvent::PerfectBlock:return PlayConfiguredCombatPulse(PerfectBlockPulse,Scale);
 case EDWCombatShakeEvent::ChargeReady:return PlayConfiguredCombatPulse(SwordChargeReadyPulse,Scale);
 default:return false;
 }
}
bool UDWGameplayCameraShakeComponent::PlayConfiguredCombatPulse(const FDWGameplayCameraPulseSettings& Settings,float Scale)
{
 if(!bShakeEnabled||!bCombatPulseEnabled||!Settings.IsValid()||!FMath::IsFinite(Scale)||Scale<=0.f||
  (Settings.PositionAmplitudeCm<=0.f&&Settings.RotationAmplitudeDegrees<=0.f)||!CanPlayCombatPulse())return false;
 auto* Hero=Cast<ADWPlayerCharacter>(GetOwner());auto* PC=Cast<APlayerController>(Hero->GetController());
 APlayerCameraManager* Manager=PC->PlayerCameraManager.Get();if(!Manager)return false;
 PruneCombatPulses();
 if(CombatCameraManager.IsValid()&&CombatCameraManager.Get()!=Manager)StopCombatPulses();
 CombatCameraManager=Manager;
 const int32 Limit=FMath::Clamp(MaxConcurrentCombatPulses,1,16);
 while(ActiveCombatPulses.Num()>=Limit){if(auto* P=ActiveCombatPulses[0].Get())Manager->StopCameraShake(P,true);ActiveCombatPulses.RemoveAt(0);}
 FAddCameraShakeParams Params(Scale,ECameraShakePlaySpace::CameraLocal);
 const TWeakObjectPtr<UWorld> World(GetWorld());
 Params.Initializer=FOnInitializeCameraShake::CreateLambda([Settings,World](UCameraShakeBase* Shake)
 {if(auto* Pattern=Cast<UDWGameplayCameraPulsePattern>(Shake->GetRootShakePattern()))Pattern->Configure(Settings,World.Get());});
 auto* Pulse=Manager->StartCameraShake(UDWGameplayPulseCameraShake::StaticClass(),Params);
 if(!IsValid(this)||IsBeingDestroyed()||!CanPlayCombatPulse()||PC->PlayerCameraManager!=Manager)
 {if(IsValid(Manager)&&IsValid(Pulse))Manager->StopCameraShake(Pulse,true);return false;}
 if(!IsValid(Pulse))return false;
 ActiveCombatPulses.Add(Pulse);if(CombatPulseStartCount<MAX_int32)++CombatPulseStartCount;return true;
}

UDWGameplayCameraShakeComponent::UDWGameplayCameraShakeComponent()
{
 PrimaryComponentTick.bCanEverTick=true;
 PrimaryComponentTick.bTickEvenWhenPaused=true;
}
void UDWGameplayCameraShakeComponent::BeginPlay()
{
 Super::BeginPlay();
 // The shared player clock publishes Ready during the actor tick. Read it afterwards,
 // without changing the camera component's group or waiting for weapon visual rendering.
 if(GetOwner())AddTickPrerequisiteActor(GetOwner());
}
void UDWGameplayCameraShakeComponent::StopGameplayShake(bool Immediately)
{
 if(ActiveShake&&(!ActiveShake->IsActive()||ActiveShake->IsFinished())){ActiveShake=nullptr;CameraManager.Reset();bStopping=false;}
 if(ActiveShake&&CameraManager.IsValid()&&(Immediately||!bStopping))CameraManager->StopCameraShake(ActiveShake,Immediately);
 bStopping=!Immediately&&ActiveShake!=nullptr;
 if(Immediately){ActiveShake=nullptr;CameraManager.Reset();bStopping=false;}
 bPlayedThisPeriod=false;
}
void UDWGameplayCameraShakeComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* F)
{
 Super::TickComponent(Dt,Type,F);
 UpdateSwordReadyHold();
 auto* Hero=Cast<ADWPlayerCharacter>(GetOwner());
 auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
 auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 auto* World=GetWorld();auto* GI=World?World->GetGameInstance<UDWGameInstance>():nullptr;
 PruneCombatPulses();
 if(!bShakeEnabled||!bCombatPulseEnabled)StopCombatPulses();
 const bool Blocked=!Hero||!PC||!PC->IsLocalController()||PC->GetViewTarget()!=Hero||PC->IsPaused()||Hero->IsDead()
  ||UDWWorldEventSubsystem::IsPlaying(this)||(HUD&&(!HUD->IsSessionStarted()||HUD->IsBlockingGameplay()))
  ||(GI&&(GI->IsPlayerRestorePending()||GI->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive()));
 if(Blocked)
 {
  StopGameplayShake(true);
  // Paused pulses are retained, and their native pattern refuses to advance the clock.
  if(!PC||!PC->IsPaused()||!Hero||Hero->IsDead()||!World||World->bIsTearingDown)StopCombatPulses();
  return;
 }
 if(!bShakeEnabled||!ShakeClass||ShakeScale<=0.f||(bOnlyWhileMoving&&Hero->GetVelocity().Size2D()<MinimumSpeed))
 {StopGameplayShake(false);return;}
 if(ActiveShake&&(bStopping||ActiveShake->GetClass()!=ShakeClass.Get()||!FMath::IsNearlyEqual(StartedScale,ShakeScale)))StopGameplayShake(true);
 if(ActiveShake&&(ActiveShake->IsFinished()||!ActiveShake->IsActive()))ActiveShake=nullptr;
 if(!ActiveShake&&(!bPlayedThisPeriod||bRepeatFiniteShake))
 {
  CameraManager=PC->PlayerCameraManager;
  if(CameraManager.IsValid())ActiveShake=CameraManager->StartCameraShake(ShakeClass,ShakeScale,ECameraShakePlaySpace::CameraLocal);
  if(ActiveShake){StartedScale=ShakeScale;bStopping=false;bPlayedThisPeriod=true;++StartCount;}
 }
}
void UDWGameplayCameraShakeComponent::EndPlay(const EEndPlayReason::Type Reason)
{StopSwordReadyHold();StopCombatPulses();StopGameplayShake(true);Super::EndPlay(Reason);}
