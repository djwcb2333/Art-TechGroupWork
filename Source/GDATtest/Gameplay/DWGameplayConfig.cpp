#include "DWGameplayConfig.h"

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
    return Items.FindByPredicate([ItemId](const FDWItemDefinition& Item) { return Item.ItemId == ItemId; });
}

const FDWRecipeDefinition* UDWGameplayConfig::GetRecipeDefinition(FName RecipeId) const
{
    return Recipes.FindByPredicate([RecipeId](const FDWRecipeDefinition& Recipe) { return Recipe.RecipeId == RecipeId; });
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
