#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWDialogueSequence.generated.h"

class UTextBlock;
class UDWTextRevealComponent;

USTRUCT(BlueprintType)
struct GDATTEST_API FDWDialogueLine
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dialogue",meta=(MultiLine="true")) FText ChineseText;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dialogue",meta=(MultiLine="true")) FText EnglishText;
 /** Readable pause after the last character and its spring animation finish. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dialogue",meta=(ClampMin="0",Units="s")) float HoldSeconds=1.5f;
 FText Resolve(const UObject* Context)const;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWDialogueLineStarted,int32,LineIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDWDialogueLineFinished,int32,LineIndex,bool,bSkipped);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWDialogueFinished,bool,bCompleted);

/** Reusable line queue. Supply an on-screen TextBlock with DW Text Reveal; this component never creates UI or locks input. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,Blueprintable,meta=(BlueprintSpawnableComponent,DisplayName="DW Dialogue Sequence"))
class GDATTEST_API UDWDialogueSequenceComponent:public UActorComponent
{
 GENERATED_BODY()
public:
 UDWDialogueSequenceComponent();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dialogue") TArray<FDWDialogueLine> Lines;
 /** If false, Advance Dialogue proceeds from a fully revealed line to the next line. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dialogue") bool bAutoAdvance=true;
 UPROPERTY(BlueprintReadOnly,Transient,Category="Dialogue|State") int32 CurrentLineIndex=INDEX_NONE;
 UPROPERTY(BlueprintReadOnly,Transient,Category="Dialogue|State") int32 CompletedLineCount=0;
 UPROPERTY(BlueprintReadOnly,Transient,Category="Dialogue|State") FText CurrentText;
 UPROPERTY(BlueprintReadOnly,Transient,Category="Dialogue|State") float HoldRemaining=0;
 UPROPERTY(BlueprintReadOnly,Transient,Category="Dialogue|State") bool bCompleted=false;
 UPROPERTY(BlueprintReadOnly,Transient,Category="Dialogue|State") FString LastError;
 UPROPERTY(BlueprintAssignable,Category="Dialogue|Events") FDWDialogueLineStarted OnLineStarted;
 UPROPERTY(BlueprintAssignable,Category="Dialogue|Events") FDWDialogueLineFinished OnLineFinished;
 UPROPERTY(BlueprintAssignable,Category="Dialogue|Events") FDWDialogueFinished OnFinished;
 /** Call after Create Widget + Add to Viewport. Rejects a busy target or External Clock. */
 UFUNCTION(BlueprintCallable,Category="Dialogue") bool PlayDialogue(UTextBlock* TextBlock);
 /** First press reveals the current line silently; a later press advances. */
 UFUNCTION(BlueprintCallable,Category="Dialogue") void AdvanceDialogue();
 UFUNCTION(BlueprintCallable,Category="Dialogue") void PauseDialogue();
 UFUNCTION(BlueprintCallable,Category="Dialogue") void ResumeDialogue();
 UFUNCTION(BlueprintCallable,Category="Dialogue") void StopDialogue(bool bClearText=true);
 UFUNCTION(BlueprintPure,Category="Dialogue") bool IsPlaying()const{return bRunning;}
 UFUNCTION(BlueprintPure,Category="Dialogue") bool IsPaused()const{return bPaused;}
 UFUNCTION(BlueprintPure,Category="Dialogue") bool IsHoldingLine()const{return bHolding;}
 virtual void TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Function)override;
protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason)override;
private:
 UPROPERTY(Transient) TArray<FDWDialogueLine> ActiveLines;
 TWeakObjectPtr<UDWTextRevealComponent> Reveal;
 bool bRunning=false,bPaused=false,bHolding=false;
 uint32 Revision=0;
 void StartNextLine();
 void Finish(bool Success,bool ClearText);
 UFUNCTION() void TextFinished(bool bSkipped);
 UFUNCTION() void TextStopped();
};
