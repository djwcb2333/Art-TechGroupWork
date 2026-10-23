#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DWCombatOverlapTestProbe.generated.h"
class AActor;
UCLASS(Transient,NotBlueprintable)
class UDWCombatOverlapTestProbe : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION() void Overlap(AActor* Actor,AActor* Other);
#if WITH_DEV_AUTOMATION_TESTS
    TFunction<void(AActor*,AActor*)> Callback;
#endif
};
