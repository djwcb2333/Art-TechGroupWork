#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DWGatheringWorld.h"
#include "DWGatheringPresentationComponent.h"
#include "DWResourcePlacementRecipe.generated.h"

/** Declarative spawn-only recipe. It has no executable Blueprint construction. */
UCLASS(BlueprintType)
class GDATTEST_API UDWResourcePlacementRecipe : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Placement") TSoftClassPtr<ADWGatheringInteractable> ActorClass;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Visual") TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Visual") FTransform MeshRelativeTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Visual") TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Visual") float RandomScaleMin=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Visual") float RandomScaleMax=1.f;
    /** 0 disables the component distance limit; other culling systems may still apply. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Visual",meta=(ClampMin="0",Units="cm")) float MaxDrawDistanceCm=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Visual") bool bCastShadow=true;
    /** Preserve authored gameplay defaults unless an explicitly reviewed recipe opts in. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Gathering") bool bOverrideGatheringRules=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Gathering") FName RequiredTool;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Gathering") double MaxDurability=100.;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Gathering") TArray<FDWItemStack> Drops;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Gathering") TMap<FName,TSoftObjectPtr<UStaticMesh>> DropMeshes;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Gathering") TMap<FName,FTransform> DropMeshTransforms;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Pickup") FName PickupItem;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Pickup") int32 PickupQuantity=1;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") EDWGatheringDestructionStyle Style=EDWGatheringDestructionStyle::None;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UStaticMesh> StumpMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UStaticMesh> UpperTreeMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") FTransform StumpLocalTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") FTransform UpperTreeLocalTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TArray<FDWGatheringFragment> Fragments;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TArray<FDWGatheringDissolveSurface> DissolveSurfaces;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TArray<TSoftObjectPtr<UMaterialInterface>> SourceDissolveMaterials;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float TreePushSpeed=180.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float TreeAngularSpeed=120.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float UpperTreeMassKg=28.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float CrackSeparation=6.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float FragmentMassKg=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float FragmentOutwardSpeed=130.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float FragmentUpwardSpeed=95.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float LinearDamping=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float AngularDamping=.25f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float SurfaceFriction=.65f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float Restitution=.08f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float GroundNormalMinZ=.45f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float SettledContactSeconds=.25f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float RestBeforeDissolve=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float DissolveDuration=1.15f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") FLinearColor DissolveEdgeColor=FLinearColor(1.f,.82f,.08f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float DissolveEdgeWidth=.045f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float DissolveEdgeStrength=5.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UStaticMesh> DustMesh;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UMaterialInterface> DustMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") int32 DustCount=9;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float DustRadius=28.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") float DustDuration=.85f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation") FLinearColor DustColor=FLinearColor(.94f,.87f,.66f,1.f);
};
