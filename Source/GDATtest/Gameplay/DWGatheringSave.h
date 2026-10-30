#pragma once
#include "CoreMinimal.h"
#include "DWGameplayTypes.h"
#include "DWGatheringSave.generated.h"

UENUM()
enum class EDWGatheringSavedKind : uint8 { Resource=1, Pickup=2, Station=3 };

/** Facts only. Authored actors are matched by stable id AND authored path/class. */
USTRUCT()
struct FDWGatheringActorSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FName Id;
    UPROPERTY(SaveGame) EDWGatheringSavedKind Kind=EDWGatheringSavedKind::Resource;
    UPROPERTY(SaveGame) bool bRuntimeSpawned=false;
    UPROPERTY(SaveGame) FString AuthoredPath;
    UPROPERTY(SaveGame) FString ClassPath;
    UPROPERTY(SaveGame) FTransform Transform=FTransform::Identity;
    UPROPERTY(SaveGame) FTransform MeshTransform=FTransform::Identity;
    UPROPERTY(SaveGame) double MaxDurability=0.;
    UPROPERTY(SaveGame) double Durability=0.;
    UPROPERTY(SaveGame) bool bDropsReleased=false;
    UPROPERTY(SaveGame) FName RequiredTool;
    UPROPERTY(SaveGame) TArray<FDWItemStack> Drops;
    UPROPERTY(SaveGame) FName ItemId;
    UPROPERTY(SaveGame) int32 Quantity=0;
    UPROPERTY(SaveGame) FSoftObjectPath PickupMesh;
    UPROPERTY(SaveGame) TArray<FSoftObjectPath> PickupMaterials;
    UPROPERTY(SaveGame) FName StationId;
    UPROPERTY(SaveGame) bool bEnabled=false;
    UPROPERTY(SaveGame) bool bPlaceableBuilding=false;
    UPROPERTY(SaveGame) FSoftObjectPath BuildingDefinition;
    // Default zero/empty preserves legacy saved buildings with unknown provenance.
    UPROPERTY(SaveGame) uint8 DemolitionCostOrigin=0;
    UPROPERTY(SaveGame) FName DemolitionCostRecipe;
    UPROPERTY(SaveGame) TArray<FDWItemStack> DemolitionCostInputs;
    UPROPERTY(SaveGame) bool bDemolished=false;
};

USTRUCT()
struct FDWGatheringPondPointSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FVector Position=FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector Arrive=FVector::ZeroVector;
    UPROPERTY(SaveGame) FVector Leave=FVector::ZeroVector;
    UPROPERTY(SaveGame) FRotator Rotation=FRotator::ZeroRotator;
    UPROPERTY(SaveGame) FVector Scale=FVector::OneVector;
    UPROPERTY(SaveGame) uint8 Type=0;
};

/** Only authored spline ponds. Geometry identifies the map asset; it is not applied from the save. */
USTRUCT()
struct FDWGatheringPondSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FName Id;
    UPROPERTY(SaveGame) FString AuthoredPath;
    UPROPERTY(SaveGame) FString ClassPath;
    UPROPERTY(SaveGame) FTransform Transform=FTransform::Identity;
    UPROPERTY(SaveGame) FTransform SplineTransform=FTransform::Identity;
    UPROPERTY(SaveGame) double InitialReserve=0.;
    UPROPERTY(SaveGame) double RemainingWater=0.;
    UPROPERTY(SaveGame) double Radius=0.;
    UPROPERTY(SaveGame) double WaterHeight=0.;
    UPROPERTY(SaveGame) double BedHeight=0.;
    UPROPERTY(SaveGame) double OuterGround=0.;
    UPROPERTY(SaveGame) double InnerSlope=0.;
    UPROPERTY(SaveGame) double CrestAbove=0.;
    UPROPERTY(SaveGame) double CrestDistance=0.;
    UPROPERTY(SaveGame) double OuterWidth=0.;
    UPROPERTY(SaveGame) double GridSpacing=0.;
    UPROPERTY(SaveGame) double Tiling=0.;
    UPROPERTY(SaveGame) double RefillRate=0.;
    UPROPERTY(SaveGame) double SlowFraction=0.;
    UPROPERTY(SaveGame) double SurfaceTolerance=0.;
    UPROPERTY(SaveGame) double StationExtraRadius=0.;
    UPROPERTY(SaveGame) int32 Samples=0;
    UPROPERTY(SaveGame) FSoftObjectPath WaterMaterial;
    UPROPERTY(SaveGame) FSoftObjectPath BankMaterial;
    UPROPERTY(SaveGame) bool bClosed=false;
    UPROPERTY(SaveGame) TArray<FDWGatheringPondPointSave> Points;
};

USTRUCT()
struct FDWGatheringWorldSave
{
    GENERATED_BODY()
    /** Zero is only the completely empty legacy block on a non-LANG map. */
    UPROPERTY(SaveGame) int32 Schema=0;
    UPROPERTY(SaveGame) FString MapPackage;
    UPROPERTY(SaveGame) FName ActiveTool;
    UPROPERTY(SaveGame) double ToolCooldownRemaining=0.;
    UPROPERTY(SaveGame) TArray<FDWGatheringActorSave> Actors;
    /** Empty for legacy schema 0/1. Schema 2 supports audited authored ponds. */
    UPROPERTY(SaveGame) TArray<FDWGatheringPondSave> Ponds;
};

