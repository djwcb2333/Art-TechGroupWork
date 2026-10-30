#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWPickupPresentationComponent.generated.h"
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
/** Cosmetic feedback only. GatheringPickup owns the inventory transaction and quantity. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWPickupPresentationComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDWPickupPresentationComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Size") bool bNormalizeSize=true;
    /** Max world XY bounds dimension. Actor location, rotation and scale are unchanged. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Size",meta=(ClampMin="1",ClampMax="200",Units="cm")) float TargetWidthCm=30.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Size") TSoftObjectPtr<UStaticMesh> SugarChunkMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Outline") bool bShowOutline=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Outline") bool bOutlineOnlyInPickupRange=true;
    /** Opt in on newly placed pickup instances only; old instances keep original behavior. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Performance") bool bBudgetIdlePickupOutline=false;
    /** Seconds between hidden outline range checks; 0 retains per-frame checks. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Performance",meta=(ClampMin="0",ClampMax="0.5",Units="s",EditCondition="bBudgetIdlePickupOutline")) float PickupHiddenPollSeconds=.1f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Outline",meta=(ClampMin="0",Units="cm")) float OutlineWidth=1.25f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Outline") FLinearColor OutlineColor=FLinearColor::White;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Assets") TSoftObjectPtr<UMaterialInterface> OutlineMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Assets") TSoftObjectPtr<UMaterialInterface> FlashMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Collect") bool bEnableCollectionEffect=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Collect",meta=(ClampMin="0.05",Units="s")) float CollectionDuration=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Collect",meta=(ClampMin="1",ClampMax="3")) float StretchHeight=1.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Collect",meta=(ClampMin="0",ClampMax="0.45")) float SquashAmount=.16f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Collect",meta=(ClampMin="0",Units="cm")) float RiseDistance=35.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Collect",meta=(ClampMin="0")) float FlashStrength=3.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pickup Presentation|Collect") FLinearColor FlashColor=FLinearColor::White;
    UFUNCTION(BlueprintCallable,Category="Pickup Presentation") void NormalizePickupVisual();
    /** Called only after successful commit; never adds inventory or alters quantity itself. */
    bool PlayCommittedPickupEffect();
    /** Drop a stale outline after a committed catalog mesh change; never changes gameplay facts. */
    void OnCatalogVisualChanged();
    bool IsCollectionActive()const{return bCollectionActive;}
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Pickup Presentation|Debug") bool bCollectionActive=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Pickup Presentation|Debug") int32 CollectionPlayCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Pickup Presentation|Debug") float CollectionElapsed=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Pickup Presentation|Debug") float CurrentHighlight=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Pickup Presentation|Debug") float CurrentFlash=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Pickup Presentation|Debug") float MeasuredWidthCm=0.f;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)override;
    virtual void Deactivate()override;
protected:
    virtual void BeginPlay()override;
    virtual void EndPlay(const EEndPlayReason::Type Reason)override;
    virtual void OnUnregister()override;
private:
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> OutlineProxy;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CollectionProxy;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> FlashProxy;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> OutlineMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlashMID;
    UPROPERTY(Transient) TWeakObjectPtr<UStaticMeshComponent> SourceMesh;
    FTransform CollectionStart=FTransform::Identity;
    FVector AnchorLocal=FVector::ZeroVector;
    FName CollectionItemId=NAME_None;
    bool bSourceHiddenBorrowed=false,bOriginalSourceHidden=false;
    bool bEndingPlay=false;
    bool bBudgetOwnsTickInterval=false,bDiscardNextCollectionTick=false;
    float BudgetTickInterval=0.f;
    bool IsBudgetedOutlineIdle() const;
    void UpdatePickupBudget(bool bIdle);
    void WakePickupBudget();
    UStaticMeshComponent* MakeProxy(UStaticMeshComponent* Source,UMaterialInterface* Material,bool bCopySurface);
    void EnsureOutline();void UpdateOutline();void UpdateCollection();
    void FinishCollection(bool bNotifyOwner);void DestroyVisuals(bool bNotifyOwner);
};
