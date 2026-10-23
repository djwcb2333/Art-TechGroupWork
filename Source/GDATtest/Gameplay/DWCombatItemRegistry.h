#pragma once
#include "CoreMinimal.h"
#include "DWWeaponAnimationSet.h"

class UDWGameplayConfig;

enum class EDWCombatItemRole : uint8 { Unrelated, PermanentWeapon, MoldCore, YeastCore, ReserveAmmo, ThrowableStock };

/** Technical identities only. No tuning, grants, inventory mutations or animation loading. */
struct GDATTEST_API FDWCombatWeaponBinding
{
    FName DesignId;
    FName ItemId;
    FName AmmoItemId;
    EDWAnimatedWeapon Presentation;
    FSoftObjectPath PresentationAsset;
    EDWCombatItemRole ItemRole;
};

class GDATTEST_API FDWCombatItemRegistry final
{
public:
    static const TArray<FDWCombatWeaponBinding>& GetWeaponBindings();
    static const FDWCombatWeaponBinding* FindWeapon(FName DesignId);
    static const FDWCombatWeaponBinding* FindPresentation(EDWAnimatedWeapon Weapon);
    static EDWCombatItemRole GetItemRole(FName ItemId);
    static FName MoldCoreId();
    static FName YeastCoreId();
    // Registration readiness is distinct from ownership and gameplay readiness.
    static bool ValidateItems(const UDWGameplayConfig* Config, FString& OutError);
    // Uses the actual assets supplied by the live character; never loads replacements.
    static bool ValidatePresentationBindings(const TArray<UDWWeaponAnimationSet*>& Sets, FString& OutError);
};
