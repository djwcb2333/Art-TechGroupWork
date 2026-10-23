#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWEnemyNest.generated.h"

class ADWEnemyCharacter;
class USphereComponent;
class UStaticMeshComponent;

/** A weighted enemy type. A zero type cap means unlimited, as on the whole nest. */
USTRUCT(BlueprintType)
struct FDWEnemySpawnEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawning") TSubclassOf<ADWEnemyCharacter> EnemyClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawning", meta=(ClampMin="0")) float Weight = 1.f;
    /** Counts this exact class, not subclasses. Duplicate rows share the strictest positive cap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawning", meta=(ClampMin="0")) int32 MaxAliveOfType = 0;
};

/** A destructible nest which spawns enemies while a player is inside its range. */
UCLASS(Blueprintable)
class GDATTEST_API ADWEnemyNest : public AActor
{
    GENERATED_BODY()
public:
    ADWEnemyNest();
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Nest|Components") TObjectPtr<UStaticMeshComponent> NestMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Nest|Components") TObjectPtr<USphereComponent> AggroRange;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Save") FName PersistentId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Health", meta=(ClampMin="1")) float MaxHealth = 500.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Nest|Health") float Health = 500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="1", Units="cm")) float AggroRadius = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="0.1", Units="s")) float SpawnInterval = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="0", Units="cm")) float SpawnRadius = 220.f;
    /** Zero means no cap. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="0")) int32 MaxAliveEnemies = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning") TSubclassOf<ADWEnemyCharacter> EnemyClass;
    /** Empty keeps the original EnemyClass behavior. A configured but exhausted/invalid pool does not fall back. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(TitleProperty="EnemyClass")) TArray<FDWEnemySpawnEntry> WeightedEnemies;
    /** Invisible, indestructible scene spawning area. It still owns spawned enemies and participates in saves. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning") bool bAmbientSpawner = false;
    /** Off keeps the original fixed-radius ring. Ambient areas always use the range below. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning") bool bUseSpawnRadiusRange = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="0", Units="cm")) float MinSpawnRadius = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="0", Units="cm")) float MaxSpawnRadius = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning") bool bUseRandomSpawnInterval = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="0.1", Units="s")) float MinSpawnInterval = 12.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="0.1", Units="s")) float MaxSpawnInterval = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="0", Units="cm")) float MinimumPlayerSpawnDistance = 0.f;
    /** Ambient areas always require reachable navigation and solid ground, even if this is false. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning") bool bRequireNavigableSpawn = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Nest|Spawning", meta=(ClampMin="1", ClampMax="128")) int32 SpawnAttemptCount = 8;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Nest|Spawning") bool bPlayerInRange = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Nest|Spawning") float SpawnProgress = 0.f;
    /** Current sampled interval; it is resampled on load, while SpawnProgress retains its saved seconds. */
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Nest|Spawning") float CurrentSpawnInterval = 0.f;

    void RegisterRestoredEnemy(ADWEnemyCharacter* Enemy){SpawnedEnemies.AddUnique(Enemy);}
    UFUNCTION(BlueprintPure, Category="Nest") bool IsAlive() const { return Health > 0.f; }
    UFUNCTION(BlueprintCallable, Category="Nest|Save") void SetHealthForLoad(float NewHealth);
    UFUNCTION(BlueprintCallable, Category="Nest|Spawning") ADWEnemyCharacter* SpawnEnemy();
    /** Extra designer eligibility rule; dead nests, invalid classes and hard caps remain blocked. */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Nest|Rules") bool CanSpawnEnemy() const;
    virtual bool CanSpawnEnemy_Implementation() const;
    UFUNCTION(BlueprintImplementableEvent, Category="Nest|Events") void OnEnemySpawned(ADWEnemyCharacter* SpawnedEnemy);
    UFUNCTION(BlueprintImplementableEvent, Category="Nest|Events") void OnPlayerRangeChanged(bool bNowInRange);
    UFUNCTION(BlueprintImplementableEvent, Category="Nest|Effects") void OnNestDestroyed();
protected:
    virtual void BeginPlay() override;
private:
    friend class FDWCombatWorldObjectsPersistence;
    friend class UDWCombatAuthoritySubsystem;
    void DeactivateNest(bool bFromSave = false);
    void SetPlayerInRange(bool bNewInRange, bool bNotify = true);
    void UpdateAmbientPresentation();
    void SampleNextSpawnInterval();
    TArray<TWeakObjectPtr<ADWEnemyCharacter>> SpawnedEnemies;
    bool bHealthRestoredFromSave = false;
    bool bSpawningEnemy = false;
    bool bCombatDeathTransition = false;
    bool bCombatDeathEffectsPending = false;
    bool bCombatDeathRangeNotice = false;
    void CommitCombatDeathFacts();
    void PublishCombatDeathEffects(bool bFromSave = false);
    // Retain the authored presentation across map saves so switching this mode off is reversible.
    UPROPERTY() bool bAmbientPresentationApplied = false;
    UPROPERTY() bool bOriginalNestVisible = true;
    UPROPERTY() bool bOriginalNestHiddenInGame = false;
    UPROPERTY() bool bOriginalCanBeDamaged = true;
    UPROPERTY() TEnumAsByte<ECollisionEnabled::Type> OriginalNestCollision = ECollisionEnabled::QueryAndPhysics;
};

/** Deterministic rules shared by actual spawning and focused native tests. They never touch a world. */
namespace DWEnemySpawnRules
{
    GDATTEST_API UClass* SelectWeightedClass(const TArray<FDWEnemySpawnEntry>& Entries,
        const TMap<UClass*, int32>& AliveCounts, double UnitSample);
    GDATTEST_API float SampleInterval(bool bRandom, float FixedSeconds, float MinimumSeconds, float MaximumSeconds, float UnitSample);
    GDATTEST_API FVector2D RadiusRange(bool bUseRange, float FixedRadius, float MinimumRadius, float MaximumRadius);
}
