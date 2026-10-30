#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DWLootVisualRules.h"
#include "DWLootVisualCatalog.generated.h"

class UStaticMesh;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FDWLootVisualEntry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot") FName ItemId;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot") TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot") FTransform RelativeTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot") TMap<int32,TSoftObjectPtr<UMaterialInterface>> MaterialOverrides;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot") bool bUseBeacon=true;
    /** Exact legacy meshes which are safe to replace for this ID, e.g. the former SugarStick stump. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot") TArray<TSoftObjectPtr<UStaticMesh>> LegacyPlaceholderMeshes;
};

/** Art-only lookup; source of truth for item quantity and ownership remains gameplay. */
UCLASS(BlueprintType)
class GDATTEST_API UDWLootVisualCatalog : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot") TArray<FDWLootVisualEntry> Entries;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Beacon") TSoftObjectPtr<UStaticMesh> BeaconMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Beacon") TSoftObjectPtr<UMaterialInterface> BeaconMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Beacon") FTransform BeaconRelativeTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Death Bag") TSoftObjectPtr<UStaticMesh> DeathBagMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Death Bag") FTransform DeathBagTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Beacon") bool bDeathBagBeacon=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Beacon",meta=(ClampMin="0",Units="cm")) float BeaconMaxDistanceCm=3000.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Beacon",meta=(ClampMin="0",ClampMax="256")) int32 MaxVisibleBeacons=32;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Loot|Beacon",meta=(ClampMin="0.05",ClampMax="2",Units="s")) float BeaconRefreshSeconds=.25f;
    /** Null also means unknown ID; duplicated IDs are rejected rather than chosen arbitrarily. */
    const FDWLootVisualEntry* FindUnique(FName Id)const
    {
        if(Id.IsNone())return nullptr;
        std::size_t Index=0;
        const auto Result=DWLootVisualRules::FindUnique(Entries,Id,[](const FDWLootVisualEntry& E){return E.ItemId;},Index);
        return Result==DWLootVisualRules::LookupStatus::Found&&Index<static_cast<std::size_t>(Entries.Num())?&Entries[static_cast<int32>(Index)]:nullptr;
    }
};
