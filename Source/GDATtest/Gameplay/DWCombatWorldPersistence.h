#pragma once
#include "CoreMinimal.h"
class ADWPlayerCharacter;
class UDWSaveGame;
class FDWCombatRules;
class UDWGameInstance;
class FDWOpeningDaggerProjectionProof;
struct FDWCombatWeaponResourceState;
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
private:
    friend class UDWGameInstance;
    static bool PrepareNewAdventureMarker(const FDWCombatRules&,UDWSaveGame&,FString& Error);
#if WITH_DEV_AUTOMATION_TESTS
    friend class FDWOpeningDaggerProjectionTest;
#endif
    static bool RestoreImpl(ADWPlayerCharacter&,const UDWSaveGame&,bool&,FString&,const FDWOpeningDaggerProjectionProof*);
    static bool PrepareOpeningDaggerProjection(ADWPlayerCharacter&,const FDWCombatWeaponResourceState&,UDWSaveGame&,FDWOpeningDaggerProjectionProof&,FString&);
    static bool RestoreOpeningFreshDestination(ADWPlayerCharacter&,const UDWSaveGame&,const FString&,UDWSaveGame&,bool&,FString&);
    static bool ValidateOpeningHandoff(const FDWCombatRules&,const UDWSaveGame&,FString&);
    static bool BuildFreshOpeningDestination(ADWPlayerCharacter&,const UDWSaveGame&,const FString& ExpectedDestination,UDWSaveGame& Out,FDWOpeningDaggerProjectionProof&,FString&);
};
