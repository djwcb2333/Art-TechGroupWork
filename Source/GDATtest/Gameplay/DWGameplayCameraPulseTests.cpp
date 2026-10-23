#if WITH_DEV_AUTOMATION_TESTS
#include "DWGameplayCameraShake.h"
#include "DWCombatTestWorld.h"
#include "DWPlayerCharacter.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace DWCameraPulseTests
{
struct FFixture
{
 TStrongObjectPtr<UDWGameInstance> GI;
 UWorld* World=nullptr;
 TStrongObjectPtr<ULocalPlayer> CameraLocalPlayer;
 ADWPlayerCharacter* CameraPlayer=nullptr;APlayerController* CameraPC=nullptr;
 bool CreateRealCameraPlayer()
 {
  if(!World||!GEngine)return false;
  FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  CameraPlayer=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Params);
  if(CameraPlayer&&!CameraPlayer->HasActorBegunPlay())CameraPlayer->DispatchBeginPlay();
  CameraPC=World->SpawnActor<APlayerController>(APlayerController::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
  if(CameraPC&&!CameraPC->HasActorBegunPlay())CameraPC->DispatchBeginPlay();
  if(!CameraPlayer||!CameraPC)return false;
  CameraPC->SetAsLocalPlayerController();
  // Unlike pattern/count tests, UpdateCamera's default client-side gate also needs
  // a real ULocalPlayer association. Use the engine's normal SetPlayer route.
  // This isolated player is not registered with a live viewport or user's session.
  CameraLocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine));CameraPC->SetPlayer(CameraLocalPlayer.Get());
  CameraPC->Possess(CameraPlayer);CameraPC->SetViewTarget(CameraPlayer);
  CameraPlayer->SetActorTickEnabled(false);CameraPC->SetActorTickEnabled(false);
  return CameraPlayer->HasCombatVitalsReady()&&CameraPlayer->GameplayCameraShake&&CameraPC->IsLocalController()&&
   CameraPC->Player==CameraLocalPlayer.Get()&&CameraPC->GetPawn()==CameraPlayer&&CameraPC->GetViewTarget()==CameraPlayer&&CameraPC->PlayerCameraManager;
 }
 FFixture(){World=DWCombatTestWorld::Create(GI,TEXT("D23CameraPulse"));}
 ~FFixture()
 {
  if(!World)return;
  World->GetWorldSettings()->SetPauserPlayerState(nullptr);
  for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
  GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 }
};
FCameraShakePatternUpdateResult Advance(UDWGameplayCameraPulsePattern& Pattern,float Seconds)
{
 FCameraShakePatternUpdateParams Params;Params.DeltaTime=Seconds;
 FCameraShakePatternUpdateResult Result;Pattern.UpdateShakePattern(Params,Result);return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCameraPulseFiniteTest,"DoughWorld.Combat.Feedback.CameraPulseFiniteAndConfiguration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCameraPulseFiniteTest::RunTest(const FString&)
{
 using namespace DWCameraPulseTests;FFixture F;
 if(!TestNotNull(TEXT("Isolated initialized world"),F.World))return false;
 const auto* Defaults=GetDefault<UDWGameplayCameraShakeComponent>();
 TestTrue(TEXT("All four event settings are valid"),Defaults->SwordAttackPulse.IsValid()&&Defaults->BlockPulse.IsValid()&&Defaults->PerfectBlockPulse.IsValid()&&Defaults->SwordChargeReadyPulse.IsValid());
 TestTrue(TEXT("Perfect block is visibly stronger in both amplitude controls"),Defaults->PerfectBlockPulse.PositionAmplitudeCm>Defaults->BlockPulse.PositionAmplitudeCm&&Defaults->PerfectBlockPulse.RotationAmplitudeDegrees>Defaults->BlockPulse.RotationAmplitudeDegrees);
 auto Invalid=Defaults->SwordAttackPulse;Invalid.DurationSeconds=0.f;TestFalse(TEXT("Zero duration rejected"),Invalid.IsValid());
 Invalid=Defaults->SwordAttackPulse;Invalid.FrequencyHz=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Nonfinite frequency rejected"),Invalid.IsValid());
 Invalid=Defaults->SwordAttackPulse;Invalid.PositionAmplitudeCm=-1.f;TestFalse(TEXT("Negative amplitude rejected"),Invalid.IsValid());
 TStrongObjectPtr<UDWGameplayCameraPulsePattern> Pulse(NewObject<UDWGameplayCameraPulsePattern>());
 Pulse->Configure(Defaults->SwordAttackPulse,F.World);Pulse->StartShakePattern({});
 const auto During=Advance(*Pulse,.023f);
 TestTrue(TEXT("Finite pulse produces additive position and rotation"),!During.Location.IsNearlyZero()&&!During.Rotation.IsNearlyZero());
 TestTrue(TEXT("Envelope stays within configured bounds"),FMath::Abs(During.Location.Y)<=Defaults->SwordAttackPulse.PositionAmplitudeCm&&FMath::Abs(During.Location.Z)<=Defaults->SwordAttackPulse.PositionAmplitudeCm);
 const auto End=Advance(*Pulse,Defaults->SwordAttackPulse.DurationSeconds+1.f);
 TestTrue(TEXT("Pulse ends with zero offset and finished state"),Pulse->IsFinished()&&End.Location.IsNearlyZero()&&End.Rotation.IsNearlyZero());
 const auto Late=Advance(*Pulse,10.f);TestTrue(TEXT("Finished pulse cannot restart"),Pulse->IsFinished()&&Late.Location.IsNearlyZero());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCameraPulsePauseTest,"DoughWorld.Combat.Feedback.CameraPulsePauseRetainsRemainingProgress",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCameraPulsePauseTest::RunTest(const FString&)
{
 using namespace DWCameraPulseTests;FFixture F;
 if(!TestNotNull(TEXT("Isolated initialized world"),F.World))return false;
 TStrongObjectPtr<UDWGameplayCameraPulsePattern> Pulse(NewObject<UDWGameplayCameraPulsePattern>()),Reference(NewObject<UDWGameplayCameraPulsePattern>());
 FDWGameplayCameraPulseSettings Settings;Pulse->Configure(Settings,F.World);Reference->Configure(Settings,F.World);
 Pulse->StartShakePattern({});Reference->StartShakePattern({});Advance(*Pulse,.02f);
 FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Pauser=F.World->SpawnActor<APlayerState>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
 if(!TestNotNull(TEXT("Real world pause state"),Pauser))return false;
 F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
 if(!TestTrue(TEXT("World is actually paused"),F.World->IsPaused()))return false;
 const auto Paused=Advance(*Pulse,100.f);
 TestTrue(TEXT("Pause outputs zero offset and preserves pulse"),Paused.Location.IsNearlyZero()&&Paused.Rotation.IsNearlyZero()&&!Pulse->IsFinished());
 F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
 const auto Resumed=Advance(*Pulse,.02f),Expected=Advance(*Reference,.04f);
 TestTrue(TEXT("Resume continues from elapsed game time only"),Resumed.Location.Equals(Expected.Location,.00001)&&Resumed.Rotation.Equals(Expected.Rotation,.00001));
 Pulse->StopShakePattern({});TestTrue(TEXT("Explicit stop is terminal"),Pulse->IsFinished());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCameraPulseFirstFrameTest,"DoughWorld.Combat.Feedback.CameraPulseLongFirstFrameStillRenders",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCameraPulseFirstFrameTest::RunTest(const FString&)
{
 using namespace DWCameraPulseTests;FFixture F;
 if(!TestNotNull(TEXT("Isolated initialized world"),F.World))return false;
 const auto* Defaults=GetDefault<UDWGameplayCameraShakeComponent>();
 const FDWGameplayCameraPulseSettings Presets[]={Defaults->SwordAttackPulse,Defaults->BlockPulse,Defaults->PerfectBlockPulse,Defaults->SwordChargeReadyPulse};
 for(const auto& Settings:Presets)
 {
  TStrongObjectPtr<UDWGameplayCameraPulsePattern> Pulse(NewObject<UDWGameplayCameraPulsePattern>());
  Pulse->Configure(Settings,F.World);Pulse->StartShakePattern({});
  const auto Invalid=Advance(*Pulse,std::numeric_limits<float>::quiet_NaN());
  TestTrue(TEXT("Invalid camera delta cannot consume the first visible sample"),!Pulse->IsFinished()&&Invalid.Location.IsNearlyZero()&&Invalid.Rotation.IsNearlyZero());
  const auto First=Advance(*Pulse,1.f);
  TestTrue(TEXT("All four presets remain live after a long first camera delta"),!Pulse->IsFinished());
  TestTrue(TEXT("First camera evaluation contains a visible position and rotation offset"),!First.Location.IsNearlyZero()&&!First.Rotation.IsNearlyZero());
  TestTrue(TEXT("First sample stays within configured position and rotation amplitudes"),
   FMath::Abs(First.Location.Y)<=Settings.PositionAmplitudeCm&&FMath::Abs(First.Location.Z)<=Settings.PositionAmplitudeCm&&
   FMath::Abs(First.Rotation.Pitch)<=Settings.RotationAmplitudeDegrees&&FMath::Abs(First.Rotation.Yaw)<=Settings.RotationAmplitudeDegrees&&FMath::Abs(First.Rotation.Roll)<=Settings.RotationAmplitudeDegrees);
  const auto End=Advance(*Pulse,Settings.DurationSeconds);
  TestTrue(TEXT("A later real delta still completes at zero without extending configured duration"),Pulse->IsFinished()&&End.Location.IsNearlyZero()&&End.Rotation.IsNearlyZero());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCameraPulseActualPOVTest,"DoughWorld.Combat.Feedback.CameraPulseChangesRealCameraManagerPOV",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCameraPulseActualPOVTest::RunTest(const FString&)
{
 using namespace DWCameraPulseTests;FFixture F;
 if(!TestTrue(TEXT("Transient real local player, possessed hero and actual camera manager"),F.CreateRealCameraPlayer()))return false;
 auto* Camera=F.CameraPlayer->GameplayCameraShake.Get();auto* Manager=F.CameraPC->PlayerCameraManager.Get();
 const FVector HeroLocation=F.CameraPlayer->GetActorLocation();const FRotator HeroRotation=F.CameraPlayer->GetActorRotation();
 Manager->UpdateCamera(0.f);const FMinimalViewInfo Baseline=Manager->GetCameraCacheView();
 Manager->UpdateCamera(.02f);const FMinimalViewInfo Stable=Manager->GetCameraCacheView();
 if(!TestTrue(TEXT("Static real hero produces stable unshaken camera baseline"),Stable.Location.Equals(Baseline.Location,.0001)&&Stable.Rotation.Equals(Baseline.Rotation,.0001)))return false;
 if(!TestTrue(TEXT("Public configured sword pulse accepted by the real local camera"),Camera->PlayCombatPulse(EDWCombatShakeEvent::SwordAttack)))return false;
 Manager->UpdateCamera(.02f);const FMinimalViewInfo During=Manager->GetCameraCacheView();
 const double PositionDelta=FVector::Distance(During.Location,Baseline.Location);
 const FRotator RotationDelta=(During.Rotation-Baseline.Rotation).GetNormalized();
 TestTrue(TEXT("Camera manager final POV position actually changes"),FMath::IsFinite(PositionDelta)&&PositionDelta>.0001);
 TestTrue(TEXT("Camera manager final POV rotation actually changes"),!RotationDelta.IsNearlyZero(.0001));
 TestTrue(TEXT("Underlying actor stays static while only final camera POV changes"),F.CameraPlayer->GetActorLocation().Equals(HeroLocation,.0001)&&F.CameraPlayer->GetActorRotation().Equals(HeroRotation,.0001));
 TestEqual(TEXT("Exactly one active real pulse before pause"),Camera->GetActiveCombatPulseCount(),1);
 FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Pauser=F.World->SpawnActor<APlayerState>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
 if(!TestNotNull(TEXT("Actual world pauser"),Pauser))return false;
 F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
 if(!TestTrue(TEXT("Camera is evaluated during a real paused world"),F.World->IsPaused()))return false;
 Manager->UpdateCamera(5.f);const FMinimalViewInfo Paused=Manager->GetCameraCacheView();
 TestTrue(TEXT("Paused manager output contains no combat offset"),Paused.Location.Equals(Baseline.Location,.0001)&&Paused.Rotation.Equals(Baseline.Rotation,.0001));
 TestEqual(TEXT("Long paused camera evaluation does not expire the owned pulse"),Camera->GetActiveCombatPulseCount(),1);
 F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
 Manager->UpdateCamera(.02f);const FMinimalViewInfo Resumed=Manager->GetCameraCacheView();
 // Control replay through the same actual camera manager, not direct pattern math.
 Camera->StopCombatPulses();Manager->UpdateCamera(0.f);
 if(!TestTrue(TEXT("Fresh public pulse for unpaused control"),Camera->PlayCombatPulse(EDWCombatShakeEvent::SwordAttack)))return false;
 Manager->UpdateCamera(.02f);Manager->UpdateCamera(.02f);const FMinimalViewInfo Control=Manager->GetCameraCacheView();
 TestTrue(TEXT("Resumed actual POV equals a pulse advanced only by the two unpaused steps"),Resumed.Location.Equals(Control.Location,.0001)&&Resumed.Rotation.Equals(Control.Rotation,.0001));
 Manager->UpdateCamera(Camera->SwordAttackPulse.DurationSeconds+1.f);const FMinimalViewInfo Expired=Manager->GetCameraCacheView();
 TestTrue(TEXT("Configured duration restores real final POV to unshaken baseline"),Expired.Location.Equals(Baseline.Location,.0001)&&Expired.Rotation.Equals(Baseline.Rotation,.0001));
 Camera->TickComponent(0.f,LEVELTICK_All,nullptr);TestEqual(TEXT("Expired real pulse pruned"),Camera->GetActiveCombatPulseCount(),0);
 Manager->UpdateCamera(.02f);const FMinimalViewInfo Later=Manager->GetCameraCacheView();
 TestTrue(TEXT("No lingering camera drift after expiry"),Later.Location.Equals(Baseline.Location,.0001)&&Later.Rotation.Equals(Baseline.Rotation,.0001));
 AddInfo(FString::Printf(TEXT("Actual PCM: baseline=(%s)/(%s), first location delta=%.6fcm, rotation delta=(%s), paused=(%s), resumed=(%s), expired=(%s)"),
  *Baseline.Location.ToString(),*Baseline.Rotation.ToString(),PositionDelta,*RotationDelta.ToString(),*Paused.Location.ToString(),*Resumed.Location.ToString(),*Expired.Location.ToString()));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCameraPulseDampedImpactTest,"DoughWorld.Combat.Feedback.CameraPulseSingleDampedImpact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCameraPulseDampedImpactTest::RunTest(const FString&)
{
 using namespace DWCameraPulseTests;FFixture F;
 if(!TestNotNull(TEXT("Isolated initialized world"),F.World))return false;
 const auto* Defaults=GetDefault<UDWGameplayCameraShakeComponent>();
 const FDWGameplayCameraPulseSettings Presets[]={Defaults->SwordAttackPulse,Defaults->BlockPulse,Defaults->PerfectBlockPulse,Defaults->SwordChargeReadyPulse};
 for(const auto& Settings:Presets)
 {
  TStrongObjectPtr<UDWGameplayCameraPulsePattern> Pulse(NewObject<UDWGameplayCameraPulsePattern>());
  Pulse->Configure(Settings,F.World);Pulse->StartShakePattern({});
  const float Step=Settings.DurationSeconds/64.f;
  const auto First=Advance(*Pulse,Step);
  const FVector PositionDirection=First.Location.GetSafeNormal();
  const FVector FirstRotation(First.Rotation.Pitch,First.Rotation.Yaw,First.Rotation.Roll);
  const FVector RotationDirection=FirstRotation.GetSafeNormal();
  TestTrue(TEXT("All presets start with a real one-off position and angular kick"),!First.Location.IsNearlyZero()&&!FirstRotation.IsNearlyZero());
  double PreviousPosition=First.Location.Size(),PreviousRotation=FirstRotation.Size();
  double Quarter=0.,Half=0.,ThreeQuarters=0.;
  for(int32 Sample=2;Sample<64;++Sample)
  {
   const auto Current=Advance(*Pulse,Step);
   const FVector Rotation(Current.Rotation.Pitch,Current.Rotation.Yaw,Current.Rotation.Roll);
   const double PositionSize=Current.Location.Size(),RotationSize=Rotation.Size();
   TestTrue(TEXT("Single impact position has no secondary peak"),PositionSize<=PreviousPosition+1.e-8);
   TestTrue(TEXT("Single impact rotation has no secondary peak"),RotationSize<=PreviousRotation+1.e-8);
   TestTrue(TEXT("Position returns along the same side of the rest point"),FVector::DotProduct(Current.Location,PositionDirection)>=-1.e-8&&FVector::CrossProduct(Current.Location,PositionDirection).Size()<=1.e-7);
   TestTrue(TEXT("Angular return never changes sign or direction"),FVector::DotProduct(Rotation,RotationDirection)>=-1.e-8&&FVector::CrossProduct(Rotation,RotationDirection).Size()<=1.e-7);
   TestTrue(TEXT("Position remains within the configured amplitude"),PositionSize<=Settings.PositionAmplitudeCm+1.e-7);
   PreviousPosition=PositionSize;PreviousRotation=RotationSize;
   if(Sample==16)Quarter=PositionSize;
   if(Sample==32)Half=PositionSize;
   if(Sample==48)ThreeQuarters=PositionSize;
  }
  TestTrue(TEXT("Return is a sustained smooth falloff rather than an immediate cutoff"),Quarter>Half&&Half>ThreeQuarters&&ThreeQuarters>0.);
  const auto End=Advance(*Pulse,Settings.DurationSeconds);
  TestTrue(TEXT("Finite impact completes at the exact resting offset"),Pulse->IsFinished()&&End.Location.IsZero()&&End.Rotation.IsZero());
  const auto Later=Advance(*Pulse,10.f);
  TestTrue(TEXT("No lingering drift or later impact restart"),Pulse->IsFinished()&&Later.Location.IsZero()&&Later.Rotation.IsZero());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCameraPulseReturnStiffnessTest,"DoughWorld.Combat.Feedback.CameraPulseReturnStiffnessControlsFalloff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCameraPulseReturnStiffnessTest::RunTest(const FString&)
{
 using namespace DWCameraPulseTests;FFixture F;
 if(!TestNotNull(TEXT("Isolated initialized world"),F.World))return false;
 FDWGameplayCameraPulseSettings Slow(.4f,3.f,.35f,4.f),Fast=Slow;Fast.FrequencyHz=40.f;
 TStrongObjectPtr<UDWGameplayCameraPulsePattern> SlowPulse(NewObject<UDWGameplayCameraPulsePattern>()),FastPulse(NewObject<UDWGameplayCameraPulsePattern>());
 SlowPulse->Configure(Slow,F.World);FastPulse->Configure(Fast,F.World);
 SlowPulse->StartShakePattern({});FastPulse->StartShakePattern({});
 const auto SlowAtQuarter=Advance(*SlowPulse,.1f),FastAtQuarter=Advance(*FastPulse,.1f);
 TestTrue(TEXT("Both stiffness settings preserve a nonzero first visible impact"),!SlowAtQuarter.Location.IsNearlyZero()&&!FastAtQuarter.Location.IsNearlyZero());
 const FVector SlowAngular(SlowAtQuarter.Rotation.Pitch,SlowAtQuarter.Rotation.Yaw,SlowAtQuarter.Rotation.Roll);
 const FVector FastAngular(FastAtQuarter.Rotation.Pitch,FastAtQuarter.Rotation.Yaw,FastAtQuarter.Rotation.Roll);
 TestTrue(TEXT("Larger legacy FrequencyHz now settles position and angle sooner"),FastAtQuarter.Location.Size()<SlowAtQuarter.Location.Size()&&FastAngular.Size()<SlowAngular.Size());
 TestTrue(TEXT("Stiffness changes only falloff, not impact direction"),FastAtQuarter.Location.GetSafeNormal().Equals(SlowAtQuarter.Location.GetSafeNormal(),1.e-7));
 FDWGameplayCameraPulseSettings Invalid=Slow;Invalid.FrequencyHz=0.f;
 TestFalse(TEXT("Zero inverse-second return stiffness remains invalid"),Invalid.IsValid());
 const auto SlowEnd=Advance(*SlowPulse,1.f),FastEnd=Advance(*FastPulse,1.f);
 TestTrue(TEXT("Both stiffness settings keep the same finite duration and zero rest point"),SlowPulse->IsFinished()&&FastPulse->IsFinished()&&SlowEnd.Location.IsZero()&&FastEnd.Location.IsZero()&&SlowEnd.Rotation.IsZero()&&FastEnd.Rotation.IsZero());
 return true;
}

#endif
