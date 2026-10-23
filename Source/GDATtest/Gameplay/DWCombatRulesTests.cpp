#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatRules.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DWCombatRulesTests
{
TSharedPtr<FJsonObject> Fixture(FString& Error)
{
    FString Text;TSharedPtr<FJsonObject> Root;
    const FString Path=FPaths::Combine(FPaths::ProjectContentDir(),TEXT("DoughWorld/Gameplay/Combat/DWCombatRules.json"));
    if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root))
    {Error=TEXT("Install the generated project payload before running D01 tests: ")+Path;return nullptr;}
    // A broken installed baseline should fail a test, never crash a fixture's key dereference.
    TSharedPtr<const FDWCombatRules> Validated;
    if(!FDWCombatRules::LoadFromJson(Text,Validated,Error))return nullptr;
    return Root;
}
TSharedPtr<FJsonObject> Parameter(const TSharedPtr<FJsonObject>& Root,const TCHAR* Id)
{
    for(const auto& Row:Root->GetArrayField(TEXT("parameters")))
        if(Row->AsObject()->GetStringField(TEXT("id"))==Id)return Row->AsObject();
    return nullptr;
}
FString Json(const TSharedPtr<FJsonObject>& Root)
{
    FString Text;FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return Text;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatRulesCoverageTest,"DoughWorld.CombatRules.CoverageAndUnits",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatRulesCoverageTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;
    if(!TestTrue(TEXT("Packaged project-relative payload loads"),FDWCombatRules::LoadProjectPayload(Rules,Error))){AddError(Error);return false;}
    TestEqual(TEXT("280 independent values"),Rules->GetBaseParameterCount(),280);
    TestEqual(TEXT("6 computed values"),Rules->GetDerivedParameterCount(),6);
    for(const TCHAR* Id:{TEXT("weapon.dagger.stage1_duration_s"),TEXT("weapon.dagger.stage2_duration_s"),TEXT("weapon.dagger.stage3_duration_s"),TEXT("weapon.dagger.stage4_duration_s")})
    {
        double Duration=0.;
        TestTrue(TEXT("Each complete dagger stage is a typed seconds parameter"),Rules->TryGetNumber(Id,EDWCombatRuleUnit::Seconds,Duration,Error));
        TestTrue(TEXT("Every complete dagger stage has positive duration"),Duration>0.);
    }
    TestEqual(TEXT("CombatSchema is independent of old SaveVersion4"),Rules->GetCombatSchema(),1);
    TestTrue(TEXT("Binding readiness is reported independently"),Rules->GetUnboundEquipmentCount()>=0&&Rules->GetUnboundEquipmentCount()<=8);
    // Known conversion inputs belong to this fixture, not the designer's live tuning.
    auto Fixture=DWCombatRulesTests::Fixture(Error);if(!Fixture){AddError(Error);return false;}
    DWCombatRulesTests::Parameter(Fixture,TEXT("weapon.spear.range_m"))->SetNumberField(TEXT("value"),3.2);
    DWCombatRulesTests::Parameter(Fixture,TEXT("player.move.base_mps"))->SetNumberField(TEXT("value"),6.);
    DWCombatRulesTests::Parameter(Fixture,TEXT("weapon.axe.block_arc_deg"))->SetNumberField(TEXT("value"),180.);
    DWCombatRulesTests::Parameter(Fixture,TEXT("weapon.bow.slow_fraction"))->SetNumberField(TEXT("value"),.35);
    DWCombatRulesTests::Parameter(Fixture,TEXT("weapon.shotgun.magazine"))->SetNumberField(TEXT("value"),6.);
    DWCombatRulesTests::Parameter(Fixture,TEXT("weapon.shotgun.ammo_per_shot"))->SetNumberField(TEXT("value"),1.);
    if(!TestTrue(TEXT("Explicit conversion fixture loads"),FDWCombatRules::LoadFromJson(DWCombatRulesTests::Json(Fixture),Rules,Error))){AddError(Error);return false;}
    double V=-1.;
    TestTrue(TEXT("Spear distance requested in centimeters"),Rules->TryGetNumber(TEXT("weapon.spear.range_m"),EDWCombatRuleUnit::Centimeters,V,Error));
    TestEqual(TEXT("3.2m is 320cm"),V,320.);
    Rules->TryGetNumber(TEXT("weapon.spear.range_m"),EDWCombatRuleUnit::Centimeters,V,Error);
    TestEqual(TEXT("Repeated query does not convert the stored value twice"),V,320.);
    Rules->TryGetNumber(TEXT("player.move.base_mps"),EDWCombatRuleUnit::CentimetersPerSecond,V,Error);
    TestEqual(TEXT("6m/s is 600cm/s"),V,600.);
    Rules->TryGetNumber(TEXT("weapon.axe.block_arc_deg"),EDWCombatRuleUnit::FullDegrees,V,Error);
    TestEqual(TEXT("Full angle remains 180, no implicit half-angle"),V,180.);
    Rules->TryGetNumber(TEXT("weapon.bow.slow_fraction"),EDWCombatRuleUnit::Fraction,V,Error);
    TestEqual(TEXT("Slow remains a fraction, not implicit percentage"),V,.35);
    V=-913.;
    TestFalse(TEXT("Wrong physical unit rejected"),Rules->TryGetNumber(TEXT("weapon.spear.range_m"),EDWCombatRuleUnit::Seconds,V,Error));
    TestEqual(TEXT("Rejected read leaves caller's output untouched"),V,-913.);
    TestFalse(TEXT("Unknown ID has no legacy default"),Rules->TryGetNumber(TEXT("weapon.unknown.damage"),EDWCombatRuleUnit::Points,V,Error));
    int32 Count=-7;
    TestFalse(TEXT("An integral-valued damage is not a count"),Rules->TryGetCount(TEXT("weapon.spear.damage"),Count,Error));
    TestEqual(TEXT("Wrong count leaves output unchanged"),Count,-7);
    TestTrue(TEXT("Magazine has count type"),Rules->TryGetCount(TEXT("weapon.shotgun.magazine"),Count,Error));
    TestEqual(TEXT("Magazine count"),Count,6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatRulesDerivedTest,"DoughWorld.CombatRules.DerivedValuesFollowSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatRulesDerivedTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatRulesTests;
    FString Error;auto Root=Fixture(Error);if(!Root){AddError(Error);return false;}
    Parameter(Root,TEXT("player.health.max"))->SetNumberField(TEXT("value"),160.);
    Parameter(Root,TEXT("player.health.floor"))->SetNumberField(TEXT("value"),0.);
    Parameter(Root,TEXT("player.health.initial_fraction"))->SetNumberField(TEXT("value"),1.);
    Parameter(Root,TEXT("respawn.health.fraction"))->SetNumberField(TEXT("value"),1.);
    Parameter(Root,TEXT("player.hydration.max"))->SetNumberField(TEXT("value"),250.);
    Parameter(Root,TEXT("player.hydration.floor"))->SetNumberField(TEXT("value"),0.);
    Parameter(Root,TEXT("player.hydration.initial_fraction"))->SetNumberField(TEXT("value"),.4);
    Parameter(Root,TEXT("respawn.hydration.fraction"))->SetNumberField(TEXT("value"),.8);
    Parameter(Root,TEXT("form.duration_s"))->SetNumberField(TEXT("value"),18.);
    Parameter(Root,TEXT("yeast.dash.min_duration_s"))->SetNumberField(TEXT("value"),1.);
    Parameter(Root,TEXT("weapon.gun.damage"))->SetNumberField(TEXT("value"),11.);
    Parameter(Root,TEXT("enemy.mold.windup_s"))->SetNumberField(TEXT("value"),.9);
    TSharedPtr<const FDWCombatRules> Rules;
    if(!TestTrue(TEXT("Edited in-memory source values load"),FDWCombatRules::LoadFromJson(Json(Root),Rules,Error))){AddError(Error);return false;}
    double V=0.;
    Rules->TryGetNumber(TEXT("player.health.initial"),EDWCombatRuleUnit::Points,V,Error);TestEqual(TEXT("Health initial recomputes"),V,160.);
    Rules->TryGetNumber(TEXT("respawn.health"),EDWCombatRuleUnit::Points,V,Error);TestEqual(TEXT("Health respawn recomputes"),V,160.);
    Rules->TryGetNumber(TEXT("player.hydration.initial"),EDWCombatRuleUnit::Points,V,Error);TestEqual(TEXT("250 x .4"),V,100.);
    Rules->TryGetNumber(TEXT("respawn.hydration"),EDWCombatRuleUnit::Points,V,Error);TestEqual(TEXT("Independent 250 x .8"),V,200.);
    Rules->TryGetNumber(TEXT("form.duration_s"),EDWCombatRuleUnit::Seconds,V,Error);TestEqual(TEXT("Form does not fall back to 15"),V,18.);
    Rules->TryGetNumber(TEXT("weapon.gun.damage"),EDWCombatRuleUnit::Points,V,Error);TestEqual(TEXT("Gun does not fall back to old DA"),V,11.);
    Rules->TryGetNumber(TEXT("enemy.mold.windup_s"),EDWCombatRuleUnit::Seconds,V,Error);TestEqual(TEXT("Enemy windup source applied"),V,.9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatRulesInvalidSchemaTest,"DoughWorld.CombatRules.RejectMalformedSchema",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatRulesInvalidSchemaTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatRulesTests;
    FString Error;auto Original=Fixture(Error);if(!Original){AddError(Error);return false;}
    const FString OriginalText=Json(Original);
    const auto Fresh=[&OriginalText](){TSharedPtr<FJsonObject> R;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(OriginalText),R);return R;};
    const auto Reject=[this](const TCHAR* Label,const TSharedPtr<FJsonObject>& R)
    {TSharedPtr<const FDWCombatRules> Rules;FString E;TestFalse(Label,FDWCombatRules::LoadFromJson(Json(R),Rules,E));TestFalse(TEXT("Invalid data publishes no partial rules"),Rules.IsValid());TestFalse(TEXT("Failure includes a reason"),E.IsEmpty());};
    const auto NullSuffix=[this](const FString& Prefix)
    {FString Bad=Prefix+TEXT("|suffix");Bad[Prefix.Len()]=TCHAR(0);TestTrue(TEXT("Fixture really contains embedded NUL, not AppendChar(0) no-op"),Bad.Len()>FCString::Strlen(*Bad));return Bad;};
    {auto R=Fresh();auto A=R->GetArrayField(TEXT("parameters"));A.Pop();R->SetArrayField(TEXT("parameters"),A);Reject(TEXT("Missing base key"),R);}
    {auto R=Fresh();auto A=R->GetArrayField(TEXT("parameters"));A[1]=A[0];R->SetArrayField(TEXT("parameters"),A);Reject(TEXT("Duplicate key cannot mask missing key"),R);}
    {auto R=Fresh();Parameter(R,TEXT("weapon.gun.damage"))->SetStringField(TEXT("id"),TEXT("weapon.gun.damgae"));Reject(TEXT("Unknown key with unchanged count"),R);}
    {auto R=Fresh();Parameter(R,TEXT("player.health.max"))->SetStringField(TEXT("id"),TEXT("Player.health.max"));Reject(TEXT("Parameter IDs use exact case"),R);}
    {auto R=Fresh();Parameter(R,TEXT("player.health.max"))->SetStringField(TEXT("id"),NullSuffix(TEXT("weapon.gun.damage")));TestTrue(TEXT("Serialized fixture retains NUL escape"),Json(R).Contains(TEXT("\\u0000")));Reject(TEXT("Embedded NUL cannot alias another key and crash relationship lookup"),R);}
    {auto R=Fresh();Parameter(R,TEXT("weapon.gun.damage"))->SetStringField(TEXT("unit"),NullSuffix(TEXT("点")));Reject(TEXT("Unit cannot match only before embedded NUL"),R);}
    {auto R=Fresh();auto Row=Parameter(R,TEXT("player.health.max"));Row->RemoveField(TEXT("id"));Row->SetStringField(TEXT("ID"),TEXT("player.health.max"));Reject(TEXT("Schema field names use exact case"),R);}
    {auto R=Fresh();Parameter(R,TEXT("weapon.gun.damage"))->SetBoolField(TEXT("value"),true);Reject(TEXT("Boolean cannot pretend to be numeric one"),R);}
    {auto R=Fresh();Parameter(R,TEXT("weapon.gun.damage"))->SetStringField(TEXT("unit"),TEXT("米"));Reject(TEXT("Value with wrong dimension"),R);}
    {auto R=Fresh();Parameter(R,TEXT("weapon.shotgun.pellets"))->SetNumberField(TEXT("value"),2.5);Reject(TEXT("Fractional projectile count"),R);}
    {auto R=Fresh();Parameter(R,TEXT("core.mold.drop_probability"))->SetNumberField(TEXT("value"),1.01);Reject(TEXT("Probability above one"),R);}
    {auto R=Fresh();Parameter(R,TEXT("weapon.spear.range_m"))->SetNumberField(TEXT("value"),1.e38);Reject(TEXT("Centimeter conversion would overflow float"),R);}
    {auto R=Fresh();Parameter(R,TEXT("save.combat_schema_version"))->SetNumberField(TEXT("value"),4);Reject(TEXT("Legacy version4 is not CombatSchema1"),R);}
    {auto R=Fresh();R->RemoveField(TEXT("rules"));Reject(TEXT("Numeric readiness still requires source rule provenance"),R);}
    {auto R=Fresh();auto A=R->GetArrayField(TEXT("derived_parameters"));A[0]->AsObject()->SetNumberField(TEXT("value"),999.);Reject(TEXT("Derived result must not become editable"),R);}
    {auto R=Fresh();auto A=R->GetArrayField(TEXT("derived_parameters"));A[0]->AsObject()->SetArrayField(TEXT("refs"),{MakeShared<FJsonValueString>(TEXT("respawn.health")),MakeShared<FJsonValueString>(TEXT("respawn.health"))});Reject(TEXT("Cyclic/replaced derived reference"),R);}
    {auto R=Fresh();auto Row=R->GetArrayField(TEXT("derived_parameters"))[0]->AsObject();Row->SetStringField(TEXT("id"),NullSuffix(Row->GetStringField(TEXT("id"))));Reject(TEXT("Derived id NUL rejected"),R);}
    {auto R=Fresh();auto Row=R->GetArrayField(TEXT("derived_parameters"))[0]->AsObject();auto A=Row->GetArrayField(TEXT("refs"));A[0]=MakeShared<FJsonValueString>(NullSuffix(A[0]->AsString()));Row->SetArrayField(TEXT("refs"),A);Reject(TEXT("Derived reference NUL rejected"),R);}
    {auto R=Fresh();R->GetArrayField(TEXT("derived_parameters"))[0]->AsObject()->SetStringField(TEXT("operation"),TEXT("Product"));Reject(TEXT("Derived operation is case-sensitive"),R);}
    TSharedPtr<const FDWCombatRules> Rules;
    TestTrue(TEXT("Seed valid pointer before failure"),FDWCombatRules::LoadFromJson(OriginalText,Rules,Error));
    TestFalse(TEXT("Nonfinite JSON rejected"),FDWCombatRules::LoadFromJson(TEXT("{\"payload_schema_version\":1e999}"),Rules,Error));
    TestFalse(TEXT("Failure cannot leave previous config silently installed"),Rules.IsValid());
    const FString Duplicated=TEXT("{\"payload_schema_version\":0,")+OriginalText.Mid(1);
    TestFalse(TEXT("Duplicate object fields cannot be silently replaced by JSON parser"),FDWCombatRules::LoadFromJson(Duplicated,Rules,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatRulesConstraintsTest,"DoughWorld.CombatRules.RejectImpossibleGameplayBudgets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatRulesConstraintsTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatRulesTests;
    FString Error;auto Original=Fixture(Error);if(!Original){AddError(Error);return false;}
    const FString Text=Json(Original);
    const auto N=[&Original](const TCHAR* Id){return Parameter(Original,Id)->GetNumberField(TEXT("value"));};
    const auto RejectValue=[this,&Text](const TCHAR* Id,double Value,const TCHAR* Label)
    {
        TSharedPtr<FJsonObject> R;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),R);Parameter(R,Id)->SetNumberField(TEXT("value"),Value);
        TSharedPtr<const FDWCombatRules> Rules;FString E;TestFalse(Label,FDWCombatRules::LoadFromJson(Json(R),Rules,E));TestFalse(TEXT("Reason reported"),E.IsEmpty());
    };
    RejectValue(TEXT("yeast.dash.spend_per_second"),N(TEXT("special.gain_per_second")),TEXT("Spend equal to automatic gain cannot drain ability"));
    RejectValue(TEXT("mold.root.spend_per_second"),N(TEXT("special.gain_per_second")),TEXT("Root spend cannot equal gain"));
    RejectValue(TEXT("yeast.dash.start_cost"),N(TEXT("special.max")),TEXT("No affordable minimum dash after start cost"));
    RejectValue(TEXT("mold.root.start_cost"),N(TEXT("special.max")),TEXT("Root requires positive post-fee duration"));
    RejectValue(TEXT("weapon.spear.active_s"),N(TEXT("weapon.spear.interval_s"))+1.,TEXT("Melee windows cannot exceed attack cycle"));
    RejectValue(TEXT("weapon.shotgun.full_damage_range_m"),N(TEXT("weapon.shotgun.range_m"))+1.,TEXT("Full-damage region cannot exceed total range"));
    RejectValue(TEXT("weapon.shotgun.ammo_per_shot"),N(TEXT("weapon.shotgun.magazine"))+1.,TEXT("Shot cost cannot exceed capacity"));
    RejectValue(TEXT("yeast.weapon.spear.rate_multiplier"),0.,TEXT("Zero rate would create division by zero"));
    for(const TCHAR* Id:{TEXT("weapon.dagger.stage1_duration_s"),TEXT("weapon.dagger.stage2_duration_s"),TEXT("weapon.dagger.stage3_duration_s"),TEXT("weapon.dagger.stage4_duration_s")})
        RejectValue(Id,0.,TEXT("A complete dagger stage cannot have zero duration"));
    RejectValue(TEXT("player.hydration.floor"),N(TEXT("player.hydration.max")),TEXT("Zero-width hydration interval is invalid"));
    RejectValue(TEXT("player.health.floor"),N(TEXT("player.health.max")),TEXT("Zero-width health interval is rejected consistently with vitals"));
    return true;
}
#endif
