#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "DWCombatCommitLedger.h"
#include "DWActionSoundSubsystem.generated.h"

class USoundWave;class AActor;class ADWPlaceableBuilding;struct FStreamableHandle;
UENUM()
enum class EDWActionSoundCue:uint8
{ None,Spear,ShieldAxe,SwordSlam,SwordSweep,DualDaggers,SporeGun,MoldBow,Shotgun,Alcohol,SimpleSugarSpear,Unarmed,BuildSuccess };

UCLASS(BlueprintType)
class GDATTEST_API UDWActionSoundSettings:public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> SpearAttackSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> ShieldAxeAttackSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> GreatswordSlamSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> GreatswordSweepSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> DualDaggersAttackSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> SporeGunShotSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> MoldBowReleaseSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> ShotgunShotSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> AlcoholReleaseSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> SimpleSugarSpearAttackSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Attack") TObjectPtr<USoundWave> UnarmedAttackSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots|Building") TObjectPtr<USoundWave> BuildSuccessSound;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots",meta=(ClampMin="0",ClampMax="4")) float Volume=1.f;
 UPROPERTY(EditAnywhere,Category="Action Sound Slots",meta=(ClampMin="0.125",ClampMax="4")) float Pitch=1.f;
 USoundWave* Find(EDWActionSoundCue Cue)const;
};

namespace DWActionSoundPolicy
{
 // Plain data only: no World, actors, playback, timers, or persistence I/O.
 struct FContext
 {
  uint64 World=0,Entity=0,Life=0,Restore=0,Epoch=0;
  bool operator==(const FContext& B)const
  {return World==B.World&&Entity==B.Entity&&Life==B.Life&&Restore==B.Restore&&Epoch==B.Epoch;}
 };
 struct FIdentity { FContext Context;uint64 Id=0;uint8 Cue=0,Domain=0; };
 class FOnce
 {
  struct FHead { uint8 Cue=0,Domain=0;uint64 Last=0; };
  FContext Current;TArray<FHead> Heads;uint64 LastBuildSerial=0;int32 Capacity;
 public:
  explicit FOnce(int32 InCapacity=64):Capacity(FMath::Max(1,InCapacity)){}
  void SetCurrent(FContext Value){if(!(Value==Current)){Current=Value;Heads.Reset();LastBuildSerial=0;}}
  bool Reserve(const FIdentity& Event,bool bConfigured,bool bRestoreReplay=false)
  {
   if(bRestoreReplay||!(Event.Context==Current)||!Event.Context.World||!Event.Context.Entity||!Event.Context.Life||
      !Event.Context.Epoch||!Event.Id||Event.Cue==0||Event.Cue>12||Event.Domain<1||Event.Domain>2)return false;
   auto* Head=Heads.FindByPredicate([&](const FHead& H){return H.Cue==Event.Cue&&H.Domain==Event.Domain;});
   if(Head){if(Event.Id<=Head->Last)return false;Head->Last=Event.Id;}
   else {if(Heads.Num()>=Capacity)return false;Heads.Add({Event.Cue,Event.Domain,Event.Id});}
   // Empty/unready events are consumed too: filling a slot never replays an old event.
   return bConfigured;
  }
  bool ReserveBuild(FName Id,uint64 Serial,bool bConfigured,bool bRestoreReplay=false)
  {
   if(bRestoreReplay||Id.IsNone()||!Serial||Serial<=LastBuildSerial)return false;
   LastBuildSerial=Serial;return bConfigured; // Constant memory; no eviction and no lifetime building limit.
  }
  int32 NumStreams()const{return Heads.Num();}int32 NumBuildHeads()const{return LastBuildSerial?1:0;}
 };
}

struct FDWBuildSoundNotice
{
 TWeakObjectPtr<ADWPlaceableBuilding> Building;TWeakObjectPtr<UWorld> World;
 FName PersistentId;FTransform Transform=FTransform::Identity;uint64 Epoch=0,PublicationSerial=0;
};

UCLASS()
class GDATTEST_API UDWActionSoundSubsystem:public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase&)override;
 virtual void Deinitialize()override;
 void Publish(AActor* Owner,EDWActionSoundCue,DWCombatCommit::FReceiptId,uint64 Restore,DWCombatCommit::FEntityLife,uint8 Domain,FVector Location);
 void PublishBuild(const FDWBuildSoundNotice&);
 FDWBuildSoundNotice FreezeBuild(ADWPlaceableBuilding*);
private:
 void Loaded();bool Prepare(float);void LogOnce(const TCHAR*);
 UPROPERTY(Transient) TObjectPtr<UDWActionSoundSettings> Settings;
 TSharedPtr<FStreamableHandle> LoadHandle;FTSTicker::FDelegateHandle Ticker;
 DWActionSoundPolicy::FOnce Once,BuildOnce;
 uint64 BuildSerial=0; // Cosmetic-local publication counter, never a combat action/receipt/save identity.
 double Started=0.;bool bStopped=false,bPrecacheStarted=false,bLogged=false;
};

namespace DWActionSound
{
 void Action(AActor*,EDWActionSoundCue,DWCombatCommit::FActionId,uint64 Restore);
 void Receipt(AActor*,EDWActionSoundCue,DWCombatCommit::FReceiptId,uint64 Restore,DWCombatCommit::FEntityLife,FVector);
 FDWBuildSoundNotice CaptureBuild(ADWPlaceableBuilding*);
 void Build(const FDWBuildSoundNotice&);
}
