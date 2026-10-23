#include "DWWeaponAnimationComponent.h"
#include "DWCombatPresentationTiming.h"
#include "DWCombatFeedbackComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "DWPlayerCharacter.h"
#include <limits>

const FDWWeaponAnimationClip& UDWWeaponAnimationComponent::GetPresentationClip()const
{
    // A release queued while paused is still the same committed Ready pose. Switch back only
    // when ReleaseCharge actually enters Slam, rather than on the physical key callback.
    if(IsFormalSwordChargeReady())
    {
        const auto* Set=GetSelectedSet();const auto* Hold=Set?Set->FindClip(TEXT("ChargeHold")):nullptr;
        if(Hold&&IsValid(Hold->Animation.Get())&&FMath::IsFinite(Hold->DurationSeconds)&&Hold->DurationSeconds>0.f)
            return *Hold;
    }
    return ActiveClip;
}
double UDWWeaponAnimationComponent::GetPresentationSourceSeconds()const
{
    using namespace DWCombatPresentationTiming;
    const auto& Presentation=GetPresentationClip();
    if(&Presentation!=&ActiveClip)
        return FMath::Clamp(SwordReadyPresentationPhase,0.,1.)*static_cast<double>(Presentation.DurationSeconds);
    const double Length=FMath::Max(0.,static_cast<double>(ActiveClip.DurationSeconds));
    // These two producers already sample explicit source subranges of their unique gameplay plan.
    if(bFormalBowActive||bFormalDaggerActive)return ElapsedSeconds;
    auto Fractions=[&](FVector2D Fallback)
    {
        const auto* Value=AttackSourcePhaseEnds.Find(SelectedWeapon);
        if(Value&&FMath::IsFinite(Value->X)&&FMath::IsFinite(Value->Y)&&Value->X>=0.&&Value->Y>=Value->X&&Value->Y<=1.)return *Value;
        return Fallback;
    };
    auto Melee=[&](const FDWSpearAttackPlan& Plan,FVector2D Source)
    {
        const auto& T=Plan.GetTuning();const double Clock=Plan.GetSourceSeconds();
        const double W=T.WindupSeconds,A=W+T.ActiveSeconds;
        if(Clock<=W)return MapSegment(Clock,0.,W,0.,Source.X*Length);
        if(Clock<=A)return MapSegment(Clock,W,A,Source.X*Length,Source.Y*Length);
        return MapSegment(Clock,A,A+T.RecoverySeconds,Source.Y*Length,Length);
    };
    if(bFormalSpearActive)return Melee(SpearPlan,Fractions(FVector2D(.2,.38)));
    if(bFormalAxeActive)return Melee(AxePlan,Fractions(FVector2D(.3916666666666667/.85,.475/.85)));
    if(bFormalUnarmedActive)
    {
        const auto& T=UnarmedPlan.GetTuning();
        return Melee(UnarmedPlan,Fractions(FVector2D(T.WindupSeconds/T.IntervalSeconds,(T.WindupSeconds+T.ActiveSeconds)/T.IntervalSeconds)));
    }
    auto Ranged=[&](double Clock,double W,double R)
    {
        const double Release=Fractions(FVector2D(.5,1.)).X*Length;
        return Clock<=W?MapSegment(Clock,0.,W,0.,Release):MapSegment(Clock,W,W+R,Release,Length);
    };
    if(bFormalSporeActive)return Ranged(SporePlan.GetSourceSeconds(),SporeConfig.WindupSeconds,SporeConfig.RecoverySeconds);
    if(bFormalBottleActive)return Ranged(BottlePlan.SourceSeconds(),BottleConfig.WindupSeconds,BottleConfig.RecoverySeconds);
    if(bFormalShotgunReload)return MapSegment(CombatWeaponResources.ReloadOneSeconds-ShotgunReloadRemaining,0.,CombatWeaponResources.ReloadOneSeconds,0.,Length);
    if(bFormalShotgunActive)
    {
        const double Release=Fractions(FVector2D(1./6.,1.)).X*Length;
        return ShotgunPlan.Phase==EDWShotgunPhase::Windup?MapSegment(ShotgunPlan.GetPhaseProgress(),0.,1.,0.,Release):MapSegment(ShotgunPlan.GetPhaseProgress(),0.,1.,Release,Length);
    }
    if(bFormalSwordActive)
    {
        const int32 Index=static_cast<int32>(SwordPlan.GetPhase())-static_cast<int32>(EDWSwordPhase::Charge);
        const double Defaults[]={1.6/(227./60.),1.95/(227./60.),(38./15.)/(227./60.),(167./60.)/(227./60.),1.};
        bool Valid=SwordSourcePhaseEnds.Num()==5;double Previous=0.;
        if(Valid)for(double V:SwordSourcePhaseEnds){if(!FMath::IsFinite(V)||V<Previous||V>1.){Valid=false;break;}Previous=V;}
        if(Index>=0&&Index<5)
        {
            const double Begin=Index?(Valid?SwordSourcePhaseEnds[Index-1]:Defaults[Index-1])*Length:0.;
            const double End=(Valid?SwordSourcePhaseEnds[Index]:Defaults[Index])*Length;
            return MapSegment(SwordPlan.GetPhaseProgress(),0.,1.,Begin,End);
        }
        return Length;
    }
    return ElapsedSeconds;
}
double UDWWeaponAnimationComponent::MapPresentationEventToCombatSeconds(double SourceSeconds)const
{
    using namespace DWCombatPresentationTiming;
    const double Length=FMath::Max(0.,static_cast<double>(ActiveClip.DurationSeconds));
    if(!FMath::IsFinite(SourceSeconds)||SourceSeconds<0.||SourceSeconds>Length)return std::numeric_limits<double>::infinity();
    if(bFormalSpearActive)
    {
        const auto* Value=AttackSourcePhaseEnds.Find(EDWAnimatedWeapon::Spear);
        const FVector2D F=Value&&FMath::IsFinite(Value->X)&&FMath::IsFinite(Value->Y)&&Value->X>=0.&&Value->Y>=Value->X&&Value->Y<=1.?*Value:FVector2D(.2,.38);
        const auto& T=SpearPlan.GetTuning();const double A=F.X*Length,B=F.Y*Length;
        if(SourceSeconds<=A)return MapSegment(SourceSeconds,0.,A,0.,T.WindupSeconds);
        if(SourceSeconds<=B)return MapSegment(SourceSeconds,A,B,T.WindupSeconds,T.WindupSeconds+T.ActiveSeconds);
        return MapSegment(SourceSeconds,B,Length,T.WindupSeconds+T.ActiveSeconds,T.WindupSeconds+T.ActiveSeconds+T.RecoverySeconds);
    }
    if(bFormalSporeActive)
    {
        const auto* Value=AttackSourcePhaseEnds.Find(EDWAnimatedWeapon::SporeGun);
        const double Release=Value&&FMath::IsFinite(Value->X)?FMath::Clamp(Value->X,0.,1.)*Length:0.;
        if(SourceSeconds<Release)return MapSegment(SourceSeconds,0.,Release,0.,SporeConfig.WindupSeconds);
        return MapSegment(SourceSeconds,Release,Length,SporeConfig.WindupSeconds,SporeConfig.WindupSeconds+SporeConfig.RecoverySeconds);
    }
    return SourceSeconds;
}
double UDWWeaponAnimationComponent::GetNextSwordPresentationBoundary()const
{
    const auto Phase=SwordPlan.GetPhase();
    const bool Pending=(Phase==EDWSwordPhase::Slam&&!bSwordSlamFeedbackPlayed)||(Phase==EDWSwordPhase::Sweep&&!bSwordSweepFeedbackPlayed);
    if(!Pending)return std::numeric_limits<double>::infinity();
    const double Value=Phase==EDWSwordPhase::Slam?SwordSlamFeedbackProgress:SwordSweepFeedbackProgress;
    const double Marker=FMath::IsFinite(Value)?FMath::Clamp(Value,0.,1.):1.;
    return FMath::Max(0.,(Marker-SwordPlan.GetPhaseProgress())*SwordPlan.GetPhaseDuration());
}
void UDWWeaponAnimationComponent::PublishSwordProgressEffects()
{
    if(!bFormalSwordActive||GetNextSwordPresentationBoundary()>1.e-9)return;
    const bool Slam=SwordPlan.GetPhase()==EDWSwordPhase::Slam;
    if(Slam)bSwordSlamFeedbackPlayed=true;else bSwordSweepFeedbackPlayed=true;
    if(auto* P=Player();P&&P->CombatFeedback)P->CombatFeedback->PlayFeedback(Slam?EDWCombatFeedbackCue::SwordSlam:EDWCombatFeedbackCue::SwordSweep);
}
