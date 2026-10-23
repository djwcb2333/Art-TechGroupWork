#include "DWCombatOverlapTestProbe.h"
void UDWCombatOverlapTestProbe::Overlap(AActor* Actor,AActor* Other)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(Callback)Callback(Actor,Other);
#endif
}
