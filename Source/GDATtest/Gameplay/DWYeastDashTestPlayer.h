#pragma once
#include "DWPlayerCharacter.h"
#include "DWYeastDashTestPlayer.generated.h"

/** Transient native fixture: intercepts the real Blueprint event dispatch without creating an asset. */
UCLASS(Transient,NotBlueprintable)
class GDATTEST_API ADWYeastDashTestPlayer : public ADWPlayerCharacter
{
    GENERATED_BODY()
public:
    virtual void ProcessEvent(UFunction* Function,void* Parameters)override;
#if WITH_DEV_AUTOMATION_TESTS
    TFunction<void()> DashStartedCallback;
#endif
};
