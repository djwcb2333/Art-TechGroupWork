#include "DWActionSoundSubsystem.h"
#include "DWAudioLibrary.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWPlaceableBuilding.h"
#include "AudioDevice.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Sound/SoundWave.h"

USoundWave* UDWActionSoundSettings::Find(EDWActionSoundCue Cue)const
{
 switch(Cue)
 {
 case EDWActionSoundCue::Spear:return SpearAttackSound;
 case EDWActionSoundCue::ShieldAxe:return ShieldAxeAttackSound;
 case EDWActionSoundCue::SwordSlam:return GreatswordSlamSound;
 case EDWActionSoundCue::SwordSweep:return GreatswordSweepSound;
 case EDWActionSoundCue::DualDaggers:return DualDaggersAttackSound;
 case EDWActionSoundCue::SporeGun:return SporeGunShotSound;
 case EDWActionSoundCue::MoldBow:return MoldBowReleaseSound;
 case EDWActionSoundCue::Shotgun:return ShotgunShotSound;
 case EDWActionSoundCue::Alcohol:return AlcoholReleaseSound;
 case EDWActionSoundCue::SimpleSugarSpear:return SimpleSugarSpearAttackSound;
 case EDWActionSoundCue::Unarmed:return UnarmedAttackSound;
 case EDWActionSoundCue::BuildSuccess:return BuildSuccessSound;
 default:return nullptr;
 }
}
namespace
{
 bool Ready(USoundWave* Wave){return IsValid(Wave)&&Wave->GetPrecacheState()==ESoundWavePrecacheState::Done;}
 UDWActionSoundSubsystem* Resolve(const UObject* O)
 {auto* W=IsValid(O)?O->GetWorld():nullptr;auto* GI=W?W->GetGameInstance():nullptr;return GI?GI->GetSubsystem<UDWActionSoundSubsystem>():nullptr;}
 bool Live(const UWorld* W){return W&&W->IsGameWorld()&&!W->bIsTearingDown&&!W->IsPaused()&&W->GetNetMode()!=NM_DedicatedServer;}
}
void UDWActionSoundSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);Started=FPlatformTime::Seconds();
 LoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(
  FSoftObjectPath(TEXT("/Game/DoughWorld/Core/Data/DA_DWActionSoundSlots.DA_DWActionSoundSlots")),
  FStreamableDelegate::CreateUObject(this,&UDWActionSoundSubsystem::Loaded));
 if(!LoadHandle){bStopped=true;LogOnce(TEXT("could not request optional settings"));return;}
 Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UDWActionSoundSubsystem::Prepare),.02f);
}
void UDWActionSoundSubsystem::Loaded()
{
 if(bStopped||!LoadHandle.IsValid())return;
 Settings=Cast<UDWActionSoundSettings>(LoadHandle->GetLoadedAsset());
 if(!Settings){bStopped=true;LogOnce(TEXT("optional settings unavailable"));}
}
void UDWActionSoundSubsystem::LogOnce(const TCHAR* Message)
{if(!bLogged){bLogged=true;UE_LOG(LogTemp,Warning,TEXT("DW action sound: %s; gameplay and save rules unchanged."),Message);}}
bool UDWActionSoundSubsystem::Prepare(float)
{
 if(bStopped)return false;
 if(FPlatformTime::Seconds()-Started>=5.)
 {bStopped=true;LogOnce(TEXT("optional preparation expired"));if(LoadHandle.IsValid()&&!LoadHandle->HasLoadCompleted())LoadHandle->CancelHandle();return false;}
 if(!Settings)return true;
 bool Any=false;for(uint8 I=1;I<=12;++I)Any|=IsValid(Settings->Find(static_cast<EDWActionSoundCue>(I)));
 if(!Any){bStopped=true;return false;} // Empty preset is intentionally quiet, including nullAudio.
 auto* World=GetGameInstance()?GetGameInstance()->GetWorld():nullptr;
 if(!World||World->bIsTearingDown)return true;
 auto* Device=World->GetAudioDeviceRaw();if(!Device)return true;
 if(!bPrecacheStarted)
 {
  bPrecacheStarted=true;
  // Reuse normal async precache. Force-full mode resets state again on audio-thread dispatch.
  // Shared/repeated slots and PostLoad-owned jobs must retain their InProgress/Done state.
  for(uint8 I=1;I<=12;++I)if(auto* Wave=Settings->Find(static_cast<EDWActionSoundCue>(I));
      IsValid(Wave)&&Wave->GetPrecacheState()==ESoundWavePrecacheState::NotStarted)
   Device->Precache(Wave,false,false,false);
 }
 for(uint8 I=1;I<=12;++I)if(auto* Wave=Settings->Find(static_cast<EDWActionSoundCue>(I));Wave&&!Ready(Wave))return true;
 bStopped=true;return false;
}
void UDWActionSoundSubsystem::Publish(AActor* Owner,EDWActionSoundCue Cue,DWCombatCommit::FReceiptId Id,
 uint64 Restore,DWCombatCommit::FEntityLife Life,uint8 Domain,FVector Location)
{
 auto* W=IsValid(Owner)?Owner->GetWorld():nullptr;
 auto* GI=GetGameInstance();
 auto* P=Cast<ADWPlayerCharacter>(Owner);auto* A=UDWCombatAuthoritySubsystem::Get(Owner);
 if(!Live(W)||!GI||W!=GI->GetWorld()||W->GetGameInstance()!=GI||!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||
    !P->IsLocallyControlled()||!P->WeaponAnimationComponent||!A||Location.ContainsNaN())return;
 const auto CurrentLife=A->FindRegisteredActorLife(P);
 DWActionSoundPolicy::FContext Current{W->GetUniqueID(),CurrentLife.Entity,CurrentLife.Life,
  P->WeaponAnimationComponent->GetCombatWeaponRestoreEpoch(),A->GetWorldEpoch()};
 Once.SetCurrent(Current);
 auto* Wave=IsValid(Settings)?Settings->Find(Cue):nullptr;
 const bool Configured=Ready(Wave)&&Settings&&FMath::IsFinite(Settings->Volume)&&Settings->Volume>0.f&&W->GetAudioDeviceRaw();
 // Reserve before any audio component can cause reentrant publication. No stale event queue.
 if(!Once.Reserve({{Current.World,Life.Entity,Life.Life,Restore,Id.Epoch},Id.Id,static_cast<uint8>(Cue),Domain},Configured))return;
 UDWAudioLibrary::PlayWorldSound(Owner,Wave,Location,Settings->Volume,Settings->Pitch,nullptr);
}
void UDWActionSoundSubsystem::PublishBuild(const FDWBuildSoundNotice& N)
{
 auto* B=N.Building.Get();auto* W=N.World.Get();auto* A=UDWCombatAuthoritySubsystem::Get(B);
 auto* GI=GetGameInstance();
 if(!IsValid(B)||B->IsActorBeingDestroyed()||!Live(W)||!GI||W!=GI->GetWorld()||B->GetWorld()!=W||W->GetGameInstance()!=GI||
    B->PersistentId!=N.PersistentId||B->IsStaged()||!B->bStationEnabled||!A||A->GetWorldEpoch()!=N.Epoch||N.Transform.ContainsNaN())return;
 BuildOnce.SetCurrent({W->GetUniqueID(),0,0,0,N.Epoch});
 auto* Wave=IsValid(Settings)?Settings->BuildSuccessSound.Get():nullptr;
 const bool Configured=Ready(Wave)&&Settings&&FMath::IsFinite(Settings->Volume)&&Settings->Volume>0.f&&W->GetAudioDeviceRaw();
 if(!BuildOnce.ReserveBuild(N.PersistentId,N.PublicationSerial,Configured))return;
 UDWAudioLibrary::PlayWorldSound(B,Wave,N.Transform.GetLocation(),Settings->Volume,Settings->Pitch,nullptr);
}
FDWBuildSoundNotice UDWActionSoundSubsystem::FreezeBuild(ADWPlaceableBuilding* B)
{
 FDWBuildSoundNotice N;auto* A=UDWCombatAuthoritySubsystem::Get(B);
 auto* GI=GetGameInstance();
 if(IsValid(B)&&!B->IsActorBeingDestroyed()&&GI&&B->GetWorld()==GI->GetWorld()&&A&&BuildSerial<MAX_uint64)
 {
  N.Building=B;N.World=B->GetWorld();N.PersistentId=B->PersistentId;N.Transform=B->GetActorTransform();
  N.Epoch=A->GetWorldEpoch();N.PublicationSerial=++BuildSerial;
 }
 return N;
}
void UDWActionSoundSubsystem::Deinitialize()
{
 bStopped=true;FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
 if(LoadHandle.IsValid())LoadHandle->CancelHandle();LoadHandle.Reset();Settings=nullptr;
 Super::Deinitialize();
}
namespace DWActionSound
{
 void Action(AActor* Owner,EDWActionSoundCue Cue,DWCombatCommit::FActionId Id,uint64 Restore)
 {
  auto* A=UDWCombatAuthoritySubsystem::Get(Owner);
  if(!A||!A->IsCurrentPresentationAction(Id,Owner))return;
  if(auto* S=Resolve(Owner))S->Publish(Owner,Cue,{Id.Epoch,Id.Id},Restore,A->FindRegisteredActorLife(Owner),1,Owner->GetActorLocation());
 }
 void Receipt(AActor* Owner,EDWActionSoundCue Cue,DWCombatCommit::FReceiptId Id,uint64 Restore,DWCombatCommit::FEntityLife Life,FVector Location)
 {if(auto* S=Resolve(Owner))S->Publish(Owner,Cue,Id,Restore,Life,2,Location);}
 FDWBuildSoundNotice CaptureBuild(ADWPlaceableBuilding* B)
 {
  if(auto* S=Resolve(B))return S->FreezeBuild(B);return {};
 }
 void Build(const FDWBuildSoundNotice& N){if(auto* S=Resolve(N.World.Get()))S->PublishBuild(N);}
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWActionSoundPolicyTest,"DoughWorld.Audio.ActionSoundIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWActionSoundPolicyTest::RunTest(const FString&)
{
 using namespace DWActionSoundPolicy;
 FOnce Once;FContext C{1,2,3,4,5};Once.SetCurrent(C);
 FIdentity Slam{C,10,3,1},Sweep{C,10,4,1};
 TestTrue(TEXT("Accepted slam"),Once.Reserve(Slam,true));TestFalse(TEXT("Same slam once"),Once.Reserve(Slam,true));
 TestTrue(TEXT("Same action independent sweep"),Once.Reserve(Sweep,true));
 Slam.Id=11;TestTrue(TEXT("Next accepted action/segment"),Once.Reserve(Slam,true));
 FIdentity Shot{C,20,6,2};TestTrue(TEXT("One committed receipt"),Once.Reserve(Shot,true));
 TestFalse(TEXT("Duplicate receipt"),Once.Reserve(Shot,true));Shot.Id=19;TestFalse(TEXT("Older receipt cannot replay"),Once.Reserve(Shot,true));
 for(uint8 Field=0;Field<5;++Field)
 {
  FContext New=C;if(Field==0)++New.World;if(Field==1)++New.Entity;if(Field==2)++New.Life;if(Field==3)++New.Restore;if(Field==4)++New.Epoch;
  Once.SetCurrent(New);TestFalse(TEXT("Old world/life/restore episode rejected"),Once.Reserve(Slam,true));
  Slam.Context=New;Slam.Id=1;TestTrue(TEXT("New episode accepted"),Once.Reserve(Slam,true));C=New;
 }
 FIdentity Empty{C,30,1,1};TestFalse(TEXT("Empty slot silent"),Once.Reserve(Empty,false));
 TestFalse(TEXT("Becoming ready does not replay empty/late event"),Once.Reserve(Empty,true));
 Empty.Id=31;TestFalse(TEXT("Explicit restore replay rejected"),Once.Reserve(Empty,true,true));
 FOnce PawnReplacement;FContext OldPawn{10,20,1,0,5},NewPawn{10,21,1,0,5};
 PawnReplacement.SetCurrent(OldPawn);TestTrue(TEXT("Old pawn accepted while controlled"),PawnReplacement.Reserve({OldPawn,1,1,1},true));
 PawnReplacement.SetCurrent(NewPawn);TestTrue(TEXT("Legitimate replacement pawn need not await old pawn GC"),PawnReplacement.Reserve({NewPawn,1,1,1},true));
 TestFalse(TEXT("Old pawn identity cannot replay after replacement"),PawnReplacement.Reserve({OldPawn,2,1,1},true));
 FOnce Small(1);Small.SetCurrent(C);TestTrue(TEXT("First bounded stream"),Small.Reserve({C,1,1,1},true));
 TestFalse(TEXT("Capacity overflow is silent"),Small.Reserve({C,1,2,1},true));
 TestEqual(TEXT("Capacity remains finite"),Small.NumStreams(),1);
 TestTrue(TEXT("Existing stream still advances when capacity full"),Small.Reserve({C,2,1,1},true));
 TestTrue(TEXT("Build committed once"),Once.ReserveBuild(TEXT("Building1"),1,true));
 TestFalse(TEXT("Duplicate immutable build notice"),Once.ReserveBuild(TEXT("Building1"),1,true));
 TestFalse(TEXT("Restore never creates construction cue"),Once.ReserveBuild(TEXT("Restored"),2,true,true));
 for(int32 I=2;I<=300;++I)TestTrue(TEXT("Future builds remain audible"),Once.ReserveBuild(FName(*FString::Printf(TEXT("Building%d"),I)),I,true));
 TestEqual(TEXT("Build history remains constant memory"),Once.NumBuildHeads(),1);
 TestFalse(TEXT("Old build cannot replay after many later builds"),Once.ReserveBuild(TEXT("Building1"),1,true));
 UDWActionSoundSettings* Defaults=GetMutableDefault<UDWActionSoundSettings>();
 for(uint8 I=1;I<=12;++I)TestNull(TEXT("All twelve default slots empty"),Defaults->Find(static_cast<EDWActionSoundCue>(I)));
 return true;
}
#endif
