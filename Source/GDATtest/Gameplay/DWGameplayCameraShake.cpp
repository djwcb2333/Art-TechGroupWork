#include "DWGameplayCameraShake.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayHUD.h"
#include "DWWorldEvent.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

UDWGameplayCameraShakeComponent::UDWGameplayCameraShakeComponent()
{
 PrimaryComponentTick.bCanEverTick=true;
 PrimaryComponentTick.bTickEvenWhenPaused=true;
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
 auto* Hero=Cast<ADWPlayerCharacter>(GetOwner());
 auto* PC=Hero?Cast<APlayerController>(Hero->GetController()):nullptr;
 auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 auto* GI=GetWorld()->GetGameInstance<UDWGameInstance>();
 const bool Blocked=!Hero||!PC||!PC->IsLocalController()||PC->GetViewTarget()!=Hero||PC->IsPaused()||Hero->IsDead()
  ||UDWWorldEventSubsystem::IsPlaying(this)||(HUD&&(!HUD->IsSessionStarted()||HUD->IsBlockingGameplay()))
  ||(GI&&(GI->IsPlayerRestorePending()||GI->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive()));
 if(Blocked){StopGameplayShake(true);return;}
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
{StopGameplayShake(true);Super::EndPlay(Reason);}
