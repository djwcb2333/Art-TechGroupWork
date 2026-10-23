#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatBottleTransaction.h"
#include "DWCombatItemRegistry.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
void UDWWeaponAnimationComponent::InitializeFormalBottleRules()
{
    const auto* P=Player();const auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;FString Error;
    bBottleConfigured=GI&&GI->GetCombatRules()&&FDWBottleConfig::Build(*GI->GetCombatRules(),BottleConfig,Error);
}
bool UDWWeaponAnimationComponent::BeginFormalBottle(FVector Target)
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Alcohol);
    if(!A||!P||!bBottleConfigured||!IsEnabled()||!HasCurrentEquipmentIdentity()||bActive||
        SelectedWeapon!=EDWAnimatedWeapon::Alcohol||!P->IsCombatActionAllowed()||P->IsFormalDashing()||
        Target.ContainsNaN()||GetCombatActionWorldSeconds()<NextAttackTime||ActionSerial==MAX_int32||
        !Binding||!P->GetInventory()||P->GetInventory()->CountItem(Binding->ItemId)<BottleConfig.StockPerThrow)return false;
    const TWeakObjectPtr<UWorld> World(GetWorld());const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch();
    const int64 Revision=CombatWeaponResources.Revision;const uint64 Restore=CombatWeaponResources.RestoreEpoch;const int32 Serial=ActionSerial;
    P->CancelCombatActions();A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||Player()!=P||!World.IsValid()||World->bIsTearingDown||
        GetWorld()!=World.Get()||!A||A->GetWorldEpoch()!=Epoch||!P->IsCombatActionAllowed()||Revision!=CombatWeaponResources.Revision||
        Restore!=CombatWeaponResources.RestoreEpoch||ActionSerial!=Serial||bActive||P->IsFormalDashing()||
        GetCombatActionWorldSeconds()<NextAttackTime||SelectedWeapon!=EDWAnimatedWeapon::Alcohol||!HasCurrentEquipmentIdentity())return false;
    const auto CurrentLife=A->CaptureActorLife(P);if(CurrentLife.Entity!=Life.Entity||CurrentLife.Life!=Life.Life)return false;
    FDWBottlePlan Candidate;if(!Candidate.Start(BottleConfig,ReadFormalSpearForm()))return false;
    const auto Action=A->BeginAction(P);if(!Action.Id)return false;
    BottleAction=Action;BottlePlan=Candidate;bFormalBottleActive=bActive=bPrimaryHeld=true;bBottleCommitted=false;
    bSecondaryHeld=bCharging=bChargeReady=bReloading=false;BottlePublishedAction={};BottleRequestedTarget=Target;
    ActiveSet=GetSelectedSet();ActiveClip={};
    if(const auto* Clip=ActiveSet?ActiveSet->FindClip(TEXT("Attack")):nullptr)ActiveClip=*Clip;
    ActiveClip.Role=TEXT("Attack");ElapsedSeconds=0.;FiredEvents.Reset();++ActionSerial;
    BottleForward=(Target-P->GetActorLocation()).GetSafeNormal2D();if(BottleForward.IsNearlyZero())BottleForward=P->GetActorForwardVector().GetSafeNormal2D();
    bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;BottlePriorAttackLock=NextAttackTime;
    NextAttackTime=FMath::Max(BottlePriorAttackLock,GetCombatActionWorldSeconds()+BottlePlan.EstimateRemaining(ReadFormalSpearForm()));
    return true;
}
void UDWWeaponAnimationComponent::PublishFormalBottleStartEffects()
{
    const auto Action=BottleAction;
    if(!IsCurrentFormalBottle(Action)||(BottlePublishedAction.Id==Action.Id&&BottlePublishedAction.Epoch==Action.Epoch))return;
    BottlePublishedAction=Action;auto* P=Player();
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!IsCurrentFormalBottle(Action))return;
    P->GetCharacterMovement()->bOrientRotationToMovement=false;P->SetActorRotation(BottleForward.Rotation());if(!IsCurrentFormalBottle(Action))return;
    P->SetHarvestHeld(false);if(!IsCurrentFormalBottle(Action))return;ResetTrails();if(!IsCurrentFormalBottle(Action))return;UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndFormalBottle()
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(BottleAction);
    BottleAction={};BottlePublishedAction={};BottlePlan.Cancel();bFormalBottleActive=false;bBottleCommitted=false;
    // Already paid bottles/areas keep their independent emitter identity and lifetime.
}
bool UDWWeaponAnimationComponent::IsCurrentFormalBottle(DWCombatCommit::FActionId Action)const
{
    const auto* P=Player();return IsValid(this)&&!IsBeingDestroyed()&&bEnabled&&bActive&&bFormalBottleActive&&
        Action.Id&&BottleAction.Id==Action.Id&&BottleAction.Epoch==Action.Epoch&&SelectedWeapon==EDWAnimatedWeapon::Alcohol&&
        HasCurrentEquipmentIdentity()&&P&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();
}
void UDWWeaponAnimationComponent::AdvanceFormalBottle(double Dt)
{
    const auto Action=BottleAction;if(!IsCurrentFormalBottle(Action)){bCombatAdvanceInterrupted=true;return;}
    PublishFormalBottleStartEffects();if(!IsCurrentFormalBottle(Action)){bCombatAdvanceInterrupted=true;return;}
    double Left=Dt,Offset=0.;
    for(int32 Budget=5;Budget>0&&IsCurrentFormalBottle(Action)&&BottlePlan.IsRunning();--Budget)
    {
        double Used=0.;if(!BottlePlan.Advance(Left,Used))return;
        Offset+=Used;Left-=Used;ElapsedSeconds=BottlePlan.SourceSeconds();
        if(!BottlePlan.AtBoundary())return;
        const auto Form=ReadFormalSpearForm(Offset);BottleBoundaryWorldSeconds=Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+Offset;
        if(BottlePlan.GetPhase()==EDWBottlePhase::Release)
        {
            if(!FDWCombatBottleTransaction::Commit(*this,Offset))
            {if(IsCurrentFormalBottle(Action))FinishAction();else bCombatAdvanceInterrupted=true;return;}
        }
        else if(!BottlePlan.EnterNext(Form))return;
        if(!IsCurrentFormalBottle(Action)){bCombatAdvanceInterrupted=true;return;}
        NextAttackTime=FMath::Max(BottlePriorAttackLock,BottleBoundaryWorldSeconds+BottlePlan.EstimateRemaining(Form));
        if(BottlePlan.GetPhase()==EDWBottlePhase::Complete){FinishAction();return;}
        if(BottlePlan.GetPhase()==EDWBottlePhase::Gap)Player()->GetCharacterMovement()->bOrientRotationToMovement=bOldOrient;
        if(Left<=0.&&BottlePlan.Remaining()>0.)return;
    }
}
