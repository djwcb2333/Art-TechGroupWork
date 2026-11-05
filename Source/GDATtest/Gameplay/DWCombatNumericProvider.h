#pragma once
#include "CoreMinimal.h"
#include "DWEnemyNumericTuning.h"
#include "DWCombatNumericProvider.generated.h"
class FDWCombatRules;class FJsonObject;class UDWPlayerCombatTuningSettings;
USTRUCT(BlueprintType)
struct GDATTEST_API FDWCombatNumber
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere) FName ParameterId;
 UPROPERTY(EditAnywhere) double Value=0.;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWEnemyNumber
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere) EDWEnemyNumericField Field=EDWEnemyNumericField::Damage;
 UPROPERTY(EditAnywhere) int32 VariantIndex=INDEX_NONE;
 UPROPERTY(EditAnywhere) FSoftObjectPath VariantAnimation;
 UPROPERTY(EditAnywhere) double Value=0.;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWEnemyNumbers
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere) FName TypeKey;
 UPROPERTY(EditAnywhere) FText DisplayName;
 UPROPERTY(EditAnywhere) FSoftClassPath TargetClass;
 UPROPERTY(EditAnywhere) EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::Legacy;
 UPROPERTY(EditAnywhere) bool bAnyProfile=false;
 UPROPERTY(EditAnywhere) bool bNeutral=false;
 UPROPERTY(EditAnywhere) TArray<FDWEnemyNumber> Values;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWSimpleSpearNumbers
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere) double Damage=25.;
 UPROPERTY(EditAnywhere) double IntervalSeconds=1.;
 UPROPERTY(EditAnywhere) double RangeCm=250.;
 UPROPERTY(EditAnywhere) double KnockbackCm=60.;
 UPROPERTY(EditAnywhere) double KnockbackSeconds=.2;
};
/** Pure immutable numeric source. Never owns health, gameplay actions, commits, inventory or saves. */
class GDATTEST_API IDWCombatNumericProvider
{
public:
 virtual ~IDWCombatNumericProvider()=default;
 virtual const TSharedPtr<const FDWCombatRules>& GetRules()const=0;
 virtual bool ProjectAbilities(TSharedPtr<FJsonObject>& InOut,FString& Error)const=0;
 virtual const FDWEnemyNumbers* FindEnemy(const TArray<FString>& Ancestry,EDWEnemyCombatProfile Profile)const=0;
 virtual const FDWSimpleSpearNumbers& GetSimpleSpear()const=0;
};
class GDATTEST_API FDWCombatNumericProvider final:public IDWCombatNumericProvider
{
public:
 static bool Build(const UDWPlayerCombatTuningSettings& Settings,const TSharedPtr<const FDWCombatRules>& Base,TSharedPtr<const FDWCombatNumericProvider>& Out,FString& Error);
 static bool ProjectEnemy(const FDWEnemyNumbers& Numbers,FDWEnemyCombatConfig& Out,FDWEnemyActorNumericSupplement& Extra,FString& Error);
 // OpeningLegacy is supplied only by the actor's private, pre-BeginPlay director binding.
 static bool ResolveEnemySource(const FDWEnemyNumbers* ClassNumbers,EDWEnemyCombatProfile Requested,const FDWEnemyNumbers* OpeningLegacy,const FDWEnemyNumbers*& Out,FString& Error);
 const TSharedPtr<const FDWCombatRules>& GetRules()const override{return Rules;}
 bool ProjectAbilities(TSharedPtr<FJsonObject>& InOut,FString& Error)const override;
 const FDWEnemyNumbers* FindEnemy(const TArray<FString>& Ancestry,EDWEnemyCombatProfile Profile)const override;
 const FDWSimpleSpearNumbers& GetSimpleSpear()const override{return Simple;}
private:
 TSharedPtr<const FDWCombatRules> Rules;
 TMap<FName,double> Abilities;
 TArray<FDWEnemyNumbers> Enemies;
 FDWSimpleSpearNumbers Simple;
};
namespace DWCombatNumericPolicy
{
 // Edited period scales existing phases proportionally, preserving a larger configured gap.
 // Edited phase keeps that value and raises period only if necessary. Other fields never normalize.
 GDATTEST_API bool CoordinateTiming(TArray<FDWCombatNumber>& Numbers,FName Edited,double PreviousValue,FString& Notice);
 GDATTEST_API bool ValidateSimple(const FDWSimpleSpearNumbers& Numbers);
}
