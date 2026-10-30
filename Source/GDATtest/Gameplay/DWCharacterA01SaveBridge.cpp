#include "DWCharacterA01SaveBridge.h"
#include "DWFormalLanguageProjection.h"
#include "DWCombatRules.h"
#include "DWSaveGame.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include <algorithm>
#include <cmath>

namespace DWA01SavePrivate
{
struct FDefinitions {DWLanguage::FCatalog Catalog;TSharedPtr<FJsonObject> Trial;};
FCriticalSection Mutex;
TSharedPtr<const FDefinitions,ESPMode::ThreadSafe> Definitions;
bool Number(const TSharedPtr<FJsonObject>& Trial,const TCHAR* Key,double& Out)
{
 const TSharedPtr<FJsonObject>* P=nullptr;const TSharedPtr<FJsonObject>* Row=nullptr;
 return Trial.IsValid()&&Trial->TryGetObjectField(TEXT("parameters"),P)&&(*P)->TryGetObjectField(Key,Row)&&(*Row)->TryGetNumberField(TEXT("value"),Out)&&FMath::IsFinite(Out);
}
bool Boolean(const TSharedPtr<FJsonObject>& Trial,const TCHAR* Key,bool& Out)
{
 const TSharedPtr<FJsonObject>* P=nullptr;const TSharedPtr<FJsonObject>* Row=nullptr;
 return Trial.IsValid()&&Trial->TryGetObjectField(TEXT("parameters"),P)&&(*P)->TryGetObjectField(Key,Row)&&(*Row)->TryGetBoolField(TEXT("value"),Out);
}
bool Fail(FString& Error,const TCHAR* Text){Error=Text;return false;}
}
bool DWCharacterA01SaveBridge::IsExactTrialMap(const FString& Package)
{return Package==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest");}
bool DWCharacterA01SaveBridge::HasFormalFields(const UDWSaveGame& S)
{return S.FormalLanguage.Schema!=0||S.FormalLanguage.SlotIndex!=INDEX_NONE||!S.FormalLanguage.ProgressJson.IsEmpty()||!S.FormalLanguage.A01StateJson.IsEmpty();}
bool DWCharacterA01SaveBridge::HasTrialFields(const UDWSaveGame& S)
{return S.LanguageTrialSchema!=0||!S.LanguageProgressJson.IsEmpty()||!S.A01StateJson.IsEmpty();}
bool DWCharacterA01SaveBridge::BuildDerivedConfig(const TSharedPtr<FJsonObject>& J,const TSet<FName>& Learned,FDWCharacterA01Config& Out,FString& Error)
{
 FDWCharacterA01Config C;
 struct FRead{const TCHAR* Key;double FDWCharacterA01Config::*Member;};
 const FRead Reads[]={
  {TEXT("health.base_max"),&FDWCharacterA01Config::HealthyMax},{TEXT("health.cap_loss.minimum_effective_max"),&FDWCharacterA01Config::MinimumEffectiveMax},
  {TEXT("health.current_initial"),&FDWCharacterA01Config::InitialHealth},{TEXT("satiety.max"),&FDWCharacterA01Config::SatietyMax},
  {TEXT("satiety.initial"),&FDWCharacterA01Config::InitialSatiety},{TEXT("hydration.max"),&FDWCharacterA01Config::WaterMax},{TEXT("hydration.initial"),&FDWCharacterA01Config::InitialWater},
  {TEXT("satiety.drain_amount"),&FDWCharacterA01Config::SatietyDrainAmount},{TEXT("satiety.drain_interval_s"),&FDWCharacterA01Config::SatietyDrainIntervalSeconds},
  {TEXT("hydration.drain_amount"),&FDWCharacterA01Config::WaterDrainAmount},{TEXT("hydration.drain_interval_s"),&FDWCharacterA01Config::WaterDrainIntervalSeconds},
  {TEXT("satiety.empty_health_loss_per_s"),&FDWCharacterA01Config::StarvationDamagePerSecond},{TEXT("hydration.empty_move_multiplier"),&FDWCharacterA01Config::ThirstMovementMultiplier},
  {TEXT("health.cap_loss.amount"),&FDWCharacterA01Config::CapLossAmount},{TEXT("health.cap_loss.interval_s"),&FDWCharacterA01Config::CapLossIntervalSeconds},
  {TEXT("health.cap_recovery.amount"),&FDWCharacterA01Config::CapRecoveryAmount},{TEXT("health.cap_recovery.interval_s"),&FDWCharacterA01Config::CapRecoveryIntervalSeconds},
  {TEXT("health.normal_regen.amount"),&FDWCharacterA01Config::NormalRegenAmount},{TEXT("health.normal_regen.interval_s"),&FDWCharacterA01Config::NormalRegenIntervalSeconds},
  {TEXT("yeast.duration_s"),&FDWCharacterA01Config::YeastDurationSeconds},{TEXT("mold.duration_s"),&FDWCharacterA01Config::MoldDurationSeconds},
  {TEXT("yeast.satiety_drain_multiplier"),&FDWCharacterA01Config::YeastSatietyMultiplier},{TEXT("yeast.hydration_drain_multiplier"),&FDWCharacterA01Config::YeastWaterMultiplier},
  {TEXT("mold.satiety_drain_multiplier"),&FDWCharacterA01Config::MoldSatietyMultiplier},{TEXT("mold.hydration_drain_multiplier"),&FDWCharacterA01Config::MoldWaterMultiplier},
  {TEXT("yeast.energy_max"),&FDWCharacterA01Config::GasMax},{TEXT("mold.energy_max"),&FDWCharacterA01Config::CorruptionMax},
  {TEXT("yeast.energy_entry_fraction"),&FDWCharacterA01Config::GasEntryFraction},{TEXT("mold.energy_entry_fraction"),&FDWCharacterA01Config::CorruptionEntryFraction},
  {TEXT("yeast.gas_regen_per_s"),&FDWCharacterA01Config::GasRegenPerSecond},{TEXT("mold.corruption_regen_per_s"),&FDWCharacterA01Config::CorruptionRegenPerSecond},
  {TEXT("mold.corruption_cost"),&FDWCharacterA01Config::CorruptionCost},{TEXT("mold.damage_bonus"),&FDWCharacterA01Config::CorruptionDamageBonus},{TEXT("mold.harvest_bonus"),&FDWCharacterA01Config::CorruptionHarvestBonus}};
 for(const auto& R:Reads)if(!DWA01SavePrivate::Number(J,R.Key,C.*R.Member)){Error=FString(TEXT("Missing numeric A01 parameter: "))+R.Key;return false;}
 struct FAdd{const TCHAR* Talent;const TCHAR* Key;double FDWCharacterA01Config::*Member;};
 for(const auto& A:{FAdd{TEXT("a4"),TEXT("talent.a4.base_health_max_add"),&FDWCharacterA01Config::HealthyMax},
  FAdd{TEXT("a2"),TEXT("talent.a2.satiety_max_add"),&FDWCharacterA01Config::SatietyMax},FAdd{TEXT("a3"),TEXT("talent.a3.hydration_max_add"),&FDWCharacterA01Config::WaterMax},
  FAdd{TEXT("b4"),TEXT("talent.b4.gas_max_add"),&FDWCharacterA01Config::GasMax},FAdd{TEXT("b5"),TEXT("talent.b5.corruption_max_add"),&FDWCharacterA01Config::CorruptionMax}})
  if(Learned.Contains(A.Talent)){double V;if(!DWA01SavePrivate::Number(J,A.Key,V))return DWA01SavePrivate::Fail(Error,TEXT("Missing talent-derived A01 parameter."));C.*A.Member+=V;}
 if(Learned.Contains(TEXT("b1")))C.YeastSatietyMultiplier=1.;if(Learned.Contains(TEXT("b2")))C.MoldWaterMultiplier=1.;
 if(Learned.Contains(TEXT("b3"))){double V;if(!DWA01SavePrivate::Number(J,TEXT("talent.b3.duration_add_s"),V))return false;C.YeastDurationSeconds+=V;C.MoldDurationSeconds+=V;}
 if(Learned.Contains(TEXT("b6"))&&!DWA01SavePrivate::Number(J,TEXT("talent.b6.max_health_cap_loss_fraction"),C.MaximumLossFraction))return false;
 if(!DWA01SavePrivate::Boolean(J,TEXT("health.normal_regen.requires_cap_loss"),C.bNormalRegenRequiresCapLoss)||
    !DWA01SavePrivate::Boolean(J,TEXT("health.regen_while_starving"),C.bRegenWhileStarving)||
    !DWA01SavePrivate::Boolean(J,TEXT("health.clamp_current_hp_on_cap_decrease"),C.bClampCurrentHealthToEffectiveMax))return DWA01SavePrivate::Fail(Error,TEXT("Missing A01 boolean policy."));
 std::string E;if(!C.IsValid(&E)){Error=UTF8_TO_TCHAR(E.c_str());return false;}Out=C;Error.Reset();return true;
}
bool DWCharacterA01SaveBridge::BindDefinitions(const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,FString& Error)
{
 FDWCharacterA01Config C;if(!DWLanguage::ValidateCatalog(Catalog,Error)||!BuildDerivedConfig(Trial,{},C,Error))return false;
 FString Json;auto Writer=TJsonWriterFactory<>::Create(&Json);if(!FJsonSerializer::Serialize(Trial.ToSharedRef(),Writer))return false;
 auto Copy=MakeShared<DWA01SavePrivate::FDefinitions,ESPMode::ThreadSafe>();Copy->Catalog=Catalog;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Copy->Trial))return false;
 FScopeLock Lock(&DWA01SavePrivate::Mutex);DWA01SavePrivate::Definitions=Copy;Error.Reset();return true;
}
bool DWCharacterA01SaveBridge::BuildMirror(const FDWCombatRules& Rules,const FDWCharacterA01Runtime& R,FDWCombatPlayerVitalsConfig& C,FDWCombatPlayerVitalsSnapshot& S,FString& Error)
{
 if(!R.IsReady()||!FDWCombatPlayerVitalsConfig::Build(Rules,C,Error))return false;
 const auto& A=R.GetConfig();const auto& V=R.GetState();
 C.Vitals.HealthMax=A.HealthyMax;C.Vitals.HealthFloor=0.;C.Vitals.WaterMax=A.WaterMax;C.Vitals.WaterFloor=0.;C.Vitals.SatietyMax=A.SatietyMax;C.Vitals.SatietyFloor=0.;
 C.Vitals.SpecialMax=FMath::Max(A.GasMax,A.CorruptionMax);C.Vitals.SpecialFloor=C.Vitals.NormalSpecialValue=0.;
 C.Vitals.InitialHealthFraction=A.InitialHealth/A.HealthyMax;C.Vitals.InitialWaterFraction=A.InitialWater/A.WaterMax;C.Vitals.InitialSatietyFraction=A.InitialSatiety/A.SatietyMax;
 C.Vitals.WaterDrainPerSecond=C.Vitals.SatietyDrainPerSecond=C.Vitals.WaterDamagePerEvent=C.Vitals.SatietyDamagePerEvent=0.;
 C.FormDurationSeconds=FMath::Max(V.EntryDurationSeconds,FMath::Max(A.YeastDurationSeconds,A.MoldDurationSeconds));C.InitialFormSpecial=0.;C.SpecialGainPerSecond=0.;
 C.NormalIncomingMultiplier=C.YeastIncomingMultiplier=C.MoldIncomingMultiplier=1.;C.YeastMoveMultiplier=1.;
 S={};S.ResourceSchema=FDWCombatPlayerVitalsRuntime::ResourceSchema;S.RulesCombatSchema=Rules.GetCombatSchema();
 S.StarvationOrdinal=V.StarvationOrdinal;
 S.Health=V.CurrentHealth;S.Water=V.Water;S.Satiety=V.Satiety;S.BaseForm=static_cast<EDWCombatBaseForm>(V.Form);S.FormRemainingSeconds=V.FormRemainingSeconds;
 S.Special=V.Form==EDWA01Form::Yeast?V.Gas:V.Form==EDWA01Form::Mold?V.Corruption:0.;
 return FDWCombatPlayerVitalsRuntime::Validate(C,S,Error);
}
bool DWCharacterA01SaveBridge::StateToJson(const FDWCharacterA01State& S,double Cooldown,FString& Out,FString& Error)
{
 if(!FMath::IsFinite(Cooldown)||Cooldown<0.)return false;auto J=MakeShared<FJsonObject>();
 J->SetNumberField(TEXT("schema_version"),1.);J->SetNumberField(TEXT("health"),S.CurrentHealth);J->SetNumberField(TEXT("water"),S.Water);J->SetNumberField(TEXT("satiety"),S.Satiety);
 J->SetNumberField(TEXT("recorded_cap_loss"),S.RecordedCapLoss);J->SetNumberField(TEXT("gas"),S.Gas);J->SetNumberField(TEXT("corruption"),S.Corruption);J->SetBoolField(TEXT("corruption_initialized"),S.bCorruptionInitialized);
 J->SetNumberField(TEXT("form"),static_cast<int32>(S.Form));J->SetNumberField(TEXT("form_remaining"),S.FormRemainingSeconds);J->SetNumberField(TEXT("entry_duration"),S.EntryDurationSeconds);
 J->SetNumberField(TEXT("cap_loss_elapsed"),S.CapLossElapsedSeconds);J->SetNumberField(TEXT("cap_recovery_elapsed"),S.CapRecoveryElapsedSeconds);J->SetNumberField(TEXT("normal_regen_elapsed"),S.NormalRegenElapsedSeconds);J->SetNumberField(TEXT("dash_cooldown"),Cooldown);
 auto W=TJsonWriterFactory<>::Create(&Out);if(!FJsonSerializer::Serialize(J,W))return false;Error.Reset();return true;
}
bool DWCharacterA01SaveBridge::StateFromJson(const FString& Json,const FDWCharacterA01Config& C,double MaxCooldown,FDWCharacterA01Runtime& Out,double& Cooldown,FString& Error)
{
 TSharedPtr<FJsonObject> J;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),J)||!J.IsValid()||J->Values.Num()!=15)return DWA01SavePrivate::Fail(Error,TEXT("Invalid A01 state JSON fields."));
 FDWCharacterA01State S;double Schema=0.,Form=0.;
 const auto N=[&](const TCHAR* Key,double& V){return J->TryGetNumberField(Key,V)&&FMath::IsFinite(V);};
 if(!N(TEXT("schema_version"),Schema)||Schema!=1.||!N(TEXT("health"),S.CurrentHealth)||!N(TEXT("water"),S.Water)||!N(TEXT("satiety"),S.Satiety)||
  !N(TEXT("recorded_cap_loss"),S.RecordedCapLoss)||!N(TEXT("gas"),S.Gas)||!N(TEXT("corruption"),S.Corruption)||!J->TryGetBoolField(TEXT("corruption_initialized"),S.bCorruptionInitialized)||
  !N(TEXT("form"),Form)||Form!=std::floor(Form)||Form<0.||Form>2.||!N(TEXT("form_remaining"),S.FormRemainingSeconds)||!N(TEXT("entry_duration"),S.EntryDurationSeconds)||
  !N(TEXT("cap_loss_elapsed"),S.CapLossElapsedSeconds)||!N(TEXT("cap_recovery_elapsed"),S.CapRecoveryElapsedSeconds)||!N(TEXT("normal_regen_elapsed"),S.NormalRegenElapsedSeconds)||
  !N(TEXT("dash_cooldown"),Cooldown)||Cooldown<0.||Cooldown>MaxCooldown)return DWA01SavePrivate::Fail(Error,TEXT("Invalid A01 state values."));
 S.Form=static_cast<EDWA01Form>(static_cast<uint8>(Form));std::string E;if(!Out.Restore(C,S,&E)){Error=UTF8_TO_TCHAR(E.c_str());return false;}Error.Reset();return true;
}
bool DWCharacterA01SaveBridge::FormalStateToJson(const FDWCharacterA01State& S,double Cooldown,FString& Out,FString& Error)
{
 FString Legacy;if(S.StarvationOrdinal==MAX_uint64||!StateToJson(S,Cooldown,Legacy,Error))return false;
 TSharedPtr<FJsonObject> J;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Legacy),J)||!J)return false;
 J->SetStringField(TEXT("starvation_ordinal"),LexToString(S.StarvationOrdinal));
 FString Candidate;if(!FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<>::Create(&Candidate)))return false;
 Out=MoveTemp(Candidate);Error.Reset();return true;
}
bool DWCharacterA01SaveBridge::FormalStateFromJson(const FString& Json,const FDWCharacterA01Config& C,double MaxCooldown,FDWCharacterA01Runtime& Out,double& Cooldown,FString& Error)
{
 TSharedPtr<FJsonObject> J;FString Text;uint64 Ordinal=0;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),J)||!J||J->Values.Num()!=16||
  !J->TryGetStringField(TEXT("starvation_ordinal"),Text)||Text.IsEmpty()||Text.Len()>20||!LexTryParseString(Ordinal,*Text)||Ordinal==MAX_uint64||LexToString(Ordinal)!=Text)
  return DWA01SavePrivate::Fail(Error,TEXT("Invalid exact formal starvation ordinal."));
 J->RemoveField(TEXT("starvation_ordinal"));FString Legacy;FDWCharacterA01Runtime Candidate;double PreparedCooldown=0.;
 if(!FJsonSerializer::Serialize(J.ToSharedRef(),TJsonWriterFactory<>::Create(&Legacy))||!StateFromJson(Legacy,C,MaxCooldown,Candidate,PreparedCooldown,Error))return false;
 auto State=Candidate.Capture();State.StarvationOrdinal=Ordinal;std::string E;if(!Candidate.Restore(C,State,&E)){Error=UTF8_TO_TCHAR(E.c_str());return false;}
 Out=MoveTemp(Candidate);Cooldown=PreparedCooldown;return true;
}
bool DWCharacterA01SaveBridge::ValidateProjectionWithDefinitions(const FDWCombatRules& Rules,const UDWSaveGame& Save,const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,FDWA01SavedProjection& Out,FString& Error)
{
 if(HasFormalFields(Save)||Save.AdventureFlow.Schema!=0||Save.LanguageTrialSchema!=1||!IsExactTrialMap(Save.MapPackage)||Save.LanguageProgressJson.IsEmpty()||Save.A01StateJson.IsEmpty())return DWA01SavePrivate::Fail(Error,TEXT("Incomplete or wrong-map LANG-01 save envelope."));
 if(!DWLanguage::ValidateCatalog(Catalog,Error))return false;
 TSharedPtr<FJsonObject> J;const TSharedPtr<FJsonObject>* Model=nullptr;const TSharedPtr<FJsonObject>* Starter=nullptr;double Schema=0.;FString Playthrough,StarterId;FGuid Guid;bool Granted=false;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Save.LanguageProgressJson),J)||!J.IsValid()||!J->TryGetNumberField(TEXT("schema_version"),Schema)||Schema!=1.||
  J->Values.Num()!=4||!J->TryGetStringField(TEXT("playthrough_id"),Playthrough)||!FGuid::ParseExact(Playthrough,EGuidFormats::Digits,Guid)||!Guid.IsValid()||!J->TryGetObjectField(TEXT("model"),Model)||
  !J->TryGetObjectField(TEXT("starter_grant"),Starter)||(*Starter)->Values.Num()!=2||!(*Starter)->TryGetStringField(TEXT("playthrough_id"),StarterId)||StarterId!=Playthrough||
  !(*Starter)->TryGetBoolField(TEXT("granted"),Granted)||!Granted)return DWA01SavePrivate::Fail(Error,TEXT("Invalid language save wrapper or starter receipt."));
 FString ModelJson;auto W=TJsonWriterFactory<>::Create(&ModelJson);if(!FJsonSerializer::Serialize((*Model).ToSharedRef(),W))return false;
 FDWA01SavedProjection Candidate;if(!DWLanguage::StateFromJson(Catalog,ModelJson,Candidate.LanguageState,Error))return false;
 TSet<FName> Learned;for(const auto& Pair:Candidate.LanguageState.LearnedTalentPaidCosts)Learned.Add(Pair.Key);
 FDWCharacterA01Config C;double MaxCooldown=0.;
 if(!BuildDerivedConfig(Trial,Learned,C,Error)||!DWA01SavePrivate::Number(Trial,TEXT("yeast.dash.cooldown_s"),MaxCooldown)||
  !StateFromJson(Save.A01StateJson,C,MaxCooldown,Candidate.Runtime,Candidate.CooldownRemaining,Error)||
  !BuildMirror(Rules,Candidate.Runtime,Candidate.LegacyConfig,Candidate.LegacySnapshot,Error))return false;
 double YeastDuration=0.,MoldDuration=0.,DurationAdd=0.;
 if(!DWA01SavePrivate::Number(Trial,TEXT("yeast.duration_s"),YeastDuration)||!DWA01SavePrivate::Number(Trial,TEXT("mold.duration_s"),MoldDuration)||
  !DWA01SavePrivate::Number(Trial,TEXT("talent.b3.duration_add_s"),DurationAdd)||Candidate.Runtime.GetState().EntryDurationSeconds>FMath::Max(YeastDuration,MoldDuration)+DurationAdd)
  return DWA01SavePrivate::Fail(Error,TEXT("Saved A01 entry duration exceeds the possible configured maximum."));
 const auto& A=Candidate.LegacySnapshot;const auto& B=Save.CombatPlayerResources.Vitals;
 const auto Equal=[](double X,double Y){return FMath::IsFinite(X)&&FMath::IsFinite(Y)&&FMath::Abs(X-Y)<=1.e-8;};
 if(B.ResourceSchema!=A.ResourceSchema||B.RulesCombatSchema!=A.RulesCombatSchema||B.BaseForm!=A.BaseForm||!Equal(B.Health,A.Health)||!Equal(B.Water,A.Water)||!Equal(B.Satiety,A.Satiety)||
  !Equal(B.Special,A.Special)||!Equal(B.FormRemainingSeconds,A.FormRemainingSeconds)||B.FormTimeCarry!=0.||B.SpecialGrowthCarry!=0.||
  B.WaterDrainCarry!=0.||B.SatietyDrainCarry!=0.||B.WaterEmptyElapsedSeconds!=0.||B.SatietyEmptyElapsedSeconds!=0.||B.WaterEmptyTimeCarry!=0.||B.SatietyEmptyTimeCarry!=0.||
  Save.Health!=static_cast<float>(A.Health)||Save.Transformation!=static_cast<float>(A.Special)||Save.bYeastForm!=(A.BaseForm==EDWCombatBaseForm::Yeast))
  return DWA01SavePrivate::Fail(Error,TEXT("LANG-01 authoritative state and legacy compatibility mirrors disagree."));
 Candidate.MaximumCooldown=MaxCooldown;Out=MoveTemp(Candidate);Error.Reset();return true;
}

namespace DWA01SavePrivate
{
thread_local const DWCharacterA01SaveBridge::FScopedFormalDefinitions* ActiveFormalDefinitions=nullptr;
}
DWCharacterA01SaveBridge::FScopedFormalDefinitions::FScopedFormalDefinitions(const DWLanguage::FCatalog* Source,const TSharedPtr<FJsonObject>& Config)
 :Previous(DWA01SavePrivate::ActiveFormalDefinitions),Trial(Config),bAvailable(Source&&Config.IsValid())
{if(Source)Catalog=*Source;DWA01SavePrivate::ActiveFormalDefinitions=this;}
DWCharacterA01SaveBridge::FScopedFormalDefinitions::~FScopedFormalDefinitions()
{check(DWA01SavePrivate::ActiveFormalDefinitions==this);DWA01SavePrivate::ActiveFormalDefinitions=Previous;}
bool DWCharacterA01SaveBridge::ValidateProjection(const FDWCombatRules& Rules,const UDWSaveGame& Save,FDWA01SavedProjection& Out,FString& Error)
{
 if(HasFormalFields(Save))
 {
  const auto* Context=DWA01SavePrivate::ActiveFormalDefinitions;
  if(IsExactTrialMap(Save.MapPackage)||!Context||!Context->HasDefinitions())
   return DWA01SavePrivate::Fail(Error,TEXT("Formal projection requires its own scoped immutable definitions and a non-trial map."));
  return DWFormalLanguageProjection::PrepareStored(Save,Save.FormalLanguage.SlotIndex,Context->GetCatalog(),Context->GetTrial(),Rules,Out,Error);
 }
 TSharedPtr<const DWA01SavePrivate::FDefinitions,ESPMode::ThreadSafe> D;
 {FScopeLock Lock(&DWA01SavePrivate::Mutex);D=DWA01SavePrivate::Definitions;}
 if(!D.IsValid())return DWA01SavePrivate::Fail(Error,TEXT("LANG-01 definitions must be bound before static save validation."));
 return ValidateProjectionWithDefinitions(Rules,Save,D->Catalog,D->Trial,Out,Error);
}
