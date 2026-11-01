#include "Misc/AutomationTest.h"
#include "DWCombatSandboxContext.h"
#include "DWGiantAttackTiming.h"
#include <limits>
#include "DWCombatRules.h"
#include "DWCombatMoldRoot.h"
#include "DWCharacterA01SaveBridge.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatSandboxPolicyTest,"DoughWorld.Combat.Source20261031.SandboxAndContactPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatSandboxPolicyTest::RunTest(const FString&)
{
 for(int Mask=0;Mask<16;++Mask)
 {
  const bool Editor=(Mask&1)!=0,PIE=(Mask&2)!=0,Commandlet=(Mask&4)!=0,Exact=(Mask&8)!=0;
  TestEqual(TEXT("Only exact editor PIE, never commandlet/standalone/other map"),DWCombatSandbox::ModeAllowed(Editor,PIE,Commandlet,Exact),Editor&&PIE&&!Commandlet&&Exact);
 }
 for(const FVector Timing:{FVector(1.25,.125,.625),FVector(2.,.4,1.),FVector(.5,.25,2.)})
 {
  const float W=Timing.X,A=Timing.Y,R=Timing.Z;
  TestTrue(TEXT("Contact follows frozen windup"),FMath::IsNearlyEqual(UDWGiantAttackTiming::Map(W,W,A,R,1.25f,1.375f,2.f),1.25f));
  TestTrue(TEXT("Active end follows frozen active duration"),FMath::IsNearlyEqual(UDWGiantAttackTiming::Map(W+A,W,A,R,1.25f,1.375f,2.f),1.375f));
  TestTrue(TEXT("Full source recovery preserved"),FMath::IsNearlyEqual(UDWGiantAttackTiming::Map(W+A+R,W,A,R,1.25f,1.375f,2.f),2.f));
  TestTrue(TEXT("No contact before strike"),UDWGiantAttackTiming::Map(W*.9f,W,A,R,1.25f,1.375f,2.f)<1.25f);
 }

 TestEqual(TEXT("Zero windup starts at contact"),UDWGiantAttackTiming::Map(0.f,0.f,.125f,.625f,1.25f,1.375f,2.f),1.25f);
 TestEqual(TEXT("Zero windup preserves active progression"),UDWGiantAttackTiming::Map(.0625f,0.f,.125f,.625f,1.25f,1.375f,2.f),1.3125f);
 TestEqual(TEXT("Zero recovery immediately ends at active boundary"),UDWGiantAttackTiming::Map(1.375f,1.25f,.125f,0.f,1.25f,1.375f,2.f),2.f);
 TestTrue(TEXT("Zero recovery still plays active segment"),UDWGiantAttackTiming::Map(1.3125f,1.25f,.125f,0.f,1.25f,1.375f,2.f)>1.25f);
 TestEqual(TEXT("Both optional segments zero: start at contact"),UDWGiantAttackTiming::Map(0.f,0.f,.125f,0.f,1.25f,1.375f,2.f),1.25f);
 TestEqual(TEXT("Both optional segments zero: end at source end"),UDWGiantAttackTiming::Map(.125f,0.f,.125f,0.f,1.25f,1.375f,2.f),2.f);
 TestEqual(TEXT("Zero active is rejected without division"),UDWGiantAttackTiming::Map(0.f,0.f,0.f,0.f,1.25f,1.375f,2.f),0.f);
 TestEqual(TEXT("Negative windup is invalid"),UDWGiantAttackTiming::Map(0.f,-1.f,.125f,.625f,1.25f,1.375f,2.f),0.f);
 TestEqual(TEXT("Negative recovery is invalid"),UDWGiantAttackTiming::Map(0.f,1.f,.125f,-1.f,1.25f,1.375f,2.f),0.f);
 TestEqual(TEXT("Nonfinite frozen segment is invalid"),UDWGiantAttackTiming::Map(0.f,std::numeric_limits<float>::quiet_NaN(),.125f,.625f,1.25f,1.375f,2.f),0.f);
 TestEqual(TEXT("Nonfinite source time is invalid"),UDWGiantAttackTiming::Map(std::numeric_limits<float>::infinity(),1.25f,.125f,.625f,1.25f,1.375f,2.f),0.f);
 return true; // Pure value checks: no world, pawn, disk or player input.
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatApprovedConfigTest,"DoughWorld.Combat.Source20261031.ApprovedConfigAndBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatApprovedConfigTest::RunTest(const FString&)
{
 FString Error,Text;TSharedPtr<const FDWCombatRules> Rules;
 if(!TestTrue(TEXT("Current strict 280+6 payload parses"),FDWCombatRules::LoadProjectPayload(Rules,Error))){AddError(Error);return false;}
 FDWMoldRootConfig Root;if(!TestTrue(TEXT("Approved Root config is coherent"),FDWMoldRootConfig::Build(*Rules,Root,Error)))return false;
 TestEqual(TEXT("Entry budget"),Root.StartCost,8.);TestEqual(TEXT("Drain budget"),Root.SpendPerSecond,3.);TestEqual(TEXT("Retired Special income does not fund A01 Root"),Root.SpecialGainPerSecond,0.);
 TestEqual(TEXT("Damage replaces old multiplier"),Root.DamageMultiplier,1.10);TestEqual(TEXT("Incoming multiplier"),Root.IncomingMultiplier,.70);
 TestTrue(TEXT("100 point idle budget is 30.6667 seconds"),FMath::IsNearlyEqual((100.-Root.StartCost)/Root.SpendPerSecond,30.6666666667,1.e-7));
 TSharedPtr<FJsonObject> Trial;
 if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWA01TrialDefaults.json")))||
  !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Trial))return false;
 FDWCharacterA01Config C;if(!TestTrue(TEXT("Real shared PDF config builds"),DWCharacterA01SaveBridge::BuildDerivedConfig(Trial,{},C,Error)))return false;
 FDWCharacterA01Runtime R;TestTrue(TEXT("Shared runtime initializes"),R.Initialize(C));TestTrue(TEXT("Mold enter"),R.EnterForm(EDWA01Form::Mold));
 TestEqual(TEXT("PDF form starts at 180 seconds"),R.GetState().FormRemainingSeconds,180.);
 TestTrue(TEXT("One accepted harvest action"),R.BeginToolAction(1,false,true));
 const auto Hit=R.ResolveToolHit(1,true,true);const double Paid=R.GetState().Corruption;
 TestTrue(TEXT("Actual accepted action charges ten once"),Hit.bChargedThisCall&&Hit.HarvestMultiplier==1.5&&Paid==90.);
 const auto Again=R.ResolveToolHit(1,true,true);TestTrue(TEXT("Another target never duplicates fee"),Again.bEnhanced&&!Again.bChargedThisCall&&R.GetState().Corruption==Paid);
 R.EndToolAction(1);FDWCharacterA01Activity Idle;FDWCharacterA01Step Step;
 TestTrue(TEXT("Tool end immediately resumes shared regen clock"),R.AdvanceNext(.5,Idle,Step));
 TestTrue(TEXT("No added regeneration delay"),FMath::IsNearlyEqual(R.GetState().Corruption,Paid+Step.AdvancedSeconds,1.e-8));
 const auto& Whitelist=Trial->GetArrayField(TEXT("corruption_whitelist"));TestEqual(TEXT("Seven distinct tools"),Whitelist.Num(),7);
 TSet<FString> Ids;for(const auto& Row:Whitelist){const FString Id=Row->AsObject()->GetStringField(TEXT("item_id"));
  TestFalse(TEXT("Permanent weapons are not tool identities"),Id.StartsWith(TEXT("DWWeapon_"))||Id==TEXT("SugarSpear"));Ids.Add(Id);}
 TestEqual(TEXT("No duplicate tool identities"),Ids.Num(),7);return true;
}
#endif
