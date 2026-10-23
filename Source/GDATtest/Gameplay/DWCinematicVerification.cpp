#include "DWGameInstance.h"
#include "DWCinematicDemo.h"
#include "DWGameplayCinematic.h"
#include "DWWorldEvent.h"
#include "DWUIOffscreenComponent.h"
#include "DWLoadingTransition.h"
#include "DWGameplayConfig.h"
#include "DWGameplayHUD.h"
#include "DWGameplayWidget.h"
#include "DWC01HUDAnchor.h"
#include "DWPlayerCharacter.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
struct FCutsceneVerification
{
 TWeakObjectPtr<UDWGameInstance> GI;
 FString Report,Demo=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DWCinematicDemo"),Away=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GameplayPrototype");
 int32 Step=0;double Started=FPlatformTime::Seconds(),At=Started;bool Success=true,ShotScene=false,ShotHold=false;
 FVector LockedLocation;TSet<int32> Phases;TArray<TSharedPtr<FJsonValue>> Checks;
 TArray<TWeakObjectPtr<UDWUIOffscreenComponent>> ExpectedHUD;
 TWeakObjectPtr<UDWC01HUDAnchor> ExpectedC01Anchor;
 bool ExpectedCursor=false,UIOnly=false;
 void Check(const FString& Name,bool Pass){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),Name);O->SetBoolField(TEXT("passed"),Pass);Checks.Add(MakeShared<FJsonValueObject>(O));Success&=Pass;UE_LOG(LogTemp,Display,TEXT("DW_CUT_QA %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),*Name);}
 // Capture each constructed component before this playback, not a fixed legacy count.
 void CaptureHUD(APlayerController* PC)
 {
  ExpectedHUD.Reset();const auto Current=UDWUIOffscreenComponent::FindForPlayer(PC);
  int32 C01AnchorCount=0;for(auto* U:Current){ExpectedHUD.Add(U);if(Cast<UDWC01HUDAnchor>(U->GetOwner().Get()))++C01AnchorCount;}
  auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD());auto* Screen=HUD?HUD->GetGameplayWidget():nullptr;
  auto* Anchor=Screen?Cast<UDWC01HUDAnchor>(Screen->GetWidgetFromName(TEXT("C01HUDCinematicAnchor"))):nullptr;
  ExpectedC01Anchor=Anchor;ExpectedCursor=PC->bShowMouseCursor;
  Check(TEXT("Constructed cinematic HUD set is nonempty"),!ExpectedHUD.IsEmpty());
  Check(TEXT("Exactly one current C01 HUD anchor participates"),Screen&&Screen->bUseC01HUD&&Anchor&&Anchor->Offscreen&&Current.Contains(Anchor->Offscreen.Get())&&C01AnchorCount==1);
 }
 bool HUDMatches(APlayerController* PC,bool Hidden)const
 {
  const auto Current=UDWUIOffscreenComponent::FindForPlayer(PC);
  auto* Anchor=ExpectedC01Anchor.Get();
  if(ExpectedHUD.IsEmpty()||Current.Num()!=ExpectedHUD.Num()||!Anchor||!Anchor->Offscreen||!Current.Contains(Anchor->Offscreen.Get()))return false;
  for(const auto& Weak:ExpectedHUD)
  {
   auto* U=Weak.Get();if(!U||!Current.Contains(U)||U->IsAnimating())return false;
   if(Hidden){if(!U->IsOffscreen())return false;}
   else if(U->IsOffscreen()||!U->GetCurrentOffset().IsNearlyZero())return false;
  }
  // Native C01 drawing reads this component's offset, so hidden must move it.
  return !Hidden||!Anchor->Offscreen->GetCurrentOffset().IsNearlyZero();
 }
 void Shot(const FString& Name){FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(Report),TEXT("images"),Name+TEXT(".png")),true,false);}
 bool Finish(const FString& Error=FString())
 {
  if(!Error.IsEmpty())Check(Error,false);auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("passed"),Success);O->SetStringField(TEXT("mode"),FString(GIsEditor?TEXT("Editor"):TEXT("Standalone -game"))+(UIOnly?TEXT(" / DWCinematicUIOnly"):TEXT("")));O->SetArrayField(TEXT("checks"),Checks);O->SetNumberField(TEXT("seconds"),FPlatformTime::Seconds()-Started);
  O->SetBoolField(TEXT("ui_only"),UIOnly);
  O->SetStringField(TEXT("scope"),UIOnly?
   TEXT("Cinematic UI playback, HUD offscreen/return, input/cursor restoration, one-shot guard and cancellation on isolated fresh slots. Cross-map persistence and the positive save/load chain are skipped; this is not a save compatibility test."):
   TEXT("Original full cinematic scenario including cross-map persistence and save/load checks."));
  TArray<TSharedPtr<FJsonValue>> Skipped;
  if(UIOnly){Skipped.Add(MakeShared<FJsonValueString>(TEXT("Demo cross-map persistence and positive save/load chain")));Skipped.Add(MakeShared<FJsonValueString>(TEXT("Load slot A after cancellation and cross-slot persistence assertions")));}
  O->SetArrayField(TEXT("skipped"),Skipped);
  TArray<TSharedPtr<FJsonValue>> P;for(int32 Phase:Phases)P.Add(MakeShared<FJsonValueNumber>(Phase));O->SetArrayField(TEXT("phases"),P);
  FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(O,Writer);FFileHelper::SaveStringToFile(Text,*Report,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  if(GEngine)GEngine->Exec(GI.IsValid()?GI->GetWorld():nullptr,TEXT("QUIT"));return false;
 }
 bool Tick(float)
 {
  if(!GI.IsValid())return Finish(TEXT("GameInstance lost"));const double Now=FPlatformTime::Seconds();if(Now-Started>180)return Finish(TEXT("Scenario timed out"));
  UWorld* W=GI->GetWorld();if(!W)return true;auto* Load=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();if(Load->IsTransitionActive()||GI->IsPlayerRestorePending())return true;
  auto* PC=UGameplayStatics::GetPlayerController(W,0);auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;if(!P)return true;
  ADWCinematicTrigger* Trigger=nullptr;ADWBridgeChangeDemo* Bridge=nullptr;for(TActorIterator<ADWCinematicTrigger>I(W);I;++I){Trigger=*I;break;}for(TActorIterator<ADWBridgeChangeDemo>I(W);I;++I){Bridge=*I;break;}
  auto* C=Trigger?Trigger->Cinematic.Get():nullptr;auto* E=Bridge?Bridge->WorldEvent.Get():nullptr;
  const FString Map=UWorld::RemovePIEPrefix(W->GetOutermost()->GetName());
  switch(Step)
  {
  case 0:
   if(Now-At<2)return true;
   GI->GetConfig()->GameplayMap=Demo;if(!UIOnly)GI->GetConfig()->AdditionalGameplayMaps.AddUnique(TSoftObjectPtr<UWorld>(FSoftObjectPath(Away)));
   Check(TEXT("Create isolated slot A"),GI->NewGame(0,TEXT("Cinematic QA A")));Step=1;At=Now;break;
  case 1:
   if(Map!=Demo||!C||!E||Now-At<2)return true;
   Check(TEXT("Fresh slot starts with intact bridge"),E->IsReady()&&Bridge->GetBreakProgress()==0);Shot(TEXT("Standalone_Before"));Step=2;At=Now;break;
  case 2:
   if(Now-At<.8)return true;
   CaptureHUD(PC);
   P->SetActorLocation(FVector(460,0,100),false,nullptr,ETeleportType::TeleportPhysics);LockedLocation=P->GetActorLocation();
   Check(TEXT("Region overlap starts cinematic once"),C->IsPlaying()&&C->PlaybackCount==1);Check(TEXT("Busy request rejected"),!C->PlayCinematic(PC));Check(TEXT("Input locked"),PC->IsMoveInputIgnored()&&PC->IsLookInputIgnored());
   Check(TEXT("Mid-cinematic save rejected"),!GI->SaveCurrentGame());Step=3;At=Now;break;
  case 3:
   Phases.Add(int32(C->Phase));
   if(C->IsPlaying()&&FVector::DistSquaredXY(P->GetActorLocation(),LockedLocation)>1)return Finish(TEXT("Player moved while cinematic was active"));
   if(C->Phase==EDWCinematicPhase::SceneEvent&&!ShotScene){ShotScene=true;Shot(TEXT("Standalone_Scene"));Check(TEXT("Captured HUD set including C01 is fully offscreen"),HUDMatches(PC,true));Check(TEXT("2.39 aspect ratio reached"),FMath::IsNearlyEqual(C->GetPlaybackCamera()->GetCameraComponent()->AspectRatio,2.39f,.01f));}
   if(C->Phase==EDWCinematicPhase::Hold&&!ShotHold){ShotHold=true;Shot(TEXT("Standalone_Broken"));Check(TEXT("Final state committed before hold"),E->IsCompleted());}
   if(C->IsPlaying())return true;
   Check(TEXT("Full sequence completed"),C->PlaybackCount==1&&E->IsCompleted()&&Phases.Num()>=5);
   Check(TEXT("Input and camera restored"),!PC->IsMoveInputIgnored()&&!PC->IsLookInputIgnored()&&PC->GetViewTarget()==P);
   Check(TEXT("Same HUD set returned with no remaining ticker or offset"),HUDMatches(PC,false));
   Check(TEXT("Mouse cursor restored after completion"),PC->bShowMouseCursor==ExpectedCursor);
   Check(TEXT("Repeated event is rejected"),!C->PlayCinematic(PC));
   if(UIOnly)
   {
    const bool Created=GI->NewGame(1,TEXT("Cinematic UI cancellation QA B"));Check(TEXT("Create isolated fresh slot B for UI-only cancellation"),Created);
    if(!Created)return Finish();Step=8;At=Now;break;
   }
   Check(TEXT("SaveCurrentGame persists event"),GI->SaveCurrentGame());Shot(TEXT("Standalone_Returned"));Step=4;At=Now;break;
  case 4:
   if(Now-At<1)return true;
   Check(TEXT("Travel to another allowed map"),GI->TravelToGameplayMap(TSoftObjectPtr<UWorld>(FSoftObjectPath(Away))));Step=5;At=Now;break;
  case 5:
   if(Map!=Away||Now-At<1)return true;
   Check(TEXT("Return to demo map"),GI->TravelToGameplayMap(TSoftObjectPtr<UWorld>(FSoftObjectPath(Demo))));Step=6;At=Now;break;
  case 6:
   if(Map!=Demo||!E||Now-At<1)return true;
   Check(TEXT("Map revisit restores broken bridge without playback"),E->IsCompleted()&&Bridge->GetBreakProgress()==1&&C->PlaybackCount==0);
   Check(TEXT("Save current destination before reload"),GI->SaveCurrentGame());
   Check(TEXT("Load slot A"),GI->LoadGameSlot(0));Step=7;At=Now;break;
  case 7:
   if(Map!=Demo||!E||Now-At<1)return true;
   Check(TEXT("Reload from disk preserves one-shot state"),E->IsCompleted()&&C->PlaybackCount==0);
   Check(TEXT("Create separate slot B"),GI->NewGame(1,TEXT("Cinematic QA B")));Step=8;At=Now;break;
  case 8:
   if(Map!=Demo||!E||Now-At<1)return true;
   Check(TEXT("Separate save slot has an intact bridge"),E->IsReady()&&Bridge->GetBreakProgress()==0);
   CaptureHUD(PC);
   Check(TEXT("Start for cancellation test"),C->PlayCinematic(PC));C->CancelCinematic();Check(TEXT("Cancel leaves event retryable and input restored"),E->IsReady()&&!PC->IsMoveInputIgnored()&&!PC->IsLookInputIgnored()&&!C->IsPlaying());
   Check(TEXT("Cancellation restores same HUD set and stops all tickers"),HUDMatches(PC,false));
   Check(TEXT("Mouse cursor restored after cancellation"),PC->bShowMouseCursor==ExpectedCursor);
   if(UIOnly)return Finish();
   Check(TEXT("Return to slot A"),GI->LoadGameSlot(0));Step=9;At=Now;break;
  case 9:
   if(Map!=Demo||!E||Now-At<1)return true;
   Check(TEXT("Slot A unaffected by slot B"),E->IsCompleted()&&Bridge->GetBreakProgress()==1);return Finish();
  }
  return true;
 }
};
TSharedPtr<FCutsceneVerification> CutVerification;
}
#endif
void DWStartCinematicVerification(UDWGameInstance* GI)
{
#if WITH_DEV_AUTOMATION_TESTS
 if(CutVerification||!GI||!FParse::Param(FCommandLine::Get(),TEXT("DWCinematicQA")))return;
 FString Report,Prefix;
 if(!FParse::Value(FCommandLine::Get(),TEXT("DWCinematicQAReport="),Report)||!FParse::Value(FCommandLine::Get(),TEXT("DWVerificationSavePrefix="),Prefix)||!Prefix.StartsWith(TEXT("DW_QA_")))return;
 CutVerification=MakeShared<FCutsceneVerification>();CutVerification->GI=GI;CutVerification->Report=Report;CutVerification->UIOnly=FParse::Param(FCommandLine::Get(),TEXT("DWCinematicUIOnly"));
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt){return CutVerification->Tick(Dt);}));
#endif
}
