#include "DWDialogueSequence.h"
#include "DWTextRevealComponent.h"
#include "DWLocalizationLibrary.h"
#include "Components/TextBlock.h"
#include "UObject/UObjectIterator.h"

FText FDWDialogueLine::Resolve(const UObject* Context)const
{
 const bool English=UDWLocalizationLibrary::GetLanguage(Context)==EDWGameLanguage::English;
 const FText& Preferred=English?EnglishText:ChineseText;
 return Preferred.IsEmptyOrWhitespace()?(English?ChineseText:EnglishText):Preferred;
}
UDWDialogueSequenceComponent::UDWDialogueSequenceComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;}
bool UDWDialogueSequenceComponent::PlayDialogue(UTextBlock* TextBlock)
{
 if(bRunning){LastError=TEXT("Dialogue is already running; stop it before starting another list");return false;}
 LastError.Empty();bCompleted=false;
 auto* R=UDWTextRevealLibrary::GetTextRevealComponent(TextBlock);
 if(!IsValid(R)||!R->IsSupportedOwner()){LastError=TEXT("Add DW Text Reveal to an on-screen TextBlock first");return false;}
 if(R->bExternalClock){LastError=TEXT("Disable External Clock for automatic dialogue playback");return false;}
 // A TextBlock may have only one active queue, including during its silent hold.
 for(TObjectIterator<UDWDialogueSequenceComponent> It;It;++It)
  if(*It!=this&&It->bRunning&&It->Reveal.Get()==R){LastError=TEXT("This TextBlock is already owned by another dialogue queue");return false;}
 ActiveLines=Lines;
 bool HasText=false;for(const auto& Line:ActiveLines)HasText|=!Line.Resolve(this).IsEmptyOrWhitespace();
 if(!HasText){LastError=TEXT("Add at least one non-empty dialogue line");return false;}
 // Stop a widget's Play on Construct before binding. No overlapping audio player is created.
 R->Stop(false);Reveal=R;CurrentLineIndex=INDEX_NONE;CompletedLineCount=0;HoldRemaining=0;
 bRunning=true;bPaused=false;bHolding=false;++Revision;
 R->OnFinished.AddUniqueDynamic(this,&ThisClass::TextFinished);
 R->OnStopped.AddUniqueDynamic(this,&ThisClass::TextStopped);
 SetComponentTickEnabled(true);StartNextLine();return bRunning||bCompleted;
}
void UDWDialogueSequenceComponent::StartNextLine()
{
 if(!bRunning)return;
 do{++CurrentLineIndex;}while(ActiveLines.IsValidIndex(CurrentLineIndex)&&ActiveLines[CurrentLineIndex].Resolve(this).IsEmptyOrWhitespace());
 if(!ActiveLines.IsValidIndex(CurrentLineIndex)){Finish(true,false);return;}
 auto* R=Reveal.Get();if(!R){LastError=TEXT("Text Reveal was removed");Finish(false,false);return;}
 bHolding=false;HoldRemaining=0;CurrentText=ActiveLines[CurrentLineIndex].Resolve(this);
 const uint32 Expected=Revision;
 R->PlayText(CurrentText);
 if(!bRunning||Revision!=Expected)return;
 if(!R->IsPlaying()){LastError=TEXT("TextBlock is not constructed: add its widget to the viewport before Play Dialogue");Finish(false,true);return;}
 OnLineStarted.Broadcast(CurrentLineIndex);
}
void UDWDialogueSequenceComponent::TextFinished(bool Skipped)
{
 if(!bRunning||bHolding||!ActiveLines.IsValidIndex(CurrentLineIndex))return;
 bHolding=true;HoldRemaining=FMath::Max(0.f,ActiveLines[CurrentLineIndex].HoldSeconds);++CompletedLineCount;
 OnLineFinished.Broadcast(CurrentLineIndex,Skipped);
 // Advance on a later component tick, never recursively inside the Text Reveal ticker.
}
void UDWDialogueSequenceComponent::TextStopped()
{if(bRunning){LastError=TEXT("Text playback was stopped externally");Finish(false,false);}}
void UDWDialogueSequenceComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Dt,Type,Function);if(!bRunning)return;
 if(!Reveal.IsValid()||!Reveal->GetOwner().IsValid()||!Reveal->IsPlaybackReady()){LastError=TEXT("Dialogue text widget was removed");Finish(false,false);return;}
 if(!bHolding&&!Reveal->IsPlaying()){LastError=TEXT("Dialogue text widget was closed or playback was interrupted");Finish(false,false);return;}
 if(bPaused)return;
 if(bHolding&&bAutoAdvance){HoldRemaining=FMath::Max(0.f,HoldRemaining-Dt);if(HoldRemaining<=0)StartNextLine();}
}
void UDWDialogueSequenceComponent::AdvanceDialogue()
{
 if(!bRunning||bPaused)return;
 if(bHolding)StartNextLine();else if(Reveal.IsValid())Reveal->SkipToEnd();
}
void UDWDialogueSequenceComponent::PauseDialogue(){if(bRunning){bPaused=true;if(Reveal.IsValid())Reveal->Pause();}}
void UDWDialogueSequenceComponent::ResumeDialogue(){if(bRunning){bPaused=false;if(Reveal.IsValid())Reveal->Resume();}}
void UDWDialogueSequenceComponent::StopDialogue(bool ClearText){if(bRunning){LastError=TEXT("Stopped");Finish(false,ClearText);}}
void UDWDialogueSequenceComponent::Finish(bool Success,bool ClearText)
{
 if(!bRunning)return;
 bRunning=false;bPaused=false;bHolding=false;bCompleted=Success;HoldRemaining=0;++Revision;SetComponentTickEnabled(false);
 if(Reveal.IsValid()){
  Reveal->OnFinished.RemoveDynamic(this,&ThisClass::TextFinished);
  Reveal->OnStopped.RemoveDynamic(this,&ThisClass::TextStopped);
  Reveal->Stop(!ClearText);
 }
 Reveal.Reset();ActiveLines.Reset();OnFinished.Broadcast(Success);
}
void UDWDialogueSequenceComponent::EndPlay(const EEndPlayReason::Type Reason)
{if(bRunning){LastError=TEXT("Dialogue owner ended play");Finish(false,true);}Super::EndPlay(Reason);}
