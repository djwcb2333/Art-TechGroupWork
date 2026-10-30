#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DWGatheringWorldPersistence.h"
#include "DWGatheringPondPersistence.h"
#include "DWGatheringPersistenceOptIn.h"
#include "DWSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringOptInSaveTest,"DoughWorld.Gathering.OptInPondFacts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringOptInSaveTest::RunTest(const FString&)
{
    auto* Settings=GetMutableDefault<UDWGatheringPersistenceOptIn>();
    const auto Before=Settings->Maps;
    struct FRestore {UDWGatheringPersistenceOptIn* S;TArray<FDWGatheringMapOptIn> Maps;~FRestore(){S->Maps=MoveTemp(Maps);}} Restore{Settings,Before};
    Settings->Maps.Reset();FString Error;
    const FString Map=TEXT("/Game/DW_QA_OptInMap"),Profile=TEXT("/Game/DW_QA_Profile.DW_QA_Profile"),PondClass=TEXT("/Game/DW_QA_Pond.DW_QA_Pond_C");
    FDWGatheringWorldSave Legacy;
    TestTrue(TEXT("empty legacy non-LANG remains valid"),FDWGatheringWorldPersistence::Validate(Legacy,Map,{},false,Error));
    TestFalse(TEXT("map disabled by default"),FDWGatheringWorldPersistence::IsSupportedMap(Map));
    FDWGatheringMapOptIn Entry;Entry.bEnabled=true;Entry.MapPackage=Map;Entry.ProfilePath=Profile;Entry.AuditedAuthoredClassPaths.Add(PondClass);
    Settings->Maps.Add(Entry);
    TestTrue(TEXT("audited map opt-in"),FDWGatheringWorldPersistence::IsSupportedMap(Map));
    TestFalse(TEXT("old schema zero is not silently migrated on opted map"),FDWGatheringWorldPersistence::Validate(Legacy,Map,{},false,Error));
    FDWGatheringWorldSave Fresh;
    TestTrue(TEXT("new adventure gets schema two"),FDWGatheringWorldPersistence::MakeNewAdventure(Map,Fresh,Error)&&Fresh.Schema==2);
    FDWGatheringPondSave Pond;Pond.Id=TEXT("DWPond_Example");Pond.AuthoredPath=TEXT("/Game/DW_QA_OptInMap.DW_QA_OptInMap:PersistentLevel.Pond");
    Pond.ClassPath=PondClass;Pond.bClosed=true;Pond.InitialReserve=300.;Pond.RemainingWater=125.;Pond.Points.SetNum(3);
    Pond.Radius=250.;Pond.WaterHeight=55.;Pond.BedHeight=5.;Pond.InnerSlope=70.;Pond.CrestAbove=10.;Pond.CrestDistance=20.;
    Pond.OuterWidth=120.;Pond.GridSpacing=15.;Pond.Tiling=200.;Pond.Samples=8;
    TestTrue(TEXT("bounded authored pond facts validate"),FDWGatheringPondPersistence::Validate(Pond,Error));
    Fresh.Ponds.Add(Pond);
    TestTrue(TEXT("new schema accepts an audited pond"),FDWGatheringWorldPersistence::Validate(Fresh,Map,{},false,Error));
    TestFalse(TEXT("new adventure marker cannot contain pond history"),FDWGatheringWorldPersistence::Validate(Fresh,Map,{},true,Error));
    {
        TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());
        Save->MapPackage=Map;Save->CombatWorld.Gathering=Fresh;
        TArray<uint8> Bytes;
        if(TestTrue(TEXT("serialize opted-in pond and resources to memory only"),UGameplayStatics::SaveGameToMemory(Save.Get(),Bytes)))
        {
            TStrongObjectPtr<UDWSaveGame> Loaded(Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)));
            if(TestNotNull(TEXT("deserialize opted-in save from memory"),Loaded.Get()))
                TestTrue(TEXT("all gathering facts survive actual SaveGame serialization"),
                    FDWGatheringWorldSave::StaticStruct()->CompareScriptStruct(&Save->CombatWorld.Gathering,&Loaded->CombatWorld.Gathering,PPF_None));
        }
    }
    Fresh.Ponds.Add(Pond);
    TestFalse(TEXT("duplicate pond id rejected"),FDWGatheringWorldPersistence::Validate(Fresh,Map,{},false,Error));
    Fresh.Ponds.Pop();Fresh.Ponds[0].RemainingWater=std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN water rejected"),FDWGatheringWorldPersistence::Validate(Fresh,Map,{},false,Error));
    Fresh.Ponds[0].RemainingWater=301.;
    TestFalse(TEXT("over capacity water rejected"),FDWGatheringWorldPersistence::Validate(Fresh,Map,{},false,Error));
    Fresh.Ponds[0].RemainingWater=125.;Fresh.Ponds[0].ClassPath=TEXT("/Game/Unknown.Unknown_C");
    TestFalse(TEXT("unknown class remains blocked"),FDWGatheringWorldPersistence::Validate(Fresh,Map,{},false,Error));
    Settings->Maps.Add(Entry);
    TestFalse(TEXT("duplicate map capability fails closed"),FDWGatheringWorldPersistence::IsSupportedMap(Map));
    return true;
}
#endif
