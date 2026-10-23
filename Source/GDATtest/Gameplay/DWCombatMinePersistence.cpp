#include "DWCombatMinePersistence.h"
#include "DWCombatMineSubsystem.h"
#include "DWCombatMine.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWPlayerCharacter.h"
#include "Engine/World.h"
#include "Serialization/MemoryWriter.h"
#include <algorithm>

namespace DWMinePersistenceDetail
{
using namespace DWCombatCommit;
bool Same(FEntityLife A,FEntityLife B){return A.Entity==B.Entity&&A.Life==B.Life;}
bool Same(const FDWMoldRootIdentity& A,const FDWMoldRootIdentity& B)
{return A.WorldEpoch==B.WorldEpoch&&A.RootSerial==B.RootSerial&&A.Action.Id==B.Action.Id&&A.Action.Epoch==B.Action.Epoch&&A.Receipt.Id==B.Receipt.Id&&A.Receipt.Epoch==B.Receipt.Epoch&&Same(A.SourceLife,B.SourceLife)&&A.Anchor==B.Anchor;}
FDWMoldMineConfig Config(const FDWMoldMineConfigSave& S)
{FDWMoldMineConfig C;C.Count=S.Count;C.HitsPerTarget=S.HitsPerTarget;C.RingRadiusCm=S.RingRadiusCm;C.TriggerRadiusCm=S.TriggerRadiusCm;C.DamageRadiusCm=S.DamageRadiusCm;C.Damage=S.Damage;C.KnockbackCm=S.KnockbackCm;C.KnockbackSeconds=S.KnockbackSeconds;C.CooldownSeconds=S.CooldownSeconds;C.GroundToleranceCm=S.GroundToleranceCm;return C;}
FDWMoldMineConfigSave Config(const FDWMoldMineConfig& C)
{FDWMoldMineConfigSave S;S.Count=C.Count;S.HitsPerTarget=C.HitsPerTarget;S.RingRadiusCm=C.RingRadiusCm;S.TriggerRadiusCm=C.TriggerRadiusCm;S.DamageRadiusCm=C.DamageRadiusCm;S.Damage=C.Damage;S.KnockbackCm=C.KnockbackCm;S.KnockbackSeconds=C.KnockbackSeconds;S.CooldownSeconds=C.CooldownSeconds;S.GroundToleranceCm=C.GroundToleranceCm;return S;}
bool Empty(const FDWMoldMineConfigSave& C)
{return !C.Count&&!C.HitsPerTarget&&C.RingRadiusCm==0.&&C.TriggerRadiusCm==0.&&C.DamageRadiusCm==0.&&C.Damage==0.&&C.KnockbackCm==0.&&C.KnockbackSeconds==0.&&C.CooldownSeconds==0.&&C.GroundToleranceCm==0.;}
void Life(FArchive& A,FEntityLife V){A<<V.Entity<<V.Life;}
void Root(FArchive& A,FDWMoldRootIdentity R){A<<R.WorldEpoch<<R.RootSerial<<R.Action.Epoch<<R.Action.Id<<R.Receipt.Epoch<<R.Receipt.Id<<R.Anchor;Life(A,R.SourceLife);}
void Tuning(FArchive& A,FDWMoldMineConfig C){A<<C.Count<<C.HitsPerTarget<<C.RingRadiusCm<<C.TriggerRadiusCm<<C.DamageRadiusCm<<C.Damage<<C.KnockbackCm<<C.KnockbackSeconds<<C.CooldownSeconds<<C.GroundToleranceCm;}
uint32 ObjectId(const UObject* A){return A?A->GetUniqueID():0;}
template<class T>void Sorted(FArchive& A,TArray<T> V){V.Sort();int32 N=V.Num();A<<N;for(auto& X:V)A<<X;}
const FDWCombatActorIdentitySave* Actor(const FDWCombatAuthoritySave& A,uint64 Entity)
{return A.Actors.FindByPredicate([&](const auto& V){return V.Life.Entity==Entity;});}
}

struct FDWCombatMineRestoreState
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWCombatMineSubsystem> Mines;
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority;
    TArray<uint8> Expected;
    TMap<TWeakObjectPtr<ADWPlayerCharacter>,UDWCombatMineSubsystem::FPlayerState> Players;
    TMap<TWeakObjectPtr<ADWPlayerCharacter>,FDWMoldRootIdentity> PendingEntries;
    TMap<FGuid,UDWCombatMineSubsystem::FRing> Rings;
    TArray<TWeakObjectPtr<ADWCombatMine>> Staged,Retire;
    FDWMoldRootIdentity RestoredRoot;
    DWCombatCommit::FEntityLife PlayerLife;
    bool bRooted=false;
    double At=0.;uint64 OldEpoch=0,NewEpoch=0;
    bool bCommitted=false,bPublished=false;
    ~FDWCombatMineRestoreState()
    {if(!bCommitted)for(auto W:Staged)if(auto* A=W.Get();A&&!A->IsActorBeingDestroyed()&&A->GetWorld()&&!A->GetWorld()->bIsTearingDown)A->Destroy();}
};
void FDWCombatMineRestoreStateDeleter::operator()(FDWCombatMineRestoreState* P)const{delete P;}
FDWCombatMineRestorePlan::FDWCombatMineRestorePlan()=default;
FDWCombatMineRestorePlan::~FDWCombatMineRestorePlan()=default;
FDWCombatMineRestorePlan::FDWCombatMineRestorePlan(FDWCombatMineRestorePlan&&)=default;
FDWCombatMineRestorePlan& FDWCombatMineRestorePlan::operator=(FDWCombatMineRestorePlan&&)=default;

bool FDWCombatMinePersistence::Capture(ADWPlayerCharacter& P,FDWCombatPlayerExtendedSave& Out,FString& Error)
{
    auto* M=UDWCombatMineSubsystem::Get(&P);auto* A=UDWCombatAuthoritySubsystem::Get(&P);if(!M||!A){Error=TEXT("Mine world unavailable");return false;}
    const auto Life=A->FindRegisteredActorLife(&P);if(!Life.Entity){Error=TEXT("Player has no existing authority identity");return false;}
    FDWMoldMineRingSave Saved;if(!M->CaptureSave(&P,Saved,Error))return false;
    FDWMoldRootIdentity Root;const bool Rooted=P.CaptureFormalRootSnapshot().bActive;
    if(Rooted&&(!P.GetFormalRootIdentity(Root)||!DWMinePersistenceDetail::Same(Life,Root.SourceLife))){Error=TEXT("Root identity unavailable");return false;}
    FDWMoldMineConfigSave Tuning;TArray<FDWMineEmitterSave> Emitters;TArray<uint64> Hits;
    if(Saved.bActive)
    {
        const auto* R=M->Rings.Find(Saved.RingId);if(!R||!Rooted||!DWMinePersistenceDetail::Same(R->Root,Root)){Error=TEXT("Ring/root identity mismatch");return false;}
        Tuning=DWMinePersistenceDetail::Config(R->Config);Hits=R->HitEntities.Array();Hits.Sort();
        for(const auto& Mine:R->Mines)if(Mine.bReleased)Emitters.Add({Mine.Save.MineId,Mine.Emitter.Id});
    }
    Saved.Mines.Sort([](const auto& L,const auto& R){return L.Ordinal<R.Ordinal;});Saved.HitFormalEnemies.Sort();Saved.HitLegacyEnemies.Sort(FNameLexicalLess());
    Emitters.Sort([](const auto& L,const auto& R){return L.MineId<R.MineId;});
    Out.bRooted=Rooted;Out.RootAnchor=Rooted?Root.Anchor:FVector::ZeroVector;Out.RootAction=Rooted?Root.Action.Id:0;Out.RootReceipt=Rooted?Root.Receipt.Id:0;
    Out.PlayerEntity=Life.Entity;Out.PlayerLife=Life.Life;Out.Ring=MoveTemp(Saved);Out.MineConfig=Tuning;Out.MineEmitters=MoveTemp(Emitters);Out.RingHitEntities=MoveTemp(Hits);Error.Reset();return true;
}
bool FDWCombatMinePersistence::ValidateSnapshot(const FDWCombatPlayerExtendedSave& S,const FDWCombatAuthoritySave& A,FString& Error)
{
    using namespace DWCombatCommit;using namespace DWMinePersistenceDetail;
    const auto Bad=[&](const TCHAR* Text){Error=Text;return false;};
    if(!DWCombatEffectsSave::ValidateAuthority(A,Error)||!DWMoldMines::ValidateSnapshot(S.Ring,Error))return false;
    const auto* P=Actor(A,S.PlayerEntity);const FEntityLife Source{S.PlayerEntity,S.PlayerLife};
    if(!P||P->Kind!=EDWCombatSavedActorKind::Player||P->bEnded||!Same(P->Life,Source)||S.RootAnchor.ContainsNaN()||S.Ring.Mines.Num()>4096||S.MineEmitters.Num()>4096||S.RingHitEntities.Num()>262144)return Bad(TEXT("Invalid root player or mine count"));
    for(const auto& E:A.Ledger.Emitters)if(E.Active&&E.RootEpisode.Id&&E.Owner.Entity==Source.Entity)
        if(!S.bRooted||!S.Ring.bActive||E.RootEpisode.Id!=S.RootAction||!Same(E.Owner,Source))return Bad(TEXT("Live root emitter has no matching current ring"));
    if(!S.bRooted)
    {if(S.RootAction||S.RootReceipt||!S.RootAnchor.IsZero()||S.Ring.bActive||!S.MineEmitters.IsEmpty()||!S.RingHitEntities.IsEmpty()||!Empty(S.MineConfig))return Bad(TEXT("Inactive root has ring identity"));Error.Reset();return true;}
    if(P->bDead||!S.RootAction||!S.RootReceipt)return Bad(TEXT("Root owner is dead or unpaid"));
    const auto& L=A.Ledger;const auto Act=std::find_if(L.Actions.begin(),L.Actions.end(),[&](const auto& V){return V.Id==S.RootAction;});
    const auto Fee=std::find_if(L.Receipts.begin(),L.Receipts.end(),[&](const auto& V){return V.Receipt.Handle.Id==S.RootReceipt;});
    if(Act==L.Actions.end()||!Act->Active||!Same(Act->Owner,Source)||Fee==L.Receipts.end()||!Fee->Resource||Fee->ResourceKind!=EResourceKind::RootDeployment||Fee->Receipt.State!=EReceiptState::Committed||Fee->Action.Id!=S.RootAction||!Same(Fee->Source,Source))return Bad(TEXT("Missing committed root deployment"));
    if(!S.Ring.bActive)
    {if(!S.MineEmitters.IsEmpty()||!S.RingHitEntities.IsEmpty()||!Empty(S.MineConfig))return Bad(TEXT("Absent ring retains mine facts"));for(const auto& E:L.Emitters)if(E.Active&&E.RootEpisode.Id==S.RootAction)return Bad(TEXT("Active root emitter missing ring"));Error.Reset();return true;}
    const auto C=Config(S.MineConfig);if(!C.IsValid()||C.Count>4096||S.Ring.Mines.Num()>C.Count||S.Ring.Anchor!=S.RootAnchor||S.MineEmitters.Num()!=S.Ring.Mines.Num())return Bad(TEXT("Invalid released mine configuration or anchor"));
    TSet<uint64> Emitters;TSet<FGuid> MineIds;
    for(const auto& M:S.Ring.Mines)
    {
        if(M.Ordinal>C.Count)return Bad(TEXT("Mine ordinal exceeds released configuration"));
        const auto* Binding=S.MineEmitters.FindByPredicate([&](const auto& V){return V.MineId==M.MineId;});
        if(!Binding||!Binding->Emitter||Emitters.Contains(Binding->Emitter)||MineIds.Contains(Binding->MineId))return Bad(TEXT("Mine emitter mapping is not one to one"));
        Emitters.Add(Binding->Emitter);MineIds.Add(Binding->MineId);
        const auto E=std::find_if(L.Emitters.begin(),L.Emitters.end(),[&](const auto& V){return V.Id==Binding->Emitter;});
        const auto Parent=std::find_if(L.Emissions.begin(),L.Emissions.end(),[&](const auto& V){return V.Emitter==Binding->Emitter;});
        if(E==L.Emitters.end()||E->RootEpisode.Id!=S.RootAction||!Same(E->Owner,Source)||E->Active==M.bConsumed||Parent==L.Emissions.end()||Parent->Parent!=S.RootReceipt||Parent->Ordinal!=static_cast<uint64>(M.Ordinal))return Bad(TEXT("Mine is not the saved paid root emission"));
    }
    for(const auto& E:L.Emitters)if(E.Active&&E.RootEpisode.Id==S.RootAction&&!Emitters.Contains(E.Id))return Bad(TEXT("Live root emission omitted from ring"));
    TSet<uint64> Hits,Committed;for(uint64 Entity:S.RingHitEntities)
    {const auto* Target=Actor(A,Entity);if(!Entity||Hits.Contains(Entity)||!Target||Target->Kind!=EDWCombatSavedActorKind::Enemy)return Bad(TEXT("Invalid ring-hit enemy identity"));Hits.Add(Entity);}
    TSet<uint64> Keys;
    for(const auto& K:L.Keys)if(K.Key[0]==4&&K.Key[2]==S.RootAction)
    {
        const auto R=std::find_if(L.Receipts.begin(),L.Receipts.end(),[&](const auto& V){return V.Receipt.Handle.Id==K.Receipt;});
        if(R==L.Receipts.end()||R->Resource||!Same(R->Source,Source)||R->Receipt.State==EReceiptState::Pending)return Bad(TEXT("Unfinished ring damage is not a shield packet"));
        Keys.Add(K.Key[6]);if(R->Receipt.State==EReceiptState::Committed)Committed.Add(K.Key[6]);
    }
    for(uint64 E:Hits)if(!Keys.Contains(E))return Bad(TEXT("Ring hit has no ledger evidence"));
    for(uint64 E:Committed)if(!Hits.Contains(E))return Bad(TEXT("Committed ring hit omitted from ring memory"));
    if(S.Ring.HitFormalEnemies.Num()+S.Ring.HitLegacyEnemies.Num()!=Hits.Num())return Bad(TEXT("Ring historical target count mismatch"));
    Error.Reset();return true;
}
bool FDWCombatMinePersistence::ValidateTargetHistory(const FDWCombatPlayerExtendedSave& S,const FDWCombatWorldObjectsSave& World,FString& Error)
{
    TSet<FGuid> Formal;TSet<FName> Legacy;
    for(uint64 Entity:S.RingHitEntities)
    {
        const auto* E=World.Enemies.FindByPredicate([&](const auto& V){return V.Life.Entity==Entity;});
        if(!E){Error=TEXT("Ring hit missing world enemy/tombstone");return false;}
        if(E->Profile!=EDWEnemyCombatProfile::Legacy)
        {if(!E->Formal.StableEnemyId.IsValid()||Formal.Contains(E->Formal.StableEnemyId)){Error=TEXT("Ambiguous historical formal enemy");return false;}Formal.Add(E->Formal.StableEnemyId);}
        else {if(E->PersistentId.IsNone()||Legacy.Contains(E->PersistentId)){Error=TEXT("Missing/ambiguous legacy enemy ID");return false;}Legacy.Add(E->PersistentId);}
    }
    if(Formal.Num()!=S.Ring.HitFormalEnemies.Num()||Legacy.Num()!=S.Ring.HitLegacyEnemies.Num()){Error=TEXT("Ring/world target history count mismatch");return false;}
    for(auto Id:S.Ring.HitFormalEnemies)if(!Formal.Contains(Id)){Error=TEXT("Ring formal target differs from world history");return false;}
    for(auto Id:S.Ring.HitLegacyEnemies)if(!Legacy.Contains(Id)){Error=TEXT("Ring legacy target differs from world history");return false;}Error.Reset();return true;
}
bool FDWCombatMinePersistence::Fingerprint(UDWCombatMineSubsystem& M,ADWPlayerCharacter& Player,TArray<uint8>& Out,FString& Error)
{
    using namespace DWMinePersistenceDetail;
    if(!M.Operational()||M.bResolving||M.bDeploying||M.PendingEntries.Contains(&Player)||!IsValid(&Player)||Player.IsActorBeingDestroyed()){Error=TEXT("Mine facts unavailable during teardown/deployment/commit or pending paid entry");return false;}
    TArray<uint8> B;FMemoryWriter W(B,true);bool Updating=M.bUpdating;W<<Updating;
    auto Snapshot=Player.CaptureFormalRootSnapshot();W<<Snapshot.bActive<<Snapshot.Anchor;
    if(Snapshot.bActive){FDWMoldRootIdentity Identity;if(!Player.GetFormalRootIdentity(Identity)){Error=TEXT("Current root identity unavailable");return false;}Root(W,Identity);}
    TArray<TWeakObjectPtr<ADWPlayerCharacter>> PlayerKeys;M.Players.GetKeys(PlayerKeys);PlayerKeys.Sort([](auto L,auto R){return ObjectId(L.Get())<ObjectId(R.Get());});int32 Count=PlayerKeys.Num();W<<Count;
    for(auto Key:PlayerKeys){uint32 Id=ObjectId(Key.Get());auto S=M.Players.FindChecked(Key);W<<Id<<S.CooldownDeadline<<S.LastAttempt.Epoch<<S.LastAttempt.Id<<S.ActiveRing;}
    PlayerKeys.Reset();M.PendingEntries.GetKeys(PlayerKeys);PlayerKeys.Sort([](auto L,auto R){return ObjectId(L.Get())<ObjectId(R.Get());});Count=PlayerKeys.Num();W<<Count;
    for(auto Key:PlayerKeys){uint32 Id=ObjectId(Key.Get());W<<Id;Root(W,M.PendingEntries.FindChecked(Key));}
    TArray<FGuid> RingIds;M.Rings.GetKeys(RingIds);RingIds.Sort();Count=RingIds.Num();W<<Count;
    for(FGuid Id:RingIds)
    {
        const auto& R=M.Rings.FindChecked(Id);uint32 Owner=ObjectId(R.Player.Get());bool Active=R.bActive,Unsaveable=R.bUnsaveableHit;W<<Id<<Owner<<Active<<Unsaveable;Root(W,R.Root);Tuning(W,R.Config);
        Count=R.Mines.Num();W<<Count;for(const auto& Mine:R.Mines)
        {auto S=Mine.Save;auto E=Mine.Emitter;bool Released=Mine.bReleased;uint32 ActorId=ObjectId(Mine.Actor.Get());W<<S.MineId<<S.Ordinal<<S.Location<<S.bConsumed<<E.Epoch<<E.Id<<Released<<ActorId;
            if(auto* Actor=Mine.Actor.Get()){FTransform Transform=Actor->GetActorTransform();bool Dying=Actor->IsActorBeingDestroyed(),Hidden=Actor->IsHidden();W<<Transform<<Dying<<Hidden;}}
        Sorted(W,R.HitEntities.Array());Sorted(W,R.HitFormalEnemies.Array());auto Names=R.HitLegacyEnemies.Array();Names.Sort(FNameLexicalLess());Count=Names.Num();W<<Count;for(auto Name:Names)W<<Name;
    }
    if(W.IsError()){Error=TEXT("Cannot capture mine fingerprint");return false;}Out=MoveTemp(B);return true;
}
bool FDWCombatMineRestorePlan::CanCommit(FString& Error)const
{
    if(!State||State->bCommitted||!State->Player.IsValid()||!State->Mines.IsValid()||!State->Authority.IsValid()||UDWCombatAuthoritySubsystem::Get(State->Player.Get())!=State->Authority.Get()||State->Authority->GetWorldEpoch()!=State->OldEpoch||State->Mines->GetWorld()->GetTimeSeconds()!=State->At){Error=TEXT("Mine restore candidate stale");return false;}
    for(auto W:State->Staged)if(!W.IsValid()||W->IsActorBeingDestroyed()||!W->IsHidden()||!W->HasActorBegunPlay()||W->GetActorEnableCollision()||W->GetRingId().IsValid()||W->GetMineId().IsValid()){Error=TEXT("Staged mine unavailable");return false;}
    for(const auto& Pair:State->Rings)if(Pair.Value.Player==State->Player)for(const auto& Mine:Pair.Value.Mines)
        if(auto* Actor=Mine.Actor.Get();Actor&&!Actor->GetActorTransform().Equals(FTransform(FRotator::ZeroRotator,Mine.Save.Location),0.)){Error=TEXT("Staged mine transform changed");return false;}
    TArray<uint8> Current;if(!FDWCombatMinePersistence::Fingerprint(*State->Mines.Get(),*State->Player.Get(),Current,Error)||Current!=State->Expected){Error=TEXT("Mine facts changed while preparing");return false;}return true;
}
bool FDWCombatMinePersistence::Prepare(ADWPlayerCharacter& P,const FDWCombatPlayerExtendedSave& S,const FDWCombatAuthoritySave& Saved,
    const FDWCombatAuthorityRestorePlan& AuthorityPlan,const FDWMoldRootIdentity& RestoredRoot,FDWCombatMineRestorePlan& Out,FString& Error)
{
    using namespace DWMinePersistenceDetail;
    if(!ValidateSnapshot(S,Saved,Error))return false;auto* M=UDWCombatMineSubsystem::Get(&P);auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    if(!M||!A||AuthorityPlan.FindActor(S.PlayerEntity)!=&P||!Same(AuthorityPlan.FindLife(S.PlayerEntity),{S.PlayerEntity,S.PlayerLife})){Error=TEXT("Restored mine owner differs from world plan");return false;}
    if(S.bRooted)
    {const auto Fee=AuthorityPlan.FindReceipt(S.RootReceipt);if(!Fee||Fee->State!=DWCombatCommit::EReceiptState::Committed||RestoredRoot.WorldEpoch!=AuthorityPlan.NewEpoch()||!RestoredRoot.RootSerial||RestoredRoot.Action.Epoch!=RestoredRoot.WorldEpoch||RestoredRoot.Receipt.Epoch!=RestoredRoot.WorldEpoch||RestoredRoot.Action.Id!=S.RootAction||RestoredRoot.Receipt.Id!=S.RootReceipt||!Same(RestoredRoot.SourceLife,{S.PlayerEntity,S.PlayerLife})||RestoredRoot.Anchor!=S.RootAnchor||!Same(AuthorityPlan.FindActionOwner(S.RootAction),RestoredRoot.SourceLife)){Error=TEXT("Root plan/whitelist rebind mismatch");return false;}}
    else if(RestoredRoot.Action.Id||RestoredRoot.Action.Epoch||RestoredRoot.Receipt.Id||RestoredRoot.Receipt.Epoch||RestoredRoot.RootSerial||RestoredRoot.WorldEpoch||RestoredRoot.SourceLife.Entity||RestoredRoot.SourceLife.Life||!RestoredRoot.Anchor.IsZero()){Error=TEXT("Inactive root received live runtime identity");return false;}
    FDWCombatMineRestorePlan Plan;Plan.State.Reset(new FDWCombatMineRestoreState());auto& C=*Plan.State;
    C.Player=&P;C.Mines=M;C.Authority=A;C.At=M->GetWorld()->GetTimeSeconds();C.OldEpoch=A->GetWorldEpoch();C.NewEpoch=AuthorityPlan.NewEpoch();
    C.RestoredRoot=RestoredRoot;C.PlayerLife={S.PlayerEntity,S.PlayerLife};C.bRooted=S.bRooted;
    if(!C.NewEpoch||C.NewEpoch<=C.OldEpoch||!Fingerprint(*M,P,C.Expected,Error))return false;
    C.Players=M->Players;C.PendingEntries=M->PendingEntries;C.Rings=M->Rings;C.PendingEntries.Remove(&P);
    for(auto It=C.Rings.CreateIterator();It;++It)if(It.Value().Player.Get()==&P){for(const auto& Mine:It.Value().Mines)if(Mine.Actor.IsValid())C.Retire.AddUnique(Mine.Actor);It.RemoveCurrent();}
    UDWCombatMineSubsystem::FPlayerState State;State.CooldownDeadline=C.At+S.Ring.CooldownRemainingSeconds;
    if(!FMath::IsFinite(State.CooldownDeadline)||(S.Ring.CooldownRemainingSeconds>0.&&State.CooldownDeadline<=C.At)){Error=TEXT("Mine cooldown cannot be represented");return false;}
    State.LastAttempt=S.bRooted?RestoredRoot.Receipt:DWCombatCommit::FReceiptId{};State.ActiveRing=S.Ring.bActive?S.Ring.RingId:FGuid{};C.Players.Add(&P,State);
    if(S.Ring.bActive)
    {
        if(C.Rings.Contains(S.Ring.RingId)){Error=TEXT("Restored ring ID belongs to another player");return false;}
        UDWCombatMineSubsystem::FRing R;R.Id=S.Ring.RingId;R.Player=&P;R.Root=RestoredRoot;R.Config=Config(S.MineConfig);
        for(uint64 Entity:S.RingHitEntities)R.HitEntities.Add(Entity);for(auto Id:S.Ring.HitFormalEnemies)R.HitFormalEnemies.Add(Id);for(auto Id:S.Ring.HitLegacyEnemies)R.HitLegacyEnemies.Add(Id);
        for(const auto& V:S.Ring.Mines)
        {
            UDWCombatMineSubsystem::FMine Mine;Mine.Save=V;Mine.bReleased=true;const auto* Binding=S.MineEmitters.FindByPredicate([&](const auto& E){return E.MineId==V.MineId;});check(Binding);Mine.Emitter={C.NewEpoch,Binding->Emitter};
            if(!V.bConsumed)
            {
                if(UDWCombatAuthoritySubsystem::Get(&P)!=A||A->GetWorldEpoch()!=C.OldEpoch||!M->Operational()){Error=TEXT("World changed during mine staging");return false;}
                const FTransform Transform(FRotator::ZeroRotator,V.Location);auto* Actor=M->GetWorld()->SpawnActorDeferred<ADWCombatMine>(ADWCombatMine::StaticClass(),Transform,&P,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                if(!IsValid(Actor)){Error=TEXT("Cannot stage mine");return false;}C.Staged.Add(Actor);Mine.Actor=Actor;
                // Keep IDs empty through BeginPlay. PhysicalBegan cannot find any existing paid ring.
                if(UDWCombatAuthoritySubsystem::Get(&P)!=A||A->GetWorldEpoch()!=C.OldEpoch||!M->Operational()||Actor->IsActorBeingDestroyed()){Error=TEXT("World changed during mine creation");return false;}
                Actor->FinishSpawning(Transform);if(!IsValid(Actor)||Actor->IsActorBeingDestroyed()||!Actor->HasActorBegunPlay()){Error=TEXT("Staged mine did not begin safely");return false;}
            }
            R.Mines.Add(MoveTemp(Mine));
        }
        C.Rings.Add(R.Id,MoveTemp(R));
    }
    if(!Plan.CanCommit(Error))return false;Out=MoveTemp(Plan);return true;
}
void FDWCombatMinePersistence::AdoptNativeFacts(FDWCombatMineRestorePlan& Plan)
{
    check(Plan.State&&!Plan.State->bCommitted);auto& C=*Plan.State;auto* M=C.Mines.Get();auto* A=C.Authority.Get();
    check(M&&A&&A->GetWorldEpoch()==C.NewEpoch&&UDWCombatAuthoritySubsystem::Get(M)==nullptr);
    for(auto W:C.Retire)if(auto* Mine=W.Get()){Mine->RingId={};Mine->MineId={};}
    for(auto& Pair:C.Rings)if(Pair.Value.Player==C.Player)for(auto& Mine:Pair.Value.Mines)if(auto* Actor=Mine.Actor.Get()){Actor->RingId=Pair.Key;Actor->MineId=Mine.Save.MineId;}
    M->Players=MoveTemp(C.Players);M->PendingEntries=MoveTemp(C.PendingEntries);M->Rings=MoveTemp(C.Rings);C.bCommitted=true;
}
bool FDWCombatMinePersistence::Publish(FDWCombatMineRestorePlan& Plan,FString& Error)
{
    if(!Plan.State||!Plan.State->bCommitted||Plan.State->bPublished){Error=TEXT("Mine publish missing or already consumed");return false;}auto& C=*Plan.State;
    const auto Current=[&](){auto* P=C.Player.Get();return P&&!P->IsActorBeingDestroyed()&&C.Mines.IsValid()&&C.Authority.IsValid()&&UDWCombatAuthoritySubsystem::Get(C.Mines.Get())==C.Authority.Get()&&C.Authority->GetWorldEpoch()==C.NewEpoch&&DWMinePersistenceDetail::Same(C.Authority->FindRegisteredActorLife(P),C.PlayerLife)&&P->FormalRoot.IsActive()==C.bRooted&&(!C.bRooted||(!P->IsDead()&&DWMinePersistenceDetail::Same(P->FormalRootIdentity,C.RestoredRoot)));};
    if(!Current()){Error=TEXT("Mine publish stale world");return false;}C.bPublished=true;
    for(auto W:C.Retire){if(!Current()){Error=TEXT("World changed during old mine cleanup");return false;}if(auto* Actor=W.Get();Actor&&!Actor->IsActorBeingDestroyed())Actor->Destroy();}
    for(auto W:C.Staged){if(!Current()){Error=TEXT("World changed during mine presentation");return false;}if(auto* Actor=W.Get();Actor&&!Actor->IsActorBeingDestroyed())Actor->SetActorHiddenInGame(false);}
    Error.Reset();return Current();
}
