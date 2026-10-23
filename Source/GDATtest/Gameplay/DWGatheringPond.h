#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWGatheringPond.generated.h"

class ADWPlayerCharacter;
class UBoxComponent;
class USceneComponent;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UDWInteractionPromptComponent;

/** Gathering-test water station. Existing maps/configurations do not enable its runtime. */
UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringPond : public AActor
{
    GENERATED_BODY()
public:
    ADWGatheringPond();
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="0")) double InitialReserve=300.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="0")) double RefillPerSecond=20.;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="0",ClampMax="1")) double SlowFraction=.2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Pond",meta=(ClampMin="1",Units="cm")) double RadiusCm=250.;
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
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Range") TObjectPtr<UBoxComponent> OverlapRange;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering|Pond|Prompt") TObjectPtr<UDWInteractionPromptComponent> Prompt;
    UFUNCTION(BlueprintPure,Category="Gathering|Pond") bool ContainsActor(const AActor* Actor,double ExtraRadius=0.) const;
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
    friend class ADWPlayerCharacter;
    /** No callback: player commits its prevalidated vitals candidate immediately after this debit. */
    bool DebitRefillWater(double Amount);
    bool bWaterRestored=false;
    TSet<TWeakObjectPtr<AActor>> SlowedActors;
    bool IsConfigurationValid() const;
    void RefreshPlaceholder();
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
