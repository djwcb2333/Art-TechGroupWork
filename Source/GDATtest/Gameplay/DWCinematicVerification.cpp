#include "DWGameInstance.h"
#include "DWCinematicDemo.h"
#include "DWGameplayCinematic.h"
#include "DWWorldEvent.h"
#include "DWUIOffscreenComponent.h"
#include "DWLoadingTransition.h"
#include "DWGameplayConfig.h"
#include "DWGameplayHUD.h"
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
 void Check(const FString& Name,bool Pass){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("name"),Name);O->SetBoolField(TEXT("passed"),Pass);Checks.Add(MakeShared<FJsonValueObject>(O));Success&=Pass;UE_LOG(LogTemp,Display,TEXT("DW_CUT_QA %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),*Name);}
 void Shot(const FString& Name){FScreenshotRequest::RequestScreenshot(FPaths::Combine(FPaths::GetPath(Report),TEXT("images"),Name+TEXT(".png")),true,false);}
 bool Finish(const FString& Error=FString())
 {
  if(!Error.IsEmpty())Check(Error,false);auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("passed"),Success);O->SetStringField(TEXT("mode"),GIsEditor?TEXT("Editor"):TEXT("Standalone -game"));O->SetArrayField(TEXT("checks"),Checks);O->SetNumberField(TEXT("seconds"),FPlatformTime::Seconds()-Started);
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
   GI->GetConfig()->GameplayMap=Demo;GI->GetConfig()->AdditionalGameplayMaps.AddUnique(TSoftObjectPtr<UWorld>(FSoftObjectPath(Away)));
   Check(TEXT("Create isolated slot A"),GI->NewGame(0,TEXT("Cinematic QA A")));Step=1;At=Now;break;
  case 1:
   if(Map!=Demo||!C||!E||Now-At<2)return true;
   Check(TEXT("Fresh slot starts with intact bridge"),E->IsReady()&&Bridge->GetBreakProgress()==0);Shot(TEXT("Standalone_Before"));Step=2;At=Now;break;
  case 2:
   if(Now-At<.8)return true;
   P->SetActorLocation(FVector(460,0,100),false,nullptr,ETeleportType::TeleportPhysics);LockedLocation=P->GetActorLocation();
   Check(TEXT("Region overlap starts cinematic once"),C->IsPlaying()&&C->PlaybackCount==1);Check(TEXT("Busy request rejected"),!C->PlayCinematic(PC));Check(TEXT("Input locked"),PC->IsMoveInputIgnored()&&PC->IsLookInputIgnored());
   Check(TEXT("Mid-cinematic save rejected"),!GI->SaveCurrentGame());Step=3;At=Now;break;
  case 3:
   Phases.Add(int32(C->Phase));
   if(C->IsPlaying()&&FVector::DistSquaredXY(P->GetActorLocation(),LockedLocation)>1)return Finish(TEXT("Player moved while cinematic was active"));
   if(C->Phase==EDWCinematicPhase::SceneEvent&&!ShotScene){ShotScene=true;Shot(TEXT("Standalone_Scene"));auto UI=UDWUIOffscreenComponent::FindForPlayer(PC);bool Hidden=UI.Num()==6;for(auto* U:UI)Hidden&=U->IsOffscreen();Check(TEXT("Six HUD groups are offscreen"),Hidden);Check(TEXT("2.39 aspect ratio reached"),FMath::IsNearlyEqual(C->GetPlaybackCamera()->GetCameraComponent()->AspectRatio,2.39f,.01f));}
   if(C->Phase==EDWCinematicPhase::Hold&&!ShotHold){ShotHold=true;Shot(TEXT("Standalone_Broken"));Check(TEXT("Final state committed before hold"),E->IsCompleted());}
   if(C->IsPlaying())return true;
   Check(TEXT("Full sequence completed"),C->PlaybackCount==1&&E->IsCompleted()&&Phases.Num()>=5);
   Check(TEXT("Input and camera restored"),!PC->IsMoveInputIgnored()&&!PC->IsLookInputIgnored()&&PC->GetViewTarget()==P);
   {bool Restored=true;for(auto* U:UDWUIOffscreenComponent::FindForPlayer(PC))Restored&=!U->IsOffscreen()&&U->GetCurrentOffset().IsNearlyZero();Check(TEXT("HUD returned to authored positions"),Restored);}
   Check(TEXT("Repeated event is rejected"),!C->PlayCinematic(PC));Check(TEXT("SaveCurrentGame persists event"),GI->SaveCurrentGame());Shot(TEXT("Standalone_Returned"));Step=4;At=Now;break;
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
   Check(TEXT("Start for cancellation test"),C->PlayCinematic(PC));C->CancelCinematic();Check(TEXT("Cancel leaves event retryable and input restored"),E->IsReady()&&!PC->IsMoveInputIgnored()&&!C->IsPlaying());
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
 CutVerification=MakeShared<FCutsceneVerification>();CutVerification->GI=GI;CutVerification->Report=Report;
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float Dt){return CutVerification->Tick(Dt);}));
#endif
}
