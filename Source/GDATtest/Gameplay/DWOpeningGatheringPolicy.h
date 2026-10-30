#pragma once
#include "CoreMinimal.h"
struct FDWAdventureFlowSave;
struct FDWGatheringWorldSave;
class UDWSaveGame;

/** Exact, one-way SC0101 source capability; no general cross-map persistence admission. */
namespace DWOpeningGatheringPolicy
{
 GDATTEST_API bool ValidateSourceFlow(const FDWAdventureFlowSave&,const FString& CurrentMap,const FString& ConfigPath,FString&);
 GDATTEST_API bool ValidateSourceSnapshot(const UDWSaveGame&,FString&);
 GDATTEST_API bool ValidateDestinationBoundary(const UDWSaveGame& Source,const UDWSaveGame& Destination,
     const FDWGatheringWorldSave& CapturedDestination,FString&);
}
