#include "DWWeaponAnimationComponent.h"
#include "DWCombatPresentationTiming.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatBowShotTransaction.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include <limits>

void UDWWeaponAnimationComponent::InitializeFormalBowRules()
{
    bBowConfigured=false;auto* P=Player();auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;FString Error;
    bBowConfigured=GI&&GI->GetCombatRules()&&FDWBowConfig::Build(*GI->GetCombatRules(),BowConfig,Error);
}
EDWBowForm UDWWeaponAnimationComponent::ReadFormalBowForm(double Offset)const
{
    switch(ReadFormalSpearForm(Offset))
    {case EDWSpearForm::Yeast:return EDWBowForm::Yeast;case EDWSpearForm::MoldRooted:return EDWBowForm::MoldRooted;
    case EDWSpearForm::MoldMobile:return EDWBowForm::MoldMobile;default:return EDWBowForm::Normal;}
}
bool UDWWeaponAnimationComponent::BeginFormalBow(FVector Target)
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::MoldBow);
    if(!A||!P||!bBowConfigured||!IsEnabled()||!HasCurrentEquipmentIdentity()||bActive||
        SelectedWeapon!=EDWAnimatedWeapon::MoldBow||!P->IsCombatActionAllowed()||P->IsFormalDashing()||
        Target.ContainsNaN()||GetCombatActionWorldSeconds()<NextAttackTime||ActionSerial==MAX_int32||
        !Binding||!P->GetInventory()||P->GetInventory()->CountItem(Binding->AmmoItemId)<BowConfig.AmmoPerShot)return false;
    const TWeakObjectPtr<UWorld> World(GetWorld());const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch();
    const int64 Revision=CombatWeaponResources.Revision;const uint64 Restore=CombatWeaponResources.RestoreEpoch;const int32 Serial=ActionSerial;
    P->CancelCombatActions(); // Accepted start interrupts injection; preserves old cooldowns and already released effects.
    A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||Player()!=P||!World.IsValid()||World->bIsTearingDown||
        GetWorld()!=World.Get()||!A||A->GetWorldEpoch()!=Epoch||!P->IsCombatActionAllowed()||Revision!=CombatWeaponResources.Revision||
        Restore!=CombatWeaponResources.RestoreEpoch||ActionSerial!=Serial||bActive||P->IsFormalDashing()||
        GetCombatActionWorldSeconds()<NextAttackTime||SelectedWeapon!=EDWAnimatedWeapon::MoldBow||!HasCurrentEquipmentIdentity())return false;
    const auto CurrentLife=A->CaptureActorLife(P);if(CurrentLife.Entity!=Life.Entity||CurrentLife.Life!=Life.Life)return false;
    FDWBowPlan Candidate;if(!Candidate.Start(BowConfig,ReadFormalBowForm()))return false;
    const auto Action=A->BeginAction(P);if(!Action.Id)return false;
    BowAction=Action;BowPlan=Candidate;bFormalBowActive=bActive=bPrimaryHeld=true;bBowShotCommitted=false;
    bSecondaryHeld=bCharging=bChargeReady=bReloading=false;BowPublishedAction={};TargetLocation=Target;
    ActiveSet=GetSelectedSet();ActiveClip={};
    if(const auto* Clip=ActiveSet?ActiveSet->FindClip(TEXT("Attack")):nullptr)ActiveClip=*Clip;
    ActiveClip.Role=TEXT("Attack");if(!FMath::IsFinite(ActiveClip.DurationSeconds)||ActiveClip.DurationSeconds<=0.f)ActiveClip.DurationSeconds=static_cast<float>(BowConfig.IntervalSeconds);
    ElapsedSeconds=0.;FiredEvents.Reset();++ActionSerial;
    BowForward=(Target-P->GetActorLocation()).GetSafeNormal2D();if(BowForward.IsNearlyZero())BowForward=P->GetActorForwardVector().GetSafeNormal2D();
    bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;
    BowPriorAttackLock=NextAttackTime;
    NextAttackTime=FMath::Max(BowPriorAttackLock,GetCombatActionWorldSeconds()+BowPlan.EstimateRemaining(ReadFormalBowForm()));
    return true; // Controller attaches the physical hold before scene effects may invoke callbacks.
}
void UDWWeaponAnimationComponent::PublishFormalBowStartEffects()
{
    const auto Action=BowAction;
    if(!IsCurrentFormalBow(Action)||(BowPublishedAction.Id==Action.Id&&BowPublishedAction.Epoch==Action.Epoch))return;
    BowPublishedAction=Action;auto* P=Player();
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!IsCurrentFormalBow(Action))return;
    P->GetCharacterMovement()->bOrientRotationToMovement=false;P->SetActorRotation(BowForward.Rotation());if(!IsCurrentFormalBow(Action))return;
    P->SetHarvestHeld(false);if(!IsCurrentFormalBow(Action))return;
    ResetTrails();if(!IsCurrentFormalBow(Action))return;UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndFormalBow()
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(BowAction);
    BowAction={};BowPublishedAction={};BowPlan.Cancel();bFormalBowActive=false;bBowShotCommitted=false;
    // No RetireEmitter here: a released bullet owns its independent lifetime.
}
bool UDWWeaponAnimationComponent::IsCurrentFormalBow(DWCombatCommit::FActionId Action)const
{
    const auto* P=Player();return IsValid(this)&&!IsBeingDestroyed()&&bEnabled&&bActive&&bFormalBowActive&&
        Action.Id&&BowAction.Id==Action.Id&&BowAction.Epoch==Action.Epoch&&SelectedWeapon==EDWAnimatedWeapon::MoldBow&&
        HasCurrentEquipmentIdentity()&&P&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();
}
double UDWWeaponAnimationComponent::GetBowPresentationSeconds()const
{
    const double Duration=ActiveClip.DurationSeconds;
    const double Release=FMath::Clamp(BowSourceReleaseSeconds,0.,Duration);
    const double ActionSeconds=BowPlan.GetSourceSeconds();
    if(BowPlan.Phase==EDWBowPhase::Windup||BowPlan.Phase==EDWBowPhase::Release)
        return DWCombatPresentationTiming::MapSegment(ActionSeconds,0.,BowConfig.WindupSeconds,0.,Release);
    return DWCombatPresentationTiming::MapSegment(ActionSeconds,BowConfig.WindupSeconds,
        BowConfig.WindupSeconds+BowConfig.RecoverySeconds,Release,Duration);
}
double UDWWeaponAnimationComponent::BowActionSecondsForSource(double SourceSeconds)const
{
    const double Duration=ActiveClip.DurationSeconds,Release=FMath::Clamp(BowSourceReleaseSeconds,0.,Duration);
    if(SourceSeconds<=Release)return DWCombatPresentationTiming::MapSegment(SourceSeconds,0.,Release,0.,BowConfig.WindupSeconds);
    return DWCombatPresentationTiming::MapSegment(SourceSeconds,Release,Duration,BowConfig.WindupSeconds,
        BowConfig.WindupSeconds+BowConfig.RecoverySeconds);
}
bool UDWWeaponAnimationComponent::EvaluateBowReleaseVisual(FDWWeaponGeometry& Geometry,FTransform& ActorLocal,UMaterialInterface*& Material)const
{
    const UDWWeaponAnimationSet* Set=nullptr;
    for(const auto& Entry:AnimationSets)if(IsValid(Entry)&&Entry->Weapon==EDWAnimatedWeapon::MoldBow){Set=Entry;break;}
    const auto* Clip=Set?Set->FindClip(TEXT("Attack")):nullptr;
    if(!Set||!Clip||!Set->bUseBakedVisualSamples||Clip->VisualSamples.IsEmpty()||
        !FMath::IsFinite(BowSourceReleaseSeconds)||BowSourceReleaseSeconds<=0.||BowSourceReleaseSeconds>=Clip->DurationSeconds)return false;
    const double At=BowSourceReleaseSeconds;int32 Index=0;
    for(int32 I=1;I<Clip->VisualSamples.Num()&&Clip->VisualSamples[I].TimeSeconds<=At;++I)Index=I;
    FTransform Sample=Clip->VisualSamples[Index].SecondaryTransform;
    if(Clip->VisualSamples.IsValidIndex(Index+1))
    {
        const auto& Next=Clip->VisualSamples[Index+1];
        const double Alpha=DWCombatPresentationTiming::GetNormalizedProgress(At,Clip->VisualSamples[Index].TimeSeconds,Next.TimeSeconds);
        Sample.Blend(Clip->VisualSamples[Index].SecondaryTransform,Next.SecondaryTransform,static_cast<float>(Alpha));
    }
    ActorLocal=Set->SecondaryGrip*Sample;ActorLocal.SetScale3D(ActorLocal.GetScale3D()*FMath::Max(.01f,Set->VisualScale));
    Geometry=Set->SecondaryGeometry;Index=0;
    if(!Clip->GeometryFrames.IsEmpty())
    {
        for(int32 I=1;I<Clip->GeometryFrames.Num()&&Clip->GeometryFrames[I].TimeSeconds<=At;++I)Index=I;
        const auto& A=Clip->GeometryFrames[Index];
        if(!A.SecondaryVertices.IsEmpty())Geometry.Vertices=A.SecondaryVertices;
        if(Clip->GeometryFrames.IsValidIndex(Index+1))
        {
            const auto& B=Clip->GeometryFrames[Index+1];
            if(B.SecondaryVertices.Num()==Geometry.Vertices.Num())
            {
                const double Alpha=DWCombatPresentationTiming::GetNormalizedProgress(At,A.TimeSeconds,B.TimeSeconds);
                for(int32 I=0;I<Geometry.Vertices.Num();++I)Geometry.Vertices[I]=FMath::Lerp(Geometry.Vertices[I],B.SecondaryVertices[I],Alpha);
            }
        }
    }
    Material=Set->WeaponMaterial;
    if(!ActorLocal.IsValid()||Geometry.Vertices.IsEmpty()||Geometry.Triangles.IsEmpty()||Geometry.Triangles.Num()%3!=0)return false;
    for(const auto& V:Geometry.Vertices)if(V.ContainsNaN())return false;
    for(const int32 I:Geometry.Triangles)if(!Geometry.Vertices.IsValidIndex(I))return false;
    return true;
}

double UDWWeaponAnimationComponent::GetNextBowBoundarySeconds(double Start)const
{
    if(!bFormalBowActive)return std::numeric_limits<double>::infinity();
    double Boundary=BowPlan.Remaining();
    for(int32 Index=0;Index<ActiveClip.DamageEvents.Num();++Index)
    {
        const double Time=ActiveClip.DamageEvents[Index].TimeSeconds;
        if(!FiredEvents.Contains(Index)&&FMath::IsFinite(Time)&&Time>=ElapsedSeconds&&Time<=ActiveClip.DurationSeconds)
            Boundary=FMath::Min(Boundary,FMath::Max(0.,BowActionSecondsForSource(Time)-BowPlan.GetSourceSeconds())/BowPlan.PhaseRate);
    }
    return Boundary;
}
void UDWWeaponAnimationComponent::AdvanceFormalBow(double Dt)
{
    if(!bFormalBowActive)return;
    const auto Action=BowAction;if(!IsCurrentFormalBow(Action)){bCombatAdvanceInterrupted=true;return;}
    PublishFormalBowStartEffects();if(!IsCurrentFormalBow(Action)){bCombatAdvanceInterrupted=true;return;}
    double Left=Dt,Offset=0.;
    for(int32 Budget=5;Budget>0&&IsCurrentFormalBow(Action)&&BowPlan.IsRunning();--Budget)
    {
        double Consumed=0.;if(!BowPlan.Advance(Left,Consumed))return;
        Offset+=Consumed;Left-=Consumed;ElapsedSeconds=GetBowPresentationSeconds();
        for(int32 Index=0;Index<ActiveClip.DamageEvents.Num();++Index)
        {
            const double Time=ActiveClip.DamageEvents[Index].TimeSeconds;
            // Compare in the clock that owns the boundary; inverse/forward source mapping can differ by one ulp.
            if(!FiredEvents.Contains(Index)&&FMath::IsFinite(Time)&&Time>=0.&&Time<=ActiveClip.DurationSeconds&&
                BowActionSecondsForSource(Time)<=BowPlan.GetSourceSeconds())DispatchEvent(Index);
            if(!IsCurrentFormalBow(Action)){bCombatAdvanceInterrupted=true;return;}
        }
        if(!BowPlan.AtBoundary())return;
        const auto Form=ReadFormalBowForm(Offset);
        BowBoundaryWorldSeconds=Player()->GetCombatFrameSliceStartWorldSeconds(Dt)+Offset;
        if(BowPlan.Phase==EDWBowPhase::Release)
        {
            if(!FDWCombatBowShotTransaction::Commit(*this,Offset))
            {if(IsCurrentFormalBow(Action)){bPrimaryHeld=false;FinishAction();}else bCombatAdvanceInterrupted=true;return;}
        }
        else if(!BowPlan.EnterNext(Form))return;
        if(!IsCurrentFormalBow(Action)){bCombatAdvanceInterrupted=true;return;}
        NextAttackTime=FMath::Max(BowPriorAttackLock,BowBoundaryWorldSeconds+BowPlan.EstimateRemaining(Form));
        if(BowPlan.Phase==EDWBowPhase::Complete){FinishAction();return;}
        if(BowPlan.Phase==EDWBowPhase::Gap)
        {
            Player()->GetCharacterMovement()->bOrientRotationToMovement=bOldOrient;
        }
        // Process the release at the same exact boundary, even when this slice has no time left.
        if(Left<=0.&&BowPlan.Remaining()>0.)return;
    }
}
