#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Components/BoxComponent.h"
#include "DWDamageVolumeLibrary.generated.h"

/** Add this box in Blueprint, then move/resize it. Does not block movement or projectiles. */
UCLASS(Blueprintable, ClassGroup=(DoughWorld), meta=(BlueprintSpawnableComponent, DisplayName="DW Damage Hitbox"))
class GDATTEST_API UDWDamageHitboxComponent : public UBoxComponent
{
    GENERATED_BODY()
public:
    UDWDamageHitboxComponent();
};

/** Shared physics queries; health/death still belong to the victim's existing damage handler. */
UCLASS()
class GDATTEST_API UDWDamageVolumeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Only explicit DW hurtboxes count when any exist. Legacy native actors fall back to their physical collision, never their origin. */
    UFUNCTION(BlueprintPure, Category="Dough World|Damage")
    static bool IsDamageHitComponent(const UPrimitiveComponent* Component);

    /** Exact engine simple-collision overlap against the attack component. Unique actors, no origin-distance filter. */
    UFUNCTION(BlueprintCallable, Category="Dough World|Damage", meta=(AutoCreateRefTerm="ActorsToIgnore"))
    static TArray<AActor*> GetDamageTargetsInVolume(UPrimitiveComponent* AttackVolume, const TArray<AActor*>& ActorsToIgnore);

    /** Reusable melee/explosion sphere versus the target's final transformed hurtboxes. */
    UFUNCTION(BlueprintPure, Category="Dough World|Damage")
    static bool IsTargetInDamageSphere(AActor* Target, FVector Center, float Radius);
};

