#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DWGameplayTypes.h"
#include "DWGameplayConfig.generated.h"

class USoundBase;

UCLASS(BlueprintType)
class GDATTEST_API UDWGameplayConfig : public UDataAsset
{
    GENERATED_BODY()

public:
    UDWGameplayConfig();
    /** Additional playable maps that may be saved and travelled to by the GameInstance. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Maps") TArray<TSoftObjectPtr<UWorld>> AdditionalGameplayMaps;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Health", meta=(ClampMin="1"))
    float MaxHealth = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Movement", meta=(ClampMin="1", Units="cm/s"))
    float BaseMoveSpeed = 600.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Sprint", meta=(ClampMin="1"))
    float SprintSpeedMultiplier = 1.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Sprint", meta=(ClampMin="0", Units="Percent"))
    float SprintHealthCostPercentPerSecond = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Sprint", meta=(ClampMin="0.01", Units="s"))
    float SprintAlcoholInterval = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Sprint", meta=(ClampMin="1"))
    int32 SprintAlcoholAmount = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Transformation", meta=(ClampMin="1"))
    float MaxTransformation = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Transformation", meta=(ClampMin="0", Units="Percent"))
    float YeastGainPercentPerItem = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Transformation", meta=(ClampMin="0", Units="Percent"))
    float AttackGainPercent = 2.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Transformation", meta=(ClampMin="0.01", Units="s"))
    float TransformationDecayInterval = 5.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Player|Transformation", meta=(ClampMin="0", Units="Percent"))
    float TransformationDecayPercent = 3.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory", meta=(ClampMin="1"))
    int32 MaxInventorySlots = 24;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory", meta=(ClampMin="1"))
    int32 MaxStackSize = 40;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory", meta=(TitleProperty="ItemId"))
    TArray<FDWItemDefinition> Items;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crafting", meta=(TitleProperty="RecipeId"))
    TArray<FDWRecipeDefinition> Recipes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inventory")
    TObjectPtr<UDataTable> ItemTable = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Crafting")
    TObjectPtr<UDataTable> RecipeTable = nullptr;

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Config")
    void RefreshDefinitionsFromTables();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maps")
    FString GameplayMap = TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GameplayPrototype");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Maps")
    FString MainMenuMap = TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GameplayPrototype");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|UI")
    TObjectPtr<USoundBase> UIClickSound = nullptr;

    /** Slot/recipe selection; separate from the generic button click. Empty slots stay silent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|UI")
    TObjectPtr<USoundBase> UISelectionSound = nullptr;

    /** General UI operation failure, such as an unsuccessful save/load/delete. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|UI")
    TObjectPtr<USoundBase> UIFailedSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> InventoryOpenSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> InventoryCloseSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> CraftingOpenSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> CraftingCloseSound = nullptr;

    /** Explicit fallback for menu panels with no dedicated open sound. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> MenuOpenSound = nullptr;

    /** Explicit fallback for menu panels with no dedicated close sound. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> MenuCloseSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> TitleOpenSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> TitleCloseSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> SaveSlotsOpenSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> SaveSlotsCloseSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> PauseOpenSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> PauseCloseSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> SettingsOpenSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> SettingsCloseSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> DefeatOpenSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Panels")
    TObjectPtr<USoundBase> DefeatCloseSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Inventory")
    TObjectPtr<USoundBase> ItemUseSuccessSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Inventory")
    TObjectPtr<USoundBase> ItemUseFailedSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Crafting")
    TObjectPtr<USoundBase> CraftSuccessSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Crafting")
    TObjectPtr<USoundBase> CraftFailedSound = nullptr;

    /** Once when a valid held harvest begins, not on every progress tick. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Gathering")
    TObjectPtr<USoundBase> GatherStartSound = nullptr;

    /** Sustained held-harvest sound. The player's audio component owns its loop and lifetime. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Gathering")
    TObjectPtr<USoundBase> GatherLoopSound = nullptr;

    /** Once after a full harvest transaction successfully adds items to the inventory. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Gathering")
    TObjectPtr<USoundBase> GatherSuccessSound = nullptr;

    /** Once when an active held harvest ends or loses its target. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Gathering")
    TObjectPtr<USoundBase> GatherStopSound = nullptr;

    /** Once for a rejected harvest attempt; the caller prevents repeated error sounds while held. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Gathering")
    TObjectPtr<USoundBase> GatherFailedSound = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
    TObjectPtr<USoundBase> GameplayBGM = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio")
    TObjectPtr<USoundBase> MenuBGM = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio", meta=(ClampMin="0", ClampMax="1"))
    float MusicVolume = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|UI", meta=(ClampMin="0", ClampMax="1"))
    float UIClickVolume = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|UI", meta=(ClampMin="0.5", ClampMax="2"))
    float UIClickPitch = 1.f;

    /** Shared gain for selection, panel transitions, item-use and crafting results. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|UI", meta=(ClampMin="0", ClampMax="1"))
    float UIEventVolume = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|UI", meta=(ClampMin="0.5", ClampMax="2"))
    float UIEventPitch = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Gathering", meta=(ClampMin="0", ClampMax="1"))
    float GatherSoundVolume = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Audio|Gathering", meta=(ClampMin="0.5", ClampMax="2"))
    float GatherSoundPitch = 1.f;

    const FDWItemDefinition* GetItemDefinition(FName ItemId) const;
    const FDWRecipeDefinition* GetRecipeDefinition(FName RecipeId) const;
};
