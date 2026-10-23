#pragma once
#include "CoreMinimal.h"
#include "DWCombatPlayerVitals.h"
#include "DWCombatWeaponResourceState.h"
#include "DWCombatPlayerSave.generated.h"

class FDWCombatRules;
class UDWSaveGame;

/** Only resource/form clock and existing owned/loaded facts. Remaining D22 groups are not implied. */
USTRUCT()
struct FDWCombatPlayerResourceSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 SubblockSchema=0;
    UPROPERTY(SaveGame) FDWCombatPlayerVitalsSnapshot Vitals;
    UPROPERTY(SaveGame) TArray<FName> OwnedPermanentWeaponIds;
    UPROPERTY(SaveGame) int32 LoadedShotgunShells=0;
    UPROPERTY(SaveGame) FName CurrentEquipmentId=NAME_None;
    /** Rejected before disk write: D22 has not yet serialized pending shield packets/counter clocks. */
    UPROPERTY(SaveGame) bool bRequiresShieldStateSchema=false;
    /** Temporary D22 guard: released world effects must never silently disappear from a file. */
    UPROPERTY(SaveGame) bool bRequiresReleasedEffectsSchema=false;
    UPROPERTY(SaveGame) bool bRequiresRecoveryStateSchema=false;
};

namespace DWCombatPlayerSave
{
    constexpr int32 SubblockSchema=2;
    bool MakeNew(const FDWCombatRules& Rules,UDWSaveGame& Save,FString& Error);
    bool Validate(const FDWCombatRules& Rules,const UDWSaveGame& Save,FString& Error);
    bool BuildWeaponCandidate(const FDWCombatRules& Rules,const FDWCombatPlayerResourceSave& Saved,
        FDWCombatWeaponResourceState& Out,FString& Error);
    float HealthFloor(const FDWCombatPlayerVitalsConfig& Config);
    float HealthMaximum(const FDWCombatPlayerVitalsConfig& Config);
    float NativeHealth(double Value,const FDWCombatPlayerVitalsConfig& Config);
}
