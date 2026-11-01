#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DWEnemyCombatProfile.h"
#include "DWCreatureDefinition.generated.h"
class UAnimationAsset;
class UStaticMesh;
UENUM(BlueprintType)
enum class EDWCreatureDisposition : uint8 { InheritExisting, AlwaysHostile, Neutral };
UENUM(BlueprintType)
enum class EDWCreatureDropPolicy : uint8 { ExistingFormalCore, ConfiguredFormalCore, None, ConfiguredItems };
UENUM(BlueprintType)
enum class EDWCreatureNumericSource : uint8 { ExistingFormalProfile, DefinitionLegacyHost };
USTRUCT(BlueprintType)
struct FDWCreatureDropEntry
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FName ItemId;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0",ClampMax="1")) float Probability=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="1",ClampMax="10000")) int32 MinimumCount=1;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="1",ClampMax="10000")) int32 MaximumCount=1;
};
/** Optional definition; never creates a second health, damage or save authority. */
UCLASS(BlueprintType)
class GDATTEST_API UDWCreatureDefinition : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature") EDWCreatureNumericSource NumericSource=EDWCreatureNumericSource::DefinitionLegacyHost;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature") EDWEnemyCombatProfile FormalProfile=EDWEnemyCombatProfile::Legacy;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature") EDWCreatureDisposition Disposition=EDWCreatureDisposition::Neutral;
 /** Single fatal receipt pipeline. Configured cores retain profile-specific item identity and durable claim records. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Drop") EDWCreatureDropPolicy DropPolicy=EDWCreatureDropPolicy::None;
 /** At most 16 independently sampled rows, total maximum count <= 10000. Frozen on the fatal commit. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Drop") TArray<FDWCreatureDropEntry> ItemDrops;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Drop",meta=(ClampMin="0",ClampMax="1")) float CoreDropProbability=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Drop",meta=(ClampMin="1")) int32 CoreDropCount=1;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Legacy",meta=(ClampMin="1")) float MaxHealth=100.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Legacy",meta=(ClampMin="0")) float MoveSpeed=180.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Legacy",meta=(ClampMin="0")) float Damage=8.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Legacy",meta=(ClampMin="1")) float AttackRange=150.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Legacy",meta=(ClampMin="0")) float Windup=.55f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Legacy",meta=(ClampMin="0.01")) float Active=.12f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Legacy",meta=(ClampMin="0")) float Recovery=.4f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Legacy",meta=(ClampMin="0.05")) float AttackInterval=1.2f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Patrol",meta=(ClampMin="0")) float PatrolRadius=400.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Patrol",meta=(ClampMin="0")) float PatrolWaitSeconds=2.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Patrol",meta=(ClampMin="0.1",Units="s")) float AmbientGoalTimeoutSeconds=12.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Patrol",meta=(ClampMin="1",Units="cm")) float AmbientArrivalRadiusCm=80.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Patrol",meta=(ClampMin="0.1",Units="s")) float AmbientFailureWaitSeconds=2.f;
 /** External component owns goal selection; host owns the single navigation command. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Patrol") bool bExternalAmbientPatrol=true;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Presentation") TSoftObjectPtr<UStaticMesh> PlaceholderMesh;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Presentation") TSoftObjectPtr<UAnimationAsset> IdleAnimation;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Presentation") TSoftObjectPtr<UAnimationAsset> MoveAnimation;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Presentation") TSoftObjectPtr<UAnimationAsset> AlertAnimation;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Presentation") TSoftObjectPtr<UAnimationAsset> AttackAnimation;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Creature|Presentation") TSoftObjectPtr<UAnimationAsset> DeathAnimation;
};
USTRUCT(BlueprintType)
struct FDWCreatureSnapshot
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) float Health=0;
 UPROPERTY(BlueprintReadOnly) float MaxHealth=0;
 UPROPERTY(BlueprintReadOnly) float EffectiveMoveSpeed=0;
 UPROPERTY(BlueprintReadOnly) bool bAlive=false;
 UPROPERTY(BlueprintReadOnly) uint8 State=0;
 UPROPERTY(BlueprintReadOnly) float AttackDamage=0;
 UPROPERTY(BlueprintReadOnly) float AttackRange=0;
 UPROPERTY(BlueprintReadOnly) float WindupSeconds=0;
 UPROPERTY(BlueprintReadOnly) float ActiveSeconds=0;
 UPROPERTY(BlueprintReadOnly) float RecoverySeconds=0;
 UPROPERTY(BlueprintReadOnly) float AttackIntervalSeconds=0;
 UPROPERTY(BlueprintReadOnly) EDWCreatureDropPolicy DropPolicy=EDWCreatureDropPolicy::ExistingFormalCore;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<UDWCreatureDefinition> Definition;
 UPROPERTY(BlueprintReadOnly) EDWCreatureDisposition Disposition=EDWCreatureDisposition::InheritExisting;
};
USTRUCT(BlueprintType)
struct FDWAmbientPatrolState
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) bool bNeedsGoal=false;
 UPROPERTY(BlueprintReadOnly) bool bHasGoal=false;
 UPROPERTY(BlueprintReadOnly) FVector Goal=FVector::ZeroVector;
 UPROPERTY(BlueprintReadOnly) float RemainingWaitSeconds=0;
};

namespace DWCreaturePersistencePolicy
{
 /** Class defaults are the supported definition identity. No-definition Legacy keeps its old numeric contract. */
 GDATTEST_API bool ValidateSource(const UDWCreatureDefinition* InstanceDefinition,const UDWCreatureDefinition* ClassDefinition,
  EDWEnemyCombatProfile HostProfile,float SavedMaxHealth,float AppliedMaxHealth,float AppliedBaseMoveSpeed,FString& Error);
}
