#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWCombatFeedbackComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

void UDWWeaponAnimationComponent::InitializeFormalSwordRules()
{
    const auto* P=Player();const auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;FString Error;
    bFormalSwordConfigured=GI&&GI->GetCombatRules()&&FDWSwordTuning::Build(*GI->GetCombatRules(),SwordTuning,Error);
}
bool UDWWeaponAnimationComponent::BeginFormalSword(FVector Target)
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsEnabled()||!bFormalSwordConfigured||!P||!A||bActive||bPrimaryHeld||SelectedWeapon!=EDWAnimatedWeapon::Greatsword||
        !P->IsCombatActionAllowed()||P->IsFormalDashing()||Target.ContainsNaN()||GetCombatActionWorldSeconds()<NextAttackTime||ActionSerial==MAX_int32)return false;
    FDWSwordAttackPlan Candidate;if(!Candidate.Start(SwordTuning))return false;
    const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch(),Restore=CombatWeaponResources.RestoreEpoch;
    const int64 Revision=CombatWeaponResources.Revision;const TWeakObjectPtr<UWorld> World(GetWorld());
    P->CancelCombatActions();A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||Player()!=P||!World.IsValid()||
        GetWorld()!=World.Get()||World->bIsTearingDown||!A||A->GetWorldEpoch()!=Epoch||!IsEnabled()||!P->IsCombatActionAllowed()||
        SelectedWeapon!=EDWAnimatedWeapon::Greatsword||CombatWeaponResources.Revision!=Revision||CombatWeaponResources.RestoreEpoch!=Restore)return false;
    const auto CurrentLife=A->CaptureActorLife(P);if(CurrentLife.Entity!=Life.Entity||CurrentLife.Life!=Life.Life)return false;
    const auto Action=A->BeginAction(P);if(!Action.Id)return false;
    SwordPlan=Candidate;SwordAction=Action;SwordPublishedAction={};bFormalSwordActive=bActive=bPrimaryHeld=true;
    bSwordVisualTail=bSwordReleasePending=bSecondaryHeld=bCharging=bChargeReady=bReloading=false;
    SwordReadyPresentationPhase=0.;
    TargetLocation=Target;SwordForward=(Target-P->GetActorLocation()).GetSafeNormal2D();
    if(SwordForward.IsNearlyZero())SwordForward=P->GetActorForwardVector().GetSafeNormal2D();
    ActiveSet=GetSelectedSet();const auto* Clip=ActiveSet?ActiveSet->FindClip(TEXT("Attack")):nullptr;
    ActiveClip=Clip?*Clip:FDWWeaponAnimationClip();ActiveClip.Role=TEXT("Attack");
    // Source duration remains authored; sampling maps each native phase into its full source interval.
    bSwordSlamFeedbackPlayed=bSwordSweepFeedbackPlayed=false;
    ElapsedSeconds=0.;FiredEvents.Reset();++ActionSerial;
    bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;
    SwordPriorAttackLock=NextAttackTime;
    NextAttackTime=FMath::Max(SwordPriorAttackLock,GetCombatActionWorldSeconds()+SwordPlan.EstimateRemainingSeconds(ReadFormalSpearForm()));
    return true; // Controller binds the physical hold before publishing scene effects.
}
void UDWWeaponAnimationComponent::PublishFormalSwordStartEffects()
{
    const auto Action=SwordAction;
    if(!IsCurrentFormalSword(Action)||(Action.Id==SwordPublishedAction.Id&&Action.Epoch==SwordPublishedAction.Epoch))return;
    SwordPublishedAction=Action;auto* P=Player();
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!IsCurrentFormalSword(Action))return;
    P->GetCharacterMovement()->bOrientRotationToMovement=false;P->SetActorRotation(SwordForward.Rotation());if(!IsCurrentFormalSword(Action))return;
    P->SetHarvestHeld(false);if(!IsCurrentFormalSword(Action))return;
    P->bSprinting=false;ResetTrails();if(!IsCurrentFormalSword(Action))return;
    UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndFormalSword()
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(SwordAction);
    SwordAction={};SwordPublishedAction={};SwordPlan.Cancel();bFormalSwordActive=bSwordVisualTail=bSwordReleasePending=bChargeReady=false;
    SwordReadyPresentationPhase=0.;
}
void UDWWeaponAnimationComponent::CancelFormalSwordFacts()
{
    // Only an actual sword action/presentation owns this reset. Generic cleanup also runs
    // for every other weapon, idle equipment switches, restore, and natural completion.
    const bool bHadSwordAction=bFormalSwordActive||bSwordVisualTail;
    EndFormalSword();
    if(!bHadSwordAction)return;
    SwordPlan={};SwordForward=FVector::ForwardVector;SwordPriorAttackLock=0.;NextAttackTime=0.;
    ActiveSet=nullptr;ActiveClip={};ActiveClip.Role=NAME_None;ActiveClip.DurationSeconds=0.f;ElapsedSeconds=0.;FiredEvents.Reset();
    // No damage receipt, target knockback, inventory or other weapon cooldown is rolled back.
}
void UDWWeaponAnimationComponent::QueueFormalSwordChargeRelease()
{
    if(!IsCurrentFormalSword(SwordAction)||!SwordPlan.IsChargeReady())return;
    // Release intent is adopted by the shared player clock at a real zero boundary.
    // Damage is never submitted from a physical-input callback or a presentation callback.
    bSwordReleasePending=true;
}
bool UDWWeaponAnimationComponent::RefreshFormalSwordReleaseAim(DWCombatCommit::FActionId Action)
{
    if(!IsCurrentFormalSword(Action)||
        (SwordPlan.GetPhase()!=EDWSwordPhase::Charge&&SwordPlan.GetPhase()!=EDWSwordPhase::Gap))return false;
    auto* P=Player();auto* Controller=Cast<APlayerController>(P->GetController());FHitResult Aim;
    const bool bHasAim=Controller&&Controller->GetHitResultUnderCursor(ECC_Visibility,false,Aim);
    if(!IsCurrentFormalSword(Action))return false;
    if(bHasAim)
    {
        const FVector Point(Aim.ImpactPoint);
        if(!Point.ContainsNaN())
        {
            const FVector Direction=(Point-P->GetActorLocation()).GetSafeNormal2D();
            if(!Direction.IsNearlyZero()){TargetLocation=Point;SwordForward=Direction;}
        }
    }
    // Refresh exactly at release, not during either effective window. A missing cursor hit
    // retains the last valid native direction (also supports headless gameplay fixtures).
    P->SetActorRotation(SwordForward.Rotation());
    return IsCurrentFormalSword(Action); // Rotation can synchronously notify overlaps.
}
bool UDWWeaponAnimationComponent::IsCurrentFormalSword(DWCombatCommit::FActionId Action)const
{
    const auto* P=Player();return IsValid(this)&&!IsBeingDestroyed()&&bActive&&bFormalSwordActive&&Action.Id&&
        Action.Id==SwordAction.Id&&Action.Epoch==SwordAction.Epoch&&SelectedWeapon==EDWAnimatedWeapon::Greatsword&&
        IsEnabled()&&P&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();
}
bool UDWWeaponAnimationComponent::IsFormalSwordGap()const
{return bFormalSwordActive&&SwordPlan.GetPhase()==EDWSwordPhase::Gap;}
bool UDWWeaponAnimationComponent::AllowsFormalSwordDashInterruption()const
{return !bFormalSwordActive||IsFormalSwordGap();}
bool UDWWeaponAnimationComponent::ApplyFormalSwordMovementPress(const FDWCombatInputPress& Press)
{
    if(!IsCurrentFormalSword(SwordAction)||SwordPlan.GetPhase()!=EDWSwordPhase::Charge||!PrimaryInputOrigin.IsSet()||
        Press.Action!=TEXT("SwordMoveCancel")||!Press.Edge.bDown||Press.Edge.bDuringSuspension||
        Press.Edge.Context!=PrimaryInputOrigin->Edge.Context||Press.Edge.Sequence<=PrimaryInputOrigin->Edge.Sequence)return false;
    // Movement input is ignored while charging/ready. It never cancels or translates this stance.
    return false;
}
bool UDWWeaponAnimationComponent::ApplyFormalSwordGapMovement()
{
    auto* P=Player();if(!IsCurrentFormalSword(SwordAction)||!IsFormalSwordGap()||!P||P->IsFormallyRooted())return false;
    CancelAction();return true;
}
void UDWWeaponAnimationComponent::UpdateFormalSwordChargeAim(FVector Target)
{
    if(!IsCurrentFormalSword(SwordAction)||SwordPlan.GetPhase()!=EDWSwordPhase::Charge||Target.ContainsNaN())return;
    auto* P=Player();const FVector Direction=(Target-P->GetActorLocation()).GetSafeNormal2D();if(Direction.IsNearlyZero())return;
    TargetLocation=Target;SwordForward=Direction;P->SetActorRotation(Direction.Rotation());
}
void UDWWeaponAnimationComponent::UpdateFormalSwordGapAim(FVector Target)
{
    if(!IsCurrentFormalSword(SwordAction)||!IsFormalSwordGap()||Target.ContainsNaN())return;
    auto* P=Player();const FVector Direction=(Target-P->GetActorLocation()).GetSafeNormal2D();if(Direction.IsNearlyZero())return;
    TargetLocation=Target;SwordForward=Direction;P->SetActorRotation(Direction.Rotation()); // Last operation may notify overlaps.
}
void UDWWeaponAnimationComponent::ResolveFormalSwordWindow(DWCombatCommit::FActionId Action)
{
    if(!IsCurrentFormalSword(Action)||!SwordPlan.GetDamagePhase())return;
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);if(!A)return;
    const auto Tuning=SwordPlan.GetTuning();const uint64 Phase=SwordPlan.GetDamagePhase();
    const double Arc=SwordPlan.GetArc(),Damage=SwordPlan.GetDamage();
    TArray<FDWSpearHitCandidate> Targets;FDWSpearHitQuery::CollectSector(GetWorld(),P,P->GetActorLocation(),SwordForward,Tuning.RangeCm,Arc,Targets);
    for(const auto& Candidate:Targets)
    {
        if(!IsCurrentFormalSword(Action)||SwordPlan.GetDamagePhase()!=Phase)return;
        FDWSpearHitCandidate Current;
        if(!FDWSpearHitQuery::EvaluateSector(GetWorld(),P,Candidate.Target.Get(),P->GetActorLocation(),SwordForward,Tuning.RangeCm,Arc,Current))continue;
        A=UDWCombatAuthoritySubsystem::Get(P);if(!A)return;
        FDWCombatDamageCommand C;C.Identity={Action,Phase,DWCombatCommit::EActionDamageKind::Melee,1};
        C.Source=P;C.Target=Current.Target;C.TargetLife=A->CaptureActorLife(Current.Target.Get());C.Instigator=P->GetController();
        C.Amount=static_cast<float>(Damage);C.KnockbackDistance=static_cast<float>(Tuning.KnockbackCm);
        C.KnockbackSeconds=static_cast<float>(Tuning.KnockbackSeconds);C.KnockbackDirection=(Current.Target->GetActorLocation()-P->GetActorLocation()).GetSafeNormal2D();
        if(C.KnockbackDirection.IsNearlyZero())C.KnockbackDirection=SwordForward;
        A->SubmitActionDamage(C);if(!IsCurrentFormalSword(Action))return;
    }
}
void UDWWeaponAnimationComponent::AdvanceSwordReadyPresentation(double GameSeconds)
{
    if(!IsFormalSwordWaitingForRelease()||!FMath::IsFinite(GameSeconds)||GameSeconds<=0.||&GetPresentationClip()==&ActiveClip)return;
    const double Loop=FMath::IsFinite(SwordReadyLoopSeconds)&&SwordReadyLoopSeconds>=.001?SwordReadyLoopSeconds:1.;
    // Only leftover game time already admitted by the shared clock reaches this branch.
    // Normalize before adding so even a very large delta cannot overflow or emit events.
    SwordReadyPresentationPhase=FMath::Fmod(SwordReadyPresentationPhase+FMath::Fmod(GameSeconds,Loop)/Loop,1.);
}
void UDWWeaponAnimationComponent::AdvanceFormalSword(double Dt)
{
    const auto Action=SwordAction;if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
    double Left=Dt,Offset=0.;
    if(SwordPlan.IsChargeReady())
    {
        if(!bChargeReady)
        {
            bChargeReady=true;PublishFormalSwordChargeReadyEffects();
            if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
        }
        if(!bSwordReleasePending){AdvanceSwordReadyPresentation(Left);return;}
        if(!RefreshFormalSwordReleaseAim(Action)){bCombatAdvanceInterrupted=true;return;}
        const auto Form=ReadFormalSpearForm(0.);
        if(!SwordPlan.ReleaseCharge(Form))return;
        SwordReadyPresentationPhase=0.;
        bSwordReleasePending=bChargeReady=false;
        NextAttackTime=FMath::Max(SwordPriorAttackLock,Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+SwordPlan.EstimateRemainingSeconds(Form));
        PublishSwordProgressEffects();
        if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
        ResolveFormalSwordWindow(Action);
        if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
    }
    for(int32 Budget=5;Budget>0&&IsCurrentFormalSword(Action)&&SwordPlan.IsRunning();--Budget)
    {
        double Consumed=0.,Remaining=0.;if(!SwordPlan.AdvanceCurrentPhase(Left,Consumed,Remaining))return;
        Offset+=Consumed;Left=Remaining;
        PublishSwordProgressEffects();
        if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
        ElapsedSeconds=FMath::Min(ElapsedSeconds+Consumed,static_cast<double>(ActiveClip.DurationSeconds));
        if(SwordPlan.GetDamagePhase()&&Consumed>0.)ResolveFormalSwordWindow(Action);
        if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
        if(!SwordPlan.IsAtBoundary())return;
        if(SwordPlan.IsChargeReady())
        {
            if(!bChargeReady)
            {
                bChargeReady=true;PublishFormalSwordChargeReadyEffects();
                if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
            }
            AdvanceSwordReadyPresentation(Left);
            return; // Only presentation loops; held time cannot flow into an attack.
        }
        // Both releases sample the mouse exactly at entry; active windows retain that direction.
        if(SwordPlan.GetPhase()==EDWSwordPhase::Gap&&
            !RefreshFormalSwordReleaseAim(Action)){bCombatAdvanceInterrupted=true;return;}
        const auto Form=ReadFormalSpearForm(Offset);if(!SwordPlan.EnterNextPhase(Form))return;
        const double Now=Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+Offset;
        NextAttackTime=FMath::Max(SwordPriorAttackLock,Now+SwordPlan.EstimateRemainingSeconds(Form));
        if(SwordPlan.GetPhase()==EDWSwordPhase::Complete)
        {
            const double SourceTime=ActiveClip.DurationSeconds;EndFormalSword();bActive=bCharging=bChargeReady=bReloading=false;
            if(auto* Owner=Player())if(auto* Movement=Owner->GetCharacterMovement())Movement->bOrientRotationToMovement=bOldOrient;
            ElapsedSeconds=SourceTime;bSwordVisualTail=ActiveClip.Animation&&ElapsedSeconds<ActiveClip.DurationSeconds;
            if(bSwordVisualTail)AdvanceFormalSwordVisualTail(Left);else ResetTrails();return;
        }
        // All active-window queries use the native direction frozen at the release boundary.
        if(SwordPlan.GetDamagePhase())
        {
            PublishSwordProgressEffects();
            if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
            ResolveFormalSwordWindow(Action);
        }
        if(!IsCurrentFormalSword(Action)){bCombatAdvanceInterrupted=true;return;}
        if(Left<=0.&&!SwordPlan.IsAtBoundary())return;
    }
}
void UDWWeaponAnimationComponent::AdvanceFormalSwordVisualTail(double Dt)
{
    if(!bSwordVisualTail||!FMath::IsFinite(Dt)||Dt<0.)return;
    ElapsedSeconds=FMath::Min(ElapsedSeconds+Dt,static_cast<double>(ActiveClip.DurationSeconds));
    if(ElapsedSeconds>=ActiveClip.DurationSeconds){bSwordVisualTail=false;ResetTrails();}
}
