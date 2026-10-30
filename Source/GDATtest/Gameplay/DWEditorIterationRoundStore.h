#pragma once
#include "CoreMinimal.h"
struct FDWEditorAuthoringSignature;
struct FDWEditorIterationRound
{
 FString Id,Signature,Directory,StorageIdentity;
};
/** Index/round files only; never reads a gameplay slot or imports a formal save. */
namespace DWEditorIterationRoundStore
{
 bool Select(const FString& Root,const FString& ProjectIdentity,const FDWEditorAuthoringSignature&,FDWEditorIterationRound&,FString& Error);
 bool CanReuse(const FString& PreviousSignature,const FString& CurrentSignature);
 bool IsRoundId(const FString&);
}
