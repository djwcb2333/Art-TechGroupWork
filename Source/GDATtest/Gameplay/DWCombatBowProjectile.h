#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatBow.h"
#include "DWWeaponAnimationSet.h"
#include "DWCombatBowProjectile.generated.h"
class ADWPlayerCharacter;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class USceneComponent;
class UDWWeaponAnimationComponent;
UCLASS(NotBlueprintable,NotPlaceable)
class GDATTEST_API ADWCombatBowProjectile:public AActor
{
    GENERATED_BODY()
public:
    ADWCombatBowProjectile();
    virtual void Tick(float DeltaSeconds)override;
    virtual void EndPlay(EEndPlayReason::Type Reason)override;
    bool IsReleased()const{return bReleased;}
    bool IsSpent()const{return bSpent;}
    DWCombatCommit::FEmitterId GetEmitter()const{return Emitter;}
    const FDWBowShotSnapshot& GetShotSnapshot()const{return Shot;}
    static bool HasUnserializedReleasedArrows(UWorld*);
private:
    friend class FDWCombatBowShotTransaction;
    friend class UDWCombatAuthoritySubsystem;
    friend class FDWCombatEffectsPersistence;
    UPROPERTY() TObjectPtr<USceneComponent> FlightRoot;
    UPROPERTY() TObjectPtr<USceneComponent> VisualRoot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Placeholder;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ArrowTip;
    UPROPERTY() TObjectPtr<UProceduralMeshComponent> AuthoredArrow;
    UPROPERTY() FDWWeaponGeometry CapturedGeometry;
    UPROPERTY() TObjectPtr<UMaterialInterface> CapturedMaterial;
    UPROPERTY() TObjectPtr<UStaticMesh> CapturedCustomMesh;
    FTransform CapturedVisualTransform=FTransform::Identity;
    bool bFlightPresentationCaptured=false,bHasAuthoredArrow=false;
    void CaptureFlightPresentation(const UDWWeaponAnimationComponent&,bool bMatchCurrentWorldPose);
    bool bFlightPresentationReady=false;
    bool PublishFlightPresentation();
    TWeakObjectPtr<ADWPlayerCharacter> Source;
    TWeakObjectPtr<AController> SourceController;
    DWCombatCommit::FEntityLife SourceLife;
    DWCombatCommit::FEmitterId Emitter;
    FDWBowShotSnapshot Shot;
    FVector Direction=FVector::ForwardVector;
    double ReleasedAt=0.,LastWorldSeconds=0.,TravelledCm=0.;
    bool bReleased=false,bSpent=false,bAreaOwned=false,bAdvancing=false;
    void ActivateUnchecked(const FDWBowShotSnapshot&,ADWPlayerCharacter&,DWCombatCommit::FEntityLife,DWCombatCommit::FEmitterId,FVector,double);
    bool FindFirstCollision(FVector Start,FVector End,FHitResult&)const;
    void FinishFlight();
};
