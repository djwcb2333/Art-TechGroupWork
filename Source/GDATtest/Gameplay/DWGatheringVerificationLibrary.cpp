#include "DWGatheringVerificationLibrary.h"
#include "DWGatheringWorld.h"
#include "DWGatheringPond.h"
#include "DWGatheringComponent.h"
#include "DWGatheringProfile.h"
#include "DWGatheringTestGameMode.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayHUD.h"
#include "DWCombatPlayerVitals.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace DWGatheringVerification
{
struct FReport
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Checks;
    int32 Failed = 0;
    void Check(const FString& Name, bool Passed, const FString& Detail = FString())
    {
        auto Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"), Name); Item->SetBoolField(TEXT("passed"), Passed);
        Item->SetStringField(TEXT("detail"), Detail); Checks.Add(MakeShared<FJsonValueObject>(Item));
        if (!Passed) ++Failed;
    }
    void Number(const FString& Name, double Actual, double Expected, double Tolerance = .0001)
    {
        Check(Name, FMath::IsFinite(Actual) && FMath::Abs(Actual - Expected) <= Tolerance,
            FString::Printf(TEXT("actual=%.6f expected=%.6f"), Actual, Expected));
    }
    FString Finish()
    {
        Root->SetArrayField(TEXT("checks"), Checks); Root->SetNumberField(TEXT("failed"), Failed);
        Root->SetNumberField(TEXT("total"), Checks.Num()); Root->SetBoolField(TEXT("all_passed"), Failed == 0);
        Root->SetBoolField(TEXT("formal_save_io"), false); Root->SetBoolField(TEXT("asset_writes"), false);
        Root->SetStringField(TEXT("scope"), TEXT("Explicit native API checks in isolated PIE. Does not verify physical F/click, animation or rendered UI."));
        FString Text; const auto Writer = TJsonWriterFactory<>::Create(&Text);
        FJsonSerializer::Serialize(Root, Writer); return Text;
    }
};

struct FSession
{
    UWorld* World;
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWInventoryComponent> Bag;
    TArray<FDWItemStack> OriginalSlots;
    FTransform OriginalTransform;
    FVector OriginalVelocity;
    FName OriginalTool;
    TSet<AActor*> InitialActors;
    explicit FSession(UWorld* InWorld, ADWPlayerCharacter* InPlayer)
        : World(InWorld), Player(InPlayer), Bag(InPlayer->GetInventory()), OriginalSlots(Bag->GetSlots()),
          OriginalTransform(InPlayer->GetActorTransform()), OriginalVelocity(InPlayer->GetCharacterMovement()->Velocity)
    {
        for (TActorIterator<AActor> It(World); It; ++It) InitialActors.Add(*It);
        if (InPlayer->GatheringComponent) OriginalTool = InPlayer->GatheringComponent->GetActiveTool();
    }
    bool Restore()
    {
        bool Restored = true;
        if (Player.IsValid())
        {
            Player->SetActorTransform(OriginalTransform, false, nullptr, ETeleportType::TeleportPhysics);
            Player->GetCharacterMovement()->Velocity = OriginalVelocity;
        }
        TArray<TWeakObjectPtr<AActor>> Cleanup;
        for (TActorIterator<AActor> It(World); It; ++It)
            if (!InitialActors.Contains(*It) && (Cast<ADWGatheringInteractable>(*It) || Cast<ADWGatheringPond>(*It) || It->Tags.Contains(TEXT("DWGatheringRuntimeQA")))) Cleanup.Add(*It);
        for (auto Item : Cleanup) if (Item.IsValid()) Item->Destroy();
        if (Bag.IsValid()) Restored = Bag->SetSlots(OriginalSlots); else Restored = false;
        if (Player.IsValid() && Player->GatheringComponent) Player->GatheringComponent->RestoreActiveTool(OriginalTool);
        return Restored;
    }
};

bool GiveRecipeInputs(UDWInventoryComponent* Bag, const FDWRecipeDefinition& Recipe)
{
    if (!Bag->SetSlots({})) return false;
    for (const auto& Input : Recipe.Inputs) if (!Bag->TryAddItem(Input.ItemId, Input.Quantity)) return false;
    return true;
}
}

FString UDWGatheringVerificationLibrary::RunRuntimeChecks(const UObject* Context)
{
    using namespace DWGatheringVerification;
    FReport R;
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
    const FString Map = World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
    const bool Allowed = World && World->WorldType == EWorldType::PIE && !World->bIsTearingDown &&
        Map == TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GatheringTest") &&
        Cast<ADWGatheringTestGameMode>(World->GetAuthGameMode());
    R.Check(TEXT("exact_pie_test_map_gate"), Allowed, Map);
    if (!Allowed) return R.Finish();
    auto* GI = World->GetGameInstance<UDWGameInstance>();
    auto* Profile = GI ? Cast<UDWGatheringProfile>(GI->GetConfig()) : nullptr;
    auto* PC = World->GetFirstPlayerController();
    auto* Player = PC ? Cast<ADWPlayerCharacter>(PC->GetPawn()) : nullptr;
    auto* HUD = PC ? Cast<ADWGameplayHUD>(PC->GetHUD()) : nullptr;
    auto* Bag = Player ? Player->GetInventory() : nullptr;
    const bool Ready = Profile && IsValid(Player) && Player->HasActorBegunPlay() && !Player->IsDead() &&
        Player->HasCombatVitalsReady() && Bag && HUD && !HUD->IsBlockingGameplay() && !World->IsPaused() && !GI->HasActiveSlot();
    R.Check(TEXT("real_player_profile_hud_ready_no_active_save"), Ready);
    if (!Ready) return R.Finish();
    R.Check(TEXT("player_and_gi_profile_match"), Player->GetGameplayConfig() == Profile);
    FString DefinitionError;
    R.Check(TEXT("profile_definition_validation"), Profile->ValidateGatheringDefinitions(DefinitionError), DefinitionError);
    R.Number(TEXT("recipe_count"), Profile->Recipes.Num(), 21.);
    R.Number(TEXT("flour_stack"), Bag->GetEffectiveStackLimit(TEXT("Flour")), 10.);
    R.Number(TEXT("water_stack"), Bag->GetEffectiveStackLimit(TEXT("Water")), 10.);
    R.Number(TEXT("dough_stack"), Bag->GetEffectiveStackLimit(TEXT("Dough")), 10.);
    for (const auto& Item : Profile->StarterItems)
        R.Check(TEXT("starter_") + Item.ItemId.ToString(), Bag->CountItem(Item.ItemId) >= Item.Quantity,
            FString::Printf(TEXT("actual=%d expected_at_least=%d"), Bag->CountItem(Item.ItemId), Item.Quantity));
    auto* Gathering = Player->GatheringComponent.Get();
    R.Check(TEXT("gathering_component_active"), Gathering && Gathering->IsGatheringEnabled());
    if (!Gathering) return R.Finish();
    R.Check(TEXT("starter_tool_active"), !Gathering->GetActiveTool().IsNone(), Gathering->GetActiveTool().ToString());

    FSession Session(World, Player);
    // Runtime-only platform separates the QA objects from authored resources/stations.
    auto* Platform = World->SpawnActor<AStaticMeshActor>();
    auto* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (!Platform || !Cube)
    {
        R.Check(TEXT("runtime_fixture_platform"), false);
        R.Check(TEXT("restore_original_inventory"), Session.Restore()); return R.Finish();
    }
    Platform->Tags.Add(TEXT("DWGatheringRuntimeQA"));
    Platform->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Platform->GetStaticMeshComponent()->SetStaticMesh(Cube);
    Platform->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
    const FVector PlatformCenter = Player->GetActorLocation() + FVector(10000., 10000., 2000.);
    Platform->SetActorLocation(PlatformCenter); Platform->SetActorScale3D(FVector(60., 60., 1.));
    const FVector Floor = PlatformCenter + FVector(0., 0., 50.);
    const float HalfHeight = Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    const FVector Home = Floor + FVector(0., 0., HalfHeight + 2.);
    Player->SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);
    Player->GetCharacterMovement()->StopMovementImmediately();
    R.Check(TEXT("empty_fixture_inventory"), Bag->SetSlots({}));

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Tree = World->SpawnActor<ADWGatheringResource>(ADWGatheringResource::StaticClass(), Floor + FVector(140., 0., 0.), FRotator::ZeroRotator, Spawn);
    R.Check(TEXT("runtime_tree_created"), IsValid(Tree));
    if (Tree)
    {
        R.Check(TEXT("wrong_pickaxe_rejected_on_tree"), !Tree->ApplyToolHit(Player, TEXT("Pickaxe"), 25., 250.f));
        R.Number(TEXT("wrong_tool_preserves_durability"), Tree->Durability, 100.);
        for (int32 Hit = 1; Hit <= 4; ++Hit)
        {
            R.Check(FString::Printf(TEXT("tree_hit_%d_accepted"), Hit), Tree->ApplyToolHit(Player, TEXT("Axe"), 25., 250.f));
            R.Number(FString::Printf(TEXT("tree_hit_%d_durability"), Hit), Tree->Durability, 100. - 25. * Hit);
        }
        R.Check(TEXT("exhaustion_committed_before_drops"), Tree->Durability == 0. && !Tree->bDropsReleased && !Tree->IsInteractionAvailable());
        R.Check(TEXT("exhaustion_has_pending_drop_timer"), Tree->GetPendingDropSeconds() >= 0.);
        R.Check(TEXT("tree_release_actual_drops"), Tree->ReleaseDrops());
        R.Check(TEXT("tree_release_cannot_repeat"), !Tree->ReleaseDrops());
        int32 StickWorld = 0, ChunkWorld = 0;
        TArray<TWeakObjectPtr<ADWGatheringPickup>> Drops;
        for (TActorIterator<ADWGatheringPickup> It(World); It; ++It)
            if (!Session.InitialActors.Contains(*It))
            {
                Drops.Add(*It);
                if (It->ItemId == TEXT("SugarStick")) StickWorld += It->Quantity;
                if (It->ItemId == TEXT("SugarChunk")) ChunkWorld += It->Quantity;
            }
        R.Number(TEXT("tree_world_sugar_sticks"), StickWorld, 2.);
        R.Number(TEXT("tree_world_sugar_chunks"), ChunkWorld, 3.);
        for (auto Drop : Drops) if (Drop.IsValid())
        {
            Player->SetActorLocation(Drop->GetActorLocation() + FVector(0., 0., HalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
            R.Check(TEXT("tree_drop_collect_") + Drop->ItemId.ToString(), Drop->TryCollect(Player));
            R.Check(TEXT("depleted_pickup_retained"), Drop.IsValid() && !Drop->IsActorBeingDestroyed() && Drop->Quantity == 0);
        }
        R.Number(TEXT("tree_inventory_sugar_sticks"), Bag->CountItem(TEXT("SugarStick")), 2.);
        R.Number(TEXT("tree_inventory_sugar_chunks"), Bag->CountItem(TEXT("SugarChunk")), 3.);
    }
    Player->SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);

    R.Check(TEXT("flour_node_inventory_fixture"), Bag->SetSlots({}));
    auto* FlourNode = World->SpawnActor<ADWGatheringResource>(ADWGatheringResource::StaticClass(), Floor + FVector(-140., 0., 0.), FRotator::ZeroRotator, Spawn);
    if (FlourNode)
    {
        FlourNode->RequiredTool = TEXT("Pickaxe");
        FlourNode->Drops = {FDWItemStack(TEXT("Flour"), 3)};
        R.Check(TEXT("axe_rejected_on_clumped_flour"), !FlourNode->ApplyToolHit(Player, TEXT("Axe"), 25., 250.f));
        for (int32 Hit = 1; Hit <= 4; ++Hit)
            R.Check(FString::Printf(TEXT("flour_pickaxe_hit_%d"), Hit), FlourNode->ApplyToolHit(Player, TEXT("Pickaxe"), 25., 250.f));
        R.Number(TEXT("flour_node_exhausted"), FlourNode->Durability, 0.);
        R.Check(TEXT("flour_node_actual_release"), FlourNode->ReleaseDrops());
        int32 FlourWorld = 0;
        TArray<TWeakObjectPtr<ADWGatheringPickup>> FlourDrops;
        for (TActorIterator<ADWGatheringPickup> It(World); It; ++It)
            if (!Session.InitialActors.Contains(*It) && It->ItemId == TEXT("Flour") && It->Quantity > 0)
            {FlourWorld += It->Quantity; FlourDrops.Add(*It);}
        R.Number(TEXT("flour_node_world_output_three"), FlourWorld, 3.);
        for (auto Drop : FlourDrops) if (Drop.IsValid())
        {
            Player->SetActorLocation(Drop->GetActorLocation() + FVector(0., 0., HalfHeight), false, nullptr, ETeleportType::TeleportPhysics);
            R.Check(TEXT("flour_node_output_collect"), Drop->TryCollect(Player));
        }
        R.Number(TEXT("flour_node_inventory_output_three"), Bag->CountItem(TEXT("Flour")), 3.);
    }
    else R.Check(TEXT("flour_node_created"), false);
    Player->SetActorLocation(Home, false, nullptr, ETeleportType::TeleportPhysics);

    TArray<FDWItemStack> FullSlots;
    const int32 FlourCap = Bag->GetEffectiveStackLimit(TEXT("Flour"));
    for (int32 Index = 0; Index < Profile->MaxInventorySlots; ++Index) FullSlots.Add(FDWItemStack(TEXT("Flour"), FlourCap - (Index == 0 ? 1 : 0)));
    R.Check(TEXT("partial_pickup_fixture"), Bag->SetSlots(FullSlots));
    auto* Partial = World->SpawnActor<ADWGatheringPickup>(ADWGatheringPickup::StaticClass(), Floor + FVector(0., 100., 20.), FRotator::ZeroRotator, Spawn);
    if (Partial)
    {
        Partial->ItemId = TEXT("Flour"); Partial->Quantity = 3; Partial->RefreshPickup();
        const int32 Before = Bag->CountItem(TEXT("Flour"));
        R.Check(TEXT("partial_pickup_accepted"), Partial->TryCollect(Player));
        R.Number(TEXT("partial_pickup_one_enters_inventory"), Bag->CountItem(TEXT("Flour")) - Before, 1.);
        R.Number(TEXT("partial_pickup_two_remain_world"), Partial->Quantity, 2.);
        R.Check(TEXT("full_inventory_rejects_without_world_loss"), !Partial->TryCollect(Player));
        R.Number(TEXT("full_inventory_world_remainder_unchanged"), Partial->Quantity, 2.);
    }
    else R.Check(TEXT("partial_pickup_actor_created"), false);

    R.Check(TEXT("discard_fixture"), Bag->SetSlots({FDWItemStack(TEXT("SugarChunk"), 3)}));
    FDWInventorySlotSnapshot DiscardSnapshot;
    const bool SnapshotReady = Bag->CaptureSlotSnapshot(0, DiscardSnapshot);
    int32 WorldChunksBefore = 0;
    for (TActorIterator<ADWGatheringPickup> It(World); It; ++It) if (It->ItemId == TEXT("SugarChunk")) WorldChunksBefore += It->Quantity;
    R.Check(TEXT("discard_snapshot_captured"), SnapshotReady);
    R.Check(TEXT("discard_to_real_world"), SnapshotReady && Bag->TryDiscardSnapshot(DiscardSnapshot));
    int32 WorldChunksAfter = 0;
    for (TActorIterator<ADWGatheringPickup> It(World); It; ++It) if (It->ItemId == TEXT("SugarChunk")) WorldChunksAfter += It->Quantity;
    R.Number(TEXT("discard_inventory_zero"), Bag->CountItem(TEXT("SugarChunk")), 0.);
    R.Number(TEXT("discard_world_delta"), WorldChunksAfter - WorldChunksBefore, 3.);
    R.Number(TEXT("discard_quantity_conserved"), Bag->CountItem(TEXT("SugarChunk")) + WorldChunksAfter - WorldChunksBefore, 3.);

    const FDWRecipeDefinition* TableRecipe = nullptr;
    const FDWRecipeDefinition* BuildingRecipe = nullptr;
    for (const auto& Recipe : Profile->Recipes)
    {
        if (!TableRecipe && Profile->GetRequiredStation(Recipe.RecipeId) == TEXT("GatherStation_CraftingTable") && Profile->GetBuildingOutput(Recipe.RecipeId).IsNone()) TableRecipe = &Recipe;
        if (!BuildingRecipe && Profile->GetBuildingOutput(Recipe.RecipeId) == TEXT("Furnace")) BuildingRecipe = &Recipe;
    }
    R.Check(TEXT("table_recipe_exists"), TableRecipe != nullptr);
    auto* Station = World->SpawnActor<ADWGatheringStation>(ADWGatheringStation::StaticClass(), Floor + FVector(0., -120., 50.), FRotator::ZeroRotator, Spawn);
    if (Station) { Station->ConfigureBuilding(TEXT("CraftingTable")); Station->bStationEnabled = false; Station->PublishInteractionState(); }
    if (TableRecipe && Station)
    {
        R.Check(TEXT("table_recipe_exact_inputs"), GiveRecipeInputs(Bag, *TableRecipe));
        R.Check(TEXT("materials_without_station_rejected"), !Bag->CanCraft(TableRecipe->RecipeId, Player->bYeastForm));
        Station->bStationEnabled = true; Station->PublishInteractionState();
        R.Check(TEXT("materials_with_station_can_craft"), Bag->CanCraft(TableRecipe->RecipeId, Player->bYeastForm));
        R.Check(TEXT("station_recipe_real_craft"), Player->CraftRecipe(TableRecipe->RecipeId));
        for (const auto& Output : TableRecipe->Outputs) R.Number(TEXT("station_craft_output_") + Output.ItemId.ToString(), Bag->CountItem(Output.ItemId), Output.Quantity);
        R.Check(TEXT("station_without_materials_rejected"), !Bag->CanCraft(TableRecipe->RecipeId, Player->bYeastForm));
    }
    R.Check(TEXT("building_recipe_exists"), BuildingRecipe != nullptr);
    if (BuildingRecipe)
    {
        R.Check(TEXT("building_recipe_exact_inputs"), GiveRecipeInputs(Bag, *BuildingRecipe));
        int32 BuildingsBefore = 0;
        for (TActorIterator<ADWGatheringStation> It(World); It; ++It) if (It->BuildingItemId == TEXT("Furnace") && It->bStationEnabled) ++BuildingsBefore;
        R.Check(TEXT("building_material_station_placement_gate"), Bag->CanCraft(BuildingRecipe->RecipeId, Player->bYeastForm));
        R.Check(TEXT("building_real_craft"), Player->CraftRecipe(BuildingRecipe->RecipeId));
        int32 BuildingsAfter = 0;
        for (TActorIterator<ADWGatheringStation> It(World); It; ++It) if (It->BuildingItemId == TEXT("Furnace") && It->bStationEnabled) ++BuildingsAfter;
        R.Number(TEXT("one_furnace_added_to_world"), BuildingsAfter - BuildingsBefore, 1.);
        R.Number(TEXT("building_never_added_to_inventory"), Bag->CountItem(TEXT("Furnace")), 0.);
        for (const auto& Input : BuildingRecipe->Inputs) R.Number(TEXT("building_spent_") + Input.ItemId.ToString(), Bag->CountItem(Input.ItemId), 0.);
    }

    auto* Pond = World->SpawnActor<ADWGatheringPond>(ADWGatheringPond::StaticClass(), Floor + FVector(-1000., 0., 0.), FRotator::ZeroRotator, Spawn);
    if (Pond)
    {
        Pond->RestoreWater(300.);
        Player->SetActorLocation(Pond->GetActorLocation() + FVector(0., 0., HalfHeight + 2.), false, nullptr, ETeleportType::TeleportPhysics);
        R.Check(TEXT("pond_foot_height_contains_player"), Pond->ContainsActor(Player));
        R.Check(TEXT("wet_pond_is_water_station"), Pond->IsUsableWaterStation(Player));
        Pond->Tick(0.f);
        R.Number(TEXT("pond_applies_twenty_percent_slow"), Player->GetGatheringEnvironmentMovementMultiplier(), .8);
        FDWCombatPlayerVitalsConfig VitalsConfig; FString VitalsError;
        const bool ConfigReady = GI->GetCombatRules() && FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(), VitalsConfig, VitalsError);
        R.Check(TEXT("real_vitals_config_available"), ConfigReady, VitalsError);
        const double WaterBefore = Player->GetCombatWater(), ReserveBefore = Pond->GetRemainingWater();
        double Added = -1.; const bool Refilled = Player->TryReceiveGatheringWater(*Pond, 20., Added);
        const double ExpectedAdded = ConfigReady ? FMath::Min(20., FMath::Max(0., VitalsConfig.Vitals.WaterMax - WaterBefore)) : 0.;
        R.Root->SetStringField(TEXT("water_refill_boundary"), ExpectedAdded > 0. ?
            TEXT("Positive real-capacity refill exercised; 20/s scheduling is separately tested.") :
            TEXT("Player was full: only zero-cost no-op exercised. Let natural thirst drain before a second PIE verification."));
        R.Check(TEXT("pond_refill_result_matches_real_capacity"), Refilled == (ExpectedAdded > 0.));
        R.Number(TEXT("pond_actual_water_received"), Added, ExpectedAdded);
        R.Number(TEXT("pond_player_water_delta"), Player->GetCombatWater() - WaterBefore, Added);
        R.Number(TEXT("pond_reserve_delta_matches_received"), ReserveBefore - Pond->GetRemainingWater(), Added);
        R.Check(TEXT("pond_dry_fixture"), Pond->RestoreWater(0.)); Pond->Tick(0.f);
        R.Check(TEXT("dry_pond_not_crafting_station"), !Pond->IsUsableWaterStation(Player));
        R.Number(TEXT("dry_pond_removes_slow"), Player->GetGatheringEnvironmentMovementMultiplier(), 1.);
        Added = -1.; R.Check(TEXT("dry_pond_cannot_grant_water"), !Player->TryReceiveGatheringWater(*Pond, 20., Added));
        R.Number(TEXT("dry_pond_grants_zero"), Added, 0.);
    }
    else R.Check(TEXT("pond_fixture_spawn"), false);

    // INDEX_NONE was verified before entering. Even a missing preview guard cannot target a real slot.
    const bool Saved = GI->SaveCurrentGame();
    const FString SaveError = GI->LastSaveError.ToString();
    R.Check(TEXT("preview_save_explicitly_rejected"), !Saved && (SaveError.Contains(TEXT("资源测试")) || SaveError.Contains(TEXT("gathering"), ESearchCase::IgnoreCase)), SaveError);
    R.Check(TEXT("active_save_slot_remains_unselected"), !GI->HasActiveSlot());
    R.Check(TEXT("restore_original_inventory_and_transform"), Session.Restore());
    R.Root->SetStringField(TEXT("remaining_state_change"), TEXT("Pond test may refill existing player water; no vitals snapshot rewrite. Stop PIE to reset test world."));
    return R.Finish();
}
