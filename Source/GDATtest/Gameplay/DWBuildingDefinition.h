#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DWBuildingDefinition.generated.h"

class UStaticMesh;
class UMaterialInterface;
class ADWPlaceableBuilding;

USTRUCT(BlueprintType)
struct GDATTEST_API FDWBuildingMeshPart
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building") FName PartName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building") TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building") FTransform RelativeTransform = FTransform::Identity;
    /** Empty entries preserve the mesh's original material for that slot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building") TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
};

/** Visual/placement definition only. Item names/icons and recipes remain in GameplayConfig or its tables. */
UCLASS(BlueprintType)
class GDATTEST_API UDWBuildingDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Identity") FName BuildingItemId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Identity") FName StationId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Identity") TSoftClassPtr<ADWPlaceableBuilding> ActorClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Visual", meta=(TitleProperty="PartName")) TArray<FDWBuildingMeshPart> Parts;
    /** Local footprint is centred on XY and rests at Z=0; Z is HALF the occupied height. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Placement", meta=(Units="cm")) FVector FootprintHalfExtent = FVector(80,60,50);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Placement", meta=(ClampMin="20",Units="cm")) float MaxPlacementDistance = 600.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Placement", meta=(ClampMin="0",ClampMax="60",Units="Degrees")) float MaxSlopeDegrees = 15.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Placement", meta=(ClampMin="0",Units="cm")) float MaxSupportHeightDifference = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Interaction", meta=(ClampMin="1",Units="cm")) float InteractionRadius = 250.f;
    /** None means hand crafting this building item; ordinary recipe station rules still apply. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Crafting") FName RequiredCraftingStation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building|Inventory", meta=(ClampMin="1",ClampMax="999")) int32 StackLimit = 1;
    UFUNCTION(BlueprintPure, Category="Building") bool ValidateDefinition(FString& OutError) const;
};
