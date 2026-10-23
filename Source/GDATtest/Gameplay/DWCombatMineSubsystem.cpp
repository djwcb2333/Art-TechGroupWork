#include "DWCombatMineSubsystem.h"
#include "DWCombatMine.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWGameInstance.h"
#include "DWDamageVolumeLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace DWMineRuntime
{
bool Same(const FDWMoldRootIdentity& A,const FDWMoldRootIdentity& B)
{return A.WorldEpoch==B.WorldEpoch&&A.RootSerial==B.RootSerial&&A.Receipt.Id==B.Receipt.Id&&A.Receipt.Epoch==B.Receipt.Epoch&&
 A.Action.Id==B.Action.Id&&A.Action.Epoch==B.Action.Epoch&&A.SourceLife.Entity==B.SourceLife.Entity&&A.SourceLife.Life==B.SourceLife.Life&&A.Anchor==B.Anchor;}
FCollisionQueryParams Environment(UWorld* World,const AActor* Ignore)
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(DWMoldMineGround),false,Ignore);
    for(TActorIterator<APawn> It(World);It;++It)Q.AddIgnoredActor(*It);
    return Q;
}
}
bool UDWCombatMineSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{return Type==EWorldType::Game||Type==EWorldType::PIE;}
bool UDWCombatMineSubsystem::Operational() const
{return IsInGameThread()&&!bStopping&&GetWorld()&&!GetWorld()->bIsTearingDown&&GetWorld()->GetNetMode()!=NM_Client;}
UDWCombatMineSubsystem* UDWCombatMineSubsystem::Get(const UObject* Context)
{auto* W=Context?Context->GetWorld():nullptr;auto* M=W?W->GetSubsystem<UDWCombatMineSubsystem>():nullptr;return M&&M->Operational()?M:nullptr;}
TStatId UDWCombatMineSubsystem::GetStatId() const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UDWCombatMineSubsystem,STATGROUP_Tickables);}
void UDWCombatMineSubsystem::Deinitialize()
{bStopping=true;Rings.Reset();Players.Reset();PendingEntries.Reset();Super::Deinitialize();}
void UDWCombatMineSubsystem::RetireEmitter(DWCombatCommit::FEmitterId Emitter)
{if(auto* A=UDWCombatAuthoritySubsystem::Get(this))if(auto* Ledger=A->GetLedgerForResourceTransaction())Ledger->RetireEmitter(Emitter);}
bool UDWCombatMineSubsystem::RootCurrent(const FRing& R) const
{
    if(!Operational()||!R.bActive||!R.Player.IsValid())return false;
    FDWMoldRootIdentity Current;return R.Player->GetWorld()==GetWorld()&&R.Player->GetFormalRootIdentity(Current)&&DWMineRuntime::Same(Current,R.Root);
}
double UDWCombatMineSubsystem::GetCooldownRemaining(const ADWPlayerCharacter* P) const
{const auto* S=Players.Find(const_cast<ADWPlayerCharacter*>(P));return S&&GetWorld()?FMath::Max(0.,S->CooldownDeadline-static_cast<double>(GetWorld()->GetTimeSeconds())):0.;}
int32 UDWCombatMineSubsystem::GetLiveMineCount(const ADWPlayerCharacter* P) const
{
    int32 Count=0;for(const auto& Pair:Rings)if(Pair.Value.Player.Get()==P&&RootCurrent(Pair.Value))
        for(const auto& M:Pair.Value.Mines)if(M.bReleased&&!M.Save.bConsumed&&M.Actor.IsValid()&&!M.Actor->IsActorBeingDestroyed())++Count;
    return Count;
}
bool UDWCombatMineSubsystem::TryGround(ADWPlayerCharacter& P,const FDWMoldRootIdentity& Root,const FDWMoldMineConfig& C,int32 Ordinal,FVector& Out) const
{
    auto* Movement=P.GetCharacterMovement();if(!Movement||!P.GetCapsuleComponent())return false;
    FFindFloorResult RootFloor;Movement->FindFloor(Root.Anchor,RootFloor,false);
    if(!RootFloor.IsWalkableFloor())return false;
    const FVector Base(Root.Anchor.X,Root.Anchor.Y,RootFloor.HitResult.ImpactPoint.Z);
    const FVector Nominal=C.Point(Base,Ordinal);const auto Query=DWMineRuntime::Environment(GetWorld(),&P);
    FHitResult Ground;
    if(!GetWorld()->LineTraceSingleByChannel(Ground,Nominal+FVector(0,0,C.GroundToleranceCm+1.),
        Nominal-FVector(0,0,C.GroundToleranceCm+1.),ECC_Visibility,Query)||!Movement->IsWalkable(Ground)||
        FMath::Abs(Ground.ImpactPoint.Z-Base.Z)>C.GroundToleranceCm)return false;
    // Fixed visual footprint. No wall-following search or movement to substitute a failed ordinal.
    const FVector Center=Ground.ImpactPoint+FVector(0,0,4.1);
    FHitResult Wall;
    if(GetWorld()->LineTraceSingleByChannel(Wall,Base+FVector(0,0,4.1),Center,ECC_Visibility,Query))return false;
    if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Visibility,
        FCollisionShape::MakeBox(FVector(12.,12.,4.)),Query))return false;
    Out=Center;return true;
}
void UDWCombatMineSubsystem::OnRootCommitted(ADWPlayerCharacter* P,const FDWMoldRootIdentity& Root)
{
    if(!Operational()||!IsValid(P)||P->GetWorld()!=GetWorld())return;
    FDWMoldRootIdentity Current;if(!P->GetFormalRootIdentity(Current)||!DWMineRuntime::Same(Current,Root))return;
    // A movement-notice callback can pause after the fee/root facts. Preserve that one paid entry.
    if(P->IsCombatSimulationSuspended()){PendingEntries.Add(P,Root);return;}
    if(bDeploying){PendingEntries.Add(P,Root);return;}
    TGuardValue<bool> DeploymentGuard(bDeploying,true);
    PendingEntries.Remove(P);
    auto& State=Players.FindOrAdd(P);
    if(State.LastAttempt.Id==Root.Receipt.Id&&State.LastAttempt.Epoch==Root.Receipt.Epoch)return;
    State.LastAttempt=Root.Receipt; // One attempt per actual entry, including cooldown/all-ground failures.
    if(GetCooldownRemaining(P)>0.)return;
    auto* GI=P->GetGameInstance<UDWGameInstance>();FDWMoldMineConfig Config;FString Error;
    if(!GI||!GI->GetCombatRules()||!FDWMoldMineConfig::Build(*GI->GetCombatRules(),Config,Error))return;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);auto* Ledger=A?A->GetLedgerForResourceTransaction():nullptr;
    if(!Ledger||!A->IsActionOwnedBy(Root.Action,P))return;
    FRing R;R.Id=FGuid::NewGuid();R.Player=P;R.Root=Root;R.Config=Config;
    const FGuid Id=R.Id;Rings.Add(Id,MoveTemp(R));State.ActiveRing=Id;
    const TWeakObjectPtr<UWorld> World(GetWorld());
    for(int32 Index=0;Index<Config.Count;++Index)
    {
        const int32 Ordinal=Index+1;
        auto* Ring=Rings.Find(Id);if(!Ring||!RootCurrent(*Ring))break;
        FVector Location;if(!TryGround(*P,Root,Config,Ordinal,Location))continue;
        A=UDWCombatAuthoritySubsystem::Get(this);Ledger=A?A->GetLedgerForResourceTransaction():nullptr;
        if(!Ledger||!A->IsActionOwnedBy(Root.Action,P))break;
        const auto Emission=Ledger->ReleaseEmitter(Root.Receipt,static_cast<uint64>(Ordinal));
        if(Emission.Result!=DWCombatCommit::EEmission::Created)continue;
        FMine Mine;Mine.Save.MineId=FGuid::NewGuid();Mine.Save.Ordinal=Ordinal;Mine.Save.Location=Location;Mine.Emitter=Emission.Handle;
        const FGuid MineId=Mine.Save.MineId;Ring->Mines.Add(Mine);
        const FTransform Transform(FRotator::ZeroRotator,Location);
        auto* Actor=GetWorld()->SpawnActorDeferred<ADWCombatMine>(ADWCombatMine::StaticClass(),Transform,P,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        // Native spawning and FinishSpawning can notify other systems; never retain Ring references across them.
        Ring=Rings.Find(Id);
        if(!Actor||!World.IsValid()||World->bIsTearingDown||!Ring||!RootCurrent(*Ring))
        {RetireEmitter(Emission.Handle);if(IsValid(Actor)&&!Actor->IsActorBeingDestroyed()&&World.IsValid()&&!World->bIsTearingDown)Actor->Destroy();break;}
        auto* Slot=Ring->Mines.FindByPredicate([MineId](const FMine& M){return M.Save.MineId==MineId;});check(Slot);
        Actor->RingId=Id;Actor->MineId=MineId;Slot->Actor=Actor;
        Actor->FinishSpawning(Transform);
        Ring=Rings.Find(Id);
        if(!Operational()||!World.IsValid()||World->bIsTearingDown)return;
        if(!Ring||!RootCurrent(*Ring)||!IsValid(Actor)||Actor->IsActorBeingDestroyed()||!Actor->HasActorBegunPlay())
        {RetireEmitter(Emission.Handle);if(IsValid(Actor)&&!Actor->IsActorBeingDestroyed())Actor->Destroy();break;}
        Slot=Ring->Mines.FindByPredicate([MineId](const FMine& M){return M.Save.MineId==MineId;});check(Slot);
        if(Slot->Save.bConsumed)continue;
        Actor->SetActorHiddenInGame(false); // Published only after the actual native release.
    }
}
void UDWCombatMineSubsystem::EndRootFacts(ADWPlayerCharacter* P,uint64 Serial)
{
    if(!Operational())return;auto* A=UDWCombatAuthoritySubsystem::Get(this);auto* Ledger=A?A->GetLedgerForResourceTransaction():nullptr;
    for(auto& Pair:Rings)
    {
        auto& R=Pair.Value;if(R.Player.Get()!=P||R.Root.RootSerial!=Serial||!R.bActive)continue;
        R.bActive=false;for(auto& M:R.Mines){M.Save.bConsumed=true;if(Ledger)Ledger->RetireEmitter(M.Emitter);}
    }
    if(auto* S=Players.Find(P))S->ActiveRing={}; // CooldownDeadline deliberately survives.
    if(const auto* Pending=PendingEntries.Find(P);Pending&&Pending->RootSerial==Serial)PendingEntries.Remove(P);
}
void UDWCombatMineSubsystem::FlushRootCleanup(ADWPlayerCharacter* P,uint64 Serial)
{
    if(!Operational())return;TArray<TWeakObjectPtr<ADWCombatMine>> Destroy;
    for(const auto& Pair:Rings)if(!Pair.Value.bActive&&Pair.Value.Player.Get()==P&&(!Serial||Pair.Value.Root.RootSerial==Serial))
        for(const auto& M:Pair.Value.Mines)if(M.Actor.IsValid())Destroy.Add(M.Actor);
    for(const auto& Weak:Destroy)
    {if(!Operational())return;if(auto* Actor=Weak.Get();Actor&&!Actor->IsActorBeingDestroyed())Actor->Destroy();}
}
UDWCombatMineSubsystem::FMine* UDWCombatMineSubsystem::CurrentMine(ADWCombatMine& Mine,FRing*& OutRing)
{
    OutRing=Rings.Find(Mine.RingId);if(!OutRing||!RootCurrent(*OutRing)||Mine.IsActorBeingDestroyed()||!Mine.HasActorBegunPlay())return nullptr;
    return OutRing->Mines.FindByPredicate([&Mine](const FMine& M){return M.Save.MineId==Mine.MineId&&M.Actor.Get()==&Mine&&M.bReleased&&!M.Save.bConsumed;});
}
bool UDWCombatMineSubsystem::HasSight(ADWPlayerCharacter& P,ADWEnemyCharacter& E,FVector Center,float Radius) const
{
    if(!UDWDamageVolumeLibrary::IsTargetInDamageSphere(&E,Center,Radius))return false;
    auto Query=DWMineRuntime::Environment(GetWorld(),&P);TInlineComponentArray<UPrimitiveComponent*> Components(&E);
    for(auto* C:Components)if(UDWDamageVolumeLibrary::IsDamageHitComponent(C)&&C->OverlapComponent(Center,FQuat::Identity,FCollisionShape::MakeSphere(Radius)))
    {
        FVector Point;if(C->GetClosestPointOnCollision(Center,Point)<0.f)continue;
        FHitResult Hit;if(!GetWorld()->LineTraceSingleByChannel(Hit,Center,Point,ECC_Visibility,Query))return true;
    }
    return false;
}
bool UDWCombatMineSubsystem::PrepareExplosion(ADWCombatMine& Mine,FDWCombatMineExplosion& Out)
{
    if(!Operational()||GetWorld()->IsPaused())return false;FRing* R=nullptr;auto* M=CurrentMine(Mine,R);if(!M)return false;
    if(!R->Player->IsCombatActionAllowed())return false;
    FDWCombatMineExplosion E;E.Player=R->Player;E.Root=R->Root;E.RingId=R->Id;E.MineId=M->Save.MineId;E.Emitter=M->Emitter;E.Center=M->Save.Location;E.Config=R->Config;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return false;bool Trigger=false;
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
    {
        auto* Enemy=*It;if(!IsValid(Enemy)||Enemy->IsActorBeingDestroyed()||!Enemy->IsAlive()||!Enemy->IsHostileToCombatPlayer(E.Player.Get()))continue;
        const auto Life=A->CaptureActorLife(Enemy);if(!Life.Entity||R->HitEntities.Contains(Life.Entity))continue;
        const auto Saved=Enemy->CaptureFormalCombatSave();
        if((Saved.StableEnemyId.IsValid()&&R->HitFormalEnemies.Contains(Saved.StableEnemyId))||
           (!Enemy->PersistentId.IsNone()&&R->HitLegacyEnemies.Contains(Enemy->PersistentId)))continue;
        if(HasSight(*E.Player,*Enemy,E.Center,static_cast<float>(E.Config.TriggerRadiusCm)))Trigger=true;
        if(HasSight(*E.Player,*Enemy,E.Center,static_cast<float>(E.Config.DamageRadiusCm)))
            E.Targets.Add({Enemy,Life,Saved.StableEnemyId,Saved.StableEnemyId.IsValid()?NAME_None:Enemy->PersistentId});
    }
    if(!Trigger)return false;Out=MoveTemp(E);return true;
}
bool UDWCombatMineSubsystem::ExplosionCurrent(const FDWCombatMineExplosion& E) const
{
    if(!Operational()||GetWorld()->IsPaused())return false;const auto* R=Rings.Find(E.RingId);if(!R||!RootCurrent(*R)||!DWMineRuntime::Same(R->Root,E.Root))return false;
    const auto* M=R->Mines.FindByPredicate([&](const FMine& M){return M.Save.MineId==E.MineId;});
    return M&&M->bReleased&&!M->Save.bConsumed&&M->Actor.IsValid()&&!M->Actor->IsActorBeingDestroyed()&&M->Emitter.Id==E.Emitter.Id&&M->Emitter.Epoch==E.Emitter.Epoch;
}
void UDWCombatMineSubsystem::CommitExplosionFacts(const FDWCombatMineExplosion& E,const TArray<uint64>& Admitted)
{
    auto& R=Rings.FindChecked(E.RingId);auto* M=R.Mines.FindByPredicate([&](const FMine& V){return V.Save.MineId==E.MineId;});check(M);
    M->Save.bConsumed=true;
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))if(auto* L=A->GetLedgerForResourceTransaction())L->RetireEmitter(M->Emitter);
    for(const auto& Target:E.Targets)if(Admitted.Contains(Target.Life.Entity))
    {
        R.HitEntities.Add(Target.Life.Entity);
        if(Target.FormalEnemyId.IsValid())R.HitFormalEnemies.Add(Target.FormalEnemyId);
        else if(!Target.LegacyEnemyId.IsNone())R.HitLegacyEnemies.Add(Target.LegacyEnemyId);
        else R.bUnsaveableHit=true; // Explicit capture error, never silently forget a hit on load.
    }
}
bool UDWCombatMineSubsystem::TryTrigger(ADWCombatMine& Mine)
{
    if(!Operational()||bResolving||GetWorld()->IsPaused())return false;
    TGuardValue<bool> Guard(bResolving,true);auto* A=UDWCombatAuthoritySubsystem::Get(this);
    return A&&A->SubmitRootMineExplosion(Mine);
}
void UDWCombatMineSubsystem::PhysicalBegan(ADWCombatMine& Mine)
{
    auto* R=Rings.Find(Mine.RingId);if(!R||!RootCurrent(*R))return;
    auto* M=R->Mines.FindByPredicate([&](const FMine& V){return V.Save.MineId==Mine.MineId&&V.Actor.Get()==&Mine;});
    if(!M||M->bReleased||M->Save.bConsumed)return;
    const bool First=!R->Mines.ContainsByPredicate([](const FMine& V){return V.bReleased;});M->bReleased=true;
    if(First)Players.FindChecked(R->Player).CooldownDeadline=GetWorld()->GetTimeSeconds()+R->Config.CooldownSeconds;
}
void UDWCombatMineSubsystem::PhysicalEnded(ADWCombatMine& Mine)
{
    auto* R=Rings.Find(Mine.RingId);if(!R)return;
    auto* M=R->Mines.FindByPredicate([&](const FMine& V){return V.Save.MineId==Mine.MineId&&V.Actor.Get()==&Mine;});if(!M)return;
    M->Save.bConsumed=true;M->Actor.Reset();if(auto* A=UDWCombatAuthoritySubsystem::Get(this))if(auto* L=A->GetLedgerForResourceTransaction())L->RetireEmitter(M->Emitter);
}
void UDWCombatMineSubsystem::Tick(float Dt)
{
    if(!Operational()||bUpdating||GetWorld()->IsPaused()||!FMath::IsFinite(Dt)||Dt<=0.f)return;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return;const uint64 TickEpoch=Authority->GetWorldEpoch();
    TGuardValue<bool> Guard(bUpdating,true);TArray<TWeakObjectPtr<ADWCombatMine>> Active,Cleanup;
    const auto Pending=MoveTemp(PendingEntries);PendingEntries.Reset();
    for(const auto& Entry:Pending)
    {if(!Operational()||Authority->GetWorldEpoch()!=TickEpoch)return;if(auto* P=Entry.Key.Get())OnRootCommitted(P,Entry.Value);}
    TArray<TPair<TWeakObjectPtr<ADWPlayerCharacter>,uint64>> End;
    for(const auto& Pair:Rings)
    {
        const auto& R=Pair.Value;if(R.bActive&&!RootCurrent(R))End.Add({R.Player,R.Root.RootSerial});
        for(const auto& M:R.Mines)if(M.Actor.IsValid())
            (RootCurrent(R)&&!M.Save.bConsumed?Active:Cleanup).Add(M.Actor);
    }
    for(const auto& E:End)EndRootFacts(E.Key.Get(),E.Value);
    for(const auto& Weak:Active){if(!Operational()||Authority->GetWorldEpoch()!=TickEpoch||GetWorld()->IsPaused())return;if(auto* Mine=Weak.Get())TryTrigger(*Mine);}
    // Include mines consumed by this tick, whose actors are never allowed to trigger twice.
    for(const auto& Pair:Rings)for(const auto& M:Pair.Value.Mines)if(M.Save.bConsumed&&M.Actor.IsValid())Cleanup.AddUnique(M.Actor);
    for(const auto& Weak:Cleanup){if(!Operational()||Authority->GetWorldEpoch()!=TickEpoch)return;if(auto* Mine=Weak.Get();Mine&&!Mine->IsActorBeingDestroyed())Mine->Destroy();}
    if(!Operational()||Authority->GetWorldEpoch()!=TickEpoch)return;
    // Old root episodes need no scene record once cleanup is complete. Ledger receipts remain tombstoned.
    for(auto It=Rings.CreateIterator();It;++It)
        if(!It.Value().bActive&&!It.Value().Mines.ContainsByPredicate([](const FMine& M){return M.Actor.IsValid()&&!M.Actor->IsActorBeingDestroyed();}))It.RemoveCurrent();
}
bool UDWCombatMineSubsystem::CaptureSave(const ADWPlayerCharacter* P,FDWMoldMineRingSave& Out,FString& Error) const
{
    if(!Operational()||bResolving||bDeploying||PendingEntries.Contains(const_cast<ADWPlayerCharacter*>(P)))
    {Error=TEXT("Mine snapshot unavailable during teardown/commit or an unpresented paid root entry.");return false;}
    FDWMoldMineRingSave S;S.CooldownRemainingSeconds=GetCooldownRemaining(P);
    const auto* State=Players.Find(const_cast<ADWPlayerCharacter*>(P));const auto* R=State?Rings.Find(State->ActiveRing):nullptr;
    if(R&&RootCurrent(*R))
    {
        if(R->bUnsaveableHit){Error=TEXT("A hit legacy enemy has no persistent ID; cannot silently forget ring dedup.");return false;}
        S.bActive=true;S.RingId=R->Id;S.Anchor=R->Root.Anchor;
        for(const auto& M:R->Mines)if(M.bReleased)S.Mines.Add(M.Save);
        S.HitFormalEnemies=R->HitFormalEnemies.Array();S.HitLegacyEnemies=R->HitLegacyEnemies.Array();
    }
    if(!DWMoldMines::ValidateSnapshot(S,Error))return false;Out=MoveTemp(S);return true;
}
