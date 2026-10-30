#pragma once
#include "CoreMinimal.h"
#include "DWCombatAnimInstance.h"
#include "DWSourEatPhases.h"
#include "DWSourDeathAnimationObservation.h"
#include "DWSourDoughAnimInstance.generated.h"
struct FAnimNode_AssetPlayerRelevancyBase;
class ADWSourDoughEnemy;
class USkeletalMeshComponent;
UENUM(BlueprintType)
enum class EDWSourVisualState : uint8 { Idle, Move, Alert, Attack, Death, EatEnter, EatLoop, EatExit };
UCLASS(Blueprintable,BlueprintType)
class GDATTEST_API UDWSourDoughAnimInstance : public UDWCombatAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    virtual void NativePostEvaluateAnimation() override;
    virtual void NativeUninitializeAnimation() override;
    UFUNCTION(BlueprintPure,Category="Sour Dough|Animation|Diagnostics") FName GetSourGraphStateName() const;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sour Dough|Animation") TObjectPtr<UAnimSequenceBase> EatEnterSequence;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sour Dough|Animation") TObjectPtr<UAnimSequenceBase> EatLoopSequence;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sour Dough|Animation") TObjectPtr<UAnimSequenceBase> EatExitSequence;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Sour Dough|Animation") EDWSourVisualState SourState=EDWSourVisualState::Idle;
private:
    FDWSourEatPhases Eat;
    // Super has no formal authored attack; retain our sampled pose during graph blend-out.
    float SourAttackPositionCache=0.f;
    void ResetDeathObservation();
    bool DeliverDeathAnimationCompleted();
    DWSourDeathAnimationObservation::FTracker DeathObservation;
    TWeakObjectPtr<ADWSourDoughEnemy> DeathObservedOwner;
    TWeakObjectPtr<USkeletalMeshComponent> DeathObservedMesh;
    TWeakObjectPtr<UAnimSequenceBase> DeathObservedSequence;
    const FAnimNode_AssetPlayerRelevancyBase* DeathObservedPlayer=nullptr; // identity only; never dereference after graph changes
    uint64 DeathObservedGeneration=0, DeathObservationSerial=0;
    uint64 DeathQueuedGeneration=0, DeathQueuedSerial=0, DeathQueuedFrame=0;
    bool bDeathNotifyQueued=false,bDeathNotifyDelivered=false;
};
