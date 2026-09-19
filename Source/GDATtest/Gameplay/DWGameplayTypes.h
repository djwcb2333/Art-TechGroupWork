#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DWGameplayTypes.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct GDATTEST_API FDWItemStack
{
    GENERATED_BODY()

    FDWItemStack() = default;
    FDWItemStack(FName InItemId, int32 InQuantity) : ItemId(InItemId), Quantity(InQuantity) {}

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Inventory")
    FName ItemId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Inventory", meta=(ClampMin="1"))
    int32 Quantity = 1;
};

USTRUCT(BlueprintType)
struct GDATTEST_API FDWItemDefinition : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item")
    FName ItemId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item|Localization")
    FText EnglishDisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item")
    FLinearColor IconColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item")
    TObjectPtr<UTexture2D> Icon = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item|Use")
    bool bUsable = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item|Use", meta=(ClampMin="0"))
    float HealAmount = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Item|Use", meta=(ClampMin="0"))
    float TransformationGainPercent = 0.f;
};

USTRUCT(BlueprintType)
struct GDATTEST_API FDWRecipeDefinition : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe")
    FName RecipeId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe|Localization")
    FText EnglishDisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe")
    TArray<FDWItemStack> Inputs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe")
    TArray<FDWItemStack> Outputs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe")
    bool bRequiresYeast = true;
};

USTRUCT(BlueprintType)
struct GDATTEST_API FDWPersistedActorState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    FString ActorId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") FTransform Transform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") bool bHasTransform=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") float SpawnProgress=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") bool bRuntimeSpawned=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") FString OwnerId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") FSoftClassPath ActorClass;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    float Health = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    bool bDestroyed = false;
};

USTRUCT(BlueprintType)
struct GDATTEST_API FDWSlotSummary
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Save")
    int32 SlotIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category="Save")
    bool bExists = false;

    UPROPERTY(BlueprintReadOnly, Category="Save")
    FString DisplayName;

    UPROPERTY(BlueprintReadOnly, Category="Save")
    FDateTime Timestamp;

    UPROPERTY(BlueprintReadOnly, Category="Save")
    FString MapPackage;
};
