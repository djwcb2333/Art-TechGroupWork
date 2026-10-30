#pragma once
#include "CoreMinimal.h"
#include "DWFormalLanguageSave.generated.h"

// Separate formal-adventure envelope. Do not reuse or reinterpret LanguageTrialSchema.
// Schema 0 is an entirely empty absent block; schema 2 is the strict formal block.
USTRUCT()
struct FDWFormalLanguageSave
{
 GENERATED_BODY()
 UPROPERTY(SaveGame) int32 Schema=0;
 UPROPERTY(SaveGame) int32 SlotIndex=INDEX_NONE;
 UPROPERTY(SaveGame) FString ProgressJson;
 UPROPERTY(SaveGame) FString A01StateJson;
};
