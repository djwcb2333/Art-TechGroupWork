#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"

class UDWInventoryComponent;
class AActor;
class UDWWeaponAnimationComponent;
class UDWCombatAuthoritySubsystem;

enum class EDWWeaponResourceOperation : uint8 { ReloadShell, MagazineShot, InventoryShot, EquipmentGrant };

/** Fixed immutable notification; no executable callback and no deferred mutation. */
struct FDWCombatWeaponResourceNotice
{
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    TWeakObjectPtr<UDWWeaponAnimationComponent> Weapon;
    TWeakObjectPtr<AActor> Owner;
    DWCombatCommit::FEntityLife OwnerLife;
    DWCombatCommit::FReceiptId Receipt;
    EDWWeaponResourceOperation Operation=EDWWeaponResourceOperation::ReloadShell;
    int64 WeaponRevision=0,InventoryRevision=0;
    uint64 WeaponRestoreEpoch=0;
    int32 LoadedBefore=0,LoadedAfter=0;
    bool bInventoryChanged=false;
    bool bPublishCommittedAttack=false;
    uint8 CommittedWeapon=0;FName CommittedRole;int32 CommittedEvent=0;
};

struct FDWCombatWeaponResourceResult
{
    DWCombatCommit::EAdmission Admission=DWCombatCommit::EAdmission::Invalid;
    DWCombatCommit::FReceipt Receipt;
    bool bCommittedThisCall=false;
};

/** Only concrete native owners may enter or publish. No public NoBroadcast/callback transaction. */
class FDWCombatInventoryTransaction final
{
    friend class UDWWeaponAnimationComponent;
    friend class UDWCombatAuthoritySubsystem;
    static FDWCombatWeaponResourceResult CommitReloadShell(UDWWeaponAnimationComponent& Weapon);
    static FDWCombatWeaponResourceResult CommitMagazineShot(UDWWeaponAnimationComponent& Weapon);
    static FDWCombatWeaponResourceResult Commit(UDWWeaponAnimationComponent& Weapon,EDWWeaponResourceOperation Operation);
    static void PublishWeaponResourceNotice(const FDWCombatWeaponResourceNotice& Notice);
};
