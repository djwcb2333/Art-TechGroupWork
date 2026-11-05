#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "DWCraftingTypes.h"
#include "DWCraftingPanel.generated.h"

class UDWGameplayWidget;
class UDWCraftingPanel;
class UCanvasPanel;class UUniformGridPanel;class UVerticalBox;class UScrollBox;class UBorder;class UImage;class UTextBlock;class USizeBox;

UCLASS()
class GDATTEST_API UDWCraftingControl : public UButton
{
    GENERATED_BODY()
public:
    void Bind(UDWCraftingPanel* Panel,int32 Action,FName Recipe=NAME_None);
private:
    UPROPERTY(Transient) TWeakObjectPtr<UDWCraftingPanel> OwnerPanel;
    int32 Code=0;FName Id;
    UFUNCTION() void Dispatch();
};

USTRUCT()
struct FDWCraftingRecipeCell
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UDWCraftingControl> Button;
    UPROPERTY(Transient) TObjectPtr<UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Fallback;
    UPROPERTY(Transient) TObjectPtr<USizeBox> Box;
    FName Id;
};
USTRUCT()
struct FDWCraftingMaterialCell
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UBorder> Card;
    UPROPERTY(Transient) TObjectPtr<UBorder> IconFrame;
    UPROPERTY(Transient) TObjectPtr<UImage> Icon;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Need;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Have;
};

/** Isolated native view: no inventory mutation outside the player's existing transaction facade. */
UCLASS()
class GDATTEST_API UDWCraftingPanel : public UUserWidget
{
    GENERATED_BODY()
public:
    void Setup(UDWGameplayWidget* Screen);
    void Refresh(FName PreferredRecipe=NAME_None,bool Force=false);
    void Act(int32 Code,FName Recipe);
private:
    UPROPERTY(Transient) TWeakObjectPtr<UDWGameplayWidget> Screen;
    UPROPERTY(Transient) TObjectPtr<UUniformGridPanel> RecipeGrid;
    UPROPERTY(Transient) TObjectPtr<UUniformGridPanel> MaterialGrid;
    UPROPERTY(Transient) TObjectPtr<UScrollBox> MaterialScroll;
    UPROPERTY(Transient) TObjectPtr<UImage> ProductImage;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Description;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CountLabel;
    UPROPERTY(Transient) TObjectPtr<UDWCraftingControl> Minus;
    UPROPERTY(Transient) TObjectPtr<UDWCraftingControl> Plus;
    UPROPERTY(Transient) TObjectPtr<UDWCraftingControl> Craft;
    UPROPERTY(Transient) TObjectPtr<class UDWLanguageRecipeNeedsWidget> Needs;
    UPROPERTY(Transient) TArray<FDWCraftingRecipeCell> Recipes;
    UPROPERTY(Transient) TArray<FDWCraftingMaterialCell> Materials;
    UPROPERTY(Transient) TArray<TObjectPtr<UDWCraftingControl>> Categories;
    UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> CategoryLabels;
    EDWCraftingCategory Category=EDWCraftingCategory::All;
    FName Selected;
    int32 Count=1;
    FString LastSignature;
    double NextRefreshAt=0.;int64 LastRevision=-1;int32 LastLocale=-1;
    TArray<FName> LastVisible;
    EDWCraftingCategory Classify(FName ItemId) const;
    bool RecipeMatches(FName Id) const;
};
