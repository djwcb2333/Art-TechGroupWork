#pragma once
#include "CoreMinimal.h"
#include "DWCombatTypes.generated.h"

class UAnimationAsset;

/** One independently authored melee strike. Gameplay owns its damage clock. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWMeleeComboStep
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Animation") TObjectPtr<UAnimationAsset> Animation=nullptr;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage",meta=(ClampMin="0")) float Damage=25.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0",Units="s")) float WindupSeconds=.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.01",Units="s")) float ActiveSeconds=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0",Units="s")) float RecoverySeconds=.3f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Range",meta=(ClampMin="1",Units="cm")) float Range=180.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Range",meta=(ClampMin="1",Units="cm")) float HalfWidth=65.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Range",meta=(ClampMin="1",Units="cm")) float HalfHeight=70.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Knockback",meta=(ClampMin="0",Units="cm")) float KnockbackDistance=80.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Knockback",meta=(ClampMin="0.01",Units="s")) float KnockbackSeconds=.15f;
};

UENUM(BlueprintType)
enum class EDWCombatPhase : uint8 { None, Windup, Active, Recovery };

UENUM(BlueprintType)
enum class EDWCombatAnimationState : uint8 { Idle, Move, Alert, Throw, Transform, Dash, Melee, Attack, Death };
