#include "DWPlayerCombatTuningSubsystem.h"
#include "DWPlayerCombatTuningPolicy.h"
#include "DWCombatRules.h"
#include "UObject/Class.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
bool UDWPlayerCombatTuningSettings::ValidateNumericSource(FString& Error)const
{TSharedPtr<const FDWCombatRules> Base;TSharedPtr<const FDWCombatNumericProvider> Candidate;return FDWCombatRules::LoadProjectPayload(Base,Error)&&FDWCombatNumericProvider::Build(*this,Base,Candidate,Error);}
bool UDWPlayerCombatTuningSettings::CoordinatePlayerTiming(FName Edited,double PreviousValue,FString& Notice)
{return DWCombatNumericPolicy::CoordinateTiming(PlayerNumbers,Edited,PreviousValue,Notice);}
void UDWPlayerCombatTuningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);TSharedPtr<const FDWCombatRules> Base;
 const auto* Settings=LoadObject<UDWPlayerCombatTuningSettings>(nullptr,TEXT("/Game/DoughWorld/Core/Data/DA_DWPlayerCombatTuning.DA_DWPlayerCombatTuning"));
 bValid=Settings&&FDWCombatRules::LoadProjectPayload(Base,ConfigurationError)&&FDWCombatNumericProvider::Build(*Settings,Base,Provider,ConfigurationError);
 if(!bValid){if(ConfigurationError.IsEmpty())ConfigurationError=TEXT("统一战斗数值不可用，请检查迁移或完整表 / Numeric provider unavailable");UE_LOG(LogTemp,Error,TEXT("DW_COMBAT_NUMERIC_PROVIDER_INVALID: %s"),*ConfigurationError);}
}
const FDWEnemyNumbers* UDWPlayerCombatTuningSubsystem::FindEnemyType(const UClass* ConcreteClass,EDWEnemyCombatProfile Profile)const
{
 if(!bValid||!Provider)return nullptr;TArray<FString> Ancestry;
 for(const UClass* C=ConcreteClass;C;C=C->GetSuperClass())Ancestry.Add(C->GetPathName());return Provider->FindEnemy(Ancestry,Profile);
}
bool UDWPlayerCombatTuningSubsystem::ResolveCombatRules(TSharedPtr<const FDWCombatRules>& Out,FString& Error)const
{if(!bValid||!Provider){Error=ConfigurationError;return false;}Out=Provider->GetRules();Error.Reset();return true;}
bool UDWPlayerCombatTuningSubsystem::ResolveAbilityParameters(TSharedPtr<FJsonObject>& InOut,FString& Error)const
{if(!bValid||!Provider){Error=ConfigurationError;return false;}return Provider->ProjectAbilities(InOut,Error);}
bool UDWPlayerCombatTuningSubsystem::ResolveSimpleAttack(FDWSimpleSpearNumbers& Out,FString& Error)const
{if(!bValid||!Provider){Error=ConfigurationError;return false;}Out=Provider->GetSimpleSpear();Error.Reset();return true;}
// Retained narrow C++ compatibility facades. No legacy per-row override storage is read here.
bool UDWPlayerCombatTuningSubsystem::ApplyRules(TSharedPtr<const FDWCombatRules>& InOut,FString& Error)const{return ResolveCombatRules(InOut,Error);}
bool UDWPlayerCombatTuningSubsystem::ApplyA01(TSharedPtr<FJsonObject>& InOut,FString& Error)const{return ResolveAbilityParameters(InOut,Error);}
bool UDWPlayerCombatTuningSubsystem::ResolveSimpleSpear(double,double,double,double& Damage,double& Interval,double& Range,FString& Error)const
{FDWSimpleSpearNumbers N;if(!ResolveSimpleAttack(N,Error))return false;Damage=N.Damage;Interval=N.IntervalSeconds;Range=N.RangeCm;return true;}
bool DWPlayerCombatTuningPolicy::ReadOverrides(const TArray<FDWPlayerCombatNumericOverride>& Rows,TMap<FName,double>& Out,bool Formal,FString& Error)
{
 TMap<FName,double> Candidate;
  for(const auto& Row:Rows)if(Row.bEnabled)
  {
   if(!(Formal?DWPlayerCombatTuningPolicy::RuleAllowed(Row.ParameterId):DWPlayerCombatTuningPolicy::A01Allowed(Row.ParameterId))||
      !FMath::IsFinite(Row.Value)||Row.Value<0.||(!Formal&&!DWPlayerCombatTuningPolicy::A01ValueAllowed(Row.ParameterId,Row.Value))||Candidate.Contains(Row.ParameterId))
   {Error=TEXT("Invalid, duplicate or disallowed player attack override: ")+Row.ParameterId.ToString();return false;}
   Candidate.Add(Row.ParameterId,Row.Value);
  }
 Out=MoveTemp(Candidate);Error.Reset();return true;
}
bool DWPlayerCombatTuningPolicy::ApplyA01Overrides(TSharedPtr<FJsonObject>& InOut,const TMap<FName,double>& Overrides,FString& Error)
{
 if(Overrides.IsEmpty()){Error.Reset();return InOut.IsValid();}
 if(!InOut){Error=TEXT("Missing A01 source configuration");return false;}
 FString Json;const auto Writer=TJsonWriterFactory<>::Create(&Json);if(!FJsonSerializer::Serialize(InOut.ToSharedRef(),Writer))return false;
 TSharedPtr<FJsonObject> Candidate;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Candidate))return false;
 const TSharedPtr<FJsonObject>* Parameters=nullptr;
 if(!Candidate->TryGetObjectField(TEXT("parameters"),Parameters)){Error=TEXT("Missing A01 parameters");return false;}
 for(const auto& Pair:Overrides)
 {
  const TSharedPtr<FJsonObject>* Entry=nullptr;double Old=0.;
  if(!DWPlayerCombatTuningPolicy::A01ValueAllowed(Pair.Key,Pair.Value)||!(*Parameters)->TryGetObjectField(Pair.Key.ToString(),Entry)||
     !(*Entry)->TryGetNumberField(TEXT("value"),Old)||!FMath::IsFinite(Old))
  {Error=TEXT("Unknown or unresolved A01 attack parameter: ")+Pair.Key.ToString();return false;}
  (*Entry)->SetNumberField(TEXT("value"),Pair.Value);
 }
 // Existing catalog/ability/runtime and save-bridge validators run against this same frozen object.
 InOut=MoveTemp(Candidate);Error.Reset();return true;
}
