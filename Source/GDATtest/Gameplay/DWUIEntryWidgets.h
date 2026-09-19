#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DWGameplayTypes.h"
#include "DWUIEntryWidgets.generated.h"
class UButton; class UTextBlock; class UImage; class UBorder; class UDWGameplayWidget;

UCLASS(Blueprintable)
class GDATTEST_API UDWInventorySlotWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Inventory UI") void ApplyItemData(UDWGameplayWidget* OwnerScreen,int32 InIndex,const FDWItemStack& InItem,bool bSelected);
    UPROPERTY(BlueprintReadOnly,Category="Inventory UI") int32 SlotIndex=INDEX_NONE;
    UPROPERTY(BlueprintReadOnly,Category="Inventory UI") FDWItemStack Item;
    UFUNCTION(BlueprintImplementableEvent,Category="Inventory UI") void OnItemPresentationUpdated(bool bSelected);
protected:
    virtual void NativeConstruct() override;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> SlotButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UImage> ItemIcon;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> ItemNameText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> QuantityText;
    UPROPERTY(Transient) TObjectPtr<UDWGameplayWidget> Screen;
    UFUNCTION() void ClickSlot();
};

UCLASS(Blueprintable)
class GDATTEST_API UDWRecipeEntryWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Crafting UI") void ApplyRecipeData(UDWGameplayWidget* OwnerScreen,const FDWRecipeDefinition& InRecipe);
    UPROPERTY(BlueprintReadOnly,Category="Crafting UI") FName RecipeId;
    UFUNCTION(BlueprintImplementableEvent,Category="Crafting UI") void OnRecipePresentationUpdated(bool bCanCraft);
protected:
    virtual void NativeConstruct() override;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> RecipeNameText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> IngredientsText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> OutputsText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> RequirementText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> CraftButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> CraftButtonText;
    UPROPERTY(Transient) TObjectPtr<UDWGameplayWidget> Screen;
    UFUNCTION() void ClickCraft();
};

UCLASS(Blueprintable)
class GDATTEST_API UDWSaveSlotWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Save UI") void ApplySaveData(UDWGameplayWidget* OwnerScreen,const FDWSlotSummary& InSummary);
    UPROPERTY(BlueprintReadOnly,Category="Save UI") FDWSlotSummary Summary;
    UFUNCTION(BlueprintImplementableEvent,Category="Save UI") void OnSavePresentationUpdated(bool bConfirmingDelete);
protected:
    virtual void NativeConstruct() override;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> SlotNameText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> SlotDateText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> LoadButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> NewButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> DeleteButton;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UTextBlock> DeleteButtonText;
    UPROPERTY(meta=(BindWidgetOptional),BlueprintReadOnly) TObjectPtr<UButton> CancelDeleteButton;
    UPROPERTY(Transient) TObjectPtr<UDWGameplayWidget> Screen;
    bool bConfirmDelete=false;
    UFUNCTION() void ClickLoad(); UFUNCTION() void ClickNew(); UFUNCTION() void ClickDelete(); UFUNCTION() void ClickCancelDelete();
    void RefreshDeleteState();
};
