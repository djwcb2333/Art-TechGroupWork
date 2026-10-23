#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatSporeShotTransaction.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include <limits>

void UDWWeaponAnimationComponent::InitializeFormalSporeRules()
{
    bSporeConfigured=false;auto* P=Player();auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;FString Error;
    bSporeConfigured=GI&&GI->GetCombatRules()&&FDWSporeGunConfig::Build(*GI->GetCombatRules(),SporeConfig,Error);
}
EDWSporeForm UDWWeaponAnimationComponent::ReadFormalSporeForm(double Offset)const
{
    switch(ReadFormalSpearForm(Offset))
    {case EDWSpearForm::Yeast:return EDWSporeForm::Yeast;case EDWSpearForm::MoldRooted:return EDWSporeForm::MoldRooted;
    case EDWSpearForm::MoldMobile:return EDWSporeForm::MoldMobile;default:return EDWSporeForm::Normal;}
}
bool UDWWeaponAnimationComponent::BeginFormalSpore(FVector Target)
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::SporeGun);
    if(!A||!P||!bSporeConfigured||!IsEnabled()||!HasCurrentEquipmentIdentity()||bActive||
        SelectedWeapon!=EDWAnimatedWeapon::SporeGun||!P->IsCombatActionAllowed()||P->IsFormalDashing()||
        Target.ContainsNaN()||GetCombatActionWorldSeconds()<NextAttackTime||ActionSerial==MAX_int32||
        !Binding||!P->GetInventory()||P->GetInventory()->CountItem(Binding->AmmoItemId)<SporeConfig.AmmoPerShot)return false;
    const TWeakObjectPtr<UWorld> World(GetWorld());const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch();
    const int64 Revision=CombatWeaponResources.Revision;const uint64 Restore=CombatWeaponResources.RestoreEpoch;const int32 Serial=ActionSerial;
    P->CancelCombatActions(); // Accepted start interrupts injection; preserves old cooldowns and already released effects.
    A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||Player()!=P||!World.IsValid()||World->bIsTearingDown||
        GetWorld()!=World.Get()||!A||A->GetWorldEpoch()!=Epoch||!P->IsCombatActionAllowed()||Revision!=CombatWeaponResources.Revision||
        Restore!=CombatWeaponResources.RestoreEpoch||ActionSerial!=Serial||bActive||P->IsFormalDashing()||
        GetCombatActionWorldSeconds()<NextAttackTime||SelectedWeapon!=EDWAnimatedWeapon::SporeGun||!HasCurrentEquipmentIdentity())return false;
    const auto CurrentLife=A->CaptureActorLife(P);if(CurrentLife.Entity!=Life.Entity||CurrentLife.Life!=Life.Life)return false;
    FDWSporeShotPlan Candidate;if(!Candidate.Start(SporeConfig,ReadFormalSporeForm()))return false;
    const auto Action=A->BeginAction(P);if(!Action.Id)return false;
    SporeAction=Action;SporePlan=Candidate;bFormalSporeActive=bActive=bPrimaryHeld=true;bSporeShotCommitted=false;
    bSecondaryHeld=bCharging=bChargeReady=bReloading=false;SporePublishedAction={};TargetLocation=Target;
    ActiveSet=GetSelectedSet();ActiveClip={};
    if(const auto* Clip=ActiveSet?ActiveSet->FindClip(TEXT("Attack")):nullptr)ActiveClip=*Clip;
    ActiveClip.Role=TEXT("Attack");if(!FMath::IsFinite(ActiveClip.DurationSeconds)||ActiveClip.DurationSeconds<=0.f)ActiveClip.DurationSeconds=static_cast<float>(SporeConfig.IntervalSeconds);
    ElapsedSeconds=0.;FiredEvents.Reset();++ActionSerial;
    SporeForward=(Target-P->GetActorLocation()).GetSafeNormal2D();if(SporeForward.IsNearlyZero())SporeForward=P->GetActorForwardVector().GetSafeNormal2D();
    bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;
    SporePriorAttackLock=NextAttackTime;
    NextAttackTime=FMath::Max(SporePriorAttackLock,GetCombatActionWorldSeconds()+SporePlan.EstimateRemaining(ReadFormalSporeForm()));
    return true; // Controller attaches the physical hold before scene effects may invoke callbacks.
}
void UDWWeaponAnimationComponent::UpdateFormalSporeGapAim(FVector Target)
{
    const auto* P=Player();
    if(Target.ContainsNaN()||SelectedWeapon!=EDWAnimatedWeapon::SporeGun||!IsEnabled()||!P||!P->IsCombatActionAllowed()||
        !bPrimaryHeld||(bActive&&(!bFormalSporeActive||SporePlan.GetPhase()!=EDWSporePhase::Gap)))return;
    // Only the next shot reads this point. The released emitter and locked attack direction are immutable.
    TargetLocation=Target;
}
void UDWWeaponAnimationComponent::PublishFormalSporeStartEffects()
{
    const auto Action=SporeAction;
    if(!IsCurrentFormalSpore(Action)||(SporePublishedAction.Id==Action.Id&&SporePublishedAction.Epoch==Action.Epoch))return;
    SporePublishedAction=Action;auto* P=Player();
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!IsCurrentFormalSpore(Action))return;
    P->GetCharacterMovement()->bOrientRotationToMovement=false;P->SetActorRotation(SporeForward.Rotation());if(!IsCurrentFormalSpore(Action))return;
    P->SetHarvestHeld(false);if(!IsCurrentFormalSpore(Action))return;
    ResetTrails();if(!IsCurrentFormalSpore(Action))return;UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndFormalSpore()
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(SporeAction);
    SporeAction={};SporePublishedAction={};SporePlan.Cancel();bFormalSporeActive=false;bSporeShotCommitted=false;
    // No RetireEmitter here: a released bullet owns its independent lifetime.
}
void UDWWeaponAnimationComponent::CancelFormalSporeFacts()
{
    const bool HadSpore=bFormalSporeActive;
    // Cancel presentation/action freely, but a paid shot retains the same cycle deadline as held fire.
    // An unpaid windup may restart and must not erase a prior weapon/equipment lock.
    const double RetainedLock=bSporeShotCommitted?NextAttackTime:SporePriorAttackLock;
    EndFormalSpore();if(!HadSpore)return;
    SporePlan={};SporeForward=FVector::ForwardVector;SporePriorAttackLock=SporeBoundaryWorldSeconds=0.;NextAttackTime=RetainedLock;
    ActiveSet=nullptr;ActiveClip={};ActiveClip.Role=NAME_None;ActiveClip.DurationSeconds=0.f;ElapsedSeconds=0.;FiredEvents.Reset();
    // Released emitters, completed inventory commits and receipts retain their independent ownership.
}
bool UDWWeaponAnimationComponent::IsCurrentFormalSpore(DWCombatCommit::FActionId Action)const
{
    const auto* P=Player();return IsValid(this)&&!IsBeingDestroyed()&&bEnabled&&bActive&&bFormalSporeActive&&
        Action.Id&&SporeAction.Id==Action.Id&&SporeAction.Epoch==Action.Epoch&&SelectedWeapon==EDWAnimatedWeapon::SporeGun&&
        HasCurrentEquipmentIdentity()&&P&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();
}
double UDWWeaponAnimationComponent::GetNextSporeBoundarySeconds(double Start)const
{
    if(!bFormalSporeActive)return bPrimaryHeld?FMath::Max(0.,NextAttackTime-Start):std::numeric_limits<double>::infinity();
    double Boundary=SporePlan.Remaining();
    for(int32 Index=0;Index<ActiveClip.DamageEvents.Num();++Index)
    {
        const double Time=MapPresentationEventToCombatSeconds(ActiveClip.DamageEvents[Index].TimeSeconds);
        const double Clock=SporePlan.GetSourceSeconds();
        if(!FiredEvents.Contains(Index)&&FMath::IsFinite(Time)&&Time>=Clock)
            Boundary=FMath::Min(Boundary,FMath::Max(0.,Time-Clock)/SporePlan.GetRate());
    }
    return Boundary;
}
void UDWWeaponAnimationComponent::AdvanceFormalSpore(double Dt)
{
    if(!bFormalSporeActive)
    {
        if(!bPrimaryHeld||GetCombatActionWorldSeconds()<NextAttackTime)return;
        const auto Origin=PrimaryInputOrigin;
        if(!BeginFormalSpore(TargetLocation)){bPrimaryHeld=false;PrimaryInputOrigin.Reset();return;}
        PrimaryInputOrigin=Origin;PublishFormalSporeStartEffects();return;
    }
    const auto Action=SporeAction;if(!IsCurrentFormalSpore(Action)){bCombatAdvanceInterrupted=true;return;}
    PublishFormalSporeStartEffects();if(!IsCurrentFormalSpore(Action)){bCombatAdvanceInterrupted=true;return;}
    double Left=Dt,Offset=0.;
    for(int32 Budget=5;Budget>0&&IsCurrentFormalSpore(Action)&&SporePlan.IsRunning();--Budget)
    {
        const double Previous=SporePlan.GetSourceSeconds();double Consumed=0.;if(!SporePlan.Advance(Left,Consumed))return;
        Offset+=Consumed;Left-=Consumed;ElapsedSeconds=SporePlan.GetSourceSeconds();
        for(int32 Index=0;Index<ActiveClip.DamageEvents.Num();++Index)
        {
            const double Time=MapPresentationEventToCombatSeconds(ActiveClip.DamageEvents[Index].TimeSeconds);
            if(FMath::IsFinite(Time)&&Time>=Previous&&Time<=SporePlan.GetSourceSeconds())DispatchEvent(Index);
            if(!IsCurrentFormalSpore(Action)){bCombatAdvanceInterrupted=true;return;}
        }
        if(!SporePlan.AtBoundary())return;
        const auto Form=ReadFormalSporeForm(Offset);
        SporeBoundaryWorldSeconds=Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+Offset;
        if(SporePlan.GetPhase()==EDWSporePhase::Release)
        {
            if(!FDWCombatSporeShotTransaction::Commit(*this,Offset))
            {if(IsCurrentFormalSpore(Action)){bPrimaryHeld=false;FinishAction();}else bCombatAdvanceInterrupted=true;return;}
        }
        else if(!SporePlan.EnterNext(Form))return;
        if(!IsCurrentFormalSpore(Action)){bCombatAdvanceInterrupted=true;return;}
        NextAttackTime=FMath::Max(SporePriorAttackLock,SporeBoundaryWorldSeconds+SporePlan.EstimateRemaining(Form));
        if(SporePlan.GetPhase()==EDWSporePhase::Complete){FinishAction();return;}
        if(SporePlan.GetPhase()==EDWSporePhase::Gap)
        {
            Player()->GetCharacterMovement()->bOrientRotationToMovement=bOldOrient;
        }
        // Process the release at the same exact boundary, even when this slice has no time left.
        if(Left<=0.&&SporePlan.Remaining()>0.)return;
    }
}
