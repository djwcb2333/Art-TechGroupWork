#pragma once
#include "CoreMinimal.h"

/** Native persistence/initialization input, never a consumable inventory stack. No starting grants. */
struct FDWCombatWeaponResourceSnapshot
{
    TSet<FName> OwnedPermanentWeaponIds;
    int32 LoadedShotgunShells=0;
    FName CurrentEquipmentId=NAME_None; // None is unarmed; permanent selections require ownership.
};

/** Sole loaded-shell fact. The animation component's AmmoInMagazine is a read-only mirror. */
struct FDWCombatWeaponResourceState
{
    FDWCombatWeaponResourceSnapshot Snapshot;
    int64 Revision=0;
    uint64 RestoreEpoch=0;
    int32 ShotgunCapacity=0;
    int32 ShotgunRoundsPerShot=0;
    double ReloadOneSeconds=0.;
    bool bRulesReady=false;
};
