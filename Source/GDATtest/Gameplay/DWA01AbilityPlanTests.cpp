#include "DWA01AbilityPlan.h"

#if defined(DW_A01_ABILITY_STANDALONE_TESTS) || WITH_DEV_AUTOMATION_TESTS
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
namespace DWA01AbilityTests
{
bool Near(double A,double B){return std::abs(A-B)<1.e-8;}
using FSink=std::function<void(const char*,bool)>;
double FinishDash(FDWA01AbilityPlan& P,double Partition=100.)
{
    double Distance=0.;int Limit=0;
    while(P.IsDashing()&&++Limit<10000)
    {
        FDWA01AbilityStep S;if(!P.AdvanceNext(Partition,450.,0.,1.,S)||S.AdvancedSeconds<=0.)break;Distance+=S.DistanceCm;
    }
    return Distance;
}
void StartDash(FDWA01AbilityPlan& P)
{FDWA01AbilityStep Step;P.Press();P.AdvanceNext(.1,450.,1.,0.,Step);P.Release(1.,0.,Step);P.AcceptDash();}
void Run(const FSink& Check)
{
    FDWA01AbilityConfig C;FDWA01AbilityPlan P;FDWA01AbilityStep S;
    Check("trial dash duration and derived route match the actual speed integral",C.IsValid()&&Near(C.DashDurationSeconds(),.45)&&Near(C.DashDistanceCm(450.),315.));
    Check("plan initializes without starting an ability",P.Initialize(C)&&P.GetState().Phase==EDWA01AbilityPhase::Idle&&!P.IsMenuBlocked());
    Check("physical down starts pending rather than spending gas or moving",P.Press()&&P.GetState().Phase==EDWA01AbilityPhase::Pending&&!P.IsDashing()&&!P.IsSprinting());
    Check("repeated down cannot manufacture another request",!P.Press());
    Check("pending tap moves no distance before release",P.AdvanceNext(.1,450.,1.,0.,S)&&Near(S.AdvancedSeconds,.1)&&S.DistanceCm==0.&&S.StartRequest==EDWA01AbilityStart::None);
    Check("early release requests exactly the dash ability",P.Release(3.,4.,S)&&S.StartRequest==EDWA01AbilityStart::Dash&&!P.IsDashing()&&!P.IsSprinting());
    Check("unresolved admission cannot silently progress or queue",!P.AdvanceNext(1.,450.,1.,0.,S));
    Check("resource owner can reject an unfunded tap without a cooldown",P.RejectStart()&&P.GetState().Phase==EDWA01AbilityPhase::Idle&&P.GetState().CooldownRemainingSeconds==0.&&!P.GetState().bReleaseRequired);
    Check("rejected tap does not execute later after time advances",P.AdvanceNext(5.,450.,1.,0.,S)&&!P.IsDashing()&&S.StartRequest==EDWA01AbilityStart::None);

    P.Initialize(C);P.Press();P.AdvanceNext(.179,450.,1.,0.,S);P.Release(1.,0.,S);P.AcceptDash();
    Check("accepted tap starts acceleration and has no invulnerability",P.GetState().Phase==EDWA01AbilityPhase::DashAcceleration&&!P.IsInvulnerable());
    P.AdvanceNext(1.,450.,0.,1.,S);
    Check("acceleration stops at its boundary and integrates a triangle",Near(S.AdvancedSeconds,.1)&&Near(S.DistanceCm,45.)&&S.PhaseAfter==EDWA01AbilityPhase::DashHold&&!S.bInvulnerableDuringStep&&S.bInvulnerableAfterStep);
    Check("dash facing is locked despite a different supplied movement direction",Near(S.DirectionX,1.)&&Near(S.DirectionY,0.));
    P.AdvanceNext(1.,450.,-1.,0.,S);
    Check("only the constant-speed hold segment is invulnerable",Near(S.AdvancedSeconds,.25)&&Near(S.DistanceCm,225.)&&S.bInvulnerableDuringStep&&!S.bInvulnerableAfterStep&&S.PhaseAfter==EDWA01AbilityPhase::DashDeceleration);
    P.AdvanceNext(1.,450.,-1.,0.,S);
    Check("deceleration integrates the other triangle and ends dash",Near(S.AdvancedSeconds,.1)&&Near(S.DistanceCm,45.)&&!S.bInvulnerableDuringStep&&S.bDashEnded&&!P.IsDashing());
    Check("finished standard dash is exactly 315cm with a 0.3 second cooldown",Near(P.GetState().DashDistanceCm,315.)&&Near(P.GetState().CooldownRemainingSeconds,.3));
    Check("M02 dash cooldown does not reject pending input",P.Press()&&P.GetState().Phase==EDWA01AbilityPhase::Pending);
    P.AdvanceNext(.05,450.,1.,0.,S);P.Release(1.,0.,S);
    Check("M02 tap selection still rejects a dash whose cooldown has not elapsed",S.StartRequest==EDWA01AbilityStart::Dash&&!P.AcceptDash()&&P.RejectStart());
    P.Press();P.AdvanceNext(.18,450.,1.,0.,S);
    Check("M02 a hold may accept sprint while dash cooldown remains",S.StartRequest==EDWA01AbilityStart::Sprint&&P.GetState().CooldownRemainingSeconds>0.&&P.AcceptSprint());
    P.AdvanceNext(.1,450.,1.,0.,S);
    Check("M02 dash cooldown advances during both pending and sprint",Near(P.GetState().CooldownRemainingSeconds,0.));P.Release(1.,0.,S);

    P.Initialize(C);P.Press();P.AdvanceNext(1.,450.,1.,0.,S);
    Check("hold threshold stops time at 0.18 and requests sprint once",Near(S.AdvancedSeconds,.18)&&S.StartRequest==EDWA01AbilityStart::Sprint&&!P.IsSprinting()&&!P.IsDashing());
    Check("dash cannot accept a sprint request",!P.AcceptDash());
    Check("external qualification can accept sprint",P.AcceptSprint()&&P.IsSprinting()&&P.IsMenuBlocked());
    P.AdvanceNext(.2,450.,1.,0.,S);
    Check("sprint uses final walk times 1.2",Near(S.DistanceCm,108.)&&Near(S.DirectionX,1.)&&Near(S.DirectionY,0.)&&!S.bInvulnerableDuringStep);
    P.AdvanceNext(.2,450.,0.,-1.,S);
    Check("sprint can freely change direction on the next slice",Near(S.DistanceCm,108.)&&Near(S.DirectionX,0.)&&Near(S.DirectionY,-1.));
    P.Release(1.,0.,S);
    Check("release after sprint ends sprint without triggering dash or cooldown",S.bSprintEnded&&S.StartRequest==EDWA01AbilityStart::None&&!P.IsSprinting()&&P.GetState().CooldownRemainingSeconds==0.);

    P.Initialize(C);P.Press();P.AdvanceNext(.18,450.,1.,0.,S);P.RejectStart();
    Check("unfunded hold requires release and does not fall back to dash",P.GetState().bReleaseRequired&&!P.AcceptDash()&&!P.AcceptSprint());
    P.AdvanceNext(1.,450.,1.,0.,S);P.Release(1.,0.,S);
    Check("release after failed hold emits no tap request",S.StartRequest==EDWA01AbilityStart::None&&!P.IsDashing()&&!P.IsSprinting());
    P.Initialize(C);P.Press();P.AdvanceNext(.18,450.,1.,0.,S);P.Release(1.,0.,S);
    Check("release while a hold admission is pending cancels it without a tap",P.GetState().Phase==EDWA01AbilityPhase::Idle&&S.StartRequest==EDWA01AbilityStart::None&&!P.AcceptSprint());
    P.Initialize(C);P.Press();P.AdvanceNext(.18,450.,1.,0.,S);P.AcceptSprint();P.AdvanceNext(.1,450.,0.,0.,S);
    Check("stationary sprint ends and requires a new release edge",S.bSprintEnded&&S.AdvancedSeconds==0.&&P.GetState().bReleaseRequired&&!P.IsSprinting());

    P.Initialize(C);StartDash(P);P.AdvanceNext(.1,450.,1.,0.,S);
    Check("root sees invulnerability during the hold before collision",P.IsInvulnerable());
    Check("enemy hit ends dash immediately and enters cooldown",P.EndOnHit()&&!P.IsInvulnerable()&&!P.IsDashing()&&Near(P.GetState().CooldownRemainingSeconds,.3));
    Check("second collision cannot terminate or charge the same episode again",!P.EndOnHit()&&!P.EndOnWall());
    P.Initialize(C);StartDash(P);
    Check("a wall terminates dash before a later phase",P.EndOnWall()&&!P.IsDashing());
    P.Initialize(C);StartDash(P);P.AdvanceNext(.1,450.,1.,0.,S);P.Cancel();
    Check("form/resource cancellation removes active invulnerability",!P.IsInvulnerable()&&!P.IsDashing()&&Near(P.GetState().CooldownRemainingSeconds,.3));
    P.Initialize(C);P.RequireRelease();
    Check("entering a form with held Space cannot start until release",!P.Press()&&P.GetState().bReleaseRequired);P.Release(1.,0.,S);
    Check("release after context change restores normal input",P.Press());

    FDWA01AbilityPlan Whole,Parts;Whole.Initialize(C);Parts.Initialize(C);StartDash(Whole);StartDash(Parts);
    const double WholeDistance=FinishDash(Whole),PartsDistance=FinishDash(Parts,.007);
    Check("phase clipping yields partition-independent dash distance",Near(WholeDistance,315.)&&Near(PartsDistance,315.)&&!Whole.IsDashing()&&!Parts.IsDashing());
    P.Initialize(C);P.Press();P.AdvanceNext(.1,450.,1.,0.,S);P.Release(0.,0.,S);
    Check("invalid facing cannot create a paid dash request",S.bInputRejected&&S.StartRequest==EDWA01AbilityStart::None&&!P.AcceptDash());
    P.Initialize(C);Check("load restores only cooldown and a fresh held-input guard",P.RestoreIdleCooldown(.2,true)&&!P.IsDashing()&&!P.IsSprinting()&&P.GetState().bReleaseRequired&&!P.Press());
    Check("invalid loaded cooldown leaves prior state intact",!P.RestoreIdleCooldown(.31,false)&&Near(P.GetState().CooldownRemainingSeconds,.2));
    auto Bad=C;Bad.DashHoldSeconds=0.;Check("missing middle phase is rejected rather than silently losing invulnerability",!Bad.IsValid());
    Check("NaN and negative time do not advance movement",!P.AdvanceNext(std::numeric_limits<double>::quiet_NaN(),450.,1.,0.,S)&&!P.AdvanceNext(-1.,450.,1.,0.,S));
}
}
#if defined(DW_A01_ABILITY_STANDALONE_TESTS)
#include <iostream>
int main()
{
    int Checks=0,Failures=0;
    DWA01AbilityTests::Run([&](const char* Name,bool Pass){++Checks;if(!Pass){++Failures;std::cerr<<"FAIL: "<<Name<<'\n';}});
    std::cout<<"A01 ability native checks: "<<Checks<<", failures: "<<Failures<<'\n';return Failures?1:0;
}
#elif WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWA01AbilityPlanTest,"DoughWorld.LanguageIntegration.A01.SpaceAbilityPlan",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWA01AbilityPlanTest::RunTest(const FString& Parameters)
{DWA01AbilityTests::Run([this](const char* Name,bool Pass){TestTrue(UTF8_TO_TCHAR(Name),Pass);});return true;}
#endif
#endif
