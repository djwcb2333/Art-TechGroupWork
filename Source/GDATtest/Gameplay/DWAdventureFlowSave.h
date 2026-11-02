#pragma once
#include "CoreMinimal.h"
#include "DWAdventureFlowSave.generated.h"

// Additive tagged fields. Schema zero is an old adventure and NEVER opts in.
UENUM() enum class EDWAdventureOpeningPhase:uint8
{ Legacy=0, Pending=1, CompletedPendingDestination=2, Completed=3 };
USTRUCT() struct FDWAdventureFlowSave
{
 GENERATED_BODY()
 UPROPERTY(SaveGame) int32 Schema=0;
 UPROPERTY(SaveGame) FString PlaythroughId;
 UPROPERTY(SaveGame) FName ScenarioId;
 UPROPERTY(SaveGame) FString OpeningMap;
 UPROPERTY(SaveGame) FString DestinationMap;
 UPROPERTY(SaveGame) EDWAdventureOpeningPhase Phase=EDWAdventureOpeningPhase::Legacy;
 UPROPERTY(SaveGame) bool bStarterGranted=false;
 // Missing in deployed saves -> legacy 0. Only genuinely new formal slots choose basic-tools 1.
 UPROPERTY(SaveGame) int32 StarterPolicyVersion=0;
};
