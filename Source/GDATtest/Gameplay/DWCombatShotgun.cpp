#include "DWCombatShotgun.h"
#include "DWCombatRules.h"
#include "DWDamageVolumeLibrary.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool FDWShotgunConfig::IsValid()const
{
    const double Values[]={Damage,Range,FullRange,FarFraction,SpreadDegrees,NearKnockback,FarKnockback,KnockbackSeconds,
        Windup,Recovery,Interval,YeastDamage,YeastRate,YeastRange,RootDamage,RootRate,ReloadMoveMultiplier};
    for(double V:Values)if(!FMath::IsFinite(V)||V<0.||V>MAX_flt)return false;
    return Pellets>0&&Damage>0.&&Range>0.&&FullRange<Range&&FullRange<YeastRange&&FarFraction<=1.&&SpreadDegrees<=360.&&
        KnockbackSeconds>0.&&Windup>0.&&Recovery>0.&&Interval>=Windup+Recovery&&YeastDamage>0.&&YeastRate>0.&&RootDamage>0.&&RootRate>0.&&
        ReloadMoveMultiplier>0.&&ReloadMoveMultiplier<=1.&&Damage*Pellets*FMath::Max(YeastDamage,RootDamage)<=MAX_flt;
}
bool FDWShotgunConfig::Build(const FDWCombatRules& Rules,FDWShotgunConfig& Out,FString& Error)
{
    FDWShotgunConfig C;
    struct R{const TCHAR* Key;EDWCombatRuleUnit Unit;double* Value;};
    const R Reads[]={
        {TEXT("weapon.shotgun.pellet_damage"),EDWCombatRuleUnit::Points,&C.Damage},
        {TEXT("weapon.shotgun.range_m"),EDWCombatRuleUnit::Centimeters,&C.Range},
        {TEXT("weapon.shotgun.full_damage_range_m"),EDWCombatRuleUnit::Centimeters,&C.FullRange},
        {TEXT("weapon.shotgun.far_fraction"),EDWCombatRuleUnit::Fraction,&C.FarFraction},
        {TEXT("weapon.shotgun.spread_angle_deg"),EDWCombatRuleUnit::FullDegrees,&C.SpreadDegrees},
        {TEXT("weapon.shotgun.near_knockback_m"),EDWCombatRuleUnit::Centimeters,&C.NearKnockback},
        {TEXT("weapon.shotgun.far_knockback_m"),EDWCombatRuleUnit::Centimeters,&C.FarKnockback},
        {TEXT("weapon.knockback_duration_s"),EDWCombatRuleUnit::Seconds,&C.KnockbackSeconds},
        {TEXT("weapon.shotgun.windup_s"),EDWCombatRuleUnit::Seconds,&C.Windup},
        {TEXT("weapon.shotgun.recovery_s"),EDWCombatRuleUnit::Seconds,&C.Recovery},
        {TEXT("weapon.shotgun.reload_move_multiplier"),EDWCombatRuleUnit::Multiplier,&C.ReloadMoveMultiplier},
        {TEXT("weapon.shotgun.interval_s"),EDWCombatRuleUnit::Seconds,&C.Interval},
        {TEXT("yeast.weapon.shotgun.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.YeastDamage},
        {TEXT("yeast.weapon.shotgun.rate_multiplier"),EDWCombatRuleUnit::Multiplier,&C.YeastRate},
        {TEXT("yeast.weapon.shotgun.range_m"),EDWCombatRuleUnit::Centimeters,&C.YeastRange},
        {TEXT("mold.root.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.RootDamage},
        {TEXT("mold.root.ranged_rate_multiplier"),EDWCombatRuleUnit::Multiplier,&C.RootRate}};
    for(const R& V:Reads)if(!Rules.TryGetNumber(V.Key,V.Unit,*V.Value,Error))return false;
    if(!Rules.TryGetCount(TEXT("weapon.shotgun.pellets"),C.Pellets,Error))return false;
    if(!C.IsValid()){Error=TEXT("Invalid finite shotgun spread/range/phase parameters.");return false;}
    Out=C;Error.Reset();return true;
}
double FDWShotgunConfig::Rate(EDWSpearForm F)const{return F==EDWSpearForm::Yeast?YeastRate:F==EDWSpearForm::MoldRooted?RootRate:1.;}
double FDWShotgunConfig::DamageScale(EDWSpearForm F)const{return F==EDWSpearForm::Yeast?YeastDamage:F==EDWSpearForm::MoldRooted?RootDamage:1.;}
double FDWShotgunConfig::RangeFor(EDWSpearForm F)const{return F==EDWSpearForm::Yeast?YeastRange:Range;}
double FDWShotgunConfig::Angle(int32 I)const{return Pellets<=1?0.:-SpreadDegrees*.5+SpreadDegrees*I/(Pellets-1);}
double FDWShotgunConfig::DamageAt(double D,double R)const
{return D<0.||D>R?0.:Damage*FMath::Lerp(1.,FarFraction,FMath::Clamp((D-FullRange)/(R-FullRange),0.,1.));}
double FDWShotgunConfig::KnockbackAt(double D,double R)const
{return D<0.||D>R?0.:FMath::Lerp(NearKnockback,FarKnockback,FMath::Clamp((D-FullRange)/(R-FullRange),0.,1.));}
bool FDWShotgunPlan::Start(const FDWShotgunConfig& C,EDWSpearForm F)
{if(!C.IsValid()||Phase==EDWShotgunPhase::Windup||Phase==EDWShotgunPhase::Recovery)return false;Config=C;Phase=EDWShotgunPhase::Windup;Remaining=PhaseDuration=C.Windup/C.Rate(F);return true;}
double FDWShotgunPlan::Advance(double Dt)
{if(!FMath::IsFinite(Dt)||Dt<0.)return 0.;const double Used=FMath::Min(Dt,Remaining);Remaining=FMath::Max(0.,Remaining-Used);return Used;}
void FDWShotgunPlan::Fired(EDWSpearForm F){Phase=EDWShotgunPhase::Recovery;Remaining=PhaseDuration=Config.Recovery/Config.Rate(F);}

namespace DWShotgunQuery
{
bool RayBox(const UBoxComponent* Box,FVector Start,FVector End,double& Out)
{
    const auto T=Box->GetComponentTransform();const FVector A=T.InverseTransformPosition(Start),D=T.InverseTransformPosition(End)-A,E=Box->GetUnscaledBoxExtent();
    double Enter=0.,Exit=1.;
    for(int32 I=0;I<3;++I)
    {
        if(FMath::Abs(D[I])<1.e-12){if(A[I]<-E[I]||A[I]>E[I])return false;continue;}
        double Lo=(-E[I]-A[I])/D[I],Hi=(E[I]-A[I])/D[I];if(Lo>Hi)Swap(Lo,Hi);
        Enter=FMath::Max(Enter,Lo);Exit=FMath::Min(Exit,Hi);if(Enter>Exit)return false;
    }
    Out=Enter;return true;
}
}
void FDWShotgunQuery::Collect(UWorld* World,AActor* Source,FVector Origin,FVector Forward,const FDWShotgunConfig& C,
    EDWSpearForm Form,TArray<FDWShotgunTarget>& Out)
{
    Out.Reset();if(!World||!IsValid(Source)||Origin.ContainsNaN()||Forward.ContainsNaN()||!C.IsValid())return;
    Forward=Forward.GetSafeNormal2D();if(Forward.IsNearlyZero())return;
    TArray<AActor*> Targets;FCollisionQueryParams Walls(SCENE_QUERY_STAT(DWShotgunWalls),false,Source);
    for(TActorIterator<ADWEnemyCharacter> It(World);It;++It){Walls.AddIgnoredActor(*It);if(It->IsAlive())Targets.Add(*It);}
    for(TActorIterator<ADWEnemyNest> It(World);It;++It){Walls.AddIgnoredActor(*It);if(It->IsAlive()&&!It->bAmbientSpawner)Targets.Add(*It);}
    const double Range=C.RangeFor(Form);
    for(int32 Pellet=0;Pellet<C.Pellets;++Pellet)
    {
        const FVector End=Origin+Forward.RotateAngleAxis(C.Angle(Pellet),FVector::UpVector)*Range;
        FHitResult Obstruction;double First=1.;
        const bool Wall=World->LineTraceSingleByChannel(Obstruction,Origin,End,ECC_Visibility,Walls);
        if(Wall)First=Obstruction.Time;
        AActor* Hit=nullptr;
        for(AActor* Target:Targets)
        {
            TInlineComponentArray<UPrimitiveComponent*> Shapes(Target);
            for(UPrimitiveComponent* Shape:Shapes)
            {
                if(!UDWDamageVolumeLibrary::IsDamageHitComponent(Shape))continue;
                double Fraction=0.;
                if(const auto* Box=Cast<UBoxComponent>(Shape)){if(!DWShotgunQuery::RayBox(Box,Origin,End,Fraction))continue;}
                else if(Shape->IsA<UCapsuleComponent>()||Shape->IsA<USphereComponent>())
                {FHitResult H;FCollisionQueryParams Q(SCENE_QUERY_STAT(DWShotgunHurtbox),false);if(!Shape->LineTraceComponent(H,Origin,End,Q))continue;Fraction=H.Time;}
                else continue; // Decorative static/skeletal meshes never become attack hurtboxes.
                if(Fraction<First||(!Wall&&!Hit&&Fraction==First)){First=Fraction;Hit=Target;}
            }
        }
        if(!Hit)continue;
        auto* Entry=Out.FindByPredicate([Hit](const auto& R){return R.Target==Hit;});
        if(!Entry){FDWShotgunTarget R;R.Target=Hit;Out.Add(R);Entry=&Out.Last();}
        const double Distance=First*Range;
        Entry->Damage+=C.DamageAt(Distance,Range)*C.DamageScale(Form);Entry->Knockback=FMath::Max(Entry->Knockback,C.KnockbackAt(Distance,Range));++Entry->Pellets;
    }
}
