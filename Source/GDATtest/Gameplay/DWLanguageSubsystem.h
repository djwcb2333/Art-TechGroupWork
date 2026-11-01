#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DWLanguageTypes.h"
#include "DWLanguageModel.h"
#include "DWLanguageSubsystem.generated.h"
class ADWLanguageCarrier;
class ADWPlayerCharacter;
class UTexture2D;
class FJsonObject;
class FDWLanguageSelectionStore;
struct FDWCombatStarterGrantState;
struct FDWFormalLanguageSession;
class FDWFormalLanguagePersistence;
namespace DWLanguage { class FModel; }
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWLanguageProgressChanged);

/** Sole owner of playthrough discovery, notebook, completion receipts and talent accounting. */
UCLASS() class GDATTEST_API UDWLanguageSubsystem:public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual ~UDWLanguageSubsystem();
 virtual void Initialize(FSubsystemCollectionBase& Collection)override;
 virtual void Deinitialize()override;
 UPROPERTY(BlueprintAssignable) FDWLanguageProgressChanged OnProgressChanged;
 UPROPERTY(BlueprintAssignable) FDWLanguageProgressChanged OnPanelRequested;
 UFUNCTION(BlueprintPure) bool IsReady()const;
 UFUNCTION(BlueprintPure) int64 GetRevision()const;
 UFUNCTION(BlueprintPure) int32 GetGuessCharacterLimit()const;
 UFUNCTION(BlueprintPure) TArray<FDWLanguageEntryView> GetEntries(EDWLanguageFilter Filter,EDWLanguageSort Sort)const;
 UFUNCTION(BlueprintPure) bool GetEntry(FName EntryId,FDWLanguageEntryView& OutEntry)const;
 UFUNCTION(BlueprintPure) UTexture2D* GetGlyphTexture(FName GlyphId)const;
 UFUNCTION(BlueprintPure) bool GetRelic(FName RelicId,FDWRelicPuzzleView& OutPuzzle)const;
 UFUNCTION(BlueprintPure) bool GetMechanism(FName MechanismId,FDWLanguageMechanismView& OutMechanism)const;
 UFUNCTION(BlueprintPure) int32 GetTalentPoints()const;
 UFUNCTION(BlueprintPure) TArray<FDWTalentNodeView> GetTalents()const;
 UFUNCTION(BlueprintPure) EDWLanguagePanel GetRequestedPanel()const{return RequestedPanel;}
 UFUNCTION(BlueprintPure) FName GetRequestedContextId()const{return RequestedContext;}
 UFUNCTION(BlueprintPure) FName GetRequestedEntryId()const{return RequestedEntry;}
 UFUNCTION(BlueprintCallable) bool SaveGuess(FName EntryId,const FString& Text,FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool SubmitRelic(FName RelicId,const TArray<FName>& MeaningIds,FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool LearnTalent(FName TalentId,FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool ForgetTalent(FName TalentId,FText& OutMessage);
 UFUNCTION(BlueprintCallable) FGuid BeginItemSelection(FName MechanismId,FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool SelectItem(FGuid RequestId,int32 InventorySlot,FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool UnselectItem(FGuid RequestId,int32 SelectionIndex,FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool SubmitOffering(FGuid RequestId,FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool SubmitTrade(FGuid RequestId,FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool SaveTrial(FText& OutMessage);
 UFUNCTION(BlueprintCallable) bool LoadTrial(FText& OutMessage);
 UFUNCTION(BlueprintCallable) void RequestJournal(FName EntryId=NAME_None);
 UFUNCTION(BlueprintCallable) void RequestTalents();
 UFUNCTION(BlueprintCallable) void ClearPanelRequest();
 UFUNCTION(BlueprintCallable) bool ReturnToInteraction(FText& OutMessage);
 UFUNCTION(BlueprintCallable) void CloseInteraction();
 bool StartPlaytest(ADWPlayerCharacter* Player,FString& Error);
 bool Interact(ADWLanguageCarrier* Carrier,bool bLanguageOnly,FText& OutMessage);
 bool HasTalent(FName TalentId)const;
 bool IsTrialWorld()const;
 bool IsFormalSession()const;
 bool IsCombatSandboxSession()const;
 bool HasCurrentA01Context(const ADWPlayerCharacter* ExpectedPlayer=nullptr)const;
 bool StartCombatSandbox(ADWPlayerCharacter* Player,FString& Error);
 bool IsRuntimeConsumable(FName ItemId)const;
 uint64 GetSessionGeneration()const{return SessionGeneration;}
 double Parameter(const FString& Id,double Fallback)const;
 bool CaptureProgressJson(FString& OutJson,FString& Error)const;
 bool ValidateProgressJson(const FString& Json,FString& Error)const;
 bool RestoreProgressJson(const FString& Json,FString& Error);
 void PublishAfterRestore(){PublishProgress();}
 bool TryInteractNearest(ADWPlayerCharacter* Player,bool bLanguageOnly,FText& OutMessage);
 const TSharedPtr<FJsonObject>& GetTrialConfig()const{return TrialConfig;}
 DWLanguage::FModel* GetNativeModel(){return Model.Get();}
 FDWCombatStarterGrantState& GetStarterGrantState();
 const FString& GetPlaythroughId()const{return PlaythroughId;}
 int32 GetTrialContainerVersion()const{return TrialContainerVersion;}
private:
 friend class FDWLanguageProgressPersistence;
 friend class FDWFormalLanguagePersistence;
 TSharedPtr<FDWFormalLanguageSession> FormalSession;
 uint64 SessionGeneration=1;
 TWeakObjectPtr<ADWPlayerCharacter> SandboxPlayer;
 TWeakObjectPtr<UWorld> SandboxWorld;
 TWeakObjectPtr<const UObject> SandboxProfile;
 FGuid SandboxIdentity;
 uint64 SandboxEpoch=0,SandboxEntity=0,SandboxLife=0,SandboxGeneration=0;
 friend class FDWLanguageInventoryTransaction;
 TUniquePtr<DWLanguage::FModel> Model;
 TSharedPtr<FDWLanguageSelectionStore> Selections;
 TSharedPtr<FDWCombatStarterGrantState> StarterGrant;
 TSharedPtr<FJsonObject> Definitions,TrialConfig;
 TMap<FName,TSharedPtr<FJsonObject>> EntryMetadata,ClueMetadata,RelicMetadata,TalentMetadata;
 UPROPERTY(Transient) TMap<FName,TObjectPtr<UTexture2D>> GlyphCache;
 TWeakObjectPtr<ADWPlayerCharacter> ActivePlayer;
 TWeakObjectPtr<ADWLanguageCarrier> ActiveCarrier;
 EDWLanguagePanel RequestedPanel=EDWLanguagePanel::None;
 FName RequestedEntry,RequestedContext;
 FString PlaythroughId;
 int32 TrialContainerVersion=4; // Trial1 was deployed with container v4; formal v5 is separate.
 bool bInitialized=false,bChanging=false;
 void InvalidateLifeContextUnchecked();
 bool LoadDefinitions(FString& Error);
 bool CheckContext(FName Id,EDWLanguageCarrierKind Kind,FText& Message)const;
 void PublishProgress();
};
