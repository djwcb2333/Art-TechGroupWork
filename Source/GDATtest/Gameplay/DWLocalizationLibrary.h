#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWGameplayTypes.h"
#include "DWLocalizationLibrary.generated.h"

UENUM(BlueprintType)
enum class EDWGameLanguage : uint8 { SimplifiedChinese, English };
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWLanguageChanged,EDWGameLanguage,Language);

/** Game-only preference. It deliberately does not change the editor or process culture. */
UCLASS()
class GDATTEST_API UDWLocalizationSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection)override;
    UPROPERTY(BlueprintReadOnly,Category="Language") EDWGameLanguage Language=EDWGameLanguage::English;
    UPROPERTY(BlueprintReadOnly,Category="Language") int32 Revision=0;
    UPROPERTY(BlueprintAssignable,Category="Language") FDWLanguageChanged OnLanguageChanged;
    UFUNCTION(BlueprintCallable,Category="Language") void SetLanguage(EDWGameLanguage NewLanguage);
};

UCLASS()
class GDATTEST_API UDWLocalizationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintPure,Category="DoughWorld|Language",meta=(WorldContext="WorldContextObject")) static EDWGameLanguage GetLanguage(const UObject* WorldContextObject);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Language",meta=(WorldContext="WorldContextObject")) static void SetLanguage(const UObject* WorldContextObject,EDWGameLanguage Language);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Language",meta=(WorldContext="WorldContextObject")) static int32 GetRevision(const UObject* WorldContextObject);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Language",meta=(WorldContext="WorldContextObject")) static FText TranslateLabel(const UObject* WorldContextObject,const FText& AuthoredChinese);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Language",meta=(WorldContext="WorldContextObject")) static FText GetItemDisplayName(const UObject* WorldContextObject,const FDWItemDefinition& Item);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Language",meta=(WorldContext="WorldContextObject")) static FText GetRecipeDisplayName(const UObject* WorldContextObject,const FDWRecipeDefinition& Recipe);
};
inline FText DWText(const UObject* Context,const TCHAR* Chinese,const TCHAR* English)
{return FText::FromString(UDWLocalizationLibrary::GetLanguage(Context)==EDWGameLanguage::English?English:Chinese);}
