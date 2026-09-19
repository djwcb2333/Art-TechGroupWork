#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DWGameplayTypes.h"
#include "DWGameInstance.generated.h"

class ADWPlayerCharacter;
class UDWGameplayConfig;
class UDWSaveGame;
class UDWLoadingTransitionSettings;

/** Owns three persistent slots; stores pending restoration across level travel. */
UCLASS(BlueprintType, Blueprintable)
class GDATTEST_API UDWGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    static constexpr int32 SaveSlotCount = 3;
    virtual void Init() override;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="DoughWorld|Loading")
    TSoftObjectPtr<UDWLoadingTransitionSettings> LoadingTransitionSettings= TSoftObjectPtr<UDWLoadingTransitionSettings>(FSoftObjectPath(TEXT("/Game/DoughWorld/UI/Transitions/DA_DWLoadingTransition.DA_DWLoadingTransition")));

    UFUNCTION(BlueprintPure, Category="DoughWorld|Config")
    UDWGameplayConfig* GetConfig();

    UFUNCTION(BlueprintPure, Category="DoughWorld|Save")
    int32 GetActiveSlot() const { return ActiveSlot; }

    UFUNCTION(BlueprintPure, Category="DoughWorld|Save")
    bool HasActiveSlot() const { return ActiveSlot >= 0 && ActiveSlot < SaveSlotCount; }
    UFUNCTION(BlueprintPure,Category="DoughWorld|Save") bool IsPlayerRestorePending()const{return PendingSave!=nullptr;}

    UFUNCTION(BlueprintPure, Category="DoughWorld|Save")
    TArray<FDWSlotSummary> GetSlotSummaries() const;

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Save")
    bool NewGame(int32 SlotIndex, const FString& Name);

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Save")
    bool LoadGameSlot(int32 SlotIndex);

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Save")
    bool DeleteGameSlot(int32 SlotIndex);

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Save")
    bool SaveCurrentGame();
    /** Save current map, preserve inventory/stats and restore destination world state. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Save") bool TravelToGameplayMap(TSoftObjectPtr<UWorld> Destination);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Save") bool IsAllowedGameplayMap(const FString& Map);


    /** Navigation only: the UI explicitly calls SaveCurrentGame first for its save-and-exit action. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Save")
    void ReturnToTitle();

    UFUNCTION(BlueprintCallable, Category="DoughWorld|Save")
    void ApplyPendingSave(ADWPlayerCharacter* Player);

    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="DoughWorld|Config")
    FSoftObjectPath SoftConfigPath = FSoftObjectPath(TEXT("/Game/DoughWorld/Core/Data/DA_DoughWorldGameplay.DA_DoughWorldGameplay"));

    UPROPERTY(BlueprintReadOnly, Category="DoughWorld|Save")
    FText LastSaveError;

private:
    UPROPERTY(Transient)
    TObjectPtr<UDWGameplayConfig> Config;

    UPROPERTY(Transient)
    TObjectPtr<UDWSaveGame> PendingSave;

    int32 ActiveSlot = INDEX_NONE;
    int32 PreviousTravelSlot = INDEX_NONE;
    bool bSaveTravelInFlight=false;
    UFUNCTION() void HandleTravelFinished();
    UFUNCTION() void HandleTravelFailure(FText Reason);

    FString MakeSlotName(int32 SlotIndex)const;
    FString VerificationSavePrefix;
    static bool IsValidSlot(int32 SlotIndex);
    bool ValidateSave(const UDWSaveGame* Save);
    bool Fail(const FText& Reason);
};
