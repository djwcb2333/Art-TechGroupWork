#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"

/** Prepared without external calls, committed alongside positive health damage. */
struct FDWPlayerCombatKnockback
{
    FVector Direction=FVector::ZeroVector;
    DWCombatCommit::FEntityLife Life;
    double DistanceCm=0.,DurationSeconds=0.;
    bool bValid=false;
};
struct FDWPlayerCombatKnockbackState
{
    FVector Direction=FVector::ZeroVector;
    DWCombatCommit::FEntityLife Life;
    double RemainingCm=0.,RemainingSeconds=0.,SelectedDistanceCm=0.,CommittedTravelThisFrame=0.;
    uint64 Frame=MAX_uint64;
    bool bActive=false;
};
