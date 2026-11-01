#pragma once
#include "CoreMinimal.h"
#include "DWMapImage.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "DWWorldMapData.h"
#include "DWWorldMapMath.h"
#include "DWWorldMap.generated.h"

class APlayerController;
class ASceneCapture2D;
class UBoxComponent;
class USceneCaptureComponent2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
class UDWWorldMapSubsystem;

/** Prepared off-barrier; adoption moves native facts only and dispatches no callbacks. */
struct FDWWorldMapRestorePlan
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UDWWorldMapSubsystem> Owner;
    FString MapPackage;
    TArray<FDWWorldMapMarker> Markers;
    FGuid SelectedMarker;
    int32 Slot=INDEX_NONE;
    uint64 SourceRevision=0;
};

/** Optional per-level settings. The box is only used when bOverrideBounds is enabled. */
UCLASS(Blueprintable,meta=(DisplayName="DW World Map Bounds"))
class GDATTEST_API ADWWorldMapBounds : public AActor
{
    GENERATED_BODY()
public:
    ADWWorldMapBounds();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="World Map") TObjectPtr<UBoxComponent> BoundsBox;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Map|Bounds") bool bOverrideBounds=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Map|Bounds",meta=(ClampMin="0",Units="cm")) float PaddingCm=1000.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Map|Bounds",meta=(ClampMin="1000",Units="cm")) float MaxNonLandscapeHalfExtentCm=100000.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Map|Capture",meta=(ClampMin="256",ClampMax="2048")) int32 Resolution=1024;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Map|Navigation",meta=(ClampMin="1",ClampMax="1024")) int32 MaxMarkers=128;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="World Map|Navigation",meta=(ClampMin="1",ClampMax="32")) float MaxZoom=8.f;
};

/** A single, on-demand snapshot for the current World; never owns UI or pauses gameplay. */
UCLASS()
class GDATTEST_API UDWWorldMapSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="DoughWorld|World Map") bool OpenMap(APlayerController* LocalPlayer,FVector2D ViewportSize,FString& OutError);
    bool OpenMapDeferred(APlayerController* LocalPlayer,FVector2D ViewportSize,FString& OutError);
    bool CaptureDeferredMap(FVector2D ViewportSize,FString& OutError);
    bool PollCaptureReadiness(bool& bReady,FString& OutError);
    void PauseCaptureDeadline(double Seconds);
    UTexture2D* GetMapImage() const {return ImageResource?ImageResource->GetReadyTexture():nullptr;}
    const FDWMapImageSettings& GetImageSettings() const {return ImageSettings;}

    UFUNCTION(BlueprintCallable,Category="DoughWorld|World Map") void CloseMap();
    UFUNCTION(BlueprintCallable,Category="DoughWorld|World Map") bool RefreshMap(FString& OutError);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|World Map") bool SetViewportSize(FVector2D PixelSize);
    UFUNCTION(BlueprintPure,Category="DoughWorld|World Map") bool WorldToPixel(FVector Position,FVector2D& OutPixel) const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|World Map") bool PixelToWorld(FVector2D Pixel,FVector& OutPosition) const;
    UFUNCTION(BlueprintCallable,Category="DoughWorld|World Map") bool PanPixels(FVector2D DragDelta);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|World Map") bool ZoomAtPixel(float Factor,FVector2D Anchor);
    UFUNCTION(BlueprintPure,Category="DoughWorld|World Map") FVector2D WorldDirectionToScreen(FVector Direction) const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|World Map") UMaterialInterface* GetMapMaterial() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|World Map") bool IsMapOpen() const {return bOpen;}
    UFUNCTION(BlueprintPure,Category="DoughWorld|World Map") float GetZoom() const {return float(View.Zoom);}
    /** Valid only while this map is open; do not retain across world teardown. */
    const DWWorldMapMath::View& GetMotionEndView() const {return View;}
    const USceneCaptureComponent2D* GetMotionCaptureComponent() const;
    FGuid GetSelectedMarkerId() const;
    bool FindMarkerAtPixel(FVector2D Pixel,float Radius,FGuid& OutId) const;
    bool SelectMarkerAtPixel(FVector2D Pixel,float Radius,FGuid& OutId,FString& OutError);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|World Map") bool ToggleMarkerAtPixel(FVector2D Pixel,float HitRadiusPx,FString& OutError);
    UFUNCTION(BlueprintPure,Category="DoughWorld|World Map") TArray<FDWWorldMapMarker> GetMarkers() const;
    /** Native save-transaction hooks. Validate before applying; no disk IO occurs here. */
    bool PrepareLoadedMarkers(int32 Slot,const FDWWorldMapMarkerSave& Save,FDWWorldMapRestorePlan& Out,FString& Error) const;
    bool CanAdoptLoadedMarkers(const FDWWorldMapRestorePlan& Plan,FString& Error) const;
    void AdoptLoadedMarkersUnchecked(FDWWorldMapRestorePlan&& Plan);
    bool PublishLoadedMarkers(int32 Slot,TFunctionRef<bool()> IsCurrentRequest,FString& Error);
    bool MergeMarkersForSave(int32 Slot,const FDWWorldMapMarkerSave& Existing,FDWWorldMapMarkerSave& Out,FString& Error) const;
    uint64 GetMarkerRevision() const {return MarkerRevision;}
    UFUNCTION(BlueprintCallable,Category="DoughWorld|World Map|Authoring") static bool CreateWorldMapMaterial(FString& OutError);
    virtual void OnWorldEndPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
    bool OpenMapInternal(APlayerController* LocalPlayer,FVector2D ViewportSize,FString& OutError,bool bDeferred);
    UPROPERTY(Transient) TObjectPtr<UDWMapImageResource> ImageResource;
    FDWMapImageSettings ImageSettings;
    uint64 ImageGeneration=0;
    bool bDeferredFirstCapture=false,bCapturePending=false;
    double CapturePendingAt=0.;
    UPROPERTY(Transient) TObjectPtr<ASceneCapture2D> CaptureActor;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> DepthTarget;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY(Transient) TWeakObjectPtr<APlayerController> OwnerPlayer;
    UPROPERTY(Transient) TArray<FDWWorldMapMarker> Markers;
    DWWorldMapMath::View View;
    FString CurrentMapPackage;
    float GroundZ=0.f;
    int32 MarkerLimit=128,BoundSlot=INDEX_NONE;
    bool bOpen=false,bSlotBound=false,bUpdatingNavigation=false;
    FGuid SelectedMarker,PendingNavigationClear,PendingNavigationSelection;
    uint64 MarkerRevision=0;
    bool CanUseBoundSlot(FString& OutError) const;
    void UpdateMaterialView();
    UFUNCTION() void OnNavigationChanged();
};
