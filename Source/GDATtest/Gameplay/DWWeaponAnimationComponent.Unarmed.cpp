#include "DWWeaponAnimationComponent.h"
#include "DWCombatUnarmed.h"
#include "DWCombatEquipment.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

void UDWWeaponAnimationComponent::InitializeUnarmedRules()
{
    bUnarmedConfigured=false;
    const auto* P=Player();const auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;FString Error;
    bUnarmedConfigured=GI&&GI->GetCombatRules()&&FDWCombatUnarmedTuning::Build(*GI->GetCombatRules(),UnarmedTuning,Error);
}
bool UDWWeaponAnimationComponent::BeginFormalUnarmed(FVector Target)
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    if(!A||!P||!IsEnabled()||!bUnarmedConfigured||bActive||SelectedWeapon!=EDWAnimatedWeapon::Unarmed||
        !P->IsCombatActionAllowed()||P->IsFormalDashing()||Target.ContainsNaN()||GetCombatActionWorldSeconds()<NextAttackTime||ActionSerial==MAX_int32)return false;
    FDWSpearAttackPlan Candidate;
    const TWeakObjectPtr<UWorld> World(GetWorld());const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch();
    const int64 Revision=CombatWeaponResources.Revision;const uint64 Restore=CombatWeaponResources.RestoreEpoch;
    P->CancelCombatActions();
    A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||Player()!=P||!World.IsValid()||
        GetWorld()!=World.Get()||World->bIsTearingDown||!A||A->GetWorldEpoch()!=Epoch||!P->IsCombatActionAllowed()||
        Revision!=CombatWeaponResources.Revision||Restore!=CombatWeaponResources.RestoreEpoch||SelectedWeapon!=EDWAnimatedWeapon::Unarmed)return false;
    const auto CurrentLife=A->CaptureActorLife(P);if(CurrentLife.Entity!=Life.Entity||CurrentLife.Life!=Life.Life)return false;
    if(!Candidate.Start(UnarmedTuning.Attack,ReadFormalSpearForm()))return false;
    const auto Action=A->BeginAction(P);if(!Action.Id)return false;
    UnarmedAction=Action;UnarmedPlan=Candidate;bFormalUnarmedActive=bActive=true;
    bPrimaryHeld=true;bSecondaryHeld=bCharging=bChargeReady=bReloading=false;
    ActiveSet=nullptr;ActiveClip={};ActiveClip.Role=TEXT("Attack");ActiveClip.DurationSeconds=static_cast<float>(UnarmedTuning.Attack.IntervalSeconds);
    ElapsedSeconds=0.;FiredEvents.Reset();++ActionSerial;
    UnarmedForward=(Target-P->GetActorLocation()).GetSafeNormal2D();if(UnarmedForward.IsNearlyZero())UnarmedForward=P->GetActorForwardVector().GetSafeNormal2D();
    bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;
    NextAttackTime=FMath::Max(NextAttackTime,GetCombatActionWorldSeconds()+Candidate.EstimateRemainingSeconds(ReadFormalSpearForm()));
    UnarmedPublishedAction={};
    return true; // The physical hold must be bound before callbacks can pause the accepted action.
}
void UDWWeaponAnimationComponent::PublishAcceptedWeaponStartEffects()
{
    if(bFormalBowActive){PublishFormalBowStartEffects();return;}
    if(SelectedWeapon==EDWAnimatedWeapon::Alcohol){PublishFormalBottleStartEffects();return;}
    if(bFormalSporeActive){PublishFormalSporeStartEffects();return;}
    if(SelectedWeapon==EDWAnimatedWeapon::ShieldAxe){PublishFormalAxeStartEffects();return;}
    if(bFormalSwordActive){PublishFormalSwordStartEffects();return;}
    if(bFormalDaggerActive){PublishFormalDaggerStartEffects();return;}
    if(bFormalShotgunActive||bFormalShotgunReload){PublishFormalShotgunStart();return;}
    const auto Action=UnarmedAction;
    if(!IsCurrentFormalUnarmed(Action)||(UnarmedPublishedAction.Id==Action.Id&&UnarmedPublishedAction.Epoch==Action.Epoch))return;
    UnarmedPublishedAction=Action;auto* P=Player();
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!IsCurrentFormalUnarmed(Action))return;
    P->GetCharacterMovement()->bOrientRotationToMovement=false;P->SetActorRotation(UnarmedForward.Rotation());if(!IsCurrentFormalUnarmed(Action))return;
    P->SetHarvestHeld(false);if(!IsCurrentFormalUnarmed(Action))return;
    ResetTrails();if(!IsCurrentFormalUnarmed(Action))return;
    UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndFormalUnarmed()
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(UnarmedAction);
    UnarmedAction={};UnarmedPublishedAction={};UnarmedPlan.Cancel();bFormalUnarmedActive=false;
}
bool UDWWeaponAnimationComponent::IsCurrentFormalUnarmed(DWCombatCommit::FActionId Action)const
{
    const auto* P=Player();
    return IsValid(this)&&!IsBeingDestroyed()&&bActive&&bFormalUnarmedActive&&Action.Id&&
        Action.Id==UnarmedAction.Id&&Action.Epoch==UnarmedAction.Epoch&&SelectedWeapon==EDWAnimatedWeapon::Unarmed&&
        CombatWeaponResources.Snapshot.CurrentEquipmentId.IsNone()&&P&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();
}
void UDWWeaponAnimationComponent::ResolveFormalUnarmedWindow(DWCombatCommit::FActionId Action)
{
    if(!IsCurrentFormalUnarmed(Action)||UnarmedPlan.GetPhase()!=EDWSpearPhase::Active)return;
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);if(!A)return;
    const auto Tuning=UnarmedPlan.GetTuning();const double Damage=UnarmedPlan.GetDamageSnapshot();
    TArray<FDWSpearHitCandidate> Targets;FDWSpearHitQuery::Collect(GetWorld(),P,P->GetActorLocation(),UnarmedForward,Tuning,Targets);
    for(const auto& Target:Targets)
    {
        if(!IsCurrentFormalUnarmed(Action))return;
        FDWSpearHitCandidate Current;
        if(!FDWSpearHitQuery::Evaluate(GetWorld(),P,Target.Target.Get(),P->GetActorLocation(),UnarmedForward,Tuning,Current))continue;
        FDWCombatDamageCommand Command;Command.Identity={Action,1,DWCombatCommit::EActionDamageKind::Melee,1};
        Command.Source=P;Command.Target=Current.Target;Command.TargetLife=A->CaptureActorLife(Current.Target.Get());Command.Instigator=P->GetController();
        Command.Amount=static_cast<float>(Damage);Command.KnockbackDistance=static_cast<float>(Tuning.KnockbackCm);
        Command.KnockbackSeconds=static_cast<float>(Tuning.KnockbackSeconds);Command.KnockbackDirection=UnarmedForward;
        A->SubmitActionDamage(Command);if(!IsCurrentFormalUnarmed(Action))return;
    }
}
void UDWWeaponAnimationComponent::AdvanceFormalUnarmed(double Dt)
{
    const auto Action=UnarmedAction;if(!IsCurrentFormalUnarmed(Action)){bCombatAdvanceInterrupted=true;return;}
    double Left=Dt,Offset=0.;
    for(int32 Budget=3;Budget>0&&IsCurrentFormalUnarmed(Action)&&UnarmedPlan.IsRunning();--Budget)
    {
        double Consumed=0.,Remaining=0.;if(!UnarmedPlan.AdvanceCurrentPhase(Left,Consumed,Remaining))return;
        Offset+=Consumed;Left=Remaining;ElapsedSeconds=UnarmedPlan.GetSourceSeconds();
        if(UnarmedPlan.GetPhase()==EDWSpearPhase::Active&&Consumed>0.)ResolveFormalUnarmedWindow(Action);
        if(!IsCurrentFormalUnarmed(Action)){bCombatAdvanceInterrupted=true;return;}
        if(!UnarmedPlan.IsAtBoundary())return;
        const auto Form=ReadFormalSpearForm(Offset);if(!UnarmedPlan.EnterNextPhase(Form))return;
        if(UnarmedPlan.GetPhase()==EDWSpearPhase::Complete){FinishAction();return;}
        const double Now=Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+Offset;
        NextAttackTime=FMath::Max(NextAttackTime,Now+UnarmedPlan.EstimateRemainingSeconds(Form));
        if(UnarmedPlan.GetPhase()==EDWSpearPhase::Active)ResolveFormalUnarmedWindow(Action);
        if(!IsCurrentFormalUnarmed(Action)){bCombatAdvanceInterrupted=true;return;}
        if(Left<=0.)return;
    }
}
