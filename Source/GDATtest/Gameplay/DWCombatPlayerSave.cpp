#include "DWCombatPlayerSave.h"
#include "DWCombatEquipment.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWSaveGame.h"
#include <cmath>
#include <limits>

float DWCombatPlayerSave::HealthFloor(const FDWCombatPlayerVitalsConfig& C)
{
    float V=static_cast<float>(C.Vitals.HealthFloor);
    if(static_cast<double>(V)<C.Vitals.HealthFloor)V=std::nextafter(V,std::numeric_limits<float>::infinity());
    return V;
}
float DWCombatPlayerSave::HealthMaximum(const FDWCombatPlayerVitalsConfig& C)
{
    float V=static_cast<float>(C.Vitals.HealthMax);
    if(static_cast<double>(V)>C.Vitals.HealthMax)V=std::nextafter(V,-std::numeric_limits<float>::infinity());
    return V;
}
float DWCombatPlayerSave::NativeHealth(double Value,const FDWCombatPlayerVitalsConfig& C)
{return FMath::Clamp(static_cast<float>(Value),HealthFloor(C),HealthMaximum(C));}

bool DWCombatPlayerSave::BuildWeaponCandidate(const FDWCombatRules& Rules,const FDWCombatPlayerResourceSave& Saved,
    FDWCombatWeaponResourceState& Out,FString& Error)
{
    if(Saved.bRequiresRecoveryStateSchema){Error=TEXT("Recovery facts require the complete D22 world snapshot; original file preserved.");return false;}
    if(Saved.bRequiresShieldStateSchema){Error=TEXT("Shield pending damage, counter charges or reopen clocks require the unfinished D22 shield schema; original save preserved.");return false;}
    if(Saved.bRequiresReleasedEffectsSchema){Error=TEXT("Released projectiles/areas require the unfinished D22 world-effect schema; original save preserved.");return false;}
    if(Saved.SubblockSchema!=SubblockSchema){Error=TEXT("Missing or incompatible player resource save subblock; no legacy conversion is authorized.");return false;}
    FDWCombatWeaponResourceState Candidate;
    if(!Rules.TryGetCount(TEXT("weapon.shotgun.magazine"),Candidate.ShotgunCapacity,Error)
        ||!Rules.TryGetCount(TEXT("weapon.shotgun.ammo_per_shot"),Candidate.ShotgunRoundsPerShot,Error)
        ||!Rules.TryGetNumber(TEXT("weapon.shotgun.reload_one_s"),EDWCombatRuleUnit::Seconds,Candidate.ReloadOneSeconds,Error))return false;
    if(Candidate.ShotgunCapacity<1||Candidate.ShotgunRoundsPerShot<1||Candidate.ShotgunRoundsPerShot>Candidate.ShotgunCapacity
        ||!FMath::IsFinite(Candidate.ReloadOneSeconds)||Candidate.ReloadOneSeconds<=0.||Candidate.ReloadOneSeconds>MAX_flt
        ||Saved.LoadedShotgunShells<0||Saved.LoadedShotgunShells>Candidate.ShotgunCapacity)
    {Error=TEXT("Invalid loaded shotgun state or rule units.");return false;}
    for(FName Id:Saved.OwnedPermanentWeaponIds)
    {
        if(FDWCombatItemRegistry::GetItemRole(Id)!=EDWCombatItemRole::PermanentWeapon||Candidate.Snapshot.OwnedPermanentWeaponIds.Contains(Id))
        {Error=TEXT("Invalid or duplicate permanent ownership id.");return false;}
        Candidate.Snapshot.OwnedPermanentWeaponIds.Add(Id);
    }
    const auto* Shotgun=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);
    if(!Shotgun||(Saved.LoadedShotgunShells>0&&!Candidate.Snapshot.OwnedPermanentWeaponIds.Contains(Shotgun->ItemId)))
    {Error=TEXT("Loaded shells require saved shotgun ownership.");return false;}
    Candidate.Snapshot.LoadedShotgunShells=Saved.LoadedShotgunShells;
    Candidate.Snapshot.CurrentEquipmentId=Saved.CurrentEquipmentId;
    if(!DWCombatEquipment::ValidateIdentity(Candidate.Snapshot,Error))return false;
    Candidate.bRulesReady=true;
    Out=MoveTemp(Candidate);Error.Reset();return true;
}
bool DWCombatPlayerSave::MakeNew(const FDWCombatRules& Rules,UDWSaveGame& Save,FString& Error)
{
    FDWCombatPlayerVitalsConfig C;FDWCombatPlayerVitalsRuntime Runtime;
    if(!FDWCombatPlayerVitalsConfig::Build(Rules,C,Error)||!Runtime.Initialize(C,EDWCombatVitalsReset::NewCharacter))return false;
    if(HealthFloor(C)>=HealthMaximum(C)){Error=TEXT("Health interval cannot be represented by the native Health field.");return false;}
    FDWCombatPlayerResourceSave Candidate;Candidate.SubblockSchema=SubblockSchema;
    const float Health=NativeHealth(C.Vitals.HealthMax*C.Vitals.InitialHealthFraction,C);
    Candidate.Vitals=Runtime.Capture(Health);
    Save.CombatPlayerResources=MoveTemp(Candidate);
    Save.Health=Health;Save.Transformation=static_cast<float>(C.Vitals.NormalSpecialValue);Save.bYeastForm=false;
    Save.SprintAlcoholElapsed=Save.TransformationDecayElapsed=0.f;
    return Validate(Rules,Save,Error);
}
bool DWCombatPlayerSave::Validate(const FDWCombatRules& Rules,const UDWSaveGame& Save,FString& Error)
{
    if(Save.Version<1||Save.Version>UDWSaveGame::CurrentVersion||
        (Save.bHasPlayerTransform&&!Save.PlayerTransform.IsValid()))
    {Error=TEXT("Invalid save version or player transform.");return false;}
    TSet<FString> ActorIds;
    for(const auto& Actor:Save.WorldActors)
    {
        if(Actor.ActorId.IsEmpty()||ActorIds.Contains(Actor.ActorId)||!FMath::IsFinite(Actor.Health)||Actor.Health<0.f||
            !FMath::IsFinite(Actor.SpawnProgress)||Actor.SpawnProgress<0.f||(Actor.bHasTransform&&!Actor.Transform.IsValid()))
        {Error=TEXT("Invalid or duplicate world actor record; no player resources were restored.");return false;}
        ActorIds.Add(Actor.ActorId);
    }
    FDWCombatPlayerVitalsConfig C;FDWCombatWeaponResourceState WeaponCandidate;
    if(!FDWCombatPlayerVitalsConfig::Build(Rules,C,Error)
        ||!FDWCombatPlayerVitalsRuntime::Validate(C,Save.CombatPlayerResources.Vitals,Error)
        ||!BuildWeaponCandidate(Rules,Save.CombatPlayerResources,WeaponCandidate,Error))return false;
    const auto& V=Save.CombatPlayerResources.Vitals;
    if(HealthFloor(C)>=HealthMaximum(C)||static_cast<double>(Save.Health)!=V.Health
        ||Save.Health<HealthFloor(C)||Save.Health>HealthMaximum(C)
        ||Save.Transformation!=static_cast<float>(V.Special-V.SpecialGrowthCarry)
        ||Save.bYeastForm!=(V.BaseForm==EDWCombatBaseForm::Yeast)
        ||Save.SprintAlcoholElapsed!=0.f||Save.TransformationDecayElapsed!=0.f)
    {Error=TEXT("Legacy compatibility mirrors disagree with authoritative player resources.");return false;}
    Error.Reset();return true;
}
