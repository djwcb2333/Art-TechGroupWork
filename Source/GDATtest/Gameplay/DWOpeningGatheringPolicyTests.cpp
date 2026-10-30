#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DWOpeningGatheringPolicy.h"
#include "DWGatheringWorldPersistence.h"
#include "DWGatheringPersistenceOptIn.h"
#include "DWSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/SplineComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWOpeningGatheringBoundaryTest,"DoughWorld.Opening.GatheringSourceDestinationBoundary",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWOpeningGatheringBoundaryTest::RunTest(const FString&)
{
 auto* Settings=GetMutableDefault<UDWGatheringPersistenceOptIn>();const auto Previous=Settings->Maps;
 struct FRestore{UDWGatheringPersistenceOptIn* Settings;TArray<FDWGatheringMapOptIn> Maps;~FRestore(){Settings->Maps=MoveTemp(Maps);}} Restore{Settings,Previous};
 Settings->Maps.Reset();FString Error;
 const FString Opening=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_SC0101_Awakening");
 const FString Target=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_StormLandscape_Test_v03");
 const FString Main=TEXT("/Game/DoughWorld/Core/Data/DA_DoughWorldGameplay.DA_DoughWorldGameplay");
 const FString Class=TEXT("/Game/DoughWorld/Gameplay/Resources/BP_DWGatherWaterPond_Test.BP_DWGatherWaterPond_Test_C");
 for(const auto& Map:{Opening,Target}){FDWGatheringMapOptIn Entry;Entry.bEnabled=true;Entry.MapPackage=Map;Entry.ProfilePath=Main;Entry.AuditedAuthoredClassPaths.Add(Class);Settings->Maps.Add(Entry);}
 TStrongObjectPtr<UDWSaveGame> S(NewObject<UDWSaveGame>()),D(NewObject<UDWSaveGame>());
 S->Version=5;S->FormalLanguage.Schema=2;S->MapPackage=Opening;
 auto& F=S->AdventureFlow;F.Schema=1;F.ScenarioId=TEXT("SC0101");F.PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 F.OpeningMap=Opening;F.DestinationMap=Target;F.Phase=EDWAdventureOpeningPhase::Pending;F.bStarterGranted=true;
 auto& G=S->CombatWorld.Gathering;
 TestTrue(TEXT("New marker uses empty schema2 before the authored birth checkpoint"),FDWGatheringWorldPersistence::MakeNewAdventure(Opening,G,Error)&&G.Schema==2&&G.Ponds.IsEmpty());
 FDWGatheringPondSave Pond;Pond.Id=TEXT("DWPond_SC0101");Pond.AuthoredPath=Opening+TEXT(".L_SC0101_Awakening:PersistentLevel.Pond");
 Pond.ClassPath=Class;Pond.bClosed=true;Pond.InitialReserve=300.;Pond.RemainingWater=125.;Pond.Points.SetNum(3);
 Pond.Points[0].Position=FVector(-200.,-150.,0.);Pond.Points[1].Position=FVector(200.,-150.,0.);Pond.Points[2].Position=FVector(0.,200.,0.);
 for(auto& Point:Pond.Points)Point.Type=uint8(ESplinePointType::Linear);
 Pond.Radius=250.;Pond.WaterHeight=55.;Pond.BedHeight=5.;Pond.InnerSlope=70.;Pond.CrestAbove=10.;Pond.CrestDistance=20.;Pond.OuterWidth=120.;Pond.GridSpacing=15.;Pond.Tiling=200.;Pond.Samples=8;
 G.Ponds.Add(Pond);
 TestTrue(TEXT("Authored source checkpoint with consumed pond water is supported"),DWOpeningGatheringPolicy::ValidateSourceSnapshot(*S,Error));
 TestFalse(TEXT("Birth marker cannot contain checkpoint pond history"),FDWGatheringWorldPersistence::Validate(G,Opening,{},true,Error));
 S->CombatWorld.bNewAdventure=true;TestFalse(TEXT("Handoff policy refuses an unfinished new marker"),DWOpeningGatheringPolicy::ValidateSourceSnapshot(*S,Error));S->CombatWorld.bNewAdventure=false;
 TArray<uint8> Bytes;
 if(TestTrue(TEXT("Source pond checkpoint serializes to memory"),UGameplayStatics::SaveGameToMemory(S.Get(),Bytes)))
 {
  TStrongObjectPtr<UDWSaveGame> Loaded(Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)));
  if(TestNotNull(TEXT("Memory checkpoint decodes without a gameplay world"),Loaded.Get()))
   TestTrue(TEXT("Restart snapshot retains exact pond water and identity"),FDWGatheringWorldSave::StaticStruct()->CompareScriptStruct(&G,&Loaded->CombatWorld.Gathering,PPF_None));
 }
 F.Phase=EDWAdventureOpeningPhase::CompletedPendingDestination;
 D->Version=5;D->FormalLanguage.Schema=2;D->MapPackage=Target;D->AdventureFlow=F;D->AdventureFlow.Phase=EDWAdventureOpeningPhase::Completed;
 auto& DG=D->CombatWorld.Gathering;DG.Schema=2;DG.MapPackage=Target;
 Pond.Id=TEXT("DWPond_Storm");Pond.AuthoredPath=Target+TEXT(".L_DoughWorld_StormLandscape_Test_v03:PersistentLevel.Pond");Pond.RemainingWater=300.;DG.Ponds.Add(Pond);
 const auto Captured=DG;const auto SourceBefore=G;
 TestTrue(TEXT("Consumed source water and fresh destination water stay independent"),DWOpeningGatheringPolicy::ValidateDestinationBoundary(*S,*D,Captured,Error));
 DG=G;TestFalse(TEXT("Copying source gathering into destination is rejected"),DWOpeningGatheringPolicy::ValidateDestinationBoundary(*S,*D,Captured,Error));DG=Captured;
 DG.Ponds[0].RemainingWater=125.;TestFalse(TEXT("Reusing source quantity after fresh capture is rejected"),DWOpeningGatheringPolicy::ValidateDestinationBoundary(*S,*D,Captured,Error));DG=Captured;
 D->AdventureFlow.Phase=EDWAdventureOpeningPhase::Pending;TestFalse(TEXT("Destination cannot replay the teaching source"),DWOpeningGatheringPolicy::ValidateDestinationBoundary(*S,*D,Captured,Error));D->AdventureFlow.Phase=EDWAdventureOpeningPhase::Completed;
 TestTrue(TEXT("Boundary validation never mutates committed source facts"),FDWGatheringWorldSave::StaticStruct()->CompareScriptStruct(&G,&SourceBefore,PPF_None));
 G.Ponds[0].ClassPath=TEXT("/Game/Unknown.Unknown_C");TestFalse(TEXT("Unknown source participant remains rejected"),DWOpeningGatheringPolicy::ValidateSourceSnapshot(*S,Error));G=SourceBefore;
 TestFalse(TEXT("A hidden test profile cannot own formal teaching source"),DWOpeningGatheringPolicy::ValidateSourceFlow(F,Opening,TEXT("/Game/DoughWorld/Core/Data/DA_DWGathering_Test.DA_DWGathering_Test"),Error));
 const FString EmptyMap=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_Map1");
 F.DestinationMap=EmptyMap;D->MapPackage=EmptyMap;D->AdventureFlow=F;D->AdventureFlow.Phase=EDWAdventureOpeningPhase::Completed;
 DG={};const FDWGatheringWorldSave EmptyCapture=DG;
 TestTrue(TEXT("Source schema2 may enter a genuinely empty unregistered schema0 destination"),DWOpeningGatheringPolicy::ValidateDestinationBoundary(*S,*D,EmptyCapture,Error));
 DG=G;TestFalse(TEXT("A schema0 destination cannot copy source pond facts"),DWOpeningGatheringPolicy::ValidateDestinationBoundary(*S,*D,EmptyCapture,Error));
 DG={};DG.Ponds=G.Ponds;const auto InvalidZeroCapture=DG;
 TestFalse(TEXT("An alleged schema0 capture containing hidden ponds is rejected"),DWOpeningGatheringPolicy::ValidateDestinationBoundary(*S,*D,InvalidZeroCapture,Error));
 DG={};DG.ToolCooldownRemaining=1.;const auto InvalidZeroClock=DG;
 TestFalse(TEXT("Nonempty legacy gathering timers are also rejected"),DWOpeningGatheringPolicy::ValidateDestinationBoundary(*S,*D,InvalidZeroClock,Error));
 F.OpeningMap=Target;TestFalse(TEXT("A general opted map cannot become an authorized teaching source"),DWOpeningGatheringPolicy::ValidateSourceSnapshot(*S,Error));
 return true;
}
#endif
