#include "DWYeastDashTestPlayer.h"
#include "UObject/Class.h"
void ADWYeastDashTestPlayer::ProcessEvent(UFunction* Function,void* Parameters)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(Function&&Function->GetFName()==GET_FUNCTION_NAME_CHECKED(ADWPlayerCharacter,OnDashStarted)&&DashStartedCallback)
    {
        auto Callback=DashStartedCallback;Callback();
        if(IsActorBeingDestroyed())return;
    }
#endif
    Super::ProcessEvent(Function,Parameters);
}
