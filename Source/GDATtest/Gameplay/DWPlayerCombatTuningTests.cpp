#if WITH_DEV_AUTOMATION_TESTS
#include "DWPlayerCombatTuningPolicy.h"
#include "DWCombatRules.h"
#include "DWPlayerCombatTuningSubsystem.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerCombatTuningTest,"DoughWorld.Combat.PlayerTuning.DefaultsAndValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerCombatTuningTest::RunTest(const FString&)
{
 TSharedPtr<const FDWCombatRules> Base,Effective;FString Error;
 if(!TestTrue(TEXT("Read only original numeric payload, no world or save I/O"),FDWCombatRules::LoadProjectPayload(Base,Error)))return false;
 TestTrue(TEXT("Empty overrides accepted"),FDWCombatRules::WithPlayerOverrides(Base,{},Effective,Error));
 TestTrue(TEXT("Empty defaults preserve exact immutable source object"),Effective==Base);
 TArray<FDWPlayerCombatNumericOverride> DisabledRules;
 for(const auto& Pair:Base->GetValidatedValues())if(DWPlayerCombatTuningPolicy::RuleAllowed(Pair.Key))
 {FDWPlayerCombatNumericOverride Row;Row.ParameterId=Pair.Key;Row.Value=Pair.Value.Value;DisabledRules.Add(Row);}
 TMap<FName,double> Compiled;
 TestTrue(TEXT("All seeded 147 rows remain disabled and compile to an empty map"),DWPlayerCombatTuningPolicy::ReadOverrides(DisabledRules,Compiled,true,Error)&&Compiled.IsEmpty());
 TestTrue(TEXT("Seeded disabled preset preserves the identical Rules object"),FDWCombatRules::WithPlayerOverrides(Base,Compiled,Effective,Error)&&Effective==Base);
 TMap<FName,double> Defaults;for(const auto& Pair:Base->GetValidatedValues())if(DWPlayerCombatTuningPolicy::RuleAllowed(Pair.Key))Defaults.Add(Pair.Key,Pair.Value.Value);
 if(!TestTrue(TEXT("All allowed current defaults validate"),FDWCombatRules::WithPlayerOverrides(Base,Defaults,Effective,Error)))return false;
 for(const auto& Pair:Base->GetValidatedValues())
 {
  const auto* V=Effective->GetValidatedValues().Find(Pair.Key);TestTrue(TEXT("Every base and derived value/unit/count unchanged"),V&&V->Value==Pair.Value.Value&&V->Unit==Pair.Value.Unit&&V->bCount==Pair.Value.bCount&&V->bDerived==Pair.Value.bDerived);
  double Before=0.,After=0.;FString E;
  TestTrue(TEXT("Every original-unit numeric getter is equal"),Base->TryGetNumber(Pair.Key,Pair.Value.Unit,Before,E)&&Effective->TryGetNumber(Pair.Key,Pair.Value.Unit,After,E)&&Before==After);
  if(Pair.Value.Unit==EDWCombatRuleUnit::Meters)TestTrue(TEXT("Every meter to UE centimeter conversion is equal"),Base->TryGetNumber(Pair.Key,EDWCombatRuleUnit::Centimeters,Before,E)&&Effective->TryGetNumber(Pair.Key,EDWCombatRuleUnit::Centimeters,After,E)&&Before==After);
  if(Pair.Value.bCount){int32 A=0,B=0;TestTrue(TEXT("Every integer/count getter is equal"),Base->TryGetCount(Pair.Key,A,E)&&Effective->TryGetCount(Pair.Key,B,E)&&A==B);}
 }
 TestTrue(TEXT("Design/source/schema/base/derived metadata remain"),Effective->GetSourceHash()==Base->GetSourceHash()&&Effective->GetDesignVersion()==Base->GetDesignVersion()&&Effective->GetCombatSchema()==Base->GetCombatSchema()&&Effective->GetBaseParameterCount()==Base->GetBaseParameterCount()&&Effective->GetDerivedParameterCount()==Base->GetDerivedParameterCount());
 TMap<FName,double> Invalid;Invalid.Add(TEXT("enemy.mold.damage"),900.);TestFalse(TEXT("Enemy values cannot leak into player profile"),FDWCombatRules::WithPlayerOverrides(Base,Invalid,Effective,Error));
 Invalid.Reset();Invalid.Add(TEXT("weapon.hits_per_target_per_stage"),2.);TestFalse(TEXT("Structural hit count remains locked"),FDWCombatRules::WithPlayerOverrides(Base,Invalid,Effective,Error));
 Invalid.Reset();Invalid.Add(TEXT("weapon.shotgun.magazine"),3.5);TestFalse(TEXT("Fractional count rejected by original domain"),FDWCombatRules::WithPlayerOverrides(Base,Invalid,Effective,Error));
 Invalid.Reset();Invalid.Add(TEXT("weapon.spear.active_s"),999.);TestFalse(TEXT("Original phase/interval relationship still enforced"),FDWCombatRules::WithPlayerOverrides(Base,Invalid,Effective,Error));
 using namespace DWPlayerCombatTuningPolicy;
 FSimpleValues Original{31.,.77,234.},Result;
 TestTrue(TEXT("Disabled simple profile preserves actual source, not placeholder fields"),ResolveSimple(Original,false,false,false,{25.,1.,100.},Result)&&Result.Damage==31.&&Result.Interval==.77&&Result.Range==234.);
 TestTrue(TEXT("Independent damage override retains source cycle/range"),ResolveSimple(Original,true,false,false,{40.,1.,100.},Result)&&Result.Damage==40.&&Result.Interval==.77&&Result.Range==234.);
 TestFalse(TEXT("Invalid effective tool interval rejected"),ResolveSimple(Original,false,true,false,{40.,0.,100.},Result));
 TSharedPtr<FJsonObject> A01=MakeShared<FJsonObject>();auto Params=MakeShared<FJsonObject>();auto Damage=MakeShared<FJsonObject>();Damage->SetNumberField(TEXT("value"),50.);Damage->SetStringField(TEXT("unit"),TEXT("points"));Params->SetObjectField(TEXT("yeast.dash.damage"),Damage);A01->SetObjectField(TEXT("parameters"),Params);
 const auto OriginalObject=A01;TestTrue(TEXT("Empty A01 defaults preserve object"),ApplyA01Overrides(A01,{},Error)&&A01==OriginalObject);
 TMap<FName,double> Changes;Changes.Add(TEXT("yeast.dash.damage"),60.);
 TestTrue(TEXT("Current A01 ability parameter can be projected"),ApplyA01Overrides(A01,Changes,Error));
 TestTrue(TEXT("Projection does not mutate original caller object"),Damage->GetNumberField(TEXT("value"))==50.&&A01!=OriginalObject);
 auto Saved=A01;Changes.Add(TEXT("health.base_max"),1000.);TestFalse(TEXT("Non-attack A01 values rejected atomically"),ApplyA01Overrides(A01,Changes,Error));TestTrue(TEXT("Failed A01 projection leaves object untouched"),A01==Saved);
 Changes.Reset();Changes.Add(TEXT("yeast.dash.damage"),static_cast<double>(MAX_flt)*2.);TestFalse(TEXT("New A01 damage cannot overflow native command float"),ApplyA01Overrides(A01,Changes,Error));
 FString TrialJson;TSharedPtr<FJsonObject> FullTrial;
 if(!TestTrue(TEXT("Read current A01 defaults including root Items additions, no save I/O"),FFileHelper::LoadFileToString(TrialJson,*(FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWA01TrialDefaults.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(TrialJson),FullTrial)))return false;
 const auto FullOriginal=FullTrial;FString CanonicalBefore;FJsonSerializer::Serialize(FullTrial.ToSharedRef(),TJsonWriterFactory<>::Create(&CanonicalBefore));
 TestTrue(TEXT("Empty A01 defaults preserve full current TrialConfig object"),ApplyA01Overrides(FullTrial,{},Error)&&FullTrial==FullOriginal);
 const TSharedPtr<FJsonObject>* AllParameters=nullptr;if(!FullTrial->TryGetObjectField(TEXT("parameters"),AllParameters))return false;
 TArray<FDWPlayerCombatNumericOverride> DisabledA01;TMap<FName,double> FullDefaultValues;
 for(const auto& Pair:(*AllParameters)->Values)if(A01Allowed(FName(*Pair.Key)))
 {const auto Obj=Pair.Value->AsObject();double V=0.;if(!Obj||!Obj->TryGetNumberField(TEXT("value"),V))return false;FDWPlayerCombatNumericOverride Row;Row.ParameterId=FName(*Pair.Key);Row.Value=V;DisabledA01.Add(Row);FullDefaultValues.Add(Row.ParameterId,V);}
 TestTrue(TEXT("All seeded 17 A01 rows disabled compile empty"),ReadOverrides(DisabledA01,Compiled,false,Error)&&Compiled.IsEmpty());
 TestTrue(TEXT("Disabled A01 preset preserves full source object"),ApplyA01Overrides(FullTrial,Compiled,Error)&&FullTrial==FullOriginal);
 TestTrue(TEXT("Explicitly projecting every existing A01 value succeeds"),ApplyA01Overrides(FullTrial,FullDefaultValues,Error));
 FString CanonicalAfter;FJsonSerializer::Serialize(FullTrial.ToSharedRef(),TJsonWriterFactory<>::Create(&CanonicalAfter));
 TestEqual(TEXT("Whole A01 JSON remains equal including runtime_consumables, metadata and noncombat values"),CanonicalAfter,CanonicalBefore);
 return true;
}
#endif
