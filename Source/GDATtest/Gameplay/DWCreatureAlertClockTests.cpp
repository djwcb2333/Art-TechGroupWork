#include "DWCreatureAlertClock.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCreatureAlertClockTest,"DoughWorld.Creature.AlertClock.Boundaries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCreatureAlertClockTest::RunTest(const FString&)
{
    using DWCreatureAlertClock::SourceSeconds;
    constexpr float L=88.f/24.f;const double D=.45,Start=10.,End=Start+D;
    TestTrue(TEXT("Before beginning clamps to first pose"),SourceSeconds(Start-1,End,D,L)==0.f);
    TestTrue(TEXT("Beginning samples first pose"),FMath::IsNearlyZero(SourceSeconds(Start,End,D,L)));
    TestTrue(TEXT("Midpoint uses formal remaining time"),FMath::IsNearlyEqual(SourceSeconds(Start+D*.5,End,D,L),L*.5f));
    TestEqual(TEXT("Deadline samples complete clip"),SourceSeconds(End,End,D,L),L);
    TestEqual(TEXT("Cooldown stationary Alert holds final pose"),SourceSeconds(End+10,End,D,L),L);
    TestEqual(TEXT("Zero duration finishes without extending authority"),SourceSeconds(Start,Start,0,L),L);
    TestEqual(TEXT("Pause: unchanged game time gives unchanged pose"),SourceSeconds(Start+.2,End,D,L),SourceSeconds(Start+.2,End,D,L));
    TestTrue(TEXT("Restore reconstructs same progress on new world clock"),FMath::IsNearlyEqual(SourceSeconds(10,10.2,D,L),SourceSeconds(100,100.2,D,L)));
    TestTrue(TEXT("Another alert restarts from its own new deadline"),FMath::IsNearlyZero(SourceSeconds(20,20+D,D,L)));
    TestEqual(TEXT("Invalid duration fails closed"),SourceSeconds(10,10.2,-1,L),0.f);
    TestEqual(TEXT("Missing source fails closed"),SourceSeconds(10,10.2,D,0),0.f);
    TestEqual(TEXT("NaN time fails closed"),SourceSeconds(std::numeric_limits<double>::quiet_NaN(),End,D,L),0.f);
    TestEqual(TEXT("Infinite end fails closed"),SourceSeconds(Start,std::numeric_limits<double>::infinity(),D,L),0.f);
    TestEqual(TEXT("Invalid source fails closed"),SourceSeconds(Start,End,D,std::numeric_limits<float>::quiet_NaN()),0.f);
    float Previous=0.f;for(int32 I=0;I<=100;++I){float T=SourceSeconds(Start+D*I/100.,End,D,L);TestTrue(TEXT("Monotone bounded source"),T>=Previous&&T<=L);Previous=T;}
    return true;
}
#endif
