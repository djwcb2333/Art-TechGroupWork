#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

void UDWWeaponAnimationComponent::InitializeShieldAxeRules()
{
    bShieldAxeConfigured=false;const auto* P=Player();const auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;FString Error;
    bShieldAxeConfigured=GI&&GI->GetCombatRules()&&FDWShieldAxeTuning::Build(*GI->GetCombatRules(),ShieldAxeTuning,Error)&&ShieldDefense.Configure(ShieldAxeTuning);
    if(P)ShieldDefense.NotifyBaseForm(static_cast<uint8>(P->GetCombatBaseForm()));
}
void UDWWeaponAnimationComponent::NotifyCombatBaseFormChanged(uint8 Form)
{ShieldDefense.NotifyBaseForm(Form);}
bool UDWWeaponAnimationComponent::HasUnserializedShieldState()const
{
    if(!bShieldAxeConfigured)return false;
    const auto S=ShieldDefense.Capture(GetCombatActionWorldSeconds());
    return S.CounterCharges>0||S.ReopenRemainingSeconds>0.||
        (S.bPerfectEligible&&S.PerfectRemainingSeconds>0.);
}
bool UDWWeaponAnimationComponent::BeginFormalAxeSwing(FVector Target)
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    if(!bShieldAxeConfigured||!IsEnabled()||!A||!P||!P->IsCombatActionAllowed()||P->IsFormalDashing()||
        bActive||ShieldDefense.IsHeld()||SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe||Target.ContainsNaN()||
        GetCombatActionWorldSeconds()<NextAttackTime||ActionSerial==MAX_int32)return false;
    const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch();const int64 Revision=CombatWeaponResources.Revision;
    P->CancelCombatActions();A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||Player()!=P||!A||A->GetWorldEpoch()!=Epoch||
        !P->IsCombatActionAllowed()||Revision!=CombatWeaponResources.Revision||SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe)return false;
    const auto NowLife=A->CaptureActorLife(P);if(NowLife.Entity!=Life.Entity||NowLife.Life!=Life.Life)return false;
    FDWSpearAttackPlan Candidate;if(!Candidate.Start(ShieldAxeTuning.Attack,ReadFormalSpearForm()))return false;
    AxeAction=A->BeginAction(P);if(!AxeAction.Id)return false;
    AxePlan=Candidate;bFormalAxeActive=bActive=true;bPrimaryHeld=true;bSecondaryHeld=bCharging=bChargeReady=bReloading=false;
    ActiveSet=GetSelectedSet();const auto* Clip=ActiveSet?ActiveSet->FindClip(TEXT("Attack")):nullptr;
    ActiveClip=Clip?*Clip:FDWWeaponAnimationClip{};ActiveClip.Role=TEXT("Attack");
    // Source duration stays authored; gameplay clocks below never depend on it or its damage markers.
    ElapsedSeconds=0.;FiredEvents.Reset();++ActionSerial;AxePendingPresentationSerial=ActionSerial;
    AxeForward=(Target-P->GetActorLocation()).GetSafeNormal2D();if(AxeForward.IsNearlyZero())AxeForward=P->GetActorForwardVector().GetSafeNormal2D();
    bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;AxeSwingCounterMultiplier=1.;
    NextAttackTime=FMath::Max(NextAttackTime,GetCombatActionWorldSeconds()+Candidate.EstimateRemainingSeconds(ReadFormalSpearForm()));return true;
}
bool UDWWeaponAnimationComponent::BeginFormalShieldGuard()
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    if(!bShieldAxeConfigured||!IsEnabled()||!P||!A||!P->IsCombatActionAllowed()||P->IsFormalDashing()||
        SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe||ShieldDefense.IsHeld()||ActionSerial==MAX_int32)return false;
    const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch();const int64 Revision=CombatWeaponResources.Revision;
    P->CancelCombatActions();A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||!A||A->GetWorldEpoch()!=Epoch||
        Revision!=CombatWeaponResources.Revision||!P->IsCombatActionAllowed()||SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe)return false;
    const auto NowLife=A->CaptureActorLife(P);if(NowLife.Entity!=Life.Entity||NowLife.Life!=Life.Life)return false;
    const double Now=GetCombatActionWorldSeconds();
    if(!ShieldDefense.Raise(Now,static_cast<uint8>(P->GetCombatBaseForm())))return false;
    bActive=bSecondaryHeld=true;bPrimaryHeld=bCharging=bChargeReady=bReloading=false;
    ActiveSet=GetSelectedSet();const auto* Clip=ActiveSet?ActiveSet->FindClip(TEXT("BlockHold")):nullptr;
    if(!Clip&&ActiveSet)Clip=ActiveSet->FindClip(TEXT("BlockEnter"));
    ActiveClip=Clip?*Clip:FDWWeaponAnimationClip{};ActiveClip.Role=TEXT("BlockHold");ElapsedSeconds=0.;FiredEvents.Reset();
    bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;++ActionSerial;AxePendingPresentationSerial=ActionSerial;
    // Native guard eligibility and rejected receipt facts precede all presentation callbacks.
    if(ShieldDefense.CanPerfectAt(Now))A->ParryEligibleShieldDamage(P,Now);
    return true;
}
void UDWWeaponAnimationComponent::ReleaseFormalShieldGuard()
{
    ShieldDefense.Release();bSecondaryHeld=false;
    if(SelectedWeapon==EDWAnimatedWeapon::ShieldAxe&&!bFormalAxeActive)
    {bActive=false;AxePendingPresentationSerial=0;if(auto* P=Player())P->GetCharacterMovement()->bOrientRotationToMovement=bOldOrient;ResetTrails();}
}
void UDWWeaponAnimationComponent::PublishFormalAxeStartEffects()
{
    auto* P=Player();const int32 Serial=AxePendingPresentationSerial;
    if(!Serial||Serial!=ActionSerial||!bActive||!P||!P->IsCombatActionAllowed()||SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe)return;
    AxePendingPresentationSerial=0;
    const auto Current=[&](){return IsValid(this)&&!IsBeingDestroyed()&&IsValid(P)&&!P->IsActorBeingDestroyed()&&P==Player()&&P->IsCombatActionAllowed()&&bActive&&ActionSerial==Serial&&SelectedWeapon==EDWAnimatedWeapon::ShieldAxe;};
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!Current())return;
    P->GetCharacterMovement()->bOrientRotationToMovement=false;
    if(bFormalAxeActive){P->SetActorRotation(AxeForward.Rotation());if(!Current())return;}
    P->SetHarvestHeld(false);if(!Current())return;ResetTrails();if(!Current())return;UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndFormalAxe()
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(AxeAction);
    AxeAction={};AxePlan.Cancel();bFormalAxeActive=false;AxePendingPresentationSerial=0;ShieldDefense.Release();
    // Cancellation/equipment changes do not own or remove an already-admitted incoming packet.
}
bool UDWWeaponAnimationComponent::IsCurrentFormalAxe(DWCombatCommit::FActionId Action)const
{
    const auto* P=Player();return IsValid(this)&&!IsBeingDestroyed()&&bActive&&bFormalAxeActive&&Action.Id&&
        Action.Id==AxeAction.Id&&Action.Epoch==AxeAction.Epoch&&HasCurrentEquipmentIdentity()&&SelectedWeapon==EDWAnimatedWeapon::ShieldAxe&&
        P&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();
}
void UDWWeaponAnimationComponent::ResolveFormalAxeWindow(DWCombatCommit::FActionId Action)
{
    if(!IsCurrentFormalAxe(Action)||AxePlan.GetPhase()!=EDWSpearPhase::Active)return;
    auto* P=Player();auto* Authority=UDWCombatAuthoritySubsystem::Get(P);if(!Authority)return;
    const auto Tuning=AxePlan.GetTuning();const double Damage=AxePlan.GetDamageSnapshot()*AxeSwingCounterMultiplier;
    TArray<FDWSpearHitCandidate> Targets;FDWSpearHitQuery::Collect(GetWorld(),P,P->GetActorLocation(),AxeForward,Tuning,Targets);
    for(const auto& Hit:Targets)
    {
        if(!IsCurrentFormalAxe(Action))return;FDWSpearHitCandidate Current;
        if(!FDWSpearHitQuery::Evaluate(GetWorld(),P,Hit.Target.Get(),P->GetActorLocation(),AxeForward,Tuning,Current))continue;
        FDWCombatDamageCommand C;C.Identity={Action,1,DWCombatCommit::EActionDamageKind::Melee,1};
        C.Source=P;C.Target=Current.Target;C.TargetLife=Authority->CaptureActorLife(Current.Target.Get());C.Instigator=P->GetController();C.Amount=static_cast<float>(Damage);
        C.KnockbackDirection=AxeForward;C.KnockbackDistance=static_cast<float>(Tuning.KnockbackCm);C.KnockbackSeconds=static_cast<float>(Tuning.KnockbackSeconds);
        Authority->SubmitActionDamage(C);if(!IsCurrentFormalAxe(Action))return;
    }
}
void UDWWeaponAnimationComponent::AdvanceFormalAxe(double Dt)
{
    const auto Action=AxeAction;if(!IsCurrentFormalAxe(Action)){bCombatAdvanceInterrupted=true;return;}
    double Left=Dt,Offset=0.;
    for(int32 Budget=3;Budget>0&&IsCurrentFormalAxe(Action)&&AxePlan.IsRunning();--Budget)
    {
        double Used=0.,Remaining=0.;if(!AxePlan.AdvanceCurrentPhase(Left,Used,Remaining))return;
        Offset+=Used;Left=Remaining;ElapsedSeconds=AxePlan.GetSourceSeconds();
        if(AxePlan.GetPhase()==EDWSpearPhase::Active&&Used>0.)ResolveFormalAxeWindow(Action);
        if(!IsCurrentFormalAxe(Action)){bCombatAdvanceInterrupted=true;return;}
        if(!AxePlan.IsAtBoundary())return;
        const auto Form=ReadFormalSpearForm(Offset);if(!AxePlan.EnterNextPhase(Form))return;
        if(AxePlan.GetPhase()==EDWSpearPhase::Complete){FinishAction();return;}
        const double Now=Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+Offset;
        NextAttackTime=FMath::Max(NextAttackTime,Now+AxePlan.EstimateRemainingSeconds(Form));
        if(AxePlan.GetPhase()==EDWSpearPhase::Active)
        {
            ShieldDefense.NotifyBaseForm(static_cast<uint8>(Player()->GetCombatBaseFormAtFrameOffset(Offset)));
            AxeSwingCounterMultiplier=ShieldDefense.ConsumeCounterAtSwing(); // Whiff consumes once; each victim shares this snapshot.
            ResolveFormalAxeWindow(Action);
        }
        if(!IsCurrentFormalAxe(Action)){bCombatAdvanceInterrupted=true;return;}
        if(Left<=0.)return;
    }
}
FDWShieldImpactPolicy UDWWeaponAnimationComponent::PrepareShieldImpact(double Now,EDWShieldIncomingKind Kind,FVector Incoming)const
{
    const auto* P=Player();
    if(!bShieldAxeConfigured||!bEnabled||!HasCurrentEquipmentIdentity()||SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe||!P)return {};
    return ShieldDefense.PrepareImpact(Now,Kind,P->GetActorForwardVector(),Incoming);
}
void UDWWeaponAnimationComponent::RecordPerfectShieldFacts()
{if(auto* P=Player())ShieldDefense.NotifyBaseForm(static_cast<uint8>(P->GetCombatBaseForm()));ShieldDefense.RecordParry();}
