#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCombatBottle.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatBottleProjectile.generated.h"
class ADWPlayerCharacter;
class UStaticMeshComponent;
class USceneComponent;
class FDWCombatBottleTransaction;
class UDWCombatAuthoritySubsystem;

/** Formal bottle only. Authored bottle mesh is optional; all flight collision is a native swept sphere. */
UCLASS(NotBlueprintable,NotPlaceable)
class GDATTEST_API ADWCombatBottleProjectile : public AActor
{
    GENERATED_BODY()
public:
    ADWCombatBottleProjectile();
    virtual void Tick(float DeltaSeconds)override;
    virtual void EndPlay(EEndPlayReason::Type Reason)override;
    bool IsReleased()const{return bReleased;}
    bool IsSpent()const{return bSpent;}
    bool IsAreaOwned()const{return bAreaOwned;}
    double GetFlightAgeSeconds()const{return FlightAge;}
    DWCombatCommit::FEmitterId GetEmitter()const{return Emitter;}
    const FDWBottleSnapshot& GetShotSnapshot()const{return Shot;}
    FVector GetGroundTarget()const{return GroundTarget;}
    static bool HasReleasedBottle(const UWorld*);
private:
    friend class FDWCombatBottleTransaction;
    friend class UDWCombatAuthoritySubsystem;
    friend class FDWCombatEffectsPersistence;
    UPROPERTY() TObjectPtr<USceneComponent> FlightRoot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> BottleVisual;
    TWeakObjectPtr<ADWPlayerCharacter> Source;
    TWeakObjectPtr<AController> SourceController;
    DWCombatCommit::FEntityLife SourceLife;
    DWCombatCommit::FEmitterId Emitter;
    FDWBottleSnapshot Shot;
    FVector FlightStart=FVector::ZeroVector,GroundTarget=FVector::ZeroVector;
    double FlightAge=0.,ReleasedWorldSeconds=0.,LastWorldSeconds=0.,PendingSeconds=0.,WalkableFloorZ=1.;
    bool bReleased=false,bSpent=false,bAreaOwned=false,bAdvancing=false;
    void ActivateUnchecked(const FDWBottleSnapshot&,ADWPlayerCharacter&,DWCombatCommit::FEntityLife,DWCombatCommit::FEmitterId,FVector Start,FVector Ground,double At,double FloorZ);
    void CopyAuthoredVisual(const ADWPlayerCharacter&);
    static bool FindLegalGround(UWorld*,const AActor* Ignore,FVector Nearby,double SearchDepth,double Radius,double FloorZ,FVector& Out);
    bool FindFirstCollision(FVector Start,FVector End,FHitResult& Out)const;
    void Land(FVector Nearby,double At);
    void FinishFlight();
};
