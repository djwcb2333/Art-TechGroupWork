#pragma once
#include "CoreMinimal.h"
#include "DWCombatCoreInjection.h"
#include "DWCombatCommitLedger.h"
#include "DWInventoryComponent.h"

class ADWPlayerCharacter;
class UDWCombatAuthoritySubsystem;

struct FDWCoreInjectionSession
{
    FDWCoreInjectionPlan Plan;
    FDWCoreInjectionConfig Config;
    FDWInventorySlotSnapshot Slot;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    DWCombatCommit::FActionId Action;
    DWCombatCommit::FEntityLife Life;
    uint64 WorldEpoch=0,DamageRevision=0;
    FVector StationaryLocation=FVector::ZeroVector;
    EDWCombatBaseForm TargetForm=EDWCombatBaseForm::Normal;
};

struct FDWCombatCoreInjectionNotice
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    DWCombatCommit::FEntityLife Life;
    DWCombatCommit::FReceiptId Receipt;
    uint64 WorldEpoch=0;
    EDWCombatBaseForm Form=EDWCombatBaseForm::Normal;
};

// One concrete operation, no callback transaction and no public no-broadcast window.
class FDWCombatCoreInjectionTransaction
{
public:
    static bool Commit(ADWPlayerCharacter& Player);
private:
    friend class UDWCombatAuthoritySubsystem;
    static void Publish(const FDWCombatCoreInjectionNotice& Notice);
};
