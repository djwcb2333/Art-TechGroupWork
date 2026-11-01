#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWCharacterA01.h"
#include "DWA01AbilityPlan.h"
#include "DWCombatPlayerVitals.h"
#include "DWCombatInputLedger.h"
#include "DWCombatCommitLedger.h"
#include "DWCharacterA01Component.generated.h"
class ADWPlayerCharacter;
class UDWLanguageSubsystem;
class FJsonObject;

// Exact-map A01 overlay: driven by the player's ordered clock, never an independent Tick.
UCLASS(ClassGroup=(DoughWorld),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWCharacterA01Component : public UActorComponent
{
 GENERATED_BODY()
public:
 UDWCharacterA01Component();
 bool InitializeForTrial(FString& Error);
 bool InitializeForCombatSandbox(FString& Error);
 bool PrepareFormalAbility(FDWA01AbilityPlan& Out,FString& Error)const;
 void AdoptFormalStateUnchecked(FDWCharacterA01Runtime&&,FDWA01AbilityPlan&&,double Cooldown);
 UFUNCTION(BlueprintPure) bool IsEnabled()const;
 const FDWCharacterA01Runtime& GetRuntime()const{return Runtime;}
 const FDWA01AbilityPlan& GetAbility()const{return Ability;}
 uint64 GetResourceRevision()const{return ResourceRevision;}
 bool RefreshTalents();
 UFUNCTION(BlueprintPure) bool IsAbilityActive()const;
 UFUNCTION(BlueprintPure) bool IsDashInvulnerable()const;
 UFUNCTION(BlueprintPure) bool CanOpenMenus()const{return !IsAbilityActive();}
 UFUNCTION(BlueprintPure) bool CanUseShortcuts()const;
 bool CanAdoptRuntime(uint64 ExpectedRevision,double ExpectedHealth)const;
 bool IsDurationTalentContributing()const;
 void NotifyShortcutUse(int32 InventoryIndex);
 bool CanForgetExtraShortcut()const;
 void OnExtraShortcutRemoved();
 void BeginRebound(double Seconds);
 bool IsDashing()const;
 bool IsSprinting()const{return IsEnabled()&&Ability.IsSprinting();}
 EDWCombatBaseForm GetBaseForm()const;
 double GetSpecial()const;
 double GetMaxHealth()const{return Runtime.EffectiveMaxHealth();}
 double GetWalkSpeedCmPerSecond()const;
 void SetMoveInput(FVector Direction){MoveInput=Direction.GetClampedToMaxSize2D(1.);}
 bool HandleSpacePressed(const FDWCombatInputPress&);
 bool HandleSpaceReleased(const FDWCombatInputPress&);
 void CancelAbility(bool bRequireRelease=false);
 bool PrepareForAttack();
 double GetNextAbilityBoundary(double MaximumSeconds)const;
 bool AdvanceSlice(double MaximumSeconds,FDWCombatPlayerVitalsStep& LegacyStep,FDWCharacterA01Step& A01Step);
 bool PumpPaidMotion();
 void SyncAuthoritativeHealth();
 void AdoptRuntimeUnchecked(FDWCharacterA01Runtime&& Candidate,bool bFormEntry=false);
 bool PrepareFormEntry(EDWCombatBaseForm,FDWCharacterA01Runtime& OutCandidate)const;
 static EDWCombatBaseForm FormForEssence(FName Item,bool bLegacyAdapter=true);
 double InjectionSeconds()const;
 int32 EssenceCount()const;
 double AddWater(double Amount);
 double AddSatiety(double Amount);
 bool PrepareSurvivalRefill(double Water,double Satiety,FDWCharacterA01Runtime& OutCandidate)const;
 bool PrepareRootCost(double Cost,FDWCharacterA01Runtime& Out)const;
 bool PrepareCorruption(uint64 ParentActionId,FName ItemId,bool bHarvest,FDWCharacterA01Runtime& OutCandidate,FDWA01CorruptionResult& OutResult)const;
 void EndToolAction(uint64 ParentActionId);
 bool IsPersistenceSettled(FString& Error)const;
 bool CaptureTrialStateJson(FString& OutJson,FString& Error)const;
 bool PrepareTrialStateJson(const FString& Json,const FDWCharacterA01Config& DerivedConfig,FDWCharacterA01Runtime& OutRuntime,double& OutCooldown,FString& Error)const;
 void AdoptTrialStateUnchecked(FDWCharacterA01Runtime&& Candidate,double Cooldown);
 bool BuildConfigForLearned(const TSet<FName>& Learned,FDWCharacterA01Config& Out,FString& Error)const;
 bool BuildLegacyMirror(const FDWCharacterA01Runtime& Candidate,FDWCombatPlayerVitalsConfig& OutConfig,FDWCombatPlayerVitalsSnapshot& OutSnapshot,FString& Error)const;
private:
 bool InitializeTransientRuntime(FString& Error);
 ADWPlayerCharacter* Player()const;
 UDWLanguageSubsystem* Language()const;
 bool BuildConfig(const TFunctionRef<bool(FName)>& Learned,FDWCharacterA01Config& Out,FString& Error)const;
 bool TryAcceptStart();
 bool IsMotionCurrent()const;
 void FinishAbilityAction();
 bool bEnabled=false,bAcceptingAbility=false,bApplyingPaidMotion=false,bReboundPending=false;
 FDWCharacterA01Runtime Runtime;
 FDWA01AbilityPlan Ability;
 FDWCombatInputPress InputOrigin;
 FVector MoveInput=FVector::ZeroVector;
 int32 LastShortcutSlot=INDEX_NONE;
 FName LastShortcutItem;
 DWCombatCommit::FActionId AbilityAction;
 DWCombatCommit::FEntityLife AbilityLife;
 uint64 AbilityEpoch=0,ResourceRevision=0;
 double ReboundRemainingSeconds=0.;
 struct FPaidMotion
 {
  bool bPending=false,bDash=false,bFormExpired=false,bMoved=false,bBlocked=false;
  FDWA01AbilityStep Step;
  FVector Start=FVector::ZeroVector;
  FVector Finish=FVector::ZeroVector,ContactDirection=FVector::ZeroVector;
  TWeakObjectPtr<AActor> ContactEnemy;
  DWCombatCommit::FActionId Action;
  DWCombatCommit::FEntityLife Life;
  uint64 Epoch=0;
 } PaidMotion;
};
