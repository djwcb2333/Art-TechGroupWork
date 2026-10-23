#include "DWCombatAnimInstance.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWGameplayConfig.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

namespace DWCombatAnimation
{
    float SafePositive(float Value, float Fallback=1.f)
    {
        return FMath::IsFinite(Value) && Value>0.f ? Value : Fallback;
    }

    float RateForDuration(const UAnimSequenceBase* Sequence, float Duration)
    {
        if (!Sequence) return 1.f;
        const float SourceRate=SafePositive(FMath::Abs(Sequence->RateScale));
        const float Rate=Sequence->GetPlayLength()/(SafePositive(Duration,.05f)*SourceRate);
        return (Sequence->RateScale<0.f ? -1.f : 1.f)*FMath::Max(.001f,Rate);
    }
}

void UDWCombatAnimInstance::ResetInputs()
{
    AnimationState=EDWCombatAnimationState::Idle;
    GroundSpeed=0.f;
    bEnemyOwner=false;
    bUsingFallbackSequence=false;
    IdleSequence=CompatibleSequence(PreviewIdleSequence);
    MoveSequence=AlertSequence=ThrowSequence=TransformSequence=DashSequence=MeleeSequence=AttackSequence=DeathSequence=IdleSequence;
    MovePlayRate=TransformPlayRate=MeleePlayRate=AttackPlayRate=1.f;
    ThrowPositionSeconds=MeleePositionSeconds=AttackPositionSeconds=0.f;
}

void UDWCombatAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    ResetInputs();
}

FName UDWCombatAnimInstance::GetCombatGraphStateName() const
{
    const int32 MachineIndex=GetStateMachineIndex(bEnemyOwner ? TEXT("EnemyCombatStates") : TEXT("PlayerCombatStates"));
    return MachineIndex!=INDEX_NONE ? GetCurrentStateName(MachineIndex) : NAME_None;
}

UAnimSequenceBase* UDWCombatAnimInstance::CompatibleSequence(UAnimationAsset* Candidate) const
{
    UAnimSequenceBase* Sequence=Cast<UAnimSequenceBase>(Candidate);
    // A Montage is also a SequenceBase, but cannot be played by these sequence nodes.
    if (!Sequence || Sequence->IsA<UAnimMontage>()) return nullptr;
    const USkeletalMeshComponent* MeshComponent=GetSkelMeshComponent();
    const USkeletalMesh* Mesh=MeshComponent ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
    const USkeleton* TargetSkeleton=Mesh ? Mesh->GetSkeleton() : nullptr;
    const USkeleton* SourceSkeleton=Sequence->GetSkeleton();
    if (!TargetSkeleton || !SourceSkeleton) return nullptr;
    if (TargetSkeleton==SourceSkeleton) return Sequence;
    // Compatible-skeleton declarations are directional from target to source.
    for (const TSoftObjectPtr<USkeleton>& Compatible : TargetSkeleton->GetCompatibleSkeletons())
    {
        if (Compatible.ToSoftObjectPath()==FSoftObjectPath(SourceSkeleton)) return Sequence;
    }
    return nullptr;
}

UAnimSequenceBase* UDWCombatAnimInstance::ResolveSequence(UAnimationAsset* Candidate, UAnimSequenceBase* Fallback) const
{
    if (UAnimSequenceBase* Sequence=CompatibleSequence(Candidate)) return Sequence;
    return Fallback;
}

void UDWCombatAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    // Owner access stays on the game thread; the graph reads only these copied inputs.
    APawn* Pawn=TryGetPawnOwner();
    if (!Pawn) { ResetInputs(); return; }
    GroundSpeed=Pawn->GetVelocity().Size2D();
    UAnimationAsset* RequestedSequence=nullptr;
    if (const ADWPlayerCharacter* Player=Cast<ADWPlayerCharacter>(Pawn))
    {
        bEnemyOwner=false;
        AnimationState=Player->GetCombatAnimationState();
        IdleSequence=ResolveSequence(Player->IdleAnimation,CompatibleSequence(PreviewIdleSequence));
        MoveSequence=ResolveSequence(Player->WalkAnimation,IdleSequence);
        ThrowSequence=ResolveSequence(Player->GetCombatThrowSequence(),IdleSequence);
        TransformSequence=ResolveSequence(Player->TransformAnimation,IdleSequence);
        DashSequence=ResolveSequence(Player->DashAnimation,IdleSequence);
        UAnimationAsset* CurrentMeleeAnimation=Player->GetCurrentMeleeAnimation();
        MeleeSequence=ResolveSequence(CurrentMeleeAnimation,IdleSequence);
        DeathSequence=ResolveSequence(Player->DeathAnimation,IdleSequence);
        AlertSequence=AttackSequence=IdleSequence;
        MovePlayRate=Player->bSprinting && Player->GameplayConfig
            ? DWCombatAnimation::SafePositive(Player->GameplayConfig->SprintSpeedMultiplier) : 1.f;
        // Match the previous player transformation playback convention.
        TransformPlayRate=TransformSequence && CompatibleSequence(Player->TransformAnimation)
            ? TransformSequence->GetPlayLength()/FMath::Max(.05f,Player->TransformAnimationSeconds) : 1.f;
        MeleePlayRate=CompatibleSequence(CurrentMeleeAnimation)
            ? DWCombatAnimation::RateForDuration(MeleeSequence,Player->GetMeleeAnimationDuration()) : 1.f;
        if (!MeleeSequence || !CompatibleSequence(CurrentMeleeAnimation)) MeleePositionSeconds=0.f;
        else
            MeleePositionSeconds=MeleeSequence->GetPlayLength()*FMath::Clamp(Player->GetMeleeAnimationElapsedSeconds()/FMath::Max(.001f,Player->GetMeleeAnimationDuration()),0.f,1.f);
        // The character freezes normal completion at the exact end, or cancellation
        // at its current pose, through the Idle/Move blend. A new strike resets it.
        AttackPositionSeconds=0.f;
        AttackPlayRate=1.f;
        const UAnimSequenceBase* SampledThrow=CompatibleSequence(Player->GetCombatThrowSequence());
        if (SampledThrow)
        {
            // Match the character's sampled throw clock, including its frozen/invalid source-rate fallback.
            const float SourceRate=FMath::IsFinite(SampledThrow->RateScale) && !FMath::IsNearlyZero(SampledThrow->RateScale)
                ? SampledThrow->RateScale : 1.f;
            const float EffectiveRate=FMath::Abs(Player->GetCombatThrowPlayRate()*SourceRate);
            const float Start=FMath::Clamp(Player->GetCombatThrowStartSeconds(),0.f,SampledThrow->GetPlayLength());
            const float End=FMath::Clamp(Player->GetCombatThrowEndSeconds(),Start,SampledThrow->GetPlayLength());
            ThrowPositionSeconds=FMath::Clamp(Start+FMath::Max(0.f,Player->GetCombatThrowElapsedSeconds())*EffectiveRate,Start,End);
        }
        else ThrowPositionSeconds=0.f;
        switch (AnimationState)
        {
            case EDWCombatAnimationState::Move: RequestedSequence=Player->WalkAnimation; break;
            case EDWCombatAnimationState::Throw: RequestedSequence=Player->GetCombatThrowSequence(); break;
            case EDWCombatAnimationState::Transform: RequestedSequence=Player->TransformAnimation; break;
            case EDWCombatAnimationState::Dash: RequestedSequence=Player->DashAnimation; break;
            case EDWCombatAnimationState::Melee: RequestedSequence=CurrentMeleeAnimation; break;
            case EDWCombatAnimationState::Death: RequestedSequence=Player->DeathAnimation; break;
            default: RequestedSequence=Player->IdleAnimation; break;
        }
    }
    else if (const ADWEnemyCharacter* Enemy=Cast<ADWEnemyCharacter>(Pawn))
    {
        bEnemyOwner=true;
        AnimationState=Enemy->GetCombatAnimationState();
        IdleSequence=ResolveSequence(Enemy->IdleAnimation,CompatibleSequence(PreviewIdleSequence));
        MoveSequence=ResolveSequence(Enemy->MoveAnimation,IdleSequence);
        AlertSequence=ResolveSequence(Enemy->AlertAnimation,IdleSequence);
        UAnimationAsset* CurrentEnemyAttackAnimation=Enemy->GetCurrentAttackAnimation();
        AttackSequence=ResolveSequence(CurrentEnemyAttackAnimation,IdleSequence);
        DeathSequence=ResolveSequence(Enemy->DeathAnimation,IdleSequence);
        ThrowSequence=TransformSequence=DashSequence=MeleeSequence=IdleSequence;
        MovePlayRate=FMath::IsFinite(Enemy->MoveAnimationReferenceSpeed)&&Enemy->MoveAnimationReferenceSpeed>0.f
            ? FMath::Clamp(GroundSpeed/Enemy->MoveAnimationReferenceSpeed,.1f,3.f) : 1.f;
        TransformPlayRate=MeleePlayRate=AttackPlayRate=1.f;
        AttackPlayRate=CompatibleSequence(CurrentEnemyAttackAnimation)
            ? DWCombatAnimation::RateForDuration(AttackSequence,Enemy->GetCombatAttackDuration()) : 1.f;
        if (!AttackSequence || !CompatibleSequence(CurrentEnemyAttackAnimation)) AttackPositionSeconds=0.f;
        else if (Enemy->AttackPhase!=EDWCombatPhase::None)
            AttackPositionSeconds=AttackSequence->GetPlayLength()*FMath::Clamp(Enemy->GetCombatAttackElapsedSeconds()/FMath::Max(.001f,Enemy->GetCombatAttackDuration()),0.f,1.f);
        // Keep the last sampled pose while this state's remaining blend weight fades out.
        ThrowPositionSeconds=MeleePositionSeconds=0.f;
        switch (AnimationState)
        {
            case EDWCombatAnimationState::Move: RequestedSequence=Enemy->MoveAnimation; break;
            case EDWCombatAnimationState::Alert: RequestedSequence=Enemy->AlertAnimation; break;
            case EDWCombatAnimationState::Attack: RequestedSequence=CurrentEnemyAttackAnimation; break;
            case EDWCombatAnimationState::Death: RequestedSequence=Enemy->DeathAnimation; break;
            default: RequestedSequence=Enemy->IdleAnimation; break;
        }
    }
    else { ResetInputs(); return; }
    bUsingFallbackSequence=CompatibleSequence(RequestedSequence)==nullptr;
}
