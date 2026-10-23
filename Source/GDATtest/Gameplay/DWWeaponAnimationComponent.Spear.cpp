#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatPlayerVitals.h"
#include "DWCombatRules.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include <limits>

namespace DWSpearBinding
{
bool MatchesClip(const FDWWeaponAnimationClip* Clip,UAnimSequenceBase* Sequence,
    const FDWSpearSourceTiming& Timing,const FDWSpearTuning& Tuning,FString& Error)
{
    if(!Clip||!IsValid(Sequence)||Clip->Animation!=Sequence||!FMath::IsFinite(Clip->DurationSeconds)||
        !FMath::IsNearlyEqual(static_cast<double>(Clip->DurationSeconds),Timing.DurationSeconds,1.e-6))
    {Error=TEXT("Spear source binding must match the current Attack sequence and exact source duration.");return false;}
    if(!Timing.Validate(Tuning,Error))return false;
    if(Clip->DamageEvents.IsEmpty())
    {Error=TEXT("HC04: spear source requires explicit reviewed contact markers; an empty list is not evidence of alignment.");return false;}
    for(const FDWWeaponDamageEvent& Event:Clip->DamageEvents)
    {
        const double Time=Event.TimeSeconds;
        if(!FMath::IsFinite(Time)||Time<Timing.ContactBeginSeconds-1.e-6||Time>Timing.ContactEndSeconds+1.e-6)
        {Error=TEXT("HC04: current clip contact markers disagree with reviewed source timing; supplying a different Timing cannot retime the clip.");return false;}
    }
    return true;
}
}

void UDWWeaponAnimationComponent::InitializeExplicitFormalSpearBinding()
{
    if(!bEnableFormalSpearExplicitBinding)return;
    // Never retain a previous successful source when a new explicit initialization fails.
    bFormalSpearConfigured=false;SpearSourceSequence.Reset();FormalSpearBindingError.Reset();
    const auto* Owner=Player();const auto* World=GetWorld();
    const auto* GI=Owner?Owner->GetGameInstance<UDWGameInstance>():nullptr;
    if(!IsInGameThread()||IsBeingDestroyed()||!IsValid(Owner)||Owner->IsActorBeingDestroyed()||
        !World||World->bIsTearingDown||!GI||!GI->GetCombatRules())
        FormalSpearBindingError=TEXT("Explicit spear binding requires a live owner/world and initialized combat rules.");
    else if(!IsValid(ApprovedFormalSpearSequence)||
        !FMath::IsFinite(ApprovedFormalSpearSourceDurationSeconds)||
        !FMath::IsNearlyEqual(static_cast<double>(ApprovedFormalSpearSequence->GetPlayLength()),ApprovedFormalSpearSourceDurationSeconds,1.e-6))
        FormalSpearBindingError=TEXT("Explicit spear sequence length must equal the approved source duration; no rescale/crop is allowed.");
    else
    {
        int32 SpearSets=0;for(const UDWWeaponAnimationSet* Set:AnimationSets)if(Set&&Set->Weapon==EDWAnimatedWeapon::Spear)++SpearSets;
        if(SpearSets!=1)FormalSpearBindingError=TEXT("Explicit spear binding requires exactly one spear animation set.");
        else ConfigureFormalSpear(*GI->GetCombatRules(),ApprovedFormalSpearSequence.Get(),
            {ApprovedFormalSpearSourceDurationSeconds,ApprovedFormalSpearContactBeginSeconds,ApprovedFormalSpearContactEndSeconds},FormalSpearBindingError);
    }
    if(!bFormalSpearConfigured)
        UE_LOG(LogTemp,Warning,TEXT("Explicit formal spear binding rejected: %s"),*FormalSpearBindingError);
}

bool UDWWeaponAnimationComponent::ConfigureFormalSpear(const FDWCombatRules& Rules,UAnimSequenceBase* Sequence,
    const FDWSpearSourceTiming& Timing,FString& Error)
{
    if(!IsInGameThread()||IsBeingDestroyed()||bActive||!IsValid(Sequence)){Error=TEXT("Spear binding requires a live idle component and explicit source sequence.");return false;}
    FDWSpearTuning Candidate;
    if(!FDWSpearTuning::Build(Rules,Candidate,Error)||!Timing.Validate(Candidate,Error))return false;
    const FDWWeaponAnimationClip* Clip=nullptr;
    for(const UDWWeaponAnimationSet* Set:AnimationSets)if(Set&&Set->Weapon==EDWAnimatedWeapon::Spear){Clip=Set->FindClip(TEXT("Attack"));break;}
    if(!DWSpearBinding::MatchesClip(Clip,Sequence,Timing,Candidate,Error))return false;
    SpearTuning=Candidate;SpearSourceSequence=Sequence;SpearSourceTiming=Timing;bFormalSpearConfigured=true;
    Error.Reset();return true;
}
EDWSpearForm UDWWeaponAnimationComponent::ReadFormalSpearForm(double FrameOffset) const
{
    const auto* P=Player();if(!P)return EDWSpearForm::Normal;
    const auto Form=FrameOffset<0.?P->GetCombatBaseForm():P->GetCombatBaseFormAtFrameOffset(FrameOffset);
    if(Form==EDWCombatBaseForm::Yeast)return EDWSpearForm::Yeast;
    // D10 committed stance and its read-only paid slice view; no visual flag inference.
    return Form==EDWCombatBaseForm::Mold?
        ((FrameOffset<0.?P->IsFormallyRooted():P->IsFormallyRootedAtFrameOffset(FrameOffset))?EDWSpearForm::MoldRooted:EDWSpearForm::MoldMobile):EDWSpearForm::Normal;
}
bool UDWWeaponAnimationComponent::BeginFormalSpear(FVector Target)
{
    auto* P=Player();auto* Set=GetSelectedSet();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Spear);
    const auto* Clip=Set?Set->FindClip(TEXT("Attack")):nullptr;
    FString Error;
    if(!bEnabled||!bFormalSpearConfigured||bActive||!A||!P||!P->HasCombatVitalsReady()||!P->IsCombatActionAllowed()||P->IsFormalDashing()||
        !Binding||!CombatWeaponResources.Snapshot.OwnedPermanentWeaponIds.Contains(Binding->ItemId)||
        Target.ContainsNaN()||SelectedWeapon!=EDWAnimatedWeapon::Spear||
        !DWSpearBinding::MatchesClip(Clip,SpearSourceSequence.Get(),SpearSourceTiming,SpearTuning,Error)||
        GetCombatActionWorldSeconds()<NextAttackTime||ActionSerial==MAX_int32)return false;
    FDWSpearAttackPlan Candidate;if(!Candidate.Start(SpearTuning,ReadFormalSpearForm()))return false;
    TargetLocation=Target;const int32 ExpectedSerial=ActionSerial+1;
    if(!StartRole(TEXT("Attack")))return false;
    // StartRole contains scene/component operations; do not continue through a reentrant replacement.
    if(!IsValid(P)||P->IsActorBeingDestroyed()||!P->IsCombatActionAllowed()||!bActive||SelectedWeapon!=EDWAnimatedWeapon::Spear||
        ActionSerial!=ExpectedSerial||ActiveSet!=Set||ActiveClip.Role!=TEXT("Attack")||ActiveClip.Animation!=SpearSourceSequence.Get())return false;
    A=UDWCombatAuthoritySubsystem::Get(P);if(!A){FinishAction();return false;}
    SpearAction=A->BeginAction(P);
    if(!SpearAction.Id){FinishAction();return false;}
    SpearPlan=Candidate;bFormalSpearActive=true;SpearForward=P->GetActorForwardVector().GetSafeNormal2D();
    bPrimaryHeld=true;bCharging=bChargeReady=false;
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();
    if(!IsCurrentFormalSpear(SpearAction))return false;
    P->GetCharacterMovement()->bOrientRotationToMovement=false;
    NextAttackTime=FMath::Max(NextAttackTime,GetCombatActionWorldSeconds()+SpearPlan.EstimateRemainingSeconds(ReadFormalSpearForm()));
    return true;
}
void UDWWeaponAnimationComponent::EndFormalSpear()
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(SpearAction);
    SpearAction={};bFormalSpearActive=false;SpearPlan.Cancel();
}
bool UDWWeaponAnimationComponent::IsCurrentFormalSpear(DWCombatCommit::FActionId Action) const
{
    const auto* P=Player();
    return IsValid(this)&&!IsBeingDestroyed()&&bActive&&bFormalSpearActive&&SpearAction.Epoch==Action.Epoch&&
        SpearAction.Id==Action.Id&&Action.Id!=0&&SelectedWeapon==EDWAnimatedWeapon::Spear&&
        ActiveClip.Role==TEXT("Attack")&&ActiveClip.Animation==SpearSourceSequence.Get()&&P&&
        !P->IsActorBeingDestroyed()&&P->HasCombatVitalsReady()&&P->IsCombatActionAllowed();
}
void UDWWeaponAnimationComponent::ResolveFormalSpearWindow(DWCombatCommit::FActionId Action)
{
    if(!IsCurrentFormalSpear(Action)||SpearPlan.GetPhase()!=EDWSpearPhase::Active)return;
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);if(!A)return;
    const auto Tuning=SpearPlan.GetTuning();const double SnapshotDamage=SpearPlan.GetDamageSnapshot();
    TArray<FDWSpearHitCandidate> Targets;
    FDWSpearHitQuery::Collect(GetWorld(),P,P->GetActorLocation(),SpearForward,Tuning,Targets);
    for(const auto& Candidate:Targets)
    {
        if(!IsCurrentFormalSpear(Action))return;
        FDWSpearHitCandidate Current;
        if(!FDWSpearHitQuery::Evaluate(GetWorld(),P,Candidate.Target.Get(),P->GetActorLocation(),SpearForward,Tuning,Current))continue;
        double Damage=0.;if(!Tuning.DamageAtDistance(Current.EdgeDistanceCm,SnapshotDamage,Damage))continue;
        FDWCombatDamageCommand C;
        C.Identity={Action,1,DWCombatCommit::EActionDamageKind::Melee,1};
        C.TargetLife=A->CaptureActorLife(Current.Target.Get());C.Source=P;C.Target=Current.Target;C.Instigator=P->GetController();
        C.Amount=static_cast<float>(Damage);C.KnockbackDistance=static_cast<float>(Tuning.KnockbackCm);
        C.KnockbackSeconds=static_cast<float>(Tuning.KnockbackSeconds);
        C.KnockbackDirection=(Current.Target->GetActorLocation()-P->GetActorLocation()).GetSafeNormal2D();
        if(C.KnockbackDirection.IsNearlyZero())C.KnockbackDirection=SpearForward;
        A->SubmitActionDamage(C); // One logical action/stage/packet; visual event count is irrelevant.
        if(!IsCurrentFormalSpear(Action))return;
    }
}
double UDWWeaponAnimationComponent::GetNextFormalSpearBoundarySeconds()const
{
    if(!bFormalSpearActive)return std::numeric_limits<double>::infinity();
    if(!SpearPlan.IsRunning())return 0.;
    double Boundary=SpearPlan.GetPhaseRemainingSeconds();const double Clock=SpearPlan.GetSourceSeconds();
    for(int32 I=0;I<ActiveClip.DamageEvents.Num();++I)
    {
        const double EventClock=MapPresentationEventToCombatSeconds(ActiveClip.DamageEvents[I].TimeSeconds);
        if(!FiredEvents.Contains(I)&&FMath::IsFinite(EventClock)&&EventClock>=Clock)
            Boundary=FMath::Min(Boundary,FMath::Max(0.,EventClock-Clock)/SpearPlan.GetPhaseRate());
    }
    return Boundary;
}
void UDWWeaponAnimationComponent::AdvanceFormalSpear(double Dt)
{
    if(bAdvancingFormalSpear||!FMath::IsFinite(Dt)||Dt<0.)return;
    const auto Action=SpearAction;if(!IsCurrentFormalSpear(Action)){bCombatAdvanceInterrupted=true;return;}
    TGuardValue<bool> Guard(bAdvancingFormalSpear,true);
    double Left=Dt,Offset=0.;
    for(int32 BoundaryBudget=3;BoundaryBudget>0&&IsCurrentFormalSpear(Action)&&SpearPlan.IsRunning();--BoundaryBudget)
    {
        const double Previous=SpearPlan.GetSourceSeconds();
        double Consumed=0.,Remaining=0.;
        if(!SpearPlan.AdvanceCurrentPhase(Left,Consumed,Remaining))return;
        Offset+=Consumed;Left=Remaining;ElapsedSeconds=SpearPlan.GetSourceSeconds();
        // Source events remain presentation notifications and never enter the legacy damage branch.
        for(int32 I=0;I<ActiveClip.DamageEvents.Num();++I)
        {
            const double Time=MapPresentationEventToCombatSeconds(ActiveClip.DamageEvents[I].TimeSeconds);
            if(FMath::IsFinite(Time)&&Time>=Previous&&Time<=SpearPlan.GetSourceSeconds())DispatchEvent(I);
            if(!IsCurrentFormalSpear(Action)){bCombatAdvanceInterrupted=true;return;}
        }
        if(SpearPlan.GetPhase()==EDWSpearPhase::Active&&Consumed>0.)ResolveFormalSpearWindow(Action);
        if(!IsCurrentFormalSpear(Action)){bCombatAdvanceInterrupted=true;return;}
        if(!SpearPlan.IsAtBoundary())return;
        const auto Form=ReadFormalSpearForm(Offset);
        if(!SpearPlan.EnterNextPhase(Form))return;
        if(SpearPlan.GetPhase()==EDWSpearPhase::Complete){FinishAction();return;}
        const double BoundaryTime=Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+Offset;
        NextAttackTime=FMath::Max(NextAttackTime,BoundaryTime+SpearPlan.EstimateRemainingSeconds(Form));
        if(SpearPlan.GetPhase()==EDWSpearPhase::Active)ResolveFormalSpearWindow(Action);
        if(!IsCurrentFormalSpear(Action)){bCombatAdvanceInterrupted=true;return;}
        if(Left<=0.)return;
    }
}
