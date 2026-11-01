#include "DWCreaturePatrolRoutePolicy.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCreaturePatrolDiskTest,"DoughWorld.Creature.PatrolRoute.Geometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCreaturePatrolDiskTest::RunTest(const FString&)
{
 using namespace DWCreaturePatrolRoutePolicy;
 const FVector C(500,-200,0);
 TestTrue(TEXT("Capsule fits exactly at outer boundary"),Inside(C+FVector(364,0,0),C,400,36));
 TestFalse(TEXT("Center inside disk is insufficient for full capsule"),Inside(C+FVector(365,0,0),C,400,36));
 TestFalse(TEXT("Moved anchor leaves pawn outside: no expanded return envelope"),Inside(C,FVector(1200,-200,0),400,36));
 TestFalse(TEXT("Endpoint cannot overlap inner exclusion band"),Target(C+FVector(135,0,0),C,100,400,36));
 TestTrue(TEXT("Endpoint at inner capsule margin"),Target(C+FVector(136,0,0),C,100,400,36));
 TestFalse(TEXT("Too-small radius cannot contain capsule"),Inside(C,C,20,36));
 TestFalse(TEXT("Inverted annulus admits no target"),Target(C+FVector(180,0,0),C,180,200,36));
 TestFalse(TEXT("Invalid numeric input is rejected"),Inside(C,C,std::numeric_limits<double>::quiet_NaN(),36));
 // A path whose endpoints fit may still leave the disk: inspect every corridor vertex.
 const TArray<FVector> Detour={C+FVector(150,0,0),C+FVector(0,380,0),C+FVector(-150,0,0)};
 bool All=true;for(const auto& Point:Detour)All&=Inside(Point,C,400,36);
 TestFalse(TEXT("Reject full-route detour beyond capsule-inclusive boundary"),All);
 return true;
}
#endif
