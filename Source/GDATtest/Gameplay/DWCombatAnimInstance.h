#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "DWCombatTypes.h"
#include "DWCombatAnimInstance.generated.h"

class UAnimationAsset;
class UAnimSequenceBase;

/** Shared, game-thread animation inputs for the editable player/enemy state machines. */
UCLASS(Blueprintable, BlueprintType)
class GDATTEST_API UDWCombatAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
    /** Read the actually evaluated state machine, rather than merely the requested gameplay state. */
    UFUNCTION(BlueprintPure, Category="DoughWorld|Combat Animation|Diagnostics")
    FName GetCombatGraphStateName() const;

    /** Preview/fallback only. Runtime slots are owned by the character, not copied into the AnimBP. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="DoughWorld|Combat Animation")
    TObjectPtr<UAnimSequenceBase> PreviewIdleSequence;

    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation")
    EDWCombatAnimationState AnimationState=EDWCombatAnimationState::Idle;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation") float GroundSpeed=0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation") bool bEnemyOwner=false;
    /** A missing or incompatible slot is replaced with compatible Idle; an empty Idle uses reference pose. */
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation") bool bUsingFallbackSequence=false;

    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> IdleSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> MoveSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> AlertSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> ThrowSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> TransformSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> DashSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> MeleeSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> AttackSequence;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Sequences") TObjectPtr<UAnimSequenceBase> DeathSequence;

    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Playback") float MovePlayRate=1.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Playback") float TransformPlayRate=1.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Playback") float MeleePlayRate=1.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Playback") float AttackPlayRate=1.f;
    /** Source-clip position for Throw's Sequence Evaluator; restarts even for consecutive throws in the same state. */
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Playback") float ThrowPositionSeconds=0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Playback") float MeleePositionSeconds=0.f;
    UPROPERTY(Transient, BlueprintReadOnly, Category="DoughWorld|Combat Animation|Playback") float AttackPositionSeconds=0.f;

private:
    UAnimSequenceBase* CompatibleSequence(UAnimationAsset* Candidate) const;
    UAnimSequenceBase* ResolveSequence(UAnimationAsset* Candidate, UAnimSequenceBase* Fallback) const;
    void ResetInputs();
};
