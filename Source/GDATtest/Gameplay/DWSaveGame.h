#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DWGameplayTypes.h"
#include "DWSaveGame.generated.h"

USTRUCT(BlueprintType)
struct FDWMapSnapshot
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") FString MapPackage;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") TArray<FName> CompletedWorldEvents;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") TArray<FDWPersistedActorState> Actors;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") FTransform PlayerTransform=FTransform::Identity;
};
UCLASS(BlueprintType)
class GDATTEST_API UDWSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    static constexpr int32 CurrentVersion = 3;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="Save")
    int32 Version = CurrentVersion;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") TArray<FDWMapSnapshot> VisitedMaps;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="Save") TArray<FName> CompletedWorldEvents;


    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    FString DisplayName;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="Save")
    FDateTime Timestamp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    FString MapPackage;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    FTransform PlayerTransform = FTransform::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    float Health = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    float Transformation = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    float SprintAlcoholElapsed = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    float TransformationDecayElapsed = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    bool bYeastForm = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    TArray<FDWItemStack> Inventory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    TArray<FDWPersistedActorState> WorldActors;

    /** New slots use the map's authored PlayerStart until the first actual player save. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category="Save")
    bool bHasPlayerTransform = false;
};
