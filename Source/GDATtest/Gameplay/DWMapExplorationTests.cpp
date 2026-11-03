#include "DWMapExploration.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DWSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWExploreLocalTest,"DoughWorld.UI.Map.ExplorationLocal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWExploreLocalTest::RunTest(const FString&)
{
    FDWMapImageSettings I;I.WorldCenterCm=FVector2D(4300,-8100);I.WorldSizeCm=FVector2D(40000,16000);I.ImageUpYaw=37.f;
    auto L=DWMapExploration::MakeFrame(I,128);FIntRect Dirty;
    TestFalse(TEXT("Legacy empty starts hidden"),DWMapExploration::IsRevealed(FDWMapExplorationLayer(),I.WorldCenterCm));
    TestTrue(TEXT("First visit changes nearby cells"),DWMapExploration::Reveal(L,I.WorldCenterCm,500.,Dirty));
    TestTrue(TEXT("Dirty rect local rather than whole grid"),Dirty.Width()<16&&Dirty.Height()<16);
    TestTrue(TEXT("Current position revealed"),DWMapExploration::IsRevealed(L,I.WorldCenterCm));
    TestFalse(TEXT("Distant position still hidden"),DWMapExploration::IsRevealed(L,I.WorldCenterCm+FVector2D(5000,5000)));
    TestFalse(TEXT("Standing still sends no new dirty update"),DWMapExploration::Reveal(L,I.WorldCenterCm,500.,Dirty));
    const auto Before=L.Bits;
    TestFalse(TEXT("Invalid radius rejected"),DWMapExploration::Reveal(L,I.WorldCenterCm,-1.,Dirty));
    TestFalse(TEXT("Outside image rejected"),DWMapExploration::Reveal(L,I.WorldCenterCm+FVector2D(1000000,0),500.,Dirty));
    TestTrue(TEXT("Rejected inputs do not corrupt facts"),Before==L.Bits);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWExploreCodecTest,"DoughWorld.UI.Map.ExplorationActualMemoryCodec",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWExploreCodecTest::RunTest(const FString&)
{
    FDWMapImageSettings I;I.WorldCenterCm=FVector2D(12345.123456789,-87654.987654321);
    I.WorldSizeCm=FVector2D(41123.7654321,19234.5678901);I.ImageUpYaw=397.123456f;
    TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());
    Save->WorldMapMarkers.Schema=1;
    auto& Set=Save->WorldMapMarkers.Sets.AddDefaulted_GetRef();Set.MapPackage=TEXT("/Game/CodecWorld");
    const int32 First=DWMapExploration::FindOrAddFrame(Set.ExplorationLayers,I,128);FIntRect D;
    if(!TestEqual(TEXT("First frame"),First,0))return false;
    DWMapExploration::Reveal(Set.ExplorationLayers[First],I.WorldCenterCm,700.,D);
    const auto Bits=Set.ExplorationLayers[First].Bits;
    // Same binary memory codec called by DWSaveStorage/SaveSource, no .sav file or World.
    for(int32 Round=0;Round<3;++Round)
    {
        TArray<uint8> Bytes;
        if(!TestTrue(TEXT("Existing SaveGame encoder to memory"),UGameplayStatics::SaveGameToMemory(Save.Get(),Bytes)))return false;
        TStrongObjectPtr<UDWSaveGame> Loaded(Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)));
        if(!TestNotNull(TEXT("Existing SaveGame decoder from memory"),Loaded.Get()))return false;
        auto& Layers=Loaded->WorldMapMarkers.Sets[0].ExplorationLayers;
        TestEqual(TEXT("Fractional frame matches after roundtrip"),DWMapExploration::FindOrAddFrame(Layers,I,128),0);
        TestEqual(TEXT("No spurious new calibration layer"),Layers.Num(),1);
        TestTrue(TEXT("Visited bits survive exact codec"),Layers[0].Bits==Bits);
        Save=MoveTemp(Loaded);
    }
    auto& Layers=Save->WorldMapMarkers.Sets[0].ExplorationLayers;
    for(int32 K=1;K<DWMapExploration::MaxLayers;++K)
    {auto Next=I;Next.WorldCenterCm.X+=1000.*K;TestEqual(TEXT("Distinct frame added"),DWMapExploration::FindOrAddFrame(Layers,Next,128),K);}
    auto Overflow=I;Overflow.WorldCenterCm.X+=10000.;
    TestEqual(TEXT("Capacity does not evict"),DWMapExploration::FindOrAddFrame(Layers,Overflow,128),INDEX_NONE);
    TestEqual(TEXT("Original layer count retained"),Layers.Num(),DWMapExploration::MaxLayers);
    TestTrue(TEXT("Capacity failure preserves original bits"),Layers[0].Bits==Bits);
    auto Repaint=I;Repaint.Image=FSoftObjectPath(TEXT("/Game/ChangedPainting.ChangedPainting"));
    TestEqual(TEXT("Repaint at capacity still matches original frame"),DWMapExploration::FindOrAddFrame(Layers,Repaint,128),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWExploreFrameTest,"DoughWorld.UI.Map.ExplorationFrameCompatibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWExploreFrameTest::RunTest(const FString&)
{
    FDWMapImageSettings I;I.WorldSizeCm=FVector2D(25000,10000);
    auto L=DWMapExploration::MakeFrame(I,128);FIntRect D;DWMapExploration::Reveal(L,FVector2D::ZeroVector,500.,D);
    FDWMapImageSettings Repaint=I;Repaint.Image=FSoftObjectPath(TEXT("/Game/OtherArt.OtherArt"));
    TestTrue(TEXT("Image path changes do not erase exploration"),DWMapExploration::SameFrame(L,Repaint,128));
    Repaint.ImageUpYaw=360.f;
    TestTrue(TEXT("Equivalent full-turn yaw keeps frame"),DWMapExploration::SameFrame(L,Repaint,128));
    Repaint.WorldCenterCm.X=100.;
    TestFalse(TEXT("New world frame does not reuse old facts"),DWMapExploration::SameFrame(L,Repaint,128));
    TArray<FDWMapExplorationLayer> Layers{L,DWMapExploration::MakeFrame(Repaint,128)};
    FString E;TestTrue(TEXT("Old and new world frames coexist"),DWMapExploration::Validate(Layers,E));
    TestTrue(TEXT("Original frame retained"),Layers[0].Bits==L.Bits);
    Layers.Add(L);TestFalse(TEXT("Duplicate frame rejected"),DWMapExploration::Validate(Layers,E));
    Layers={L};Layers[0].Bits.Pop();TestFalse(TEXT("Truncated bits rejected"),DWMapExploration::Validate(Layers,E));
    Layers={L};Layers[0].GridSize=100000;TestFalse(TEXT("Unbounded grid rejected"),DWMapExploration::Validate(Layers,E));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWExploreSaveTest,"DoughWorld.UI.Map.ExplorationSaveBoundary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWExploreSaveTest::RunTest(const FString&)
{
    FDWWorldMapMarkerSave Legacy;FString E;
    TestTrue(TEXT("Legacy absent field remains valid"),DWWorldMapData::ValidateSave(Legacy,E));
    FDWWorldMapMarkerSave Save;Save.Schema=1;
    FDWWorldMapMarkerSet A;A.MapPackage=TEXT("/Game/TestA");
    FDWWorldMapMarkerSet B;B.MapPackage=TEXT("/Game/TestB");
    FDWMapImageSettings I;auto L=DWMapExploration::MakeFrame(I,128);FIntRect D;
    DWMapExploration::Reveal(L,FVector2D::ZeroVector,1000.,D);A.ExplorationLayers.Add(L);
    B.ExplorationLayers.Add(DWMapExploration::MakeFrame(I,128));Save.Sets={A,B};
    TestTrue(TEXT("Package-separated layers validate"),DWWorldMapData::ValidateSave(Save,E));
    TestFalse(TEXT("Exploring A does not reveal B"),DWMapExploration::IsRevealed(Save.Sets[1].ExplorationLayers[0],FVector2D::ZeroVector));
    Save.Sets[0].ExplorationLayers[0].Schema=99;
    TestFalse(TEXT("Unknown exploration schema rejected"),DWWorldMapData::ValidateSave(Save,E));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWExploreAtlasTest,"DoughWorld.UI.Map.ExplorationBakedAtlasCalibration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWExploreAtlasTest::RunTest(const FString&)
{
    // Frozen from ResourcesMap/BakedAtlas/MapAtlas.json, not a guessed world extent.
    FDWMapImageSettings I;I.WorldCenterCm=FVector2D(0,0);I.WorldSizeCm=FVector2D(64890,64890);I.ImageUpYaw=0.f;
    const FVector2D World[]={FVector2D(-15370.671645872399,-12147.001940068636),FVector2D(13913.739744271155,359.1836793994494),FVector2D(7354.1911108578515,-19009.228391653352)};
    const FVector2D Expected[]={FVector2D(.31280625766576303,.7368727330231529),FVector2D(.5055352701402289,.2855796001807496),FVector2D(.2070545786461188,.3866668036545254)};
    auto L=DWMapExploration::MakeFrame(I,128);FIntRect Dirty;
    for(int32 K=0;K<3;++K)
    {
        FVector2D UV,Back;
        TestTrue(TEXT("Authored landmark projects"),I.WorldToUV(World[K],UV));
        TestTrue(TEXT("Image top +X/right +Y matches atlas"),UV.Equals(Expected[K],1.e-11));
        TestTrue(TEXT("Authored UV inverse"),I.UVToWorld(UV,Back)&&Back.Equals(World[K],1.e-7));
        DWMapExploration::Reveal(L,World[K],1500.,Dirty);
        TestTrue(TEXT("Landmark and mask share world frame"),DWMapExploration::IsRevealed(L,World[K]));
    }
    return true;
}
#endif
