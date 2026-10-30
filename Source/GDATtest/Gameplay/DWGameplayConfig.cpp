#include "DWGameplayConfig.h"
#include "DWRetiredItemPolicy.h"
#include "DWBuildingDefinition.h"
#include "UObject/ConstructorHelpers.h"
#include "Sound/SoundBase.h"

namespace
{
    // Pure data defaults also support legacy data-table rows whose new English field is empty.
    FText DefaultEnglishItemName(FName ItemId)
    {
        for (const TCHAR* Name : {TEXT("Flour"), TEXT("Water"), TEXT("Dough"), TEXT("Yeast"), TEXT("Alcohol")})
            if (ItemId == FName(Name)) return FText::FromString(Name);
        return FText::GetEmpty();
    }

    FText DefaultEnglishRecipeName(FName RecipeId)
    {
        if (RecipeId == TEXT("CraftDough")) return FText::FromString(TEXT("Dough"));
        if (RecipeId == TEXT("CraftAlcohol")) return FText::FromString(TEXT("Alcohol"));
        return FText::GetEmpty();
    }
}

UDWGameplayConfig::UDWGameplayConfig()
{
    static ConstructorHelpers::FObjectFinder<USoundBase> CombatMusic(TEXT("/Game/DoughWorld/Audio/Music/Dune_Patrol.Dune_Patrol"));
    CombatBGM=CombatMusic.Object;
    auto AddItem = [this](const TCHAR* Id, const TCHAR* Name, FLinearColor Color, bool bUsable = false, float Heal = 0.f)
    {
        FDWItemDefinition Item;
        Item.ItemId = FName(Id);
        Item.DisplayName = FText::FromString(Name);
        Item.EnglishDisplayName = DefaultEnglishItemName(Item.ItemId);
        Item.IconColor = Color;
        Item.bUsable = bUsable;
        Item.HealAmount = Heal;
        Items.Add(Item);
    };
    AddItem(TEXT("Flour"), TEXT("面粉"), FLinearColor(0.92f, 0.84f, 0.63f));
    AddItem(TEXT("Water"), TEXT("水"), FLinearColor(0.20f, 0.65f, 1.f));
    AddItem(TEXT("Dough"), TEXT("面团"), FLinearColor(0.85f, 0.57f, 0.29f), true, 10.f);
    AddItem(TEXT("Yeast"), TEXT("酵母"), FLinearColor(0.57f, 0.86f, 0.30f));
    AddItem(TEXT("Alcohol"), TEXT("酒精"), FLinearColor(0.63f, 0.38f, 0.95f));

    FDWRecipeDefinition Dough;
    Dough.RecipeId = TEXT("CraftDough");
    Dough.DisplayName = FText::FromString(TEXT("面团"));
    Dough.EnglishDisplayName = FText::FromString(TEXT("Dough"));
    Dough.Inputs = {FDWItemStack(TEXT("Flour"), 2), FDWItemStack(TEXT("Water"), 2)};
    Dough.Outputs = {FDWItemStack(TEXT("Dough"), 1)};
    Recipes.Add(Dough);

    FDWRecipeDefinition Alcohol;
    Alcohol.RecipeId = TEXT("CraftAlcohol");
    Alcohol.DisplayName = FText::FromString(TEXT("酒精"));
    Alcohol.EnglishDisplayName = FText::FromString(TEXT("Alcohol"));
    Alcohol.Inputs = {FDWItemStack(TEXT("Dough"), 3), FDWItemStack(TEXT("Yeast"), 2)};
    Alcohol.Outputs = {FDWItemStack(TEXT("Alcohol"), 1)};
    Recipes.Add(Alcohol);
}

const FDWItemDefinition* UDWGameplayConfig::GetItemDefinition(FName ItemId) const
{
    if(DWRetiredItemPolicy::IsRetired(ItemId))return nullptr;
    return Items.FindByPredicate([ItemId](const FDWItemDefinition& Item) { return Item.ItemId == ItemId; });
}

const FDWRecipeDefinition* UDWGameplayConfig::GetRecipeDefinition(FName RecipeId) const
{
    return Recipes.FindByPredicate([RecipeId](const FDWRecipeDefinition& Recipe) { return Recipe.RecipeId == RecipeId&&!DWRetiredItemPolicy::RecipeUsesRetired(Recipe); });
}

void UDWGameplayConfig::RefreshDefinitionsFromTables()
{
    if (ItemTable && ItemTable->GetRowStruct() && ItemTable->GetRowStruct()->IsChildOf(FDWItemDefinition::StaticStruct()))
    {
        TArray<FDWItemDefinition> NewItems;
        TSet<FName> Ids;
        bool bValid = true;
        for (const TPair<FName, uint8*>& Row : ItemTable->GetRowMap())
        {
            FDWItemDefinition Item = *reinterpret_cast<const FDWItemDefinition*>(Row.Value);
            if (Item.ItemId.IsNone()) Item.ItemId = Row.Key;
            if (Item.EnglishDisplayName.IsEmpty()) Item.EnglishDisplayName = DefaultEnglishItemName(Item.ItemId);
            if (Ids.Contains(Item.ItemId) || !FMath::IsFinite(Item.HealAmount) || Item.HealAmount < 0.f ||
                !FMath::IsFinite(Item.TransformationGainPercent) || Item.TransformationGainPercent < 0.f)
            {
                bValid = false;
                break;
            }
            Ids.Add(Item.ItemId);
            NewItems.Add(Item);
        }
        if (bValid && !NewItems.IsEmpty()) Items = MoveTemp(NewItems);
        else UE_LOG(LogTemp, Warning, TEXT("DW item table is empty or invalid; retaining configured item definitions."));
    }
    else if (ItemTable)
    {
        UE_LOG(LogTemp, Warning, TEXT("DW ItemTable has an incompatible row struct; retaining configured item definitions."));
    }

    if (RecipeTable && RecipeTable->GetRowStruct() && RecipeTable->GetRowStruct()->IsChildOf(FDWRecipeDefinition::StaticStruct()))
    {
        TArray<FDWRecipeDefinition> NewRecipes;
        TSet<FName> Ids;
        bool bValid = true;
        for (const TPair<FName, uint8*>& Row : RecipeTable->GetRowMap())
        {
            FDWRecipeDefinition Recipe = *reinterpret_cast<const FDWRecipeDefinition*>(Row.Value);
            if (Recipe.RecipeId.IsNone()) Recipe.RecipeId = Row.Key;
            if (Recipe.EnglishDisplayName.IsEmpty()) Recipe.EnglishDisplayName = DefaultEnglishRecipeName(Recipe.RecipeId);
            if (Ids.Contains(Recipe.RecipeId) || Recipe.Inputs.IsEmpty() || Recipe.Outputs.IsEmpty())
            {
                bValid = false;
                break;
            }
            for (const FDWItemStack& Stack : Recipe.Inputs)
                bValid = bValid && Stack.Quantity > 0 && GetItemDefinition(Stack.ItemId) != nullptr;
            for (const FDWItemStack& Stack : Recipe.Outputs)
                bValid = bValid && Stack.Quantity > 0 && GetItemDefinition(Stack.ItemId) != nullptr;
            if (!bValid) break;
            Ids.Add(Recipe.RecipeId);
            NewRecipes.Add(Recipe);
        }
        if (bValid && !NewRecipes.IsEmpty()) Recipes = MoveTemp(NewRecipes);
        else UE_LOG(LogTemp, Warning, TEXT("DW recipe table is empty or invalid; retaining configured recipe definitions."));
    }
    else if (RecipeTable)
    {
        UE_LOG(LogTemp, Warning, TEXT("DW RecipeTable has an incompatible row struct; retaining configured recipe definitions."));
    }
}

UDWBuildingDefinition* UDWGameplayConfig::GetBuildingDefinition(FName ItemId) const
{
    if(ItemId.IsNone())return nullptr;
    UDWBuildingDefinition* Found=nullptr;
    for(const auto& Reference:BuildingDefinitions)
        if(auto* Definition=Reference.LoadSynchronous();Definition&&Definition->BuildingItemId==ItemId)
        {if(Found&&Found!=Definition)return nullptr;Found=Definition;}
    return Found;
}

bool UDWGameplayConfig::IsInventoryItem(FName ItemId) const
{return !ItemId.IsNone()&&GetItemDefinition(ItemId)&&(!NonInventoryItems.Contains(ItemId)||GetBuildingDefinition(ItemId));}

int32 UDWGameplayConfig::GetConfiguredStackLimit(FName ItemId) const
{
    if(!IsInventoryItem(ItemId))return 0;
    if(const auto* Definition=GetBuildingDefinition(ItemId))return Definition->StackLimit;
    const int32* Limit=StackLimits.Find(ItemId);
    return Limit?*Limit:MaxStackSize;
}

FName UDWGameplayConfig::GetRequiredStation(FName RecipeId) const
{const FName* Value=RecipeStations.Find(RecipeId);return Value?*Value:NAME_None;}

FName UDWGameplayConfig::GetBuildingOutput(FName RecipeId) const
{const FName* Value=RecipeBuildings.Find(RecipeId);return Value?*Value:NAME_None;}

bool UDWGameplayConfig::ValidateGatheringDefinitions(FString& Error) const
{
    Error.Reset();
    const auto Fail=[&](const FString& Message){Error=Message;return false;};
    if(MaxInventorySlots<1||MaxStackSize<1)return Fail(TEXT("Invalid gathering inventory capacity."));
    TSet<FName> ItemIds,RecipeIds;
    for(const auto& Item:Items)
    {
        if(Item.ItemId.IsNone()||ItemIds.Contains(Item.ItemId))return Fail(TEXT("Empty or duplicate gathering item identity."));
        ItemIds.Add(Item.ItemId);
    }
    for(FName Id:NonInventoryItems)
        if(Id.IsNone()||!ItemIds.Contains(Id))return Fail(TEXT("Unknown non-inventory building identity."));
    for(const auto& Pair:StackLimits)
        if(!IsInventoryItem(Pair.Key)||Pair.Value<1)return Fail(TEXT("Invalid gathering per-item stack limit."));
    for(const auto& Stack:StarterItems)
        if(!IsInventoryItem(Stack.ItemId)||Stack.Quantity<1)return Fail(TEXT("Invalid gathering starter item."));
    for(const auto& Pair:Tools)
    {
        const auto& Tool=Pair.Value;
        const bool bKnownType=Tool.ToolType==TEXT("Axe")||Tool.ToolType==TEXT("Pickaxe")||Tool.ToolType==TEXT("Hammer")||Tool.ToolType==TEXT("Spear");
        if(!IsInventoryItem(Pair.Key)||!bKnownType||!FMath::IsFinite(Tool.Sharpness)||Tool.Sharpness<=0.f||
            !FMath::IsFinite(Tool.UseIntervalSeconds)||Tool.UseIntervalSeconds<.05f||!FMath::IsFinite(Tool.GatheringSpeedMultiplier)||Tool.GatheringSpeedMultiplier<=0.f||!FMath::IsFinite(Tool.ReachCm)||Tool.ReachCm<=0.f)
            return Fail(TEXT("Invalid gathering tool definition."));
    }
    for(const auto& Recipe:Recipes)
    {
        if(Recipe.RecipeId.IsNone()||RecipeIds.Contains(Recipe.RecipeId)||Recipe.Inputs.IsEmpty()||Recipe.Outputs.IsEmpty())
            return Fail(TEXT("Invalid gathering recipe identity or empty recipe."));
        RecipeIds.Add(Recipe.RecipeId);
        for(const auto& Input:Recipe.Inputs)
            if(!IsInventoryItem(Input.ItemId)||Input.Quantity<1)return Fail(TEXT("Invalid gathering recipe input."));
        const FName Building=GetBuildingOutput(Recipe.RecipeId);
        if(!Building.IsNone())
        {
            const auto* Definition=GetBuildingDefinition(Building);
            FString DefinitionError;
            if(!Definition||!Definition->ValidateDefinition(DefinitionError)||Recipe.Outputs.Num()!=1||Recipe.Outputs[0].ItemId!=Building||Recipe.Outputs[0].Quantity!=1)
                return Fail(TEXT("A building recipe must produce one registered placeable building; automatic spawning is retired."));
        }
        else for(const auto& Output:Recipe.Outputs)
            if(!IsInventoryItem(Output.ItemId)||Output.Quantity<1)return Fail(TEXT("Invalid gathering recipe output."));
    }
    for(const auto& Pair:RecipeStations)
        if(!RecipeIds.Contains(Pair.Key)||Pair.Value.IsNone())return Fail(TEXT("Unknown recipe or empty station requirement."));
    for(const auto& Pair:RecipeBuildings)
        if(!RecipeIds.Contains(Pair.Key)||Pair.Value.IsNone())return Fail(TEXT("Unknown recipe or empty building output."));
    return true;
}
