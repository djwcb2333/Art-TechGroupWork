#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DWWeaponAnimationSet.h"
#include "DWWeaponEventReentryTestProbe.generated.h"
class AActor;
class AController;
class UDamageType;
/** Transient delegate fixture only; no production instance or asset references. */
UCLASS(Transient, NotBlueprintable)
class GDATTEST_API UDWWeaponEventReentryTestProbe : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION() void HandleDamage(AActor* DamagedActor,float Damage,const UDamageType* DamageType,AController* InstigatedBy,AActor* DamageCauser);
    UFUNCTION() void HandleAnimation(EDWAnimatedWeapon Weapon,FName Role,int32 EventIndex);
#if WITH_DEV_AUTOMATION_TESTS
    TFunction<void()> DamageCallback;
    TFunction<void(EDWAnimatedWeapon,FName,int32)> AnimationCallback;
#endif
};
