#include "DWCombatRules.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <cmath>
#include <initializer_list>

namespace DWCombatRulesInternal
{
enum class EDWCombatValueDomain : uint8 { NonNegative, Positive, PositiveInteger, UnitInterval, FullAngle };
struct FBaseSpec { const TCHAR* Id; EDWCombatRuleUnit Unit; EDWCombatValueDomain Domain; };
struct FDerivedSpec { const TCHAR* Id; EDWCombatRuleUnit Unit; const TCHAR* Left; const TCHAR* Right; };
static const FBaseSpec BaseSpecs[] = {
#define DW_COMBAT_PARAMETER(Id, Unit, Domain) { Id, Unit, Domain },
#define DW_COMBAT_DERIVED(Id, Unit, Left, Right)
#include "DWCombatRulesSchema.inl"
#undef DW_COMBAT_PARAMETER
#undef DW_COMBAT_DERIVED
};
static const FDerivedSpec DerivedSpecs[] = {
#define DW_COMBAT_PARAMETER(Id, Unit, Domain)
#define DW_COMBAT_DERIVED(Id, Unit, Left, Right) { Id, Unit, Left, Right },
#include "DWCombatRulesSchema.inl"
#undef DW_COMBAT_PARAMETER
#undef DW_COMBAT_DERIVED
};
static_assert(UE_ARRAY_COUNT(BaseSpecs) == 280, "revision105 plus feedback25 base coverage changed");
static_assert(UE_ARRAY_COUNT(DerivedSpecs) == 6, "revision105 derived coverage changed");

bool Fail(FString& Error, const FString& Message) { Error=Message; return false; }
bool HasEmbeddedNull(const FString& Text)
{
    for(int32 I=0;I<Text.Len();++I)if(Text[I]==TCHAR(0))return true;
    return false;
}
bool Exact(const FString& Actual,const TCHAR* Expected)
{
    // FString==const TCHAR* ignores case and terminates at NUL in UE5.8.
    return !HasEmbeddedNull(Actual)&&Actual.Equals(FString(Expected),ESearchCase::CaseSensitive);
}
TSharedPtr<FJsonValue> ExactField(const TSharedPtr<FJsonObject>& Object,const TCHAR* Name)
{
    for(const auto& Pair:Object->Values)if(Exact(FString(Pair.Key.ToView()),Name))return Pair.Value;
    return nullptr;
}
bool ExactFields(const TSharedPtr<FJsonObject>& Object,std::initializer_list<const TCHAR*> Names)
{
    if(Object->Values.Num()!=static_cast<int32>(Names.size()))return false;
    for(const TCHAR* Name:Names)if(!ExactField(Object,Name))return false;
    return true;
}
bool RejectDuplicateObjectFields(const FString& Json,FString& Error)
{
    struct FScope { bool bObject=false; TSet<FString> Keys; };
    TArray<FScope> Scopes;
    auto Reader=TJsonReaderFactory<>::Create(Json);EJsonNotation Token;
    while(Reader->ReadNext(Token))
    {
        if(Token==EJsonNotation::Error)return Fail(Error,TEXT("invalid JSON token"));
        if(Token==EJsonNotation::ObjectEnd||Token==EJsonNotation::ArrayEnd)
        {if(Scopes.IsEmpty())return Fail(Error,TEXT("invalid JSON nesting"));Scopes.Pop();continue;}
        if(!Scopes.IsEmpty()&&Scopes.Last().bObject)
        {
            const FString& Key=Reader->GetIdentifier();
            if(HasEmbeddedNull(Key))return Fail(Error,TEXT("embedded NUL in JSON field name"));
            if(Scopes.Last().Keys.Contains(Key))return Fail(Error,TEXT("duplicate JSON field: ")+Key);
            Scopes.Last().Keys.Add(Key);
        }
        if(Token==EJsonNotation::ObjectStart||Token==EJsonNotation::ArrayStart)
        {FScope Scope;Scope.bObject=Token==EJsonNotation::ObjectStart;Scopes.Add(MoveTemp(Scope));}
    }
    return Scopes.IsEmpty()||Fail(Error,TEXT("incomplete JSON"));
}
const TCHAR* UnitLabel(EDWCombatRuleUnit Unit)
{
    switch(Unit)
    {
    case EDWCombatRuleUnit::Points: return TEXT("点");
    case EDWCombatRuleUnit::PointsPerSecond: return TEXT("点/秒");
    case EDWCombatRuleUnit::PointsPerEvent: return TEXT("点/次");
    case EDWCombatRuleUnit::Seconds: return TEXT("秒");
    case EDWCombatRuleUnit::MetersPerSecond: return TEXT("米/秒");
    case EDWCombatRuleUnit::Multiplier: return TEXT("倍率");
    case EDWCombatRuleUnit::CoreCount: return TEXT("颗");
    case EDWCombatRuleUnit::Meters: return TEXT("米");
    case EDWCombatRuleUnit::HitCount: return TEXT("次");
    case EDWCombatRuleUnit::ObjectCount: return TEXT("枚");
    case EDWCombatRuleUnit::Probability: return TEXT("概率");
    case EDWCombatRuleUnit::FullDegrees: return TEXT("度");
    case EDWCombatRuleUnit::Fraction: return TEXT("比例");
    case EDWCombatRuleUnit::RoundCount: return TEXT("发");
    case EDWCombatRuleUnit::ArrowCount: return TEXT("支");
    case EDWCombatRuleUnit::BottleCount: return TEXT("瓶");
    case EDWCombatRuleUnit::DegreesPerSecond: return TEXT("度/秒");
    case EDWCombatRuleUnit::Version: return TEXT("版本");
    case EDWCombatRuleUnit::Centimeters: return TEXT("cm");
    case EDWCombatRuleUnit::CentimetersPerSecond: return TEXT("cm/s");
    }
    return TEXT("unknown");
}
bool StringField(const TSharedPtr<FJsonObject>& Object,const TCHAR* Name,FString& Out)
{
    const auto Field=ExactField(Object,Name);
    if(!Field||Field->Type!=EJson::String)return false;
    Out=Field->AsString();return !HasEmbeddedNull(Out);
}
bool NumberField(const TSharedPtr<FJsonObject>& Object,const TCHAR* Name,double& Out)
{
    const auto Field=ExactField(Object,Name);
    if(!Field||Field->Type!=EJson::Number)return false;
    Out=Field->AsNumber();return FMath::IsFinite(Out);
}
bool ValidValue(double V,EDWCombatValueDomain Domain)
{
    if(!FMath::IsFinite(V)||V<0.||V>MAX_flt)return false;
    switch(Domain)
    {
    case EDWCombatValueDomain::Positive:return V>0.;
    case EDWCombatValueDomain::PositiveInteger:return V>0.&&V<=MAX_int32&&std::floor(V)==V;
    case EDWCombatValueDomain::UnitInterval:return V<=1.;
    case EDWCombatValueDomain::FullAngle:return V>0.&&V<=360.;
    case EDWCombatValueDomain::NonNegative:return true;
    }
    return false;
}
bool ValidateRelationships(const TMap<FName,FDWCombatRules::FValue>& Values,FString& Error)
{
    // Only called after every expected key was validated. There are deliberately no defaults.
    const auto N=[&Values](const FString& Id){return Values.FindChecked(FName(*Id)).Value;};
    for(const TCHAR* Resource:{TEXT("health"),TEXT("hydration"),TEXT("satiety")})
    {
        const FString Prefix=FString(TEXT("player."))+Resource;
        if(N(Prefix+TEXT(".floor"))>=N(Prefix+TEXT(".max")))return Fail(Error,Prefix+TEXT(": resource max must exceed floor"));
        const double Initial=N(Prefix+TEXT(".max"))*N(Prefix+TEXT(".initial_fraction"));
        const double Respawn=N(Prefix+TEXT(".max"))*N(FString(TEXT("respawn."))+Resource+TEXT(".fraction"));
        if(Initial<N(Prefix+TEXT(".floor"))||Respawn<N(Prefix+TEXT(".floor")))return Fail(Error,Prefix+TEXT(": derived initial/respawn below floor"));
    }
    if(N(TEXT("special.floor"))>N(TEXT("special.max"))||N(TEXT("special.initial"))<N(TEXT("special.floor"))||N(TEXT("special.initial"))>N(TEXT("special.max"))||N(TEXT("special.normal"))<N(TEXT("special.floor"))||N(TEXT("special.normal"))>N(TEXT("special.max")))
        return Fail(Error,TEXT("special: initial/normal/floor/max inconsistent"));
    const double Gain=N(TEXT("special.gain_per_second"));
    if(N(TEXT("yeast.dash.spend_per_second"))<=Gain||N(TEXT("mold.root.spend_per_second"))<=Gain)
        return Fail(Error,TEXT("ability spend per second must exceed special growth"));
    const double MinimumDash=N(TEXT("yeast.dash.start_cost"))+(N(TEXT("yeast.dash.spend_per_second"))-Gain)*N(TEXT("yeast.dash.min_duration_s"));
    if(!FMath::IsFinite(MinimumDash)||MinimumDash>N(TEXT("special.max"))-N(TEXT("special.floor"))||N(TEXT("mold.root.start_cost"))>=N(TEXT("special.max"))-N(TEXT("special.floor")))
        return Fail(Error,TEXT("ability cannot have a positive affordable duration"));
    if(N(TEXT("yeast.dash.min_duration_s"))>N(TEXT("form.duration_s")))return Fail(Error,TEXT("dash minimum duration exceeds whole form duration"));
    for(const TCHAR* Weapon:{TEXT("spear"),TEXT("axe"),TEXT("dagger")})
    {
        const FString P=FString(TEXT("weapon."))+Weapon;
        if(N(P+TEXT(".windup_s"))+N(P+TEXT(".active_s"))+N(P+TEXT(".recovery_s"))>N(P+TEXT(".interval_s"))+1.e-8)
            return Fail(Error,P+TEXT(": phases exceed attack interval"));
    }
    if(N(TEXT("unarmed.windup_s"))+N(TEXT("unarmed.active_s"))+N(TEXT("unarmed.recovery_s"))>N(TEXT("unarmed.interval_s"))+1.e-8)
        return Fail(Error,TEXT("unarmed: phases exceed interval"));
    for(const TCHAR* Weapon:{TEXT("gun"),TEXT("bow"),TEXT("shotgun"),TEXT("alcohol")})
    {
        const FString P=FString(TEXT("weapon."))+Weapon;
        if(N(P+TEXT(".windup_s"))+N(P+TEXT(".recovery_s"))>N(P+TEXT(".interval_s"))+1.e-8)
            return Fail(Error,P+TEXT(": windup/recovery exceed interval"));
    }
    if(N(TEXT("weapon.shotgun.full_damage_range_m"))>N(TEXT("weapon.shotgun.range_m")))return Fail(Error,TEXT("shotgun: full damage distance exceeds range"));
    if(N(TEXT("weapon.shotgun.ammo_per_shot"))>N(TEXT("weapon.shotgun.magazine")))return Fail(Error,TEXT("shotgun: cost exceeds magazine"));
    if(N(TEXT("form.core_cost"))>N(TEXT("core.stack_limit")))return Fail(Error,TEXT("core: cost exceeds one selectable stack"));
    if(N(TEXT("weapon.shotgun.reload_move_multiplier"))>1.)return Fail(Error,TEXT("shotgun reload movement multiplier exceeds one"));
    if(N(TEXT("save.combat_schema_version"))!=1.)return Fail(Error,TEXT("unsupported CombatSchema; this is independent from legacy SaveVersion"));
    return true;
}
}

bool FDWCombatRules::LoadFromJson(const FString& Json,TSharedPtr<const FDWCombatRules>& OutRules,FString& OutError)
{
    using namespace DWCombatRulesInternal;
    OutRules.Reset();OutError.Reset();
    if(!RejectDuplicateObjectFields(Json,OutError))return false;
    TSharedPtr<FJsonObject> Root;
    const auto Reader=TJsonReaderFactory<>::Create(Json);
    if(!FJsonSerializer::Deserialize(Reader,Root)||!Root.IsValid())return Fail(OutError,TEXT("combat payload is invalid JSON"));
    if(!ExactFields(Root,{TEXT("payload_schema_version"),TEXT("design_version"),TEXT("source_sha256"),TEXT("parameters"),TEXT("derived_parameters"),TEXT("rules"),TEXT("equipment_bindings")}))
        return Fail(OutError,TEXT("unexpected or incorrectly cased payload fields"));
    double PayloadVersion=0.;
    if(!NumberField(Root,TEXT("payload_schema_version"),PayloadVersion)||PayloadVersion!=1.)return Fail(OutError,TEXT("unsupported payload schema"));
    TSharedPtr<FDWCombatRules> Candidate(new FDWCombatRules());
    if(!StringField(Root,TEXT("design_version"),Candidate->DesignVersion)||Candidate->DesignVersion.IsEmpty()||
       !StringField(Root,TEXT("source_sha256"),Candidate->SourceHash)||Candidate->SourceHash.Len()!=64)
        return Fail(OutError,TEXT("missing design version or SHA256 provenance"));
    for(TCHAR C:Candidate->SourceHash)if(!FChar::IsHexDigit(C))return Fail(OutError,TEXT("invalid source SHA256"));
    const TSharedPtr<FJsonObject>* RuleProvenance=nullptr;
    if(!Root->TryGetObjectField(TEXT("rules"),RuleProvenance)||!RuleProvenance->IsValid()||(*RuleProvenance)->Values.Num()!=166)
        return Fail(OutError,TEXT("missing/incomplete revision105 rule provenance; numeric readiness alone is not gameplay readiness"));
    const TArray<TSharedPtr<FJsonValue>>* Parameters=nullptr;
    if(!Root->TryGetArrayField(TEXT("parameters"),Parameters)||Parameters->Num()!=UE_ARRAY_COUNT(BaseSpecs))return Fail(OutError,TEXT("expected exactly 280 base parameter entries"));
    TSet<FString> Seen;
    for(const auto& RowValue:*Parameters)
    {
        if(!RowValue||RowValue->Type!=EJson::Object)return Fail(OutError,TEXT("parameter entry must be an object"));
        const auto Row=RowValue->AsObject();FString Id,Unit;double Number=0.;
        if(!ExactFields(Row,{TEXT("id"),TEXT("unit"),TEXT("value")})||!StringField(Row,TEXT("id"),Id)||!StringField(Row,TEXT("unit"),Unit)||!NumberField(Row,TEXT("value"),Number))
            return Fail(OutError,TEXT("parameter requires only string id/unit and finite numeric value"));
        if(Seen.Contains(Id))return Fail(OutError,TEXT("duplicate parameter: ")+Id);
        Seen.Add(Id);
        const FBaseSpec* Spec=nullptr;
        for(const auto& Entry:BaseSpecs)if(Exact(Id,Entry.Id)){Spec=&Entry;break;}
        if(!Spec)return Fail(OutError,TEXT("unknown parameter: ")+Id);
        if(!Exact(Unit,UnitLabel(Spec->Unit))||!ValidValue(Number,Spec->Domain))return Fail(OutError,TEXT("invalid unit or numeric domain: ")+Id);
        if((Spec->Unit==EDWCombatRuleUnit::Meters||Spec->Unit==EDWCombatRuleUnit::MetersPerSecond)&&Number>static_cast<double>(MAX_flt)/100.)return Fail(OutError,TEXT("UE centimeter value would overflow: ")+Id);
        FValue V;V.Value=Number;V.Unit=Spec->Unit;V.bCount=Spec->Domain==EDWCombatValueDomain::PositiveInteger;
        Candidate->Values.Add(FName(*Id),V);
    }
    Candidate->BaseParameterCount=Seen.Num();
    if(Candidate->Values.Num()!=UE_ARRAY_COUNT(BaseSpecs))return Fail(OutError,TEXT("parameter identity collision"));
    for(const auto& Spec:BaseSpecs)if(!Candidate->Values.Contains(FName(Spec.Id)))return Fail(OutError,FString(TEXT("missing validated parameter: "))+Spec.Id);
    if(!ValidateRelationships(Candidate->Values,OutError))return false;
    const TArray<TSharedPtr<FJsonValue>>* Derived=nullptr;
    if(!Root->TryGetArrayField(TEXT("derived_parameters"),Derived)||Derived->Num()!=UE_ARRAY_COUNT(DerivedSpecs))return Fail(OutError,TEXT("expected exactly 6 derived definitions"));
    Seen.Reset();
    for(const auto& RowValue:*Derived)
    {
        if(!RowValue||RowValue->Type!=EJson::Object)return Fail(OutError,TEXT("derived entry must be an object"));
        const auto Row=RowValue->AsObject();FString Id,Unit,Operation;
        const TArray<TSharedPtr<FJsonValue>>* Refs=nullptr;
        if(!ExactFields(Row,{TEXT("id"),TEXT("unit"),TEXT("operation"),TEXT("refs")})||!StringField(Row,TEXT("id"),Id)||!StringField(Row,TEXT("unit"),Unit)||!StringField(Row,TEXT("operation"),Operation)||!Row->TryGetArrayField(TEXT("refs"),Refs)||Refs->Num()!=2)
            return Fail(OutError,TEXT("invalid derived definition"));
        if(Seen.Contains(Id))return Fail(OutError,TEXT("duplicate derived id: ")+Id);
        Seen.Add(Id);const FDerivedSpec* Spec=nullptr;
        for(const auto& Entry:DerivedSpecs)if(Exact(Id,Entry.Id)){Spec=&Entry;break;}
        if(!Spec||!Exact(Unit,UnitLabel(Spec->Unit))||!Exact(Operation,TEXT("product"))||(*Refs)[0]->Type!=EJson::String||(*Refs)[1]->Type!=EJson::String||!Exact((*Refs)[0]->AsString(),Spec->Left)||!Exact((*Refs)[1]->AsString(),Spec->Right))
            return Fail(OutError,TEXT("derived relationship differs from schema: ")+Id);
        FValue V;V.Value=Candidate->Values.FindChecked(FName(Spec->Left)).Value*Candidate->Values.FindChecked(FName(Spec->Right)).Value;V.Unit=Spec->Unit;V.bDerived=true;
        if(!FMath::IsFinite(V.Value))return Fail(OutError,TEXT("derived value overflow: ")+Id);
        Candidate->Values.Add(FName(*Id),V);
    }
    Candidate->DerivedParameterCount=Seen.Num();
    Candidate->CombatSchema=static_cast<int32>(Candidate->Values.FindChecked(TEXT("save.combat_schema_version")).Value);
    const TArray<TSharedPtr<FJsonValue>>* Bindings=nullptr;
    if(!Root->TryGetArrayField(TEXT("equipment_bindings"),Bindings)||Bindings->Num()!=8)return Fail(OutError,TEXT("expected eight equipment binding records"));
    const TCHAR* ExpectedWeapons[]={TEXT("spear"),TEXT("axe"),TEXT("sword"),TEXT("dagger"),TEXT("gun"),TEXT("bow"),TEXT("shotgun"),TEXT("alcohol")};
    Seen.Reset();
    for(const auto& Entry:*Bindings)
    {
        if(!Entry||Entry->Type!=EJson::Object)return Fail(OutError,TEXT("invalid binding record"));
        const auto Row=Entry->AsObject();FString Id;
        if(!StringField(Row,TEXT("weapon_id"),Id)||Seen.Contains(Id))return Fail(OutError,TEXT("invalid/duplicate weapon binding id"));
        bool bKnownWeapon=false;for(const TCHAR* Expected:ExpectedWeapons)bKnownWeapon|=Exact(Id,Expected);
        if(!bKnownWeapon)return Fail(OutError,TEXT("unknown or incorrectly cased weapon binding id"));
        Seen.Add(Id);bool bUnbound=false;
        for(const TCHAR* FieldName:{TEXT("engine_weapon_asset"),TEXT("engine_item_id"),TEXT("engine_ammo_item_id")})
        {
            const auto Field=ExactField(Row,FieldName);
            if(!Field||(Field->Type!=EJson::Null&&Field->Type!=EJson::String))return Fail(OutError,TEXT("binding must be null or string: ")+Id);
            if(Field->Type==EJson::String&&HasEmbeddedNull(Field->AsString()))return Fail(OutError,TEXT("embedded NUL in binding: ")+Id);
            const bool bAmmo=FCString::Strcmp(FieldName,TEXT("engine_ammo_item_id"))==0;
            const bool bRequiresAmmo=Id==TEXT("gun")||Id==TEXT("bow")||Id==TEXT("shotgun")||Id==TEXT("alcohol");
            if((!bAmmo||bRequiresAmmo)&&(Field->Type==EJson::Null||Field->AsString().IsEmpty()))bUnbound=true;
        }
        Candidate->UnboundEquipmentCount+=bUnbound?1:0;
    }
    OutRules=Candidate;return true;
}

bool FDWCombatRules::LoadProjectPayload(TSharedPtr<const FDWCombatRules>& OutRules,FString& OutError)
{
    OutRules.Reset();FString Json;
    const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("DoughWorld/Gameplay/Combat/DWCombatRules.json"));
    if(!FFileHelper::LoadFileToString(Json,*Path))return DWCombatRulesInternal::Fail(OutError,TEXT("cannot read packaged combat payload: ")+Path);
    return LoadFromJson(Json,OutRules,OutError);
}

bool FDWCombatRules::TryGetNumber(FName Id,EDWCombatRuleUnit RequestedUnit,double& OutValue,FString& OutError) const
{
    OutError.Reset();const FValue* V=Values.Find(Id);
    if(!V)return DWCombatRulesInternal::Fail(OutError,TEXT("unknown combat key: ")+Id.ToString());
    double Scale=1.;
    if(V->Unit!=RequestedUnit)
    {
        if((V->Unit==EDWCombatRuleUnit::Meters&&RequestedUnit==EDWCombatRuleUnit::Centimeters)||
           (V->Unit==EDWCombatRuleUnit::MetersPerSecond&&RequestedUnit==EDWCombatRuleUnit::CentimetersPerSecond))Scale=100.;
        else return DWCombatRulesInternal::Fail(OutError,TEXT("unit mismatch for combat key: ")+Id.ToString());
    }
    const double Converted=V->Value*Scale;
    if(!FMath::IsFinite(Converted))return DWCombatRulesInternal::Fail(OutError,TEXT("combat unit conversion overflow: ")+Id.ToString());
    OutValue=Converted;return true;
}

bool FDWCombatRules::TryGetCount(FName Id,int32& OutValue,FString& OutError) const
{
    OutError.Reset();const FValue* V=Values.Find(Id);
    if(!V||!V->bCount)return DWCombatRulesInternal::Fail(OutError,TEXT("combat key is not a count: ")+Id.ToString());
    OutValue=static_cast<int32>(V->Value);return true;
}
