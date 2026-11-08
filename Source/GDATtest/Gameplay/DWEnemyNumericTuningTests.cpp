#if WITH_DEV_AUTOMATION_TESTS
#include "DWEnemyNumericTuning.h"
#include "DWCombatRules.h"
#include "DWEnemyCharacter.h"
#include "DWCreatureDefinition.h"
#include "Misc/AutomationTest.h"
namespace DWEnemyNumericTest
{
static TArray<double> Scalars(const FDWEnemyCombatConfig& C)
{return {C.Health,C.Damage,C.WindupSeconds,C.ActiveSeconds,C.RecoverySeconds,C.ExtraCooldownSeconds,C.RangeCm,C.MoveSpeedCmPerSecond,C.AttackFullAngleDegrees,C.AttackHeightCm,C.KnockbackMultiplier,C.AttackKnockbackMultiplier,C.OutgoingKnockbackCm,C.OutgoingKnockbackSeconds,C.AlertRadiusCm,C.DisengageDistanceCm,C.AlertDelaySeconds,C.DisengageSeconds,C.WindupLockFraction,C.ReturnSpeedMultiplier,static_cast<double>(C.HitsPerTarget),C.AttackCycleSeconds()};}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemyNumericTuningTest,"DoughWorld.Combat.EditorNumeric.EnemyIsolationAndDefaults",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEnemyNumericTuningTest::RunTest(const FString&)
{
 FString Error;TSharedPtr<const FDWCombatRules> Rules;if(!FDWCombatRules::LoadProjectPayload(Rules,Error)){AddError(Error);return false;}
 for(auto Profile:{EDWEnemyCombatProfile::MoldGrunt,EDWEnemyCombatProfile::YeastGrunt,EDWEnemyCombatProfile::DoughZombie,EDWEnemyCombatProfile::DoughElite})
 {
  FDWEnemyCombatConfig Base,Projected;FDWEnemyActorNumericSupplement Extra;
  if(!DWEnemyFormal::BuildConfig(*Rules,Profile,Base,Error)){AddError(Error);return false;}
  TestTrue(TEXT("Absent type leaves every formal scalar unchanged"),DWEnemyNumericPolicy::ApplyFormal(nullptr,Base,Projected,Extra,Error)&&DWEnemyNumericTest::Scalars(Base)==DWEnemyNumericTest::Scalars(Projected));
  FDWEnemyTypeNumericTuning Type;Type.TypeKey=TEXT("Disabled");FDWEnemyNumericOverride Row;Row.Field=EDWEnemyNumericField::Damage;Row.Value=9999.;Type.Values.Add(Row);
  TestTrue(TEXT("Disabled field does not change default profile or any clock"),DWEnemyNumericPolicy::ApplyFormal(&Type,Base,Projected,Extra,Error)&&DWEnemyNumericTest::Scalars(Base)==DWEnemyNumericTest::Scalars(Projected)&&Extra.Values.IsEmpty());
  Type.Values[0].bEnabled=true;Type.Values[0].Value=Base.Damage+1.;TestTrue(TEXT("Explicit damage edits numeric projection only"),DWEnemyNumericPolicy::ApplyFormal(&Type,Base,Projected,Extra,Error)&&Projected.Damage==Base.Damage+1.&&Projected.RangeCm==Base.RangeCm&&Projected.WindupSeconds==Base.WindupSeconds&&Projected.HitsPerTarget==1);
  const auto Before=DWEnemyNumericTest::Scalars(Projected);Extra.Values.Add(EDWEnemyNumericField::PatrolRadius,77.);
  Type.Values[0].Value=-1.;TestFalse(TEXT("Illegal input fails"),DWEnemyNumericPolicy::ApplyFormal(&Type,Base,Projected,Extra,Error));
  TestTrue(TEXT("Failure leaves output and supplement atomically unchanged"),Before==DWEnemyNumericTest::Scalars(Projected)&&Extra.Values.FindRef(EDWEnemyNumericField::PatrolRadius)==77.);
  Type.Values[0].Field=EDWEnemyNumericField::VariantWeight;Type.Values[0].Value=3.;
  TestFalse(TEXT("Formal unsupported variant knob rejected"),DWEnemyNumericPolicy::ApplyFormal(&Type,Base,Projected,Extra,Error));
 }
 TArray<FDWEnemyTypeNumericTuning> Types;
 FDWEnemyTypeNumericTuning Mold;Mold.TypeKey=TEXT("Mold");Mold.TargetClass=FSoftClassPath(TEXT("/Game/QA/Mold.Mold_C"));Mold.bAnyProfile=true;Types.Add(Mold);
 FDWEnemyTypeNumericTuning Sour;Sour.TypeKey=TEXT("Sour");Sour.TargetClass=FSoftClassPath(TEXT("/Game/QA/Sour.Sour_C"));Sour.bAnyProfile=true;Types.Add(Sour);
 const auto* Selected=DWEnemyNumericPolicy::Select(Types,{TEXT("/Game/QA/Sour.Sour_C"),TEXT("/Script/GDATtest.DWEnemyCharacter")},EDWEnemyCombatProfile::MoldGrunt);
 TestTrue(TEXT("Sour class selects its own group even if same Profile as Mold"),Selected&&Selected->TypeKey==TEXT("Sour"));
 FDWEnemyTypeNumericTuning Giant;Giant.TypeKey=TEXT("Giant");Giant.TargetClass=FSoftClassPath(TEXT("/Game/QA/Giant.Giant_C"));Giant.bAnyProfile=true;Types.Add(Giant);
 Selected=DWEnemyNumericPolicy::Select(Types,{TEXT("/Game/QA/Giant.Giant_C")},EDWEnemyCombatProfile::Legacy);
 TestTrue(TEXT("Actual Legacy giant is not silently treated as elite"),Selected&&Selected->TypeKey==TEXT("Giant"));
 auto Duplicated=Types;Duplicated.Add(Types[0]);TestFalse(TEXT("Duplicate bindings fail configuration"),DWEnemyNumericPolicy::ValidateTypes(Duplicated,Error));
 TestTrue(TEXT("Authorized effective Legacy projection accepted"),DWEnemyNumericPolicy::ValidateLegacyProjection(300,190,300,300,190));
 TestFalse(TEXT("Forged saved cap not admitted by numeric editor"),DWEnemyNumericPolicy::ValidateLegacyProjection(300,190,333,300,190));
 TestFalse(TEXT("Mixed live source remains rejected"),DWEnemyNumericPolicy::ValidateLegacyProjection(300,190,300,100,190));
 // Keep the oversized input at runtime so MSVC does not fold the guarded float cast in a unity build.
 const volatile double OversizedValue=static_cast<double>(MAX_flt)*2.;
 TestFalse(TEXT("Native overflow rejected"),DWEnemyNumericPolicy::ValidateValue(EDWEnemyNumericField::Damage,OversizedValue));
 TestFalse(TEXT("Legacy active duration below original .01 rejected"),DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::Active,.001));
 TestFalse(TEXT("Legacy interval below original .05 rejected"),DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::Interval,.01));
 TestFalse(TEXT("Legacy health below original one rejected"),DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::MaxHealth,.5));
 TestFalse(TEXT("Legacy range above original 100000 rejected"),DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::Range,100001.));
 TestFalse(TEXT("Legacy windup above original 60 rejected"),DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::Windup,60.01));
 TestFalse(TEXT("Legacy angle below original two rejected"),DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::FullAngle,1.));
 TestFalse(TEXT("Legacy cleanup below original .1 rejected"),DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::DeathCleanupSeconds,.01));
 TestTrue(TEXT("Legacy exact active and interval minima accepted"),DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::Active,.01)&&DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField::Interval,.05));
 TestTrue(TEXT("Formal domain not silently replaced by Legacy clamps"),DWEnemyNumericPolicy::ValidateValue(EDWEnemyNumericField::Active,.001));
 for(auto F:{EDWEnemyNumericField::MaxHealth,EDWEnemyNumericField::Damage,EDWEnemyNumericField::Range,EDWEnemyNumericField::MoveSpeed,EDWEnemyNumericField::Windup,EDWEnemyNumericField::Active,EDWEnemyNumericField::Recovery,EDWEnemyNumericField::Interval,EDWEnemyNumericField::FullAngle,EDWEnemyNumericField::AlertRadius,EDWEnemyNumericField::DisengageDistance,EDWEnemyNumericField::AlertSeconds,EDWEnemyNumericField::PatrolRadius,EDWEnemyNumericField::PatrolWaitSeconds,EDWEnemyNumericField::DeathCleanupSeconds,EDWEnemyNumericField::VariantWeight})
 {
  double Low=-1.,High=-1.;TestTrue(TEXT("Supported Legacy field has shared UI/runtime bounds"),DWEnemyNumericPolicy::LegacyBounds(F,Low,High));
  TestTrue(TEXT("Shared bound endpoints accepted"),DWEnemyNumericPolicy::ValidateLegacyValue(F,Low)&&DWEnemyNumericPolicy::ValidateLegacyValue(F,High));
  TestFalse(TEXT("Below shared lower endpoint rejected"),DWEnemyNumericPolicy::ValidateLegacyValue(F,Low-1.));
 }
 auto* Definition=NewObject<UDWCreatureDefinition>();auto* Forged=NewObject<UDWCreatureDefinition>();
 TestFalse(TEXT("Original source identity guard still rejects substituted definition"),DWCreaturePersistencePolicy::ValidateSource(Forged,Definition,EDWEnemyCombatProfile::Legacy,100,100,180,Error));
 TestTrue(TEXT("Original Legacy source cap and speed accepted"),DWCreaturePersistencePolicy::ValidateSource(Definition,Definition,EDWEnemyCombatProfile::Legacy,Definition->MaxHealth,Definition->MaxHealth,Definition->MoveSpeed,Error));
 TestFalse(TEXT("Original source cap cannot be bypassed without explicit projection"),DWCreaturePersistencePolicy::ValidateSource(Definition,Definition,EDWEnemyCombatProfile::Legacy,999,Definition->MaxHealth,Definition->MoveSpeed,Error));
 return true;
}
#endif
