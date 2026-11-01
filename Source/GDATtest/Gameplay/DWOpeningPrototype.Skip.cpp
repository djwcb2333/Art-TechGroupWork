#include "DWOpeningPrototype.h"
#include "DWGameInstance.h"
#include "DWPlayerController.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWGameplayCinematic.h"
#include "Components/InputComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Camera/CameraActor.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"

DWOpeningHoldSkip::Segment ADWOpeningPrototype::HoldSkipSegment()const
{
 using S=DWOpeningHoldSkip::Segment;
 switch(Stage){case EDWOpeningStage::Wake:return S::Wake;case EDWOpeningStage::Stretch:return S::Stretch;
 case EDWOpeningStage::Reveal:return S::Reveal;case EDWOpeningStage::Encounter:return S::Encounter;default:return S::None;}
}
bool ADWOpeningPrototype::HoldSkipFocused()const
{
 if(!PC.IsValid()||PC->IsPaused()||!FSlateApplication::IsInitialized()||!FSlateApplication::Get().IsActive())return false;
 const ULocalPlayer* Local=PC->GetLocalPlayer();
 return Local&&Local->ViewportClient&&Local->ViewportClient->Viewport&&Local->ViewportClient->Viewport->HasFocus();
}
bool ADWOpeningPrototype::HoldSkipEligible()const
{
 return bAllowHoldToSkip&&FMath::IsFinite(HoldToSkipSeconds)&&HoldToSkipSeconds>0.f&&HoldToSkipSeconds<=30.f
  &&HoldSkipSegment()!=DWOpeningHoldSkip::Segment::None&&bLocked&&bPresentationInitialized&&!bPresentationCleaned&&!bDialogueFailure
  &&GetWorld()&&!GetWorld()->bIsTearingDown&&!IsActorBeingDestroyed()
  &&Player.IsValid()&&PC.IsValid()&&!Player->IsActorBeingDestroyed()&&!PC->IsActorBeingDestroyed()
  &&!Player->IsDead()&&!Player->IsCombatRestoreInProgress()
  &&PC->GetPawn()==Player.Get()&&Player->GetWorld()==GetWorld()&&PC->GetWorld()==GetWorld();
}
void ADWOpeningPrototype::InvalidateOpeningHoldSkip(){HoldSkipClock.Invalidate();}
void ADWOpeningPrototype::ResetOpeningHoldSkipForStage()
{
 HoldSkipClock.NewScene(++HoldSkipGeneration);
 if(HoldSkipFocused()&&PC.IsValid()&&!PC->IsInputKeyDown(EKeys::SpaceBar))HoldSkipClock.Release();
 if(Stage==EDWOpeningStage::Stretch){StretchCrumbsApplied=StretchFloorApplied=0.f;}
 if(Stage==EDWOpeningStage::Reveal){RevealBiteApplied=0.f;RevealBiteGround.Reset();}
 if(Overlay)Overlay->SetHoldSkipHint(false,0.f,HoldToSkipSeconds);
}
void ADWOpeningPrototype::OpeningSkipPressed()
{
 if(bHoldSkipPressHeld)return;
 bHoldSkipPressHeld=true;
 HoldSkipClock.Press(FPlatformTime::Seconds(),HoldSkipEligible()&&HoldSkipFocused());
}
void ADWOpeningPrototype::OpeningSkipReleased()
{bHoldSkipPressHeld=false;HoldSkipClock.Release();}

bool ADWOpeningPrototype::TickOpeningHoldSkip()
{
 if(!bPresentationInitialized||bPresentationCleaned||!PC.IsValid())
 {if(OpeningSkipInput)ShutdownOpeningHoldSkip();else InvalidateOpeningHoldSkip();return false;}
 if(!HoldSkipActivationHandle.IsValid()&&FSlateApplication::IsInitialized())
 {
  // Observe loss even if the actor is not ticking between loss and regain.
  HoldSkipActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddWeakLambda(this,[this](bool){InvalidateOpeningHoldSkip();});
  HoldSkipFocusHandle=FSlateApplication::Get().OnFocusChanging().AddWeakLambda(this,[this](const FFocusEvent&,const FWeakWidgetPath&,const TSharedPtr<SWidget>&,const FWidgetPath&,const TSharedPtr<SWidget>&){InvalidateOpeningHoldSkip();});
 }
 const bool Focused=HoldSkipFocused();
 const bool Down=PC->IsInputKeyDown(EKeys::SpaceBar);
 if(Focused&&!Down)bHoldSkipPressHeld=false;
 const bool Eligible=HoldSkipEligible();
 const bool NeedInput=Eligible||bHoldSkipPressHeld;
 if(NeedInput)
 {
  if(!OpeningSkipInput)
  {
   OpeningSkipInput=NewObject<UInputComponent>(this,NAME_None,RF_Transient);OpeningSkipInput->RegisterComponent();OpeningSkipInput->Priority=10001;OpeningSkipInput->bBlockInput=false;
   auto& Press=OpeningSkipInput->BindKey(EKeys::SpaceBar,IE_Pressed,this,&ThisClass::OpeningSkipPressed);Press.bConsumeInput=true;Press.bExecuteWhenPaused=true;
   auto& Release=OpeningSkipInput->BindKey(EKeys::SpaceBar,IE_Released,this,&ThisClass::OpeningSkipReleased);Release.bConsumeInput=true;Release.bExecuteWhenPaused=true;
  }
  if(!bHoldSkipInputPushed){PC->PushInputComponent(OpeningSkipInput);HoldSkipLeaseController=PC;bHoldSkipInputPushed=true;}
 }
 else if(bHoldSkipInputPushed)
 {
  if(HoldSkipLeaseController.IsValid())HoldSkipLeaseController->PopInputComponent(OpeningSkipInput);
  bHoldSkipInputPushed=false;HoldSkipLeaseController.Reset();
 }
 const bool Fire=HoldSkipClock.Tick(HoldSkipGeneration,FPlatformTime::Seconds(),HoldToSkipSeconds,Eligible&&Focused,Down&&bHoldSkipPressHeld);
 if(Overlay)Overlay->SetHoldSkipHint(Eligible&&Focused,float(HoldSkipClock.Progress()),HoldToSkipSeconds);
 return Fire?CompleteHeldOpeningScene():false;
}
void ADWOpeningPrototype::ShutdownOpeningHoldSkip()
{
 InvalidateOpeningHoldSkip();bHoldSkipPressHeld=false;
 if(bHoldSkipInputPushed&&HoldSkipLeaseController.IsValid()&&OpeningSkipInput)HoldSkipLeaseController->PopInputComponent(OpeningSkipInput);
 bHoldSkipInputPushed=false;HoldSkipLeaseController.Reset();
 if(OpeningSkipInput){OpeningSkipInput->DestroyComponent();OpeningSkipInput=nullptr;}
 if(FSlateApplication::IsInitialized())
 {
  if(HoldSkipActivationHandle.IsValid())FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(HoldSkipActivationHandle);
  if(HoldSkipFocusHandle.IsValid())FSlateApplication::Get().OnFocusChanging().Remove(HoldSkipFocusHandle);
 }
 HoldSkipActivationHandle.Reset();HoldSkipFocusHandle.Reset();
 if(Overlay)Overlay->SetHoldSkipHint(false,0.f,HoldToSkipSeconds);
}

bool ADWOpeningPrototype::CompleteHeldOpeningScene()
{
 if(!HoldSkipEligible()||!HoldSkipFocused())return false;
 auto* GI=GetGameInstance<UDWGameInstance>();
 if(!GI||!InputGate||!IsValid(ShotCamera)||!GI->IsOpeningDirectorCurrent(this,Player.Get()))return false;
 const EDWOpeningStage From=Stage;
 if(From==EDWOpeningStage::Encounter&&(!Enemies.IsValidIndex(0)||!IsValid(Enemies[0].Get())||Enemies[0]->IsActorBeingDestroyed()||!EnemyPoses.IsValidIndex(0)))
 {FailOpeningDialogue(TEXT("Cannot settle skipped encounter: its actor is no longer valid."));return true;}
 // Silence the skipped presentation; never replay skipped sounds or manufacture combat hits.
 for(auto& Sound:OpeningOneShots)if(auto* Audio=Sound.Get()){Audio->Stop();if(IsValid(Audio))Audio->DestroyComponent();}
 OpeningOneShots.Reset();RestoreTimeDilation();HitStop=0.f;Trauma=0.f;Recoil=.4f;
 StopOpeningDialogue(); // Existing reentrancy guard suppresses cancellation-as-failure.
 if(Overlay){Overlay->SetBlack(0.f);Overlay->SetSubtitle(TEXT(""),true);Overlay->SetCenterLine(TEXT(""));Overlay->SetPrompt(TEXT(""));}
 switch(From)
 {
 case EDWOpeningStage::Wake:
  // The cancelled sequence still exists here, permitting the original evaluator's exact endpoint.
  BuildWake(true);PlayShots();if(Shots.Num())TickShots(Shots[0].Duration);
  if(!Ambience)StartOpeningAmbience(true); // Persistent ambience is part of the normal post-wake state.
  OpeningSequence.Reset();Shots.Reset();ShotIndex=INDEX_NONE;
  Enter(EDWOpeningStage::StretchPrompt);break;
 case EDWOpeningStage::Stretch:
 {
  const float CrumbR=float(DWOpeningHoldSkip::Remaining(StretchCrumbsApplied,1.2));
  for(int32 I=0;I<Crumbs.Num();++I)if(IsValid(Crumbs[I])&&CrumbHome.IsValidIndex(I))
   Crumbs[I]->AddActorWorldOffset((PitCenter-CrumbHome[I]).GetSafeNormal2D()*CrumbR*9.f+FVector(0,0,-CrumbR*15.f));
  const float FloorR=float(DWOpeningHoldSkip::Remaining(StretchFloorApplied,1.));
  for(auto A:PitFloor)if(IsValid(A))A->AddActorWorldOffset(FVector(0,0,-FloorR*9.f));
  StretchCrumbsApplied=FMath::Max(1.2f,StretchCrumbsApplied);StretchFloorApplied=FMath::Max(1.f,StretchFloorApplied);
  bDoubleActive=false;ShowDouble(false);RestoreDoubleVisibility();RestoreHeroPose();
  StartedDialogueBeats.Add(TEXT("SinkThought"));
  OpeningSequence.Reset();Shots.Reset();ShotIndex=INDEX_NONE;CutToGameplay();Enter(EDWOpeningStage::Escape);break;
 }
 case EDWOpeningStage::Reveal:
 {
  AActor* Ground=RevealBiteGround.Get();
  if(!Ground)
  {
   const FVector First=Enemies.IsValidIndex(0)&&IsValid(Enemies[0].Get())?Enemies[0]->GetActorLocation():EnemyCenter;
   for(auto A:FeedingGround)if(IsValid(A)&&(!Ground||FVector::Dist2D(A->GetActorLocation(),First)<FVector::Dist2D(Ground->GetActorLocation(),First)))Ground=A;
  }
  const float Rest=float(DWOpeningHoldSkip::Remaining(RevealBiteApplied,.35));
  if(IsValid(Ground)){Ground->AddActorWorldOffset(FVector(0,0,-Rest*40.f));RevealBiteGround=Ground;}
  RevealBiteApplied=FMath::Max(.35f,RevealBiteApplied);
  OpeningSequence.Reset();Shots.Reset();ShotIndex=INDEX_NONE;CutToGameplay();Enter(EDWOpeningStage::Approach);break;
 }
 case EDWOpeningStage::Encounter:
 {
  auto* Enemy=Enemies[0].Get();auto& Pose=EnemyPoses[0];
  const FVector HeroFeet=Feet(Player.Get()),EnemyFeet=Feet(Enemy);
  const FVector Direction=(EnemyFeet-HeroFeet).GetSafeNormal2D();
  const bool HasAuthoredLanding=Pose.bFree||PreparedSequenceStep==DWOpeningSteps::Step::EncounterFace;
  const FVector Landing=HasAuthoredLanding?LeapEnd:FVector(HeroFeet.X+Direction.X*190.f*Unit,HeroFeet.Y+Direction.Y*190.f*Unit,Enemy->GetActorLocation().Z);
  Pose.Yaw=(HeroFeet-EnemyFeet).Rotation().Yaw;Pose.bFree=true;
  for(auto& P:EnemyPoses)P.bFeeding=false;
  Enemy->SetActorRotation(FRotator(0,Pose.Yaw,0));Enemy->SetActorLocation(Landing,false,nullptr,ETeleportType::TeleportPhysics);
  RestoreHeroPose();OpeningSequence.Reset();Shots.Reset();ShotIndex=INDEX_NONE;
  Enter(EDWOpeningStage::Combat);break; // Original entry restores pose/AI/control/damage/music/HUD.
 }
 default:return false;
 }
 UE_LOG(LogTemp,Display,TEXT("DW_OPENING_HOLD_SKIP from=%d to=%d held_seconds=%.2f"),int32(From),int32(Stage),HoldToSkipSeconds);
 // Keep the consuming Space binding until Up, including the first gameplay frame.
 if(Overlay)Overlay->SetHoldSkipHint(false,0.f,HoldToSkipSeconds);
 return true;
}
