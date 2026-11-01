#include "DWCharacterA01Component.h"
#include "DWCharacterA01SaveBridge.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGatheringComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWGameInstance.h"
#include "DWLanguageSubsystem.h"

bool UDWCharacterA01Component::IsPersistenceSettled(FString& Error)const
{
 auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(this);
 if((Language()&&Language()->IsCombatSandboxSession())||!IsEnabled()||!P||!A||PaidMotion.bPending||IsAbilityActive()||Ability.GetState().Phase==EDWA01AbilityPhase::Pending||P->IsCoreInjecting()||
  P->CombatKnockback.bActive||P->bAdvancingGameplayClocks||P->bApplyingCombatDamage||P->bGameplayActionInProgress)
 {Error=TEXT("A01 capture requires a settled resource/action boundary.");return false;}
 Error.Reset();return true;
}
bool UDWCharacterA01Component::CaptureTrialStateJson(FString& Out,FString& Error)const
{
 if(!IsPersistenceSettled(Error))return false;const auto* P=Player();
 auto Candidate=Runtime;if(!Candidate.SyncCurrentHealth(P->Health)){Error=TEXT("A01 authoritative HP is invalid.");return false;}
 return DWCharacterA01SaveBridge::StateToJson(Candidate.Capture(),Ability.GetState().CooldownRemainingSeconds,Out,Error);
}
bool UDWCharacterA01Component::PrepareTrialStateJson(const FString& Json,const FDWCharacterA01Config& C,FDWCharacterA01Runtime& Out,double& Cooldown,FString& Error)const
{
 const auto* L=Language();if(!IsEnabled()||!L||L->IsCombatSandboxSession()){Error=TEXT("A01 restore requires the exact trial world.");return false;}
 if(!DWCharacterA01SaveBridge::StateFromJson(Json,C,L->Parameter(TEXT("yeast.dash.cooldown_s"),-1.),Out,Cooldown,Error))return false;
 const double MaximumEntry=FMath::Max(L->Parameter(TEXT("yeast.duration_s"),0.),L->Parameter(TEXT("mold.duration_s"),0.))+L->Parameter(TEXT("talent.b3.duration_add_s"),0.);
 if(Out.GetState().EntryDurationSeconds>MaximumEntry){Error=TEXT("Saved A01 entry duration exceeds the configured possible maximum.");return false;}return true;
}
void UDWCharacterA01Component::AdoptTrialStateUnchecked(FDWCharacterA01Runtime&& Candidate,double Cooldown)
{
 // Root already validated the complete language/inventory/world aggregate. No callbacks here.
 CancelAbility(true);Runtime=MoveTemp(Candidate);++ResourceRevision;PaidMotion={};ReboundRemainingSeconds=0.;
 const auto* PC=Player()?Cast<APlayerController>(Player()->GetController()):nullptr;
 const bool Held=PC&&InputOrigin.Edge.Key.IsValid()&&PC->IsInputKeyDown(InputOrigin.Edge.Key);
 Ability.RestoreIdleCooldown(Cooldown,Held);
 if(auto* P=Player()){P->Health=static_cast<float>(Runtime.GetState().CurrentHealth);P->SyncCombatVitalsCompatibility();}
}
bool UDWCharacterA01Component::PrepareFormalAbility(FDWA01AbilityPlan& Out,FString& Error)const
{
 const auto* L=Language();if(!L||!L->GetTrialConfig())return false;
 FDWA01AbilityConfig C;C.HoldThresholdSeconds=L->Parameter(TEXT("input.hold_threshold_s"),-1.);
 C.SprintSpeedMultiplier=L->Parameter(TEXT("yeast.sprint.move_multiplier"),-1.);C.DashSpeedMultiplier=L->Parameter(TEXT("yeast.dash.move_multiplier"),-1.);
 C.DashAccelerationSeconds=L->Parameter(TEXT("yeast.dash.accel_s"),-1.);C.DashHoldSeconds=L->Parameter(TEXT("yeast.dash.hold_s"),-1.);
 C.DashDecelerationSeconds=L->Parameter(TEXT("yeast.dash.decel_s"),-1.);C.DashCooldownSeconds=L->Parameter(TEXT("yeast.dash.cooldown_s"),-1.);
 std::string E;if(!Out.Initialize(C,&E)){Error=UTF8_TO_TCHAR(E.c_str());return false;}return true;
}
void UDWCharacterA01Component::AdoptFormalStateUnchecked(FDWCharacterA01Runtime&& Candidate,FDWA01AbilityPlan&& PreparedAbility,double Cooldown)
{
 CancelAbility(true);Ability=MoveTemp(PreparedAbility);bEnabled=true;
 AdoptTrialStateUnchecked(MoveTemp(Candidate),Cooldown);
}
bool UDWCharacterA01Component::BuildLegacyMirror(const FDWCharacterA01Runtime& Candidate,FDWCombatPlayerVitalsConfig& C,FDWCombatPlayerVitalsSnapshot& S,FString& Error)const
{
 const auto* P=Player();const auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;
 if(!GI||!GI->GetCombatRules()){Error=TEXT("A01 legacy mirror requires validated combat rules.");return false;}
 return DWCharacterA01SaveBridge::BuildMirror(*GI->GetCombatRules(),Candidate,C,S,Error);
}
