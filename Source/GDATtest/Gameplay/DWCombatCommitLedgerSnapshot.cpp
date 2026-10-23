#include "DWCombatCommitLedgerSnapshot.h"
#include <algorithm>
#include <limits>
#include <set>

namespace DWCombatCommit
{
namespace
{
bool SnapshotSame(FEntityLife A,FEntityLife B){return A.Entity==B.Entity&&A.Life==B.Life;}
bool SnapshotZero(FEntityLife A){return A.Entity==0&&A.Life==0;}
bool SnapshotSame(FActionId A,FActionId B){return A.Epoch==B.Epoch&&A.Id==B.Id;}
bool SnapshotSame(const FReceiptSave& A,const FReceiptSave& B)
{return A.Receipt.Handle.Epoch==B.Receipt.Handle.Epoch&&A.Receipt.Handle.Id==B.Receipt.Handle.Id&&A.Receipt.State==B.Receipt.State&&A.Resource==B.Resource&&SnapshotSame(A.Source,B.Source)&&SnapshotSame(A.Target,B.Target)&&SnapshotSame(A.Action,B.Action)&&A.ResourceKind==B.ResourceKind;}
bool SnapshotEqual(const FLedgerSnapshot& A,const FLedgerSnapshot& B)
{
    if(A.Schema!=B.Schema||A.Epoch!=B.Epoch||A.LastAction!=B.LastAction||A.LastEmitter!=B.LastEmitter||A.LastReceipt!=B.LastReceipt||A.Lives.size()!=B.Lives.size()||A.Actions.size()!=B.Actions.size()||A.Emitters.size()!=B.Emitters.size()||A.Receipts.size()!=B.Receipts.size()||A.Keys.size()!=B.Keys.size()||A.Emissions.size()!=B.Emissions.size())return false;
    for(size_t I=0;I<A.Lives.size();++I)if(A.Lives[I].Entity!=B.Lives[I].Entity||A.Lives[I].Life!=B.Lives[I].Life)return false;
    for(size_t I=0;I<A.Actions.size();++I)if(A.Actions[I].Id!=B.Actions[I].Id||!SnapshotSame(A.Actions[I].Owner,B.Actions[I].Owner)||A.Actions[I].Active!=B.Actions[I].Active)return false;
    for(size_t I=0;I<A.Emitters.size();++I)if(A.Emitters[I].Id!=B.Emitters[I].Id||!SnapshotSame(A.Emitters[I].Owner,B.Emitters[I].Owner)||!SnapshotSame(A.Emitters[I].RootEpisode,B.Emitters[I].RootEpisode)||A.Emitters[I].Active!=B.Emitters[I].Active)return false;
    for(size_t I=0;I<A.Receipts.size();++I)if(!SnapshotSame(A.Receipts[I],B.Receipts[I]))return false;
    for(size_t I=0;I<A.Keys.size();++I)if(A.Keys[I].Key!=B.Keys[I].Key||A.Keys[I].Receipt!=B.Keys[I].Receipt)return false;
    for(size_t I=0;I<A.Emissions.size();++I)if(A.Emissions[I].Parent!=B.Emissions[I].Parent||A.Emissions[I].Ordinal!=B.Emissions[I].Ordinal||A.Emissions[I].Emitter!=B.Emissions[I].Emitter)return false;
    return true;
}
}
FLedgerSnapshot FLedger::CaptureSnapshot()const
{
    FLedgerSnapshot S;S.Epoch=Epoch;S.LastAction=LastAction;S.LastEmitter=LastEmitter;S.LastReceipt=LastReceipt;
    for(const auto& P:Lives)S.Lives.push_back({P.first,P.second});
    for(const auto& P:Actions)S.Actions.push_back({P.first,P.second.Owner,P.second.Active});
    for(const auto& P:Emitters)S.Emitters.push_back({P.first,P.second.Owner,P.second.RootEpisode,P.second.Active});
    for(const auto& P:Receipts){const auto& R=P.second;S.Receipts.push_back({R.Receipt,R.Resource,R.Source,R.Target,R.Action,R.ResourceKind});}
    for(const auto& P:Keys)S.Keys.push_back({P.first,P.second});
    for(const auto& P:Emissions)S.Emissions.push_back({P.first.first,P.first.second,P.second});return S;
}
bool FLedger::ValidateSnapshot(const FLedgerSnapshot& S,std::string& Error)
{
    constexpr size_t Limit=262144;
    const auto Bad=[&](const char* M){Error=M;return false;};
    if(S.Schema!=1||!S.Epoch)return Bad("ledger schema or epoch");
    if(S.Lives.size()>Limit||S.Actions.size()>Limit||S.Emitters.size()>Limit||S.Receipts.size()>Limit||S.Keys.size()>Limit||S.Emissions.size()>Limit)return Bad("ledger count limit");
    std::map<FId,FId> L;std::map<FId,const FActionSave*> A;std::map<FId,const FEmitterSave*> E;std::map<FId,const FReceiptSave*> R;
    FId Previous=0;
    for(const auto& V:S.Lives){if(!V.Entity||!V.Life||V.Entity<=Previous||!L.emplace(V.Entity,V.Life).second)return Bad("lives must be unique ordered nonzero");Previous=V.Entity;}
    const auto Historic=[&](FEntityLife V){const auto I=L.find(V.Entity);return V.Life&&I!=L.end()&&V.Life<=I->second;};
    const auto Current=[&](FEntityLife V){const auto I=L.find(V.Entity);return V.Life&&I!=L.end()&&V.Life==I->second;};
    Previous=0;for(const auto& V:S.Actions){if(!V.Id||V.Id<=Previous||V.Id>S.LastAction||!Historic(V.Owner)||(V.Active&&!Current(V.Owner)))return Bad("invalid action");A.emplace(V.Id,&V);Previous=V.Id;}
    Previous=0;for(const auto& V:S.Emitters)
    {if(!V.Id||V.Id<=Previous||V.Id>S.LastEmitter||!Historic(V.Owner))return Bad("invalid emitter");
        if(V.RootEpisode.Id){const auto I=A.find(V.RootEpisode.Id);if(V.RootEpisode.Epoch!=S.Epoch||I==A.end()||!SnapshotSame(I->second->Owner,V.Owner))return Bad("root emitter parent");}
        else if(V.RootEpisode.Epoch)return Bad("partial root identity");E.emplace(V.Id,&V);Previous=V.Id;}
    Previous=0;for(const auto& V:S.Receipts)
    {
        if(!V.Receipt.Handle.Id||V.Receipt.Handle.Id<=Previous||V.Receipt.Handle.Id>S.LastReceipt||V.Receipt.Handle.Epoch!=S.Epoch||static_cast<unsigned>(V.Receipt.State)>static_cast<unsigned>(EReceiptState::Invalidated)||!Historic(V.Source)||static_cast<unsigned>(V.ResourceKind)>static_cast<unsigned>(EResourceKind::PlayerRespawn))return Bad("invalid receipt");
        if(V.Action.Id){const auto I=A.find(V.Action.Id);if(V.Action.Epoch!=S.Epoch||I==A.end()||!SnapshotSame(I->second->Owner,V.Source))return Bad("receipt action identity");}
        else if(V.Action.Epoch)return Bad("partial action identity");
        if(V.Resource){if(!V.Action.Id||!SnapshotZero(V.Target))return Bad("resource target or action");if(V.Receipt.State==EReceiptState::Pending&&!A.at(V.Action.Id)->Active)return Bad("inactive pending resource");}
        else if(!Historic(V.Target)||(V.Receipt.State==EReceiptState::Pending&&!Current(V.Target)))return Bad("damage target life");
        R.emplace(V.Receipt.Handle.Id,&V);Previous=V.Receipt.Handle.Id;
    }
    if(S.Keys.size()!=R.size())return Bad("one key per receipt required");
    std::set<FId> KeyReceipts;std::array<FId,8> LastKey{};bool First=true;
    for(const auto& V:S.Keys)
    {
        const auto& K=V.Key;const auto I=R.find(V.Receipt);if(I==R.end()||!KeyReceipts.insert(V.Receipt).second||(!First&&!(LastKey<K))||K[1]!=S.Epoch)return Bad("invalid or duplicate key");LastKey=K;First=false;const auto& Receipt=*I->second;
        if(K[0]==1){if(!Receipt.Resource||K[2]!=Receipt.Action.Id||!K[3]||K[4]!=static_cast<FId>(Receipt.ResourceKind)||!K[5]||K[6]||K[7])return Bad("resource key");}
        else
        {
            if(Receipt.Resource||K[6]!=Receipt.Target.Entity)return Bad("damage key target");
            if(K[0]==2){if(!Receipt.Action.Id||K[2]!=Receipt.Action.Id||!K[3]||K[4]>static_cast<FId>(EActionDamageKind::EnvironmentFatal)||!K[5]||K[7]!=Receipt.Target.Life)return Bad("action damage key");}
            else if(K[0]==3){const auto J=E.find(K[2]);if(Receipt.Action.Id||J==E.end()||J->second->RootEpisode.Id||!SnapshotSame(J->second->Owner,Receipt.Source)||K[3]||K[4]>static_cast<FId>(EEmitterDamageKind::AreaTick)||!K[5]||K[7]!=Receipt.Target.Life)return Bad("emitter damage key");}
            else if(K[0]==4){const auto J=A.find(K[2]);if(Receipt.Action.Id||J==A.end()||!SnapshotSame(J->second->Owner,Receipt.Source)||K[3]||K[4]||K[5]||K[7])return Bad("root shared key");bool Parent=false;for(const auto& Emit:E)if(Emit.second->RootEpisode.Id==K[2]){Parent=true;break;}if(!Parent)return Bad("root key missing episode emitter");}
            else if(K[0]==5){if(Receipt.Action.Id||K[2]!=Receipt.Source.Entity||K[3]!=Receipt.Source.Life||!SnapshotSame(Receipt.Source,Receipt.Target)||K[4]>static_cast<FId>(EVitalsSource::Starvation)||!K[5]||K[7]!=Receipt.Target.Life)return Bad("vitals key");}
            else return Bad("unknown key domain");
        }
    }
    if(S.Emissions.size()!=E.size())return Bad("one release parent per emitter required");
    std::set<FId> Bound;std::pair<FId,FId> Last{};First=true;
    for(const auto& V:S.Emissions)
    {
        const auto Parent=R.find(V.Parent);const auto Emit=E.find(V.Emitter);const auto Key=std::make_pair(V.Parent,V.Ordinal);
        if(!V.Ordinal||Parent==R.end()||Emit==E.end()||!Bound.insert(V.Emitter).second||(!First&&!(Last<Key)))return Bad("emission identity");Last=Key;First=false;
        const auto& P=*Parent->second;const auto& Q=*Emit->second;
        if(!P.Resource||P.Receipt.State!=EReceiptState::Committed||!SnapshotSame(P.Source,Q.Owner)||(P.ResourceKind!=EResourceKind::WeaponUse&&P.ResourceKind!=EResourceKind::RootDeployment))return Bad("emitter requires paid parent");
        if(P.ResourceKind==EResourceKind::RootDeployment?!SnapshotSame(P.Action,Q.RootEpisode):Q.RootEpisode.Id!=0)return Bad("emitter root classification");
    }
    Error.clear();return true;
}
bool FLedger::PrepareSnapshotRestore(const FLedgerSnapshot& S,FId NewEpoch,const std::vector<FId>& KeepActions,FLedgerRestorePlan& Out,std::string& Error)const
{
    if(!ValidateSnapshot(S,Error))return false;
    if(NewEpoch<=Epoch||NewEpoch<=S.Epoch){Error="restore epoch must increase beyond current and saved worlds";return false;}
    std::set<FId> Keep;for(FId Id:KeepActions){if(!Id||!Keep.insert(Id).second){Error="duplicate active action whitelist";return false;}}
    auto C=std::make_unique<FLedger>();C->Epoch=NewEpoch;C->LastAction=std::max(LastAction,S.LastAction);C->LastEmitter=std::max(LastEmitter,S.LastEmitter);C->LastReceipt=std::max(LastReceipt,S.LastReceipt);
    for(const auto& V:S.Lives)C->Lives.emplace(V.Entity,V.Life);
    for(const auto& V:S.Actions){const bool Active=V.Active&&Keep.erase(V.Id)!=0;C->Actions.emplace(V.Id,FActionRecord{V.Owner,Active});}
    if(!Keep.empty()){Error="whitelist must name saved active actions";return false;}
    for(const auto& V:S.Emitters)C->Emitters.emplace(V.Id,FEmitterRecord{V.Owner,V.RootEpisode.Id?FActionId{NewEpoch,V.RootEpisode.Id}:FActionId{},V.Active});
    for(const auto& V:S.Receipts)
    {
        FRecord R;R.Receipt={{NewEpoch,V.Receipt.Handle.Id},V.Receipt.State};R.Resource=V.Resource;R.Source=V.Source;R.Target=V.Target;R.Action=V.Action.Id?FActionId{NewEpoch,V.Action.Id}:FActionId{};R.ResourceKind=V.ResourceKind;
        if(R.Resource&&R.Receipt.State==EReceiptState::Pending&&!C->Actions.at(R.Action.Id).Active)R.Receipt.State=EReceiptState::Invalidated;
        C->Receipts.emplace(R.Receipt.Handle.Id,R);
    }
    for(const auto& V:S.Keys){auto K=V.Key;K[1]=NewEpoch;C->Keys.emplace(K,V.Receipt);}
    for(const auto& V:S.Emissions)C->Emissions.emplace(std::make_pair(V.Parent,V.Ordinal),V.Emitter);
    FLedgerRestorePlan Plan;Plan.Expected=CaptureSnapshot();Plan.Candidate=std::move(C);Out=std::move(Plan);Error.clear();return true;
}
bool FLedger::CanCommitSnapshotRestore(const FLedgerRestorePlan& P)const
{return P.Candidate&&P.Candidate->Epoch>Epoch&&SnapshotEqual(CaptureSnapshot(),P.Expected);}
void FLedger::CommitSnapshotRestoreUnchecked(FLedgerRestorePlan&& P)
{
    // The world transaction validates all candidates immediately before its callback-free native adoption interval.
    if(!P.Candidate)return;auto& C=*P.Candidate;
    std::swap(Epoch,C.Epoch);std::swap(LastAction,C.LastAction);std::swap(LastEmitter,C.LastEmitter);std::swap(LastReceipt,C.LastReceipt);
    Lives.swap(C.Lives);Actions.swap(C.Actions);Emitters.swap(C.Emitters);Receipts.swap(C.Receipts);Keys.swap(C.Keys);Emissions.swap(C.Emissions);P.Candidate.reset();
}
FId FLedgerRestorePlan::NewEpoch()const{return Candidate?Candidate->CaptureSnapshot().Epoch:0;}
std::vector<FReceiptId> FLedgerRestorePlan::PendingDamage()const
{std::vector<FReceiptId> R;if(Candidate)for(const auto& V:Candidate->CaptureSnapshot().Receipts)if(!V.Resource&&V.Receipt.State==EReceiptState::Pending)R.push_back(V.Receipt.Handle);return R;}
std::optional<FReceipt> FLedgerRestorePlan::FindReceipt(FId Id)const{return Candidate?Candidate->FindReceipt({NewEpoch(),Id}):std::nullopt;}
bool FLedgerRestorePlan::IsEmitterActive(FId Id)const{return Candidate&&Candidate->IsEmitterActive({NewEpoch(),Id});}
}
