#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "DWGatheringPresentationComponent.generated.h"
class UStaticMesh; class UStaticMeshComponent; class UPrimitiveComponent; class UMaterialInterface; class UMaterialInstanceDynamic; class UPhysicalMaterial;
UENUM(BlueprintType)
enum class EDWGatheringDestructionStyle : uint8 { None, TreeFall, RockFracture };
USTRUCT(BlueprintType)
struct GDATTEST_API FDWGatheringFragment
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fragment") TSoftObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Fragment") FTransform LocalTransform=FTransform::Identity;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWGatheringDissolveSurface
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Surface") TSoftObjectPtr<UMaterialInterface> OriginalMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Surface") TSoftObjectPtr<UMaterialInterface> DissolveMaterial;
};
USTRUCT(BlueprintType)
struct FDWGatheringPieceRuntime
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Physics") TObjectPtr<UStaticMeshComponent> Component;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DissolveMIDs;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Physics") FTransform Rest=FTransform::Identity;
    FVector LocalCenter=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Physics") float ContactElapsed=-1.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Physics") double ContactWorldSeconds=-1.;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Physics") double PhysicsStartWorldSeconds=-1.;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Physics") float DissolveAmount=0.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Physics") bool bFinished=false;
    float StableElapsed=0.f;
    float DissolveStartActiveSeconds=-1.f;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWGatheringPhysicalGroundContact);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWGatheringSettlementReady);
/** Physics presentation never credits inventory. Resource owns the exact-once reward transaction. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWGatheringPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDWGatheringPresentationComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation") EDWGatheringDestructionStyle Style=EDWGatheringDestructionStyle::None;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Tree") TSoftObjectPtr<UStaticMesh> StumpMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Tree") TSoftObjectPtr<UStaticMesh> UpperTreeMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Tree") FTransform StumpLocalTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Tree") FTransform UpperTreeLocalTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Tree",meta=(ClampMin="0.1",Units="kg")) float UpperTreeMassKg=28.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Tree",meta=(ClampMin="0",Units="cm/s")) float TreePushSpeed=65.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Tree",meta=(ClampMin="0",Units="deg/s")) float TreeAngularSpeed=60.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Fracture") TArray<FDWGatheringFragment> Fragments;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Fracture",meta=(ClampMin="0",Units="cm")) float CrackSeparation=6.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Fracture",meta=(ClampMin="0.1",Units="kg")) float FragmentMassKg=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Fracture",meta=(ClampMin="0",Units="cm/s")) float FragmentOutwardSpeed=130.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Fracture",meta=(ClampMin="0",Units="cm/s")) float FragmentUpwardSpeed=95.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Physics",meta=(ClampMin="0")) float LinearDamping=.18f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Physics",meta=(ClampMin="0")) float AngularDamping=.25f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Physics",meta=(ClampMin="0",ClampMax="2")) float SurfaceFriction=.65f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Physics",meta=(ClampMin="0",ClampMax="1")) float Restitution=.08f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Physics",meta=(ClampMin="0.1",ClampMax="1")) float GroundNormalMinZ=.45f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Physics",meta=(ClampMin="0.05",Units="s")) float SettledContactSeconds=.25f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dissolve") TArray<FDWGatheringDissolveSurface> DissolveSurfaces;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dissolve") TArray<TSoftObjectPtr<UMaterialInterface>> SourceDissolveMaterials;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dissolve",meta=(ClampMin="0",Units="s")) float RestBeforeDissolve=2.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dissolve",meta=(ClampMin="0.05",Units="s")) float DissolveDuration=1.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dissolve") FLinearColor DissolveEdgeColor=FLinearColor(1.f,.82f,.08f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dissolve") float DissolveEdgeWidth=.045f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dissolve") float DissolveEdgeStrength=5.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dust") TSoftObjectPtr<UStaticMesh> DustMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dust") TSoftObjectPtr<UMaterialInterface> DustMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dust") int32 DustCount=9;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dust") float DustRadius=28.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dust") float DustDuration=.85f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Dust") FLinearColor DustColor=FLinearColor(.94f,.87f,.66f,1.f);
    /** Legacy serialized values retained only for compatibility. They no longer drive movement/contact. */
    UPROPERTY() float FallDuration=1.25f;
    UPROPERTY() float CrownStartHeightRatio=.45f;
    UPROPERTY() float FractureDuration=.7f;
    UPROPERTY() float FragmentScatterRadius=105.f;
    /** Reward readiness: real contact or bounded physical fallback, once per exhaustion. */
    UPROPERTY(BlueprintAssignable,Category="Gathering Presentation|Physics") FDWGatheringSettlementReady OnExhaustionSettlementReady;
    /** Active simulation seconds before stuck pieces may settle. Pauses do not advance this clock. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering Presentation|Physics",meta=(ClampMin="0.25",ClampMax="60",Units="s")) float MaxPhysicalWaitSeconds=6.f;
    /** True only if a physical timeout was used; never substitutes for bContactedGround. */
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") bool bUsedSettlementFallback=false;
    UPROPERTY(BlueprintAssignable,Category="Gathering Presentation|Physics") FDWGatheringPhysicalGroundContact OnPhysicalGroundContact;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") bool bExhaustionStarted=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") bool bContactedGround=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") bool bPresentationFinished=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") bool bPhysicsSetupFailed=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") float CurrentDissolve=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") int32 AcceptedHitCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") int32 GroundContactCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") int32 BodyGroundContactCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") int32 PhysicsHitEventCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") int32 SettledGroundContactCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") int32 MissingDissolveSlots=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gathering Presentation|Debug") float PhysicsElapsed=0.f;
    UFUNCTION(BlueprintPure,Category="Gathering Presentation") bool IsExhaustionVisible() const {return bExhaustionStarted&&!bPresentationFinished;}
    UFUNCTION(BlueprintPure,Category="Gathering Presentation") bool IsAwaitingPhysicalContact() const {return bExhaustionStarted&&!bContactedGround&&!bPhysicsSetupFailed;}
    UFUNCTION(BlueprintPure,Category="Gathering Presentation") bool HasExhaustionSettlementReady() const {return bContactedGround||bUsedSettlementFallback;}
    UFUNCTION(BlueprintPure,Category="Gathering Presentation") bool IsAwaitingExhaustionSettlement() const {return bExhaustionStarted&&!HasExhaustionSettlementReady()&&!bPhysicsSetupFailed;}
    UFUNCTION(BlueprintPure,Category="Gathering Presentation") bool HasPersistentStump() const;
    UFUNCTION(BlueprintPure,Category="Gathering Presentation") int32 GetActivePhysicsBodyCount() const;
    UFUNCTION(BlueprintPure,Category="Gathering Presentation|Physics") TArray<FDWGatheringPieceRuntime> GetPhysicsPieces() const {return Pieces;}
    UFUNCTION(BlueprintPure,Category="Gathering Presentation|Physics") UStaticMeshComponent* GetStumpComponent() const {return Stump;}
    UFUNCTION(BlueprintPure,Category="Gathering Presentation|Physics") UStaticMeshComponent* GetUpperTreeComponent() const {return Style==EDWGatheringDestructionStyle::TreeFall&&Pieces.Num()>0?Pieces[0].Component.Get():nullptr;}
    /** Compatibility query: physics has no predetermined contact delay. */
    UFUNCTION(BlueprintPure,Category="Gathering Presentation",meta=(DeprecatedFunction,DeprecationMessage="Wait for OnPhysicalGroundContact instead.")) float GetExhaustionContactDelay() const {return 0.f;}
    void PlayAcceptedHit(const FVector& InstigatorLocation,float GatheredFraction,bool Exhausted);
    void ResetPresentation();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TWeakObjectPtr<UStaticMeshComponent> Source;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Stump;
    /** Stationary stump shares the trunk's first real-ground-contact dissolve clock. */
    UPROPERTY(Transient) FDWGatheringPieceRuntime StumpDissolve;
    UPROPERTY(Transient) TArray<FDWGatheringPieceRuntime> Pieces;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Dust;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DustMID;
    UPROPERTY(Transient) TObjectPtr<UPhysicalMaterial> PhysicsMaterial;
    FTransform SourceRest=FTransform::Identity;
    FVector Pivot=FVector::ZeroVector,FallDirection=FVector::ForwardVector,ContactPoint=FVector::ZeroVector;
    float HitElapsed=1.f,Progress=0.f,DustDiameter=100.f,DustElapsed=0.f;
    double StumpDissolveStartActiveSeconds=-1.;
    ECollisionEnabled::Type SourceCollision=ECollisionEnabled::QueryAndPhysics;
    bool bCaptured=false,bOriginalVisible=true,bFragmentsReady=false;
    uint64 PhysicsGeneration=0;
    bool bSettlementEventPublished=false;
    void PublishSettlementReady();
    void BeginBoundedSettlement();
    bool CaptureSource();
    void ApplyDissolveMaterials(FDWGatheringPieceRuntime& Piece);
    bool BuildFragments();
    bool BuildBrokenTree();
    void UpdateCracks();
    bool BeginExhaustion();
    bool EnablePhysics(FDWGatheringPieceRuntime& Piece,float MassKg,const FVector& Velocity,const FVector& AngularDegrees);
    void MarkGroundContact(FDWGatheringPieceRuntime& Piece,const FVector& Point,bool FromHit);
    bool IsGroundSurface(AActor* OtherActor,UPrimitiveComponent* OtherComponent,const FVector& Normal) const;
    bool HasSupportingGround(UStaticMeshComponent* Body,FVector& Point) const;
    UFUNCTION() void HandlePhysicsHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit);
    void SpawnDust();
    void UpdateDust(float AfterContact);
    void ClearTransientVisuals(bool KeepStump=false);
};
