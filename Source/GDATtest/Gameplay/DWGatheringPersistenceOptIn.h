#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DWGatheringPersistenceOptIn.generated.h"

/** Exact audited identities; an empty/invalid/duplicate entry never enables persistence. */
USTRUCT()
struct FDWGatheringMapOptIn
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,Category="Gathering") bool bEnabled=false;
    UPROPERTY(EditAnywhere,Category="Gathering") FString MapPackage;
    UPROPERTY(EditAnywhere,Category="Gathering") FString ProfilePath;
    UPROPERTY(EditAnywhere,Category="Gathering") TArray<FString> AuditedAuthoredClassPaths;
};
UCLASS(Config=Game,DefaultConfig,meta=(DisplayName="DoughWorld Gathering Save Capabilities"))
class GDATTEST_API UDWGatheringPersistenceOptIn : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    virtual FName GetCategoryName()const override{return TEXT("Project");}
    UPROPERTY(Config,EditAnywhere,Category="Gathering") TArray<FDWGatheringMapOptIn> Maps;
    static const FDWGatheringMapOptIn* Find(const FString& Map);
    static bool ProfileAllowed(const FString& Map,const FString& Profile);
    static bool ClassAllowed(const FString& Map,const FString& ClassPath);
};
