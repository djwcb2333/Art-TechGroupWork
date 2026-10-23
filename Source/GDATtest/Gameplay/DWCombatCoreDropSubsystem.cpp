#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatCorePickup.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatRules.h"
#include "DWEnemyCharacter.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"

namespace DWCombatCoreDrop
{
static bool Ground(EDWEnemyCoreDropDecision D)
{return D==EDWEnemyCoreDropDecision::SpawnPending||D==EDWEnemyCoreDropDecision::Spawned;}
static bool ValidSave(const FDWCombatCoreDropSave& S)
{
    if(S.Schema!=1||!S.DeathId.IsValid()||!S.StableEnemyId.IsValid()||S.Location.ContainsNaN()||
        !S.FatalEpoch||!S.FatalReceipt||!S.EnemyEntity||!S.EnemyLife||
        S.Profile<EDWEnemyCombatProfile::MoldGrunt||S.Profile>EDWEnemyCombatProfile::DoughElite||
        (S.PlayerEntity==0)!=(S.PlayerLife==0)||
        (S.ForcedFallReceiptEpoch==0)!=(S.ForcedFallReceiptId==0)||
        (S.ForcedFallReceiptId&&!S.PlayerEntity))return false;
    const bool Eligible=(S.Profile==EDWEnemyCombatProfile::MoldGrunt||S.Profile==EDWEnemyCombatProfile::YeastGrunt)&&S.PlayerEntity;
    if(S.Decision==EDWEnemyCoreDropDecision::Ineligible)
        return !Eligible&&!S.PickupId.IsValid()&&S.ItemId.IsNone()&&S.Count==0;
    if(S.Decision==EDWEnemyCoreDropDecision::Failed)
        return Eligible&&!S.PickupId.IsValid()&&S.ItemId.IsNone()&&S.Count==0;
    if(!Ground(S.Decision)&&S.Decision!=EDWEnemyCoreDropDecision::Claimed)return false;
    const FName Expected=S.Profile==EDWEnemyCombatProfile::MoldGrunt?FDWCombatItemRegistry::MoldCoreId():FDWCombatItemRegistry::YeastCoreId();
    return Eligible&&S.PickupId.IsValid()&&S.ItemId==Expected&&S.Count>0;
}
static bool SameDrop(const FDWCombatCoreDropSave& D,const FDWEnemyFormalSaveData& S)
{
    return D.StableEnemyId==S.StableEnemyId&&D.Profile==S.Profile&&D.DeathId==S.CoreDropDeathId&&
        D.Decision==S.DropDecision&&D.FatalEpoch==S.FatalReceiptEpoch&&D.FatalReceipt==S.FatalReceiptId&&
        D.EnemyEntity==S.DeathEntityId&&D.EnemyLife==S.DeathLife&&D.PlayerEntity==S.CreditedPlayerEntityId&&
        D.PlayerLife==S.CreditedPlayerLife&&D.PickupId==S.PickupId&&D.ItemId==S.CoreItemId&&D.Count==S.CoreCount;
}
}

bool UDWCombatCoreDropSubsystem::DoesSupportWorldType(EWorldType::Type Type)const
{return Type==EWorldType::Game||Type==EWorldType::PIE;}
bool UDWCombatCoreDropSubsystem::Operational()const
{return IsInGameThread()&&!bStopping&&GetWorld()&&!GetWorld()->bIsTearingDown&&GetWorld()->GetNetMode()!=NM_Client;}
UDWCombatCoreDropSubsystem* UDWCombatCoreDropSubsystem::Get(const UObject* Context)
{auto* W=Context?Context->GetWorld():nullptr;auto* S=W?W->GetSubsystem<UDWCombatCoreDropSubsystem>():nullptr;return S&&S->Operational()?S:nullptr;}
void UDWCombatCoreDropSubsystem::Deinitialize()
{bStopping=true;Records.Reset();Super::Deinitialize();}
TStatId UDWCombatCoreDropSubsystem::GetStatId()const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UDWCombatCoreDropSubsystem,STATGROUP_Tickables);}

bool UDWCombatCoreDropSubsystem::PrepareFatal(ADWEnemyCharacter& Enemy,DWCombatCommit::FReceiptId Receipt,
    DWCombatCommit::FEntityLife EnemyLife,DWCombatCommit::FEntityLife PlayerLife,FDWPreparedCoreDrop& Out,const FVector* ForcedFallPoint,DWCombatCommit::FReceiptId ForcedFallReceipt)
{
    Out={};
    if(ForcedFallPoint?(ForcedFallPoint->ContainsNaN()||!ForcedFallReceipt.Epoch||!ForcedFallReceipt.Id||!PlayerLife.Entity||!PlayerLife.Life):
        (ForcedFallReceipt.Epoch!=0||ForcedFallReceipt.Id!=0))return false;
    if(!Operational()||Revision==MAX_uint64||!Enemy.IsFormalCombatReady()||Enemy.GetWorld()!=GetWorld()||
        Enemy.IsActorBeingDestroyed()||!Enemy.FormalEnemyId.IsValid()||
        Enemy.FormalCoreDrop.Decision!=EDWEnemyCoreDropDecision::Unresolved||Records.Num()==MAX_int32||
        !FMath::IsFinite(Enemy.FormalCoreDropConfig.PickupRadiusCm)||Enemy.FormalCoreDropConfig.PickupRadiusCm<=0.||
        Enemy.FormalCoreDropConfig.PickupRadiusCm>MAX_flt)return false;
    FDWPreparedCoreDrop P;P.Revision=Revision;P.Enemy=&Enemy;
    // This draw is only reached for a valid pending fatal commit. The resulting failure is a terminal fact too.
    const bool bEligible=(Enemy.CombatProfile==EDWEnemyCombatProfile::MoldGrunt||Enemy.CombatProfile==EDWEnemyCombatProfile::YeastGrunt)&&PlayerLife.Entity&&PlayerLife.Life;
    const double Sample=bEligible?FMath::FRand():0.; // Other factions/environment deaths never draw.
    if(!DWEnemyFormal::DecideCoreDrop(P.EnemyRecord,Enemy.CombatProfile,Enemy.FormalCoreDropConfig,
        Receipt,EnemyLife,PlayerLife,Sample,FGuid::NewGuid()))return false;
    P.EnemyRecord.DeathId=FGuid::NewGuid();
    auto& S=P.Save;S.DeathId=P.EnemyRecord.DeathId;S.StableEnemyId=Enemy.FormalEnemyId;S.Profile=Enemy.CombatProfile;
    S.Decision=P.EnemyRecord.Decision;S.FatalEpoch=Receipt.Epoch;S.FatalReceipt=Receipt.Id;
    S.EnemyEntity=EnemyLife.Entity;S.EnemyLife=EnemyLife.Life;S.PlayerEntity=PlayerLife.Entity;S.PlayerLife=PlayerLife.Life;
    S.PickupId=P.EnemyRecord.PickupId;S.ItemId=P.EnemyRecord.CoreItemId;S.Count=P.EnemyRecord.Count;S.Location=ForcedFallPoint?*ForcedFallPoint:Enemy.GetActorLocation();
    S.ForcedFallReceiptEpoch=ForcedFallReceipt.Epoch;S.ForcedFallReceiptId=ForcedFallReceipt.Id;
    if(!DWCombatCoreDrop::ValidSave(S)||Records.Contains(S.DeathId))return false;
    Records.Reserve(Records.Num()+1);P.bValid=true;Out=MoveTemp(P);return true;
}
void UDWCombatCoreDropSubsystem::CommitFatalUnchecked(FDWPreparedCoreDrop&& P)
{
    check(P.bValid&&P.Revision==Revision&&Operational());
    auto* Enemy=P.Enemy.Get();check(Enemy);
    FRecord Record;Record.Save=P.Save;Record.Origin=Enemy;Record.PickupRadiusCm=Enemy->FormalCoreDropConfig.PickupRadiusCm;
    Record.bMayMaterialize=DWCombatCoreDrop::Ground(P.Save.Decision);
    Enemy->FormalCoreDrop=P.EnemyRecord;Records.Add(P.Save.DeathId,MoveTemp(Record));++Revision;P.bValid=false;
}

void UDWCombatCoreDropSubsystem::Tick(float DeltaTime)
{
    if(!Operational()||bUpdating||!GetWorld()->HasBegunPlay()||GetWorld()->IsPaused()||!FMath::IsFinite(DeltaTime)||DeltaTime<=0.f)return;
    TGuardValue<bool> Guard(bUpdating,true);TArray<FGuid> Ids;Records.GetKeys(Ids);
    for(const auto& Id:Ids){if(!Operational())return;Materialize(Id);}
}
void UDWCombatCoreDropSubsystem::Materialize(FGuid Id)
{
    FRecord* Record=Records.Find(Id);
    if(!Record||!Record->bMayMaterialize||Record->Physical.IsValid()||!DWCombatCoreDrop::Ground(Record->Save.Decision)||Revision==MAX_uint64)return;
    // A committed death belongs to this world record, not to the continued existence of its corpse.
    // EndPlay by itself cannot create this record; only a committed fatal receipt or validated load can.
    const FVector Position=Record->Save.Location;const TWeakObjectPtr<UWorld> World(GetWorld());
    Record->bMayMaterialize=false; // Reentrant spawning never sees an available second creation.
    auto* Actor=GetWorld()->SpawnActorDeferred<ADWCombatCorePickup>(ADWCombatCorePickup::StaticClass(),FTransform(Position),nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Operational()||GetWorld()!=World.Get()||!GetWorld()->HasBegunPlay())
    {
        if(World.IsValid()&&!World->bIsTearingDown&&IsValid(Actor)&&!Actor->IsActorBeingDestroyed()&&Actor->GetWorld()==World.Get())Actor->Destroy();
        return;
    }
    Record=Records.Find(Id);
    if(!Record||Revision==MAX_uint64||!DWCombatCoreDrop::Ground(Record->Save.Decision)||Record->Physical.IsValid())
    {if(IsValid(Actor)&&!Actor->IsActorBeingDestroyed())Actor->Destroy();return;}
    if(!IsValid(Actor)){Record->bMayMaterialize=true;return;}
    Actor->DeathId=Id;Actor->PickupId=Record->Save.PickupId;Actor->ItemId=Record->Save.ItemId;Actor->Count=Record->Save.Count;
    Actor->PickupRadiusCm=Record->PickupRadiusCm;Actor->PickupRange->SetSphereRadius(static_cast<float>(Record->PickupRadiusCm),false);Actor->bBound=true;
    Record->Physical=Actor;Record->Save.Decision=EDWEnemyCoreDropDecision::Spawned;++Revision;
    if(auto* E=Record->Origin.Get();IsValid(E)&&!E->IsActorBeingDestroyed()&&E->GetWorld()==GetWorld()&&E->FormalCoreDrop.DeathId==Id)E->FormalCoreDrop.Decision=EDWEnemyCoreDropDecision::Spawned;
    Actor->FinishSpawning(FTransform(Position));
    if(!Operational()||GetWorld()!=World.Get()||!GetWorld()->HasBegunPlay())
    {
        if(World.IsValid()&&!World->bIsTearingDown&&IsValid(Actor)&&!Actor->IsActorBeingDestroyed()&&Actor->GetWorld()==World.Get())Actor->Destroy();
        return;
    }
    Record=Records.Find(Id);
    if(!Record||Record->Physical.Get()!=Actor||!DWCombatCoreDrop::Ground(Record->Save.Decision))
    {if(IsValid(Actor)&&!Actor->IsActorBeingDestroyed())Actor->Destroy();}
}
bool UDWCombatCoreDropSubsystem::IsPhysicalCurrent(const ADWCombatCorePickup& P)const
{
    const auto* R=Records.Find(P.DeathId);
    return Operational()&&R&&R->Save.Decision==EDWEnemyCoreDropDecision::Spawned&&R->Physical.Get()==&P&&
        P.bBound&&!P.bClaimed&&P.PickupId==R->Save.PickupId&&P.ItemId==R->Save.ItemId&&P.Count==R->Save.Count;
}
void UDWCombatCoreDropSubsystem::PhysicalEnded(ADWCombatCorePickup& P)
{
    if(auto* R=Records.Find(P.DeathId);R&&R->Physical.Get()==&P)
    {R->Physical.Reset();R->bMayMaterialize=false;}
    // EndPlay/streaming/destruction never constitutes a death or grants permission to respawn.
}
bool UDWCombatCoreDropSubsystem::FindDrop(FGuid Id,FDWCombatCoreDropSave& Out)const
{if(const auto* R=Records.Find(Id)){Out=R->Save;return true;}return false;}
ADWCombatCorePickup* UDWCombatCoreDropSubsystem::FindPhysicalPickup(FGuid Id)const
{for(const auto& Pair:Records)if(Pair.Value.Save.PickupId==Id)return Pair.Value.Physical.Get();return nullptr;}
bool UDWCombatCoreDropSubsystem::CaptureSave(TArray<FDWCombatCoreDropSave>& Out,FString& Error)const
{
    if(!Operational()||bUpdating){Error=TEXT("Core snapshot requested during lifecycle/materialization transition.");return false;}
    TArray<FDWCombatCoreDropSave> Candidate;Candidate.Reserve(Records.Num());
    for(const auto& Pair:Records)
    {auto S=Pair.Value.Save;if(const auto* P=Pair.Value.Physical.Get())S.Location=P->GetActorLocation();
     if(!DWCombatCoreDrop::ValidSave(S)){Error=TEXT("Invalid core drop snapshot; output unchanged.");return false;}Candidate.Add(S);}
    Out=MoveTemp(Candidate);Error.Reset();return true;
}
bool UDWCombatCoreDropSubsystem::RestoreBeforeBeginPlay(const TArray<FDWCombatCoreDropSave>& Saved,const FDWCombatRules& Rules,FString& Error)
{
    if(!Operational()||bUpdating||GetWorld()->HasBegunPlay()||!Records.IsEmpty()||Revision==MAX_uint64)
    {Error=TEXT("Core restore is only allowed into an empty, not-yet-playing world; D22 owns live whole-world restore.");return false;}
    FDWEnemyCoreDropConfig C;if(!DWEnemyFormal::BuildCoreDropConfig(Rules,C,Error))return false;
    if(!FMath::IsFinite(C.PickupRadiusCm)||C.PickupRadiusCm<=0.||C.PickupRadiusCm>MAX_flt)
    {Error=TEXT("Core pickup radius cannot be represented by the engine component.");return false;}
    TMap<FGuid,FRecord> Candidate;TSet<FGuid> Pickups;TSet<FString> Deaths,Receipts;
    for(const auto& S:Saved)
    {
        const FString Death=FString::Printf(TEXT("%llu/%llu/%llu"),S.FatalEpoch,S.EnemyEntity,S.EnemyLife);
        const FString Receipt=FString::Printf(TEXT("%llu/%llu"),S.FatalEpoch,S.FatalReceipt);
        if(!DWCombatCoreDrop::ValidSave(S)||Candidate.Contains(S.DeathId)||Deaths.Contains(Death)||Receipts.Contains(Receipt)||(S.PickupId.IsValid()&&Pickups.Contains(S.PickupId)))
        {Error=TEXT("Invalid or duplicate core death/pickup identity; no records imported.");return false;}
        FRecord R;R.Save=S;R.PickupRadiusCm=C.PickupRadiusCm;R.bRestored=true;R.bMayMaterialize=DWCombatCoreDrop::Ground(S.Decision);
        Candidate.Add(S.DeathId,MoveTemp(R));Deaths.Add(Death);Receipts.Add(Receipt);if(S.PickupId.IsValid())Pickups.Add(S.PickupId);
    }
    Records=MoveTemp(Candidate);++Revision;Error.Reset();return true;
}
bool UDWCombatCoreDropSubsystem::ValidateEnemySavedDrop(const FDWEnemyFormalSaveData& S,float Health)const
{
    if(!Operational())return false;
    if(S.DropDecision==EDWEnemyCoreDropDecision::Unresolved)
        return !S.CoreDropDeathId.IsValid()&&!S.PickupId.IsValid()&&S.CoreItemId.IsNone()&&S.CoreCount==0&&
            !S.FatalReceiptEpoch&&!S.FatalReceiptId&&!S.DeathEntityId&&!S.DeathLife&&!S.CreditedPlayerEntityId&&!S.CreditedPlayerLife;
    const auto* R=Records.Find(S.CoreDropDeathId);
    return Health==0.f&&R&&DWCombatCoreDrop::SameDrop(R->Save,S);
}
bool UDWCombatCoreDropSubsystem::BindRestoredEnemy(ADWEnemyCharacter& E,const FDWEnemyFormalSaveData& S)
{
    if(!Operational()||!ValidateEnemySavedDrop(S,E.Health))return false;
    if(auto* R=Records.Find(S.CoreDropDeathId))R->Origin=&E;
    return true;
}
