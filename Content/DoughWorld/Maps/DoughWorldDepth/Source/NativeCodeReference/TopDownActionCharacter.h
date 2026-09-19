#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "TopDownActionCharacter.generated.h"

/** Movement actions for BP_TopDownCharacter; keeps its existing mesh and camera. */
UCLASS(Blueprintable)
class GDATTEST_API ATopDownActionCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ATopDownActionCharacter();
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Player Actions|Jump", meta=(ClampMin="1"))
    float JumpLaunchSpeed = 600.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Player Actions|Roll", meta=(ClampMin="1", Units="cm"))
    float RollDistance = 400.f;

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Player Actions|Roll", meta=(ClampMin="0.1", Units="s"))
    float RollDuration = 0.45f;

    /** Time after a roll ends before another roll can start. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Player Actions|Roll", meta=(ClampMin="0", Units="s"))
    float RollCooldown = 0.35f;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Player Actions|Roll")
    bool bIsRolling = false;

    UFUNCTION(BlueprintCallable, Category="Player Actions")
    void StartActionJump();

    UFUNCTION(BlueprintCallable, Category="Player Actions")
    void StartForwardRoll();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
        bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

private:
    void FinishRoll();
    float RollElapsed = 0.f;
    float ActiveRollDuration = 0.45f;
    float NextRollTime = 0.f;
    uint16 RollMotionId = 0;
    FVector RollDirection = FVector::ForwardVector;
    FTransform MeshRestTransform;
    bool bPreviousOrientToMovement = true;
    bool bPreviousPauseAnims = false;
    TWeakObjectPtr<AController> LockedController;
};
