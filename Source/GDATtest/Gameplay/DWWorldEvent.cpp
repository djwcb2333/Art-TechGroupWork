#include "DWWorldEvent.h"
#include "DWGameplayCinematic.h"
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
