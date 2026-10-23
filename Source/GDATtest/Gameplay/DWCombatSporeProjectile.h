#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatSporeGun.h"
#include "DWCombatSporeProjectile.generated.h"
class ADWPlayerCharacter;
class ADWEnemyCharacter;
class USceneComponent;
class UStaticMeshComponent;
class UDWCombatAuthoritySubsystem;
class FDWCombatSporeShotTransaction;
/** Native visible bullet; separate swept collision, no blueprint overlap damage or free respawn. */
UCLASS(NotBlueprintable,NotPlaceable)
class GDATTEST_API ADWCombatSporeProjectile : public AActor
{
    GENERATED_BODY()
public:
    ADWCombatSporeProjectile();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    bool IsReleased()const{return bReleased;}
    bool IsSpent()const{return bSpent;}
    double GetTravelledCentimeters()const{return TravelledCm;}
    FVector GetFlightDirection()const{return Direction;}
    DWCombatCommit::FEmitterId GetEmitter()const{return Emitter;}
    ADWEnemyCharacter* GetLockedTarget()const;
    const FDWSporeShotSnapshot& GetShotSnapshot()const{return Shot;}
private:
    friend class UDWCombatAuthoritySubsystem;
    friend class FDWCombatEffectsPersistence;
    friend class FDWCombatSporeShotTransaction;
    UPROPERTY() TObjectPtr<USceneComponent> FlightRoot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Placeholder;
    TWeakObjectPtr<ADWPlayerCharacter> Source;
    TWeakObjectPtr<AController> SourceController;
    TWeakObjectPtr<ADWEnemyCharacter> LockedTarget;
    DWCombatCommit::FEntityLife SourceLife,LockedTargetLife;
    DWCombatCommit::FEmitterId Emitter;
    FDWSporeShotSnapshot Shot;
    FVector Direction=FVector::ForwardVector;
    double TravelledCm=0.,LastWorldSeconds=0.,PendingSeconds=0.;
    bool bReleased=false,bSpent=false,bLostLock=false,bAdvancing=false;
    void ActivateUnchecked(const FDWSporeShotSnapshot&,ADWPlayerCharacter&,ADWEnemyCharacter*,
        DWCombatCommit::FEntityLife,DWCombatCommit::FEntityLife,DWCombatCommit::FEmitterId,FVector,double);
    void FinishFlight();
    static ADWEnemyCharacter* FindLock(ADWPlayerCharacter&,FVector Origin,FVector Facing,const FDWSporeGunConfig&,const FDWSporeShotSnapshot&);
    bool FindFirstCollision(FVector Start,FVector End,FHitResult& Out)const;
};
