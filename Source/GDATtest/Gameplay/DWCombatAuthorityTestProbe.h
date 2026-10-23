#pragma once
#include "CoreMinimal.h"
#include "Engine/HitResult.h"
#include "UObject/Object.h"
#include "DWCombatAuthorityTestProbe.generated.h"
class AActor;
class AController;
class UDamageType;
class UPrimitiveComponent;
/** Transient test fixture; no asset or production instance. */
UCLASS(Transient, NotBlueprintable)
class GDATTEST_API UDWCombatAuthorityTestProbe : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION() void Any(AActor* Actor, float Damage, const UDamageType* Type, AController* Instigator, AActor* Causer);
    UFUNCTION() void Point(AActor* Actor, float Damage, AController* Instigator, FVector Location,
        UPrimitiveComponent* Component, FName Bone, FVector Direction, const UDamageType* Type, AActor* Causer);
    UFUNCTION() void Radial(AActor* Actor, float Damage, const UDamageType* Type, FVector Origin,
        const FHitResult& Hit, AController* Instigator, AActor* Causer);
#if WITH_DEV_AUTOMATION_TESTS
    TFunction<void(AActor*, float, AController*, AActor*)> AnyCallback;
    TFunction<void(AActor*, float, AController*, AActor*)> PointCallback;
    TFunction<void(AActor*, float, AController*, AActor*)> RadialCallback;
#endif
};
