#pragma once
#include "CoreMinimal.h"
#include "DWGameplayGameMode.h"
#include "DWLanguagePlaytestGameMode.generated.h"
/** Exact-map bootstrap. Never creates/loads a formal adventure slot. */
UCLASS() class GDATTEST_API ADWLanguagePlaytestGameMode:public ADWGameplayGameMode
{
 GENERATED_BODY()
public:
 ADWLanguagePlaytestGameMode();
 virtual void StartPlay()override;
};
