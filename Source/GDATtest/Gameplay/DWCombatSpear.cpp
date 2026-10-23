#include "DWCombatSpear.h"
#include "DWCombatRules.h"
#include "DWDamageVolumeLibrary.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "Components/PrimitiveComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
namespace DWSpear
{
static bool ValidForm(EDWSpearForm F){return static_cast<uint8>(F)<=static_cast<uint8>(EDWSpearForm::MoldRooted);}
static bool Positive(double V){return FMath::IsFinite(V)&&V>0.;}
static bool TargetAllowed(AActor* A)
{
    if(!IsValid(A)||A->IsActorBeingDestroyed()||!A->CanBeDamaged()||!A->GetActorEnableCollision())return false;
    if(const auto* E=Cast<ADWEnemyCharacter>(A))return E->IsAlive();
    if(const auto* N=Cast<ADWEnemyNest>(A))return !N->bAmbientSpawner&&N->IsAlive();
    return false;
}
static bool InAngle(FVector Point,FVector Forward,double Degrees)
{
    const FVector Direction=Point.GetSafeNormal2D();
    return Degrees>=360.||Direction.IsNearlyZero()||
        FVector::DotProduct(Forward,Direction)>=FMath::Cos(FMath::DegreesToRadians(Degrees*.5))-1.e-8;
}
// Closest point in the 3D range ball restricted by an XY angle; no invented height parameter.
static FVector ClosestSectorPoint(FVector Point,FVector Forward,double Range,double Degrees)
{
    FVector Best=Point;
    if(!InAngle(Point,Forward,Degrees))
    {
        double Distance=MAX_dbl;
        for(double Side:{-1.,1.})
        {
            const FVector Ray=Forward.RotateAngleAxis(Degrees*.5*Side,FVector::UpVector);
            FVector Candidate=Ray*FMath::Max(0.,FVector::DotProduct(Point,Ray));Candidate.Z=Point.Z;
            Candidate=Candidate.GetClampedToMaxSize(Range);
            const double D=FVector::DistSquared(Point,Candidate);if(D<Distance){Distance=D;Best=Candidate;}
        }
        return Best;
    }
    return Best.GetClampedToMaxSize(Range);
}
// Clip each real OBB face by the two angular half-planes, then measure the clipped surface.
// A new cap lies in a plane through the origin: if the origin is outside the box its closest
// point is on a cap edge, already on these clipped original faces. The caller handles inside.
static bool BoxSectorPoint(const UBoxComponent* Box,FVector Origin,FVector Forward,double Range,double Degrees,FVector& Out)
{
    if(Degrees>180.)
    {
        FVector A,B;const bool HasA=BoxSectorPoint(Box,Origin,Forward.RotateAngleAxis(Degrees*.25,FVector::UpVector),Range,Degrees*.5,A);
        const bool HasB=BoxSectorPoint(Box,Origin,Forward.RotateAngleAxis(-Degrees*.25,FVector::UpVector),Range,Degrees*.5,B);
        if(!HasA&&!HasB)return false;
        Out=HasA&&(!HasB||FVector::DistSquared(A,Origin)<FVector::DistSquared(B,Origin))?A:B;return true;
    }
    const FVector Extent=Box->GetUnscaledBoxExtent();const FTransform Transform=Box->GetComponentTransform();
    FVector V[8];for(int32 I=0;I<8;++I)V[I]=Transform.TransformPosition(FVector((I&1)?Extent.X:-Extent.X,(I&2)?Extent.Y:-Extent.Y,(I&4)?Extent.Z:-Extent.Z))-Origin;
    static const int32 Faces[6][4]={{0,2,6,4},{1,5,7,3},{0,4,5,1},{2,3,7,6},{0,1,3,2},{4,6,7,5}};
    const FVector Left=Forward.RotateAngleAxis(-Degrees*.5,FVector::UpVector),Right=Forward.RotateAngleAxis(Degrees*.5,FVector::UpVector);
    const FVector Normals[2]={FVector(-Left.Y,Left.X,0.),FVector(Right.Y,-Right.X,0.)};
    double Best=MAX_dbl;FVector Closest=FVector::ZeroVector;
    auto Consider=[&](FVector Point){const double D=Point.SizeSquared();if(D<Best){Best=D;Closest=Point;}};
    for(const auto& Face:Faces)
    {
        TArray<FVector,TInlineAllocator<8>> Polygon;for(int32 I:Face)Polygon.Add(V[I]);
        for(FVector N:Normals)
        {
            TArray<FVector,TInlineAllocator<8>> Clipped;
            for(int32 I=0;I<Polygon.Num();++I)
            {
                const FVector A=Polygon[I],B=Polygon[(I+1)%Polygon.Num()];const double Da=FVector::DotProduct(N,A),Db=FVector::DotProduct(N,B);
                if(Da>=0.)Clipped.Add(A);
                if((Da>=0.)!=(Db>=0.))Clipped.Add(A+(B-A)*(Da/(Da-Db)));
            }
            Polygon=MoveTemp(Clipped);
        }
        for(int32 I=0;I<Polygon.Num();++I)
        {
            Consider(Polygon[I]);Consider(FMath::ClosestPointOnSegment(FVector::ZeroVector,Polygon[I],Polygon[(I+1)%Polygon.Num()]));
            if(I>0&&I+1<Polygon.Num()&&FVector::CrossProduct(Polygon[I]-Polygon[0],Polygon[I+1]-Polygon[0]).SizeSquared()>UE_DOUBLE_SMALL_NUMBER)
                Consider(FMath::ClosestPointOnTriangleToPoint(FVector::ZeroVector,Polygon[0],Polygon[I],Polygon[I+1]));
        }
    }
    if(Best>Range*Range)return false;Out=Origin+Closest;return true;
}
static bool ShapeSectorPoint(UPrimitiveComponent* C,FVector Closest,FVector Origin,FVector Forward,double Range,double Degrees,FVector& Out)
{
    if(InAngle(Closest-Origin,Forward,Degrees)&&FVector::DistSquared(Closest,Origin)<=Range*Range){Out=Closest;return true;}
    if(const auto* Box=Cast<UBoxComponent>(C))return BoxSectorPoint(Box,Origin,Forward,Range,Degrees,Out);
    FVector Center;double Radius=0.;
    if(const auto* Sphere=Cast<USphereComponent>(C)){Center=Sphere->GetComponentLocation();Radius=Sphere->GetScaledSphereRadius();}
    else if(const auto* Capsule=Cast<UCapsuleComponent>(C))
    {
        // Native character capsules are upright; arbitrary tilted custom hurtboxes use explicit boxes.
        if(FMath::Abs(Capsule->GetUpVector().Z)<1.-1.e-6)return false;
        Radius=Capsule->GetScaledCapsuleRadius();Center=Capsule->GetComponentLocation();
        const double HalfAxis=FMath::Max(0.,static_cast<double>(Capsule->GetScaledCapsuleHalfHeight())-Radius);
        Center.Z=FMath::Clamp(Origin.Z,Center.Z-HalfAxis,Center.Z+HalfAxis);
    }
    else return false;
    const FVector SectorPoint=ClosestSectorPoint(Center-Origin,Forward,Range,Degrees)+Origin;
    if(FVector::DistSquared(Center,SectorPoint)>Radius*Radius)return false;Out=SectorPoint;return true;
}
}
bool FDWSpearTuning::IsValid() const
{
    const double Values[]={Damage,RangeCm,FullArcDegrees,IntervalSeconds,KnockbackSeconds,
        ActiveSeconds,YeastDamageMultiplier,YeastRateMultiplier,RootedDamageMultiplier};
    for(double V:Values)if(!DWSpear::Positive(V)||V>MAX_flt)return false;
    return FullArcDegrees<=360.&&FMath::IsFinite(WindupSeconds)&&WindupSeconds>=0.&&
        FMath::IsFinite(RecoverySeconds)&&RecoverySeconds>=0.&&
        FMath::IsFinite(KnockbackCm)&&KnockbackCm>=0.&&KnockbackCm<=MAX_flt&&
        FMath::IsFinite(EdgeBandFraction)&&EdgeBandFraction>=0.&&EdgeBandFraction<=1.&&
        FMath::IsFinite(MinimumDamageFraction)&&MinimumDamageFraction>=0.&&MinimumDamageFraction<=1.&&
        FMath::IsFinite(SwitchLockSeconds)&&SwitchLockSeconds>=0.&&SwitchLockSeconds<=MAX_flt&&
        FMath::IsNearlyEqual(WindupSeconds+ActiveSeconds+RecoverySeconds,IntervalSeconds,1.e-8)&&
        FMath::IsFinite(Damage*FMath::Max(YeastDamageMultiplier,RootedDamageMultiplier))&&
        Damage*FMath::Max(YeastDamageMultiplier,RootedDamageMultiplier)<=MAX_flt;
}
bool FDWSpearTuning::Build(const FDWCombatRules& Rules,FDWSpearTuning& Out,FString& Error)
{
    FDWSpearTuning C;
    struct FRead{const TCHAR* Id;EDWCombatRuleUnit Unit;double* Value;};
    const FRead Reads[]={
        {TEXT("weapon.spear.damage"),EDWCombatRuleUnit::Points,&C.Damage},
        {TEXT("weapon.spear.range_m"),EDWCombatRuleUnit::Centimeters,&C.RangeCm},
        {TEXT("weapon.spear.arc_deg"),EDWCombatRuleUnit::FullDegrees,&C.FullArcDegrees},
        {TEXT("weapon.spear.interval_s"),EDWCombatRuleUnit::Seconds,&C.IntervalSeconds},
        {TEXT("weapon.spear.edge_band_fraction"),EDWCombatRuleUnit::Fraction,&C.EdgeBandFraction},
        {TEXT("weapon.spear.min_damage_fraction"),EDWCombatRuleUnit::Fraction,&C.MinimumDamageFraction},
        {TEXT("weapon.spear.knockback_m"),EDWCombatRuleUnit::Centimeters,&C.KnockbackCm},
        {TEXT("weapon.knockback_duration_s"),EDWCombatRuleUnit::Seconds,&C.KnockbackSeconds},
        {TEXT("weapon.spear.windup_s"),EDWCombatRuleUnit::Seconds,&C.WindupSeconds},
        {TEXT("weapon.spear.active_s"),EDWCombatRuleUnit::Seconds,&C.ActiveSeconds},
        {TEXT("weapon.spear.recovery_s"),EDWCombatRuleUnit::Seconds,&C.RecoverySeconds},
        {TEXT("yeast.weapon.spear.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.YeastDamageMultiplier},
        {TEXT("yeast.weapon.spear.rate_multiplier"),EDWCombatRuleUnit::Multiplier,&C.YeastRateMultiplier},
        {TEXT("mold.root.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.RootedDamageMultiplier},
        {TEXT("weapon.switch_lock_s"),EDWCombatRuleUnit::Seconds,&C.SwitchLockSeconds}};
    for(const FRead& R:Reads)if(!Rules.TryGetNumber(R.Id,R.Unit,*R.Value,Error))return false;
    int32 Hits=0;if(!Rules.TryGetCount(TEXT("weapon.hits_per_target_per_stage"),Hits,Error))return false;
    if(Hits!=1||!C.IsValid()){Error=TEXT("Spear requires one hit per target/stage and finite rules with windup+active+recovery=interval.");return false;}
    Out=C;Error.Reset();return true;
}
double FDWSpearTuning::DamageMultiplier(EDWSpearForm F) const
{return F==EDWSpearForm::Yeast?YeastDamageMultiplier:F==EDWSpearForm::MoldRooted?RootedDamageMultiplier:1.;}
double FDWSpearTuning::RateMultiplier(EDWSpearForm F) const
{return F==EDWSpearForm::Yeast?YeastRateMultiplier:1.;}
bool FDWSpearTuning::DamageAtDistance(double D,double Base,double& Out) const
{
    if(!IsValid()||!FMath::IsFinite(D)||D<0.||D>RangeCm||!FMath::IsFinite(Base)||Base<0.)return false;
    const double Band=RangeCm*(1.-EdgeBandFraction);
    const double Alpha=Band>0.?FMath::Clamp(D/Band,0.,1.):1.;
    const double Value=Base*(MinimumDamageFraction+(1.-MinimumDamageFraction)*Alpha);
    if(!FMath::IsFinite(Value)||Value>MAX_flt)return false;
    Out=Value;return true;
}
bool FDWSpearSourceTiming::Validate(const FDWSpearTuning& T,FString& Error) const
{
    if(!T.IsValid()||!DWSpear::Positive(DurationSeconds)||!FMath::IsFinite(ContactBeginSeconds)||
        !FMath::IsFinite(ContactEndSeconds)||ContactBeginSeconds<0.||ContactEndSeconds<ContactBeginSeconds||ContactEndSeconds>DurationSeconds)
    {Error=TEXT("Invalid spear source timing or rules.");return false;}
    // User-approved timing policy: gameplay duration retimes the complete approved source.
    // Source identity/length and marker truth remain checked by the component binding;
    // contact seconds do not have to masquerade as unscaled gameplay-window seconds.
    Error.Reset();return true;
}
bool FDWSpearAttackPlan::Start(const FDWSpearTuning& T,EDWSpearForm Form)
{
    if(IsRunning()||!T.IsValid()||!DWSpear::ValidForm(Form))return false;
    Tuning=T;Phase=EDWSpearPhase::Windup;PhaseSourceElapsed=0.;PhaseRate=T.RateMultiplier(Form);DamageSnapshot=0.;return true;
}
bool FDWSpearAttackPlan::IsRunning() const
{return Phase==EDWSpearPhase::Windup||Phase==EDWSpearPhase::Active||Phase==EDWSpearPhase::Recovery;}
double FDWSpearAttackPlan::PhaseDuration() const
{return Phase==EDWSpearPhase::Windup?Tuning.WindupSeconds:Phase==EDWSpearPhase::Active?Tuning.ActiveSeconds:Phase==EDWSpearPhase::Recovery?Tuning.RecoverySeconds:0.;}
bool FDWSpearAttackPlan::IsAtBoundary() const {return IsRunning()&&PhaseSourceElapsed>=PhaseDuration();}
double FDWSpearAttackPlan::GetPhaseRemainingSeconds() const
{return IsRunning()?FMath::Max(0.,PhaseDuration()-PhaseSourceElapsed)/PhaseRate:0.;}
bool FDWSpearAttackPlan::AdvanceCurrentPhase(double Seconds,double& Consumed,double& Remaining)
{
    if(!IsRunning()||!FMath::IsFinite(Seconds)||Seconds<0.)return false;
    const double ToEnd=GetPhaseRemainingSeconds(),Used=FMath::Min(Seconds,ToEnd);
    if(Seconds>=ToEnd)PhaseSourceElapsed=PhaseDuration();
    else PhaseSourceElapsed=FMath::Min(PhaseDuration(),PhaseSourceElapsed+Used*PhaseRate);
    Consumed=Used;Remaining=Seconds-Used;return true;
}
bool FDWSpearAttackPlan::EnterNextPhase(EDWSpearForm Form)
{
    if(!IsAtBoundary()||!DWSpear::ValidForm(Form))return false;
    if(Phase==EDWSpearPhase::Windup){Phase=EDWSpearPhase::Active;DamageSnapshot=Tuning.Damage*Tuning.DamageMultiplier(Form);}
    else if(Phase==EDWSpearPhase::Active)Phase=EDWSpearPhase::Recovery;
    else Phase=EDWSpearPhase::Complete;
    PhaseSourceElapsed=0.;PhaseRate=Tuning.RateMultiplier(Form);return true;
}
void FDWSpearAttackPlan::Cancel(){if(IsRunning())Phase=EDWSpearPhase::Cancelled;}
double FDWSpearAttackPlan::GetSourceSeconds() const
{
    if(Phase==EDWSpearPhase::Windup)return PhaseSourceElapsed;
    if(Phase==EDWSpearPhase::Active)return Tuning.WindupSeconds+PhaseSourceElapsed;
    if(Phase==EDWSpearPhase::Recovery)return Tuning.WindupSeconds+Tuning.ActiveSeconds+PhaseSourceElapsed;
    return Phase==EDWSpearPhase::Complete?Tuning.IntervalSeconds:0.;
}
double FDWSpearAttackPlan::EstimateRemainingSeconds(EDWSpearForm Form) const
{
    if(!IsRunning()||!DWSpear::ValidForm(Form))return 0.;
    const double Future=Phase==EDWSpearPhase::Windup?Tuning.ActiveSeconds+Tuning.RecoverySeconds:Phase==EDWSpearPhase::Active?Tuning.RecoverySeconds:0.;
    return GetPhaseRemainingSeconds()+Future/Tuning.RateMultiplier(Form);
}
bool FDWSpearHitQuery::Evaluate(UWorld* World,AActor* Source,AActor* Target,const FVector& Origin,
    const FVector& Facing,const FDWSpearTuning& T,FDWSpearHitCandidate& Out)
{return T.IsValid()&&EvaluateSector(World,Source,Target,Origin,Facing,T.RangeCm,T.FullArcDegrees,Out);}
void FDWSpearHitQuery::Collect(UWorld* World,AActor* Source,const FVector& Origin,const FVector& Facing,
    const FDWSpearTuning& T,TArray<FDWSpearHitCandidate>& Out)
{Out.Reset();if(T.IsValid())CollectSector(World,Source,Origin,Facing,T.RangeCm,T.FullArcDegrees,Out);}
bool FDWSpearHitQuery::EvaluateSector(UWorld* World,AActor* Source,AActor* Target,const FVector& Origin,
    const FVector& Facing,double RangeCm,double FullArc,FDWSpearHitCandidate& Out)
{
    if(!World||World->bIsTearingDown||!IsValid(Source)||Source->IsActorBeingDestroyed()||Source->GetWorld()!=World||
        Target==Source||!DWSpear::TargetAllowed(Target)||Target->GetWorld()!=World||(!FMath::IsFinite(RangeCm)||RangeCm<=0.||RangeCm>MAX_flt||!FMath::IsFinite(FullArc)||FullArc<=0.||FullArc>360.)||Origin.ContainsNaN()||Facing.ContainsNaN())return false;
    const FVector Forward=Facing.GetSafeNormal2D();if(Forward.IsNearlyZero())return false;
    TInlineComponentArray<UPrimitiveComponent*> Components(Target);double Best=MAX_dbl;TArray<FVector,TInlineAllocator<8>> Witnesses;
    for(UPrimitiveComponent* C:Components)
    {
        if(!UDWDamageVolumeLibrary::IsDamageHitComponent(C))continue;
        FVector Point=Origin;const double D=C->GetClosestPointOnCollision(Origin,Point);
        if(!FMath::IsFinite(D)||D<0.||Point.ContainsNaN())continue;
        if(D==0.)
        {
            // UE also returns zero for unsupported non-convex shapes. Require actual containment.
            if(!C->OverlapComponent(Origin,FQuat::Identity,FCollisionShape::MakeSphere(UE_KINDA_SMALL_NUMBER)))continue;
            Point=Origin;
        }
        Best=FMath::Min(Best,D);
        FVector Witness;if(DWSpear::ShapeSectorPoint(C,Point,Origin,Forward,RangeCm,FullArc,Witness))Witnesses.Add(Witness);
    }
    if(Best>RangeCm)return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWFormalSpearSight),false,Source);Params.AddIgnoredActor(Target);
    for(FVector Witness:Witnesses)
    {
        FHitResult Block;if(World->LineTraceSingleByChannel(Block,Origin,Witness,ECC_Visibility,Params))continue;
        Out.Target=Target;Out.ClosestPoint=Witness;Out.EdgeDistanceCm=Best;return true;
    }
    return false;
}
void FDWSpearHitQuery::CollectSector(UWorld* World,AActor* Source,const FVector& Origin,const FVector& Facing,
    double RangeCm,double FullArc,TArray<FDWSpearHitCandidate>& Out)
{
    Out.Reset();if(!World||World->bIsTearingDown||!IsValid(Source)||(!FMath::IsFinite(RangeCm)||RangeCm<=0.||RangeCm>MAX_flt||!FMath::IsFinite(FullArc)||FullArc<=0.||FullArc>360.)||Origin.ContainsNaN()||Facing.ContainsNaN())return;
    TArray<FOverlapResult> Hits;FCollisionQueryParams Params(SCENE_QUERY_STAT(DWFormalSpear),false,Source);
    World->OverlapMultiByObjectType(Hits,Origin,FQuat::Identity,FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),FCollisionShape::MakeSphere(static_cast<float>(RangeCm)),Params);
    TSet<TWeakObjectPtr<AActor>> Seen;
    for(const FOverlapResult& Hit:Hits)
    {
        AActor* Target=Hit.GetActor();if(!Target||Seen.Contains(Target)||!UDWDamageVolumeLibrary::IsDamageHitComponent(Hit.GetComponent()))continue;
        Seen.Add(Target);FDWSpearHitCandidate C;if(EvaluateSector(World,Source,Target,Origin,Facing,RangeCm,FullArc,C))Out.Add(C);
    }
    Out.Sort([](const FDWSpearHitCandidate& A,const FDWSpearHitCandidate& B){return A.EdgeDistanceCm<B.EdgeDistanceCm;});
}
