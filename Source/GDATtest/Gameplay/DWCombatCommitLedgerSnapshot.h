#pragma once
#include "DWCombatCommitLedger.h"
#include <memory>
#include <string>
#include <vector>

namespace DWCombatCommit
{
struct FLifeSave { FId Entity=0,Life=0; };
struct FActionSave { FId Id=0; FEntityLife Owner; bool Active=false; };
struct FEmitterSave { FId Id=0; FEntityLife Owner; FActionId RootEpisode; bool Active=false; };
struct FReceiptSave
{
    FReceipt Receipt; bool Resource=false; FEntityLife Source,Target; FActionId Action;
    EResourceKind ResourceKind=EResourceKind::WeaponUse;
};
struct FKeySave { std::array<FId,8> Key{}; FId Receipt=0; };
struct FEmissionSave { FId Parent=0,Ordinal=0,Emitter=0; };
struct FLedgerSnapshot
{
    std::uint32_t Schema=1;
    FId Epoch=0,LastAction=0,LastEmitter=0,LastReceipt=0;
    std::vector<FLifeSave> Lives;
    std::vector<FActionSave> Actions;
    std::vector<FEmitterSave> Emitters;
    std::vector<FReceiptSave> Receipts;
    std::vector<FKeySave> Keys;
    std::vector<FEmissionSave> Emissions;
};
// Move-only, single-use candidate. Only the ledger can adopt it; root aggregates all native facts before publishing.
struct FLedgerRestorePlan
{
    FLedgerRestorePlan()=default;
    FLedgerRestorePlan(FLedgerRestorePlan&&)=default;
    FLedgerRestorePlan& operator=(FLedgerRestorePlan&&)=default;
    FLedgerRestorePlan(const FLedgerRestorePlan&)=delete;
    FLedgerRestorePlan& operator=(const FLedgerRestorePlan&)=delete;
    FId NewEpoch()const;
    std::vector<FReceiptId> PendingDamage()const;
    std::optional<FReceipt> FindReceipt(FId Id)const;
    bool IsEmitterActive(FId Id)const;
private:
    friend class FLedger;
    std::unique_ptr<FLedger> Candidate;
    FLedgerSnapshot Expected;
};
}
