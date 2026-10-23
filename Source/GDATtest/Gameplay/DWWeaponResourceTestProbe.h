#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DWWeaponResourceTestProbe.generated.h"

UCLASS(Transient,NotBlueprintable)
class UDWWeaponResourceTestProbe : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION() void OnResources(int32 Loaded,int64 Revision,bool bReload);
#if WITH_DEV_AUTOMATION_TESTS
    TFunction<void(int32,int64,bool)> TestHook;
#endif
};
