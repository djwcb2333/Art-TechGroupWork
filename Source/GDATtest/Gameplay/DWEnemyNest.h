#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWEnemyNest.generated.h"

class ADWEnemyCharacter;
class USphereComponent;
class UStaticMeshComponent;

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
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Nest|Spawning") bool bPlayerInRange = false;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Nest|Spawning") float SpawnProgress = 0.f;

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
    void DeactivateNest(bool bFromSave = false);
    void SetPlayerInRange(bool bNewInRange, bool bNotify = true);
    TArray<TWeakObjectPtr<ADWEnemyCharacter>> SpawnedEnemies;
    bool bHealthRestoredFromSave = false;
    bool bSpawningEnemy = false;
};
