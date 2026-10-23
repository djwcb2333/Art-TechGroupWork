#include "DWWeaponResourceTestProbe.h"
void UDWWeaponResourceTestProbe::OnResources(int32 Loaded,int64 Revision,bool bReload)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(TestHook)TestHook(Loaded,Revision,bReload);
#endif
}
