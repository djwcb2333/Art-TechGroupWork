#pragma once
#include "CoreMinimal.h"
#include "DWEnemyNumericTuning.h"
#include "DWCombatNumericProvider.h"
#include "Engine/DataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DWPlayerCombatTuningSubsystem.generated.h"
class FDWCombatRules;class FJsonObject;

USTRUCT(BlueprintType)
struct GDATTEST_API FDWPlayerCombatNumericOverride
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bEnabled=false;
 /** Exact ID from the published allowlist. Units are the original JSON units. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FName ParameterId;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(EditCondition="bEnabled")) double Value=0.;
};
/** Complete authoritative numeric source. Legacy override fields are retained only for migration/serialization. */
UCLASS(BlueprintType)
class GDATTEST_API UDWPlayerCombatTuningSettings:public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,AdvancedDisplay,Category="统一战斗数值") int32 NumericSchema=0;
 UPROPERTY(EditAnywhere,Category="统一战斗数值") TArray<FDWCombatNumber> PlayerNumbers;
 UPROPERTY(EditAnywhere,Category="统一战斗数值") TArray<FDWCombatNumber> AbilityNumbers;
 UPROPERTY(EditAnywhere,Category="统一战斗数值") TArray<FDWEnemyNumbers> EnemyNumbers;
 UPROPERTY(EditAnywhere,Category="统一战斗数值") FDWSimpleSpearNumbers SimpleSpearNumbers;
 UFUNCTION(BlueprintCallable,Category="Combat Numeric Data") bool ValidateNumericSource(FString& Error)const;
 UFUNCTION(BlueprintCallable,Category="Combat Numeric Data") bool CoordinatePlayerTiming(FName Edited,double PreviousValue,FString& Notice);
 UPROPERTY(EditAnywhere,AdvancedDisplay,Category="兼容技术数据",meta=(DisplayName="敌人类型数值存储（具名界面管理）")) TArray<FDWEnemyTypeNumericTuning> EnemyTypes;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,AdvancedDisplay,Category="兼容技术数据",meta=(DisplayName="旧规则存储（界面直接映射，请勿手填ID）")) TArray<FDWPlayerCombatNumericOverride> RuleOverrides;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,AdvancedDisplay,Category="兼容技术数据",meta=(DisplayName="旧A01存储（界面直接映射，请勿手填ID）")) TArray<FDWPlayerCombatNumericOverride> A01Overrides;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,AdvancedDisplay,Category="兼容技术数据",meta=(DisplayName="简易矛伤害继承开关（具名界面管理）")) bool bOverrideSimpleSpearDamage=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Player Combat Tuning|Simple Spear",meta=(EditCondition="bOverrideSimpleSpearDamage",ClampMin="0")) double SimpleSpearDamage=25.;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Player Combat Tuning|Simple Spear") bool bOverrideSimpleSpearInterval=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Player Combat Tuning|Simple Spear",meta=(EditCondition="bOverrideSimpleSpearInterval",ClampMin="0.001",Units="s")) double SimpleSpearIntervalSeconds=1.;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Player Combat Tuning|Simple Spear") bool bOverrideSimpleSpearRange=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Player Combat Tuning|Simple Spear",meta=(EditCondition="bOverrideSimpleSpearRange",ClampMin="0.001",Units="cm")) double SimpleSpearRangeCm=100.;
};

UCLASS()
class GDATTEST_API UDWPlayerCombatTuningSubsystem:public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase&)override;
 const FDWEnemyNumbers* FindEnemyType(const UClass* ConcreteClass,EDWEnemyCombatProfile Profile)const;
 bool ResolveCombatRules(TSharedPtr<const FDWCombatRules>& Out,FString& Error)const;
 bool ResolveAbilityParameters(TSharedPtr<FJsonObject>& InOut,FString& Error)const;
 bool ResolveSimpleAttack(FDWSimpleSpearNumbers& Out,FString& Error)const;
 bool ApplyRules(TSharedPtr<const FDWCombatRules>& InOut,FString& Error)const;
 bool ApplyA01(TSharedPtr<FJsonObject>& InOut,FString& Error)const;
 bool ResolveSimpleSpear(double RawDamage,double RawInterval,double RawRange,double& Damage,double& Interval,double& Range,FString& Error)const;
 UFUNCTION(BlueprintPure,Category="Player Combat Tuning") bool IsConfigurationValid()const{return bValid;}
 UFUNCTION(BlueprintPure,Category="Player Combat Tuning") FString GetConfigurationError()const{return ConfigurationError;}
private:
 TSharedPtr<const FDWCombatNumericProvider> Provider;
 bool bValid=false;
 FString ConfigurationError;
};
