#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWWorldEvent.generated.h"

class UDWCinematicComponent;
class UDWWorldEventComponent;
class UDWWorldEventSubsystem;
class UDWCombatAuthoritySubsystem;
class ADWPlayerCharacter;
namespace DWCombatCommit { struct FEntityLife; }

/** Native aggregate candidate. No gameplay object is changed until AdoptCompletedFacts. */
struct GDATTEST_API FDWWorldEventRestorePlan
{
 bool CanCommit(FString& Error)const;
private:
 friend class UDWWorldEventSubsystem;
 struct FEntry
 {
  FName Id;
  TWeakObjectPtr<UDWWorldEventComponent> Component;
  TWeakObjectPtr<AActor> Actor;
  bool bWasCompleted=false,bWasRunning=false,bHasBegunPlay=false,bTargetCompleted=false;
 };
 TWeakObjectPtr<UDWWorldEventSubsystem> Owner;
 TWeakObjectPtr<UWorld> World;
 TWeakObjectPtr<UDWCinematicComponent> InitialActive;
 TArray<FName> BeforeIds,TargetIds;
 TSet<FName> PreparedCompleted;
 TArray<FEntry> Entries;
 bool bPrepared=false,bAdopted=false,bPublishStarted=false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWWorldEventSignal);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWWorldEventStateSignal,bool,bCompleted);

/** Per-map persistent event ledger. Only final state is saved, never a partly played animation. */
UCLASS()
class GDATTEST_API UDWWorldEventSubsystem : public UWorldSubsystem
{
 GENERATED_BODY()
public:
 bool RegisterEvent(UDWWorldEventComponent* Event);
 void UnregisterEvent(UDWWorldEventComponent* Event);
 void Commit(FName Id);
 TArray<FName> ExportCompleted() const;
 void ImportCompleted(const TArray<FName>& Ids);
 // D22 aggregate-only path. Adopt has no transform, Blueprint or delegate calls.
 bool PrepareCompletedFacts(const TArray<FName>& Ids,FDWWorldEventRestorePlan& Out,FString& Error);
 bool CanCommitCompletedFacts(const FDWWorldEventRestorePlan& Plan,FString& Error)const;
 void AdoptCompletedFacts(FDWWorldEventRestorePlan& Plan);
 bool PublishCompletedFacts(FDWWorldEventRestorePlan& Plan,UDWCombatAuthoritySubsystem* Authority,
     uint64 ExpectedEpoch,ADWPlayerCharacter* Player,const DWCombatCommit::FEntityLife& ExpectedPlayerLife,FString& Error);

 bool IsCompleted(FName Id) const { return Completed.Contains(Id); }
 bool Acquire(UDWCinematicComponent* Cinematic);
 void Release(UDWCinematicComponent* Cinematic);
 bool IsCinematicActive() const { return Active.IsValid(); }
 static bool IsPlaying(const UObject* Context);
private:
 bool CheckCompletedFactsPlan(const FDWWorldEventRestorePlan& Plan,bool bAdopted,FString& Error)const;
 TSet<FName> Completed;
 TMap<FName,TWeakObjectPtr<UDWWorldEventComponent>> Events;
 TWeakObjectPtr<UDWCinematicComponent> Active;
};

/** Attach to the changing actor. Bind OnRequested -> animation -> CompleteEvent.
 * OnApplyState must set BOTH final appearance and collision, and be safe to call repeatedly. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,Blueprintable,meta=(BlueprintSpawnableComponent,DisplayName="DW World Event"))
class GDATTEST_API UDWWorldEventComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UDWWorldEventComponent();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Event",meta=(ToolTip="Unique stable ID within this map. Do not rename after releasing saves.")) FName EventId;
 UPROPERTY(BlueprintAssignable,Category="World Event") FDWWorldEventSignal OnRequested;
 UPROPERTY(BlueprintAssignable,Category="World Event") FDWWorldEventSignal OnCompleted;
 UPROPERTY(BlueprintAssignable,Category="World Event") FDWWorldEventStateSignal OnApplyState;
 UFUNCTION(BlueprintCallable,Category="World Event") bool RequestEvent();
 UFUNCTION(BlueprintCallable,Category="World Event") void CompleteEvent();
 UFUNCTION(BlueprintCallable,Category="World Event") void CancelEvent();
 UFUNCTION(BlueprintPure,Category="World Event") bool IsCompleted() const { return bCompleted; }
 UFUNCTION(BlueprintPure,Category="World Event") bool IsRunning() const { return bRunning; }
 UFUNCTION(BlueprintPure,Category="World Event") bool IsReady() const { return bRegistered&&!bCompleted&&!bRunning; }
 void RestoreState(bool Value);
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 friend class UDWWorldEventSubsystem;
 bool bRegistered=false,bCompleted=false,bRunning=false;
 FName RegisteredId;
};
