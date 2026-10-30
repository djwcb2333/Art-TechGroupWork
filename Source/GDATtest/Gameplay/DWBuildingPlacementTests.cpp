#if WITH_DEV_AUTOMATION_TESTS
#include "DWBuildingDefinition.h"
#include "DWBuildingPlacementComponent.h"
#include "DWPlaceableBuilding.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.h"
#include "DWInventoryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DWBuildingPlacementTests
{
    void SetDefinition(UDWBuildingDefinition& Definition)
    {
        Definition.BuildingItemId=TEXT("QA_Building");Definition.StationId=TEXT("QA_Station");
        Definition.ActorClass=ADWPlaceableBuilding::StaticClass();
        FDWBuildingMeshPart Part;Part.PartName=TEXT("Body");
        Part.Mesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
        Part.RelativeTransform=FTransform(FVector(0,0,50));Definition.Parts={Part};
        Definition.FootprintHalfExtent=FVector(50);Definition.StackLimit=1;
    }
    void SetConfig(UDWGameplayConfig& Config,UDWBuildingDefinition* Definition)
    {
        Config.Items.Reset();Config.Recipes.Reset();Config.ItemTable=nullptr;Config.RecipeTable=nullptr;
        Config.MaxInventorySlots=2;Config.MaxStackSize=40;Config.bRequireTransformationForCrafting=false;
        for(FName Id:{FName(TEXT("QA_Material")),FName(TEXT("QA_Building"))})
        {FDWItemDefinition Item;Item.ItemId=Id;Item.bUsable=Id==TEXT("QA_Building");Config.Items.Add(Item);}
        FDWRecipeDefinition Recipe;Recipe.RecipeId=TEXT("QA_CraftBuilding");
        Recipe.Inputs={FDWItemStack(TEXT("QA_Material"),1)};Recipe.Outputs={FDWItemStack(TEXT("QA_Building"),1)};
        Config.Recipes.Add(Recipe);Config.BuildingDefinitions.Add(TSoftObjectPtr<UDWBuildingDefinition>(Definition));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingBagCraft,
    "DoughWorld.Buildings.Inventory.CraftToBagAndCapacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingBagCraft::RunTest(const FString&)
{
    using namespace DWBuildingPlacementTests;
    TStrongObjectPtr<UDWBuildingDefinition> Definition(NewObject<UDWBuildingDefinition>());SetDefinition(*Definition);
    TStrongObjectPtr<UDWGameplayConfig> Config(NewObject<UDWGameplayConfig>());SetConfig(*Config,Definition.Get());
    TStrongObjectPtr<UDWInventoryComponent> Bag(NewObject<UDWInventoryComponent>());Bag->InitializeInventory(Config.Get());
    TestTrue(TEXT("Initial stock accepted"),Bag->TryAddItem(TEXT("QA_Material"),5));
    TestTrue(TEXT("Registered building craft succeeds without any world spawn context"),Bag->TryCraft(TEXT("QA_CraftBuilding"),false));
    TestEqual(TEXT("One building in bag"),Bag->CountItem(TEXT("QA_Building")),1);
    TestEqual(TEXT("One material paid"),Bag->CountItem(TEXT("QA_Material")),4);
    TestEqual(TEXT("Building uses its configured stack limit"),Bag->GetEffectiveStackLimit(TEXT("QA_Building")),1);
    const auto Revision=Bag->GetInventoryRevision();
    TestFalse(TEXT("Full bag cannot silently place a second building"),Bag->TryCraft(TEXT("QA_CraftBuilding"),false));
    TestEqual(TEXT("Failed craft preserves input"),Bag->CountItem(TEXT("QA_Material")),4);
    TestEqual(TEXT("Failed craft preserves revision"),Bag->GetInventoryRevision(),Revision);
    FDWInventorySlotSnapshot Slot;TestTrue(TEXT("Capture the building item"),Bag->CaptureSlotSnapshot(1,Slot));
    FDWPreparedInventoryChange Remove;TestTrue(TEXT("Preparing placement cost leaves bag intact"),Bag->PrepareRemoveSnapshot(Slot,1,Remove));
    TestEqual(TEXT("Preview has not consumed building"),Bag->CountItem(TEXT("QA_Building")),1);
    TestTrue(TEXT("Concurrent inventory change succeeds"),Bag->TryRemoveItem(TEXT("QA_Material"),1));
    TestFalse(TEXT("Stale placement cost cannot commit"),Bag->CommitPrepared(MoveTemp(Remove)));
    TestEqual(TEXT("Stale placement preserves building"),Bag->CountItem(TEXT("QA_Building")),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingProfileCompatibility,
    "DoughWorld.Buildings.Inventory.ProfileStationAndLegacyCompatibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingProfileCompatibility::RunTest(const FString&)
{
    using namespace DWBuildingPlacementTests;
    TStrongObjectPtr<UDWBuildingDefinition> Definition(NewObject<UDWBuildingDefinition>());SetDefinition(*Definition);
    TStrongObjectPtr<UDWGatheringProfile> Config(NewObject<UDWGatheringProfile>());SetConfig(*Config,Definition.Get());
    Config->NonInventoryItems.Add(TEXT("QA_Building"));Config->RecipeBuildings.Add(TEXT("QA_CraftBuilding"),TEXT("QA_Building"));
    Config->RecipeStations.Add(TEXT("QA_CraftBuilding"),TEXT("QA_RequiredStation"));
    Definition->RequiredCraftingStation=TEXT("QA_RequiredStation");
    FString Error;TestTrue(TEXT("Explicit registered building upgrades legacy profile without deleting unrelated rules"),Config->ValidateGatheringDefinitions(Error));
    TStrongObjectPtr<UDWInventoryComponent> Bag(NewObject<UDWInventoryComponent>());Bag->InitializeInventory(Config.Get());
    Bag->TryAddItem(TEXT("QA_Material"),3);
    TestFalse(TEXT("Required station absent fails closed"),Bag->CanCraft(TEXT("QA_CraftBuilding"),false));
    bool NearStation=true;
    Bag->SetCraftingContextValidator([&](FName,FName Station,FName Building){return NearStation&&Station==TEXT("QA_RequiredStation")&&Building.IsNone();});
    FDWPreparedInventoryChange Craft;TestTrue(TEXT("Registered profile recipe prepares normal inventory output"),Bag->PrepareCraftRecipe(TEXT("QA_CraftBuilding"),false,Craft));
    NearStation=false;TestFalse(TEXT("Moving away before commit preserves ingredients"),Bag->CommitPrepared(MoveTemp(Craft)));
    TestEqual(TEXT("Ingredients unchanged"),Bag->CountItem(TEXT("QA_Material")),3);
    NearStation=true;TestTrue(TEXT("Normal commit after station recheck"),Bag->TryCraft(TEXT("QA_CraftBuilding"),false));
    TestEqual(TEXT("Profile output is in bag"),Bag->CountItem(TEXT("QA_Building")),1);
    TStrongObjectPtr<UDWGatheringProfile> Legacy(NewObject<UDWGatheringProfile>());SetConfig(*Legacy,Definition.Get());Legacy->BuildingDefinitions.Reset();
    Legacy->NonInventoryItems.Add(TEXT("QA_Building"));
    TStrongObjectPtr<UDWInventoryComponent> LegacyBag(NewObject<UDWInventoryComponent>());LegacyBag->InitializeInventory(Legacy.Get());
    TestFalse(TEXT("Unregistered legacy non-inventory buildings stay protected"),LegacyBag->TryAddItem(TEXT("QA_Building"),1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingGridAndDefinition,
    "DoughWorld.Buildings.Placement.GridRotationAndDefinitionValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingGridAndDefinition::RunTest(const FString&)
{
    const auto P=UDWBuildingPlacementComponent::SnapXY(FVector(31,-29,17.25),20.);
    TestEqual(TEXT("Positive X snaps to 20cm grid"),P.X,40.);TestEqual(TEXT("Negative Y snaps symmetrically"),P.Y,-20.);
    TestEqual(TEXT("Height is never grid-quantized"),P.Z,17.25);
    double Yaw=0.;for(int32 I=0;I<24;++I)Yaw=UDWBuildingPlacementComponent::StepYaw(Yaw,15.);
    TestEqual(TEXT("24 discrete rotations make one exact turn"),Yaw,0.);
    TestEqual(TEXT("359 plus15 wraps correctly"),UDWBuildingPlacementComponent::StepYaw(359.,15.),14.);
    TStrongObjectPtr<UDWBuildingDefinition> Definition(NewObject<UDWBuildingDefinition>());DWBuildingPlacementTests::SetDefinition(*Definition);
    FString Error;TestTrue(TEXT("Usable definition validates"),Definition->ValidateDefinition(Error));
    Definition->FootprintHalfExtent.Z=0;TestFalse(TEXT("Zero occupancy height rejected"),Definition->ValidateDefinition(Error));
    Definition->FootprintHalfExtent.Z=50;Definition->MaxPlacementDistance=std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Nonfinite reach rejected"),Definition->ValidateDefinition(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingActorVisuals,
    "DoughWorld.Buildings.Actor.VisualRebuildAndStaging",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingActorVisuals::RunTest(const FString&)
{
    if(!GEngine)return false;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWBuildingTestWorld")),GetTransientPackage());
    if(!TestNotNull(TEXT("Isolated world"),World))return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);World->InitializeActorsForPlay(FURL());
    TStrongObjectPtr<UDWBuildingDefinition> Definition(NewObject<UDWBuildingDefinition>());DWBuildingPlacementTests::SetDefinition(*Definition);
    auto* Building=World->SpawnActorDeferred<ADWPlaceableBuilding>(ADWPlaceableBuilding::StaticClass(),FTransform::Identity);
    if(Building)
    {
        Building->Definition=Definition.Get();Building->SetStaged(true);Building->FinishSpawning(FTransform::Identity);
        TestTrue(TEXT("Generated visuals available after spawn/BeginPlay"),Building->HasCompleteBuildingVisuals());
        TestFalse(TEXT("Staged building does not interact"),Building->IsInteractionAvailable());
        Building->SetStaged(false);Building->PublishInteractionState();
        TestTrue(TEXT("Main-config-independent station is available"),Building->IsInteractionAvailable());
        TestTrue(TEXT("Inherited preview cube removed"),Building->Mesh->GetStaticMesh()==nullptr);
        TestTrue(TEXT("Repeated rebuild succeeds"),Building->RebuildBuildingVisuals());
        TArray<UStaticMeshComponent*> Parts;Building->GetComponents(Parts);int32 AuthoredCount=0;
        for(auto* Part:Parts)if(Part->ComponentHasTag(TEXT("DWBuildingVisual")))++AuthoredCount;
        TestEqual(TEXT("Repeated rebuild does not duplicate authored components"),AuthoredCount,1);
    }
    else AddError(TEXT("Could not spawn building."));
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return Building!=nullptr;
}
#endif
