#include "DWEnemyCombatProfile.h"
#include "DWCombatRules.h"
#include "DWCombatPlayerVitals.h"
#include "DWCombatItemRegistry.h"

namespace DWEnemyFormal
{
    bool BoxIntersectsSector(const FTransform& Transform,FVector Extent,FVector Origin,FVector Facing,
        double Radius,double Height,double Angle)
    {
        if(Transform.ContainsNaN()||Extent.ContainsNaN()||Origin.ContainsNaN()||Facing.ContainsNaN()||
            Extent.GetMin()<0.||!FMath::IsFinite(Radius)||Radius<=0.||!FMath::IsFinite(Height)||Height<=0.||
            !FMath::IsFinite(Angle)||Angle<=0.||Angle>360.)return false;
        Facing=Facing.GetSafeNormal2D();if(Facing.IsNearlyZero())return false;
        // Intersect the 3D box with the attack's horizontal slab before projecting to XY.
        // Projecting its world AABB first could hit an empty corner of a tilted hurtbox.
        FVector Corners[8];TArray<FVector2D> Points;Points.Reserve(32);
        const double Low=Origin.Z-Height*.5,High=Origin.Z+Height*.5;
        auto Add=[&](FVector P){Points.AddUnique(FVector2D(P.X-Origin.X,P.Y-Origin.Y));};
        for(int32 I=0;I<8;++I)
        {
            Corners[I]=Transform.TransformPosition(FVector((I&1)?Extent.X:-Extent.X,(I&2)?Extent.Y:-Extent.Y,(I&4)?Extent.Z:-Extent.Z));
            if(Corners[I].Z>=Low&&Corners[I].Z<=High)Add(Corners[I]);
        }
        for(int32 I=0;I<8;++I)for(int32 Bit=1;Bit<=4;Bit<<=1)
        {
            if(I&Bit)continue;const FVector A=Corners[I],B=Corners[I|Bit];const double Dz=B.Z-A.Z;
            if(FMath::Abs(Dz)<=UE_DOUBLE_SMALL_NUMBER)continue;
            for(const double Plane:{Low,High})
            {const double T=(Plane-A.Z)/Dz;if(T>=0.&&T<=1.)Add(A+(B-A)*T);}
        }
        if(Points.IsEmpty())return false;
        Points.Sort([](const FVector2D& A,const FVector2D& B){return A.X==B.X?A.Y<B.Y:A.X<B.X;});
        auto Cross=[](FVector2D A,FVector2D B){return A.X*B.Y-A.Y*B.X;};
        TArray<FVector2D> Hull;Hull.Reserve(Points.Num()*2);
        for(const auto P:Points)
        {while(Hull.Num()>=2&&Cross(Hull.Last()-Hull[Hull.Num()-2],P-Hull.Last())<=0.)Hull.Pop(EAllowShrinking::No);Hull.Add(P);}
        const int32 Lower=Hull.Num();
        for(int32 I=Points.Num()-2;I>=0;--I)
        {while(Hull.Num()>Lower&&Cross(Hull.Last()-Hull[Hull.Num()-2],Points[I]-Hull.Last())<=0.)Hull.Pop(EAllowShrinking::No);Hull.Add(Points[I]);}
        if(Hull.Num()>1)Hull.Pop(EAllowShrinking::No);
        const FVector2D Forward(Facing.X,Facing.Y);const double Half=FMath::DegreesToRadians(Angle*.5);
        const double Cosine=FMath::Cos(Half),RadiusSquared=Radius*Radius;
        auto Inside=[&](FVector2D P)
        {return P.SizeSquared()<=RadiusSquared&&(P.IsNearlyZero(UE_DOUBLE_SMALL_NUMBER)||Angle>=360.||FVector2D::DotProduct(Forward,P.GetSafeNormal())>=Cosine);};
        if(Hull.Num()==1)return Inside(Hull[0]);
        bool ContainsOrigin=Hull.Num()>=3;
        for(int32 I=0;I<Hull.Num();++I)
        {
            const FVector2D A=Hull[I],D=Hull[(I+1)%Hull.Num()]-A;
            ContainsOrigin&=Cross(D,-A)>=0.;
            if(Inside(A))return true;
            const double LengthSquared=D.SizeSquared();
            if(LengthSquared>0.&&Inside(A+D*FMath::Clamp(-FVector2D::DotProduct(A,D)/LengthSquared,0.,1.)))return true;
            for(const double Side:{-1.,1.})
            {
                const double Sin=FMath::Sin(Half)*Side;
                const FVector2D Ray(Forward.X*Cosine-Forward.Y*Sin,Forward.X*Sin+Forward.Y*Cosine);
                const double Denominator=Cross(Ray,D);if(FMath::Abs(Denominator)<=UE_DOUBLE_SMALL_NUMBER)continue;
                const double T=Cross(A,D)/Denominator,U=Cross(A,Ray)/Denominator;
                if(T>=0.&&T<=Radius&&U>=0.&&U<=1.)return true;
            }
        }
        return ContainsOrigin;
    }

    bool BuildConfig(const FDWCombatRules& Rules,EDWEnemyCombatProfile Profile,FDWEnemyCombatConfig& Out,FString& Error)
    {
        const TCHAR* Prefix=nullptr;
        switch(Profile)
        {
        case EDWEnemyCombatProfile::MoldGrunt:Prefix=TEXT("enemy.mold.");break;
        case EDWEnemyCombatProfile::YeastGrunt:Prefix=TEXT("enemy.yeast.");break;
        case EDWEnemyCombatProfile::DoughZombie:Prefix=TEXT("enemy.zombie.");break;
        case EDWEnemyCombatProfile::DoughElite:Prefix=TEXT("enemy.elite.");break;
        default:Error=TEXT("Legacy/unrecognized enemies do not consume formal four-profile rules.");return false;
        }
        FDWEnemyCombatConfig C;C.Profile=Profile;
        using U=EDWCombatRuleUnit;
        auto Read=[&](const TCHAR* Suffix,U Unit,double& Value)
        {return Rules.TryGetNumber(FName(*(FString(Prefix)+Suffix)),Unit,Value,Error);};
        if(!Read(TEXT("health"),U::Points,C.Health)||!Read(TEXT("damage"),U::Points,C.Damage)||
            !Read(TEXT("windup_s"),U::Seconds,C.WindupSeconds)||!Read(TEXT("active_s"),U::Seconds,C.ActiveSeconds)||
            !Read(TEXT("recovery_s"),U::Seconds,C.RecoverySeconds)||!Read(TEXT("extra_cooldown_s"),U::Seconds,C.ExtraCooldownSeconds)||
            !Read(TEXT("range_m"),U::Centimeters,C.RangeCm)||!Read(TEXT("speed_mps"),U::CentimetersPerSecond,C.MoveSpeedCmPerSecond)||
            !Read(TEXT("attack_arc_deg"),U::FullDegrees,C.AttackFullAngleDegrees)||!Read(TEXT("attack_height_m"),U::Centimeters,C.AttackHeightCm)||
            !Read(TEXT("knockback_multiplier"),U::Multiplier,C.KnockbackMultiplier)||!Read(TEXT("attack_knockback_multiplier"),U::Multiplier,C.AttackKnockbackMultiplier)||
            !Read(TEXT("outgoing_knockback_m"),U::Centimeters,C.OutgoingKnockbackCm)||!Read(TEXT("outgoing_knockback_s"),U::Seconds,C.OutgoingKnockbackSeconds))return false;
        const bool Elite=Profile==EDWEnemyCombatProfile::DoughElite;
        if(!Rules.TryGetNumber(Elite?TEXT("ai.elite.alert_radius_m"):TEXT("ai.humanoid.alert_radius_m"),U::Centimeters,C.AlertRadiusCm,Error)||
            !Rules.TryGetNumber(Elite?TEXT("ai.elite.disengage_distance_m"):TEXT("ai.humanoid.disengage_distance_m"),U::Centimeters,C.DisengageDistanceCm,Error)||
            !Rules.TryGetNumber(TEXT("ai.alert_delay_s"),U::Seconds,C.AlertDelaySeconds,Error)||
            !Rules.TryGetNumber(TEXT("ai.disengage_s"),U::Seconds,C.DisengageSeconds,Error)||
            !Rules.TryGetNumber(TEXT("ai.windup_lock_fraction"),U::Fraction,C.WindupLockFraction,Error)||
            !Rules.TryGetNumber(TEXT("ai.return_speed_multiplier"),U::Multiplier,C.ReturnSpeedMultiplier,Error)||
            !Rules.TryGetCount(TEXT("enemy.attack.hits_per_target"),C.HitsPerTarget,Error))return false;
        const double NativeValues[]={C.Health,C.Damage,C.WindupSeconds,C.ActiveSeconds,C.RecoverySeconds,C.ExtraCooldownSeconds,
            C.RangeCm,C.MoveSpeedCmPerSecond,C.AttackFullAngleDegrees,C.AttackHeightCm,C.KnockbackMultiplier,C.AttackKnockbackMultiplier,
            C.OutgoingKnockbackCm,C.OutgoingKnockbackSeconds,C.AlertRadiusCm,C.DisengageDistanceCm,C.AlertDelaySeconds,C.DisengageSeconds,
            C.WindupLockFraction,C.ReturnSpeedMultiplier,C.AttackCycleSeconds()};
        for(const double Value:NativeValues)if(!FMath::IsFinite(Value)||Value<0.||Value>MAX_flt||(Value>0.&&static_cast<float>(Value)==0.f))
        {Error=TEXT("Formal enemy rule cannot be represented by the native movement/presentation fields.");return false;}
        // A changed rule must not silently inherit a one-hit implementation with a contradictory limit.
        if(C.Health<=0.||C.ActiveSeconds<=0.||C.RangeCm<=0.||C.AttackHeightCm<=0.||C.AttackFullAngleDegrees<=0.||
            C.AttackFullAngleDegrees>360.||C.HitsPerTarget!=1||!FMath::IsFinite(C.AttackCycleSeconds())||C.AttackCycleSeconds()<=0.)
        {Error=TEXT("Formal enemy rules are unsupported or invalid; no legacy fallback applied.");return false;}
        Out=C;Error.Reset();return true;
    }

    bool BuildCoreDropConfig(const FDWCombatRules& Rules,FDWEnemyCoreDropConfig& Out,FString& Error)
    {
        FDWEnemyCoreDropConfig C;
        if(!Rules.TryGetNumber(TEXT("core.mold.drop_probability"),EDWCombatRuleUnit::Probability,C.MoldProbability,Error)||
            !Rules.TryGetNumber(TEXT("core.yeast.drop_probability"),EDWCombatRuleUnit::Probability,C.YeastProbability,Error)||
            !Rules.TryGetNumber(TEXT("core.pickup_radius_m"),EDWCombatRuleUnit::Centimeters,C.PickupRadiusCm,Error)||
            !Rules.TryGetCount(TEXT("core.success_drop_count"),C.Count,Error)||!Rules.TryGetCount(TEXT("core.stack_limit"),C.StackLimit,Error))return false;
        if(C.Count<1||C.StackLimit<C.Count||C.PickupRadiusCm<=0.){Error=TEXT("Invalid core pickup rules.");return false;}
        Out=C;Error.Reset();return true;
    }

    bool IsHostile(EDWEnemyCombatProfile Profile,EDWCombatBaseForm Form,bool bRememberedCombat)
    {
        switch(Profile)
        {
        case EDWEnemyCombatProfile::MoldGrunt:return bRememberedCombat||Form!=EDWCombatBaseForm::Mold;
        case EDWEnemyCombatProfile::YeastGrunt:return bRememberedCombat||Form!=EDWCombatBaseForm::Yeast;
        case EDWEnemyCombatProfile::DoughZombie:case EDWEnemyCombatProfile::DoughElite:return true;
        default:return false; // Legacy policy remains in the actor's original branch.
        }
    }

    bool DecideCoreDrop(FDWEnemyCoreDropRecord& R,EDWEnemyCombatProfile Profile,const FDWEnemyCoreDropConfig& C,
        DWCombatCommit::FReceiptId Fatal,DWCombatCommit::FEntityLife Enemy,DWCombatCommit::FEntityLife Player,
        double Sample,const FGuid& PickupId)
    {
        if(R.Decision!=EDWEnemyCoreDropDecision::Unresolved||!Fatal.Epoch||!Fatal.Id||!Enemy.Entity||!Enemy.Life)return false;
        const bool Mold=Profile==EDWEnemyCombatProfile::MoldGrunt,Yeast=Profile==EDWEnemyCombatProfile::YeastGrunt;
        FDWEnemyCoreDropRecord Candidate;Candidate.FatalReceipt=Fatal;Candidate.EnemyLife=Enemy;Candidate.PlayerLife=Player;
        if((!Mold&&!Yeast)||!Player.Entity||!Player.Life)Candidate.Decision=EDWEnemyCoreDropDecision::Ineligible;
        else
        {
            const double Probability=Mold?C.MoldProbability:C.YeastProbability;
            if(!FMath::IsFinite(Sample)||Sample<0.||Sample>=1.||!FMath::IsFinite(Probability)||Probability<0.||Probability>1.||C.Count<1)return false;
            if(Sample<Probability)
            {
                if(!PickupId.IsValid())return false;
                Candidate.Decision=EDWEnemyCoreDropDecision::SpawnPending;Candidate.PickupId=PickupId;
                Candidate.CoreItemId=Mold?FDWCombatItemRegistry::MoldCoreId():FDWCombatItemRegistry::YeastCoreId();Candidate.Count=C.Count;
            }
            else Candidate.Decision=EDWEnemyCoreDropDecision::Failed;
        }
        R=Candidate;return true;
    }
}

void FDWEnemyCombatMemory::Engage()
{bEngaged=true;bReturningHome=false;RecordDamage();}
void FDWEnemyCombatMemory::RecordDamage()
{bDisengageCandidate=false;DisengageElapsedSeconds=0.;}
bool FDWEnemyCombatMemory::ObserveDisengage(double Now,double Distance,bool Sight,bool Damage,const FDWEnemyCombatConfig& C)
{
    if(!FMath::IsFinite(Now)||Now<0.||!FMath::IsFinite(Distance)||Distance<0.||
        !FMath::IsFinite(C.DisengageSeconds)||C.DisengageSeconds<0.)return false;
    if(LastObservationTime>Now){RecordDamage();LastObservationTime=Now;return false;}
    const double Dt=LastObservationTime>=0.?Now-LastObservationTime:0.;LastObservationTime=Now;
    if(!bEngaged||Distance<=C.DisengageDistanceCm||Sight||Damage){RecordDamage();return false;}
    // Do not credit the frame before the condition was first observed.
    if(!bDisengageCandidate){bDisengageCandidate=true;DisengageElapsedSeconds=0.;}
    else DisengageElapsedSeconds+=Dt;
    if(DisengageElapsedSeconds<C.DisengageSeconds)return false;
    bEngaged=false;bReturningHome=true;RecordDamage();return true;
}
