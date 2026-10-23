#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedgerSnapshot.h"
#include "DWCombatSporeGun.h"
#include "DWCombatBow.h"
#include "DWCombatBottle.h"
class AActor;

// Root owns exact persistent IDs and resolves them after its scene object table exists.
struct FDWCombatObjectBinding { FString StableId; TWeakObjectPtr<AActor> Actor; };
enum class EDWCombatSavedActorKind:uint8 { Other,Player,Enemy,Nest,Controller };
struct FDWCombatActorIdentitySave
{
    FString StableId;
    DWCombatCommit::FEntityLife Life;
    uint64 RestoreRevision=0;
    EDWCombatSavedActorKind Kind=EDWCombatSavedActorKind::Other;
    bool bDead=false,bEnded=false;
};
struct FDWCombatAuthoritySave
{
    DWCombatCommit::FLedgerSnapshot Ledger;
    uint64 LastEntity=0;
    TArray<FDWCombatActorIdentitySave> Actors;
};
struct FDWReleasedEffectOwnerSave
{
    DWCombatCommit::FEntityLife SourceLife;
    FString InstigatorId;
    uint64 Emitter=0;
};
struct FDWSporeFlightSave
{
    FDWReleasedEffectOwnerSave Owner;
    FDWSporeShotSnapshot Shot;
    FVector Position=FVector::ZeroVector,Direction=FVector::ForwardVector;
    DWCombatCommit::FEntityLife LockedTargetLife;
    double TravelledCm=0.,LastWorldOffset=0.,PendingSeconds=0.;
    bool bLostLock=true;
};
struct FDWBowFlightSave
{
    FDWReleasedEffectOwnerSave Owner;FDWBowShotSnapshot Shot;
    FVector Position=FVector::ZeroVector,Direction=FVector::ForwardVector;
    double TravelledCm=0.,LastWorldOffset=0.,ReleasedOffset=0.;
    bool bHasVisualTransform=false;FTransform VisualTransform=FTransform::Identity;
};
struct FDWBottleFlightSave
{
    FDWReleasedEffectOwnerSave Owner;FDWBottleSnapshot Shot;
    FVector Position=FVector::ZeroVector,FlightStart=FVector::ZeroVector,GroundTarget=FVector::ZeroVector;
    double FlightAge=0.,LastWorldOffset=0.,ReleasedOffset=0.,PendingSeconds=0.,WalkableFloorZ=0.;
};
struct FDWGroundAreaSave
{
    FDWReleasedEffectOwnerSave Owner;FVector Ground=FVector::ZeroVector;
    double StartedOffset=0.,EndsOffset=0.,RadiusCm=0.,SlowFraction=0.,Damage=0.,Period=0.;
    bool bActive=false;
};
struct FDWAreaTargetPhaseSave
{
    DWCombatCommit::FEntityLife TargetLife;
    double AnchorOffset=0.,PeriodSeconds=0.;uint64 NextOrdinal=0;
};
struct FDWCombatEffectsSave
{
    uint32 Schema=1;
    FDWCombatAuthoritySave Authority;
    TArray<FDWSporeFlightSave> Spores;
    TArray<FDWBowFlightSave> Arrows;
    TArray<FDWBottleFlightSave> Bottles;
    TArray<FDWGroundAreaSave> Areas;
    TArray<FDWAreaTargetPhaseSave> TargetPhases;
};
namespace DWCombatEffectsSave
{
    // No UObject loading or mutation. Reject malformed references, non-finite values, and trailing bytes.
    bool ValidateAuthority(const FDWCombatAuthoritySave&,FString& Error);
    bool Validate(const FDWCombatEffectsSave&,FString& Error);
    bool Encode(const FDWCombatEffectsSave&,TArray<uint8>& Out,FString& Error);
    bool Decode(const TArray<uint8>&,FDWCombatEffectsSave& Out,FString& Error);
}
