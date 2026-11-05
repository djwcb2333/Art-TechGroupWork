// Candidate only: root installs in existing Source/GDATtest/Gameplay directory.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifyQueue.h"
#include "AnimNotify_PlayNiagaraEffect.h"
#include "AnimNotifyState_TimedNiagaraEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWNativeFxIntervalTest,"DoughWorld.Animation.NativeFX.NativeIntervals",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWNativeFxIntervalTest::RunTest(const FString&)
{
 UAnimSequence* Sequence=NewObject<UAnimSequence>(GetTransientPackage());
 Sequence->Notifies.SetNum(3);
 FAnimNotifyEvent& Zero=Sequence->Notifies[0];
 Zero.Notify=NewObject<UAnimNotify_PlayNiagaraEffect>(Sequence);Zero.SetTime(0.f);
 FAnimNotifyEvent& One=Sequence->Notifies[1];
 One.Notify=NewObject<UAnimNotify_PlayNiagaraEffect>(Sequence);One.SetTime(.25f);
 FAnimNotifyEvent& State=Sequence->Notifies[2];
 State.NotifyStateClass=NewObject<UAnimNotifyState_TimedNiagaraEffect>(Sequence);State.SetTime(.2f);State.SetDuration(.4f);
 auto Extract=[Sequence](float Previous,float Current)
 {
  FAnimNotifyContext Context;Sequence->GetAnimNotifiesFromDeltaPositions(Previous,Current,Context);
  return Context.ActiveNotifies;
 };
 auto Contains=[](const TArray<FAnimNotifyEventReference>& Refs,const FAnimNotifyEvent* Event)
 {return Refs.ContainsByPredicate([Event](const FAnimNotifyEventReference& R){return R.GetNotify()==Event;});};
 auto Start=Extract(-KINDA_SMALL_NUMBER,0.f);
 TestTrue(TEXT("Negative initial boundary includes time-zero one-shot"),Contains(Start,&Zero));
 TestEqual(TEXT("Frozen zero does not repeat one-shot"),Extract(0.f,0.f).Num(),0);
 auto Cross=Extract(.1f,.3f);
 TestTrue(TEXT("Crossing extracts one-shot"),Contains(Cross,&One));
 TestTrue(TEXT("Crossing extracts active state"),Contains(Cross,&State));
 auto Frozen=Extract(.3f,.3f);
 TestEqual(TEXT("Frozen active interval contains state only"),Frozen.Num(),1);
 TestTrue(TEXT("Frozen active interval retains timed state"),Contains(Frozen,&State));
 TestFalse(TEXT("Restore seeded at current time does not replay one-shot"),Contains(Frozen,&One));
 TestTrue(TEXT("Crossing state end yields final state extraction"),Contains(Extract(.5f,.7f),&State));
 TestFalse(TEXT("After end no active state remains"),Contains(Extract(.7f,.8f),&State));
 auto Restart=Extract(-KINDA_SMALL_NUMBER,.3f);
 TestTrue(TEXT("New action extraction includes time-zero event"),Contains(Restart,&Zero));
 TestTrue(TEXT("New action extraction includes active state"),Contains(Restart,&State));
 // No World, AnimInstance, Niagara component, Notify/Begin/Tick/End calls or package writes.
 // This verifies native interval extraction, not runtime state lifecycle or visuals.
 return true;
}
#endif
