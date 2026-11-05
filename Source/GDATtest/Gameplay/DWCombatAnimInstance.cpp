#include "DWCombatAnimInstance.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGatheringComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "Engine/World.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimationInstanceScope.h"
#include "Animation/AnimNotifies/AnimNotify_PlayParticleEffect.h"
#include "Animation/AnimNotifies/AnimNotify_PlaySound.h"
#include "Animation/AnimNotifies/AnimNotifyState_TimedParticleEffect.h"
#include "AnimNotify_PlayNiagaraEffect.h"
#include "AnimNotifyState_TimedNiagaraEffect.h"
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
    ThrowPositionSeconds=MeleePositionSeconds=AttackPositionSeconds=AlertPositionSeconds=0.f;
}

void UDWCombatAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    ResetInputs();
    ResetNativeFxClock();
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
        AlertPositionSeconds=0.f;
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
        AlertPositionSeconds=CompatibleSequence(Enemy->AlertAnimation)
            ? Enemy->GetCombatAlertSourceSeconds(AlertSequence->GetPlayLength()) : 0.f;
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
            AttackPositionSeconds=Enemy->GetCombatAttackSourceSeconds(AttackSequence->GetPlayLength());
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

// Candidate method definitions appended to the existing DWCombatAnimInstance.cpp.
namespace DWNativeAnimationFx
{
 bool ExactFx(const FAnimNotifyEvent& E)
 {
  const UClass* N=E.Notify?E.Notify->GetClass():nullptr;const UClass* S=E.NotifyStateClass?E.NotifyStateClass->GetClass():nullptr;
  return N==UAnimNotify_PlayNiagaraEffect::StaticClass()||N==UAnimNotify_PlayParticleEffect::StaticClass()
   ||S==UAnimNotifyState_TimedNiagaraEffect::StaticClass()||S==UAnimNotifyState_TimedParticleEffect::StaticClass();
 }
}

void UDWCombatAnimInstance::ResetNativeFxClock()
{
 NativeFxSequence.Reset();NativeFxRole=NAME_None;NativeFxSerial=INDEX_NONE;NativeFxPrevious=0.f;NativeFxEpoch=0;NativeFxGeneration=0;NativeFxClockValid=false;NativeFxSeedCurrent=false;
}

void UDWCombatAnimInstance::NativeUninitializeAnimation()
{
 ResetNativeFxClock();Super::NativeUninitializeAnimation(); // Engine UninitializeAnimation owns NotifyState End.
}

void UDWCombatAnimInstance::NativeFinalizeAnimation(bool bWasEvaluated)
{
 Super::NativeFinalizeAnimation(bWasEvaluated);
 // UE5.8 SkeletalMeshComponent calls Finalize before ConditionallyDispatchQueuedAnimEvents,
 // after PostUpdate has appended the proxy queue. No pose advance or RootMotion mode changes.
 if(!IsInGameThread()||!GetWorld()||!GetWorld()->IsGameWorld())return;
 APawn* Pawn=TryGetPawnOwner();auto* Authority=UDWCombatAuthoritySubsystem::Get(Pawn);
 if(!Pawn||!Authority){NativeFxClockValid=false;return;}
 const uint64 Epoch=Authority->GetWorldEpoch();
 UAnimSequenceBase* Sequence=nullptr;FName Role=NAME_None;int32 Serial=INDEX_NONE;float Current=0.f;
 if(const auto* P=Cast<ADWPlayerCharacter>(Pawn))
 {
  if(P->IsCombatRestoreInProgress()){NativeFxSeedCurrent=true;NativeFxClockValid=false;return;}
  const auto* Weapon=P->WeaponAnimationComponent.Get();
  if(!P->IsDead()&&AnimationState==EDWCombatAnimationState::Melee&&Weapon&&Weapon->IsEnabled()&&Weapon->HasWeaponPresentation()
   &&(!P->GatheringComponent||!P->GatheringComponent->IsGatheringSwingActive())&&P->GetCurrentMeleeAnimation()==MeleeSequence.Get())
  {Sequence=MeleeSequence;Current=MeleePositionSeconds;Role=Weapon->GetPresentationClip().Role;Serial=Weapon->ActionSerial;}
 }
 else if(const auto* E=Cast<ADWEnemyCharacter>(Pawn))
 {
  if(E->IsAlive()&&AnimationState==EDWCombatAnimationState::Attack&&E->GetCurrentAttackAnimation()==AttackSequence.Get())
  {Sequence=AttackSequence;Current=AttackPositionSeconds;Role=TEXT("Attack");Serial=E->AttackSerial;}
 }
 const bool Restored=NativeFxEpoch!=0&&NativeFxEpoch!=Epoch;NativeFxEpoch=Epoch;
 if(!Sequence||!FMath::IsFinite(Current)){NativeFxClockValid=false;NativeFxSeedCurrent=false;return;}
 Current=FMath::Clamp(Current,0.f,Sequence->GetPlayLength());
 const bool NewAction=!NativeFxClockValid||NativeFxSequence.Get()!=Sequence||NativeFxRole!=Role||NativeFxSerial!=Serial;
 const bool Rewind=NativeFxClockValid&&!NewAction&&Current<NativeFxPrevious;
 float Previous=NewAction?-KINDA_SMALL_NUMBER:NativeFxPrevious;
 if(Restored||NativeFxSeedCurrent||Rewind)Previous=Current; // States at the current point, never replay passed one-shots.
 if(NewAction||Restored||Rewind){if(++NativeFxGeneration==0)++NativeFxGeneration;}
 NativeFxSequence=Sequence;NativeFxRole=Role;NativeFxSerial=Serial;NativeFxPrevious=Current;NativeFxClockValid=true;NativeFxSeedCurrent=false;
 FAnimTickRecord Tick;Tick.SourceAsset=Sequence;Tick.TimeAccumulator=&Current;Tick.EffectiveBlendWeight=1.f;
 FAnimNotifyContext Context(Tick);
 // Unlike GetAnimNotifies(delta), the direct positions API preserves the small negative first boundary for time0.
 // Equal positions yield active states only, allowing Ready/frozen poses without replaying one-shots.
 Sequence->GetAnimNotifiesFromDeltaPositions(Previous,Current,Context);
 TArray<FAnimNotifyEventReference> Fx;
 for(auto& Ref:Context.ActiveNotifies)
 {
  const auto* Event=Ref.GetNotify();if(!Event||!DWNativeAnimationFx::ExactFx(*Event))continue;
  // Do not double-submit if a user later enables the node's own native notify extraction.
  if(NotifyQueue.AnimNotifies.ContainsByPredicate([&](const FAnimNotifyEventReference& Existing){return Existing.GetNotify()==Event&&Existing.GetSourceObject()==Sequence;}))continue;
  Ref.AddContextData<UE::Anim::FAnimNotifyAssetPlayerInstanceContext>(NativeFxGeneration);Fx.Add(Ref);
 }
 NotifyQueue.AddAnimNotifies(Fx,1.f); // Engine dispatch owns Notify/Begin/Tick/End and component retirement.
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWNativeFxClassFilterTest,"DoughWorld.Animation.NativeFX.ExactClassFilter",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWNativeFxClassFilterTest::RunTest(const FString&)
{
 FAnimNotifyEvent One;One.Notify=NewObject<UAnimNotify_PlayNiagaraEffect>();TestTrue(TEXT("Exact native Niagara one-shot admitted"),DWNativeAnimationFx::ExactFx(One));
 FAnimNotifyEvent State;State.NotifyStateClass=NewObject<UAnimNotifyState_TimedNiagaraEffect>();TestTrue(TEXT("Exact native timed Niagara admitted"),DWNativeAnimationFx::ExactFx(State));
 FAnimNotifyEvent Generic;Generic.Notify=NewObject<UAnimNotify_PlaySound>();TestFalse(TEXT("Generic/custom gameplay notify not admitted"),DWNativeAnimationFx::ExactFx(Generic));
 FAnimNotifyEvent Named;Named.NotifyName=TEXT("Damage");TestFalse(TEXT("Named damage event not admitted"),DWNativeAnimationFx::ExactFx(Named));return true;
}
#endif
