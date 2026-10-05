#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWMinimapNavigation.generated.h"

UENUM(BlueprintType)
enum class EDWMinimapMarkerKind : uint8
{
    Landmark,
    Enemy,
    Resource
};

UENUM(BlueprintType)
enum class EDWMinimapMarkerShape : uint8
{
    Diamond,
    Circle,
    Triangle
};

/** Read-only world annotation. It never changes the owner's rendering, collision or save identity. */
UCLASS(ClassGroup=(DoughWorld), BlueprintType, Blueprintable,
    meta=(BlueprintSpawnableComponent, DisplayName="DW Minimap Marker"))
class GDATTEST_API UDWMinimapMarkerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UDWMinimapMarkerComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Marker")
    FText Label;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Marker")
    EDWMinimapMarkerKind Kind = EDWMinimapMarkerKind::Landmark;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Marker")
    EDWMinimapMarkerShape Shape = EDWMinimapMarkerShape::Diamond;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Marker")
    bool bUseCategoryColor = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Marker",
        meta=(EditCondition="!bUseCategoryColor", HideAlphaChannel))
    FLinearColor MarkerColor;

    /** Larger values draw later when icons overlap. The selected navigation target has separate priority. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Marker")
    int32 Priority = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Marker")
    bool bEnabled = true;

    /** Use existing Enemy/Nest IsAlive and Resource IsAvailable queries; no gameplay action is executed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Visibility")
    bool bRespectOwnerGameplayState = true;

    /** Hidden owners are omitted by default. Disable this for a deliberately invisible point of interest. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Visibility")
    bool bRespectOwnerVisibility = true;

    /** Empty means the owner's root. Only a SceneComponent on the same owner is accepted. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Placement",
        meta=(UseComponentPicker, AllowedClasses="/Script/Engine.SceneComponent"))
    FComponentReference DisplayAnchor;

    /** Added in world axes, in centimeters. It does not move the actor or anchor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Minimap|Placement", meta=(Units="cm"))
    FVector WorldOffset = FVector::ZeroVector;

    UFUNCTION(BlueprintPure, Category="Minimap|Marker")
    bool IsMarkerValid() const;

    UFUNCTION(BlueprintPure, Category="Minimap|Marker")
    FVector GetMarkerWorldPosition() const;

    UFUNCTION(BlueprintPure, Category="Minimap|Marker")
    FLinearColor GetMarkerColor() const;
};

/** One resolved navigation target. Display placement and physical destination remain separate. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWMinimapNavigationTarget
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    bool bValid = false;

    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    bool bIsActorTarget = false;

    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    FVector WorldPosition = FVector::ZeroVector;

    /** Actual destination used for distance/bearing; an Actor's visual marker offset does not affect it. */
    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    FVector NavigationPosition = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    FText Label;

    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    EDWMinimapMarkerKind Kind = EDWMinimapMarkerKind::Landmark;

    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    EDWMinimapMarkerShape Shape = EDWMinimapMarkerShape::Diamond;

    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    FLinearColor Color = FLinearColor::White;

    UPROPERTY(BlueprintReadOnly, Category="Minimap|Navigation")
    int32 Priority = 100;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWMinimapNavigationChanged);

/** Shared by the minimap and a future big map. State belongs to this World and is never written to a save. */
UCLASS(BlueprintType)
class GDATTEST_API UDWMinimapNavigationSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Typed access for Blueprint/Python callers; null when the context has no supported World. */
    UFUNCTION(BlueprintPure, Category="Minimap|Navigation", meta=(WorldContext="WorldContextObject"))
    static UDWMinimapNavigationSubsystem* GetNavigationSubsystem(const UObject* WorldContextObject);

    /** Select a world-space waypoint. Invalid coordinates leave the current target unchanged. */
    UFUNCTION(BlueprintCallable, Category="Minimap|Navigation")
    bool SetWaypoint(FVector WorldPosition, FText Label);

    /** Select a marker in this same World. Its position and editable annotation remain live. */
    UFUNCTION(BlueprintCallable, Category="Minimap|Navigation")
    bool SetActorTarget(UDWMinimapMarkerComponent* Marker);

    UFUNCTION(BlueprintCallable, Category="Minimap|Navigation")
    void ClearWaypoint();

    /** Resolves the actor's current position; querying a no-longer-valid target also clears it. */
    UFUNCTION(BlueprintPure, Category="Minimap|Navigation")
    FDWMinimapNavigationTarget GetNavigationTarget();

    /** The currently selected live actor marker, or null for a static waypoint/no selection. */
    UFUNCTION(BlueprintPure, Category="Minimap|Navigation")
    UDWMinimapMarkerComponent* GetTargetMarker();

    /** Selection, replacement or invalidation signal. Actor movement is resolved by GetNavigationTarget. */
    UPROPERTY(BlueprintAssignable, Category="Minimap|Navigation")
    FDWMinimapNavigationChanged OnNavigationTargetChanged;

    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    virtual void Deinitialize() override;
    virtual void OnWorldEndPlay(UWorld& InWorld) override;

protected:
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
    bool ValidateActorTarget();
    void ClearTargetWithoutNotification();

    UPROPERTY(Transient)
    TWeakObjectPtr<UDWMinimapMarkerComponent> ActorTarget;

    UPROPERTY(Transient)
    FDWMinimapNavigationTarget StaticTarget;

    bool bHasActorTarget = false;
};

/** Shared centimeter/compass conventions. NorthYaw=0 means +X north and +Y east. */
namespace DWMinimapMath
{
    GDATTEST_API float DistanceXYMeters(const FVector& From, const FVector& To);
    /** East/right and north/up distances in centimeters; independent of the gameplay camera. */
    GDATTEST_API FVector2D WorldToNorthUp(const FVector& From, const FVector& To, float NorthYaw = 0.f);
    /** Clockwise from north, in [0,360). A coincident point returns zero. */
    GDATTEST_API float BearingDegrees(const FVector& From, const FVector& To, float NorthYaw = 0.f);
    GDATTEST_API FLinearColor CategoryColor(EDWMinimapMarkerKind Kind);
}
