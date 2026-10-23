#include "DWCombatWorldObjectsSave.h"
#include "DWCombatItemRegistry.h"
#include "Misc/PackageName.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
namespace
{
constexpr uint32 Magic=0x4457574f,Version=1,MaxCount=65536;
constexpr int32 MaxBytes=64*1024*1024;
bool Fin(double V){return FMath::IsFinite(V);}
bool Nonnegative(double V){return Fin(V)&&V>=0.;}
bool Text(const FString& V,bool Empty=false){return (Empty||!V.IsEmpty())&&V.Len()<=4096&&V.Len()==FCString::Strlen(*V);}
bool Pose(const FTransform& V){return !V.ContainsNaN()&&V.GetRotation().IsNormalized()&&V.GetScale3D().GetAbsMin()>0.;}
bool Life(DWCombatCommit::FEntityLife V,bool Empty=true){return V.Entity?V.Life>0:Empty&&!V.Life;}
void Flag(FArchive& A,bool& V){uint8 B=V?1:0;A<<B;if(A.IsLoading()){if(B>1)A.SetError();V=B!=0;}}
template<class E>void Enum(FArchive& A,E& V){uint8 B=static_cast<uint8>(V);A<<B;if(A.IsLoading())V=static_cast<E>(B);}
void String(FArchive& A,FString& V)
{
    if(A.IsSaving()){FTCHARToUTF8 U(*V,V.Len());int32 N=U.Length();A<<N;if(N<0||N>16384){A.SetError();return;}A.Serialize(const_cast<ANSICHAR*>(U.Get()),N);}
    else{int32 N=0;A<<N;if(A.IsError()||N<0||N>16384||N>A.TotalSize()-A.Tell()){A.SetError();return;}
        TArray<ANSICHAR>B;B.SetNumUninitialized(N+1);A.Serialize(B.GetData(),N);B[N]=0;FUTF8ToTCHAR T(B.GetData(),N);V=FString(T.Length(),T.Get());
        FTCHARToUTF8 U(*V,V.Len());if(U.Length()!=N||(N&&FMemory::Memcmp(U.Get(),B.GetData(),N))||V.Len()!=FCString::Strlen(*V))A.SetError();}
}
void Name(FArchive& A,FName& V){FString S=A.IsSaving()?(V.IsNone()?FString():V.ToString()):FString();String(A,S);if(A.IsLoading()&&!A.IsError())V=S.IsEmpty()?NAME_None:FName(*S);}
void Vec(FArchive& A,FVector& V){A<<V.X<<V.Y<<V.Z;}
void Transform(FArchive& A,FTransform& V){FVector P=V.GetLocation(),S=V.GetScale3D();FQuat Q=V.GetRotation();Vec(A,P);Vec(A,S);A<<Q.X<<Q.Y<<Q.Z<<Q.W;if(A.IsLoading())V=FTransform(Q,P,S);}
void Entity(FArchive& A,DWCombatCommit::FEntityLife& V){A<<V.Entity<<V.Life;}
template<class T,class F>void Items(FArchive& A,TArray<T>& V,F Fn){uint32 N=V.Num();A<<N;if(A.IsError()||N>MaxCount){A.SetError();return;}if(A.IsLoading())V.SetNum(N);for(auto& X:V){Fn(A,X);if(A.IsError())return;}}
void Formal(FArchive& A,FDWEnemyFormalSaveData& S)
{
    A<<S.Schema;Enum(A,S.Profile);A<<S.StableEnemyId;Vec(A,S.BirthLocation);Flag(A,S.bEngaged);Flag(A,S.bReturningHome);Flag(A,S.bPlayerDeathReturn);Flag(A,S.bDisengageCandidate);
    A<<S.DisengageElapsedSeconds<<S.AlertRemainingSeconds<<S.AttackCooldownRemainingSeconds;Enum(A,S.DropDecision);
    A<<S.FatalReceiptEpoch<<S.FatalReceiptId<<S.DeathEntityId<<S.DeathLife<<S.CreditedPlayerEntityId<<S.CreditedPlayerLife<<S.CoreDropDeathId<<S.PickupId;Name(A,S.CoreItemId);A<<S.CoreCount;
}
void Drop(FArchive& A,FDWCombatCoreDropSave& S)
{
    A<<S.Schema<<S.DeathId<<S.StableEnemyId;Enum(A,S.Profile);Enum(A,S.Decision);A<<S.FatalEpoch<<S.FatalReceipt<<S.EnemyEntity<<S.EnemyLife<<S.PlayerEntity<<S.PlayerLife<<S.PickupId;
    Name(A,S.ItemId);A<<S.Count;Vec(A,S.Location);A<<S.ForcedFallReceiptEpoch<<S.ForcedFallReceiptId;
}
void Enemy(FArchive& A,FDWCombatEnemyObjectSave& S)
{
    String(A,S.StableId);String(A,S.ActorPath);String(A,S.ClassPath);String(A,S.SourceNestId);Name(A,S.PersistentId);Transform(A,S.Transform);Entity(A,S.Life);
    A<<S.Health<<S.MaxHealth<<S.State<<S.Phase<<S.MovementMode<<S.CustomMovementMode<<S.AttackSerial<<S.VariantIndex;Enum(A,S.Profile);Formal(A,S.Formal);
    Flag(A,S.Dagger.bInitialized);Flag(A,S.Dagger.bInside);Flag(A,S.Dagger.bEquipped);A<<S.Dagger.GraceRemainingSeconds;
    auto& F=S.ForcedFall;A<<F.Schema<<F.StableEnemyId;Enum(A,F.Stage);A<<F.ReceiptEpoch<<F.ReceiptId<<F.PlayerEntity<<F.PlayerLife<<F.OriginalEnemyEntity<<F.OriginalEnemyLife;Flag(A,F.bHasStandablePoint);Vec(A,F.LastStandablePoint);
    Entity(A,S.DaggerPlayerLife);Entity(A,S.ObservedPlayerLife);Entity(A,S.MeleeTargetLife);A<<S.MeleeAction.Epoch<<S.MeleeAction.Id;
    String(A,S.TargetId);String(A,S.MeleeTargetId);String(A,S.AttackAnimationPath);
    Vec(A,S.Home);Vec(A,S.PatrolGoal);Vec(A,S.LockedDirection);Vec(A,S.KnockbackDirection);Vec(A,S.Velocity);
    A<<S.AttackAge<<S.PhaseRemaining<<S.AttackCooldown<<S.AlertRemaining<<S.PatrolRemaining<<S.PathRemaining;
    A<<S.CurrentWindup<<S.CurrentActive<<S.CurrentRecovery<<S.CurrentRange<<S.CurrentDamage;
    A<<S.KnockbackDistance<<S.KnockbackSeconds<<S.KnockbackSpeed<<S.KnockbackInitialDistance<<S.KnockbackRevision<<S.ObservedPlayerDamageRevision<<S.ObservationAge;
    A<<S.DeathCleanupRemaining<<S.LifeSpanRemaining;Flag(A,S.bDeathCleanupScheduled);
    Flag(A,S.bHasObservation);Flag(A,S.bRuntimeSpawned);Flag(A,S.bEnded);Flag(A,S.bAttackResolved);Flag(A,S.bHasAttackSnapshot);Flag(A,S.bFacingLocked);
    Flag(A,S.bKnockedBack);Flag(A,S.bAttackContinuesDuringKnockback);Flag(A,S.bNeedsChaseState);Flag(A,S.bHasPatrolGoal);Flag(A,S.bPlayerDeathReturn);
    Flag(A,S.bHidden);Flag(A,S.bActorTick);Flag(A,S.bCollision);Flag(A,S.bMeshTick);Flag(A,S.bHasPreDeathState);Flag(A,S.bPreDeathCollision);Flag(A,S.bPreDeathHidden);Flag(A,S.bPreDeathTick);Flag(A,S.bPreDeathMeshTick);
    A<<S.PreDeathMovementMode<<S.PreDeathCustomMovementMode;Items(A,S.LegacySlows,[](FArchive& R,FVector2D& V){R<<V.X<<V.Y;});
}
void Recovery(FArchive& A,FDWCombatRecoverySave& S)
{
    A<<S.Schema<<S.PlayerId<<S.Revision;Flag(A,S.bAwaitingRespawn);A<<S.CurrentDeathBagId;
    Flag(A,S.SafePoint.bActive);A<<S.SafePoint.PointId;String(A,S.SafePoint.MapPackage);Transform(A,S.SafePoint.Transform);
    Items(A,S.Bags,[](FArchive& R,FDWDeathBagSave& B){R<<B.Schema<<B.BagId<<B.PlayerId;String(R,B.MapPackage);Vec(R,B.Location);R<<B.Revision;Flag(R,B.bClaimed);
        Items(R,B.Contents,[](FArchive& Q,FDWDeathBagStack& C){Name(Q,C.ItemId);Q<<C.Original<<C.Remaining;});});
}
void Serialize(FArchive& A,FDWCombatWorldObjectsSave& S)
{
    uint32 M=Magic,V=Version;A<<M<<V<<S.Schema;if(M!=Magic||V!=Version||S.Schema!=1){A.SetError();return;}
    String(A,S.MapPackage);Items(A,S.Enemies,Enemy);Items(A,S.NestClocks,[](FArchive& R,FDWCombatNestClockSave& N){String(R,N.StableId);R<<N.SampledInterval;});Items(A,S.CoreDrops,Drop);Recovery(A,S.Recovery);
}
bool ValidDrop(const FDWCombatCoreDropSave& S)
{
    if(S.Schema!=1||!S.DeathId.IsValid()||!S.StableEnemyId.IsValid()||S.Location.ContainsNaN()||!S.FatalEpoch||!S.FatalReceipt||!S.EnemyEntity||!S.EnemyLife||
        S.Profile<EDWEnemyCombatProfile::MoldGrunt||S.Profile>EDWEnemyCombatProfile::DoughElite||(S.PlayerEntity==0)!=(S.PlayerLife==0)||(S.ForcedFallReceiptEpoch==0)!=(S.ForcedFallReceiptId==0))return false;
    const bool Eligible=(S.Profile==EDWEnemyCombatProfile::MoldGrunt||S.Profile==EDWEnemyCombatProfile::YeastGrunt)&&S.PlayerEntity;
    if(S.Decision==EDWEnemyCoreDropDecision::Ineligible)return !Eligible&&!S.PickupId.IsValid()&&S.ItemId.IsNone()&&S.Count==0;
    if(S.Decision==EDWEnemyCoreDropDecision::Failed)return Eligible&&!S.PickupId.IsValid()&&S.ItemId.IsNone()&&S.Count==0;
    if(S.Decision!=EDWEnemyCoreDropDecision::SpawnPending&&S.Decision!=EDWEnemyCoreDropDecision::Spawned&&S.Decision!=EDWEnemyCoreDropDecision::Claimed)return false;
    return Eligible&&S.PickupId.IsValid()&&S.Count>0&&S.ItemId==(S.Profile==EDWEnemyCombatProfile::MoldGrunt?FDWCombatItemRegistry::MoldCoreId():FDWCombatItemRegistry::YeastCoreId());
}
}
bool DWCombatWorldObjectsSave::Validate(const FDWCombatWorldObjectsSave& S,FString& Error)
{
    const auto Bad=[&](const TCHAR* M){Error=M;return false;};
    if(S.Schema!=1||!Text(S.MapPackage)||!FPackageName::IsValidLongPackageName(S.MapPackage)||S.Enemies.Num()>MaxCount||S.NestClocks.Num()>MaxCount||S.CoreDrops.Num()>MaxCount||!DWCombatRecovery::Validate(S.Recovery,Error))return Bad(TEXT("Invalid world object schema/map/recovery/count"));
    TSet<FString> Ids,Paths,Deaths,Receipts;TSet<uint64> Entities;TSet<FGuid> FormalIds,DeathIds,PickupIds;TMap<FGuid,const FDWCombatCoreDropSave*> Drops;
    for(const auto& D:S.CoreDrops){const FString Death=FString::Printf(TEXT("%llu/%llu/%llu"),D.FatalEpoch,D.EnemyEntity,D.EnemyLife),Receipt=FString::Printf(TEXT("%llu/%llu"),D.FatalEpoch,D.FatalReceipt);
        if(!ValidDrop(D)||DeathIds.Contains(D.DeathId)||Deaths.Contains(Death)||Receipts.Contains(Receipt)||(D.PickupId.IsValid()&&PickupIds.Contains(D.PickupId)))return Bad(TEXT("Invalid/duplicate core death record"));DeathIds.Add(D.DeathId);Deaths.Add(Death);Receipts.Add(Receipt);if(D.PickupId.IsValid())PickupIds.Add(D.PickupId);Drops.Add(D.DeathId,&D);}
    for(const auto& E:S.Enemies)
    {
        if(!Text(E.StableId)||!Text(E.ActorPath)||!Text(E.ClassPath)||!FSoftClassPath(E.ClassPath).IsValid()||!Text(E.SourceNestId,true)||!Pose(E.Transform)||!Life(E.Life,false)||Ids.Contains(E.StableId)||Entities.Contains(E.Life.Entity)||(!E.bRuntimeSpawned&&Paths.Contains(E.ActorPath))||
            !Fin(E.Health)||!Fin(E.MaxHealth)||E.MaxHealth<=0.f||E.Health<0.f||E.Health>E.MaxHealth||E.State>5||E.Phase>3||E.MovementMode>6||E.PreDeathMovementMode>6||E.AttackSerial<0||E.VariantIndex<INDEX_NONE||E.Profile>EDWEnemyCombatProfile::DoughElite||
            E.bRuntimeSpawned!=!E.SourceNestId.IsEmpty()||!Life(E.DaggerPlayerLife)||!Life(E.ObservedPlayerLife)||!Life(E.MeleeTargetLife)||!Text(E.TargetId,true)||!Text(E.MeleeTargetId,true)||!Text(E.AttackAnimationPath,true)||
            E.Home.ContainsNaN()||E.PatrolGoal.ContainsNaN()||E.LockedDirection.ContainsNaN()||E.KnockbackDirection.ContainsNaN()||E.Velocity.ContainsNaN()||
            !FDWDaggerAwareness::Validate(E.Dagger)||!FDWEnemyForcedFall::Validate(E.ForcedFall))return Bad(TEXT("Invalid enemy identity/geometry/state"));
        for(double V:{E.AttackAge,E.PhaseRemaining,E.AttackCooldown,E.AlertRemaining,E.PatrolRemaining,E.PathRemaining,E.CurrentWindup,E.CurrentActive,E.CurrentRecovery,E.CurrentRange,E.CurrentDamage,
            E.KnockbackDistance,E.KnockbackSeconds,E.KnockbackSpeed,E.KnockbackInitialDistance,E.ObservationAge,E.DeathCleanupRemaining,E.LifeSpanRemaining})if(!Nonnegative(V))return Bad(TEXT("Invalid enemy clock/damage"));
        if((!E.bDeathCleanupScheduled&&E.DeathCleanupRemaining!=0.)||(E.bDeathCleanupScheduled&&E.Health>0.f))return Bad(TEXT("Invalid corpse cleanup clock"));
        if((E.Health==0.f)!=(E.State==3)||(E.bEnded&&E.Health!=0.f)||(E.Health==0.f&&(E.Phase||E.bKnockedBack||E.bPlayerDeathReturn))||
            (!E.Phase&&(E.PhaseRemaining!=0.||E.MeleeAction.Id))||(E.Phase&&!E.bHasAttackSnapshot)||(E.MeleeAction.Epoch==0)!=(E.MeleeAction.Id==0)||
            (E.bKnockedBack&&(E.KnockbackSeconds<=0.||E.KnockbackDistance<=0.||E.KnockbackSpeed<=0.||!FMath::IsNearlyEqual(E.KnockbackDirection.SizeSquared(),1.,1.e-5)))||
            (E.bFacingLocked&&!FMath::IsNearlyEqual(E.LockedDirection.SizeSquared(),1.,1.e-5))||(!E.bHasObservation&&E.ObservationAge!=0.)||(E.ForcedFall.Stage!=EDWForcedFallStage::None&&!E.KnockbackRevision)||E.LegacySlows.Num()>MaxCount)return Bad(TEXT("Contradictory enemy phase/driver facts"));
        for(auto V:E.LegacySlows)if(!Fin(V.X)||V.X<0.||V.X>100.||!Nonnegative(V.Y))return Bad(TEXT("Invalid slow"));
        for(double V:{E.CurrentWindup,E.CurrentActive,E.CurrentRecovery,E.CurrentRange,E.CurrentDamage,E.KnockbackDistance,E.KnockbackSeconds,E.KnockbackSpeed,E.DeathCleanupRemaining,E.LifeSpanRemaining})if(V>MAX_flt)return Bad(TEXT("Enemy scalar cannot be represented by native float"));
        if(E.Profile!=EDWEnemyCombatProfile::Legacy)
        {
            const auto& F=E.Formal;if(F.Schema!=1||F.Profile!=E.Profile||!F.StableEnemyId.IsValid()||FormalIds.Contains(F.StableEnemyId)||F.BirthLocation!=E.Home||F.bPlayerDeathReturn!=E.bPlayerDeathReturn||
                (F.bEngaged&&F.bReturningHome)||!Nonnegative(F.DisengageElapsedSeconds)||(!F.bDisengageCandidate&&F.DisengageElapsedSeconds!=0.)||(F.bDisengageCandidate&&!F.bEngaged)||
                !Nonnegative(F.AlertRemainingSeconds)||!Nonnegative(F.AttackCooldownRemainingSeconds)||F.DropDecision>EDWEnemyCoreDropDecision::Claimed||
                !E.StableId.Equals(TEXT("enemy:")+F.StableEnemyId.ToString(EGuidFormats::Digits),ESearchCase::CaseSensitive))return Bad(TEXT("Invalid formal enemy memory"));
            if(F.DropDecision==EDWEnemyCoreDropDecision::Unresolved)
            {if(E.Health==0.f||F.CoreDropDeathId.IsValid()||F.FatalReceiptId||F.FatalReceiptEpoch||F.DeathEntityId||F.DeathLife||F.CreditedPlayerEntityId||F.CreditedPlayerLife||F.PickupId.IsValid()||!F.CoreItemId.IsNone()||F.CoreCount)return Bad(TEXT("Invalid unresolved drop"));}
            else
            {const auto* const* D=Drops.Find(F.CoreDropDeathId);if(E.Health!=0.f||!D||(*D)->StableEnemyId!=F.StableEnemyId||(*D)->Profile!=F.Profile||(*D)->Decision!=F.DropDecision||(*D)->FatalEpoch!=F.FatalReceiptEpoch||(*D)->FatalReceipt!=F.FatalReceiptId||(*D)->EnemyEntity!=F.DeathEntityId||(*D)->EnemyLife!=F.DeathLife||(*D)->PlayerEntity!=F.CreditedPlayerEntityId||(*D)->PlayerLife!=F.CreditedPlayerLife||(*D)->PickupId!=F.PickupId||(*D)->ItemId!=F.CoreItemId||(*D)->Count!=F.CoreCount)return Bad(TEXT("Enemy and core drop disagree"));}
            FormalIds.Add(F.StableEnemyId);
        }
        else if(E.Formal.Schema||E.MeleeAction.Id||E.ForcedFall.Stage!=EDWForcedFallStage::None)return Bad(TEXT("Legacy enemy has formal identity"));
        Ids.Add(E.StableId);Entities.Add(E.Life.Entity);if(!E.bRuntimeSpawned)Paths.Add(E.ActorPath);
    }
    for(const auto& D:S.CoreDrops)if(!FormalIds.Contains(D.StableEnemyId))return Bad(TEXT("Destroyed formal enemy has no world tombstone"));
    for(const auto& N:S.NestClocks){if(!Text(N.StableId)||!N.StableId.StartsWith(TEXT("nest:"),ESearchCase::CaseSensitive)||Ids.Contains(N.StableId)||!Fin(N.SampledInterval)||N.SampledInterval<=0.)return Bad(TEXT("Invalid nest clock"));Ids.Add(N.StableId);}
    Error.Reset();return true;
}
bool DWCombatWorldObjectsSave::ValidateLegacy(const TArray<FDWPersistedActorState>& S,FString& Error)
{
    TSet<FString> Ids;for(const auto& A:S)
    {const bool Resource=A.ActorId.StartsWith(TEXT("resource:"),ESearchCase::CaseSensitive),Nest=A.ActorId.StartsWith(TEXT("nest:"),ESearchCase::CaseSensitive);
        if(S.Num()>MaxCount||(!Resource&&!Nest)||!Text(A.ActorId)||Ids.Contains(A.ActorId)||!Text(A.ActorPath)||!A.bHasTransform||!Pose(A.Transform)||A.bRuntimeSpawned||!A.OwnerId.IsEmpty()||!A.OwnerPath.IsEmpty()||!A.ActorClass.IsNull()||!Fin(A.Health)||A.Health<0.f||!Nonnegative(A.SpawnProgress)||(Resource&&(A.Health>MAX_int32||A.Health!=FMath::FloorToDouble(A.Health)||A.SpawnProgress!=0.f))||(Nest&&A.bDestroyed!=(A.Health==0.f)))
        {Error=TEXT("Invalid/duplicate legacy resource/nest record");return false;}Ids.Add(A.ActorId);}
    Error.Reset();return true;
}
bool DWCombatWorldObjectsSave::Encode(const FDWCombatWorldObjectsSave& S,TArray<uint8>& Out,FString& Error)
{if(!Validate(S,Error))return false;auto C=S;TArray<uint8>B;FMemoryWriter A(B,true);Serialize(A,C);if(A.IsError()||B.Num()>MaxBytes){Error=TEXT("World object byte limit");return false;}Out=MoveTemp(B);return true;}
bool DWCombatWorldObjectsSave::Decode(const TArray<uint8>& B,FDWCombatWorldObjectsSave& Out,FString& Error)
{if(B.IsEmpty()||B.Num()>MaxBytes){Error=TEXT("World object blob size");return false;}FDWCombatWorldObjectsSave C;FMemoryReader A(B,true);Serialize(A,C);if(A.IsError()||A.Tell()!=A.TotalSize()){Error=TEXT("World object truncated/corrupt/trailing bytes");return false;}if(!Validate(C,Error))return false;Out=MoveTemp(C);return true;}
