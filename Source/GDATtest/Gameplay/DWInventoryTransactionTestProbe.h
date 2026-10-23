#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DWInventoryTransactionTestProbe.generated.h"

// A dynamic delegate needs a real reflected callback. This class has no production instances,
// Blueprint subclass, or gameplay state. Its callback is inert outside developer tests.
UCLASS(Transient, NotBlueprintable)
class UDWInventoryTransactionTestProbe : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION()
    void OnInventoryChanged();
    UFUNCTION()
    void OnWorldEventState(bool bCompleted);
#if WITH_DEV_AUTOMATION_TESTS
    TFunction<void()> TestHook;
    TFunction<void(bool)> WorldEventHook;
#endif
};
