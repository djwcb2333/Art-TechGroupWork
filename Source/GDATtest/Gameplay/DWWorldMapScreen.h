#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"
#include "DWWorldMapTransitionMath.h"
#include "DWMapOpenPhases.h"
#include "DWMapMotionAdapter.h"
#include "DWMinimap.h"
#include "DWWorldMapScreen.generated.h"

class UDWWorldMapSubsystem;
class ADWGameplayHUD;
class UMaterialInterface;
class UTexture2D;
class APlayerController;
class UDWMinimapComponent;
class UDWMapTransitionCapture;
class UMaterialInstanceDynamic;

/** Runtime-only presentation. The existing gameplay WBP is not rebuilt. */
UCLASS()
class GDATTEST_API UDWWorldMapScreen : public UUserWidget
{
    GENERATED_BODY()
public:
    bool InitializeMap(ADWGameplayHUD* InHUD, FString& OutError);
    void SetSettingsOverlayActive(bool bActive);
    void RequestClose();
    void CloseMapView();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|World Map", meta=(ClampMin="0.0", Units="s"))
    float ExpandSeconds = .32f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|World Map", meta=(ClampMin="0.01", Units="s")) float MiniFadeSeconds=.14f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|World Map", meta=(ClampMin="0.01", Units="s")) float FullFadeSeconds=.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|World Map", meta=(ClampMin="256", ClampMax="1024"))
    int32 TransitionResolution = 512;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|World Map", meta=(ClampMin="1.0", ClampMax="80.0"))
    float MarkerHitRadius = 18.f;

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
        FSlateWindowElementList& DrawElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    virtual void NativeOnFocusLost(const FFocusEvent& Event) override;
    virtual void NativeDestruct() override;

private:
    FVector2D ContentSize(const FGeometry& Geometry) const;
    bool LocalPoint(const FGeometry& Geometry, FVector2D ScreenPoint, FVector2D& Out) const;
    void CaptureMinimapRect(APlayerController* PC);
    void ApplyTransition(FVector2D ViewSize,float Progress);
    void AbortTransition(const FString& Reason);
    TWeakObjectPtr<UDWWorldMapSubsystem> Map;
    TWeakObjectPtr<ADWGameplayHUD> HUD;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> MapImage;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> CloseArt;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> InitialMiniImage;
    FDWMapImageSettings InitialImageSettings;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> VertexTintMaterial;
    UPROPERTY(Transient) TObjectPtr<UDWMapTransitionCapture> TransitionCapture;
    TWeakObjectPtr<UDWMinimapComponent> MiniComponent;
    DWMapMotionAdapter::FFrame StartFrame,EndFrame,DisplayFrame;
    FVector2D MiniOrigin=FVector2D::ZeroVector,MiniSize=FVector2D::ZeroVector;
    FVector2D PressStart=FVector2D::ZeroVector;
    FGuid PulseMarker;
    double PulseAt=0.,PauseAt=0.;
    double MotionTime=0.,MotionProgress=0.;
    float DisplayAlpha=0.f;
    DWMapOpenPhases::Timeline OpeningTimeline;
    DWMapOpenPhases::Sample OpeningSample;
    bool bCaptureStarted=false,bMapCaptureReady=false;
    bool bMoved=false,bDisplayCommitted=false;
    TArray<FDWMinimapDisplayMarker> MiniMarkers;
    FDWMinimapNavigationTarget MiniTarget;
    FVector MiniPlayer=FVector::ZeroVector,MiniForward=FVector::ForwardVector,MiniNorth=FVector::ForwardVector;
    FSlateBrush MapBrush;
    FSlateBrush CloseBrush;
    DWWorldMapTransitionMath::Rect MinimapSnapshot;
    bool bMeasuredMinimapGeometry=false;
    FVector2D PreviousDrag = FVector2D::ZeroVector;
    FVector2D LastViewportSize = FVector2D::ZeroVector;
    bool bDragging = false;
    bool bSettingsOverlayActive = false;
    bool bOpen = false;
    bool bClosing = false;
    double OpenedAt = 0.;
    double CloseStartedAt = 0.;
    float CloseStartEase = 1.f;
    FText LastError;
};
