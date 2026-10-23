#include "DWCombatCommitLedger.h"
#include <limits>

namespace DWCombatCommit
{
namespace
{
bool Valid(FEntityLife V) { return V.Entity != 0 && V.Life != 0; }
bool Same(FEntityLife A, FEntityLife B) { return A.Entity == B.Entity && A.Life == B.Life; }
FAdmission Failure(EAdmission Reason) { FAdmission R; R.Result = Reason; return R; }
bool Allocate(FId& Last) { if (Last == std::numeric_limits<FId>::max()) return false; ++Last; return true; }
}

bool FLedger::SetWorldEpoch(FId NewEpoch)
{
    if (NewEpoch == 0 || NewEpoch <= Epoch) return false;
    Epoch = NewEpoch;
    Lives.clear(); Actions.clear(); Emitters.clear(); Receipts.clear(); Keys.clear(); Emissions.clear();
    return true;
}
bool FLedger::CurrentLife(FEntityLife Entity) const
{
    const auto It = Lives.find(Entity.Entity);
    return Epoch != 0 && Valid(Entity) && It != Lives.end() && It->second == Entity.Life;
}
bool FLedger::SetLife(FEntityLife Entity)
{
    if (Epoch == 0 || !Valid(Entity)) return false;
    const auto Old = Lives.find(Entity.Entity);
    if (Old != Lives.end() && Entity.Life <= Old->second) return false;
    Lives[Entity.Entity] = Entity.Life;
    for (auto& Pair : Actions)
        if (Pair.second.Owner.Entity == Entity.Entity) Pair.second.Active = false;
    for (auto& Pair : Receipts)
    {
        auto& R = Pair.second;
        if (R.Receipt.State != EReceiptState::Pending) continue;
        // Landed damage is owned by the target: source death cannot undo it.
        if ((R.Resource && R.Source.Entity == Entity.Entity) || (!R.Resource && R.Target.Entity == Entity.Entity))
            R.Receipt.State = EReceiptState::Invalidated;
    }
    return true;
}
FActionId FLedger::BeginAction(FEntityLife Owner)
{
    if (!CurrentLife(Owner) || !Allocate(LastAction)) return {};
    Actions.emplace(LastAction, FActionRecord{Owner, true});
    return {Epoch, LastAction};
}
bool FLedger::CurrentAction(FActionId Action) const
{
    if (Action.Epoch != Epoch || Action.Id == 0) return false;
    const auto It = Actions.find(Action.Id);
    return It != Actions.end() && It->second.Active && CurrentLife(It->second.Owner);
}
bool FLedger::EndAction(FActionId Action)
{
    if (!CurrentAction(Action)) return false;
    Actions.find(Action.Id)->second.Active = false;
    for (auto& Pair : Receipts)
    {
        auto& R = Pair.second;
        if (R.Resource && R.Action.Id == Action.Id && R.Receipt.State == EReceiptState::Pending)
            R.Receipt.State = EReceiptState::Invalidated;
    }
    return true;
}
FAdmission FLedger::Existing(const FKey& Key) const
{
    const auto It = Keys.find(Key);
    if (It == Keys.end()) return Failure(EAdmission::Invalid);
    return {EAdmission::Duplicate, Receipts.at(It->second).Receipt};
}
FAdmission FLedger::Admit(const FKey& Key, FRecord Record)
{
    if (!Allocate(LastReceipt)) return Failure(EAdmission::Invalid);
    Record.Receipt = {{Epoch, LastReceipt}, EReceiptState::Pending};
    Receipts.emplace(LastReceipt, Record); Keys.emplace(Key, LastReceipt);
    return {EAdmission::Admitted, Record.Receipt};
}
FAdmission FLedger::AdmitResource(const FResourceRequest& R)
{
    if (R.Action.Id == 0 || R.Action.Epoch == 0 || R.Phase == 0 || R.Ordinal == 0 ||
        static_cast<unsigned>(R.Kind) > static_cast<unsigned>(EResourceKind::PlayerRespawn)) return Failure(EAdmission::Invalid);
    if (R.Action.Epoch != Epoch) return Failure(EAdmission::Stale);
    const FKey Key{1, Epoch, R.Action.Id, R.Phase, static_cast<FId>(R.Kind), R.Ordinal, 0, 0};
    if (Keys.find(Key) != Keys.end()) return Existing(Key);
    if (!CurrentAction(R.Action)) return Failure(EAdmission::Stale);
    FRecord Record; Record.Resource = true; Record.Action = R.Action;
    Record.Source = Actions.at(R.Action.Id).Owner; Record.ResourceKind = R.Kind;
    return Admit(Key, Record);
}
FAdmission FLedger::AdmitDamage(const FDamageRequest& R)
{
    if (!Valid(R.Target)) return Failure(EAdmission::Invalid);
    if (!CurrentLife(R.Target)) return Failure(EAdmission::Stale);
    FKey Key{}; FRecord Record; Record.Target = R.Target; bool SourceCurrent = false;
    if (const auto* S = std::get_if<FActionDamage>(&R.Source))
    {
        if (S->Action.Id == 0 || S->Action.Epoch == 0 || S->Phase == 0 || S->PacketOrdinal == 0 ||
            static_cast<unsigned>(S->Kind) > static_cast<unsigned>(EActionDamageKind::EnvironmentFatal)) return Failure(EAdmission::Invalid);
        if (S->Action.Epoch != Epoch) return Failure(EAdmission::Stale);
        const auto A = Actions.find(S->Action.Id);
        if (A == Actions.end()) return Failure(EAdmission::Stale);
        Record.Source = A->second.Owner; Record.Action = S->Action;
        Key = {2, Epoch, S->Action.Id, S->Phase, static_cast<FId>(S->Kind), S->PacketOrdinal, R.Target.Entity, R.Target.Life};
        SourceCurrent = CurrentAction(S->Action);
    }
    else if (const auto* Emitted = std::get_if<FEmitterDamage>(&R.Source))
    {
        if (Emitted->Emitter.Id == 0 || Emitted->Emitter.Epoch == 0 || Emitted->PacketOrdinal == 0 ||
            static_cast<unsigned>(Emitted->Kind) > static_cast<unsigned>(EEmitterDamageKind::AreaTick)) return Failure(EAdmission::Invalid);
        if (Emitted->Emitter.Epoch != Epoch) return Failure(EAdmission::Stale);
        const auto E = Emitters.find(Emitted->Emitter.Id);
        if (E == Emitters.end() || E->second.RootEpisode.Id != 0) return Failure(EAdmission::Invalid);
        Record.Source = E->second.Owner;
        Key = {3, Epoch, Emitted->Emitter.Id, 0, static_cast<FId>(Emitted->Kind), Emitted->PacketOrdinal, R.Target.Entity, R.Target.Life};
        SourceCurrent = E->second.Active;
    }
    else if (const auto* Mine = std::get_if<FRootMineDamage>(&R.Source))
    {
        if (Mine->Emitter.Id == 0 || Mine->Emitter.Epoch == 0) return Failure(EAdmission::Invalid);
        if (Mine->Emitter.Epoch != Epoch) return Failure(EAdmission::Stale);
        const auto E = Emitters.find(Mine->Emitter.Id);
        if (E == Emitters.end() || E->second.RootEpisode.Id == 0) return Failure(EAdmission::Invalid);
        Record.Source = E->second.Owner;
        Key = {4, Epoch, E->second.RootEpisode.Id, 0, 0, 0, R.Target.Entity, 0};
        SourceCurrent = E->second.Active;
    }
    else if (const auto* Vitals = std::get_if<FVitalsDamage>(&R.Source))
    {
        if (Vitals->Epoch == 0 || !Valid(Vitals->Owner) || !Same(Vitals->Owner, R.Target) || Vitals->PeriodOrdinal == 0 ||
            static_cast<unsigned>(Vitals->Source) > static_cast<unsigned>(EVitalsSource::Starvation)) return Failure(EAdmission::Invalid);
        if (Vitals->Epoch != Epoch) return Failure(EAdmission::Stale);
        Record.Source = Vitals->Owner;
        Key = {5, Epoch, Vitals->Owner.Entity, Vitals->Owner.Life, static_cast<FId>(Vitals->Source), Vitals->PeriodOrdinal, R.Target.Entity, R.Target.Life};
        SourceCurrent = CurrentLife(Vitals->Owner);
    }
    else return Failure(EAdmission::Invalid);
    if (Keys.find(Key) != Keys.end()) return Existing(Key);
    if (!SourceCurrent) return Failure(EAdmission::Stale);
    return Admit(Key, Record);
}
EResolve FLedger::Resolve(FReceiptId Handle, EOutcome Outcome)
{
    if (Handle.Id == 0 || Handle.Epoch == 0 || static_cast<unsigned>(Outcome) > static_cast<unsigned>(EOutcome::Rejected)) return EResolve::Invalid;
    if (Handle.Epoch != Epoch) return EResolve::Stale;
    const auto It = Receipts.find(Handle.Id);
    if (It == Receipts.end()) return EResolve::Invalid;
    auto& R = It->second;
    if (R.Receipt.State == EReceiptState::Invalidated) return EResolve::Stale;
    if (R.Receipt.State != EReceiptState::Pending) return EResolve::AlreadyFinal;
    if ((R.Resource && !CurrentAction(R.Action)) || (!R.Resource && !CurrentLife(R.Target)))
    { R.Receipt.State = EReceiptState::Invalidated; return EResolve::Stale; }
    R.Receipt.State = Outcome == EOutcome::Committed ? EReceiptState::Committed : EReceiptState::Rejected;
    return EResolve::Resolved;
}
std::optional<FReceipt> FLedger::FindReceipt(FReceiptId Handle) const
{
    if (Handle.Epoch != Epoch || Handle.Id == 0) return std::nullopt;
    const auto It = Receipts.find(Handle.Id);
    return It == Receipts.end() ? std::nullopt : std::optional<FReceipt>(It->second.Receipt);
}
FEmission FLedger::ReleaseEmitter(FReceiptId Parent, FId EmissionOrdinal)
{
    if (Parent.Epoch == 0 || Parent.Id == 0 || EmissionOrdinal == 0) return {};
    if (Parent.Epoch != Epoch) return {EEmission::Stale, {}};
    const auto It = Receipts.find(Parent.Id);
    if (It == Receipts.end() || !It->second.Resource || It->second.Receipt.State != EReceiptState::Committed ||
        (It->second.ResourceKind != EResourceKind::WeaponUse && It->second.ResourceKind != EResourceKind::RootDeployment)) return {};
    const auto Key = std::make_pair(Parent.Id, EmissionOrdinal);
    const auto Previous = Emissions.find(Key);
    if (Previous != Emissions.end())
        return {Emitters.at(Previous->second).Active ? EEmission::Existing : EEmission::Retired, {Epoch, Previous->second}};
    // Register releases before publishing callbacks. A historical committed receipt
    // may identify an existing effect, but cannot mint new effects after action exit.
    if (!CurrentAction(It->second.Action)) return {EEmission::Stale, {}};
    if (!Allocate(LastEmitter)) return {};
    FEmitterRecord Record; Record.Owner = It->second.Source;
    if (It->second.ResourceKind == EResourceKind::RootDeployment) Record.RootEpisode = It->second.Action;
    Emitters.emplace(LastEmitter, Record); Emissions.emplace(Key, LastEmitter);
    return {EEmission::Created, {Epoch, LastEmitter}};
}
FEmission FLedger::CommitWeaponUseWithEmitter(FReceiptId Parent,FId Ordinal)
{
    if(!Parent.Id||!Parent.Epoch||!Ordinal)return {};
    if(Parent.Epoch!=Epoch)return {EEmission::Stale,{}};
    const auto It=Receipts.find(Parent.Id);
    if(It==Receipts.end()||!It->second.Resource||It->second.ResourceKind!=EResourceKind::WeaponUse)return {};
    const auto Key=std::make_pair(Parent.Id,Ordinal);const auto Prior=Emissions.find(Key);
    if(Prior!=Emissions.end())
        return {Emitters.at(Prior->second).Active?EEmission::Existing:EEmission::Retired,{Epoch,Prior->second}};
    if(It->second.Receipt.State!=EReceiptState::Pending||!CurrentAction(It->second.Action))return {EEmission::Stale,{}};
    if(LastEmitter==std::numeric_limits<FId>::max())return {};
    // No callback can observe these containers. Ordinary failure paths all precede fact adoption.
    // As with existing ledger containers, process-level allocation failure is not a recoverable gameplay result.
    const FId Id=LastEmitter+1;FEmitterRecord E;E.Owner=It->second.Source;
    Emitters.emplace(Id,E);Emissions.emplace(Key,Id);
    LastEmitter=Id;It->second.Receipt.State=EReceiptState::Committed;
    return {EEmission::Created,{Epoch,Id}};
}

bool FLedger::IsEmitterActive(FEmitterId Handle) const
{const auto E=Emitters.find(Handle.Id);return Handle.Id&&Handle.Epoch==Epoch&&E!=Emitters.end()&&E->second.Active;}
bool FLedger::RetireEmitter(FEmitterId Handle)
{
    if (Handle.Epoch != Epoch || Handle.Id == 0) return false;
    const auto It = Emitters.find(Handle.Id);
    if (It == Emitters.end() || !It->second.Active) return false;
    It->second.Active = false; return true;
}
}
