#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatRecoveryData.h"
#include "DWInventoryComponent.h"
class ADWPlayerCharacter;
class ADWCombatDeathBag;
class UDWCombatAuthoritySubsystem;
class UDWCombatRecoverySubsystem;
enum class EDWRecoveryNotice:uint8 {Death,Claim,Respawn};
struct FDWCombatRecoveryNotice
{
    EDWRecoveryNotice Kind=EDWRecoveryNotice::Death;
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    TWeakObjectPtr<UDWCombatRecoverySubsystem> Recovery;
    DWCombatCommit::FEntityLife Life;
    FGuid PlayerId,BagId;
    uint64 WorldEpoch=0,RecoveryRevision=0;
    bool bInventoryChanged=false;
    FTransform Destination=FTransform::Identity;
};
struct FDWPreparedCombatDeath
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWCombatRecoverySubsystem> Recovery;
    FDWPreparedInventoryChange Inventory;
    FDWCombatRecoverySave Candidate;
    FDWCombatRecoveryNotice Notice;
    bool bValid=false;
};
/** Fixed native transactions, all facts before the shared authority FIFO. */
class FDWCombatRecoveryTransaction final
{
    friend class UDWCombatAuthoritySubsystem;
    friend class UDWCombatRecoverySubsystem;
    friend class ADWCombatDeathBag;
    static bool PrepareFatal(ADWPlayerCharacter&,FDWPreparedCombatDeath&);
    static void CommitFatalUnchecked(FDWPreparedCombatDeath&&);
    static bool Claim(ADWCombatDeathBag&,ADWPlayerCharacter&);
    static bool Respawn(ADWPlayerCharacter&);
    static void Publish(const FDWCombatRecoveryNotice&);
};
