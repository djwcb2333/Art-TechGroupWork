#include "DWCombatMoldRoot.h"
#include "DWCombatRules.h"
#include <cmath>
#include <limits>

bool FDWMoldRootConfig::IsValid() const
{
    const double Values[]={StartCost,SpendPerSecond,SpecialGainPerSecond,SpecialFloor,DamageMultiplier,
        RangedRateMultiplier,IncomingMultiplier,MoveCentimetersPerSecond,EnterVisualSeconds,ExitVisualSeconds};
    for(double V:Values)if(!std::isfinite(V))return false;
    // B05/B06 defines an anchored stance. A nonzero movement parameter conflicts with that rule.
    return StartCost>=0.&&SpendPerSecond>=0.&&SpecialGainPerSecond>=0.&&SpecialFloor>=0.&&
        DamageMultiplier>0.&&RangedRateMultiplier>0.&&IncomingMultiplier>=0.&&MoveCentimetersPerSecond==0.&&
        EnterVisualSeconds>=0.&&ExitVisualSeconds>=0.;
}
bool FDWMoldRootConfig::Build(const FDWCombatRules& Rules,FDWMoldRootConfig& Out,FString& Error)
{
    FDWMoldRootConfig C;
    struct FRead{const TCHAR* Key;EDWCombatRuleUnit Unit;double* Value;};
    const FRead Reads[]={
        {TEXT("mold.root.start_cost"),EDWCombatRuleUnit::Points,&C.StartCost},
        {TEXT("mold.root.spend_per_second"),EDWCombatRuleUnit::PointsPerSecond,&C.SpendPerSecond},
        {TEXT("special.gain_per_second"),EDWCombatRuleUnit::PointsPerSecond,&C.SpecialGainPerSecond},
        {TEXT("special.floor"),EDWCombatRuleUnit::Points,&C.SpecialFloor},
        {TEXT("mold.root.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.DamageMultiplier},
        {TEXT("mold.root.ranged_rate_multiplier"),EDWCombatRuleUnit::Multiplier,&C.RangedRateMultiplier},
        {TEXT("mold.root.incoming_multiplier"),EDWCombatRuleUnit::Multiplier,&C.IncomingMultiplier},
        {TEXT("mold.root.move_mps"),EDWCombatRuleUnit::CentimetersPerSecond,&C.MoveCentimetersPerSecond},
        {TEXT("mold.root.enter_visual_s"),EDWCombatRuleUnit::Seconds,&C.EnterVisualSeconds},
        {TEXT("mold.root.exit_visual_s"),EDWCombatRuleUnit::Seconds,&C.ExitVisualSeconds}};
    for(const FRead& R:Reads)if(!Rules.TryGetNumber(R.Key,R.Unit,*R.Value,Error))return false;
    if(!C.IsValid()){Error=TEXT("Root stance requires finite valid rates and exactly zero autonomous movement.");return false;}
    Out=C;Error.Reset();return true;
}
EDWMoldRootFailure FDWMoldRootPlan::CanStart(const FDWMoldRootConfig& C,EDWCombatBaseForm Form,
    double Special,bool bLegalGround,FVector Position) const
{
    if(bActive)return EDWMoldRootFailure::AlreadyRooted;
    if(Serial==MAX_uint64)return EDWMoldRootFailure::IdentityExhausted;
    if(!C.IsValid())return EDWMoldRootFailure::InvalidConfig;
    if(Form!=EDWCombatBaseForm::Mold)return EDWMoldRootFailure::WrongForm;
    if(!std::isfinite(Special)||Special<=C.StartCost||Special-C.StartCost<=C.SpecialFloor)return EDWMoldRootFailure::InsufficientSpecial;
    if(!bLegalGround)return EDWMoldRootFailure::InvalidGround;
    if(Position.ContainsNaN())return EDWMoldRootFailure::InvalidPosition;
    return EDWMoldRootFailure::None;
}
bool FDWMoldRootPlan::Start(const FDWMoldRootConfig& C,EDWCombatBaseForm Form,double Special,bool bLegalGround,FVector Position)
{
    if(CanStart(C,Form,Special,bLegalGround,Position)!=EDWMoldRootFailure::None)return false;
    ++Serial;bActive=true;Config=C;Anchor=Position;return true;
}
void FDWMoldRootPlan::End(EDWMoldRootEnd Reason)
{if(!bActive)return;bActive=false;EndReason=Reason;}
double FDWMoldRootPlan::EstimateResourceBoundarySeconds(double Special) const
{
    if(!bActive)return std::numeric_limits<double>::infinity();
    if(!std::isfinite(Special)||Special<Config.SpecialFloor)return std::numeric_limits<double>::quiet_NaN();
    if(Special==Config.SpecialFloor)return 0.;
    const double Net=Config.SpendPerSecond-Config.SpecialGainPerSecond;
    return Net>0.?(Special-Config.SpecialFloor)/Net:std::numeric_limits<double>::infinity();
}
bool FDWMoldRootPlan::ValidateSnapshot(const FDWMoldRootConfig& C,const FDWMoldRootSnapshot& S,
    EDWCombatBaseForm Form,double Special,double Remaining)
{
    if(!C.IsValid()||S.Anchor.ContainsNaN()||!std::isfinite(Special)||Special<C.SpecialFloor||
        !std::isfinite(Remaining)||Remaining<0.||static_cast<uint8>(Form)>static_cast<uint8>(EDWCombatBaseForm::Yeast))return false;
    if(!S.bActive)return S.Anchor==FVector::ZeroVector;
    // Entry fee is not paid again and need not remain available on a restored ongoing episode.
    return Form==EDWCombatBaseForm::Mold&&Special>C.SpecialFloor&&Remaining>0.;
}
bool FDWMoldRootPlan::Restore(const FDWMoldRootConfig& C,const FDWMoldRootSnapshot& S,
    EDWCombatBaseForm Form,double Special,double Remaining)
{
    if(!ValidateSnapshot(C,S,Form,Special,Remaining)||Serial==MAX_uint64)return false;
    ++Serial;Config=C;bActive=S.bActive;Anchor=S.Anchor;EndReason=EDWMoldRootEnd::Restore;return true;
}
bool FDWMoldRootPlan::ValidateBinding(const FDWMoldRootPlan& P,const FDWMoldRootIdentity& I,
    uint64 Epoch,DWCombatCommit::FEntityLife Life)
{
    if(!Epoch||!Life.Entity||!Life.Life)return false;
    if(!P.IsActive())return !I.Action.Id&&!I.Action.Epoch&&!I.Receipt.Id&&!I.Receipt.Epoch&&
        !I.SourceLife.Entity&&!I.SourceLife.Life&&!I.WorldEpoch&&!I.RootSerial&&I.Anchor==FVector::ZeroVector;
    return I.Action.Id&&I.Receipt.Id&&I.Action.Epoch==Epoch&&I.Receipt.Epoch==Epoch&&I.WorldEpoch==Epoch&&
        I.SourceLife.Entity==Life.Entity&&I.SourceLife.Life==Life.Life&&I.RootSerial==P.GetSerial()&&I.Anchor==P.GetAnchor();
}
