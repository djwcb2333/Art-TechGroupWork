#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPresentationTiming.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "Animation/AnimSequenceBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"

void UDWWeaponAnimationComponent::InitializeFormalDaggerRules()
{
    const auto* P=Player();const auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;FString Error;
    bFormalDaggerConfigured=GI&&GI->GetCombatRules()&&FDWDaggerTuning::Build(*GI->GetCombatRules(),DaggerTuning,Error);
}
void UDWWeaponAnimationComponent::RefreshDaggerEquipmentFacts()
{
    auto* P=Player();if(!P||!GetWorld()||GetWorld()->bIsTearingDown)return;
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)It->SyncDaggerAwareness(P,true);
}
void UDWWeaponAnimationComponent::BreakDaggerGraceForDamage()
{
    if(!GetWorld()||GetWorld()->bIsTearingDown)return;
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)It->DaggerAwareness.BreakProtection();
}
bool UDWWeaponAnimationComponent::RequestFormalDagger(FVector Target)
{
    if(bPrimaryHeld||Target.ContainsNaN())return false;
    if(bFormalDaggerActive)
    {
        if(!IsCurrentFormalDagger(DaggerAction)||bDaggerQueued)return false;
        // One actual new press buffers one continuation. Holding never repeats the attack.
        DaggerQueuedTarget=Target;bDaggerQueued=bPrimaryHeld=true;return true;
    }
    return BeginFormalDagger(Target);
}
bool UDWWeaponAnimationComponent::ReadDaggerSourceSegment(int32 Segment,FVector2D& Out,double& FullSourceDuration)const
{
    if(DaggerSourceSegments.Num()!=4||!DaggerSourceSegments.IsValidIndex(Segment))return false;
    for(int32 I=0;I<DaggerSourceSegments.Num();++I)
    {
        const auto& R=DaggerSourceSegments[I];
        if(!FMath::IsFinite(R.X)||!FMath::IsFinite(R.Y)||R.X<0.||R.Y<=R.X||R.Y>MAX_flt||
            (I==0&&!FMath::IsNearlyZero(R.X,1.e-6))||(I>0&&!FMath::IsNearlyEqual(R.X,DaggerSourceSegments[I-1].Y,1.e-6)))return false;
    }
    Out=DaggerSourceSegments[Segment];FullSourceDuration=DaggerSourceSegments.Last().Y;return true;
}
bool UDWWeaponAnimationComponent::BeginFormalDagger(FVector Target,bool Chain,double Offset,double StartTime,int32 Segment)
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    const double Now=StartTime>=0.?StartTime:GetCombatActionWorldSeconds();
    if(!IsEnabled()||!bFormalDaggerConfigured||!P||!A||bActive||(!Chain&&bPrimaryHeld)||
        SelectedWeapon!=EDWAnimatedWeapon::DualDaggers||!P->IsCombatActionAllowed()||P->IsFormalDashing()||
        Target.ContainsNaN()||Now<NextAttackTime||ActionSerial==MAX_int32)return false;
    FDWSpearTuning SegmentTuning;FVector2D Source;double FullSourceDuration=0.;
    if(!DaggerTuning.MakeSegmentTuning(Segment,SegmentTuning)||!ReadDaggerSourceSegment(Segment,Source,FullSourceDuration))return false;
    FDWSpearAttackPlan Candidate;if(!Candidate.Start(SegmentTuning,ReadFormalSpearForm(Offset)))return false;
    const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch(),Restore=CombatWeaponResources.RestoreEpoch;
    const int64 Revision=CombatWeaponResources.Revision;const TWeakObjectPtr<UWorld> World(GetWorld());
    if(!Chain)P->CancelCombatActions();
    A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||Player()!=P||!World.IsValid()||
        GetWorld()!=World.Get()||World->bIsTearingDown||!A||A->GetWorldEpoch()!=Epoch||!IsEnabled()||!P->IsCombatActionAllowed()||
        SelectedWeapon!=EDWAnimatedWeapon::DualDaggers||CombatWeaponResources.Revision!=Revision||CombatWeaponResources.RestoreEpoch!=Restore)return false;
    const auto Current=A->CaptureActorLife(P);if(Current.Entity!=Life.Entity||Current.Life!=Life.Life)return false;
    const auto Action=A->BeginAction(P);if(!Action.Id)return false;
    DaggerPlan=Candidate;DaggerAction=Action;DaggerPublishedAction={};bFormalDaggerActive=bActive=true;
    if(!Chain)bPrimaryHeld=true;
    bDaggerVisualTail=bDaggerQueued=bDaggerBackstabSelected=bSecondaryHeld=bCharging=bChargeReady=bReloading=false;
    DaggerBackstabTarget.Reset();TargetLocation=Target;DaggerForward=(Target-P->GetActorLocation()).GetSafeNormal2D();
    if(DaggerForward.IsNearlyZero())DaggerForward=P->GetActorForwardVector().GetSafeNormal2D();
    ActiveSet=GetSelectedSet();const auto* Clip=ActiveSet?ActiveSet->FindClip(TEXT("Attack")):nullptr;
    ActiveClip=Clip?*Clip:FDWWeaponAnimationClip();ActiveClip.Role=TEXT("Attack");
    // A full source group maps to one parameterized gameplay segment. Never crop it at the old .30 s.
    // Keeping the FULL source duration here preserves the existing sequence-evaluator's absolute-source mapping.
    ActiveClip.DurationSeconds=static_cast<float>(FullSourceDuration);DaggerComboStage=Segment;DaggerSourceSegment=Source;
    ElapsedSeconds=Source.X;FiredEvents.Reset();++ActionSerial;bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;
    DaggerPriorAttackLock=NextAttackTime;NextAttackTime=FMath::Max(DaggerPriorAttackLock,Now+DaggerPlan.EstimateRemainingSeconds(ReadFormalSpearForm(Offset)));
    return true;
}
bool UDWWeaponAnimationComponent::IsCurrentFormalDagger(DWCombatCommit::FActionId Action)const
{
    const auto* P=Player();return IsValid(this)&&!IsBeingDestroyed()&&bActive&&bFormalDaggerActive&&Action.Id&&
        Action.Id==DaggerAction.Id&&Action.Epoch==DaggerAction.Epoch&&SelectedWeapon==EDWAnimatedWeapon::DualDaggers&&
        IsEnabled()&&P&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();
}
void UDWWeaponAnimationComponent::PublishFormalDaggerStartEffects()
{
    const auto Action=DaggerAction;
    if(!IsCurrentFormalDagger(Action)||(Action.Id==DaggerPublishedAction.Id&&Action.Epoch==DaggerPublishedAction.Epoch))return;
    DaggerPublishedAction=Action;auto* P=Player();
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!IsCurrentFormalDagger(Action))return;
    P->GetCharacterMovement()->bOrientRotationToMovement=false;P->SetActorRotation(DaggerForward.Rotation());if(!IsCurrentFormalDagger(Action))return;
    P->SetHarvestHeld(false);if(!IsCurrentFormalDagger(Action))return;
    P->bSprinting=false;ResetTrails();if(!IsCurrentFormalDagger(Action))return;UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndFormalDagger()
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(DaggerAction);
    DaggerAction={};DaggerPublishedAction={};DaggerPlan.Cancel();DaggerBackstabTarget.Reset();
    bFormalDaggerActive=bDaggerQueued=bDaggerVisualTail=bDaggerBackstabSelected=false;
}
void UDWWeaponAnimationComponent::ResolveFormalDaggerWindow(DWCombatCommit::FActionId Action)
{
    if(!IsCurrentFormalDagger(Action)||DaggerPlan.GetPhase()!=EDWSpearPhase::Active)return;
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);if(!A)return;
    const auto Tuning=DaggerTuning.Attack;
    TArray<FDWSpearHitCandidate> Targets;FDWSpearHitQuery::CollectSector(GetWorld(),P,P->GetActorLocation(),DaggerForward,Tuning.RangeCm,Tuning.FullArcDegrees,Targets);
    if(!bDaggerBackstabSelected)
    {
        bDaggerBackstabSelected=true;
        // CollectSector sorts by collision-edge distance. Freeze one candidate for this strike.
        for(const auto& C:Targets)if(auto* E=Cast<ADWEnemyCharacter>(C.Target.Get());E&&E->IsEligibleForDaggerBackstab(P,DaggerTuning.BackArcDegrees))
        {DaggerBackstabTarget=E;break;}
    }
    for(const auto& Candidate:Targets)
    {
        if(!IsCurrentFormalDagger(Action))return;FDWSpearHitCandidate Hit;
        if(!FDWSpearHitQuery::EvaluateSector(GetWorld(),P,Candidate.Target.Get(),P->GetActorLocation(),DaggerForward,Tuning.RangeCm,Tuning.FullArcDegrees,Hit))continue;
        A=UDWCombatAuthoritySubsystem::Get(P);if(!A)return;
        if(auto* E=Cast<ADWEnemyCharacter>(Hit.Target.Get());E&&E==DaggerBackstabTarget.Get()&&E->IsEligibleForDaggerBackstab(P,DaggerTuning.BackArcDegrees))
            A->SubmitDaggerBackstab(*this,*E,Action);
        else
        {
            FDWCombatDamageCommand C;C.Identity={Action,1,DWCombatCommit::EActionDamageKind::Melee,1};
            C.Source=P;C.Target=Hit.Target;C.TargetLife=A->CaptureActorLife(Hit.Target.Get());C.Instigator=P->GetController();
            C.Amount=static_cast<float>(DaggerPlan.GetDamageSnapshot());C.KnockbackDistance=static_cast<float>(Tuning.KnockbackCm);
            C.KnockbackSeconds=static_cast<float>(Tuning.KnockbackSeconds);C.KnockbackDirection=DaggerForward;A->SubmitActionDamage(C);
        }
        if(!IsCurrentFormalDagger(Action))return;
    }
}
void UDWWeaponAnimationComponent::AdvanceFormalDagger(double Dt)
{
    auto Action=DaggerAction;if(!IsCurrentFormalDagger(Action)){bCombatAdvanceInterrupted=true;return;}
    double Left=Dt,Offset=0.;
    for(int32 Budget=6;Budget>0&&IsCurrentFormalDagger(Action)&&DaggerPlan.IsRunning();--Budget)
    {
        double Used=0.,Carry=0.;if(!DaggerPlan.AdvanceCurrentPhase(Left,Used,Carry))return;
        Offset+=Used;Left=Carry;
        ElapsedSeconds=DWCombatPresentationTiming::MapSegment(DaggerPlan.GetSourceSeconds(),0.,DaggerPlan.GetTuning().IntervalSeconds,DaggerSourceSegment.X,DaggerSourceSegment.Y);
        if(DaggerPlan.GetPhase()==EDWSpearPhase::Active&&Used>0.)ResolveFormalDaggerWindow(Action);
        if(!IsCurrentFormalDagger(Action)){bCombatAdvanceInterrupted=true;return;}
        if(!DaggerPlan.IsAtBoundary())return;
        const auto Form=ReadFormalSpearForm(Offset);if(!DaggerPlan.EnterNextPhase(Form))return;
        const double Now=Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+Offset;
        NextAttackTime=FMath::Max(DaggerPriorAttackLock,Now+DaggerPlan.EstimateRemainingSeconds(Form));
        if(DaggerPlan.GetPhase()==EDWSpearPhase::Complete)
        {
            const bool Chain=bDaggerQueued;const FVector Aim=DaggerQueuedTarget;
            const int32 NextSegment=(DaggerComboStage+1)%4;const double SourceEnd=DaggerSourceSegment.Y;
            EndFormalDagger();bActive=bCharging=bChargeReady=bReloading=false;
            if(auto* Owner=Player())Owner->GetCharacterMovement()->bOrientRotationToMovement=bOldOrient;
            if(Chain&&BeginFormalDagger(Aim,true,Offset,Now,NextSegment))
            {Action=DaggerAction;PublishFormalDaggerStartEffects();if(!IsCurrentFormalDagger(Action)){bCombatAdvanceInterrupted=true;return;}continue;}
            // One input ends at THIS group's authored endpoint. Idle/Move blends out; no later group is played.
            ElapsedSeconds=SourceEnd;bDaggerVisualTail=false;ResetTrails();return;
        }
        if(DaggerPlan.GetPhase()==EDWSpearPhase::Active)ResolveFormalDaggerWindow(Action);
        if(!IsCurrentFormalDagger(Action)){bCombatAdvanceInterrupted=true;return;}
        if(Left<=0.&&!DaggerPlan.IsAtBoundary())return;
    }
}
void UDWWeaponAnimationComponent::AdvanceFormalDaggerVisualTail(double Dt)
{
    // There is no independent long tail: the entire selected group belongs to its one plan.
    // Keep the existing private call site inert for other weapon compatibility.
    bDaggerVisualTail=false;
}
