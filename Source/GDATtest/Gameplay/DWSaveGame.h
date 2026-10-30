#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DWGameplayTypes.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatWorldSave.h"
#include "DWAdventureFlowSave.h"
#include "DWFormalLanguageSave.h"
#include "DWWorldMapData.h"
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
    static constexpr int32 CurrentVersion = 5;
    // Schema-zero old slots never opt into the new opening or starter policy.
    UPROPERTY(SaveGame) FDWAdventureFlowSave AdventureFlow;
    UPROPERTY(SaveGame) FDWFormalLanguageSave FormalLanguage;
    /** Optional additive data. Missing field in old saves defaults to schema zero. */
    UPROPERTY(SaveGame) FDWWorldMapMarkerSave WorldMapMarkers;

    // Optional LANG-01 aggregate. Old adventure files keep schema zero and the original path.
    UPROPERTY(SaveGame) int32 LanguageTrialSchema=0;
    UPROPERTY(SaveGame) FString LanguageProgressJson;
    UPROPERTY(SaveGame) FString A01StateJson;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame, Category="Save")
    int32 Version = 4; // Preserve the original unbound/legacy construction format. NewGame explicitly chooses v5.
    // Additive resource subblock only. Missing schema is rejected without migrating/deleting files.
    UPROPERTY(SaveGame) FDWCombatPlayerResourceSave CombatPlayerResources;
    // Complete D22 aggregate; never independently load a participant.
    UPROPERTY(SaveGame) FDWCombatWorldSave CombatWorld;
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
