#pragma once
#include "CoreMinimal.h"
struct FDWEditorAuthoringSignature;
struct FDWEditorIterationRound
{
 FString Id,Signature,Directory,StorageIdentity,ContextKey;
};
/** Index/round metadata only. Never reads a gameplay slot or imports a formal save. */
namespace DWEditorIterationRoundStore
{
 // Stable author entry + exact config object identity; content versions remain separate.
 bool MakeContextKey(const FString& EntryPackage,const FString& ConfigObjectPath,FString& Out,FString& Error);
 bool Select(const FString& Root,const FString& ProjectIdentity,const FString& ContextKey,
     const FDWEditorAuthoringSignature&,FDWEditorIterationRound&,FString& Error);
 bool CanReuse(const FString& PreviousSignature,const FString& CurrentSignature);
 bool IsRoundId(const FString&);
}
