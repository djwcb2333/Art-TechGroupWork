#include "DWCombatWorldObjectsPersistence.h"
#include "DWCombatWorldObjectsRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWPlayerCharacter.h"
#include "DWResourceNode.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimationAsset.h"
#include "TimerManager.h"
namespace
{
FString Path(const AActor* A){return UWorld::RemovePIEPrefix(A->GetPathName());}
FString Of(const TArray<FDWCombatObjectBinding>& B,const AActor* A){if(!A)return {};for(const auto& X:B)if(X.Actor.Get()==A)return X.StableId;return {};}
bool Same(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B){return A.Entity==B.Entity&&A.Life==B.Life;}
}
bool FDWCombatWorldObjectsPersistence::CaptureEnemy(ADWEnemyCharacter& E,const TArray<FDWCombatObjectBinding>& B,FDWCombatEnemyObjectSave& S,FString& Error)
{
    auto* W=E.GetWorld();auto* A=UDWCombatAuthoritySubsystem::Get(&E);
    const bool Terminal=E.IsActorBeingDestroyed()&&E.Health==0.f&&E.State==EDWEnemyState::Dead;
    if(!W||!A||(!Terminal&&(E.bStartingMeleeAttack||E.bDamageInProgress||E.bDeathTransitionInProgress||E.bCombatDeathEffectsPending))||E.bCombatRestoreInProgress||E.ForcedFall.bMoving)
    {Error=TEXT("Enemy snapshot requested inside an incomplete native transition");return false;}
    const double Now=W->GetTimeSeconds();const bool Formal=E.CombatProfile!=EDWEnemyCombatProfile::Legacy;
    if(Formal&&!E.IsFormalCombatReady()){Error=TEXT("Uninitialized formal enemy cannot be saved");return false;}
    S.StableId=Of(B,&E);if(S.StableId.IsEmpty())S.StableId=Formal?TEXT("enemy:")+E.FormalEnemyId.ToString(EGuidFormats::Digits):E.CombatWorldStableId;
    S.ActorPath=Path(&E);S.ClassPath=E.GetClass()->GetPathName();S.PersistentId=E.PersistentId;S.Transform=E.GetActorTransform();S.Life=A->FindRegisteredActorLife(&E);
    if(!S.Life.Entity&&!E.IsActorBeingDestroyed())S.Life=A->CaptureActorLife(&E);
    S.SourceNestId=Of(B,E.SourceNest.Get());S.bRuntimeSpawned=E.SourceNest!=nullptr;
    if(S.bRuntimeSpawned&&S.SourceNestId.IsEmpty())S.SourceNestId=TEXT("nest:")+Path(E.SourceNest.Get());
    S.Health=E.Health;S.MaxHealth=E.MaxHealth;S.State=static_cast<uint8>(E.State);S.Phase=static_cast<uint8>(E.AttackPhase);S.AttackSerial=E.AttackSerial;S.VariantIndex=E.CurrentAttackVariantIndex;S.Profile=E.CombatProfile;
    if(Formal)S.Formal=E.CaptureFormalCombatSave();S.Dagger=E.CaptureDaggerAwarenessSave();S.ForcedFall=E.ForcedFall.Saved;
    S.DaggerPlayerLife=E.DaggerPlayerLife;S.ObservedPlayerLife=E.FormalObservedPlayerLife;S.ObservedPlayerDamageRevision=E.FormalObservedPlayerDamageRevision;
    S.MeleeAction=E.FormalMeleeAction;S.MeleeTargetLife=E.FormalMeleeTargetLife;S.TargetId=Of(B,E.TargetPawn.Get());S.MeleeTargetId=Of(B,E.MeleeTarget.Get());
    if(!Formal&&E.MeleeTarget.IsValid())S.MeleeTargetLife=A->CaptureActorLife(E.MeleeTarget.Get());
    if((E.TargetPawn.IsValid()&&S.TargetId.IsEmpty())||(E.MeleeTarget.IsValid()&&S.MeleeTargetId.IsEmpty())){Error=TEXT("Enemy references an unregistered scene target");return false;}
    S.AttackAnimationPath=E.CurrentAttackAnimation?E.CurrentAttackAnimation->GetPathName():FString();
    S.Home=E.HomeLocation;S.PatrolGoal=E.PatrolGoal;S.LockedDirection=E.LockedAttackDirection;S.KnockbackDirection=E.KnockbackDirection;S.Velocity=E.GetVelocity();
    const double Start=Formal?E.FormalAttackStartedTime:E.AttackStartedTime;
    S.AttackAge=FMath::Max(0.,Now-Start);S.PhaseRemaining=E.AttackPhase==EDWCombatPhase::None?0.:FMath::Max(0.,(Formal?E.FormalPhaseEndTime:E.AttackPhaseEndTime)-Now);
    S.AttackCooldown=FMath::Max(0.,(Formal?E.FormalNextAttackTime:E.NextAttackTime)-Now);S.AlertRemaining=FMath::Max(0.,(Formal?E.FormalAlertEndTime:E.AlertEndTime)-Now);
    S.PatrolRemaining=FMath::Max(0.,E.NextPatrolTime-Now);S.PathRemaining=FMath::Max(0.,E.NextPathRequestTime-Now);
    S.CurrentWindup=E.CurrentAttackWindup;S.CurrentActive=E.CurrentAttackActiveDuration;S.CurrentRecovery=E.CurrentAttackRecovery;S.CurrentRange=E.CurrentAttackRange;S.CurrentDamage=E.CurrentAttackDamage;
    S.KnockbackDistance=E.KnockbackDistanceRemaining;S.KnockbackSeconds=E.KnockbackTimeRemaining;S.KnockbackSpeed=E.KnockbackSpeed;S.KnockbackInitialDistance=E.FormalKnockbackInitialDistance;S.KnockbackRevision=E.FormalKnockbackRevision;
    S.bHasObservation=E.FormalMemory.LastObservationTime>=0.;S.ObservationAge=S.bHasObservation?FMath::Max(0.,Now-E.FormalMemory.LastObservationTime):0.;
    S.bDeathCleanupScheduled=W->GetTimerManager().TimerExists(E.DeathCleanupTimer);S.DeathCleanupRemaining=S.bDeathCleanupScheduled?FMath::Max(0.,static_cast<double>(W->GetTimerManager().GetTimerRemaining(E.DeathCleanupTimer))):0.;S.LifeSpanRemaining=FMath::Max(0.f,E.GetLifeSpan());
    S.bAttackResolved=E.bAttackResolved;S.bHasAttackSnapshot=E.bHasAttackSnapshot;S.bFacingLocked=E.bFormalFacingLocked;S.bKnockedBack=E.bIsCombatKnockedBack;
    S.bAttackContinuesDuringKnockback=E.bFormalAttackContinuesDuringKnockback;S.bNeedsChaseState=E.bFormalNeedsChaseState;S.bHasPatrolGoal=E.bHasPatrolGoal;S.bPlayerDeathReturn=E.IsAlive()&&E.bPlayerDeathReturn;
    S.bHidden=E.IsHidden();S.bActorTick=E.IsActorTickEnabled();S.bCollision=E.GetActorEnableCollision();S.bMeshTick=E.GetMesh()&&E.GetMesh()->IsComponentTickEnabled();
    S.MovementMode=E.GetCharacterMovement()->MovementMode;S.CustomMovementMode=E.GetCharacterMovement()->CustomMovementMode;
    S.bHasPreDeathState=E.bHasPreDeathState;S.bPreDeathCollision=E.bPreDeathCollision;S.bPreDeathHidden=E.bPreDeathHidden;S.bPreDeathTick=E.bPreDeathTick;S.bPreDeathMeshTick=E.bPreDeathMeshTick;
    S.PreDeathMovementMode=E.PreDeathMovementMode;S.PreDeathCustomMovementMode=E.PreDeathCustomMovementMode;
    for(auto V:E.SlowEffects)if(V.Y>Now)S.LegacySlows.Add(FVector2D(V.X,V.Y-Now));
    return true;
}
bool FDWCombatWorldObjectsPersistence::Capture(UWorld& W,ADWPlayerCharacter& P,FDWCombatWorldObjectsSave& Out,TArray<FDWPersistedActorState>& Legacy,TArray<FDWCombatObjectBinding>& B,FString& Error)
{return CaptureInternal(W,P,Out,Legacy,B,Error,{});}
bool FDWCombatWorldObjectsPersistence::CaptureInternal(UWorld& W,ADWPlayerCharacter& P,FDWCombatWorldObjectsSave& Out,TArray<FDWPersistedActorState>& OutLegacy,
    TArray<FDWCombatObjectBinding>& OutBindings,FString& Error,const TSet<TWeakObjectPtr<AActor>>& Ignored)
{
    auto* A=UDWCombatAuthoritySubsystem::Get(&W);auto* D=UDWCombatCoreDropSubsystem::Get(&W);auto* R=UDWCombatRecoverySubsystem::Get(&W);auto* Registry=W.GetSubsystem<UDWCombatWorldObjectsRegistry>();
    if(!IsInGameThread()||W.bIsTearingDown||!IsValid(&P)||P.IsActorBeingDestroyed()||P.GetWorld()!=&W||!A||!D||!R||!Registry||Registry->bCaptureFailure)
    {Error=TEXT("World object capture lifecycle unavailable");return false;}
    if(!R->HasRecoveryFacts()&&!R->BindNewPlayer(P)){Error=TEXT("Recovery identity is not initialized");return false;}
    const uint64 Epoch=A->GetWorldEpoch();const auto PLife=A->CaptureActorLife(&P);
    FDWCombatWorldObjectsSave S;S.MapPackage=UDWCombatRecoverySubsystem::MapPackage(&W);TArray<FDWPersistedActorState>L;TArray<FDWCombatObjectBinding>B;
    B.Add({TEXT("player:0"),&P});if(P.GetController())B.Add({TEXT("controller:0"),P.GetController()});
    for(TActorIterator<ADWEnemyNest> It(&W);It;++It)if(!It->IsActorBeingDestroyed()&&!Ignored.Contains(*It))B.Add({TEXT("nest:")+Path(*It),*It});
    for(TActorIterator<ADWResourceNode> It(&W);It;++It)if(!It->IsActorBeingDestroyed()&&!Ignored.Contains(*It))B.Add({TEXT("resource:")+Path(*It),*It});
    for(TActorIterator<ADWEnemyCharacter> It(&W);It;++It)
    {
        auto* E=*It;if(E->IsActorBeingDestroyed()||Ignored.Contains(E)||E->bCombatWorldStaged)continue;
        FString Id;
        if(E->CombatProfile!=EDWEnemyCombatProfile::Legacy)Id=TEXT("enemy:")+E->FormalEnemyId.ToString(EGuidFormats::Digits);
        else{if(E->CombatWorldStableId.IsEmpty())E->CombatWorldStableId=E->SourceNest?TEXT("enemy:runtime:")+FGuid::NewGuid().ToString(EGuidFormats::Digits):TEXT("enemy:scene:")+Path(E);Id=E->CombatWorldStableId;}
        B.Add({Id,E});A->CaptureActorLife(E);if(E->GetController())B.Add({TEXT("controller:")+Id,E->GetController()});
    }
    TSet<FString> Names;for(const auto& V:B){if(Names.Contains(V.StableId)){Error=TEXT("Duplicate world binding identity");return false;}Names.Add(V.StableId);}
    for(TActorIterator<ADWResourceNode> It(&W);It;++It)if(!It->IsActorBeingDestroyed()&&!Ignored.Contains(*It))
    {if(It->bCommittingHarvest){Error=TEXT("Resource is committing harvest");return false;}FDWPersistedActorState V;V.ActorId=Of(B,*It);V.ActorPath=Path(*It);V.Health=It->RemainingAmount;V.bDestroyed=!It->bInfinite&&It->RemainingAmount<=0;V.Transform=It->GetActorTransform();V.bHasTransform=true;L.Add(V);}
    for(TActorIterator<ADWEnemyNest> It(&W);It;++It)if(!It->IsActorBeingDestroyed()&&!Ignored.Contains(*It))
    {if(It->bSpawningEnemy||It->bCombatDeathTransition||It->bCombatDeathEffectsPending){Error=TEXT("Nest is in a partial transition");return false;}FDWPersistedActorState V;V.ActorId=Of(B,*It);V.ActorPath=Path(*It);V.Health=It->Health;V.bDestroyed=It->Health<=0.;V.Transform=It->GetActorTransform();V.bHasTransform=true;V.SpawnProgress=It->SpawnProgress;L.Add(V);S.NestClocks.Add({V.ActorId,It->CurrentSpawnInterval});}
    for(TActorIterator<ADWEnemyCharacter> It(&W);It;++It)if(!It->IsActorBeingDestroyed()&&!Ignored.Contains(*It)&&!It->bCombatWorldStaged)
    {FDWCombatEnemyObjectSave E;if(!CaptureEnemy(**It,B,E,Error))return false;S.Enemies.Add(MoveTemp(E));}
    for(const auto& Pair:Registry->Tombstones)
    {
        auto Tombstone=Pair.Value;FDWCombatCoreDropSave Drop;
        // Claiming a core after its corpse vanished changes the world record, not the immutable death identity.
        if(D->FindDrop(Tombstone.Formal.CoreDropDeathId,Drop))Tombstone.Formal.DropDecision=Drop.Decision;
        S.Enemies.Add(MoveTemp(Tombstone));
    }
    if(!D->CaptureSave(S.CoreDrops,Error)||!R->CaptureSave(S.Recovery,Error))return false;
    S.Enemies.Sort([](const auto& X,const auto& Y){return X.StableId.Compare(Y.StableId,ESearchCase::CaseSensitive)<0;});
    S.NestClocks.Sort([](const auto& X,const auto& Y){return X.StableId.Compare(Y.StableId,ESearchCase::CaseSensitive)<0;});
    S.CoreDrops.Sort([](const auto& X,const auto& Y){return X.DeathId.ToString()<Y.DeathId.ToString();});
    L.Sort([](const auto& X,const auto& Y){return X.ActorId.Compare(Y.ActorId,ESearchCase::CaseSensitive)<0;});
    if(A->GetWorldEpoch()!=Epoch||!Same(A->CaptureActorLife(&P),PLife)||!DWCombatWorldObjectsSave::Validate(S,Error)||!DWCombatWorldObjectsSave::ValidateLegacy(L,Error))return false;
    Out=MoveTemp(S);OutLegacy=MoveTemp(L);OutBindings=MoveTemp(B);return true;
}
void FDWCombatWorldObjectsPersistence::RecordDestroyedEnemy(ADWEnemyCharacter& E)
{
    auto* W=E.GetWorld();if(!W||W->bIsTearingDown||E.bCombatWorldStaged||E.CombatProfile==EDWEnemyCombatProfile::Legacy||E.Health>0.f)return;
    auto* R=W->GetSubsystem<UDWCombatWorldObjectsRegistry>();if(!R)return;FDWCombatEnemyObjectSave S;FString Error;
    TArray<FDWCombatObjectBinding>B;
    // Dead snapshots have no future target/action. Retain the native committed drop and provenance.
    if(!CaptureEnemy(E,B,S,Error)||R->Revision==MAX_uint64){R->bCaptureFailure=true;return;}
    S.bEnded=true;S.bHidden=true;S.bActorTick=false;S.bCollision=false;S.bMeshTick=false;
    S.bDeathCleanupScheduled=false;S.DeathCleanupRemaining=0.;S.LifeSpanRemaining=0.;
    S.TargetId.Reset();S.MeleeTargetId.Reset();S.MeleeTargetLife={};S.MeleeAction={};S.Phase=0;S.PhaseRemaining=0.;
    R->Tombstones.Add(S.StableId,MoveTemp(S));++R->Revision;
}
