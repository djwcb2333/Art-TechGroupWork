#include "DWCombatEffectsPersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatGroundAreaSubsystem.h"
#include "DWCombatSporeProjectile.h"
#include "DWCombatBowProjectile.h"
#include "DWCombatBottleProjectile.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"

struct FDWCombatEffectsRestoreState
{
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority;
    TWeakObjectPtr<UDWCombatGroundAreaSubsystem> Areas;
    TArray<FDWCombatObjectBinding> Bindings;
    TArray<uint8> Expected;
    FDWCombatEffectsSave Save;
    TArray<TWeakObjectPtr<ADWCombatSporeProjectile>> Spores;
    TArray<TWeakObjectPtr<ADWCombatBowProjectile>> Arrows;
    TArray<TWeakObjectPtr<ADWCombatBottleProjectile>> Bottles;
    TArray<TWeakObjectPtr<AActor>> Retire;
    TMap<uint64,UDWCombatGroundAreaSubsystem::FArea> AreaFacts;
    TMap<uint64,UDWCombatGroundAreaSubsystem::FTarget> TargetFacts;
    double At=0.;uint64 NewEpoch=0;
    bool bCommitted=false,bPublished=false;
    ~FDWCombatEffectsRestoreState()
    {
        if(bCommitted)return;
        const auto Destroy=[](AActor* A){if(IsValid(A)&&!A->IsActorBeingDestroyed()&&A->GetWorld()&&!A->GetWorld()->bIsTearingDown)A->Destroy();};
        for(auto W:Spores)Destroy(W.Get());for(auto W:Arrows)Destroy(W.Get());for(auto W:Bottles)Destroy(W.Get());
    }
};
void FDWCombatEffectsRestoreStateDeleter::operator()(FDWCombatEffectsRestoreState* P)const{delete P;}
FDWCombatEffectsRestorePlan::FDWCombatEffectsRestorePlan()=default;
FDWCombatEffectsRestorePlan::~FDWCombatEffectsRestorePlan()=default;
FDWCombatEffectsRestorePlan::FDWCombatEffectsRestorePlan(FDWCombatEffectsRestorePlan&&)=default;
FDWCombatEffectsRestorePlan& FDWCombatEffectsRestorePlan::operator=(FDWCombatEffectsRestorePlan&&)=default;
namespace
{
FString IdOf(const TArray<FDWCombatObjectBinding>& B,const AActor* A)
{if(A)for(const auto& V:B)if(V.Actor.Get()==A)return V.StableId;return {};}
bool HasOwner(const FDWCombatAuthoritySave& A,DWCombatCommit::FEntityLife Life)
{for(const auto& V:A.Actors)if(V.Life.Entity==Life.Entity&&Life.Life&&Life.Life<=V.Life.Life)return true;return false;}
template<class T>T* Stage(UWorld* World,FVector Position)
{
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<T>(T::StaticClass(),Position,FRotator::ZeroRotator,P);
}
}
bool FDWCombatEffectsPersistence::Capture(UDWCombatAuthoritySubsystem& Authority,const TArray<FDWCombatObjectBinding>& Bindings,FDWCombatEffectsSave& Out,FString& Error)
{
    if(!Authority.IsOperational()){Error=TEXT("Effects world not operational");return false;}auto* World=Authority.GetWorld();auto* Areas=UDWCombatGroundAreaSubsystem::Get(&Authority);
    if(!Areas||!Areas->Operational()){Error=TEXT("Missing ground-area world");return false;}
    FDWCombatEffectsSave S;if(!Authority.CaptureWorldIdentity(Bindings,S.Authority,Error))return false;const double Now=World->GetTimeSeconds();
    const auto Owner=[&](DWCombatCommit::FEntityLife Life,DWCombatCommit::FEmitterId Emitter,AController* Instigator,FDWReleasedEffectOwnerSave& V)
    {if(!HasOwner(S.Authority,Life)||Emitter.Epoch!=S.Authority.Ledger.Epoch)return false;V.SourceLife=Life;V.Emitter=Emitter.Id;V.InstigatorId=IdOf(Bindings,Instigator);return !Instigator||!V.InstigatorId.IsEmpty();};
    for(TActorIterator<ADWCombatSporeProjectile> It(World);It;++It)
    {
        const auto& P=**It;if(!P.bReleased||P.bSpent||P.IsActorBeingDestroyed())continue;FDWSporeFlightSave V;
        if(!Owner(P.SourceLife,P.Emitter,P.SourceController.Get(),V.Owner)){Error=TEXT("Spore source or controller missing stable binding");return false;}
        V.Shot=P.Shot;V.Position=P.GetActorLocation();V.Direction=P.Direction;V.LockedTargetLife=P.LockedTargetLife;V.bLostLock=P.bLostLock;
        V.TravelledCm=P.TravelledCm;V.LastWorldOffset=P.LastWorldSeconds-Now;V.PendingSeconds=P.PendingSeconds;S.Spores.Add(V);
    }
    for(TActorIterator<ADWCombatBowProjectile> It(World);It;++It)
    {
        const auto& P=**It;if(!P.bReleased||P.bSpent||P.IsActorBeingDestroyed())continue;FDWBowFlightSave V;
        if(!Owner(P.SourceLife,P.Emitter,P.SourceController.Get(),V.Owner)){Error=TEXT("Arrow source or controller missing stable binding");return false;}
        V.Shot=P.Shot;V.Position=P.GetActorLocation();V.Direction=P.Direction;V.TravelledCm=P.TravelledCm;
        V.LastWorldOffset=P.LastWorldSeconds-Now;V.ReleasedOffset=P.ReleasedAt-Now;
        V.bHasVisualTransform=P.bFlightPresentationCaptured;V.VisualTransform=P.CapturedVisualTransform;S.Arrows.Add(V);
    }
    for(TActorIterator<ADWCombatBottleProjectile> It(World);It;++It)
    {
        const auto& P=**It;if(!P.bReleased||P.bSpent||P.IsActorBeingDestroyed())continue;FDWBottleFlightSave V;
        if(!Owner(P.SourceLife,P.Emitter,P.SourceController.Get(),V.Owner)){Error=TEXT("Bottle source or controller missing stable binding");return false;}
        V.Shot=P.Shot;V.Position=P.GetActorLocation();V.FlightStart=P.FlightStart;V.GroundTarget=P.GroundTarget;V.FlightAge=P.FlightAge;
        V.LastWorldOffset=P.LastWorldSeconds-Now;V.ReleasedOffset=P.ReleasedWorldSeconds-Now;V.PendingSeconds=P.PendingSeconds;V.WalkableFloorZ=P.WalkableFloorZ;S.Bottles.Add(V);
    }
    for(const auto& Pair:Areas->Areas)
    {
        const auto& A=Pair.Value;FDWGroundAreaSave V;if(!Owner(A.SourceLife,A.Emitter,A.Instigator.Get(),V.Owner)){Error=TEXT("Area source or controller missing stable binding");return false;}
        V.Ground=A.Ground;V.StartedOffset=A.Started-Now;V.EndsOffset=A.Ends-Now;V.RadiusCm=A.RadiusCm;V.SlowFraction=A.SlowFraction;V.Damage=A.Damage;V.Period=A.Period;V.bActive=A.bActive;S.Areas.Add(V);
    }
    for(const auto& Pair:Areas->Targets)
    {const auto& T=Pair.Value;S.TargetPhases.Add({T.Life,T.Clock.AnchorSeconds-Now,T.Clock.PeriodSeconds,T.Clock.NextOrdinal});}
    // Deterministic byte snapshots are also the optimistic-restore mutation check.
    S.Spores.Sort([](const auto& A,const auto& B){return A.Owner.Emitter<B.Owner.Emitter;});S.Arrows.Sort([](const auto& A,const auto& B){return A.Owner.Emitter<B.Owner.Emitter;});
    S.Bottles.Sort([](const auto& A,const auto& B){return A.Owner.Emitter<B.Owner.Emitter;});S.Areas.Sort([](const auto& A,const auto& B){return A.Owner.Emitter<B.Owner.Emitter;});S.TargetPhases.Sort([](const auto& A,const auto& B){return A.TargetLife.Entity<B.TargetLife.Entity;});
    if(!DWCombatEffectsSave::Validate(S,Error))return false;Out=MoveTemp(S);return true;
}
bool FDWCombatEffectsRestorePlan::CanCommit(FString& Error)const
{
    if(!State||State->bCommitted||!State->Authority.IsValid()||!State->Areas.IsValid()||!State->Authority->GetWorld()||State->Authority->GetWorld()->GetTimeSeconds()!=State->At||!State->Authority->CanCommitWorldIdentity(AuthorityPlan,Error)){Error=TEXT("Effects restore candidate stale");return false;}
    for(auto P:State->Spores)if(!P.IsValid()||P->IsActorBeingDestroyed()||P->IsReleased()){Error=TEXT("Staged spore invalid");return false;}
    for(auto P:State->Arrows)if(!P.IsValid()||P->IsActorBeingDestroyed()||P->IsReleased()){Error=TEXT("Staged arrow invalid");return false;}
    for(auto P:State->Bottles)if(!P.IsValid()||P->IsActorBeingDestroyed()||P->IsReleased()){Error=TEXT("Staged bottle invalid");return false;}
    FDWCombatEffectsSave Current;TArray<uint8> Bytes;if(!FDWCombatEffectsPersistence::Capture(*State->Authority.Get(),State->Bindings,Current,Error)||!DWCombatEffectsSave::Encode(Current,Bytes,Error)||Bytes!=State->Expected){Error=TEXT("Live effects changed while preparing restore");return false;}return true;
}
bool FDWCombatEffectsPersistence::PrepareRestore(UDWCombatAuthoritySubsystem& Authority,const FDWCombatEffectsSave& S,const TArray<FDWCombatObjectBinding>& LiveBindings,const TArray<FDWCombatObjectBinding>& RestoredBindings,
    const TArray<uint64>& Keep,FDWCombatEffectsRestorePlan& Out,FString& Error)
{
    if(!DWCombatEffectsSave::Validate(S,Error)||!Authority.IsOperational())return false;
    auto* World=Authority.GetWorld();auto* Areas=UDWCombatGroundAreaSubsystem::Get(&Authority);if(!Areas){Error=TEXT("Missing destination area subsystem");return false;}
    FDWCombatEffectsRestorePlan Plan;Plan.State.Reset(new FDWCombatEffectsRestoreState());auto& C=*Plan.State;
    C.Authority=&Authority;C.Areas=Areas;C.Bindings=LiveBindings;C.Save=S;C.At=World->GetTimeSeconds();
    FDWCombatEffectsSave Before;if(!Capture(Authority,LiveBindings,Before,Error)||!DWCombatEffectsSave::Encode(Before,C.Expected,Error))return false;
    if(!Authority.PrepareWorldIdentityRestore(S.Authority,RestoredBindings,Keep,Plan.AuthorityPlan,Error))return false;C.NewEpoch=Plan.AuthorityPlan.NewEpoch();
    const TWeakObjectPtr<UWorld> ExpectedWorld(World);const uint64 ExpectedEpoch=Authority.GetWorldEpoch();
    const auto StagingCurrent=[&](){return ExpectedWorld.IsValid()&&!ExpectedWorld->bIsTearingDown&&C.Authority.IsValid()&&C.Authority->IsOperational()&&C.Authority->GetWorld()==ExpectedWorld.Get()&&C.Authority->GetWorldEpoch()==ExpectedEpoch&&ExpectedWorld->GetTimeSeconds()==C.At;};
    const auto ValidTime=[&](double Offset){return FMath::IsFinite(C.At+Offset);};
    for(const auto& V:S.Spores)if(!ValidTime(V.LastWorldOffset)){Error=TEXT("Spore time cannot be represented");return false;}
    for(const auto& V:S.Arrows)if(!ValidTime(V.LastWorldOffset)||!ValidTime(V.ReleasedOffset)){Error=TEXT("Arrow time cannot be represented");return false;}
    for(const auto& V:S.Bottles)if(!ValidTime(V.LastWorldOffset)||!ValidTime(V.ReleasedOffset)){Error=TEXT("Bottle time cannot be represented");return false;}
    // Prepare all maps and scene actors before the native adoption interval. They cannot collide, move, or charge.
    for(const auto& V:S.Spores){if(!StagingCurrent()){Error=TEXT("World changed while staging");return false;}auto* P=Stage<ADWCombatSporeProjectile>(World,V.Position);if(!IsValid(P)){Error=TEXT("Cannot stage spore");return false;}C.Spores.Add(P);}
    for(const auto& V:S.Arrows)
    {
        if(!StagingCurrent()){Error=TEXT("World changed while staging");return false;}
        auto* P=Stage<ADWCombatBowProjectile>(World,V.Position);if(!IsValid(P)){Error=TEXT("Cannot stage arrow");return false;}C.Arrows.Add(P);
        if(auto* Source=Cast<ADWPlayerCharacter>(Plan.AuthorityPlan.FindActor(V.Owner.SourceLife.Entity)))
            if(const auto* W=Source->WeaponAnimationComponent.Get())P->CaptureFlightPresentation(*W,false);
        if(V.bHasVisualTransform){P->CapturedVisualTransform=V.VisualTransform;P->bFlightPresentationCaptured=true;}
    }
    for(const auto& V:S.Bottles){if(!StagingCurrent()){Error=TEXT("World changed while staging");return false;}auto* P=Stage<ADWCombatBottleProjectile>(World,V.Position);if(!IsValid(P)){Error=TEXT("Cannot stage bottle");return false;}C.Bottles.Add(P);if(!StagingCurrent()){Error=TEXT("World changed during bottle staging");return false;}if(auto* Source=Cast<ADWPlayerCharacter>(Plan.AuthorityPlan.FindActor(V.Owner.SourceLife.Entity)))P->CopyAuthoredVisual(*Source);}
    if(!StagingCurrent()){Error=TEXT("World changed after staging");return false;}
    for(const auto& V:S.Areas)
    {
        UDWCombatGroundAreaSubsystem::FArea A;A.Emitter={C.NewEpoch,V.Owner.Emitter};A.SourceLife=V.Owner.SourceLife;A.Source=Cast<ADWPlayerCharacter>(Plan.AuthorityPlan.FindActor(V.Owner.SourceLife.Entity));A.Instigator=Cast<AController>(Plan.AuthorityPlan.FindActor(V.Owner.InstigatorId));
        A.Ground=V.Ground;A.Started=C.At+V.StartedOffset;A.Ends=C.At+V.EndsOffset;A.RadiusCm=V.RadiusCm;A.SlowFraction=V.SlowFraction;A.Damage=V.Damage;A.Period=V.Period;A.bActive=V.bActive;
        if(!FMath::IsFinite(A.Started)||!FMath::IsFinite(A.Ends)||A.Ends<=A.Started){Error=TEXT("Area time cannot be represented in destination world");return false;}C.AreaFacts.Add(V.Owner.Emitter,A);
    }
    for(const auto& V:S.TargetPhases)
    {
        UDWCombatGroundAreaSubsystem::FTarget T;T.Actor=Cast<ADWEnemyCharacter>(Plan.AuthorityPlan.FindActor(V.TargetLife.Entity));T.Life=V.TargetLife;
        T.Clock.AnchorSeconds=C.At+V.AnchorOffset;T.Clock.PeriodSeconds=V.PeriodSeconds;T.Clock.NextOrdinal=V.NextOrdinal;
        if(!FMath::IsFinite(T.Clock.AnchorSeconds)||!FMath::IsFinite(T.Clock.NextSeconds())){Error=TEXT("Target cadence time cannot be represented");return false;}C.TargetFacts.Add(V.TargetLife.Entity,T);
    }
    for(TActorIterator<ADWCombatSporeProjectile> It(World);It;++It)if(It->bReleased)C.Retire.Add(*It);
    for(TActorIterator<ADWCombatBowProjectile> It(World);It;++It)if(It->bReleased)C.Retire.Add(*It);
    for(TActorIterator<ADWCombatBottleProjectile> It(World);It;++It)if(It->bReleased)C.Retire.Add(*It);
    for(const auto& V:Areas->Areas)if(V.Value.Marker.IsValid())C.Retire.Add(V.Value.Marker);
    if(!Plan.CanCommit(Error))return false;Out=MoveTemp(Plan);return true;
}
void FDWCombatEffectsPersistence::CommitFactsUnchecked(FDWCombatEffectsRestorePlan& Plan)
{
    check(Plan.State&&!Plan.State->bCommitted);auto& C=*Plan.State;auto* A=C.Authority.Get();check(A&&C.Areas.IsValid());
    A->CommitWorldIdentityUnchecked(Plan.AuthorityPlan);
    for(auto Weak:C.Retire)
    {if(auto* Spore=Cast<ADWCombatSporeProjectile>(Weak.Get())){Spore->bSpent=true;Spore->bReleased=false;}
        else if(auto* Arrow=Cast<ADWCombatBowProjectile>(Weak.Get())){Arrow->bSpent=true;Arrow->bReleased=false;Arrow->bAreaOwned=true;}
        else if(auto* Bottle=Cast<ADWCombatBottleProjectile>(Weak.Get())){Bottle->bSpent=true;Bottle->bReleased=false;Bottle->bAreaOwned=true;}}
    for(int32 I=0;I<C.Save.Spores.Num();++I)
    {
        auto& P=*C.Spores[I].Get();const auto& V=C.Save.Spores[I];P.Shot=V.Shot;P.SourceLife=V.Owner.SourceLife;P.Emitter={C.NewEpoch,V.Owner.Emitter};P.Source=Cast<ADWPlayerCharacter>(Plan.AuthorityPlan.FindActor(V.Owner.SourceLife.Entity));P.SourceController=Cast<AController>(Plan.AuthorityPlan.FindActor(V.Owner.InstigatorId));
        P.Direction=V.Direction;P.TravelledCm=V.TravelledCm;P.LastWorldSeconds=C.At+V.LastWorldOffset;P.PendingSeconds=V.PendingSeconds;P.LockedTargetLife=V.LockedTargetLife;P.LockedTarget=Cast<ADWEnemyCharacter>(Plan.AuthorityPlan.FindActor(V.LockedTargetLife.Entity));P.bLostLock=V.bLostLock;P.bReleased=true;
    }
    for(int32 I=0;I<C.Save.Arrows.Num();++I)
    {
        auto& P=*C.Arrows[I].Get();const auto& V=C.Save.Arrows[I];P.Shot=V.Shot;P.SourceLife=V.Owner.SourceLife;P.Emitter={C.NewEpoch,V.Owner.Emitter};P.Source=Cast<ADWPlayerCharacter>(Plan.AuthorityPlan.FindActor(V.Owner.SourceLife.Entity));P.SourceController=Cast<AController>(Plan.AuthorityPlan.FindActor(V.Owner.InstigatorId));
        P.Direction=V.Direction;P.TravelledCm=V.TravelledCm;P.LastWorldSeconds=C.At+V.LastWorldOffset;P.ReleasedAt=C.At+V.ReleasedOffset;P.bReleased=true;
    }
    for(int32 I=0;I<C.Save.Bottles.Num();++I)
    {
        auto& P=*C.Bottles[I].Get();const auto& V=C.Save.Bottles[I];P.Shot=V.Shot;P.SourceLife=V.Owner.SourceLife;P.Emitter={C.NewEpoch,V.Owner.Emitter};P.Source=Cast<ADWPlayerCharacter>(Plan.AuthorityPlan.FindActor(V.Owner.SourceLife.Entity));P.SourceController=Cast<AController>(Plan.AuthorityPlan.FindActor(V.Owner.InstigatorId));
        P.FlightStart=V.FlightStart;P.GroundTarget=V.GroundTarget;P.FlightAge=V.FlightAge;P.LastWorldSeconds=C.At+V.LastWorldOffset;P.ReleasedWorldSeconds=C.At+V.ReleasedOffset;P.PendingSeconds=V.PendingSeconds;P.WalkableFloorZ=V.WalkableFloorZ;P.bReleased=true;
    }
    C.Areas->Areas=MoveTemp(C.AreaFacts);C.Areas->Targets=MoveTemp(C.TargetFacts);C.Areas->Epoch=C.NewEpoch;C.bCommitted=true;
}
void FDWCombatEffectsPersistence::PublishRestoredEffects(FDWCombatEffectsRestorePlan& Plan)
{
    if(!Plan.State||!Plan.State->bCommitted||Plan.State->bPublished)return;auto& C=*Plan.State;auto* A=C.Authority.Get();
    const auto Current=[&](){return A&&UDWCombatAuthoritySubsystem::Get(A)==A&&A->GetWorldEpoch()==C.NewEpoch;};if(!Current())return;
    C.bPublished=true;
    // Retired carriers have been disarmed, so their EndPlay cannot retire a restored emitter with the same numeric ID.
    for(auto Weak:C.Retire){if(!Current())return;if(auto* P=Weak.Get();P&&!P->IsActorBeingDestroyed())P->Destroy();}
    for(auto Weak:C.Spores){if(!Current())return;if(auto* P=Weak.Get();P&&!P->IsActorBeingDestroyed())P->SetActorHiddenInGame(false);}
    for(auto Weak:C.Arrows){if(!Current())return;if(auto* P=Weak.Get();P&&!P->IsActorBeingDestroyed())if(!P->PublishFlightPresentation())return;}
    for(auto Weak:C.Bottles){if(!Current())return;if(auto* P=Weak.Get();P&&!P->IsActorBeingDestroyed())P->SetActorHiddenInGame(false);}
    if(Current())for(TActorIterator<ADWEnemyCharacter> It(A->GetWorld());It;++It)if(It->IsAlive())It->RefreshCombatGroundAreaSlow();
    // Area markers are presentation only and materialize on their regular tick; do not execute owed DOT during loading.
}
