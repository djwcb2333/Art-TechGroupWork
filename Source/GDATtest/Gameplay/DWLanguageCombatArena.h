#pragma once
#include "CoreMinimal.h"
#include "DWLanguageCarrier.h"
#include "DWLanguageCombatArena.generated.h"
class ADWEnemyCharacter;
struct FDWCombatWorldObjectsSave;
/** Optional isolated training. Leaving explicitly restores the complete pre-round memory snapshot. */
UCLASS() class GDATTEST_API ADWLanguageCombatArena:public ADWLanguageCarrier
{
 GENERATED_BODY()
public:
 ADWLanguageCombatArena();
 virtual void Tick(float DeltaSeconds)override;
 UPROPERTY(EditAnywhere,Category="Arena") TSubclassOf<ADWEnemyCharacter> EnemyClass;
 UPROPERTY(EditAnywhere,Category="Arena") TObjectPtr<AActor> EntranceGateActor;
 UPROPERTY(EditAnywhere,Category="Arena") FVector PlayerInside=FVector(8500,0,100);
 UPROPERTY(EditAnywhere,Category="Arena") FVector EnemySpawn=FVector(10500,0,100);
 UPROPERTY(EditAnywhere,Category="Arena") FVector InsideConsole=FVector(8500,-800,30);
 UFUNCTION(BlueprintPure) bool IsTrainingActive()const{return bActive;}
 bool InteractTraining(ADWPlayerCharacter*,FText& Message);
protected:
 virtual void BeginPlay()override;
private:
 friend class FDWCombatWorldObjectsPersistence;
 friend class FDWCombatWorldObjectsRestorePlan;
 // Native-only permission, valid only inside this arena's exact entry-snapshot restore.
 bool CanRetireRoundEnemy(const ADWEnemyCharacter&,const ADWPlayerCharacter&,const FDWCombatWorldObjectsSave&)const;
 TArray<uint8> BeforeRound,EntryWorldObjects;
 TWeakObjectPtr<ADWEnemyCharacter> RoundEnemy;
 TWeakObjectPtr<ADWPlayerCharacter> RoundPlayer;
 FGuid ActiveRoundId;
 FTransform OutsideConsole;
 int32 NextProfile=0;
 bool bActive=false,bChanging=false,bRestoringRound=false;
 uint64 RoundEpoch=0;
 double NextRecoveryAttempt=0.;
 bool RestoreRound(ADWPlayerCharacter*,FText& Message);
 void SetEntranceClosed(bool Closed);
};
