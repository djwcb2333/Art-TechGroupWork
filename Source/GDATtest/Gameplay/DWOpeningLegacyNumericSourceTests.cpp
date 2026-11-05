#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatNumericProvider.h"
#include "DWPlayerCombatTuningSubsystem.h"
#include "DWCombatRules.h"
#include "DWEnemyCharacter.h"
#include "DWSourDoughEnemy.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWOpeningLegacyNumericSourceTest,"DoughWorld.Combat.UnifiedNumeric.OpeningLegacyHostWithFormalSourSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWOpeningLegacyNumericSourceTest::RunTest(const FString&)
{
 // No World, actor instance, BeginPlay, input, slot read/write, damage or travel.
 // Read the actual installed 23-field Sour catalog and its actual source CDO.
 FString Error;const auto* Settings=LoadObject<UDWPlayerCombatTuningSettings>(nullptr,TEXT("/Game/DoughWorld/Core/Data/DA_DWPlayerCombatTuning.DA_DWPlayerCombatTuning"));
 TSharedPtr<const FDWCombatRules> Base;TSharedPtr<const FDWCombatNumericProvider> Provider;
 if(!Settings||!FDWCombatRules::LoadProjectPayload(Base,Error)||!FDWCombatNumericProvider::Build(*Settings,Base,Provider,Error)){AddError(Error);return false;}
 const TCHAR* Path=TEXT("/Game/DoughWorld/Characters/Enemies/Sour/Blueprints/BP_DWSourDoughEnemy.BP_DWSourDoughEnemy_C");
 auto* Class=LoadClass<ADWSourDoughEnemy>(nullptr,Path);const auto* C=Class?Class->GetDefaultObject<ADWSourDoughEnemy>():nullptr;
 if(!C){AddError(TEXT("Actual Sour class defaults missing"));return false;}
 const auto* Formal=Provider->FindEnemy({Class->GetPathName()},EDWEnemyCombatProfile::Legacy);
 if(!Formal){AddError(TEXT("Sour class-any source missing"));return false;}
 TestTrue(TEXT("Real failing combination is Legacy request plus class-any formal Sour23"),Formal->Profile==EDWEnemyCombatProfile::DoughZombie&&Formal->Values.Num()==23&&C->CombatProfile==EDWEnemyCombatProfile::DoughZombie);
 bool LegacyCanConsumeFormal=true;for(const auto& R:Formal->Values)LegacyCanConsumeFormal&=DWEnemyNumericPolicy::ValidateLegacyValue(R.Field,R.Value);
 TestFalse(TEXT("Prior bug reproduced without any World: formal record rejected by Legacy adapter"),LegacyCanConsumeFormal);
 FDWEnemyNumbers Opening;Opening.TypeKey=TEXT("OpeningLegacy");Opening.TargetClass=FSoftClassPath(Class->GetPathName());Opening.Profile=EDWEnemyCombatProfile::Legacy;
 auto Add=[&](EDWEnemyNumericField Field,double Value){auto& R=Opening.Values.AddDefaulted_GetRef();R.Field=Field;R.Value=Value;};
 // Exact director preparation: only these four values differ from actor defaults.
 Add(EDWEnemyNumericField::MaxHealth,60);Add(EDWEnemyNumericField::Damage,4);Add(EDWEnemyNumericField::PatrolRadius,0);Add(EDWEnemyNumericField::AlertRadius,1);
 Add(EDWEnemyNumericField::MoveSpeed,C->MoveSpeed);Add(EDWEnemyNumericField::Range,C->AttackRange);Add(EDWEnemyNumericField::Windup,C->AttackWindup);Add(EDWEnemyNumericField::Active,C->AttackActiveDuration);Add(EDWEnemyNumericField::Recovery,C->AttackRecovery);Add(EDWEnemyNumericField::Interval,C->AttackInterval);
 Add(EDWEnemyNumericField::FullAngle,C->AttackHalfAngle*2.);Add(EDWEnemyNumericField::DisengageDistance,C->LoseTargetRadius);Add(EDWEnemyNumericField::AlertSeconds,C->AlertDuration);Add(EDWEnemyNumericField::PatrolWaitSeconds,C->PatrolWaitTime);Add(EDWEnemyNumericField::DeathCleanupSeconds,C->DeathDestroyDelay);
 TestTrue(TEXT("Actual Sour still has no variants; fixture does not invent attacks"),C->AttackVariants.IsEmpty());
 const auto FormalBefore=Formal->Values;
 const FDWEnemyNumbers* Selected=Formal;
 TestFalse(TEXT("Legacy profile alone cannot silently bypass source identity"),FDWCombatNumericProvider::ResolveEnemySource(Formal,EDWEnemyCombatProfile::Legacy,nullptr,Selected,Error));
 TestTrue(TEXT("Unbound rejection is atomic and diagnostic"),Selected==Formal&&!Error.IsEmpty());
 for(int32 I=0;I<3;++I)
 {
  TestTrue(TEXT("Each of the three director-bound Legacy hosts selects its prepared full Legacy source"),FDWCombatNumericProvider::ResolveEnemySource(Formal,EDWEnemyCombatProfile::Legacy,&Opening,Selected,Error)&&Selected==&Opening&&Selected->Values.Num()==15);
  for(const auto& R:Selected->Values)TestTrue(TEXT("Every selected row now passes the same actual Legacy domain gate"),DWEnemyNumericPolicy::ValidateLegacyValue(R.Field,R.Value));
 }
 auto N=[&](EDWEnemyNumericField F){return Opening.Values.FindByPredicate([&](const auto& R){return R.Field==F;})->Value;};
 TestTrue(TEXT("Tutorial HP60/damage4 and original timing remain exact"),N(EDWEnemyNumericField::MaxHealth)==60&&N(EDWEnemyNumericField::Damage)==4&&N(EDWEnemyNumericField::Windup)==C->AttackWindup&&N(EDWEnemyNumericField::Active)==C->AttackActiveDuration&&N(EDWEnemyNumericField::Recovery)==C->AttackRecovery&&N(EDWEnemyNumericField::Interval)==C->AttackInterval);
 TestTrue(TEXT("Formal map Sour still selects original exact formal object"),FDWCombatNumericProvider::ResolveEnemySource(Formal,EDWEnemyCombatProfile::DoughZombie,nullptr,Selected,Error)&&Selected==Formal);
 TestTrue(TEXT("Capture/validation resolve the same bound source repeatedly, without reapplying StartCombat fields"),FDWCombatNumericProvider::ResolveEnemySource(Formal,EDWEnemyCombatProfile::Legacy,&Opening,Selected,Error)&&DWEnemyNumericPolicy::ValidateLegacyProjection(60,static_cast<float>(C->MoveSpeed),60,60,C->MoveSpeed));
 TestFalse(TEXT("Saved HP-cap forgery remains rejected"),DWEnemyNumericPolicy::ValidateLegacyProjection(60,C->MoveSpeed,61,60,C->MoveSpeed));
 auto WrongClass=Opening;WrongClass.TargetClass=FSoftClassPath(TEXT("/Game/Other.Other_C"));Selected=Formal;
 TestFalse(TEXT("Wrong class snapshot cannot be used as teaching context"),FDWCombatNumericProvider::ResolveEnemySource(Formal,EDWEnemyCombatProfile::Legacy,&WrongClass,Selected,Error));TestTrue(TEXT("Wrong identity rejection keeps output"),Selected==Formal);
 auto Bad=Opening;Bad.Values[0].Value=-1.;
 TestFalse(TEXT("Invalid teaching data cannot bypass original finite/bounds validation"),FDWCombatNumericProvider::ResolveEnemySource(Formal,EDWEnemyCombatProfile::Legacy,&Bad,Selected,Error));
 TestFalse(TEXT("Teaching source cannot replace formal gameplay profile"),FDWCombatNumericProvider::ResolveEnemySource(Formal,EDWEnemyCombatProfile::DoughZombie,&Opening,Selected,Error));
 TestEqual(TEXT("Original formal field count unchanged"),Formal->Values.Num(),FormalBefore.Num());
 for(int32 I=0;I<FormalBefore.Num();++I)TestTrue(TEXT("Original formal fields and source identities untouched"),Formal->Values[I].Field==FormalBefore[I].Field&&Formal->Values[I].Value==FormalBefore[I].Value&&Formal->Values[I].VariantAnimation==FormalBefore[I].VariantAnimation);
 return true;
}
#endif
