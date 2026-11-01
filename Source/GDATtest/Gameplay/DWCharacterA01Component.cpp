#include "DWCharacterA01Component.h"
#include "DWLanguageSubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGatheringComponent.h"
#include "DWInventoryComponent.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWGameInstance.h"
#include "DWCombatRules.h"
#include "DWCharacterA01SaveBridge.h"
#include "DWEnemyCharacter.h"
#include "DWDamageVolumeLibrary.h"
#include "DWAudioLibrary.h"
#include "Dom/JsonObject.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include <limits>

namespace DWCharacterA01ComponentPrivate
{
bool SameLife(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B){return A.Entity&&A.Entity==B.Entity&&A.Life==B.Life;}
bool ReadBool(const TSharedPtr<FJsonObject>& Json,const TCHAR* Key,bool& Out)
{
 const TSharedPtr<FJsonObject>* Params=nullptr;const TSharedPtr<FJsonObject>* Row=nullptr;
 return Json.IsValid()&&Json->TryGetObjectField(TEXT("parameters"),Params)&&(*Params)->TryGetObjectField(Key,Row)&&(*Row)->TryGetBoolField(TEXT("value"),Out);
}
}
UDWCharacterA01Component::UDWCharacterA01Component(){PrimaryComponentTick.bCanEverTick=false;}
ADWPlayerCharacter* UDWCharacterA01Component::Player()const{return Cast<ADWPlayerCharacter>(GetOwner());}
UDWLanguageSubsystem* UDWCharacterA01Component::Language()const{return GetWorld()&&GetWorld()->GetGameInstance()?GetWorld()->GetGameInstance()->GetSubsystem<UDWLanguageSubsystem>():nullptr;}
bool UDWCharacterA01Component::BuildConfig(const TFunctionRef<bool(FName)>& Learned,FDWCharacterA01Config& Out,FString& Error)const
{
 const auto* L=Language();if(!L||!L->GetTrialConfig().IsValid()){Error=TEXT("LANG-01 trial parameters are unavailable.");return false;}
 TSet<FName> Ids;for(const TCHAR* Id:{TEXT("a1"),TEXT("a2"),TEXT("a3"),TEXT("a4"),TEXT("b1"),TEXT("b2"),TEXT("b3"),TEXT("b4"),TEXT("b5"),TEXT("b6")})if(Learned(Id))Ids.Add(Id);
 return DWCharacterA01SaveBridge::BuildDerivedConfig(L->GetTrialConfig(),Ids,Out,Error);
}
bool UDWCharacterA01Component::BuildConfigForLearned(const TSet<FName>& Learned,FDWCharacterA01Config& Out,FString& Error)const
{return BuildConfig([&](FName Id){return Learned.Contains(Id);},Out,Error);}
bool UDWCharacterA01Component::InitializeForTrial(FString& Error)
{
 auto* L=Language();auto* P=Player();if(!L||!L->IsTrialWorld()){Error.Reset();return true;}
 return InitializeTransientRuntime(Error);
}
bool UDWCharacterA01Component::IsEnabled()const
{const auto* L=Language();return bEnabled&&Runtime.IsReady()&&L&&L->HasCurrentA01Context(Player());}
bool UDWCharacterA01Component::InitializeForCombatSandbox(FString& Error)
{
 auto* L=Language();if(bEnabled)return IsEnabled();
 if(!L||!L->StartCombatSandbox(Player(),Error))return false;
 return InitializeTransientRuntime(Error);
}
bool UDWCharacterA01Component::InitializeTransientRuntime(FString& Error)
{
 auto* L=Language();auto* P=Player();
 FDWCharacterA01Config C;if(!L||!P||(!L->IsTrialWorld()&&!L->IsCombatSandboxSession())||!BuildConfig([L](FName Id){return L->HasTalent(Id);},C,Error))return false;
 FDWA01AbilityConfig A;A.HoldThresholdSeconds=L->Parameter(TEXT("input.hold_threshold_s"),-1.);
 A.SprintSpeedMultiplier=L->Parameter(TEXT("yeast.sprint.move_multiplier"),-1.);A.DashSpeedMultiplier=L->Parameter(TEXT("yeast.dash.move_multiplier"),-1.);
 A.DashAccelerationSeconds=L->Parameter(TEXT("yeast.dash.accel_s"),-1.);A.DashHoldSeconds=L->Parameter(TEXT("yeast.dash.hold_s"),-1.);
 A.DashDecelerationSeconds=L->Parameter(TEXT("yeast.dash.decel_s"),-1.);A.DashCooldownSeconds=L->Parameter(TEXT("yeast.dash.cooldown_s"),-1.);
 FDWCharacterA01Runtime Candidate;FDWA01AbilityPlan CandidateAbility;
 std::string E;if(!Candidate.Initialize(C,&E)||!CandidateAbility.Initialize(A,&E)){Error=UTF8_TO_TCHAR(E.c_str());return false;}
 if(!L->GetNativeModel()||!DWCharacterA01SaveBridge::BindDefinitions(L->GetNativeModel()->GetCatalog(),L->GetTrialConfig(),Error))return false;
 Runtime=MoveTemp(Candidate);Ability=MoveTemp(CandidateAbility);bEnabled=true;++ResourceRevision;P->Health=static_cast<float>(Runtime.GetState().CurrentHealth);
 P->SyncCombatVitalsCompatibility();Error.Reset();return true;
}
bool UDWCharacterA01Component::RefreshTalents()
{
 if(!IsEnabled())return false;auto* L=Language();auto* P=Player();FString Error;FDWCharacterA01Config C;
 if(!L||!P||!BuildConfig([L](FName Id){return L->HasTalent(Id);},C,Error))return false;
 auto Candidate=Runtime;Candidate.SyncCurrentHealth(P->Health);if(!Candidate.ApplyConfig(C))return false;
 AdoptRuntimeUnchecked(MoveTemp(Candidate));return true;
}
bool UDWCharacterA01Component::IsAbilityActive()const{return IsEnabled()&&(Ability.IsSprinting()||IsDashing());}
bool UDWCharacterA01Component::IsDashInvulnerable()const{return IsEnabled()&&!bReboundPending&&(bApplyingPaidMotion?PaidMotion.Step.bInvulnerableDuringStep:Ability.IsInvulnerable());}
bool UDWCharacterA01Component::IsDashing()const{return IsEnabled()&&(Ability.IsDashing()||(PaidMotion.bPending&&PaidMotion.bDash)||bReboundPending);}
bool UDWCharacterA01Component::CanUseShortcuts()const{return Language()&&Language()->IsReady()&&CanOpenMenus()&&(!Player()||!Player()->IsCoreInjecting());}
bool UDWCharacterA01Component::CanAdoptRuntime(uint64 Revision,double Health)const{return IsEnabled()&&Player()&&!Player()->IsDead()&&ResourceRevision==Revision&&Player()->Health==Health;}
bool UDWCharacterA01Component::IsDurationTalentContributing()const
{const auto* L=Language();return IsEnabled()&&L&&GetBaseForm()!=EDWCombatBaseForm::Normal&&Runtime.GetState().EntryDurationSeconds>L->Parameter(GetBaseForm()==EDWCombatBaseForm::Yeast?TEXT("yeast.duration_s"):TEXT("mold.duration_s"),0.);}
void UDWCharacterA01Component::BeginRebound(double Seconds){if(IsEnabled()&&FMath::IsFinite(Seconds)&&Seconds>0.){ReboundRemainingSeconds=Seconds;bReboundPending=true;}}
void UDWCharacterA01Component::NotifyShortcutUse(int32 Index)
{
 LastShortcutSlot=Index;LastShortcutItem=NAME_None;FDWInventorySlotSnapshot Slot;
 if(Player()&&Player()->Inventory&&Player()->Inventory->CaptureSlotSnapshot(Index,Slot))LastShortcutItem=Slot.ItemId;
}
bool UDWCharacterA01Component::CanForgetExtraShortcut()const
{
 const auto* P=Player();const auto* W=P?P->WeaponAnimationComponent.Get():nullptr;
 return LastShortcutSlot!=3||!W||LastShortcutItem.IsNone()||W->GetCurrentEquipmentId()!=FDWCombatItemRegistry::CanonicalWeaponItemId(LastShortcutItem)||W->CombatWeaponResources.Revision<MAX_int64;
}
void UDWCharacterA01Component::OnExtraShortcutRemoved()
{
 if(LastShortcutSlot!=3||!Player())return;auto* P=Player();
 if(P->CoreInjection.Plan.IsPending()&&P->CoreInjection.Slot.SlotIndex==3)P->CancelCoreInjectionFacts();
 if(P->GatheringComponent&&P->GatheringComponent->GetActiveTool()==LastShortcutItem)P->GatheringComponent->ClearActiveToolAfterShortcutForgotten();
 if(auto* W=P->WeaponAnimationComponent.Get();W&&!LastShortcutItem.IsNone()&&W->GetCurrentEquipmentId()==FDWCombatItemRegistry::CanonicalWeaponItemId(LastShortcutItem))
 {
  // Same no-callback native cancellation used by complete world restore; inventory/ownership and cooldowns survive.
  W->EndFormalBow();W->EndFormalBottle();W->EndFormalSpore();W->EndFormalSpear();W->EndFormalUnarmed();W->EndFormalAxe();W->EndFormalSword();W->EndFormalDagger();W->EndFormalShotgun();W->EndWeaponResourceActions();
  W->bActive=W->bPrimaryHeld=W->bSecondaryHeld=W->bCharging=W->bChargeReady=W->bReloading=false;
  W->PrimaryInputOrigin.Reset();W->SecondaryInputOrigin.Reset();W->ActiveSet=nullptr;W->ActiveClip={};W->ElapsedSeconds=0.;W->FiredEvents.Reset();
  W->ClearShieldCounterFacts();W->CombatWeaponResources.Snapshot.CurrentEquipmentId=NAME_None;++W->CombatWeaponResources.Revision;
  W->SelectedWeapon=EDWAnimatedWeapon::Unarmed;W->AmmoInMagazine=0;P->PaidCombatWeaponSlice={};
 }
 LastShortcutSlot=INDEX_NONE;LastShortcutItem=NAME_None;
}
EDWCombatBaseForm UDWCharacterA01Component::GetBaseForm()const{return static_cast<EDWCombatBaseForm>(Runtime.GetState().Form);}
double UDWCharacterA01Component::GetSpecial()const{return GetBaseForm()==EDWCombatBaseForm::Yeast?Runtime.GetState().Gas:GetBaseForm()==EDWCombatBaseForm::Mold?Runtime.GetState().Corruption:0.;}
double UDWCharacterA01Component::GetWalkSpeedCmPerSecond()const
{if(Player()&&Player()->IsFormallyRooted())return 0.;const auto* L=Language();return L?L->Parameter(TEXT("move.base_speed_mps"),0.)*100.*Runtime.MovementMultiplier()*(Ability.IsSprinting()?Ability.GetConfig().SprintSpeedMultiplier:1.):0.;}
void UDWCharacterA01Component::SyncAuthoritativeHealth(){if(IsEnabled()&&Player())Runtime.SyncCurrentHealth(Player()->Health);}
void UDWCharacterA01Component::AdoptRuntimeUnchecked(FDWCharacterA01Runtime&& Candidate,bool FormEntry)
{
 const auto OldForm=Runtime.GetState().Form;Runtime=MoveTemp(Candidate);++ResourceRevision;
 if(auto* P=Player())
 {
  P->Health=FMath::Min(P->Health,static_cast<float>(Runtime.EffectiveMaxHealth()));
  if(OldForm!=Runtime.GetState().Form||FormEntry)CancelAbility();
  if(P->IsFormallyRooted()&&(Runtime.GetState().Form!=EDWA01Form::Mold||Runtime.GetState().Corruption<=0.))
   P->EndFormalRootFacts(Runtime.GetState().Form!=EDWA01Form::Mold?EDWMoldRootEnd::FormExpired:EDWMoldRootEnd::SpecialExhausted);
  if(FormEntry)
   if(const auto* PC=Cast<APlayerController>(P->GetController());PC&&InputOrigin.Edge.Key.IsValid()&&PC->IsInputKeyDown(InputOrigin.Edge.Key))Ability.RequireRelease();
  P->SyncCombatVitalsCompatibility();
 }
}
EDWCombatBaseForm UDWCharacterA01Component::FormForEssence(FName Item,bool Legacy)
{
 if(Item==TEXT("MoldEssence")||(Legacy&&Item==TEXT("DWCore_Mold")))return EDWCombatBaseForm::Mold;
 if(Item==TEXT("YeastEssence")||(Legacy&&Item==TEXT("DWCore_Yeast")))return EDWCombatBaseForm::Yeast;
 return EDWCombatBaseForm::Normal;
}
bool UDWCharacterA01Component::PrepareFormEntry(EDWCombatBaseForm Form,FDWCharacterA01Runtime& Out)const
{
 if(!IsEnabled()||!Language()||!Language()->IsReady()||!Player()||Player()->IsDead()||IsAbilityActive()||Form==EDWCombatBaseForm::Normal)return false;
 auto C=Runtime;C.SyncCurrentHealth(Player()->Health);if(!C.EnterForm(static_cast<EDWA01Form>(Form)))return false;Out=MoveTemp(C);return true;
}
double UDWCharacterA01Component::InjectionSeconds()const{return Language()?Language()->Parameter(TEXT("essence.injection_duration_s"),0.):0.;}
int32 UDWCharacterA01Component::EssenceCount()const{return Language()?static_cast<int32>(Language()->Parameter(TEXT("essence.use_count"),0.)):0;}
double UDWCharacterA01Component::AddWater(double Amount){if(!IsEnabled())return 0.;const double Added=Runtime.AddWater(Amount);if(Added>0.)++ResourceRevision;return Added;}
double UDWCharacterA01Component::AddSatiety(double Amount){if(!IsEnabled())return 0.;const double Added=Runtime.AddSatiety(Amount);if(Added>0.)++ResourceRevision;return Added;}
bool UDWCharacterA01Component::PrepareSurvivalRefill(double Water,double Satiety,FDWCharacterA01Runtime& Out)const
{
 if(!IsEnabled()||!Language()||!Language()->IsReady()||!Player()||Player()->IsDead()||!FMath::IsFinite(Water)||Water<0.||!FMath::IsFinite(Satiety)||Satiety<0.)return false;
 Out=Runtime;Out.SyncCurrentHealth(Player()->Health);Out.AddWater(Water);Out.AddSatiety(Satiety);return true;
}
bool UDWCharacterA01Component::PrepareRootCost(double Cost,FDWCharacterA01Runtime& Out)const
{
 if(!IsEnabled()||!Player()||Player()->IsDead())return false;
 Out=Runtime;Out.SyncCurrentHealth(Player()->Health);return Out.SpendCorruption(Cost);
}
bool UDWCharacterA01Component::PrepareCorruption(uint64 Action,FName Item,bool Harvest,FDWCharacterA01Runtime& Out,FDWA01CorruptionResult& Result)const
{
 Result={};if(!IsEnabled()||!Action||!Player()||Player()->IsDead()||GetBaseForm()!=EDWCombatBaseForm::Mold)return false;
 bool Eligible=false;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;auto* L=Language();
 if(L&&L->GetTrialConfig().IsValid()&&L->GetTrialConfig()->TryGetArrayField(TEXT("corruption_whitelist"),Rows))
  for(const auto& Row:*Rows)if(Row.IsValid()&&Row->Type==EJson::Object)
  {FString Id;bool Allowed=false;const auto J=Row->AsObject();if(J->TryGetStringField(TEXT("item_id"),Id)&&FName(*Id)==Item&&J->TryGetBoolField(Harvest?TEXT("harvest"):TEXT("damage"),Allowed)&&Allowed){Eligible=true;break;}}
 if(!Eligible)return false;Out=Runtime;Out.SyncCurrentHealth(Player()->Health);
 bool StillInShortcut=true;
 if(!Harvest)
 {
  StillInShortcut=false;const int32 Count=3+((L&&L->HasTalent(TEXT("a1")))?1:0);
  if(Player()->Inventory)for(int32 Index=0;Index<Count;++Index){FDWInventorySlotSnapshot Slot;
   if(Player()->Inventory->CaptureSlotSnapshot(Index,Slot)&&Slot.Quantity>0&&FDWCombatItemRegistry::CanonicalWeaponItemId(Slot.ItemId)==FDWCombatItemRegistry::CanonicalWeaponItemId(Item)){StillInShortcut=true;break;}}
 }
 Out.BeginToolAction(Action,!Harvest,Harvest);Result=Out.ResolveToolHit(Action,true,StillInShortcut);return true;
}
void UDWCharacterA01Component::EndToolAction(uint64 Action){if(IsEnabled())Runtime.EndToolAction(Action);}
bool UDWCharacterA01Component::HandleSpacePressed(const FDWCombatInputPress& Press)
{
 InputOrigin=Press;auto* P=Player();if(!IsEnabled()||!P||!P->IsCombatActionAllowed()||P->IsCoreInjecting()||P->CombatKnockback.bActive)return false;
 if(GetBaseForm()==EDWCombatBaseForm::Mold)return P->TryToggleFormalRoot(); // Same deduplicated Space edge; E remains the existing stance shortcut.
 if(GetBaseForm()!=EDWCombatBaseForm::Yeast)return false;
 if(bReboundPending){Ability.RequireRelease();return false;}
 return Ability.Press();
}
bool UDWCharacterA01Component::HandleSpaceReleased(const FDWCombatInputPress& Press)
{
 if(!IsEnabled()||!Player()||Press.Edge.Context!=InputOrigin.Edge.Context||Press.Edge.Sequence!=InputOrigin.Edge.Sequence)return false;
 FDWA01AbilityStep Step;const FVector Face=Player()->GetActorForwardVector();const bool Result=Ability.Release(Face.X,Face.Y,Step);
 if(Step.bSprintEnded)FinishAbilityAction();if(Step.StartRequest!=EDWA01AbilityStart::None)TryAcceptStart();Player()->RefreshCombatMovementSpeed();return Result;
}
void UDWCharacterA01Component::FinishAbilityAction()
{if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(AbilityAction);AbilityAction={};}
void UDWCharacterA01Component::CancelAbility(bool RequireRelease)
{
 if(!IsEnabled()||bAcceptingAbility)return;Ability.Cancel(RequireRelease);PaidMotion={};ReboundRemainingSeconds=0.;bReboundPending=false;FinishAbilityAction();
 if(auto* P=Player()){P->bSprinting=false;P->RefreshCombatMovementSpeed();}
}
bool UDWCharacterA01Component::PrepareForAttack()
{
 if(!IsEnabled())return true;if(IsDashing())return false;
 if(Ability.IsSprinting()||Ability.GetState().Phase==EDWA01AbilityPhase::Pending)CancelAbility(true);return true;
}
bool UDWCharacterA01Component::TryAcceptStart()
{
 auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(this);const auto Request=Ability.GetState().StartRequest;
 if(Request==EDWA01AbilityStart::None)return true;
 const auto Reject=[&](){Ability.RejectStart();return false;};
 if(!P||!A||!P->IsCombatActionAllowed()||P->IsCoreInjecting()||P->CombatKnockback.bActive||bReboundPending||GetBaseForm()!=EDWCombatBaseForm::Yeast)return Reject();
 const auto* L=Language();if(!L)return Reject();auto Resources=Runtime;Resources.SyncCurrentHealth(P->Health);auto Plan=Ability;
 if(Request==EDWA01AbilityStart::Dash)
 {if(!Resources.SpendGas(L->Parameter(TEXT("yeast.dash.gas_cost"),-1.))||!Plan.AcceptDash())return Reject();}
 else if(MoveInput.IsNearlyZero()||Resources.GetState().Gas<L->Parameter(TEXT("input.sprint_start_min_gas"),1.)||!Plan.AcceptSprint())return Reject();
 const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch();
 TGuardValue<bool> AdmissionGuard(bAcceptingAbility,true);P->CancelCombatActions();
 A=UDWCombatAuthoritySubsystem::Get(this);if(!A||A->GetWorldEpoch()!=Epoch||!DWCharacterA01ComponentPrivate::SameLife(A->CaptureActorLife(P),Life)||!P->IsCombatActionAllowed()||P->IsCoreInjecting()||P->CombatKnockback.bActive||GetBaseForm()!=EDWCombatBaseForm::Yeast)return Reject();
 Resources=Runtime;Resources.SyncCurrentHealth(P->Health); // Cancellation retired old tool actions; never restore their stale copies.
 if(Request==EDWA01AbilityStart::Dash&&!Resources.SpendGas(L->Parameter(TEXT("yeast.dash.gas_cost"),-1.)))return Reject();
 const auto Action=A->BeginAction(P);if(!Action.Id)return Reject();
 if(Request==EDWA01AbilityStart::Dash)
 {
  auto* Ledger=A->GetLedgerForResourceTransaction();if(!Ledger){A->EndAction(Action);return Reject();}
  const auto Receipt=Ledger->AdmitResource({Action,1,DWCombatCommit::EResourceKind::YeastDashStart,1});
  if(Receipt.Result!=DWCombatCommit::EAdmission::Admitted||Ledger->Resolve(Receipt.Receipt.Handle,DWCombatCommit::EOutcome::Committed)!=DWCombatCommit::EResolve::Resolved)
  {A->EndAction(Action);return Reject();}
 }
 Runtime=MoveTemp(Resources);Ability=MoveTemp(Plan);++ResourceRevision;AbilityAction=Action;AbilityLife=Life;AbilityEpoch=Epoch;
 P->SetHarvestHeld(false);P->bSprinting=Ability.IsSprinting();P->RefreshCombatMovementSpeed();
 if(Request==EDWA01AbilityStart::Dash)
 {
  P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();
  P->SetActorRotation(FVector(Ability.GetState().DashDirectionX,Ability.GetState().DashDirectionY,0.).Rotation());
  P->TrailElapsed=P->DashTrailInterval;
  if(P->CanPlayPlayerVFX())P->PlayPlayerBurstVFX(P->DashStartComponent,P->DashStartNiagara,P->DashStartOffset,P->DashStartRotation,P->DashStartScale);
  UDWAudioLibrary::PlayWorldSound(P,P->DashStartSound,P->GetActorLocation(),P->DashStartSoundVolume,P->DashStartSoundPitch);P->OnDashStarted();
 }
 return true;
}
double UDWCharacterA01Component::GetNextAbilityBoundary(double Maximum)const
{
 if(!IsEnabled())return std::numeric_limits<double>::infinity();if(Ability.GetState().StartRequest!=EDWA01AbilityStart::None)return 0.;
 if(ReboundRemainingSeconds>0.)return Maximum>0.?FMath::Min(Maximum,ReboundRemainingSeconds):std::numeric_limits<double>::infinity();
 if(bReboundPending)return Maximum>0.?Maximum:std::numeric_limits<double>::infinity();
 if(Maximum<=0.)return std::numeric_limits<double>::infinity();auto Copy=Ability;FDWA01AbilityStep Step;
 // The ability clock needs nominal walk tuning even while the separate rooted stance reports zero effective speed.
 const auto* L=Language();const double Nominal=L?L->Parameter(TEXT("move.base_speed_mps"),0.)*100.*Runtime.MovementMultiplier():0.;
 if(!Copy.AdvanceNext(Maximum,Nominal,MoveInput.X,MoveInput.Y,Step))return 0.;
 return Step.AdvancedSeconds;
}
bool UDWCharacterA01Component::AdvanceSlice(double Maximum,FDWCombatPlayerVitalsStep& Legacy,FDWCharacterA01Step& Step)
{
 Legacy={};if(!IsEnabled()||!Player()||PaidMotion.bPending)return false;
 if(Player()->IsFormallyRooted()&&(GetBaseForm()!=EDWCombatBaseForm::Mold||GetSpecial()<=0.))
  Player()->EndFormalRootFacts(EDWMoldRootEnd::SpecialExhausted);
 auto Candidate=Runtime;Candidate.SyncCurrentHealth(Player()->Health);auto Plan=Ability;
 FDWCharacterA01Activity Activity;Activity.GasSpendPerSecond=Ability.IsSprinting()?Language()->Parameter(TEXT("yeast.sprint.gas_per_s"),-1.):0.;
 Activity.bSuppressGasRegeneration=Ability.IsDashing()||bReboundPending;Activity.bToolActionActive=Player()->GatheringComponent&&Player()->GatheringComponent->IsGatheringSwingActive();
 if(Player()->IsFormallyRooted())
 {Activity.CorruptionSpendPerSecond=Player()->FormalRoot.GetConfig().SpendPerSecond;Activity.bSuppressCorruptionRegeneration=true;}
 if(!Candidate.AdvanceNext(Maximum,Activity,Step)||Step.AdvancedSeconds<=0.)return false;
 FDWA01AbilityStep Movement;const bool WasDash=Ability.IsDashing();
 const double Walk=Language()->Parameter(TEXT("move.base_speed_mps"),0.)*100.*Runtime.MovementMultiplier();
 if(bReboundPending)Movement.AdvancedSeconds=Step.AdvancedSeconds;
 else if(!Plan.AdvanceNext(Step.AdvancedSeconds,Walk,MoveInput.X,MoveInput.Y,Movement)||Movement.AdvancedSeconds!=Step.AdvancedSeconds)return false;
 Runtime=MoveTemp(Candidate);Ability=MoveTemp(Plan);++ResourceRevision;
 if(Player()->IsFormallyRooted()&&(Step.bFormExpired||GetSpecial()<=0.))
 {Player()->CombatFrameRootExitOffset=Step.AdvancedSeconds;Player()->EndFormalRootFacts(Step.bFormExpired?EDWMoldRootEnd::FormExpired:EDWMoldRootEnd::SpecialExhausted);}
 // Keep the real pre-step HP unchanged. The authority does damage -> downward cap -> regen atomically.
 if(ReboundRemainingSeconds>0.)ReboundRemainingSeconds=FMath::Max(0.,ReboundRemainingSeconds-Step.AdvancedSeconds);
 PaidMotion.bPending=true;PaidMotion.bDash=WasDash;PaidMotion.bFormExpired=Step.bFormExpired;PaidMotion.Step=Movement;
 PaidMotion.Start=Player()->GetActorLocation();PaidMotion.Action=AbilityAction;PaidMotion.Life=AbilityLife;PaidMotion.Epoch=AbilityEpoch;
 if(Step.bFormExpired){Ability.Cancel();if(bReboundPending)Player()->ResetCombatKnockbackFacts();}
 Legacy.AdvancedSeconds=Step.AdvancedSeconds;Legacy.bFormExpired=Step.bFormExpired;
 if(Step.HungerDamageDue>0.){Legacy.Starvation.EventCount=1;Legacy.Starvation.DamagePerEvent=Step.HungerDamageDue;Legacy.StarvationOrdinal=Runtime.GetState().StarvationOrdinal;}
 Player()->SyncCombatVitalsCompatibility();return true;
}
bool UDWCharacterA01Component::IsMotionCurrent()const
{
 auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(this);return P&&A&&!P->IsDead()&&!P->bRestoringSave&&
  A->GetWorldEpoch()==PaidMotion.Epoch&&DWCharacterA01ComponentPrivate::SameLife(A->CaptureActorLife(P),PaidMotion.Life)&&A->IsActionOwnedBy(PaidMotion.Action,P);
}
bool UDWCharacterA01Component::PumpPaidMotion()
{
 auto* P=Player();if(!IsEnabled()||!P)return true;if(!P->IsCombatActionAllowed())return false;
 if(bReboundPending&&ReboundRemainingSeconds<=0.&&!P->CombatKnockback.bActive)bReboundPending=false;
 if(Ability.GetState().bReleaseRequired&&InputOrigin.Edge.Key.IsValid())
  if(const auto* PC=Cast<APlayerController>(P->GetController());PC&&!PC->IsInputKeyDown(InputOrigin.Edge.Key))
  {FDWA01AbilityStep Unused;const auto Face=P->GetActorForwardVector();Ability.Release(Face.X,Face.Y,Unused);}
 if(Ability.IsSprinting()&&(MoveInput.IsNearlyZero()||Runtime.GetState().Gas<=0.)){CancelAbility(true);return true;}
 if(!PaidMotion.bPending)return TryAcceptStart()||P->IsCombatActionAllowed();
 auto Paid=PaidMotion;
 if(Paid.bDash)
 {
  if(!IsMotionCurrent()||!P->GetActorLocation().Equals(Paid.bMoved?Paid.Finish:Paid.Start,UE_DOUBLE_SMALL_NUMBER)){CancelAbility(true);return true;}
  const FVector Direction(Paid.Step.DirectionX,Paid.Step.DirectionY,0.);const FVector Desired=Paid.Start+Direction*Paid.Step.DistanceCm;
  if(!Paid.bMoved)
  {
  FCollisionQueryParams Query(SCENE_QUERY_STAT(DWA01Dash),false,P);TArray<FHitResult> Hits;
  const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
  const float Radius=P->GetCapsuleComponent()->GetScaledCapsuleRadius();
  GetWorld()->SweepMultiByObjectType(Hits,Paid.Start,Desired,FQuat::Identity,Objects,FCollisionShape::MakeSphere(Radius),Query);
  FHitResult EnemyHit;bool HasEnemy=false;
  if(!Paid.bFormExpired)for(const auto& H:Hits)if(auto* E=Cast<ADWEnemyCharacter>(H.GetActor());E&&E->IsAlive()&&UDWDamageVolumeLibrary::IsDamageHitComponent(H.GetComponent())&&(!HasEnemy||H.Time<EnemyHit.Time))
  {EnemyHit=H;HasEnemy=true;}
  const FVector Target=HasEnemy?FMath::Lerp(Paid.Start,Desired,FMath::Clamp(EnemyHit.Time,0.f,1.f)):Desired;
  FHitResult Wall;{TGuardValue<bool> Moving(P->bApplyingFormalDashMotion,true);TGuardValue<bool> PhaseGuard(bApplyingPaidMotion,true);P->AddActorWorldOffset(Target-Paid.Start,true,&Wall,ETeleportType::None);}
  if(!IsMotionCurrent()){PaidMotion={};return P->IsCombatActionAllowed();}
  bool HitEnemy=HasEnemy&&(!Wall.bBlockingHit||Wall.GetActor()==EnemyHit.GetActor());
  if(auto* NativeContact=Cast<ADWEnemyCharacter>(Wall.GetActor());!Paid.bFormExpired&&NativeContact&&NativeContact->IsAlive())
  {HitEnemy=true;EnemyHit=Wall;}
  Paid.bMoved=true;Paid.Finish=P->GetActorLocation();Paid.bBlocked=Wall.bBlockingHit;
  if(HitEnemy){Paid.ContactEnemy=EnemyHit.GetActor();Paid.ContactDirection=(-EnemyHit.ImpactNormal).GetSafeNormal2D();if(Paid.ContactDirection.IsNearlyZero())Paid.ContactDirection=Direction;}
  PaidMotion=Paid;
  }
  if(!P->IsCombatActionAllowed())return false; // Resume this already-paid, already-moved slice without another move/debit.
  const bool HitEnemy=Paid.ContactEnemy.IsValid();
  if(HitEnemy)
  {
   auto* A=UDWCombatAuthoritySubsystem::Get(this);auto* L=Language();FDWCombatDamageCommand C;
   C.Identity={Paid.Action,1,DWCombatCommit::EActionDamageKind::Dash,1};C.Source=P;C.Target=Paid.ContactEnemy;C.TargetLife=A->CaptureActorLife(Paid.ContactEnemy.Get());C.Instigator=P->GetController();
   C.Amount=static_cast<float>(L->Parameter(TEXT("yeast.dash.damage"),50.));C.KnockbackDirection=Paid.ContactDirection;
   C.KnockbackDistance=static_cast<float>(L->Parameter(TEXT("yeast.dash.enemy_rebound_m"),1.6)*100.);C.KnockbackSeconds=static_cast<float>(L->Parameter(TEXT("yeast.dash.rebound_duration_s"),.18));
   A->SubmitActionDamage(C);if(IsValid(P)&&!P->IsActorBeingDestroyed())Ability.EndOnHit();
  }
  else if(Paid.bBlocked)Ability.EndOnWall();
 }
 PaidMotion={};if(Paid.Step.bDashEnded||Paid.Step.bSprintEnded||Paid.bFormExpired||(!Ability.IsDashing()&&!Ability.IsSprinting()&&AbilityAction.Id))FinishAbilityAction();
 P->RefreshCombatMovementSpeed();if(!P->IsCombatActionAllowed())return false;TryAcceptStart();return P->IsCombatActionAllowed();
}
