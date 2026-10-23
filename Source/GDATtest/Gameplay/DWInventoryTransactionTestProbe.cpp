#include "DWInventoryTransactionTestProbe.h"

void UDWInventoryTransactionTestProbe::OnWorldEventState(bool bCompleted)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(WorldEventHook)WorldEventHook(bCompleted);
#endif
}

void UDWInventoryTransactionTestProbe::OnInventoryChanged()
{
#if WITH_DEV_AUTOMATION_TESTS
    if(TestHook)TestHook();
#endif
}
