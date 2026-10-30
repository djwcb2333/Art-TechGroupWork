#pragma once
#include "CoreMinimal.h"
#include "DWGatheringSave.h"
class ADWGatheringPond;
/** Same-map authored pond facts only; no asset load, spawn, UI or file writes. */
class FDWGatheringPondPersistence
{
public:
    static bool Capture(const ADWGatheringPond&,FDWGatheringPondSave&,FString& Error);
    static bool Validate(const FDWGatheringPondSave&,FString& Error);
    static bool MatchAuthored(const ADWGatheringPond&,const FDWGatheringPondSave&,FString& Error);
};
