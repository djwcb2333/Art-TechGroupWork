#pragma once
#include "CoreMinimal.h"
#include "DWEnemyCombatProfile.h"
#include "UObject/SoftObjectPath.h"
#include "DWEnemyNumericTuning.generated.h"

UENUM(BlueprintType)
enum class EDWEnemyNumericField:uint8
{
 MaxHealth UMETA(DisplayName="最大生命 / Max Health"),
 MoveSpeed UMETA(DisplayName="移动速度 / Move Speed"),
 Damage UMETA(DisplayName="攻击伤害 / Damage"),
 Windup UMETA(DisplayName="攻击前摇 / Windup"),
 Active UMETA(DisplayName="有效期 / Active"),
 Recovery UMETA(DisplayName="攻击后摇 / Recovery"),
 ExtraCooldown UMETA(DisplayName="额外冷却 / Extra Cooldown"),
 Interval UMETA(DisplayName="攻击周期 / Interval"),
 Range UMETA(DisplayName="判定范围 / Range"),
 FullAngle UMETA(DisplayName="完整判定角度 / Full Angle"),
 Height UMETA(DisplayName="判定高度 / Height"),
 IncomingKnockbackMultiplier UMETA(DisplayName="受到击退倍率 / Incoming Knockback"),
 AttackKnockbackMultiplier UMETA(DisplayName="攻击击退倍率 / Attack Knockback"),
 OutgoingKnockback UMETA(DisplayName="造成击退距离 / Outgoing Knockback"),
 OutgoingKnockbackSeconds UMETA(DisplayName="造成击退时长 / Knockback Duration"),
 AlertRadius UMETA(DisplayName="警觉半径 / Alert Radius"),
 DisengageDistance UMETA(DisplayName="脱战距离 / Disengage Distance"),
 AlertSeconds UMETA(DisplayName="警觉时长 / Alert Duration"),
 DisengageSeconds UMETA(DisplayName="脱战等待 / Disengage Delay"),
 WindupLockFraction UMETA(DisplayName="前摇方向锁定比例 / Facing Lock"),
 ReturnSpeedMultiplier UMETA(DisplayName="返程速度倍率 / Return Speed"),
 PatrolRadius UMETA(DisplayName="巡逻半径 / Patrol Radius"),
 PatrolWaitSeconds UMETA(DisplayName="巡逻等待 / Patrol Wait"),
 DeathCleanupSeconds UMETA(DisplayName="死亡清理延迟 / Death Cleanup"),
 VariantWeight UMETA(DisplayName="变招选择权重 / Selection Weight")
};

/** Compact typed storage. Normal users edit named native Details controls, never these IDs. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWEnemyNumericOverride
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,Category="兼容技术数据") EDWEnemyNumericField Field=EDWEnemyNumericField::Damage;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") int32 VariantIndex=INDEX_NONE;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") FSoftObjectPath VariantAnimation;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") bool bEnabled=false;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") double Value=0.;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWEnemyTypeNumericTuning
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,Category="兼容技术数据") FName TypeKey;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") FText DisplayName;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") FSoftClassPath TargetClass;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::Legacy;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") bool bAnyProfile=false;
 UPROPERTY(EditAnywhere,Category="兼容技术数据") TArray<FDWEnemyNumericOverride> Values;
};
struct FDWEnemyActorNumericSupplement
{
 TMap<EDWEnemyNumericField,double> Values;
};
namespace DWEnemyNumericPolicy
{
 GDATTEST_API const FDWEnemyTypeNumericTuning* Select(const TArray<FDWEnemyTypeNumericTuning>& Types,const TArray<FString>& ClassAncestry,EDWEnemyCombatProfile Profile);
 GDATTEST_API bool ValidateValue(EDWEnemyNumericField Field,double Value);
 GDATTEST_API bool LegacyBounds(EDWEnemyNumericField Field,double& Minimum,double& Maximum);
 GDATTEST_API bool ValidateLegacyValue(EDWEnemyNumericField Field,double Value);
 GDATTEST_API bool ValidateTypes(const TArray<FDWEnemyTypeNumericTuning>& Types,FString& Error);
 GDATTEST_API bool ApplyFormal(const FDWEnemyTypeNumericTuning* Type,const FDWEnemyCombatConfig& Base,FDWEnemyCombatConfig& Out,FDWEnemyActorNumericSupplement& Extra,FString& Error);
 GDATTEST_API bool IsSupplement(EDWEnemyNumericField Field);
 GDATTEST_API bool ValidateLegacyProjection(float ExpectedMax,float ExpectedSpeed,float SavedMax,float ActualMax,float ActualSpeed);
}
