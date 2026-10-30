#if WITH_DEV_AUTOMATION_TESTS
#include "DWRetiredSaveProjection.h"
#include "DWSaveGame.h"
#include "DWGatheringWorld.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRetiredSaveProjectionTest,
    "DoughWorld.Gathering.RetiredItems.LegacyProjection",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRetiredSaveProjectionTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDWSaveGame> Original(NewObject<UDWSaveGame>());
    const FString Map=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest");
    Original->MapPackage=Map;Original->CombatWorld.Gathering.Schema=1;Original->CombatWorld.Gathering.MapPackage=Map;
    Original->Inventory={{TEXT("Jelly"),1},{TEXT("SugarChunk"),2},{TEXT("QA_Unknown"),1}};
    FDWGatheringActorSave Authored;Authored.Id=TEXT("TEST_AUTHORED");Authored.Kind=EDWGatheringSavedKind::Pickup;
    Authored.AuthoredPath=Map+TEXT(".PersistentLevel.Pickup");Authored.ClassPath=ADWGatheringPickup::StaticClass()->GetPathName();Authored.ItemId=TEXT("Waffle");Authored.Quantity=3;
    FDWGatheringActorSave Runtime=Authored;Runtime.Id=TEXT("TEST_RUNTIME");Runtime.AuthoredPath.Reset();Runtime.bRuntimeSpawned=true;
    FDWGatheringActorSave Resource;Resource.Id=TEXT("TEST_RESOURCE");Resource.Kind=EDWGatheringSavedKind::Resource;
    Resource.AuthoredPath=Map+TEXT(".PersistentLevel.Resource");Resource.ClassPath=ADWGatheringResource::StaticClass()->GetPathName();
    Resource.MaxDurability=100.;Resource.Durability=50.;Resource.RequiredTool=TEXT("Axe");Resource.Drops={{TEXT("Jelly"),1},{TEXT("SugarChunk"),2}};
    Original->CombatWorld.Gathering.Actors={Authored,Runtime,Resource};
    TStrongObjectPtr<UDWSaveGame> Copy(DuplicateObject<UDWSaveGame>(Original.Get(),GetTransientPackage()));FString Error;
    if(!TestTrue(TEXT("Projection accepts supported historic IDs"),DWRetiredSaveProjection::ApplyToDuplicate(*Copy,Error,24)))return false;
    TestEqual(TEXT("Original inventory remains unchanged"),Original->Inventory.Num(),3);
    TestEqual(TEXT("Original world remains unchanged"),Original->CombatWorld.Gathering.Actors.Num(),3);
    TestEqual(TEXT("Only retired inventory is removed from copy"),Copy->Inventory.Num(),2);
    TestEqual(TEXT("Unknown remains subject to strict validation"),Copy->Inventory[1].ItemId,FName(TEXT("QA_Unknown")));
    const auto& Actors=Copy->CombatWorld.Gathering.Actors;
    TestEqual(TEXT("Runtime retired pickup removed"),Actors.Num(),2);
    TestEqual(TEXT("Authored identity remains, but pickup disabled"),Actors[0].Quantity,0);
    TestEqual(TEXT("Other resource rewards preserved"),Actors[1].Drops.Num(),1);
    TestEqual(TEXT("Remaining reward identity preserved"),Actors[1].Drops[0].ItemId,FName(TEXT("SugarChunk")));
    TStrongObjectPtr<UDWSaveGame> Duplicate(DuplicateObject<UDWSaveGame>(Original.Get(),GetTransientPackage()));
    Duplicate->CombatWorld.Gathering.Actors.Add(Runtime);
    TestFalse(TEXT("Duplicate retired world identity rejected before filtering"),DWRetiredSaveProjection::ApplyToDuplicate(*Duplicate,Error,24));
    Copy->Inventory.Add(FDWItemStack(TEXT("Jelly"),-1));
    TestFalse(TEXT("Malformed retired stack rejected"),DWRetiredSaveProjection::ApplyToDuplicate(*Copy,Error,24));
    Copy->Inventory.Last().Quantity=11;
    TestFalse(TEXT("Retired stack exceeding historical limit rejected"),DWRetiredSaveProjection::ApplyToDuplicate(*Copy,Error,24));
    Copy->Inventory.Last().Quantity=1;
    TestFalse(TEXT("Over-capacity inventory rejected before retired slots are filtered"),DWRetiredSaveProjection::ApplyToDuplicate(*Copy,Error,2));
    return true;
}
#endif
