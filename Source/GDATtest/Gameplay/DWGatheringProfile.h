#pragma once
#include "CoreMinimal.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.generated.h"
/** Compatibility class for existing isolated test assets; fields are inherited unchanged. */
UCLASS(BlueprintType)
class GDATTEST_API UDWGatheringProfile : public UDWGameplayConfig
{
    GENERATED_BODY()
public:
    UDWGatheringProfile();
};
