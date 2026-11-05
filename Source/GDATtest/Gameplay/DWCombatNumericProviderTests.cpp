#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatNumericProvider.h"
#include "DWPlayerCombatTuningSubsystem.h"
#include "DWCombatRules.h"
#include "DWCombatSpear.h"
#include "DWSimpleSpearPolicy.h"
#include "DWEnemyCharacter.h"
#include "DWCreatureDefinition.h"
#include "Misc/AutomationTest.h"
namespace DWUnifiedTest
{
static FDWCombatNumber Number(const TCHAR* Id,double Value){FDWCombatNumber R;R.ParameterId=Id;R.Value=Value;return R;}
static double Value(const TArray<FDWCombatNumber>& Rows,const TCHAR* Id){const auto* R=Rows.FindByPredicate([&](const auto& V){return V.ParameterId==FName(Id);});return R?R->Value:-1.;}
static bool Same(const TArray<FDWCombatNumber>& A,const TArray<FDWCombatNumber>& B){if(A.Num()!=B.Num())return false;for(int32 I=0;I<A.Num();++I)if(A[I].ParameterId!=B[I].ParameterId||A[I].Value!=B[I].Value)return false;return true;}
static TArray<FDWCombatNumber> Timing(){return {Number(TEXT("weapon.spear.windup_s"),.2),Number(TEXT("weapon.spear.active_s"),.18),Number(TEXT("weapon.spear.recovery_s"),.62),Number(TEXT("weapon.spear.interval_s"),1.)};}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWUnifiedTimingTest,"DoughWorld.Combat.UnifiedNumeric.TimingTwoDirections",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWUnifiedTimingTest::RunTest(const FString&)
{
 using namespace DWUnifiedTest;FString Notice;auto Values=Timing();const auto Defaults=Values;
 TestTrue(TEXT("Unrelated damage edit never normalizes timing"),DWCombatNumericPolicy::CoordinateTiming(Values,TEXT("weapon.spear.damage"),35,Notice)&&Same(Values,Defaults));
 Values[3].Value=.6;
 TestTrue(TEXT("User period-only intent coordinates"),DWCombatNumericPolicy::CoordinateTiming(Values,TEXT("weapon.spear.interval_s"),1.,Notice));
 TestTrue(TEXT(".6 cycle retained, complete phases proportionally .12/.108/.372"),Value(Values,TEXT("weapon.spear.interval_s"))==.6&&FMath::IsNearlyEqual(Values[0].Value,.12,1.e-12)&&FMath::IsNearlyEqual(Values[1].Value,.108,1.e-12)&&FMath::IsNearlyEqual(Values[2].Value,.372,1.e-12));
 Values[0].Value=.5;
 TestTrue(TEXT("Explicit phase grows cycle without truncating any phase"),DWCombatNumericPolicy::CoordinateTiming(Values,TEXT("weapon.spear.windup_s"),.12,Notice)&&Values[0].Value==.5&&FMath::IsNearlyEqual(Values[3].Value,.5+.108+.372,1.e-12));
 auto Gap=Timing();Gap[3].Value=2.;Gap[0].Value=.3;TestTrue(TEXT("Existing adequate longer interval preserved on phase edit"),DWCombatNumericPolicy::CoordinateTiming(Gap,TEXT("weapon.spear.windup_s"),.2,Notice)&&Gap[3].Value==2.);
 auto Invalid=Timing();Invalid[3].Value=.6;const auto Before=Invalid;
 TestFalse(TEXT("No blanket normalization without a coherent prior-period intent"),DWCombatNumericPolicy::CoordinateTiming(Invalid,TEXT("weapon.spear.interval_s"),.1,Notice));TestTrue(TEXT("Failed coordination atomic"),Same(Before,Invalid));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWUnifiedProviderTest,"DoughWorld.Combat.UnifiedNumeric.CompleteProviderAndSourceGuards",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWUnifiedProviderTest::RunTest(const FString&)
{
 FString Error;const auto* Installed=LoadObject<UDWPlayerCombatTuningSettings>(nullptr,TEXT("/Game/DoughWorld/Core/Data/DA_DWPlayerCombatTuning.DA_DWPlayerCombatTuning"));
 if(!Installed||Installed->NumericSchema!=1){AddError(TEXT("Run root's preserving data migration before this installed-source test; no implicit fallback/migration in test."));return false;}
 auto* S=NewObject<UDWPlayerCombatTuningSettings>();S->NumericSchema=1;S->PlayerNumbers=Installed->PlayerNumbers;S->AbilityNumbers=Installed->AbilityNumbers;S->EnemyNumbers=Installed->EnemyNumbers;S->SimpleSpearNumbers=Installed->SimpleSpearNumbers;
 TSharedPtr<const FDWCombatRules> Base;TSharedPtr<const FDWCombatNumericProvider> Provider;
 if(!FDWCombatRules::LoadProjectPayload(Base,Error)||!FDWCombatNumericProvider::Build(*S,Base,Provider,Error)){AddError(Error);return false;}
 TestEqual(TEXT("Complete player source"),S->PlayerNumbers.Num(),147);TestEqual(TEXT("Complete ability source"),S->AbilityNumbers.Num(),17);TestEqual(TEXT("Nine enemies plus two Neutral NPC sources"),S->EnemyNumbers.Num(),11);
 for(const auto& Row:S->PlayerNumbers){const auto* Actual=Provider->GetRules()->GetValidatedValues().Find(Row.ParameterId);TestTrue(TEXT("Every explicitly stored player number projects exactly"),Actual&&Actual->Value==Row.Value);}
 auto Original=Provider;const auto SavedRows=S->PlayerNumbers;S->PlayerNumbers.RemoveAt(0);
 TestFalse(TEXT("Missing player entry atomically rejects complete catalog"),FDWCombatNumericProvider::Build(*S,Base,Provider,Error));TestTrue(TEXT("Failed build preserves old provider and supplies reason"),Provider==Original&&!Error.IsEmpty());S->PlayerNumbers=SavedRows;
 const FDWEnemyNumbers DuplicateEnemy=S->EnemyNumbers[0];S->EnemyNumbers.Add(DuplicateEnemy);TestFalse(TEXT("Duplicate enemy identity rejected without guard bypass"),FDWCombatNumericProvider::Build(*S,Base,Provider,Error));S->EnemyNumbers.Pop();
 const auto* Sour=Original->FindEnemy({TEXT("/Game/DoughWorld/Characters/Enemies/Sour/Blueprints/BP_DWSourDoughEnemy.BP_DWSourDoughEnemy_C")},EDWEnemyCombatProfile::DoughZombie);
 TestTrue(TEXT("Class-specific Sour does not reuse Mold group"),Sour&&Sour->TypeKey==TEXT("SourDough"));
 for(const TCHAR* Kind:{TEXT("Mold"),TEXT("Yeast")})
 {const FString Path=FString(TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWCreature_"))+Kind+TEXT("NPC.BP_DWCreature_")+Kind+TEXT("NPC_C");const auto* N=Original->FindEnemy({Path},EDWEnemyCombatProfile::Legacy);TestTrue(TEXT("Neutral NPC has complete numeric source without changing disposition"),N&&N->bNeutral&&N->Profile==EDWEnemyCombatProfile::Legacy);}
 auto* Definition=NewObject<UDWCreatureDefinition>();auto* Forged=NewObject<UDWCreatureDefinition>();
 TestFalse(TEXT("Original Definition identity cannot be substituted"),DWCreaturePersistencePolicy::ValidateSource(Forged,Definition,EDWEnemyCombatProfile::Legacy,100,100,180,Error));
 TestFalse(TEXT("Saved cap cannot be forged"),DWEnemyNumericPolicy::ValidateLegacyProjection(100,180,101,100,180));
 const auto& N=Original->GetSimpleSpear();FDWSpearTuning T;
 TestTrue(TEXT("Simple spear uses existing phase/attack construction with independent knockback"),DWSimpleSpearPolicy::Build(N.Damage,N.IntervalSeconds,N.RangeCm,70.,FVector2D(.2,.38),1.1,T,N.KnockbackCm,N.KnockbackSeconds)&&T.Damage==N.Damage&&T.KnockbackCm==N.KnockbackCm&&T.KnockbackSeconds==N.KnockbackSeconds);
 const auto PreviousSimple=S->SimpleSpearNumbers;S->SimpleSpearNumbers.KnockbackSeconds=0.;Error.Reset();
 TestFalse(TEXT("Invalid knockback duration rejected with readable error"),FDWCombatNumericProvider::Build(*S,Base,Provider,Error));TestFalse(TEXT("No empty failure diagnostic"),Error.IsEmpty());S->SimpleSpearNumbers=PreviousSimple;
 // Source mutation after construction cannot alter the snapshot already used by gameplay.
 const double FrozenDamage=Original->GetSimpleSpear().Damage;S->SimpleSpearNumbers.Damage+=3.;TestTrue(TEXT("Frozen provider does not reference editable asset memory"),Original->GetSimpleSpear().Damage==FrozenDamage&&S->SimpleSpearNumbers.Damage!=FrozenDamage);
 return true;
}

#include "DWPlayerCombatTuningPolicy.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWUnifiedDefaultProjectionTest,"DoughWorld.Combat.UnifiedNumeric.DefaultProjectionEquality",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWUnifiedDefaultProjectionTest::RunTest(const FString&)
{
 FString Error;TSharedPtr<const FDWCombatRules> Base,Projected;if(!FDWCombatRules::LoadProjectPayload(Base,Error)){AddError(Error);return false;}
 TMap<FName,double> Complete;for(const auto& Pair:Base->GetValidatedValues())if(DWPlayerCombatTuningPolicy::RuleAllowed(Pair.Key))Complete.Add(Pair.Key,Pair.Value.Value);
 if(!FDWCombatRules::FromCompletePlayerValues(Base,Complete,Projected,Error)){AddError(Error);return false;}
 for(const auto& Pair:Base->GetValidatedValues()){const auto* V=Projected->GetValidatedValues().Find(Pair.Key);TestTrue(TEXT("Legal unchanged full projection preserves every base/derived value and unit"),V&&V->Value==Pair.Value.Value&&V->Unit==Pair.Value.Unit&&V->bCount==Pair.Value.bCount&&V->bDerived==Pair.Value.bDerived);}
 for(auto Profile:{EDWEnemyCombatProfile::MoldGrunt,EDWEnemyCombatProfile::YeastGrunt,EDWEnemyCombatProfile::DoughZombie,EDWEnemyCombatProfile::DoughElite})
 {
  FDWEnemyCombatConfig C,Out;FDWEnemyActorNumericSupplement X;if(!DWEnemyFormal::BuildConfig(*Base,Profile,C,Error)){AddError(Error);return false;}
  FDWEnemyNumbers Source;Source.Profile=Profile;
  auto Add=[&](EDWEnemyNumericField Field,double Value){auto& R=Source.Values.AddDefaulted_GetRef();R.Field=Field;R.Value=Value;};
  Add(EDWEnemyNumericField::MaxHealth,C.Health);
  Add(EDWEnemyNumericField::MoveSpeed,C.MoveSpeedCmPerSecond);
  Add(EDWEnemyNumericField::Damage,C.Damage);
  Add(EDWEnemyNumericField::Windup,C.WindupSeconds);
  Add(EDWEnemyNumericField::Active,C.ActiveSeconds);
  Add(EDWEnemyNumericField::Recovery,C.RecoverySeconds);
  Add(EDWEnemyNumericField::ExtraCooldown,C.ExtraCooldownSeconds);
  Add(EDWEnemyNumericField::Range,C.RangeCm);
  Add(EDWEnemyNumericField::FullAngle,C.AttackFullAngleDegrees);
  Add(EDWEnemyNumericField::Height,C.AttackHeightCm);
  Add(EDWEnemyNumericField::IncomingKnockbackMultiplier,C.KnockbackMultiplier);
  Add(EDWEnemyNumericField::AttackKnockbackMultiplier,C.AttackKnockbackMultiplier);
  Add(EDWEnemyNumericField::OutgoingKnockback,C.OutgoingKnockbackCm);
  Add(EDWEnemyNumericField::OutgoingKnockbackSeconds,C.OutgoingKnockbackSeconds);
  Add(EDWEnemyNumericField::AlertRadius,C.AlertRadiusCm);
  Add(EDWEnemyNumericField::DisengageDistance,C.DisengageDistanceCm);
  Add(EDWEnemyNumericField::AlertSeconds,C.AlertDelaySeconds);
  Add(EDWEnemyNumericField::DisengageSeconds,C.DisengageSeconds);
  Add(EDWEnemyNumericField::WindupLockFraction,C.WindupLockFraction);
  Add(EDWEnemyNumericField::ReturnSpeedMultiplier,C.ReturnSpeedMultiplier);

  Add(EDWEnemyNumericField::PatrolRadius,450);Add(EDWEnemyNumericField::PatrolWaitSeconds,1.5);Add(EDWEnemyNumericField::DeathCleanupSeconds,1.5);
  if(!FDWCombatNumericProvider::ProjectEnemy(Source,Out,X,Error)){AddError(Error);return false;}
  TestEqual(TEXT("Complete enemy Health default preserved"),Out.Health,C.Health);
  TestEqual(TEXT("Complete enemy MoveSpeedCmPerSecond default preserved"),Out.MoveSpeedCmPerSecond,C.MoveSpeedCmPerSecond);
  TestEqual(TEXT("Complete enemy Damage default preserved"),Out.Damage,C.Damage);
  TestEqual(TEXT("Complete enemy WindupSeconds default preserved"),Out.WindupSeconds,C.WindupSeconds);
  TestEqual(TEXT("Complete enemy ActiveSeconds default preserved"),Out.ActiveSeconds,C.ActiveSeconds);
  TestEqual(TEXT("Complete enemy RecoverySeconds default preserved"),Out.RecoverySeconds,C.RecoverySeconds);
  TestEqual(TEXT("Complete enemy ExtraCooldownSeconds default preserved"),Out.ExtraCooldownSeconds,C.ExtraCooldownSeconds);
  TestEqual(TEXT("Complete enemy RangeCm default preserved"),Out.RangeCm,C.RangeCm);
  TestEqual(TEXT("Complete enemy AttackFullAngleDegrees default preserved"),Out.AttackFullAngleDegrees,C.AttackFullAngleDegrees);
  TestEqual(TEXT("Complete enemy AttackHeightCm default preserved"),Out.AttackHeightCm,C.AttackHeightCm);
  TestEqual(TEXT("Complete enemy KnockbackMultiplier default preserved"),Out.KnockbackMultiplier,C.KnockbackMultiplier);
  TestEqual(TEXT("Complete enemy AttackKnockbackMultiplier default preserved"),Out.AttackKnockbackMultiplier,C.AttackKnockbackMultiplier);
  TestEqual(TEXT("Complete enemy OutgoingKnockbackCm default preserved"),Out.OutgoingKnockbackCm,C.OutgoingKnockbackCm);
  TestEqual(TEXT("Complete enemy OutgoingKnockbackSeconds default preserved"),Out.OutgoingKnockbackSeconds,C.OutgoingKnockbackSeconds);
  TestEqual(TEXT("Complete enemy AlertRadiusCm default preserved"),Out.AlertRadiusCm,C.AlertRadiusCm);
  TestEqual(TEXT("Complete enemy DisengageDistanceCm default preserved"),Out.DisengageDistanceCm,C.DisengageDistanceCm);
  TestEqual(TEXT("Complete enemy AlertDelaySeconds default preserved"),Out.AlertDelaySeconds,C.AlertDelaySeconds);
  TestEqual(TEXT("Complete enemy DisengageSeconds default preserved"),Out.DisengageSeconds,C.DisengageSeconds);
  TestEqual(TEXT("Complete enemy WindupLockFraction default preserved"),Out.WindupLockFraction,C.WindupLockFraction);
  TestEqual(TEXT("Complete enemy ReturnSpeedMultiplier default preserved"),Out.ReturnSpeedMultiplier,C.ReturnSpeedMultiplier);

  TestTrue(TEXT("Profile, one-hit contract and total cycle preserved"),Out.Profile==C.Profile&&Out.HitsPerTarget==C.HitsPerTarget&&Out.AttackCycleSeconds()==C.AttackCycleSeconds());
 }
 return true;
}

#endif
