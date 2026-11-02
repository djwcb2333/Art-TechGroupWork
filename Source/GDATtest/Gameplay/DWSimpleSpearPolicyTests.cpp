#if WITH_DEV_AUTOMATION_TESTS
#include "DWSimpleSpearPolicy.h"
#include "DWCombatCommitLedger.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSimpleSpearPolicyTest,"DoughWorld.Combat.SimpleSpear.SourceAndClock",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSimpleSpearPolicyTest::RunTest(const FString&)
{
 using namespace DWSimpleSpearPolicy;
 FFacts Expected;Expected.ItemId=Item();Expected.Slot=1;Expected.Quantity=1;Expected.InventoryRevision=7;Expected.EquipmentRevision=4;
 Expected.Epoch=2;Expected.Entity=3;Expected.Life=1;Expected.Restore=0;Expected.Action=9;Expected.bSelected=Expected.bAlive=true;
 TestTrue(TEXT("Real simple source needs no permanent identity"),Current(Expected,Expected));
 auto Bad=Expected;Bad.ItemId=TEXT("SugarSpear");TestFalse(TEXT("Permanent alias not a simple tool source"),Current(Expected,Bad));
 Bad=Expected;Bad.Quantity=0;TestFalse(TEXT("Dropped tool cannot hit"),Current(Expected,Bad));
 Bad=Expected;++Bad.InventoryRevision;TestFalse(TEXT("Changed inventory invalidates source"),Current(Expected,Bad));
 Bad=Expected;++Bad.Slot;TestFalse(TEXT("Different physical slot invalidates source"),Current(Expected,Bad));
 Bad=Expected;++Bad.EquipmentRevision;TestFalse(TEXT("Equipment switch invalidates source"),Current(Expected,Bad));
 Bad=Expected;++Bad.Restore;TestFalse(TEXT("Restore generation invalidates source"),Current(Expected,Bad));
 Bad=Expected;++Bad.Life;TestFalse(TEXT("New life invalidates source"),Current(Expected,Bad));
 Bad=Expected;++Bad.Epoch;TestFalse(TEXT("New world invalidates source"),Current(Expected,Bad));
 Bad=Expected;++Bad.Action;TestFalse(TEXT("Another attack cannot use this source"),Current(Expected,Bad));
 Bad=Expected;Bad.bSelected=false;TestFalse(TEXT("Unequipped source cannot hit"),Current(Expected,Bad));
 Bad=Expected;Bad.bAlive=false;TestFalse(TEXT("Dead/invalid owner cannot hit"),Current(Expected,Bad));
 FDWSpearTuning T;
 if(!TestTrue(TEXT("Build independent 25 / 1s / 250cm tool attack"),Build(25.,1.,250.,60.,FVector2D(.2,.38),1.1,T)))return false;
 TestEqual(TEXT("No added knockback"),T.KnockbackCm,0.);
 TestEqual(TEXT("No yeast permanent multiplier"),T.YeastDamageMultiplier,1.);
 TestEqual(TEXT("No permanent attack-speed multiplier"),T.YeastRateMultiplier,1.);
 for(double D:{0.,100.,249.,250.}){double Damage=0.;TestTrue(TEXT("No distance falloff"),T.DamageAtDistance(D,25.,Damage)&&Damage==25.);}
 FDWSpearAttackPlan Plan;TestTrue(TEXT("Existing native plan starts"),Plan.Start(T,EDWSpearForm::Normal));
 TestEqual(TEXT("Windup uses source normalized contact"),Plan.GetPhaseRemainingSeconds(),.2);
 double Used=0.,Left=0.;Plan.AdvanceCurrentPhase(.2,Used,Left);Plan.EnterNextPhase(EDWSpearForm::Normal);
 TestTrue(TEXT("At correct active window"),Plan.GetPhase()==EDWSpearPhase::Active&&FMath::IsNearlyEqual(Plan.GetPhaseRemainingSeconds(),.18));
 FDWSpearTuning Slower;TestTrue(TEXT("Changed Tools cycle remaps same full source"),Build(25.,2.,400.,60.,FVector2D(.2,.38),1.1,Slower)&&Slower.WindupSeconds==.4&&Slower.RangeCm==400.);
 TestFalse(TEXT("Invalid tool cycle rejects"),Build(25.,0.,250.,60.,FVector2D(.2,.38),1.1,Slower));
 TestFalse(TEXT("Invalid source window rejects"),Build(25.,1.,250.,60.,FVector2D(.5,.3),1.1,Slower));
 using namespace DWCombatCommit;
 FLedger Ledger;Ledger.SetWorldEpoch(1);FEntityLife Player{1,1},Enemy{2,1};Ledger.SetLife(Player);Ledger.SetLife(Enemy);
 const auto Action=Ledger.BeginAction(Player);FDamageRequest Hit{FActionDamage{Action,1,EActionDamageKind::Melee,1},Enemy};
 const auto First=Ledger.AdmitDamage(Hit);TestTrue(TEXT("Typed action hit admitted"),First.Result==EAdmission::Admitted);
 Ledger.Resolve(First.Receipt.Handle,EOutcome::Committed);
 TestFalse(TEXT("Same action/phase/target cannot hit twice"),Ledger.AdmitDamage(Hit).Result==EAdmission::Admitted);
 Ledger.EndAction(Action);TestFalse(TEXT("Ended action cannot hit"),Ledger.AdmitDamage(Hit).Result==EAdmission::Admitted);
 // Value types/ledger only. No gameplay World, player, AI, assets or saved-slot loading.
 return true;
}
#endif
