#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWMinimapNavigation.h"
#include "DWMinimap.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDWMinimapWidget;
class ASceneCapture2D;

/** Capture parameters for the last rendered background. All markers use this same snapshot. */
struct FDWMinimapProjection
{
    FVector Center=FVector::ZeroVector;
    FVector Right=FVector::YAxisVector;
    FVector Up=FVector::XAxisVector;
    float Width=10000.f;
    float NorthScale=1.f;
    FVector2D Direction(const FVector& WorldDirection) const
    {
        return FVector2D(FVector::DotProduct(WorldDirection,Right),-FVector::DotProduct(WorldDirection,Up)/NorthScale).GetSafeNormal();
    }
    FVector2D Project(const FVector& Position) const
    {
        const FVector D=Position-Center;
        return FVector2D(FVector::DotProduct(D,Right)/Width, -FVector::DotProduct(D,Up)/(Width*NorthScale));
    }
};

struct FDWMinimapDisplayMarker
{
    FVector WorldPosition=FVector::ZeroVector;
    FLinearColor Color=FLinearColor::White;
    EDWMinimapMarkerShape Shape=EDWMinimapMarkerShape::Diamond;
    int32 Priority=0;
};

/** Local-player map capture and independent HUD. Never changes original meshes, lighting or saves. */
UCLASS(BlueprintType,Blueprintable,ClassGroup=(DoughWorld),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWMinimapComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDWMinimapComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap") bool bMinimapEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|View",meta=(ClampMin="500",ClampMax="100000",Units="cm")) float ViewRadius=5000.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|View",meta=(ClampMin="-90",ClampMax="-45",Units="deg")) float CapturePitch=-70.f;
    /** Rotate the map with the owning player's camera yaw. Pitch stays at CapturePitch for legibility. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|View",meta=(DisplayName="Follow Camera Yaw")) bool bFollowCameraYaw=true;
    /** World north used by compass labels and bearings; also the fixed capture yaw when follow is disabled. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|View",meta=(Units="deg")) float NorthYaw=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|View",meta=(ClampMin="128",ClampMax="1024")) int32 CaptureResolution=1024;
    /** Color-space coverage samples per axis. 3 gives nine samples; 2 gives four; 1 disables supersampling. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|Art",meta=(ClampMin="1",ClampMax="3",DisplayName="Edge AA Sample Grid")) int32 EdgeSampleGrid=3;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|View",meta=(ClampMin="0.05",ClampMax="2",Units="s")) float CaptureInterval=0.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|UI",meta=(ClampMin="160",ClampMax="400")) float Diameter=300.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|UI") bool bMatchBrewingCardWidth=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|UI") FVector2D BottomRightMargin=FVector2D(24.f,76.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|UI") TSoftObjectPtr<UMaterialInterface> MapMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|UI") TSoftClassPtr<UDWMinimapWidget> WidgetClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|Art") FLinearColor GroundTint=FLinearColor(.48f,.43f,.36f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|Art") FLinearColor SurfaceTint=FLinearColor(.94f,.91f,.85f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|Art") FLinearColor WallTint=FLinearColor(.30f,.27f,.23f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|Markers") bool bShowExistingEnemies=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Minimap|Markers") bool bShowExistingResources=true;
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Minimap") void RefreshMinimap();
    UFUNCTION(BlueprintPure,Category="DoughWorld|Minimap|Diagnostics") int32 GetCaptureRequestCount() const {return CaptureRequestCount;}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Minimap|Diagnostics") bool IsMinimapVisible() const {return bVisible;}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Minimap|Diagnostics") UTextureRenderTarget2D* GetDepthRenderTarget() const {return DepthTarget;}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Minimap|Diagnostics") UDWMinimapWidget* GetMinimapWidget() const {return Widget;}
    const FDWMinimapProjection& GetProjection() const {return Projection;}
    void GatherDisplayMarkers(TArray<FDWMinimapDisplayMarker>& Out) const;
    FDWMinimapNavigationTarget GetNavigationTarget() const;
    FVector GetPlayerLocation() const;
    FVector2D GetPlayerForwardOnMap() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Minimap|Diagnostics") float GetCaptureYaw() const {return CapturedYaw;}
    FVector2D GetWorldDirectionOnMap(const FVector& Direction) const {return Projection.Direction(Direction);}
    UMaterialInstanceDynamic* GetDisplayMaterial() const {return DisplayMaterial;}
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Minimap|Authoring") static bool CreateMinimapMaterial();
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Minimap|Authoring") static bool CreateMinimapWidgetBlueprint();
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Minimap|Authoring") static bool ConfigureMinimapPresentation();
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Minimap|Authoring") static bool CaptureAuthoringScreenshot(const FString& Filename);
    virtual void Activate(bool bReset=false) override;
    virtual void Deactivate() override;
protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
private:
    UPROPERTY(Transient) TObjectPtr<ASceneCapture2D> CaptureActor;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> DepthTarget;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DisplayMaterial;
    UPROPERTY(Transient) TObjectPtr<UDWMinimapWidget> Widget;
    FDWMinimapProjection Projection;
    TArray<TWeakObjectPtr<AActor>> CachedActors;
    float CaptureElapsed=100.f,ScanElapsed=100.f;
    bool bVisible=false;
    int32 CaptureRequestCount=0;
    bool CanShow(bool bIgnoreCinematic=false) const;
    bool EnsureCapture();
    void ScanActors();
    void CaptureNow();
    float ResolveCaptureYaw() const;
    float CapturedYaw=0.f;
    void Cleanup();
};
