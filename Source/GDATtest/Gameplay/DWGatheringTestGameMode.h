#pragma once
#include "CoreMinimal.h"
#include "DWGameplayGameMode.h"
#include "DWGatheringTestGameMode.generated.h"
/** Opt-in, session-only gathering playground. Never selects or writes a save slot. */
UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringTestGameMode : public ADWGameplayGameMode
{
    GENERATED_BODY()
public:
    virtual void StartPlay() override;
private:
    void StartTestSession();
};
