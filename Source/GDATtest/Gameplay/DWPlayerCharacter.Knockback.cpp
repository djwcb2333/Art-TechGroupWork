#include "DWPlayerCharacter.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWWorldEvent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"

FDWPlayerCombatKnockback ADWPlayerCharacter::PrepareCombatKnockback(FVector Direction,float Distance,float Seconds,DWCombatCommit::FEntityLife Life) const
{return PrepareCombatKnockbackAtImpact(Direction,Distance,Seconds,Life,IsFormallyRooted());}

FDWPlayerCombatKnockback ADWPlayerCharacter::PrepareCombatKnockbackAtImpact(FVector Direction,float Distance,float Seconds,DWCombatCommit::FEntityLife Life,bool bRootedAtImpact)const
{
    FDWPlayerCombatKnockback K;
    if(!CanReceiveCombatDamage()||CombatKnockbackRevision==MAX_uint64||!Life.Entity||!Life.Life||Direction.ContainsNaN()||
        !FMath::IsFinite(Distance)||Distance<=0.f||!FMath::IsFinite(Seconds)||Seconds<=0.f||
        !FMath::IsFinite(static_cast<double>(Distance)/Seconds))return K;
    if(bRootedAtImpact)return K; // Committed stance immunity; positive HP damage still applies normally.
    if(CombatKnockback.Frame==GFrameCounter&&Distance<=CombatKnockback.SelectedDistanceCm)return K;
    K.Direction=Direction.GetSafeNormal2D();if(K.Direction.IsNearlyZero())return K;
    K.DistanceCm=Distance;K.DurationSeconds=Seconds;K.Life=Life;K.bValid=true;return K;
}

void ADWPlayerCharacter::InterruptLegacyRollForKnockbackFacts()
{
    if(!bIsRolling)return;
    bIsRolling=false;bDashCombatActive=false;DashHitActors.Reset();
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(DashCommitAction);
    DashCommitAction={};
    // UE5.8 RemoveRootMotionSourceByID only marks the matching native source for removal (no dispatch).
    if(auto* Movement=GetCharacterMovement())
    {Movement->RemoveRootMotionSourceByID(RollMotionId);Movement->bOrientRotationToMovement=bPreviousOrientToMovement;Movement->Velocity.X=Movement->Velocity.Y=0.f;}
    // Explicit base call: Engine/Private/Controller.cpp is a native counter update, no override/delegate.
    if(AController* C=LockedController.Get())C->AController::SetIgnoreMoveInput(false);
    LockedController.Reset();
    if(GetMesh())GetMesh()->bPauseAnims=bPreviousPauseAnims;
    CombatRollRestTransform=MeshRestTransform;bCombatRollRestorePending=true;
    if(GetWorld())NextRollTime=GetWorld()->GetTimeSeconds()+FMath::Max(0.f,RollCooldown); // Retain old roll cooldown policy; D09 is separate.
}

void ADWPlayerCharacter::CommitCombatKnockbackFacts(const FDWPlayerCombatKnockback& K)
{
    if(!K.bValid||CombatKnockbackRevision==MAX_uint64)return;
    ++CombatKnockbackRevision;
    const double Spent=CombatKnockback.Frame==GFrameCounter?CombatKnockback.CommittedTravelThisFrame:0.;
    CombatKnockback.Direction=K.Direction;CombatKnockback.Life=K.Life;CombatKnockback.Frame=GFrameCounter;
    CombatKnockback.SelectedDistanceCm=K.DistanceCm;CombatKnockback.CommittedTravelThisFrame=Spent;
    CombatKnockback.RemainingCm=FMath::Max(0.,K.DistanceCm-Spent);CombatKnockback.RemainingSeconds=K.DurationSeconds;
    CombatKnockback.bActive=CombatKnockback.RemainingCm>0.;
    EndFormalDashFacts(EDWYeastDashEnd::Control);
    InterruptLegacyRollForKnockbackFacts();
    // Ordinary injury never cancels the weapon action or resets its timing/ammunition.
}

void ADWPlayerCharacter::ResetCombatKnockbackFacts()
{
    if(CombatKnockbackRevision<MAX_uint64)++CombatKnockbackRevision;
    CombatKnockback={};EndFormalDashFacts(IsDead()?EDWYeastDashEnd::Death:EDWYeastDashEnd::Cancelled);InterruptLegacyRollForKnockbackFacts();
}

void ADWPlayerCharacter::PublishCombatKnockbackEffects()
{
    if(!bCombatRollRestorePending)return;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return;
    const auto Life=A->CaptureActorLife(this);const uint64 Revision=CombatKnockbackRevision;
    const TWeakObjectPtr<UWorld> World(GetWorld());const bool Begun=HasActorBegunPlay();
    const auto Current=[&]()
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
        if(!IsValid(this)||IsActorBeingDestroyed()||!World.IsValid()||GetWorld()!=World.Get()||World->bIsTearingDown||
            (Begun&&!HasActorBegunPlay())||Revision!=CombatKnockbackRevision||!Authority)return false;
        const auto L=Authority->CaptureActorLife(this);return L.Entity==Life.Entity&&L.Life==Life.Life;
    };
    if(!Current())return;
    const FTransform Restore=CombatRollRestTransform;bCombatRollRestorePending=false;
    if(GetMesh())GetMesh()->SetRelativeTransform(Restore); // Last operation: scene callbacks cannot restart any old work.
}

bool ADWPlayerCharacter::AdvanceCombatKnockback(float Dt)
{
    const TWeakObjectPtr<UWorld> BeforeWorld(GetWorld());const uint64 BeforeRevision=CombatKnockbackRevision;
    const bool HadBegunPlay=HasActorBegunPlay();
    PublishCombatKnockbackEffects();
    if(!IsValid(this)||IsActorBeingDestroyed()||!BeforeWorld.IsValid()||GetWorld()!=BeforeWorld.Get()||BeforeWorld->bIsTearingDown||
        (HadBegunPlay&&!HasActorBegunPlay())||CombatKnockbackRevision!=BeforeRevision)return false;
    if(IsFormallyRooted()){ResetCombatKnockbackFacts();return true;}
    if(!CombatKnockback.bActive)return true;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);const TWeakObjectPtr<UWorld> World(GetWorld());
    const auto Life=CombatKnockback.Life;const uint64 Revision=CombatKnockbackRevision;const bool Begun=HasActorBegunPlay();
    auto Current=[&]()
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
        if(!IsValid(this)||IsActorBeingDestroyed()||!World.IsValid()||GetWorld()!=World.Get()||World->bIsTearingDown||
            (Begun&&!HasActorBegunPlay())||Revision!=CombatKnockbackRevision||IsDead()||bRestoringSave||!Authority)return false;
        const auto L=Authority->CaptureActorLife(this);return L.Entity==Life.Entity&&L.Life==Life.Life;
    };
    if(!A||!Current())return false;
    if(IsCombatSimulationSuspended()||World->IsPaused()||UDWWorldEventSubsystem::IsPlaying(this))return true;
    if(!FMath::IsFinite(Dt)||Dt<=0.f)return true;
    if(AController* C=GetController())C->StopMovement();if(!Current())return false;
    ConsumeMovementInputVector();GetCharacterMovement()->StopMovementImmediately();if(!Current())return false;
    const double Seconds=FMath::Min(static_cast<double>(Dt),CombatKnockback.RemainingSeconds);
    const double Distance=CombatKnockback.RemainingCm*(Seconds/CombatKnockback.RemainingSeconds);
    // Debit attempted travel before Sweep callbacks: a stronger reentrant hit in this frame
    // can replace the budget, but cannot add the already committed travel a second time.
    CombatKnockback.RemainingCm=FMath::Max(0.,CombatKnockback.RemainingCm-Distance);
    CombatKnockback.RemainingSeconds=FMath::Max(0.,CombatKnockback.RemainingSeconds-Seconds);
    if(CombatKnockback.Frame==GFrameCounter)CombatKnockback.CommittedTravelThisFrame+=Distance;
    FHitResult Hit;AddActorWorldOffset(CombatKnockback.Direction*Distance,true,&Hit,ETeleportType::None);
    if(!Current())return false;
    if(Hit.bBlockingHit||CombatKnockback.RemainingCm<=UE_DOUBLE_SMALL_NUMBER||CombatKnockback.RemainingSeconds<=UE_DOUBLE_SMALL_NUMBER)
    {CombatKnockback.bActive=false;CombatKnockback.RemainingCm=CombatKnockback.RemainingSeconds=0.;}
    return true;
}
