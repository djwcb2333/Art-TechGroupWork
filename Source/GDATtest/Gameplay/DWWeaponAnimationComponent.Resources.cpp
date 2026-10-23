#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipment.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatRules.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "Engine/World.h"

bool UDWWeaponAnimationComponent::RestoreCombatWeaponState(const FDWCombatWeaponResourceSnapshot& Snapshot,
    const FDWCombatRules& Rules,FString& OutError)
{
    OutError.Reset();auto* Owner=Player();
    if(!IsInGameThread()||!IsValid(this)||IsBeingDestroyed()||!IsValid(Owner)||Owner->IsActorBeingDestroyed()||
        bActive||CombatWeaponResources.Revision==MAX_int64||CombatWeaponResources.RestoreEpoch==MAX_uint64)
    {OutError=TEXT("Weapon resource restore requires a valid idle native owner.");return false;}
    int32 Capacity=0,ShotCost=0;double ReloadSeconds=0.;
    if(!Rules.TryGetCount(TEXT("weapon.shotgun.magazine"),Capacity,OutError)||
        !Rules.TryGetCount(TEXT("weapon.shotgun.ammo_per_shot"),ShotCost,OutError)||
        !Rules.TryGetNumber(TEXT("weapon.shotgun.reload_one_s"),EDWCombatRuleUnit::Seconds,ReloadSeconds,OutError))return false;
    if(Capacity<1||ShotCost<1||ShotCost>Capacity||!FMath::IsFinite(ReloadSeconds)||ReloadSeconds<=0.||ReloadSeconds>MAX_flt||
        Snapshot.LoadedShotgunShells<0||Snapshot.LoadedShotgunShells>Capacity)
    {OutError=TEXT("Invalid shotgun resource snapshot or rules.");return false;}
    for(FName Id:Snapshot.OwnedPermanentWeaponIds)
        if(FDWCombatItemRegistry::GetItemRole(Id)!=EDWCombatItemRole::PermanentWeapon)
        {OutError=TEXT("Ownership snapshot contains a non-permanent-weapon item.");return false;}
    const auto* Shotgun=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);
    if(!Shotgun||(Snapshot.LoadedShotgunShells>0&&!Snapshot.OwnedPermanentWeaponIds.Contains(Shotgun->ItemId)))
    {OutError=TEXT("Loaded shotgun shells require explicit shotgun ownership.");return false;}
    double SwitchLock=0.;
    if(!DWCombatEquipment::ValidateIdentity(Snapshot,OutError)||!DWCombatEquipment::BuildSwitchLock(Rules,SwitchLock,OutError))return false;
    EDWAnimatedWeapon Selection;if(!DWCombatEquipment::ToPresentation(Snapshot.CurrentEquipmentId,Selection))return false;
    FDWCombatWeaponResourceSnapshot Candidate=Snapshot;
    EndWeaponResourceActions();
    CombatWeaponResources.Snapshot=MoveTemp(Candidate);
    CombatWeaponResources.ShotgunCapacity=Capacity;CombatWeaponResources.ShotgunRoundsPerShot=ShotCost;
    CombatWeaponResources.ReloadOneSeconds=ReloadSeconds;CombatWeaponResources.bRulesReady=true;
    ++CombatWeaponResources.Revision;++CombatWeaponResources.RestoreEpoch;
    SelectedWeapon=Selection;EquipmentSwitchLockSeconds=SwitchLock;bEquipmentRulesReady=true;
    RefreshDaggerEquipmentFacts();
    AmmoInMagazine=SelectedWeapon==EDWAnimatedWeapon::Shotgun?CombatWeaponResources.Snapshot.LoadedShotgunShells:0;
    return true;
}

void UDWWeaponAnimationComponent::InitializeCombatWeaponRules()
{
    const auto* Owner=Player();const auto* GI=Owner?Owner->GetGameInstance<UDWGameInstance>():nullptr;
    if(!GI||!GI->GetCombatRules())return;
    FString Error;
    // Preserve an explicit prior restore; initialization never grants weapons or fills magazines.
    if(!RestoreCombatWeaponState(CombatWeaponResources.Snapshot,*GI->GetCombatRules(),Error))
        UE_LOG(LogTemp,Warning,TEXT("Combat weapon resource configuration rejected: %s"),*Error);
}

bool UDWWeaponAnimationComponent::CanUseShotgunResources() const
{
    const auto* Owner=Player();const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);
    return IsInGameThread()&&IsValid(this)&&!IsBeingDestroyed()&&IsValid(Owner)&&!Owner->IsActorBeingDestroyed()&&
        Owner->IsCombatActionAllowed()&&HasCurrentEquipmentIdentity()&&SelectedWeapon==EDWAnimatedWeapon::Shotgun&&Binding&&
        CombatWeaponResources.bRulesReady&&CombatWeaponResources.Snapshot.OwnedPermanentWeaponIds.Contains(Binding->ItemId)&&
        CombatWeaponResources.Revision<MAX_int64&&CombatWeaponResources.Snapshot.LoadedShotgunShells>=0&&
        CombatWeaponResources.Snapshot.LoadedShotgunShells<=CombatWeaponResources.ShotgunCapacity;
}
bool UDWWeaponAnimationComponent::HasShotgunReloadCapacityAndStock() const
{
    if(!CanUseShotgunResources()||CombatWeaponResources.Snapshot.LoadedShotgunShells>=CombatWeaponResources.ShotgunCapacity)return false;
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);
    const auto* Inventory=Player()->GetInventory();
    return Inventory&&Binding&&Inventory->CountItem(Binding->AmmoItemId)>0;
}
void UDWWeaponAnimationComponent::EndWeaponResourceActions()
{
    if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))
    {Authority->EndAction(ReloadResourceAction);Authority->EndAction(MagazineResourceAction);}
    ReloadResourceAction={};MagazineResourceAction={};ReloadShellOrdinal=0;
    bReloadShellCompletionDue=bReloadShellCommitted=bMagazineShotEventDue=false;
}
bool UDWWeaponAnimationComponent::EnterReloadShellPhase()
{
    if(!bReloading||!ReloadResourceAction.Id||ReloadShellOrdinal==MAX_uint64||!HasShotgunReloadCapacityAndStock())return false;
    // DurationSeconds drives both source pose and procedural samples. Never retime art from a gameplay rule.
    // Current approved ReloadOne source cycle and rule are both .55 s; a future mismatch requires coordination.
    if(!FMath::IsFinite(ActiveClip.DurationSeconds)||
        !FMath::IsNearlyEqual(static_cast<double>(ActiveClip.DurationSeconds),CombatWeaponResources.ReloadOneSeconds,1.e-6))return false;
    ++ReloadShellOrdinal;bReloadShellCommitted=false;bReloadShellCompletionDue=false;
    return true;
}
