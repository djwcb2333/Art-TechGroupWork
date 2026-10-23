#include "DWCombatItemRegistry.h"
#include "DWGameplayConfig.h"

namespace
{
bool Fail(FString& Error, const FString& Message) { Error=Message; return false; }
}

const TArray<FDWCombatWeaponBinding>& FDWCombatItemRegistry::GetWeaponBindings()
{
    // Preserve existing enum order independently from the design table order.
    static const TArray<FDWCombatWeaponBinding> Bindings = {
        {TEXT("spear"),TEXT("DWWeapon_Spear"),NAME_None,EDWAnimatedWeapon::Spear,FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Spear.DA_DWHeroWeapon_Spear")),EDWCombatItemRole::PermanentWeapon},
        {TEXT("axe"),TEXT("DWWeapon_ShieldAxe"),NAME_None,EDWAnimatedWeapon::ShieldAxe,FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_ShieldAxe.DA_DWHeroWeapon_ShieldAxe")),EDWCombatItemRole::PermanentWeapon},
        {TEXT("sword"),TEXT("DWWeapon_Greatsword"),NAME_None,EDWAnimatedWeapon::Greatsword,FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Greatsword.DA_DWHeroWeapon_Greatsword")),EDWCombatItemRole::PermanentWeapon},
        {TEXT("dagger"),TEXT("DWWeapon_DualDaggers"),NAME_None,EDWAnimatedWeapon::DualDaggers,FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_DualDaggers.DA_DWHeroWeapon_DualDaggers")),EDWCombatItemRole::PermanentWeapon},
        {TEXT("gun"),TEXT("DWWeapon_SporeGun"),TEXT("DWAmmo_Spore"),EDWAnimatedWeapon::SporeGun,FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_SporeGun.DA_DWHeroWeapon_SporeGun")),EDWCombatItemRole::PermanentWeapon},
        {TEXT("bow"),TEXT("DWWeapon_MoldBow"),TEXT("DWAmmo_MoldArrow"),EDWAnimatedWeapon::MoldBow,FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_MoldBow.DA_DWHeroWeapon_MoldBow")),EDWCombatItemRole::PermanentWeapon},
        {TEXT("shotgun"),TEXT("DWWeapon_Shotgun"),TEXT("DWAmmo_BreadShell"),EDWAnimatedWeapon::Shotgun,FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Shotgun.DA_DWHeroWeapon_Shotgun")),EDWCombatItemRole::PermanentWeapon},
        {TEXT("alcohol"),TEXT("Alcohol"),TEXT("Alcohol"),EDWAnimatedWeapon::Alcohol,FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Alcohol.DA_DWHeroWeapon_Alcohol")),EDWCombatItemRole::ThrowableStock}
    };
    return Bindings;
}
FName FDWCombatItemRegistry::MoldCoreId() { return TEXT("DWCore_Mold"); }
FName FDWCombatItemRegistry::YeastCoreId() { return TEXT("DWCore_Yeast"); }
const FDWCombatWeaponBinding* FDWCombatItemRegistry::FindWeapon(FName Id)
{ return GetWeaponBindings().FindByPredicate([Id](const auto& Binding){return Binding.DesignId==Id;}); }
const FDWCombatWeaponBinding* FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon Weapon)
{ return GetWeaponBindings().FindByPredicate([Weapon](const auto& Binding){return Binding.Presentation==Weapon;}); }
EDWCombatItemRole FDWCombatItemRegistry::GetItemRole(FName Id)
{
    if(Id.IsNone())return EDWCombatItemRole::Unrelated;
    if(Id==MoldCoreId())return EDWCombatItemRole::MoldCore;
    if(Id==YeastCoreId())return EDWCombatItemRole::YeastCore;
    for(const auto& Binding:GetWeaponBindings())if(Binding.ItemId==Id)return Binding.ItemRole;
    for(const auto& Binding:GetWeaponBindings())if(Binding.AmmoItemId==Id)return EDWCombatItemRole::ReserveAmmo;
    return EDWCombatItemRole::Unrelated;
}
bool FDWCombatItemRegistry::ValidateItems(const UDWGameplayConfig* Config,FString& OutError)
{
    OutError.Reset();
    if(!IsValid(Config))return Fail(OutError,TEXT("Missing effective gameplay item configuration."));
    TSet<FName> Registered;
    for(const auto& Item:Config->Items)
    {
        if(Item.ItemId.IsNone()||Registered.Contains(Item.ItemId))return Fail(OutError,TEXT("Empty or duplicate registered item identity."));
        Registered.Add(Item.ItemId);
        if(GetItemRole(Item.ItemId)!=EDWCombatItemRole::Unrelated &&
            (!FMath::IsFinite(Item.HealAmount)||Item.HealAmount!=0.f||!FMath::IsFinite(Item.TransformationGainPercent)||Item.TransformationGainPercent!=0.f))
            return Fail(OutError,FString::Printf(TEXT("Combat item %s must not also apply legacy healing/transformation."),*Item.ItemId.ToString()));
    }
    TSet<FName> Required{MoldCoreId(),YeastCoreId()};
    for(const auto& Binding:GetWeaponBindings())
    {
        Required.Add(Binding.ItemId);
        if(!Binding.AmmoItemId.IsNone())Required.Add(Binding.AmmoItemId);
    }
    for(FName Id:Required)if(!Registered.Contains(Id))return Fail(OutError,FString::Printf(TEXT("Combat item is not registered: %s"),*Id.ToString()));
    return true;
}
bool FDWCombatItemRegistry::ValidatePresentationBindings(const TArray<UDWWeaponAnimationSet*>& Sets,FString& OutError)
{
    OutError.Reset();
    if(Sets.Num()!=GetWeaponBindings().Num())return Fail(OutError,TEXT("Expected exactly eight live presentation bindings."));
    TSet<EDWAnimatedWeapon> Seen;
    for(const auto* Set:Sets)
    {
        if(!IsValid(Set))return Fail(OutError,TEXT("Missing live weapon presentation."));
        const auto* Binding=FindPresentation(Set->Weapon);
        if(!Binding||Seen.Contains(Set->Weapon))return Fail(OutError,TEXT("Unknown or duplicate weapon presentation identity."));
        // The original asset remains valid for preserved sources. Only the exact
        // user-approved R1 spear presentation is an additional accepted identity.
        const FSoftObjectPath Actual(Set);
        const bool bApprovedR1=Set->Weapon==EDWAnimatedWeapon::Spear&&Actual==FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Spear_HC04_CandidateA_R1.DA_DWHeroWeapon_Spear_HC04_CandidateA_R1"));
        if(Actual!=Binding->PresentationAsset&&!bApprovedR1)return Fail(OutError,FString::Printf(TEXT("Unexpected presentation asset for %s: %s"),*Binding->DesignId.ToString(),*Set->GetPathName()));
        Seen.Add(Set->Weapon);
    }
    return true;
}
