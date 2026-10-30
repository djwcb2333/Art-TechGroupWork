#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWInventoryComponent.h"
#include "DWBuildingPlacementComponent.generated.h"

class ADWPlayerCharacter;
class ADWPlayerController;
class UDWBuildingDefinition;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDWGameplayConfig;

/** A transient preview is never a building, station, save object, or inventory transaction. */
UCLASS(ClassGroup=(DoughWorld),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWBuildingPlacementComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDWBuildingPlacementComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Placement") TSoftObjectPtr<UMaterialInterface> PreviewMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Placement",meta=(ClampMin="1",Units="cm")) float GridSize=20.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Placement",meta=(ClampMin="1",ClampMax="90",Units="Degrees")) float RotationStep=15.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Placement",meta=(ClampMin="0.05",Units="s")) float RotationFirstDelay=.35f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Building Placement",meta=(ClampMin="0.02",Units="s")) float RotationRepeatInterval=.10f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Building Placement") bool bPlacing=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Building Placement") bool bPlacementValid=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Building Placement") FTransform PreviewTransform=FTransform::Identity;
    UFUNCTION(BlueprintCallable,Category="Building Placement") bool BeginPlacement(const FDWInventorySlotSnapshot& Slot);
    UFUNCTION(BlueprintCallable,Category="Building Placement") void CancelPlacement();
    UFUNCTION(BlueprintCallable,Category="Building Placement") bool ConfirmPlacement();
    UFUNCTION(BlueprintPure,Category="Building Placement") bool IsAttackInputBlocked() const;
    /** Pure deterministic utilities, also used by automated boundary tests. */
    static FVector SnapXY(const FVector& Position,double Grid);
    static double StepYaw(double Yaw,double Step);
    static bool ValidateGroundTransform(UWorld* World,const ADWPlayerCharacter* Player,const UDWBuildingDefinition& Definition,
        const FTransform& Candidate,const AActor* IgnoreActor=nullptr);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FDWBuildingPlacementAutomationAccess;
#endif
    bool CommitPlacementTransaction();
    void UpdateRotationInput(bool CounterClockwiseHeld,bool CounterClockwisePressed,bool ClockwiseHeld,bool ClockwisePressed,double Now);
    bool UpdateConfirmationInput(bool MouseHeld,bool MousePressed);
    void ResetPlacementInput();
    void RestorePlacementInputFocus();
    ADWPlayerCharacter* Player() const;
    ADWPlayerController* Controller() const;
    bool IsSessionCurrent() const;
    bool HasGameplayFocus() const;
    bool CreatePreview();
    bool UpdatePreviewFromCursor();
    bool ResolveGround(const FVector& CursorHit,FTransform& Out) const;
    void SetPreviewVisible(bool Visible);
    void EndPlacement(bool SuppressInput);
    UPROPERTY(Transient) TObjectPtr<UDWBuildingDefinition> ActiveDefinition;
    UPROPERTY(Transient) TObjectPtr<AActor> PreviewActor;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PreviewMID;
    UPROPERTY(Transient) FDWInventorySlotSnapshot SourceSlot;
    TWeakObjectPtr<ADWPlayerCharacter> SessionPlayer;
    TWeakObjectPtr<ADWPlayerController> SessionController;
    TWeakObjectPtr<UDWInventoryComponent> SessionInventory;
    TWeakObjectPtr<UDWGameplayConfig> SessionConfig;
    uint64 SessionEpoch=0,SessionEntity=0,SessionLife=0;
    bool bMouseArmed=false,bRotateArmed=false,bCommitting=false;
    int32 LastRotationDirection=0;
    uint32 PlacementInputGeneration=0;
    bool bSuppressCombatUntilReleased=false;
    double SuppressCombatUntil=0.,NextRotation=0.,PlacementYaw=0.;
};
