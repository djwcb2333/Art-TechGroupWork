#pragma once
#include "CoreMinimal.h"
#include "DWWorldMapData.generated.h"

/** A player-created point, independent of scene Actor identity or streaming. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWWorldMapMarker
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="World Map") FGuid Id;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="World Map") FVector Position=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="World Map") FText Label;
};

/** Optional validated exploration layer. Absent in legacy saves; art paths are not its identity. */
USTRUCT()
struct GDATTEST_API FDWMapExplorationLayer
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=0;
    UPROPERTY(SaveGame) int32 GridSize=0;
    UPROPERTY(SaveGame) FVector2D WorldCenterCm=FVector2D::ZeroVector;
    UPROPERTY(SaveGame) FVector2D WorldSizeCm=FVector2D::ZeroVector;
    UPROPERTY(SaveGame) float ImageUpYaw=0.f;
    UPROPERTY(SaveGame) TArray<uint8> Bits;
};

/** Markers for one normalized /Game/... map package. Empty on older saves. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWWorldMapMarkerSet
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="World Map") FString MapPackage;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="World Map") TArray<FDWWorldMapMarker> Markers;
    /** Optional active navigation target; invalid means no selected saved marker. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="World Map") FGuid SelectedMarker;
    UPROPERTY(SaveGame) TArray<FDWMapExplorationLayer> ExplorationLayers;
};

/** Schema 0 is the absent/empty field in a legacy save; schema 1 owns validated sets. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWWorldMapMarkerSave
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="World Map") int32 Schema=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,SaveGame,Category="World Map") TArray<FDWWorldMapMarkerSet> Sets;
};

namespace DWWorldMapData
{
    // Hard file-validation limits, independent of editable per-level UI limits.
    constexpr int32 MaxMaps=64,MaxPerMap=1024,MaxTotal=4096,MaxLabelLength=128;
    GDATTEST_API bool Validate(const TArray<FDWWorldMapMarkerSet>& Sets,FString& Error);
    GDATTEST_API bool ValidateSave(const FDWWorldMapMarkerSave& Save,FString& Error);
}
