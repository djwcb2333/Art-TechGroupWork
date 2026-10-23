#include "DWWorldEvent.h"
#include "DWGameplayCinematic.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

bool UDWWorldEventSubsystem::RegisterEvent(UDWWorldEventComponent* E)
{
 if(!E||E->EventId.IsNone())return false;
 if(auto* Existing=Events.Find(E->EventId);Existing&&Existing->IsValid()&&Existing->Get()!=E)
 {UE_LOG(LogTemp,Error,TEXT("DW World Event duplicate ID: %s"),*E->EventId.ToString());return false;}
 Events.Add(E->EventId,E);return true;
}
void UDWWorldEventSubsystem::UnregisterEvent(UDWWorldEventComponent* E)
{for(auto It=Events.CreateIterator();It;++It)if(It.Value().Get()==E)It.RemoveCurrent();}
void UDWWorldEventSubsystem::Commit(FName Id){if(!Id.IsNone())Completed.Add(Id);}
TArray<FName> UDWWorldEventSubsystem::ExportCompleted()const
{TArray<FName> Result=Completed.Array();Result.Sort(FNameLexicalLess());return Result;}
void UDWWorldEventSubsystem::ImportCompleted(const TArray<FName>& Ids)
{
 Completed.Reset();for(FName Id:Ids)if(!Id.IsNone())Completed.Add(Id);
 TArray<TWeakObjectPtr<UDWWorldEventComponent>> Snapshot;Events.GenerateValueArray(Snapshot);
 for(auto E:Snapshot)if(E.IsValid())E->RestoreState(Completed.Contains(E->EventId));
}

bool FDWWorldEventRestorePlan::CanCommit(FString& Error)const
{
 const auto* S=Owner.Get();
 if(!S){Error=TEXT("World-event restore owner expired.");return false;}
 return S->CanCommitCompletedFacts(*this,Error);
}
bool UDWWorldEventSubsystem::PrepareCompletedFacts(const TArray<FName>& Ids,FDWWorldEventRestorePlan& Out,FString& Error)
{
 FDWWorldEventRestorePlan Plan;Plan.Owner=this;Plan.World=GetWorld();Plan.InitialActive=Active;
 if(!IsValid(this)||!Plan.World.IsValid()||GetWorld()->bIsTearingDown||Out.bAdopted)
 {Error=TEXT("World-event restore has no current world or an already adopted output.");return false;}
 for(FName Id:Ids)
 {
  if(Id.IsNone()||Plan.PreparedCompleted.Contains(Id))
  {Error=TEXT("Completed world-event identities must be nonempty and unique.");return false;}
  Plan.PreparedCompleted.Add(Id);
 }
 Plan.BeforeIds=ExportCompleted();Plan.TargetIds=Plan.PreparedCompleted.Array();Plan.TargetIds.Sort(FNameLexicalLess());
 for(const auto& Pair:Events)
 {
  auto* E=Pair.Value.Get();
  if(!IsValid(E)||!E->bRegistered||E->RegisteredId!=Pair.Key||E->EventId!=Pair.Key||
     E->GetWorld()!=GetWorld()||!IsValid(E->GetOwner())||E->GetOwner()->IsActorBeingDestroyed())
  {Error=TEXT("A registered world event is invalid, renamed or leaving the world.");return false;}
  FDWWorldEventRestorePlan::FEntry Entry;Entry.Id=Pair.Key;Entry.Component=E;Entry.Actor=E->GetOwner();
  Entry.bWasCompleted=E->bCompleted;Entry.bWasRunning=E->bRunning;Entry.bHasBegunPlay=E->HasBegunPlay();
  Entry.bTargetCompleted=Plan.PreparedCompleted.Contains(Pair.Key);Plan.Entries.Add(Entry);
 }
 Plan.Entries.Sort([](const auto& A,const auto& B){return FNameLexicalLess()(A.Id,B.Id);});
 Plan.bPrepared=true;
 if(!CanCommitCompletedFacts(Plan,Error))return false;
 Out=MoveTemp(Plan);Error.Reset();return true;
}
bool UDWWorldEventSubsystem::CheckCompletedFactsPlan(const FDWWorldEventRestorePlan& Plan,bool bAdopted,FString& Error)const
{
 if(!Plan.bPrepared||Plan.bAdopted!=bAdopted||Plan.Owner.Get()!=this||!IsValid(this)||
    !Plan.World.IsValid()||GetWorld()!=Plan.World.Get()||GetWorld()->bIsTearingDown||
    Active!=Plan.InitialActive||Events.Num()!=Plan.Entries.Num())
 {Error=TEXT("World-event world, active cinematic or registration set changed.");return false;}
 const auto& Expected=bAdopted?Plan.TargetIds:Plan.BeforeIds;
 if(Completed.Num()!=Expected.Num())
 {Error=TEXT("World-event completed ledger changed.");return false;}
 for(FName Id:Expected)if(!Completed.Contains(Id))
 {Error=TEXT("World-event completed identity changed.");return false;}
 for(const auto& Entry:Plan.Entries)
 {
  const auto* Registered=Events.Find(Entry.Id);const auto* E=Entry.Component.Get();const auto* Actor=Entry.Actor.Get();
  if(!Registered||Registered->Get()!=E||!IsValid(E)||!IsValid(Actor)||Actor->IsActorBeingDestroyed()||
     E->GetOwner()!=Actor||E->GetWorld()!=Plan.World.Get()||!E->bRegistered||
     E->RegisteredId!=Entry.Id||E->EventId!=Entry.Id||E->HasBegunPlay()!=Entry.bHasBegunPlay||
     E->bCompleted!=(bAdopted?Entry.bTargetCompleted:Entry.bWasCompleted)||E->bRunning!=(bAdopted?false:Entry.bWasRunning))
  {Error=TEXT("World-event component identity or native facts changed.");return false;}
 }
 Error.Reset();return true;
}
bool UDWWorldEventSubsystem::CanCommitCompletedFacts(const FDWWorldEventRestorePlan& Plan,FString& Error)const
{return CheckCompletedFactsPlan(Plan,false,Error);}
void UDWWorldEventSubsystem::AdoptCompletedFacts(FDWWorldEventRestorePlan& Plan)
{
 FString Error;checkf(CanCommitCompletedFacts(Plan,Error),TEXT("%s"),*Error);
 // All allocations and lookups were prepared. Adopt every event before the first notice.
 Completed=MoveTemp(Plan.PreparedCompleted);
 for(const auto& Entry:Plan.Entries)
 {auto* E=Entry.Component.Get();E->bRunning=false;E->bCompleted=Entry.bTargetCompleted;}
 Plan.bAdopted=true;
}
bool UDWWorldEventSubsystem::PublishCompletedFacts(FDWWorldEventRestorePlan& Plan,UDWCombatAuthoritySubsystem* Authority,
 uint64 ExpectedEpoch,ADWPlayerCharacter* Player,const DWCombatCommit::FEntityLife& ExpectedPlayerLife,FString& Error)
{
 if(!Plan.bAdopted||Plan.bPublishStarted)
 {Error=TEXT("World-event facts must be adopted and may be published only once.");return false;}
 const TWeakObjectPtr<UDWWorldEventSubsystem> WeakOwner(this);
 const TWeakObjectPtr<UDWCombatAuthoritySubsystem> WeakAuthority(Authority);
 const TWeakObjectPtr<ADWPlayerCharacter> WeakPlayer(Player);
 auto Current=[&]()
 {
  const auto* S=WeakOwner.Get();const auto* A=WeakAuthority.Get();const auto* P=WeakPlayer.Get();
  if(!S||!A||!P||!Plan.World.IsValid()||A->GetWorld()!=Plan.World.Get()||P->GetWorld()!=Plan.World.Get()||
     P->IsActorBeingDestroyed()||!ExpectedEpoch||A->GetWorldEpoch()!=ExpectedEpoch||
     !ExpectedPlayerLife.Entity||!ExpectedPlayerLife.Life)
  {Error=TEXT("World-event publication lost its restored world, epoch or player.");return false;}
  const auto Life=A->FindRegisteredActorLife(P);
  if(Life.Entity!=ExpectedPlayerLife.Entity||Life.Life!=ExpectedPlayerLife.Life)
  {Error=TEXT("World-event publication player life changed.");return false;}
  return S->CheckCompletedFactsPlan(Plan,true,Error);
 };
 if(!Current())return false;
 Plan.bPublishStarted=true;
 for(const auto& Entry:Plan.Entries)
 {
  if(!Current())return false;
  // RestoreState would rewrite facts after callbacks. Only its original presentation
  // notification belongs here; all bRunning/bCompleted facts are already adopted.
  Entry.Component->OnApplyState.Broadcast(Entry.bTargetCompleted);
  if(!Current())return false;
 }
 Error.Reset();return true;
}

bool UDWWorldEventSubsystem::Acquire(UDWCinematicComponent* C){if(Active.IsValid()||!C)return false;Active=C;return true;}
void UDWWorldEventSubsystem::Release(UDWCinematicComponent* C){if(Active.Get()==C)Active.Reset();}
bool UDWWorldEventSubsystem::IsPlaying(const UObject* C)
{const UWorld* W=GEngine?GEngine->GetWorldFromContextObject(C,EGetWorldErrorMode::ReturnNull):nullptr;return W&&W->GetSubsystem<UDWWorldEventSubsystem>()->IsCinematicActive();}
UDWWorldEventComponent::UDWWorldEventComponent(){PrimaryComponentTick.bCanEverTick=false;}
void UDWWorldEventComponent::BeginPlay()
{
 Super::BeginPlay();RegisteredId=EventId;auto* S=GetWorld()->GetSubsystem<UDWWorldEventSubsystem>();bRegistered=S->RegisterEvent(this);
 if(!bRegistered)UE_LOG(LogTemp,Warning,TEXT("DW World Event %s: assign a unique non-empty Event ID"),*GetOwner()->GetName());
 RestoreState(bRegistered&&S->IsCompleted(RegisteredId));
}
void UDWWorldEventComponent::EndPlay(const EEndPlayReason::Type R)
{if(auto* W=GetWorld())if(auto* S=W->GetSubsystem<UDWWorldEventSubsystem>())S->UnregisterEvent(this);Super::EndPlay(R);}
void UDWWorldEventComponent::RestoreState(bool V){bRunning=false;bCompleted=V;OnApplyState.Broadcast(V);}
bool UDWWorldEventComponent::RequestEvent()
{if(!IsReady()||EventId!=RegisteredId)return false;bRunning=true;OnRequested.Broadcast();return true;}
void UDWWorldEventComponent::CompleteEvent()
{if(!bRunning||bCompleted)return;bRunning=false;bCompleted=true;GetWorld()->GetSubsystem<UDWWorldEventSubsystem>()->Commit(RegisteredId);OnApplyState.Broadcast(true);OnCompleted.Broadcast();}
void UDWWorldEventComponent::CancelEvent(){if(!bRunning)return;RestoreState(bCompleted);}
