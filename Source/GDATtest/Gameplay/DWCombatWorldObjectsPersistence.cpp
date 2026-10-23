#include "DWCombatWorldObjectsPersistence.h"
#include "DWCombatWorldObjectsRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWCombatRecoveryActors.h"
#include "DWCombatCorePickup.h"
#include "DWCombatRules.h"
#include "DWGameInstance.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWPlayerCharacter.h"
#include "DWResourceNode.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Animation/AnimationAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "TimerManager.h"

struct FDWWorldEnemyPrepared
{
    FDWCombatEnemyObjectSave Saved;TWeakObjectPtr<ADWEnemyCharacter> Actor;
    TStrongObjectPtr<UAnimationAsset> Animation;
    FDWDaggerAwareness Dagger;FDWEnemyForcedFall Fall;
    FDWEnemyCombatConfig Config;FDWEnemyCoreDropConfig DropConfig;
};
struct FDWCombatWorldObjectsRestoreState
{
    TWeakObjectPtr<UWorld> World;TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority;
    TWeakObjectPtr<UDWCombatCoreDropSubsystem> Drops;TWeakObjectPtr<UDWCombatRecoverySubsystem> Recovery;
    TWeakObjectPtr<UDWCombatWorldObjectsRegistry> Registry;
    FDWCombatWorldObjectsSave Saved;TArray<FDWPersistedActorState> Legacy,BeforeLegacy;
    TArray<uint8> BeforeBytes;TArray<FDWCombatObjectBinding> Live,Restored;
    TArray<uint64> Keep;TArray<FDWWorldEnemyPrepared> Enemies;
    TArray<TWeakObjectPtr<AActor>> Staged,Retire;
    TArray<TWeakObjectPtr<AController>> StagedControllers;
    TSet<TWeakObjectPtr<AActor>> Ignored;
    double At=0.;uint64 Epoch=0,NewEpoch=0;DWCombatCommit::FEntityLife PlayerLife;
    FDWEnemyCoreDropConfig DropConfig;
    bool bPrepared=false,bAdopted=false,bPublished=false;
};
namespace
{
bool Same(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B){return A.Entity==B.Entity&&A.Life==B.Life;}
AActor* Find(const TArray<FDWCombatObjectBinding>& B,const FString& Id)
{for(const auto& V:B)if(V.StableId.Equals(Id,ESearchCase::CaseSensitive))return V.Actor.Get();return nullptr;}
bool ActorCurrent(const AActor* A,const UWorld* W){return IsValid(A)&&!A->IsActorBeingDestroyed()&&A->GetWorld()==W;}
bool LegacyEqual(const TArray<FDWPersistedActorState>& A,const TArray<FDWPersistedActorState>& B)
{
    if(A.Num()!=B.Num())return false;for(int32 I=0;I<A.Num();++I)
    {const auto& X=A[I];const auto& Y=B[I];if(!X.ActorId.Equals(Y.ActorId,ESearchCase::CaseSensitive)||!X.ActorPath.Equals(Y.ActorPath,ESearchCase::CaseSensitive)||X.Health!=Y.Health||X.SpawnProgress!=Y.SpawnProgress||X.bDestroyed!=Y.bDestroyed||!X.Transform.Equals(Y.Transform,0.))return false;}return true;
}
}
void FDWCombatWorldObjectsRestoreStateDeleter::operator()(FDWCombatWorldObjectsRestoreState* P)const{delete P;}
FDWCombatWorldObjectsRestorePlan::FDWCombatWorldObjectsRestorePlan()=default;
FDWCombatWorldObjectsRestorePlan::~FDWCombatWorldObjectsRestorePlan(){DiscardStagedObjects();}
FDWCombatWorldObjectsRestorePlan::FDWCombatWorldObjectsRestorePlan(FDWCombatWorldObjectsRestorePlan&&)=default;
FDWCombatWorldObjectsRestorePlan& FDWCombatWorldObjectsRestorePlan::operator=(FDWCombatWorldObjectsRestorePlan&& Other)
{if(this!=&Other){DiscardStagedObjects();State=MoveTemp(Other.State);}return *this;}
const TArray<FDWCombatObjectBinding>& FDWCombatWorldObjectsRestorePlan::LiveBindings()const{static const TArray<FDWCombatObjectBinding> Empty;return State?State->Live:Empty;}
const TArray<FDWCombatObjectBinding>& FDWCombatWorldObjectsRestorePlan::RestoredBindings()const{static const TArray<FDWCombatObjectBinding> Empty;return State?State->Restored:Empty;}
const TArray<uint64>& FDWCombatWorldObjectsRestorePlan::KeepActionIds()const{static const TArray<uint64> Empty;return State?State->Keep:Empty;}
void FDWCombatWorldObjectsRestorePlan::DiscardStagedObjects()
{
    if(!State||State->bAdopted)return;auto Staged=MoveTemp(State->Staged);State->Ignored.Reset();
    for(auto Weak:Staged)if(auto* Actor=Weak.Get();ActorCurrent(Actor,State->World.Get()))Actor->Destroy();
    auto Controllers=MoveTemp(State->StagedControllers);for(auto Weak:Controllers)if(auto* Actor=Weak.Get();ActorCurrent(Actor,State->World.Get()))Actor->Destroy();
}
bool FDWCombatWorldObjectsRestorePlan::IsStagingIntact(FString& Error)const
{
    if(!State||State->bAdopted||!State->World.IsValid()||State->World->bIsTearingDown||!ActorCurrent(State->Player.Get(),State->World.Get())||
        !State->Authority.IsValid()||State->Authority->GetWorldEpoch()!=State->Epoch||!Same(State->Authority->CaptureActorLife(State->Player.Get()),State->PlayerLife)||State->World->GetTimeSeconds()!=State->At)
    {Error=TEXT("World staging lifecycle/time changed");return false;}
    for(auto Weak:State->Staged)
    {auto* E=Cast<ADWEnemyCharacter>(Weak.Get());if(!ActorCurrent(E,State->World.Get())||!E->bCombatWorldStaged||!E->IsHidden()||E->GetActorEnableCollision()||E->IsActorTickEnabled())
        {Error=TEXT("Prepared enemy no longer inert/current");return false;}}
    for(const auto& V:State->Restored)if(!ActorCurrent(V.Actor.Get(),State->World.Get())){Error=TEXT("Prepared object binding ended");return false;}
    Error.Reset();return true;
}
bool FDWCombatWorldObjectsRestorePlan::CanCommit(FString& Error)const
{
    if(!IsStagingIntact(Error)||!State->bPrepared){if(Error.IsEmpty())Error=TEXT("World facts are not prepared");return false;}
    FDWCombatWorldObjectsSave Live;TArray<FDWPersistedActorState> Legacy;TArray<FDWCombatObjectBinding>B;TArray<uint8>Bytes;
    if(!FDWCombatWorldObjectsPersistence::CaptureInternal(*State->World.Get(),*State->Player.Get(),Live,Legacy,B,Error,State->Ignored)||
        !DWCombatWorldObjectsSave::Encode(Live,Bytes,Error)||Bytes!=State->BeforeBytes||!LegacyEqual(Legacy,State->BeforeLegacy))
    {Error=TEXT("Live world facts changed while preparing restore; candidate rejected");return false;}
    return true;
}
bool FDWCombatWorldObjectsPersistence::PrepareWorldObjects(UWorld& W,ADWPlayerCharacter& P,const FDWCombatWorldObjectsSave& S,
    const TArray<FDWPersistedActorState>& L,FDWCombatWorldObjectsRestorePlan& Out,FString& Error)
{
    if(!DWCombatWorldObjectsSave::Validate(S,Error)||!DWCombatWorldObjectsSave::ValidateLegacy(L,Error))return false;
    if(!S.MapPackage.Equals(UDWCombatRecoverySubsystem::MapPackage(&W),ESearchCase::CaseSensitive)){Error=TEXT("World object map mismatch");return false;}
    auto* GI=P.GetGameInstance<UDWGameInstance>();const auto Rules=GI?GI->GetCombatRules():nullptr;
    if(!Rules){Error=TEXT("Missing formal rules");return false;}
    FDWCombatWorldObjectsRestorePlan Plan;Plan.State.Reset(new FDWCombatWorldObjectsRestoreState);auto& C=*Plan.State;
    C.World=&W;C.Player=&P;C.Authority=UDWCombatAuthoritySubsystem::Get(&W);C.Drops=UDWCombatCoreDropSubsystem::Get(&W);C.Recovery=UDWCombatRecoverySubsystem::Get(&W);C.Registry=W.GetSubsystem<UDWCombatWorldObjectsRegistry>();C.At=W.GetTimeSeconds();
    FDWCombatWorldObjectsSave Before;if(!Capture(W,P,Before,C.BeforeLegacy,C.Live,Error)||!DWCombatWorldObjectsSave::Encode(Before,C.BeforeBytes,Error))return false;
    C.Epoch=C.Authority->GetWorldEpoch();C.PlayerLife=C.Authority->CaptureActorLife(&P);C.Saved=S;C.Legacy=L;
    if(!DWEnemyFormal::BuildCoreDropConfig(*Rules,C.DropConfig,Error))return false;
    C.Restored.Add({TEXT("player:0"),&P});if(P.GetController())C.Restored.Add({TEXT("controller:0"),P.GetController()});
    for(const auto& V:L)
    {
        auto* Actor=Find(C.Live,V.ActorId);if(!ActorCurrent(Actor,&W)||!UWorld::RemovePIEPrefix(Actor->GetPathName()).Equals(V.ActorPath,ESearchCase::CaseSensitive))
        {Error=TEXT("Saved resource/nest is missing or has an ambiguous scene path");return false;}
        if(auto* N=Cast<ADWEnemyNest>(Actor))
        {if(V.Health>N->MaxHealth){Error=TEXT("Saved nest health exceeds authored max");return false;}const auto* Clock=S.NestClocks.FindByPredicate([&](const auto& X){return X.StableId.Equals(V.ActorId,ESearchCase::CaseSensitive);});
            if(!Clock||V.SpawnProgress>Clock->SampledInterval){Error=TEXT("Nest sampled clock mismatch");return false;}}
        else if(!Cast<ADWResourceNode>(Actor)){Error=TEXT("Saved basic world actor type mismatch");return false;}
        C.Restored.Add({V.ActorId,Actor});
    }
    if(L.Num()!=C.BeforeLegacy.Num()){Error=TEXT("Resource/nest scene population changed");return false;}
    TSet<TWeakObjectPtr<ADWEnemyCharacter>> Used;
    for(const auto& V:S.Enemies)
    {
        ADWEnemyCharacter* E=Cast<ADWEnemyCharacter>(Find(C.Live,V.StableId));
        if(!E&&!V.bRuntimeSpawned)
            for(TActorIterator<ADWEnemyCharacter> It(&W);It;++It)if(!It->IsActorBeingDestroyed()&&!It->SourceNest&&UWorld::RemovePIEPrefix(It->GetPathName()).Equals(V.ActorPath,ESearchCase::CaseSensitive))
            {if(E){Error=TEXT("Ambiguous placed enemy path");return false;}E=*It;}
        if(V.bEnded){if(E){Used.Add(E);C.Retire.Add(E);}continue;}
        if(E&&Used.Contains(E)){Error=TEXT("Two snapshots resolved to one enemy");return false;}
        if(E&&(!E->GetClass()->GetPathName().Equals(V.ClassPath,ESearchCase::CaseSensitive)||E->CombatProfile!=V.Profile))
        {Error=TEXT("Existing enemy class/profile differs from snapshot");return false;}
        ADWEnemyNest* Nest=nullptr;
        if(!V.SourceNestId.IsEmpty()){Nest=Cast<ADWEnemyNest>(Find(C.Restored,V.SourceNestId));if(!Nest){Error=TEXT("Missing saved spawn owner");return false;}}
        if(!E)
        {
            UClass* Class=FSoftClassPath(V.ClassPath).TryLoadClass<ADWEnemyCharacter>();
            if(!Class||Class->HasAnyClassFlags(CLASS_Abstract)||!Plan.IsStagingIntact(Error))return false;
            E=W.SpawnActorDeferred<ADWEnemyCharacter>(Class,V.Transform,Nest,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            if(!E){Error=TEXT("Could not stage saved enemy");return false;}C.Staged.Add(E);C.Ignored.Add(E);
            E->bCombatWorldStaged=true;E->CombatProfile=V.Profile;E->SourceNest=Nest;E->SetActorHiddenInGame(true);E->SetActorEnableCollision(false);E->SetActorTickEnabled(false);
            if(!ActorCurrent(E,&W)||!Plan.IsStagingIntact(Error))return false;
            E->FinishSpawning(V.Transform);
            if(!ActorCurrent(E,&W)){Error=TEXT("Staged enemy ended during construction");return false;}
            if(!E->HasActorBegunPlay())E->DispatchBeginPlay();
            if(!ActorCurrent(E,&W)){Error=TEXT("Staged enemy ended during BeginPlay");return false;}
            if(E->GetController())C.StagedControllers.Add(E->GetController());
            E->SetActorHiddenInGame(true);E->SetActorEnableCollision(false);E->SetActorTickEnabled(false);
            if(!Plan.IsStagingIntact(Error))return false;
        }
        Used.Add(E);FDWWorldEnemyPrepared Row;Row.Saved=V;Row.Actor=E;
        if(V.Profile!=EDWEnemyCombatProfile::Legacy)
        {if(!E->IsFormalCombatReady()||!DWEnemyFormal::BuildConfig(*Rules,V.Profile,Row.Config,Error)||!DWEnemyFormal::BuildCoreDropConfig(*Rules,Row.DropConfig,Error)||V.Health>static_cast<float>(Row.Config.Health)||V.MaxHealth!=static_cast<float>(Row.Config.Health)||
            V.Formal.DisengageElapsedSeconds>Row.Config.DisengageSeconds||V.Formal.AlertRemainingSeconds>Row.Config.AlertDelaySeconds||V.AttackCooldown>Row.Config.AttackCycleSeconds())
            {Error=TEXT("Formal enemy snapshot exceeds current formal rule contract");return false;}
            const auto& T=Row.Config;const double PhaseDuration=V.Phase==1?T.WindupSeconds:V.Phase==2?T.ActiveSeconds:V.Phase==3?T.RecoverySeconds:0.;
            if(V.PhaseRemaining>PhaseDuration||(V.bHasAttackSnapshot&&(V.CurrentWindup!=static_cast<float>(T.WindupSeconds)||V.CurrentActive!=static_cast<float>(T.ActiveSeconds)||V.CurrentRecovery!=static_cast<float>(T.RecoverySeconds)||V.CurrentRange!=static_cast<float>(T.RangeCm)||V.CurrentDamage!=static_cast<float>(T.Damage))))
            {Error=TEXT("Saved formal attack snapshot differs from rule timing/damage");return false;}}
        if(!V.AttackAnimationPath.IsEmpty())
        {Row.Animation.Reset(LoadObject<UAnimationAsset>(nullptr,*V.AttackAnimationPath));if(!Row.Animation.IsValid()||!Plan.IsStagingIntact(Error)){Error=TEXT("Missing saved attack visual");return false;}}
        C.Enemies.Add(MoveTemp(Row));C.Restored.Add({V.StableId,E});if(E->GetController())C.Restored.Add({TEXT("controller:")+V.StableId,E->GetController()});
        if(V.MeleeAction.Id)C.Keep.AddUnique(V.MeleeAction.Id);
    }
    for(TActorIterator<ADWEnemyCharacter> It(&W);It;++It)if(!It->IsActorBeingDestroyed()&&!It->bCombatWorldStaged&&!Used.Contains(*It))
    {if(It->SourceNest)C.Retire.Add(*It);else{Error=TEXT("Unrepresented placed enemy; refusing to erase authored scene facts");return false;}}
    if(!Plan.IsStagingIntact(Error))return false;
    // FinishSpawning callbacks must not mutate the already-playing scene.
    FDWCombatWorldObjectsSave After;TArray<FDWPersistedActorState> AL;TArray<FDWCombatObjectBinding> AB;TArray<uint8>Bytes;
    if(!CaptureInternal(W,P,After,AL,AB,Error,C.Ignored)||!DWCombatWorldObjectsSave::Encode(After,Bytes,Error)||Bytes!=C.BeforeBytes||!LegacyEqual(AL,C.BeforeLegacy))
    {Error=TEXT("Enemy construction changed the live scene; restore rejected");return false;}
    // Include staged registrations in the live identity table only; they are not live gameplay facts.
    for(auto Weak:C.Staged)if(auto* E=Cast<ADWEnemyCharacter>(Weak.Get()))
    {C.Live.Add({TEXT("staging:")+FGuid::NewGuid().ToString(EGuidFormats::Digits),E});if(E->GetController())C.Live.Add({TEXT("staging-controller:")+FGuid::NewGuid().ToString(EGuidFormats::Digits),E->GetController()});}
    Out=MoveTemp(Plan);return true;
}
bool FDWCombatWorldObjectsPersistence::PrepareWorldFacts(FDWCombatWorldObjectsRestorePlan& Plan,const FDWCombatAuthorityRestorePlan& Authority,FString& Error)
{
    if(!Plan.IsStagingIntact(Error)||!Authority.NewEpoch())return false;auto& C=*Plan.State;
    const auto Bad=[&](const TCHAR* M){Error=M;return false;};
    if(!C.Drops.IsValid()||C.Drops->Revision==MAX_uint64||!C.Registry.IsValid()||C.Registry->Revision==MAX_uint64)return Bad(TEXT("World revision exhausted"));
    if(Authority.FindActor(TEXT("player:0"))!=C.Player.Get())return Bad(TEXT("Player world/authority binding mismatch"));
    const auto Active=Authority.ActiveActions();
    for(const auto& S:C.Saved.Enemies)
    {
        const auto Life=Authority.FindLife(S.Life.Entity);
        if(S.bEnded){if(Life.Life<S.Life.Life||Life.Entity!=S.Life.Entity||Authority.FindActor(S.Life.Entity))return Bad(TEXT("Ended enemy identity is not an authority tombstone"));}
        else if(!Same(Life,S.Life)||Authority.FindActor(S.Life.Entity)!=Find(C.Restored,S.StableId))return Bad(TEXT("Enemy life/binding mismatch"));
    }
    for(auto& Row:C.Enemies)
    {
        auto& S=Row.Saved;auto* E=Row.Actor.Get();if(!ActorCurrent(E,C.World.Get()))return Bad(TEXT("Enemy ended during fact preparation"));
        for(double V:{S.PhaseRemaining,S.AttackCooldown,S.AlertRemaining,S.PatrolRemaining,S.PathRemaining,S.KnockbackSeconds,S.Dagger.GraceRemainingSeconds})
            if(!FMath::IsFinite(C.At+V)||C.At+V>MAX_flt)return Bad(TEXT("Enemy clock cannot be represented in destination world"));
        if(!FMath::IsFinite(C.At-S.AttackAge)||C.At-S.AttackAge<-MAX_flt)return Bad(TEXT("Enemy animation clock cannot be represented"));
        if(!S.TargetId.IsEmpty()&&!Cast<APawn>(Find(C.Restored,S.TargetId)))return Bad(TEXT("Invalid enemy chase target"));
        if(!S.MeleeTargetId.IsEmpty())
        {auto* Target=Find(C.Restored,S.MeleeTargetId);if(!Target||Authority.FindActor(S.MeleeTargetLife.Entity)!=Target||!Same(Authority.FindLife(S.MeleeTargetLife.Entity),S.MeleeTargetLife))return Bad(TEXT("Enemy attack target life mismatch"));}
        if(S.Profile!=EDWEnemyCombatProfile::Legacy&&S.Phase&&(S.Phase<3||S.MeleeAction.Id)&&(!S.MeleeAction.Id||S.MeleeTargetId.IsEmpty()))return Bad(TEXT("Incomplete formal enemy attack identity"));
        if(S.MeleeAction.Id)
        {if(!Active.ContainsByPredicate([&](const auto& X){return X.Id==S.MeleeAction.Id;})||!Same(Authority.FindActionOwner(S.MeleeAction.Id),S.Life))return Bad(TEXT("Enemy action is not owned by its saved life"));S.MeleeAction.Epoch=Authority.NewEpoch();}
        for(auto Life:{S.DaggerPlayerLife,S.ObservedPlayerLife})if(Life.Entity)
        {const auto Present=Authority.FindLife(Life.Entity);if(Present.Entity!=Life.Entity||Present.Life<Life.Life)return Bad(TEXT("Enemy remembers an unknown player life"));}
        if(!Row.Dagger.Restore(S.Dagger,C.At))return Bad(TEXT("Invalid dagger awareness clock"));
        if(S.Profile!=EDWEnemyCombatProfile::Legacy)
        {
            auto Fall=S.ForcedFall;
            if(Fall.Stage!=EDWForcedFallStage::None)
            {const auto Receipt=Authority.FindReceipt(Fall.ReceiptId);if(!Receipt||Receipt->State!=DWCombatCommit::EReceiptState::Committed||Authority.FindLife(Fall.PlayerEntity).Life<Fall.PlayerLife||Fall.OriginalEnemyEntity!=S.Life.Entity||Fall.OriginalEnemyLife>S.Life.Life)return Bad(TEXT("Invalid historical forced fall cause"));}
            if(!Row.Fall.Restore(Fall,S.Formal.StableEnemyId,Authority.NewEpoch(),S.Life,FMath::Max<uint64>(1,S.KnockbackRevision),S.bKnockedBack))return Bad(TEXT("Forced fall cannot restore without its driver"));
        }
    }
    for(const auto& D:C.Saved.CoreDrops)
    {
        const auto Receipt=Authority.FindReceipt(D.FatalReceipt);const auto Life=Authority.FindLife(D.EnemyEntity);
        if(!Receipt||Receipt->State!=DWCombatCommit::EReceiptState::Committed||Life.Entity!=D.EnemyEntity||Life.Life<D.EnemyLife||
            (D.PlayerEntity&&Authority.FindLife(D.PlayerEntity).Life<D.PlayerLife))return Bad(TEXT("Core drop has no committed historical fatal identity"));
        if(D.ForcedFallReceiptId){const auto Cause=Authority.FindReceipt(D.ForcedFallReceiptId);if(!Cause||Cause->State!=DWCombatCommit::EReceiptState::Committed)return Bad(TEXT("Core forced-fall cause is not committed"));}
    }
    // An equipped dagger's saved ring state must be checked against target PlayerExtended by root.
    C.NewEpoch=Authority.NewEpoch();C.bPrepared=true;
    return Plan.CanCommit(Error);
}
void FDWCombatWorldObjectsPersistence::AdoptNativeFacts(FDWCombatWorldObjectsRestorePlan& Plan)
{
    check(Plan.State&&Plan.State->bPrepared&&!Plan.State->bAdopted);auto& C=*Plan.State;
    check(C.Authority.IsValid()&&C.Authority->GetWorldEpoch()==C.NewEpoch);
    auto* D=C.Drops.Get();auto* R=C.Recovery.Get();auto* Registry=C.Registry.Get();check(D&&R&&Registry);
    // Old physical items become non-interactive before any publication. Their destruction is deferred.
    for(auto& Pair:D->Records)if(auto* P=Pair.Value.Physical.Get()){P->bBound=false;C.Retire.Add(P);}
    for(auto& Pair:R->Physical)if(auto* P=Pair.Value.Get()){P->bBound=false;C.Retire.Add(P);}
    D->Records.Reset();++D->Revision;
    for(auto Saved:C.Saved.CoreDrops)
    {
        Saved.FatalEpoch=C.NewEpoch; // Receipt numeric IDs are preserved in the candidate ledger.
        UDWCombatCoreDropSubsystem::FRecord Record;Record.Save=Saved;Record.PickupRadiusCm=C.DropConfig.PickupRadiusCm;
        Record.bRestored=true;Record.bMayMaterialize=Saved.Decision==EDWEnemyCoreDropDecision::SpawnPending||Saved.Decision==EDWEnemyCoreDropDecision::Spawned;
        for(const auto& Row:C.Enemies)if(Row.Saved.Formal.StableEnemyId==Saved.StableEnemyId){Record.Origin=Row.Actor;break;}
        D->Records.Add(Saved.DeathId,MoveTemp(Record));
    }
    R->State=C.Saved.Recovery;R->BoundPlayer=C.Player;R->Physical.Reset();R->bInitialized=true;R->bRestoredCold=false;R->bPlacementPending=false;
    Registry->Tombstones.Reset();Registry->bCaptureFailure=false;++Registry->Revision;
    for(auto Saved:C.Saved.Enemies)if(Saved.bEnded)
    {if(Saved.Formal.FatalReceiptId)Saved.Formal.FatalReceiptEpoch=C.NewEpoch;Registry->Tombstones.Add(Saved.StableId,MoveTemp(Saved));}
    for(const auto& V:C.Legacy)
    {
        if(auto* N=Cast<ADWEnemyNest>(Find(C.Restored,V.ActorId)))
        {N->Health=V.Health;N->bHealthRestoredFromSave=true;N->SpawnProgress=V.SpawnProgress;N->bPlayerInRange=false;N->bCombatDeathEffectsPending=false;N->bCombatDeathRangeNotice=false;N->SpawnedEnemies.Reset();
            for(const auto& Clock:C.Saved.NestClocks)if(Clock.StableId.Equals(V.ActorId,ESearchCase::CaseSensitive)){N->CurrentSpawnInterval=Clock.SampledInterval;break;}}
        else if(auto* Resource=Cast<ADWResourceNode>(Find(C.Restored,V.ActorId)))Resource->RemainingAmount=static_cast<int32>(V.Health);
    }
    for(auto& Row:C.Enemies)
    {
        auto* E=Row.Actor.Get();check(E);const auto& S=Row.Saved;
        E->bCombatWorldStaged=true;E->Health=S.Health;E->MaxHealth=S.MaxHealth;E->State=static_cast<EDWEnemyState>(S.State);E->AttackPhase=static_cast<EDWCombatPhase>(S.Phase);E->AttackSerial=S.AttackSerial;
        E->CombatProfile=S.Profile;E->PersistentId=S.PersistentId;E->CombatWorldStableId=S.StableId;E->bHealthRestoredFromSave=true;
        E->bCombatDeathEffectsPending=false;E->bCombatRestoreInProgress=false;E->bStartingMeleeAttack=false;E->bDamageInProgress=false;E->bDeathTransitionInProgress=false;
        E->SourceNest=Cast<ADWEnemyNest>(Find(C.Restored,S.SourceNestId));if(E->SourceNest)E->SourceNest->SpawnedEnemies.Add(E);
        E->HomeLocation=S.Home;E->PatrolGoal=S.PatrolGoal;E->bHasPatrolGoal=S.bHasPatrolGoal;
        E->TargetPawn=Cast<APawn>(Find(C.Restored,S.TargetId));E->MeleeTarget=Find(C.Restored,S.MeleeTargetId);
        E->LockedAttackDirection=S.LockedDirection;E->bHasAttackSnapshot=S.bHasAttackSnapshot;E->bAttackResolved=S.bAttackResolved;
        E->CurrentAttackVariantIndex=S.VariantIndex;E->CurrentAttackAnimation=Row.Animation.Get();E->CurrentAttackWindup=S.CurrentWindup;E->CurrentAttackActiveDuration=S.CurrentActive;E->CurrentAttackRecovery=S.CurrentRecovery;E->CurrentAttackRange=S.CurrentRange;E->CurrentAttackDamage=S.CurrentDamage;
        E->AttackStartedTime=C.At-S.AttackAge;E->AttackPhaseEndTime=C.At+S.PhaseRemaining;E->NextAttackTime=C.At+S.AttackCooldown;E->AlertEndTime=C.At+S.AlertRemaining;E->NextPatrolTime=C.At+S.PatrolRemaining;E->NextPathRequestTime=C.At+S.PathRemaining;
        E->bIsCombatKnockedBack=S.bKnockedBack;E->KnockbackDirection=S.KnockbackDirection;E->KnockbackDistanceRemaining=S.KnockbackDistance;E->KnockbackTimeRemaining=S.KnockbackSeconds;E->KnockbackSpeed=S.KnockbackSpeed;
        E->FormalKnockbackRevision=S.KnockbackRevision;E->FormalKnockbackFrame=MAX_uint64;E->FormalKnockbackInitialDistance=S.KnockbackInitialDistance;E->FormalKnockbackCommittedTravelThisFrame=0.;
        E->bFormalAttackContinuesDuringKnockback=S.bAttackContinuesDuringKnockback;E->bFormalFacingLocked=S.bFacingLocked;E->bFormalNeedsChaseState=S.bNeedsChaseState;E->bPlayerDeathReturn=S.bPlayerDeathReturn;
        E->FormalMeleeAction=S.MeleeAction;E->FormalMeleeTargetLife=S.MeleeTargetLife;E->DaggerAwareness=Row.Dagger;E->DaggerPlayerLife=S.DaggerPlayerLife;E->PendingDaggerAlertTarget.Reset();
        E->FormalObservedPlayerLife=S.ObservedPlayerLife;E->FormalObservedPlayerDamageRevision=S.ObservedPlayerDamageRevision;E->ForcedFall=Row.Fall;
        E->bHasPreDeathState=S.bHasPreDeathState;E->bPreDeathCollision=S.bPreDeathCollision;E->bPreDeathHidden=S.bPreDeathHidden;E->bPreDeathTick=S.bPreDeathTick;E->bPreDeathMeshTick=S.bPreDeathMeshTick;E->PreDeathMovementMode=S.PreDeathMovementMode;E->PreDeathCustomMovementMode=S.PreDeathCustomMovementMode;
        E->SlowEffects.Reset();for(auto Slow:S.LegacySlows)E->SlowEffects.Add(FVector2D(Slow.X,C.At+Slow.Y));
        if(S.Profile!=EDWEnemyCombatProfile::Legacy)
        {
            const auto& F=S.Formal;E->FormalCombatConfig=Row.Config;E->FormalCoreDropConfig=Row.DropConfig;E->bFormalCombatReady=true;E->FormalEnemyId=F.StableEnemyId;E->bFormalHomeCaptured=true;
            E->FormalMemory.bEngaged=F.bEngaged;E->FormalMemory.bReturningHome=F.bReturningHome;E->FormalMemory.bDisengageCandidate=F.bDisengageCandidate;E->FormalMemory.DisengageElapsedSeconds=F.DisengageElapsedSeconds;
            E->FormalMemory.LastObservationTime=S.bHasObservation?C.At-S.ObservationAge:-1.;E->FormalAlertEndTime=C.At+S.AlertRemaining;E->FormalNextAttackTime=C.At+S.AttackCooldown;E->FormalAttackStartedTime=C.At-S.AttackAge;E->FormalPhaseEndTime=C.At+S.PhaseRemaining;
            E->FormalCoreDrop.DeathId=F.CoreDropDeathId;E->FormalCoreDrop.Decision=F.DropDecision;E->FormalCoreDrop.FatalReceipt=F.FatalReceiptId?DWCombatCommit::FReceiptId{C.NewEpoch,F.FatalReceiptId}:DWCombatCommit::FReceiptId{};
            E->FormalCoreDrop.EnemyLife={F.DeathEntityId,F.DeathLife};E->FormalCoreDrop.PlayerLife={F.CreditedPlayerEntityId,F.CreditedPlayerLife};E->FormalCoreDrop.PickupId=F.PickupId;E->FormalCoreDrop.CoreItemId=F.CoreItemId;E->FormalCoreDrop.Count=F.CoreCount;
            E->DetectionRadius=Row.Config.AlertRadiusCm;E->LoseTargetRadius=Row.Config.DisengageDistanceCm;E->MoveSpeed=Row.Config.MoveSpeedCmPerSecond;E->AttackRange=Row.Config.RangeCm;E->AttackDamage=Row.Config.Damage;
        }
    }
    // Suppress EndPlay tombstones for actors excluded by this saved scene, and keep staged actors inert until pose publish.
    for(auto Weak:C.Retire)if(auto* E=Cast<ADWEnemyCharacter>(Weak.Get()))E->bCombatWorldStaged=true;
    C.bAdopted=true;
}
bool FDWCombatWorldObjectsPersistence::Publish(FDWCombatWorldObjectsRestorePlan& Plan,FString& Error)
{
    if(!Plan.State||!Plan.State->bAdopted||Plan.State->bPublished){Error=TEXT("World publication unavailable/already completed");return false;}auto& C=*Plan.State;
    const auto Current=[&](){return C.World.IsValid()&&!C.World->bIsTearingDown&&ActorCurrent(C.Player.Get(),C.World.Get())&&C.Authority.IsValid()&&C.Authority->GetWorldEpoch()==C.NewEpoch;};
    const auto Fail=[&](){Error=TEXT("World lifecycle changed during restored scene publication");return false;};
    if(!Current())return Fail();
    for(auto Weak:C.Retire){if(auto* A=Weak.Get();ActorCurrent(A,C.World.Get()))A->Destroy();if(!Current())return Fail();}
    for(auto& Row:C.Enemies)
    {
        auto* E=Row.Actor.Get();const auto& S=Row.Saved;const auto Here=[&](){return Current()&&ActorCurrent(E,C.World.Get())&&Same(C.Authority->FindRegisteredActorLife(E),S.Life);};
        if(!Here())return Fail();E->StopAIMovement();if(!Here())return Fail();
        E->GetWorldTimerManager().ClearTimer(E->DeathCleanupTimer);E->SetLifeSpan(0.f);E->SetActorEnableCollision(false);if(!Here())return Fail();
        if(!E->SetActorTransform(S.Transform,false,nullptr,ETeleportType::TeleportPhysics)||!Here())return Fail();
        E->GetCharacterMovement()->SetMovementMode(static_cast<EMovementMode>(S.MovementMode),S.CustomMovementMode);if(!Here())return Fail();
        E->GetCharacterMovement()->Velocity=S.Velocity;
        if(E->DetectionRange)E->DetectionRange->SetSphereRadius(E->DetectionRadius,false);if(!Here())return Fail();
        E->SetActorHiddenInGame(S.bHidden);if(!Here())return Fail();E->SetActorEnableCollision(S.bCollision);if(!Here())return Fail();E->SetActorTickEnabled(S.bActorTick);if(!Here())return Fail();
        if(E->GetMesh())E->GetMesh()->SetComponentTickEnabled(S.bMeshTick);if(!Here())return Fail();
        E->bCombatWorldStaged=false;E->RefreshCombatGroundAreaSlow();if(!Here())return Fail();E->UpdatePlaceholderPresentation();if(!Here())return Fail();
        if(S.bDeathCleanupScheduled)
        {if(S.DeathCleanupRemaining>0.)E->GetWorldTimerManager().SetTimer(E->DeathCleanupTimer,E,&ADWEnemyCharacter::FinishPersistentDeath,static_cast<float>(S.DeathCleanupRemaining),false);else E->FinishPersistentDeath();if(!Here())return Fail();}
        if(S.LifeSpanRemaining>0.)E->SetLifeSpan(static_cast<float>(S.LifeSpanRemaining));if(!Here())return Fail();
    }
    for(const auto& V:C.Legacy)
    {
        auto* Actor=Find(C.Restored,V.ActorId);if(!Current()||!ActorCurrent(Actor,C.World.Get()))return Fail();
        if(!Actor->SetActorTransform(V.Transform,false,nullptr,ETeleportType::TeleportPhysics)||!Current()||!ActorCurrent(Actor,C.World.Get()))return Fail();
        if(auto* N=Cast<ADWEnemyNest>(Actor))
        {N->SetActorHiddenInGame(V.bDestroyed);if(!Current()||!ActorCurrent(N,C.World.Get()))return Fail();N->SetActorEnableCollision(!V.bDestroyed);if(!Current()||!ActorCurrent(N,C.World.Get()))return Fail();N->SetActorTickEnabled(!V.bDestroyed);if(!Current()||!ActorCurrent(N,C.World.Get()))return Fail();if(!V.bDestroyed)N->UpdateAmbientPresentation();}
        else if(auto* R=Cast<ADWResourceNode>(Actor))R->RefreshAvailability();
        if(!Current())return Fail();
    }
    // Physical drop/bag creation is publication, after every fact already exists. Each subsystem rechecks its own records after spawn callbacks.
    TArray<FGuid> DropIds;C.Drops->Records.GetKeys(DropIds);for(auto Id:DropIds){C.Drops->Materialize(Id);if(!Current())return Fail();}
    for(const auto& Bag:C.Saved.Recovery.Bags){if(!Bag.bClaimed)C.Recovery->Materialize(Bag.BagId);if(!Current())return Fail();}
    C.bPublished=true;C.Staged.Reset();C.StagedControllers.Reset();Error.Reset();return true;
}
