#if WITH_DEV_AUTOMATION_TESTS
#include "DWGameplayCameraShake.h"
#include "DWCombatFeedbackComponent.h"
#include "Engine/LocalPlayer.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatSword.h"
#include "DWCombatEquipment.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatTestWorld.h"
#include "DWPlayerCharacter.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace DWSwordReadyHoldTests
{
struct FFixture
{
 TStrongObjectPtr<UDWGameInstance> GI;
 TStrongObjectPtr<ULocalPlayer> CameraLocalPlayer;
 UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;APlayerController* PC=nullptr;
 FDWSwordTuning Tuning;bool bGranted=false;
 FFixture()
 {
  World=DWCombatTestWorld::Create(GI,TEXT("SwordReadyHold"));if(!World)return;
  Player=Spawn<ADWPlayerCharacter>();PC=Spawn<APlayerController>();
  if(PC)
  {
   PC->SetAsLocalPlayerController();
   CameraLocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine));PC->SetPlayer(CameraLocalPlayer.Get());
  }
  if(Player){Player->SetActorTickEnabled(false);Player->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);}
  if(PC&&Player){PC->Possess(Player);PC->SetViewTarget(Player);PC->SetActorTickEnabled(false);}
  FString Error;if(GI->GetCombatRules())FDWSwordTuning::Build(*GI->GetCombatRules(),Tuning,Error);
 }
 ~FFixture()
 {
  if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
  for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
  World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 }
 template<class C>C* Spawn()
 {
  FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  auto* Actor=World->SpawnActor<C>(C::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Params);
  if(Actor&&!Actor->HasActorBegunPlay())Actor->DispatchBeginPlay();return Actor;
 }
 UDWWeaponAnimationComponent* Weapon()const{return Player?Player->WeaponAnimationComponent.Get():nullptr;}
 UDWGameplayCameraShakeComponent* Camera()const{return Player?Player->GameplayCameraShake.Get():nullptr;}
 void AdvanceWeapon(double Dt){World->TimeSeconds+=static_cast<float>(Dt);Weapon()->AdvanceAnimation(Dt);}
 bool Ready()
 {
  if(!World||!Player||!PC||!PC->IsLocalController()||!PC->PlayerCameraManager||!Camera()||!Weapon()||!Player->HasCombatVitalsReady()||!Tuning.IsValid())return false;
  if(!bGranted)
  {
   auto* Authority=UDWCombatAuthoritySubsystem::Get(Player);if(!Authority)return false;
   const auto Action=Authority->BeginAction(Player);
   FDWCombatEquipmentGrantRequest Request{Action,Authority->CaptureActorLife(Player),1,Weapon()->GetCombatWeaponRestoreEpoch(),TEXT("DWWeapon_Greatsword")};
   bGranted=FDWCombatEquipmentTransaction::Grant(*Weapon(),Request).bCommittedThisCall;Authority->EndAction(Action);
  }
  if(!bGranted||!Weapon()->SelectWeapon(EDWAnimatedWeapon::Greatsword))return false;
  AdvanceWeapon(Weapon()->GetRemainingAttackLockSeconds()+.01);
  if(!Weapon()->PrimaryPressed(Player->GetActorLocation()+FVector(450,0,0)))return false;
  Weapon()->PublishAcceptedWeaponStartEffects();AdvanceWeapon(Tuning.ChargeSeconds);
  Camera()->TickComponent(.01f,LEVELTICK_All,nullptr);
  return Weapon()->IsFormalSwordWaitingForRelease()&&!Camera()->IsSwordReadyHoldActive();
 }
 UDWGameplayCameraHoldPattern* Pattern()const
 {auto* Hold=Camera()?Camera()->GetActiveSwordReadyHold():nullptr;return Hold?Cast<UDWGameplayCameraHoldPattern>(Hold->GetRootShakePattern()):nullptr;}
};
FCameraShakePatternUpdateResult Advance(UDWGameplayCameraHoldPattern& Pattern,float Seconds)
{
 FCameraShakePatternUpdateParams Params;Params.DeltaTime=Seconds;FCameraShakePatternUpdateResult Result;
 Pattern.UpdateShakePattern(Params,Result);return Result;
}
}

// The two former sustained-hold tests now exercise the user's replacement rule.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordReadySingleImpactTest,"DoughWorld.Combat.Feedback.SwordReadySingleImpactAndNoLegacyHold",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordReadySingleImpactTest::RunTest(const FString&)
{
 using namespace DWSwordReadyHoldTests;FFixture F;
 if(!TestTrue(TEXT("Real owned sword, local player and camera ready"),F.Ready()))return false;
 auto* Camera=F.Camera();auto* Feedback=F.Player->CombatFeedback.Get();auto* Manager=F.PC->PlayerCameraManager.Get();
 if(!TestNotNull(TEXT("Committed feedback presenter"),Feedback))return false;
 TestEqual(TEXT("Natural full charge publishes one ready cue"),Feedback->ChargeReadyCount,1);
 TestEqual(TEXT("Natural ready cue creates exactly one finite impact"),Camera->CombatPulseStartCount,1);
 TestEqual(TEXT("Ready never starts a continuous hold"),Camera->SwordReadyHoldStartCount,0);
 TestFalse(TEXT("Legacy hold is disabled by default"),GetDefault<UDWGameplayCameraShakeComponent>()->bSwordReadyHoldEnabled);
 Camera->bSwordReadyHoldEnabled=true;Camera->TickComponent(.01f,LEVELTICK_All,nullptr);
 TestFalse(TEXT("Saved legacy true cannot reactivate continuous shake"),Camera->IsSwordReadyHoldActive());
 auto Invalid=Camera->SwordReadyHold;Invalid.FrequencyHz=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Legacy nonfinite settings still rejected"),Invalid.IsValid());
 Invalid=Camera->SwordReadyHold;Invalid.PositionAmplitudeCm=-1.f;TestFalse(TEXT("Legacy negative amplitude still rejected"),Invalid.IsValid());
 TStrongObjectPtr<UDWGameplayCameraHoldPattern> Legacy(NewObject<UDWGameplayCameraHoldPattern>());
 Legacy->Configure(Camera->SwordReadyHold,Camera);Legacy->StartShakePattern({});const auto LegacyOutput=Advance(*Legacy,.02f);
 TestTrue(TEXT("Old native hold API cannot bypass the stopped runtime rule"),Legacy->IsFinished()&&LegacyOutput.Location.IsNearlyZero()&&LegacyOutput.Rotation.IsNearlyZero());
 Manager->UpdateCamera(0.f);const auto Baseline=Manager->GetCameraCacheView();const float Source=F.Weapon()->GetElapsedSeconds();
 Manager->UpdateCamera(.02f);const auto Impact=Manager->GetCameraCacheView();
 TestTrue(TEXT("Ready impact reaches actual final camera POV"),!Impact.Location.Equals(Baseline.Location,.0001)&&!Impact.Rotation.Equals(Baseline.Rotation,.0001));
 Manager->UpdateCamera(Camera->SwordChargeReadyPulse.DurationSeconds+1.f);Camera->TickComponent(.01f,LEVELTICK_All,nullptr);
 const auto Rest=Manager->GetCameraCacheView();
 TestTrue(TEXT("Ready impact returns to the exact unshaken camera"),Rest.Location.Equals(Baseline.Location,.0001)&&Rest.Rotation.Equals(Baseline.Rotation,.0001));
 TestEqual(TEXT("Ready pulse expires normally"),Camera->GetActiveCombatPulseCount(),0);
 TestEqual(TEXT("Camera sampling does not advance the source animation"),F.Weapon()->GetElapsedSeconds(),Source);
 F.AdvanceWeapon(1.5);Camera->TickComponent(1.5f,LEVELTICK_All,nullptr);Manager->UpdateCamera(.02f);
 TestTrue(TEXT("Long ready wait remains harmless without auto release"),F.Weapon()->IsFormalSwordWaitingForRelease());
 TestEqual(TEXT("Long ready wait does not repeat the cue"),Feedback->ChargeReadyCount,1);
 TestEqual(TEXT("Long ready wait does not repeat the impact"),Camera->CombatPulseStartCount,1);
 TestEqual(TEXT("Long ready wait never starts a legacy hold"),Camera->SwordReadyHoldStartCount,0);
 F.Weapon()->PrimaryReleased();Camera->TickComponent(.01f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Queued release still ends the ready wait before gameplay advances"),F.Weapon()->IsFormalSwordChargeReady()&&!F.Weapon()->IsFormalSwordWaitingForRelease());
 TestFalse(TEXT("Release cannot leave a continuous hold behind"),Camera->IsSwordReadyHoldActive());
 FFixture Disabled; if(!TestNotNull(TEXT("Second isolated camera"),Disabled.Camera()))return false;
 Disabled.Camera()->bCombatPulseEnabled=false;Disabled.Camera()->bSwordReadyHoldEnabled=true;
 if(!TestTrue(TEXT("Real ready state is independent of presentation toggle"),Disabled.Ready()))return false;
 TestEqual(TEXT("Ready one-shot respects the common combat-pulse switch"),Disabled.Camera()->CombatPulseStartCount,0);
 TestEqual(TEXT("Disabled pulse cannot fall back to old continuous hold"),Disabled.Camera()->SwordReadyHoldStartCount,0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordReadyImpactPauseTest,"DoughWorld.Combat.Feedback.SwordReadyImpactPauseRetainsProgress",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordReadyImpactPauseTest::RunTest(const FString&)
{
 using namespace DWSwordReadyHoldTests;FFixture F;
 if(!TestTrue(TEXT("Real ready source and local camera"),F.Ready()))return false;
 auto* Camera=F.Camera();auto* Manager=F.PC->PlayerCameraManager.Get();
 Camera->bSwordReadyHoldEnabled=true;Manager->UpdateCamera(0.f);const auto Baseline=Manager->GetCameraCacheView();
 Manager->UpdateCamera(.02f);const int32 Starts=Camera->CombatPulseStartCount;
 auto* Pauser=F.Spawn<APlayerState>();if(!TestNotNull(TEXT("Actual pauser"),Pauser))return false;
 F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);if(!TestTrue(TEXT("World actually paused"),F.World->IsPaused()))return false;
 Camera->TickComponent(100.f,LEVELTICK_All,nullptr);Manager->UpdateCamera(100.f);const auto Paused=Manager->GetCameraCacheView();
 TestTrue(TEXT("Paused ready impact outputs zero final POV offset"),Paused.Location.Equals(Baseline.Location,.0001)&&Paused.Rotation.Equals(Baseline.Rotation,.0001));
 TestEqual(TEXT("Pause retains the one finite impact"),Camera->GetActiveCombatPulseCount(),1);
 TestEqual(TEXT("Pause cannot restart the impact"),Camera->CombatPulseStartCount,Starts);
 TestEqual(TEXT("Pause cannot activate legacy continuous hold"),Camera->SwordReadyHoldStartCount,0);
 F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
 Manager->UpdateCamera(.02f);const auto Resumed=Manager->GetCameraCacheView();
 Camera->StopCombatPulses();Manager->UpdateCamera(0.f);
 if(!TestTrue(TEXT("Same public ready pulse for an unpaused real-camera control"),Camera->PlayCombatPulse(EDWCombatShakeEvent::ChargeReady)))return false;
 Manager->UpdateCamera(.02f);Manager->UpdateCamera(.02f);const auto Control=Manager->GetCameraCacheView();
 TestTrue(TEXT("Ready impact resumes at exactly the unpaused progress"),Resumed.Location.Equals(Control.Location,.0001)&&Resumed.Rotation.Equals(Control.Rotation,.0001));
 Manager->UpdateCamera(Camera->SwordChargeReadyPulse.DurationSeconds+1.f);Camera->TickComponent(.01f,LEVELTICK_All,nullptr);
 const auto Rest=Manager->GetCameraCacheView();
 TestTrue(TEXT("Resumed ready impact finishes at rest without drift"),Rest.Location.Equals(Baseline.Location,.0001)&&Rest.Rotation.Equals(Baseline.Rotation,.0001)&&Camera->GetActiveCombatPulseCount()==0);
 TestTrue(TEXT("Waiting gameplay remains ready after presentation finishes"),F.Weapon()->IsFormalSwordWaitingForRelease());
 TestEqual(TEXT("Camera control replay did not publish a new gameplay-ready cue"),F.Player->CombatFeedback->ChargeReadyCount,1);
 return true;
}

#endif
