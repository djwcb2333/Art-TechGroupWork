#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWGatheringPond.generated.h"

class ADWPlayerCharacter;
class UBoxComponent;
class UChildActorComponent;
class USceneComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UDWInteractionPromptComponent;
class USplineComponent;
class UProceduralMeshComponent;
class UMaterialInterface;
class UNiagaraSystem;
class UNiagaraComponent;
class UMaterialInstanceDynamic;

/** Gathering-test water station. Existing maps/configurations do not enable its runtime. */
UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringPond : public AActor
{
    GENERATED_BODY()
public:
    ADWGatheringPond();
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostRegisterAllComponents() override;
#if WITH_EDITOR
    virtual void PostActorCreated() override;
    virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
    virtual void PostEditImport() override;
    virtual void PostEditMove(bool bFinished) override;
    virtual void PostEditUndo() override;
    bool CanRebuildBedInEditor(double CandidateBed,FString& Error) const;
    bool TrySetBedHeightInEditor(double CandidateBed,FString& Error);
#endif
    /** Explicitly opt in only for a new authored pond after identity/old-save review. */
    UPROPERTY(EditInstanceOnly,BlueprintReadOnly,Category="Gathering|Pond|Save") FName PersistentId;
    UPROPERTY(EditInstanceOnly,BlueprintReadOnly,Category="Gathering|Pond|Save") bool bAuthoredPersistence=false;
    bool CanPersistPondConfiguration() const;
    void AdoptPondPersistenceFacts(double SavedRemaining);
    void PublishPondPersistenceFacts();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="0")) double InitialReserve=300.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="0")) double RefillPerSecond=20.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="0",ClampMax="1")) double SlowFraction=.2;
    /** Legacy circle size; in spline mode used only by the explicit Reset Boundary To Circle button. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="1",Units="cm")) double RadiusCm=250.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape") bool bUseSplineBoundary=true;
    /** Edit this native closed spline in XY. Z is controlled by Water Height Cm. */
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Shape") TObjectPtr<USplineComponent> PondBoundary;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Shape") TObjectPtr<UProceduralMeshComponent> WaterMesh;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Shape") TObjectPtr<UProceduralMeshComponent> PondBank;
    /** Local cm; actor positive scale applies to generated geometry. Rebuild after runtime parameter edits. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="1",ClampMax="1000",Units="cm")) double WaterHeightCm=55.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="-10000",ClampMax="999",Units="cm")) double BedHeightCm=5.;
    /** Local Z at the outside edge. Fit this to the existing ground without moving the actor. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="-10000",ClampMax="999",Units="cm")) double OuterGroundHeightCm=0.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="1",ClampMax="10000",Units="cm")) double InnerSlopeWidthCm=70.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="1",ClampMax="1000",Units="cm")) double BankCrestAboveWaterCm=10.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="1",ClampMax="10000",Units="cm")) double BankCrestDistanceCm=20.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="2",ClampMax="10000",Units="cm")) double OuterBankWidthCm=120.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="5",ClampMax="1000",Units="cm")) double BankGridSpacingCm=15.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Shape",meta=(ClampMin="1",ClampMax="32")) int32 BoundarySamplesPerSegment=8;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Art",meta=(ClampMin="1",Units="cm")) double MaterialTilingCm=200.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Art") TObjectPtr<UMaterialInterface> WaterMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Art") TObjectPtr<UMaterialInterface> BankMaterial;
    /** Display-only continuous waterline. Invalid optional values disable ONLY this layer. */
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Edge Foam") TObjectPtr<UProceduralMeshComponent> EdgeFoamMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam") bool bEnableEdgeFoam=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam") TObjectPtr<UMaterialInterface> EdgeFoamMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="1",ClampMax="500",Units="cm")) double EdgeFoamWidthCm=25.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="0",ClampMax="1")) float EdgeFoamOpacity=.85f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="-4",ClampMax="4")) float EdgeFoamSpeed=-.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam") FLinearColor EdgeFoamColor=FLinearColor::White;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin=".01",ClampMax="100")) float EdgeFoamNoiseSize=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="1",Units="cm")) double EdgeFoamUVScaleCm=120.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="0",ClampMax="5",Units="cm")) double EdgeFoamLiftCm=.5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="5",Units="cm")) double EdgeFoamGridSpacingCm=15.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="8",ClampMax="192")) int32 EdgeFoamMaxGridCells=128;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="64",ClampMax="32768")) int32 EdgeFoamMaxTriangles=16384;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Edge Foam",meta=(ClampMin="0",Units="cm")) float EdgeFoamCullDistanceCm=6000.f;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> EdgeFoamMID;
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Gathering|Pond|Edge Foam") void RefreshEdgeFoam(){RebuildEdgeFoam();}
    /** Display-only. BankMaterial remains the persisted authored material and manual fallback. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Art") bool bMatchLandscapeBank=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Art") TObjectPtr<UMaterialInterface> MatchedBankMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX") bool bEnablePondVFX=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX") TObjectPtr<UNiagaraSystem> WaterRippleSystem;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX") TObjectPtr<UNiagaraSystem> EntrySplashSystem;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX") bool bMovementSplash=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX",meta=(ClampMin="0.3",ClampMax="10",Units="s")) float MovementSplashInterval=1.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX",meta=(ClampMin="0.01",ClampMax="10")) float EntrySplashScale=.08f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX",meta=(ClampMin="0.05",ClampMax="10")) float RippleScale=.3f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX",meta=(ClampMin="0.1",ClampMax="10",Units="s")) float MovementRippleInterval=.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX",meta=(ClampMin="0.1",ClampMax="30",Units="s")) float ShoreRippleInterval=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|VFX",meta=(ClampMin="0.1",ClampMax="5",Units="s")) float RippleLifetime=1.5f;
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Gathering|Pond|Art") void RefreshBankAppearance();
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering|Pond|Shape") bool bBoundaryValid=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering|Pond|Shape") FString BoundaryValidationMessage;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering|Pond|Shape") int32 BoundarySampleCount=0;
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Gathering|Pond|Shape") void RebuildPondGeometry();
    /** Destructive to this instance's control points only; never called by construction or BeginPlay. */
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Gathering|Pond|Shape") void ResetBoundaryToCircle();
    /** Compared against character feet, not its capsule center. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="0",Units="cm")) double SurfaceHeightToleranceCm=120.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="0",Units="cm")) double StationExtraRadiusCm=150.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Scale",meta=(ClampMin="0.01")) float RandomScaleMin=.7f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond|Scale",meta=(ClampMin="0.01")) float RandomScaleMax=1.f;
    /** Explicit editor action only; never rerandomize placed/saved ponds on BeginPlay. */
    UFUNCTION(CallInEditor,Category="Gathering|Pond|Scale") void RandomizeSceneScale();
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Gathering|Pond") double RemainingWater=300.;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Gathering|Pond") bool bIsDry=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Placeholder") TObjectPtr<USceneComponent> PondRoot;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Placeholder") TObjectPtr<UStaticMeshComponent> DryBed;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Placeholder") TObjectPtr<UStaticMeshComponent> WaterSurface;
    /** Legacy circle art retained for compatibility; hidden and non-colliding in spline mode. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Art") TSubclassOf<AActor> WaterArtClass;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Art") TObjectPtr<UChildActorComponent> WaterArt;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Range") TObjectPtr<UBoxComponent> OverlapRange;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Prompt") TObjectPtr<UDWInteractionPromptComponent> Prompt;
    UFUNCTION(BlueprintPure,Category="Gathering|Pond") bool ContainsActor(const AActor* Actor,double ExtraRadius=0.) const;
    UFUNCTION(BlueprintPure,Category="Gathering|Pond") bool ContainsWorldPoint(FVector WorldFeet,double ExtraRadius=0.) const;
    /** A bank/bed hit on this same actor must not open the water station. */
    UFUNCTION(BlueprintPure,Category="Gathering|Pond") bool IsWaterSurfaceHit(const FHitResult& Hit) const;
    UFUNCTION(BlueprintPure,Category="Gathering|Pond") bool IsUsableWaterStation(const AActor* User=nullptr) const;
    UFUNCTION(BlueprintPure,Category="Gathering|Pond") double GetRemainingWater() const {return RemainingWater;}
    /** Reject invalid save values unchanged. No Blueprint events, inventory or UI writes. */
    UFUNCTION(BlueprintCallable,Category="Gathering|Pond|Save") bool RestoreWater(double Value);
    bool IsGatheringProfileActive() const;
    bool IsWetAndOperational() const;
    static FName WaterStationId(){return FName(TEXT("GatherStation_WaterPond"));}
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
#if WITH_EDITOR
    bool CanPrepareNewEditorPondIdentity() const;
    void PrepareNewEditorPondIdentity(bool bInitializeNewAuthorship);
    FName PreparedNewEditorPondId=NAME_None;
#endif
    friend class ADWPlayerCharacter;
    /** No callback: player commits its prevalidated vitals candidate immediately after this debit. */
    bool DebitRefillWater(double Amount);
    bool bWaterRestored=false;
    bool bAppliedDryState=false;
    bool bAppliedSurfaceEnabled=false;
    TArray<FVector2D> BoundaryPolygon;
    FTransform LastRangeTransform=FTransform::Identity;
    TSet<TWeakObjectPtr<AActor>> SlowedActors;
    TArray<TPair<TWeakObjectPtr<UNiagaraComponent>,float>> PondPulses;
    TWeakObjectPtr<AActor> VisualOccupant;
    float MovementRippleClock=0.f,MovementSplashClock=0.f,ShoreRippleClock=0.f,VisualSampleClock=0.f;
    FVector LastWaterVFXPosition=FVector::ZeroVector;
    void RebuildEdgeFoam();
    void RefreshEdgeFoamVisibility();
    void TickPondVFX(float DeltaSeconds);
    void ClearPondVFX();
    void SpawnPondRipple(FVector WorldPosition,bool bSplash=false);
    bool IsConfigurationValid() const;
    void RefreshPlaceholder();
    void RefreshSplineState();
    void RefreshRangeBounds();
    void HideLegacyGeometry();
    bool AreShapeParametersValid() const;
    void RefreshPromptAvailability();
    void RefreshOccupants(double DeltaSeconds);
    void SetActorEnvironmentSlow(AActor* Actor);
    void RemoveActorEnvironmentSlow(AActor* Actor);
    void ReleaseAllEnvironmentSlow();
    UFUNCTION() void OnPondBeginOverlap(UPrimitiveComponent* Component,AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,int32 OtherBodyIndex,bool bFromSweep,const FHitResult& Sweep);
    UFUNCTION() void OnPondEndOverlap(UPrimitiveComponent* Component,AActor* OtherActor,
        UPrimitiveComponent* OtherComponent,int32 OtherBodyIndex);
};
