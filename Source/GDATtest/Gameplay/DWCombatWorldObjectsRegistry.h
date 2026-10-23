#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWCombatWorldObjectsRegistry.generated.h"
/** Keeps committed formal deaths after corpse destruction; never rolls loot or respawns actors. */
UCLASS()
class GDATTEST_API UDWCombatWorldObjectsRegistry : public UWorldSubsystem
{
    GENERATED_BODY()
private:
    friend class FDWCombatWorldObjectsPersistence;
    friend class FDWCombatWorldObjectsRestorePlan;
    TMap<FString,FDWCombatEnemyObjectSave> Tombstones;
    uint64 Revision=1;
    bool bCaptureFailure=false;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type)const override {return Type==EWorldType::Game||Type==EWorldType::PIE;}
};
