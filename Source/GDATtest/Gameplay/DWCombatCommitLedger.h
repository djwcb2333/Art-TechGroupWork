#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <variant>
#include <string>
#include <vector>

// D05 bookkeeping only: single simulation thread, no callbacks or gameplay mutation.
// All zero identities are invalid. The caller owns rules, timing, and actual effects.
namespace DWCombatCommit
{
using FId = std::uint64_t;
struct FEntityLife { FId Entity = 0; FId Life = 0; };
struct FActionId { FId Epoch = 0; FId Id = 0; };
struct FEmitterId { FId Epoch = 0; FId Id = 0; };
struct FReceiptId { FId Epoch = 0; FId Id = 0; };
enum class EResourceKind : std::uint8_t { WeaponUse, ReloadShell, RootDeployment, LegacyItemUse, CorePickup, CoreInjection, YeastDashStart, EquipmentGrant, DeathBagClaim, PlayerRespawn };
enum class EActionDamageKind : std::uint8_t { Melee, Dash, EnemyMelee, LegacyExternal, Shotgun, EnvironmentFatal };
enum class EEmitterDamageKind : std::uint8_t { ProjectileImpact, AreaTick };
enum class EVitalsSource : std::uint8_t { Dehydration, Starvation };
enum class EReceiptState : std::uint8_t { Pending, Committed, Rejected, Invalidated };
enum class EAdmission : std::uint8_t { Admitted, Duplicate, Invalid, Stale };
enum class EResolve : std::uint8_t { Resolved, AlreadyFinal, Invalid, Stale };
enum class EEmission : std::uint8_t { Created, Existing, Retired, Invalid, Stale };
enum class EOutcome : std::uint8_t { Committed, Rejected };
struct FReceipt { FReceiptId Handle; EReceiptState State = EReceiptState::Invalidated; };
struct FAdmission { EAdmission Result = EAdmission::Invalid; FReceipt Receipt; };
struct FEmission { EEmission Result = EEmission::Invalid; FEmitterId Handle; };
struct FResourceRequest
{
    FActionId Action; FId Phase = 0;
    EResourceKind Kind = EResourceKind::WeaponUse; FId Ordinal = 0;
};
struct FActionDamage
{
    FActionId Action; FId Phase = 0;
    EActionDamageKind Kind = EActionDamageKind::Melee; FId PacketOrdinal = 0;
};
struct FEmitterDamage
{
    FEmitterId Emitter; EEmitterDamageKind Kind = EEmitterDamageKind::ProjectileImpact;
    FId PacketOrdinal = 0;
};
// Inherits root episode from the RootDeployment parent. Individual mine and
// changing victim Life are deliberately absent from the shared ring dedup key.
struct FRootMineDamage { FEmitterId Emitter; };
struct FVitalsDamage
{
    FId Epoch = 0; FEntityLife Owner;
    EVitalsSource Source = EVitalsSource::Dehydration; FId PeriodOrdinal = 0;
};
using FDamageSource = std::variant<FActionDamage, FEmitterDamage, FRootMineDamage, FVitalsDamage>;
struct FDamageRequest { FDamageSource Source; FEntityLife Target; };

struct FLedgerSnapshot;
struct FLedgerRestorePlan;
class FLedger final
{
public:
    FLedger() = default;
    FLedgerSnapshot CaptureSnapshot() const;
    static bool ValidateSnapshot(const FLedgerSnapshot&,std::string& Error);
    bool PrepareSnapshotRestore(const FLedgerSnapshot&,FId NewEpoch,const std::vector<FId>& KeepActions,FLedgerRestorePlan&,std::string& Error)const;
    bool CanCommitSnapshotRestore(const FLedgerRestorePlan&)const;
    void CommitSnapshotRestoreUnchecked(FLedgerRestorePlan&&);
    FLedger(const FLedger&) = delete;
    FLedger& operator=(const FLedger&) = delete;
    FLedger(FLedger&&) = delete;
    FLedger& operator=(FLedger&&) = delete;
    // Epoch/Life must strictly increase; equal/older/zero rejects without changes.
    // Issued IDs never restart in this object, including across world resets.
    bool SetWorldEpoch(FId Epoch);
    bool SetLife(FEntityLife Entity);
    FActionId BeginAction(FEntityLife Owner);
    bool EndAction(FActionId Action);
    FAdmission AdmitResource(const FResourceRequest& Request);
    FAdmission AdmitDamage(const FDamageRequest& Request);
    EResolve Resolve(FReceiptId Receipt, EOutcome Outcome);
    std::optional<FReceipt> FindReceipt(FReceiptId Receipt) const;
    // Only a committed WeaponUse/RootDeployment can emit. Repeating parent+ordinal
    // returns its original emitter, even if retired; it cannot resurrect effects.
    // Only Created permits first-time spawning; Existing/Retired are lookup results.
    FEmission ReleaseEmitter(FReceiptId Parent, FId EmissionOrdinal);
    // Fixed one-emitter WeaponUse commit; failure leaves a pending receipt uncommitted.
    FEmission CommitWeaponUseWithEmitter(FReceiptId Parent,FId EmissionOrdinal);
    bool IsEmitterActive(FEmitterId Handle) const;
    bool RetireEmitter(FEmitterId Emitter);
private:
    using FKey = std::array<FId, 8>;
    struct FActionRecord { FEntityLife Owner; bool Active = true; };
    struct FEmitterRecord { FEntityLife Owner; FActionId RootEpisode; bool Active = true; };
    struct FRecord
    {
        FReceipt Receipt; bool Resource = false;
        FEntityLife Source; FEntityLife Target; FActionId Action;
        EResourceKind ResourceKind = EResourceKind::WeaponUse;
    };
    FId Epoch = 0, LastAction = 0, LastEmitter = 0, LastReceipt = 0;
    std::map<FId, FId> Lives;
    std::map<FId, FActionRecord> Actions;
    std::map<FId, FEmitterRecord> Emitters;
    std::map<FId, FRecord> Receipts;
    std::map<FKey, FId> Keys;
    std::map<std::pair<FId, FId>, FId> Emissions;
    bool CurrentLife(FEntityLife Entity) const;
    bool CurrentAction(FActionId Action) const;
    FAdmission Existing(const FKey& Key) const;
    FAdmission Admit(const FKey& Key, FRecord Record);
};
}
