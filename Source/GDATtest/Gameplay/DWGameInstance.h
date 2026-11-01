#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "DWGameplayTypes.h"
#include "DWCombatCommitLedger.h"
#include "DWAdventureFlowSave.h"
#include "Containers/Ticker.h"
#include "DWGameInstance.generated.h"

class ADWPlayerCharacter;
class AActor;
class UDWGameplayConfig;
class UDWSaveGame;
class USaveGame;
class UDWLoadingTransitionSettings;
class FDWCombatRules;
class FDWCombatEquipmentTransaction;
class FDWFormalLanguagePersistence;
struct FDWCombatStarterGrantState;
struct FDWActiveSlotSource;

/** Owns three persistent slots; stores pending restoration across level travel. */
UCLASS(BlueprintType, Blueprintable)
class GDATTEST_API UDWGameInstance : public UGameInstance
{
    GENERATED_BODY()

public:
    static constexpr int32 SaveSlotCount = 3;
    virtual void Init() override;
    virtual void Shutdown() override;
    /** Numeric foundation only. Equipment binding and gameplay activation are separate gates. */
    const TSharedPtr<const FDWCombatRules>& GetCombatRules() const { return CombatRules; }
    UFUNCTION(BlueprintPure, Category="DoughWorld|Combat|Configuration")
    bool AreCombatNumericRulesValid() const { return CombatRules.IsValid(); }
    UFUNCTION(BlueprintPure, Category="DoughWorld|Combat|Configuration")
    FString GetCombatRulesError() const { return CombatRulesError; }
    /** Registration only; never grants ownership or enables unimplemented gameplay. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Combat|Configuration")
    bool ValidateCombatItemRegistration(FString& OutError);
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="DoughWorld|Loading")
    TSoftObjectPtr<UDWLoadingTransitionSettings> LoadingTransitionSettings= TSoftObjectPtr<UDWLoadingTransitionSettings>(FSoftObjectPath(TEXT("/Game/DoughWorld/UI/Transitions/DA_DWLoadingTransition.DA_DWLoadingTransition")));

    UFUNCTION(BlueprintPure, Category="DoughWorld|Config")
    UDWGameplayConfig* GetConfig();

    /** Resolves before level travel as well as during play; never mutates the default config. */
    UFUNCTION(BlueprintPure, Category="DoughWorld|Config")
    UDWGameplayConfig* GetConfigForMap(const FString& MapPackage);
    UFUNCTION(BlueprintPure, Category="DoughWorld|Config")
    bool AreGameplayMapProfilesCompatible(const FString& FirstMap,const FString& SecondMap);

    /** Native, world-lifetime admission guard until gathering actors have a complete save participant. */
    void MarkUnsupportedGatheringPersistence(UWorld* World);

    UFUNCTION(BlueprintPure, Category="DoughWorld|Save")
    int32 GetActiveSlot() const { return ActiveSlot; }

    UFUNCTION(BlueprintPure, Category="DoughWorld|Save")
    bool HasActiveSlot() const { return ActiveSlot >= 0 && ActiveSlot < SaveSlotCount; }
    bool IsNonPersistentCombatSandboxContext()const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Save") bool IsPlayerRestorePending()const{return PendingSave!=nullptr&&!bCombatRestoreFailure;}
    bool HasPlayerRestoreFailure()const{return bCombatRestoreFailure;}

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
    UFUNCTION(BlueprintPure,Category="DoughWorld|Opening") bool IsOpeningPending()const;
    // Native director/typed transaction only; no arbitrary setters or writable receipt getters.
    bool CanGrantFormalStarter(const ADWPlayerCharacter*,const FString& PlaythroughId)const;
    bool IsOpeningDirectorCurrent(AActor* Director,const ADWPlayerCharacter*)const;
    bool TryStartOpening(AActor* Director,ADWPlayerCharacter*);
    bool CompleteOpening(AActor* Director,ADWPlayerCharacter*);
    bool RestartOpening(AActor* Director,ADWPlayerCharacter*);
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
#if WITH_DEV_AUTOMATION_TESTS
    friend struct FDWBuildingPlacementAutomationAccess;
#endif
    friend class FDWCombatEquipmentTransaction;
    friend class FDWFormalLanguagePersistence;
    friend class FDWCombatWorldPersistence;
    int32 ActiveSaveVersion=0;
    TSharedPtr<const FDWActiveSlotSource> ActiveSlotSource,PreviousTravelSource;
    bool bPreparingFormalFirstCheckpoint=false,bPreparingFormalFreshDestination=false;
    FDWAdventureFlowSave AdventureFlow;
    FString FormalPlaythroughId;
    TSharedPtr<FDWCombatStarterGrantState> FormalStarterGrant;
    TWeakObjectPtr<AActor> OpeningDirector;
    TWeakObjectPtr<ADWPlayerCharacter> OpeningStartupPlayer;
    TWeakObjectPtr<UWorld> OpeningStartupWorld;
    DWCombatCommit::FEntityLife OpeningStartupLife;
    uint64 OpeningStartupGeneration=0,OpeningStartupEpoch=0;
    int32 OpeningStartupSlot=INDEX_NONE;
    bool bOpeningStartupReady=false,bCompletingOpening=false;
    bool SetAdventureFlowOwner(const UDWSaveGame*,const TSharedPtr<const FDWActiveSlotSource>& Source={});
    bool ConfigureOpeningNewGame(UDWSaveGame&,FString&);
    bool IsOpeningPersistenceBlocked()const;
    bool ValidateAdventureFlow(const UDWSaveGame&);
    bool ResolveAdventureTravelMap(UDWSaveGame&,FString& Map,FString& Error);
    bool PendingSaveMatchesWorld(const UDWSaveGame&,const UWorld&)const;
    bool HasFormalStarterIdentity(const ADWPlayerCharacter*,const FString& PlaythroughId)const;
    bool OpeningStartupIdentityIsCurrent(const ADWPlayerCharacter*)const;
    bool OpeningStartupIsCurrent(const ADWPlayerCharacter*)const;
    bool FinishOpeningPlayerRestore(ADWPlayerCharacter&,const UDWSaveGame&,FString&);
    bool ExecuteOpeningFreshDestination(ADWPlayerCharacter&,const UDWSaveGame&,bool& Committed,FString&);
    TSharedPtr<const FDWCombatRules> CombatRules;
    FString CombatRulesError;
    UPROPERTY(Transient)
    TObjectPtr<UDWGameplayConfig> Config;
    bool bDefaultConfigRejected=false;
    TWeakObjectPtr<UWorld> UnsupportedGatheringPersistenceWorld;
    FString UnsupportedGatheringPersistenceReason;

    UPROPERTY(Transient) TMap<FName,TObjectPtr<UDWGameplayConfig>> MapConfigs;
    UDWGameplayConfig* GetDefaultGameplayConfig();

    UPROPERTY(Transient)
    TObjectPtr<UDWSaveGame> PendingSave;

    int32 ActiveSlot = INDEX_NONE;
    int32 PreviousTravelSlot = INDEX_NONE;
    bool bSaveTravelInFlight=false;
    UFUNCTION() void HandleTravelFinished();
    UFUNCTION() void HandleTravelFailure(FText Reason);

    FString MakeSlotName(int32 SlotIndex)const;
    FString VerificationSavePrefix;
    FString SaveDirectory;
    TSharedPtr<class FDWEditorMapIterationSession> EditorMapIteration;
    bool bEditorIterationRequired=false;
    FString SaveStorageInitializationError;
    bool CheckSaveStorageContext(FString& Error)const;
    FString GetSaveStorageIdentity()const;
    bool bUseDocumentsStorage = false;
    void InitializeSaveStorage();
    bool SlotExists(const FString& Slot) const;
    USaveGame* ReadSlot(const FString& Slot) const;
    UDWSaveGame* ReadSlotWithSource(const FString&,TSharedPtr<const FDWActiveSlotSource>&,FString&)const;
    UDWSaveGame* ReadSlotMatchingSource(int32,const TSharedPtr<const FDWActiveSlotSource>&,FString&)const;
    UDWSaveGame* ReadActiveSlotForCapture(TSharedPtr<const FDWActiveSlotSource>&,FString&);
    bool HasActiveSlotSourceOwner()const;
    bool WriteNewSlot(UDWSaveGame&,int32,TSharedPtr<const FDWActiveSlotSource>&,FString&)const;
    bool CommitActiveSlotSave(UDWSaveGame&,const TSharedPtr<const FDWActiveSlotSource>&,const FDWAdventureFlowSave&,FString&);
    bool RemoveSlot(const FString& Slot) const;
public:
    UFUNCTION(BlueprintPure, Category="DoughWorld|Save") FString GetSaveDirectory() const { return SaveDirectory; }
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    // Read-only native evidence for the fixed isolated PIE QA library; never changes save routing.
    const FString& GetResourceVerificationSavePrefix() const { return VerificationSavePrefix; }
#endif
private:
    static bool IsValidSlot(int32 SlotIndex);
    bool ValidateSave(const UDWSaveGame* Save,int32 ExpectedSlot=INDEX_NONE);
    // True means rejected; the existing SaveError toast reads LastSaveError.
    bool RejectGatheringPreviewPersistence(const FString& RequestedMap=FString());
    friend class ADWPlayerCharacter;
    // Only the player death-effects tail can create this fixed native ticket; no Blueprint allow-dead save API.
    void QueueCommittedCombatDeath(ADWPlayerCharacter*,DWCombatCommit::FEntityLife);
    bool SaveCombatSnapshot(ADWPlayerCharacter* ExpectedPlayer,bool bAllowCommittedDeath);
    void ApplyCompletePendingSave(ADWPlayerCharacter*);
    void ExecuteCompletePendingSave(ADWPlayerCharacter*);
    void ResetCombatSaveFlow();
    void RegisterCombatRestoreFailure(UWorld*,bool bCommitted,const FString& Detail);
    void ShowPendingCombatRestoreError();
    bool bCombatSaveAlive=false,bApplyingCombatPending=false,bCombatRestoreFailure=false,bCombatRestoreErrorShown=false;
    uint64 CombatSaveGeneration=1,CombatDeathSaveGeneration=0,CombatDeathSaveEpoch=0;
    int32 CombatDeathSaveSlot=INDEX_NONE;
    DWCombatCommit::FEntityLife CombatDeathSaveLife;
    TWeakObjectPtr<ADWPlayerCharacter> CombatDeathSavePlayer;
    TWeakObjectPtr<UWorld> CombatDeathSaveWorld,CombatRestoreFailureWorld;
    FTSTicker::FDelegateHandle CombatDeathSaveTicker,CombatRestoreErrorTicker,CombatPendingRestoreTicker;
    bool Fail(const FText& Reason);
};
