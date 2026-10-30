#pragma once
#include "CoreMinimal.h"
class ADWPlayerCharacter;
class ADWGatheringStation;

// Candidate alternatives; the caller must select only the user-confirmed return mode.
enum class EDWBuildingDemolitionReturn : uint8 { MaterialsToWorld,MaterialsToInventory,BuildingToWorld };
class GDATTEST_API FDWBuildingDemolitionTransaction
{
public:
    // Called only at 100% by one authoritative session. It must remain current across all staged actor callbacks.
    static bool Commit(ADWPlayerCharacter& Player,ADWGatheringStation& Building,EDWBuildingDemolitionReturn Mode,
        TFunctionRef<bool()> SessionCurrent,TFunctionRef<bool()> ConsumeCompletion,FString& Error);
};
