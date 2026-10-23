#pragma once
#include "CoreMinimal.h"
class ADWPlayerCharacter;
class UDWSaveGame;
class FDWCombatRules;
struct FDWShieldPacketSave;
struct FDWCombatEffectsSave;

/** Single complete world snapshot entry. No component may independently apply a subset. */
class GDATTEST_API FDWCombatWorldPersistence
{
public:
    static bool Validate(const FDWCombatRules&,const UDWSaveGame&,FString& Error);
    static bool MarkNewAdventure(const FDWCombatRules&,UDWSaveGame&,FString& Error);
    static bool ValidateShieldPackets(const TArray<FDWShieldPacketSave>&,const FDWCombatEffectsSave&,FString& Error);
    static bool Capture(ADWPlayerCharacter&,UDWSaveGame&,FString& Error);
    // OutCommitted distinguishes rejection from publication interrupted after all native facts were adopted.
    static bool Restore(ADWPlayerCharacter&,const UDWSaveGame&,bool& OutCommitted,FString& Error);
};
