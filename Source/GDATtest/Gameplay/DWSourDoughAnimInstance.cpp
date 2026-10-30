#include "DWSourDoughAnimInstance.h"
#include "DWSourDoughEnemy.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/AnimNode_RelevantAssetPlayerBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "CoreGlobals.h"
void UDWSourDoughAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation(); ResetDeathObservation(); Eat=FDWSourEatPhases(); SourState=EDWSourVisualState::Idle; SourAttackPositionCache=0.f;
}
void UDWSourDoughAnimInstance::NativeUninitializeAnimation()
{
    ResetDeathObservation(); Super::NativeUninitializeAnimation();
}
void UDWSourDoughAnimInstance::ResetDeathObservation()
{
    ++DeathObservationSerial; DeathObservation.Reset(); DeathObservedOwner.Reset(); DeathObservedMesh.Reset();
    DeathObservedSequence.Reset(); DeathObservedPlayer=nullptr; DeathObservedGeneration=0;
    DeathQueuedGeneration=DeathQueuedSerial=DeathQueuedFrame=0;
    bDeathNotifyQueued=false; bDeathNotifyDelivered=false;
}

void UDWSourDoughAnimInstance::NativePostEvaluateAnimation()
{
    Super::NativePostEvaluateAnimation();
    if(!IsInGameThread())return; // Never dispatch actor mutations from a worker thread.
    auto* Owner=Cast<ADWSourDoughEnemy>(TryGetPawnOwner());
    auto* Mesh=GetSkelMeshComponent(); auto* World=GetWorld();
    if(!IsValid(Owner)||Owner->IsActorBeingDestroyed()||!IsValid(Mesh)||Owner->GetMesh()!=Mesh||
       Mesh->GetAnimInstance()!=this||!World||World->bIsTearingDown||Owner->GetWorld()!=World)
    {ResetDeathObservation();return;}
    if(World->IsPaused())return; // Preserve observation for a later unpaused evaluation.
    const uint64 Generation=Owner->GetSourDeathPresentationGeneration();
    const int32 MachineIndex=GetStateMachineIndex(TEXT("SourDoughEatingAndCombat"));
    const FAnimNode_StateMachine* Machine=MachineIndex!=INDEX_NONE?GetStateMachineInstance(MachineIndex):nullptr;
    const int32 StateIndex=Machine?Machine->GetStateIndex(TEXT("Death")):INDEX_NONE;
    auto* Sequence=Cast<UAnimSequenceBase>(Owner->DeathAnimation.Get());
    if(!Generation||!Machine||StateIndex==INDEX_NONE||GetCurrentStateName(MachineIndex)!=TEXT("Death")||
       !IsValid(Sequence)||!FMath::IsFinite(Sequence->RateScale)||Sequence->RateScale<0.f||
       !FMath::IsFinite(GetInstanceStateWeight(MachineIndex,StateIndex))||GetInstanceStateWeight(MachineIndex,StateIndex)<=0.f)
    {ResetDeathObservation();return;}
    if(Sequence->RateScale==0.f)return; // Temporarily paused sequence: retain pending observation.
    const auto* Player=GetRelevantAssetPlayerInterfaceFromState(MachineIndex,StateIndex);
    if(!Player||Player->GetAnimAsset()!=Sequence||Player->IsLooping())
    {ResetDeathObservation();return;}
    if(DeathObservedGeneration!=Generation||DeathObservedOwner.Get()!=Owner||DeathObservedMesh.Get()!=Mesh||
       DeathObservedSequence.Get()!=Sequence||DeathObservedPlayer!=Player)
    {
        ResetDeathObservation(); DeathObservedGeneration=Generation; DeathObservedOwner=Owner;
        DeathObservedMesh=Mesh; DeathObservedSequence=Sequence; DeathObservedPlayer=Player;
    }
    if(bDeathNotifyDelivered||bDeathNotifyQueued)return;
    const auto* Delta=Player->GetDeltaTimeRecord();
    if(!Delta||!Delta->IsPreviousValid())return;
    const float Length=Player->GetCurrentAssetLength();
    const float Remaining=GetRelevantAnimTimeRemaining(MachineIndex,StateIndex);
    if(!DeathObservation.Sample(Length,Player->GetCurrentAssetTime(),Delta->GetPrevious(),Delta->Delta,Remaining,true))return;
    // Preserve the FIRST evaluated end frame; never reschedule or replace it.
    // The next NativeUpdate on a later engine frame performs safe delivery.
    bDeathNotifyQueued=true; DeathQueuedGeneration=Generation;
    DeathQueuedSerial=DeathObservationSerial; DeathQueuedFrame=GFrameCounter;
}

bool UDWSourDoughAnimInstance::DeliverDeathAnimationCompleted()
{
    if(!IsInGameThread()||!bDeathNotifyQueued)return false;
    const uint64 Generation=DeathQueuedGeneration;
    if(DeathQueuedSerial!=DeathObservationSerial||Generation!=DeathObservedGeneration)
    {ResetDeathObservation();return false;}
    auto* Owner=DeathObservedOwner.Get(); auto* Mesh=DeathObservedMesh.Get(); auto* Sequence=DeathObservedSequence.Get();
    auto* World=GetWorld();
    if(!IsValid(Owner)||Owner->IsActorBeingDestroyed()||!IsValid(Mesh)||!IsValid(Sequence)||!World||World->bIsTearingDown||
       Owner->GetWorld()!=World||Owner->GetMesh()!=Mesh||Mesh->GetAnimInstance()!=this||TryGetPawnOwner()!=Owner||
       Owner->GetSourDeathPresentationGeneration()!=Generation)
    {ResetDeathObservation();return false;}
    // Keep the original end frame across same-frame updates and pause/resume.
    if(!DWSourDeathAnimationObservation::ReadyForDelivery(GFrameCounter,DeathQueuedFrame,World->IsPaused()))return false;
    if(FMath::IsFinite(Sequence->RateScale)&&Sequence->RateScale==0.f)return false;
    const int32 MachineIndex=GetStateMachineIndex(TEXT("SourDoughEatingAndCombat"));
    const FAnimNode_StateMachine* Machine=MachineIndex!=INDEX_NONE?GetStateMachineInstance(MachineIndex):nullptr;
    const int32 StateIndex=Machine?Machine->GetStateIndex(TEXT("Death")):INDEX_NONE;
    if(!Machine||StateIndex==INDEX_NONE||GetCurrentStateName(MachineIndex)!=TEXT("Death"))
    {ResetDeathObservation();return false;}
    const auto* Player=GetRelevantAssetPlayerInterfaceFromState(MachineIndex,StateIndex);
    if(!Player||Player!=DeathObservedPlayer||Player->GetAnimAsset()!=Sequence||Owner->DeathAnimation.Get()!=Sequence||Player->IsLooping()||
       !FMath::IsFinite(Sequence->RateScale)||Sequence->RateScale<0.f||
       !FMath::IsFinite(Player->GetCurrentAssetLength())||Player->GetCurrentAssetLength()<=0.f||
       !FMath::IsFinite(Player->GetCurrentAssetTime())||Player->GetCurrentAssetTime()<Player->GetCurrentAssetLength()||
       !FMath::IsFinite(GetRelevantAnimTimeRemaining(MachineIndex,StateIndex))||GetRelevantAnimTimeRemaining(MachineIndex,StateIndex)>0.f)
    {ResetDeathObservation();return false;}
    // Set before calling out: notification may re-enter, destroy the owner or invalidate this instance.
    bDeathNotifyQueued=false; bDeathNotifyDelivered=true;
    Owner->NotifySourDeathAnimationCompleted(this,Generation);
    return true;
}
FName UDWSourDoughAnimInstance::GetSourGraphStateName() const
{
    const int32 Index=GetStateMachineIndex(TEXT("SourDoughEatingAndCombat"));
    return Index!=INDEX_NONE ? GetCurrentStateName(Index) : NAME_None;
}
void UDWSourDoughAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    // NativeUpdate is game-thread preparation, before this frame's graph evaluation.
    // Delivery returns immediately so reentrant owner changes cannot be used below.
    if(bDeathNotifyQueued && DeliverDeathAnimationCompleted())return;
    const ADWSourDoughEnemy* Owner=Cast<ADWSourDoughEnemy>(TryGetPawnOwner());
    const USkeletalMeshComponent* Component=GetSkelMeshComponent();
    const USkeletalMesh* Mesh=Component ? Component->GetSkeletalMeshAsset() : nullptr;
    const USkeleton* Skeleton=Mesh ? Mesh->GetSkeleton() : nullptr;
    UAnimSequence* SourAttack=Owner ? Owner->SourAttackAnimation.Get() : nullptr;
    const bool bSourAttackReady=Skeleton && SourAttack && SourAttack->GetSkeleton()==Skeleton
        && FMath::IsFinite(SourAttack->GetPlayLength()) && SourAttack->GetPlayLength()>UE_SMALL_NUMBER;
    if (Owner)
    {
        // Rendering follows the accepted combat clock. It never supplies damage or phase timing.
        AttackSequence=bSourAttackReady ? SourAttack : IdleSequence.Get();
        if (!bSourAttackReady) SourAttackPositionCache=0.f;
        else if (Owner->AttackPhase!=EDWCombatPhase::None)
        {
            const float Duration=Owner->GetCombatAttackDuration();
            const float Elapsed=Owner->GetCombatAttackElapsedSeconds();
            const float SafeDuration=FMath::IsFinite(Duration) ? FMath::Max(.001f,Duration) : .001f;
            const float SafeElapsed=FMath::IsFinite(Elapsed) ? FMath::Max(0.f,Elapsed) : 0.f;
            SourAttackPositionCache=SourAttack->GetPlayLength()*FMath::Clamp(SafeElapsed/SafeDuration,0.f,1.f);
        }
        else SourAttackPositionCache=FMath::Clamp(SourAttackPositionCache,0.f,SourAttack->GetPlayLength());
        AttackPositionSeconds=SourAttackPositionCache;
        // Super reports the deliberately empty legacy slot; expose the real visual fallback instead.
        if (AnimationState==EDWCombatAnimationState::Attack) bUsingFallbackSequence=!bSourAttackReady;
    }
    else SourAttackPositionCache=0.f;
    if (Owner && Owner->IsAlive() && Owner->bDirectorPresentationControlled && Owner->bDirectorAttackPose && bSourAttackReady)
    {
        // Existing Attack SequenceEvaluator only: sample a visual pose, never start a gameplay strike.
        const float Position=FMath::IsFinite(Owner->DirectorAttackNormalized) ? FMath::Clamp(Owner->DirectorAttackNormalized,0.f,1.f) : 0.f;
        AttackSequence=SourAttack; AttackPositionSeconds=SourAttack->GetPlayLength()*Position;
        SourAttackPositionCache=AttackPositionSeconds;
        Eat.Step(false,false,0.f,0.f,0.f); bUsingFallbackSequence=false;
        SourState=EDWSourVisualState::Attack; return;
    }
    const bool bAssets=Skeleton && EatEnterSequence && EatLoopSequence && EatExitSequence
        && EatEnterSequence->GetSkeleton()==Skeleton && EatLoopSequence->GetSkeleton()==Skeleton && EatExitSequence->GetSkeleton()==Skeleton;
    const bool bEligible=Owner && Owner->IsAlive() && AnimationState==EDWCombatAnimationState::Idle && bAssets;
    const EDWSourEatPhase Phase=Eat.Step(bEligible,Owner && Owner->bWantsToEat,DeltaSeconds,
        EatEnterSequence ? EatEnterSequence->GetPlayLength() : 0.f,EatExitSequence ? EatExitSequence->GetPlayLength() : 0.f);
    switch (AnimationState)
    {
    case EDWCombatAnimationState::Move: SourState=EDWSourVisualState::Move; return;
    case EDWCombatAnimationState::Alert: SourState=EDWSourVisualState::Alert; return;
    case EDWCombatAnimationState::Attack: SourState=EDWSourVisualState::Attack; return;
    case EDWCombatAnimationState::Death: SourState=EDWSourVisualState::Death; return;
    default: break;
    }
    switch (Phase)
    {
    case EDWSourEatPhase::Enter: SourState=EDWSourVisualState::EatEnter; break;
    case EDWSourEatPhase::Loop: SourState=EDWSourVisualState::EatLoop; break;
    case EDWSourEatPhase::Exit: SourState=EDWSourVisualState::EatExit; break;
    default: SourState=EDWSourVisualState::Idle; break;
    }
}
