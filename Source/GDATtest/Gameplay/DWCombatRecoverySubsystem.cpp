#include "DWCombatRecoverySubsystem.h"
#include "DWCombatRecoveryActors.h"
#include "DWCombatRecoveryTransaction.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UDWCombatRecoverySubsystem* UDWCombatRecoverySubsystem::Get(const UObject* C)
{auto* W=C?C->GetWorld():nullptr;return W&&!W->bIsTearingDown?W->GetSubsystem<UDWCombatRecoverySubsystem>():nullptr;}
bool UDWCombatRecoverySubsystem::DoesSupportWorldType(EWorldType::Type T)const
{return T==EWorldType::Game||T==EWorldType::PIE;}
TStatId UDWCombatRecoverySubsystem::GetStatId()const
{RETURN_QUICK_DECLARE_CYCLE_STAT(UDWCombatRecoverySubsystem,STATGROUP_Tickables);}
bool UDWCombatRecoverySubsystem::Operational()const
{return IsInGameThread()&&!bStopping&&GetWorld()&&!GetWorld()->bIsTearingDown&&UDWCombatAuthoritySubsystem::Get(this);}
FString UDWCombatRecoverySubsystem::MapPackage(const UWorld* W)
{
    if(!W)return {};FString Result=W->GetOutermost()->GetName();
    if(!W->StreamingLevelsPrefix.IsEmpty())Result.ReplaceInline(*W->StreamingLevelsPrefix,TEXT(""),ESearchCase::CaseSensitive);
    return Result;
}
bool UDWCombatRecoverySubsystem::EnsurePlayer(ADWPlayerCharacter& P)
{
    if(!Operational()||!IsValid(&P)||P.IsActorBeingDestroyed()||P.GetWorld()!=GetWorld()||!P.HasCombatVitalsReady())return false;
    if(bInitialized)return BoundPlayer.Get()==&P;
    FDWCombatRecoverySave Fresh;Fresh.Schema=1;Fresh.PlayerId=FGuid::NewGuid();FString Error;
    if(!DWCombatRecovery::Validate(Fresh,Error))return false;
    State=MoveTemp(Fresh);BoundPlayer=&P;bInitialized=true;return true;
}
bool UDWCombatRecoverySubsystem::ActivateSafePoint(ADWPlayerCharacter& P,const ADWCombatSafePoint& Point)
{
    if(!P.IsCombatActionAllowed()||!Point.HasActorBegunPlay()||Point.IsActorBeingDestroyed()||Point.GetWorld()!=GetWorld()||
        !Point.StablePointId.IsValid()||!FMath::IsFinite(Point.ActivationRadiusCm)||Point.ActivationRadiusCm<=0.||
        FVector::DistSquared(P.GetActorLocation(),Point.GetActorLocation())>FMath::Square(Point.ActivationRadiusCm)||!EnsurePlayer(P)||
        State.bAwaitingRespawn||State.Revision==MAX_uint64)return false;
    // Duplicate authored IDs are ambiguous and fail closed, not first-actor-wins.
    for(TActorIterator<ADWCombatSafePoint> It(GetWorld());It;++It)
        if(*It!=&Point&&It->StablePointId==Point.StablePointId&&!It->IsActorBeingDestroyed())return false;
    auto Candidate=State;Candidate.SafePoint.bActive=true;Candidate.SafePoint.PointId=Point.StablePointId;
    Candidate.SafePoint.MapPackage=MapPackage(GetWorld());Candidate.SafePoint.Transform=Point.GetActorTransform();++Candidate.Revision;
    FString Error;if(!DWCombatRecovery::Validate(Candidate,Error))return false;
    State=MoveTemp(Candidate);return true;
}
bool UDWCombatRecoverySubsystem::CanRespawn(const ADWPlayerCharacter& P)const
{
    if(!Operational()||BoundPlayer.Get()!=&P||!bInitialized||!State.bAwaitingRespawn||!State.SafePoint.bActive||
        State.SafePoint.MapPackage!=MapPackage(GetWorld())||P.IsActorBeingDestroyed()||!P.IsDead()||!P.HasCombatVitalsReady()||
        State.Revision==MAX_uint64||UDWWorldEventSubsystem::IsPlaying(&P))return false;
    const auto* GI=P.GetGameInstance<UDWGameInstance>();
    if(!GI||GI->IsPlayerRestorePending())return false;
    const auto* Loading=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();
    return !Loading||!Loading->IsTransitionActive();
}
bool UDWCombatRecoverySubsystem::RequestRespawn(ADWPlayerCharacter& P)
{return CanRespawn(P)&&FDWCombatRecoveryTransaction::Respawn(P);}
bool UDWCombatRecoverySubsystem::BindNewPlayer(ADWPlayerCharacter& P)
{return !P.IsDead()&&!bRestoredCold&&EnsurePlayer(P);}
bool UDWCombatRecoverySubsystem::CaptureSave(FDWCombatRecoverySave& Out,FString& Error)const
{
    if(!Operational()||!bInitialized||bPlacementPending){Error=TEXT("Recovery state is uninitialized or respawn placement has not finished.");return false;}
    if(!DWCombatRecovery::Validate(State,Error))return false;Out=State;return true;
}
bool UDWCombatRecoverySubsystem::RestoreBeforeBeginPlay(const FDWCombatRecoverySave& Saved,FString& Error)
{
    if(!Operational()||bInitialized||GetWorld()->HasBegunPlay())
    {Error=TEXT("Recovery restore requires a fresh cold world and atomic D22 aggregate adoption.");return false;}
    for(TActorIterator<ADWPlayerCharacter> It(GetWorld());It;++It)if(It->HasActorBegunPlay())
    {Error=TEXT("A live player cannot be independently rewound with recovery data.");return false;}
    if(!DWCombatRecovery::Validate(Saved,Error))return false;
    State=Saved;bInitialized=bRestoredCold=true;return true;
}
bool UDWCombatRecoverySubsystem::BindRestoredPlayer(ADWPlayerCharacter& P)
{
    if(!Operational()||!bRestoredCold||BoundPlayer.IsValid()||P.GetWorld()!=GetWorld()||P.IsActorBeingDestroyed()||
        !P.HasCombatVitalsReady()||P.IsDead()!=State.bAwaitingRespawn)return false;
    BoundPlayer=&P;bRestoredCold=false;return true;
}
bool UDWCombatRecoverySubsystem::FindBag(FGuid Id,FDWDeathBagSave& Out)const
{if(!Operational()||!bInitialized)return false;for(const auto& B:State.Bags)if(B.BagId==Id){Out=B;return true;}return false;}
ADWCombatDeathBag* UDWCombatRecoverySubsystem::FindPhysicalBag(FGuid Id)const
{const auto* P=Physical.Find(Id);return P?P->Get():nullptr;}
bool UDWCombatRecoverySubsystem::IsPhysicalCurrent(const ADWCombatDeathBag& P)const
{
    FDWDeathBagSave Bag;return Operational()&&P.bBound&&FindPhysicalBag(P.BagId)==&P&&!P.IsActorBeingDestroyed()&&
        FindBag(P.BagId,Bag)&&!Bag.bClaimed&&Bag.MapPackage==MapPackage(GetWorld())&&P.GetActorLocation().Equals(Bag.Location,0.);
}
void UDWCombatRecoverySubsystem::Materialize(FGuid Id)
{
    FDWDeathBagSave B;if(!Operational()||GetWorld()->IsPaused()||!FindBag(Id,B)||B.bClaimed||B.MapPackage!=MapPackage(GetWorld())||FindPhysicalBag(Id))return;
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* P=GetWorld()->SpawnActor<ADWCombatDeathBag>(ADWCombatDeathBag::StaticClass(),B.Location,FRotator::ZeroRotator,Params);
    FDWDeathBagSave Current;
    if(!Operational()||!FindBag(Id,Current)||Current.bClaimed||FindPhysicalBag(Id)||!IsValid(P)||P->IsActorBeingDestroyed())
    {if(IsValid(P)&&P->GetWorld()&&!P->GetWorld()->bIsTearingDown)P->Destroy();return;}
    P->BagId=Id;P->bBound=true;Physical.Add(Id,P);
}
void UDWCombatRecoverySubsystem::Tick(float Dt)
{
    if(!Operational()||bUpdating||GetWorld()->IsPaused()||!bInitialized||!FMath::IsFinite(Dt)||Dt<0.f)return;
    TGuardValue<bool> Guard(bUpdating,true);TArray<FGuid> Pending;
    for(const auto& B:State.Bags)if(!B.bClaimed&&B.MapPackage==MapPackage(GetWorld())&&!FindPhysicalBag(B.BagId))Pending.Add(B.BagId);
    for(const auto Id:Pending){if(!Operational()||GetWorld()->IsPaused())return;Materialize(Id);}
}
void UDWCombatRecoverySubsystem::Deinitialize()
{bStopping=true;Physical.Reset();BoundPlayer.Reset();Super::Deinitialize();}
