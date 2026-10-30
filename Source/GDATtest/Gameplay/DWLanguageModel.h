#pragma once
#include "CoreMinimal.h"

/** Pure native facts. No widgets, actor/world references, delegates, answers-as-notes or inventory writes. */
namespace DWLanguage
{
constexpr int32 StateVersion=1;
enum class EEntryKind:uint8 { Single,Compound };
enum class EEntryFilter:uint8 { All,Singles,Compounds };
enum class EEntrySort:uint8 { UnrecordedFirst,NewestFirst,DiscoveryOrder };
enum class EEntryStatus:uint8 { Undiscovered,Unrecorded,Recorded };
enum class ECode:uint8
{
    Ready,NoChange,AlreadyApplied,AlreadyCompleted,InteractionRequired,UnknownId,InvalidDefinition,InvalidState,
    InvalidRequest,GuessTooLong,IncompleteSolution,IncorrectSolution,InsufficientPoints,PrerequisiteMissing,
    DisabledTalent,DependentLearned,StalePrepared,ArithmeticOverflow,WrongDoorItem,ExcessQuantity
};

struct FEntryDefinition
{
    EEntryKind Kind=EEntryKind::Single;
    TArray<FName> Components; // Ordered positions, including duplicates; Compound may contain Single entries only.
    TSet<FName> AllowedClueIds; // Explicit stable Entry+Clue pairs supplied by data/actors, never proximity.
};
struct FRelicDefinition
{
    TArray<FName> AnswerIds; // Meaning/candidate IDs, independent of every player guess.
    TSet<FName> CandidateAnswerIds;
    int32 RewardPoints=1;
};
struct FTalentDefinition
{
    int32 Cost=1;
    bool bEnabled=true;
    TArray<FName> Prerequisites; // All, not any. The complete graph must be a DAG.
};
struct FDoorDefinition
{
    FName RequiredItemId;
    int32 RequiredCount=2;
    int32 MaxUnitsPerSubmission=1;
    int32 RewardPoints=0;
};
struct FCatalog
{
    TMap<FName,FEntryDefinition> Entries;
    TMap<FName,FRelicDefinition> Relics;
    TMap<FName,FTalentDefinition> Talents;
    TMap<FName,FDoorDefinition> Doors;
    TSet<FName> KnownItemIds;
    TMap<FName,int32> PointSources; // Explicit additional trial sources; callers cannot mint arbitrary amounts.
    int32 MaxGuessCharacters=40; // Unicode scalar count, after trim. Too long fails; never silently truncates.
    int32 MaxTalentDepth=32;
};
struct FEntryState
{
    int64 FirstDiscoveryOrder=0;
    FString Guess;
    TArray<FName> ClueIds;
    TArray<FName> AppearedInCompounds; // Composition sources are not forged scene clues.
};
struct FRelicCompletion
{
    FName RewardSourceId;
    int32 AwardedPoints=0; // Historical actual amount, not the current configurable reward.
};
struct FDoorProgress
{
    int32 AcceptedCount=0;
    bool bCompleted=false;
    FName RewardSourceId;
    int32 AwardedPoints=0;
};
struct FDoorSubmission
{
    FName DoorId,ItemId;
    int32 Quantity=0,AcceptedAfter=0;
    bool bAccepted=false,bCompletedNow=false;
};
struct FState
{
    int32 Version=StateVersion;
    int64 Revision=0,NextDiscoveryOrder=1;
    TMap<FName,FEntryState> Entries; // Undiscovered entries are absent, never editable.
    TMap<FName,FRelicCompletion> CompletedRelics;
    TMap<FName,FDoorProgress> DoorProgress;
    TMap<FName,FDoorSubmission> ProcessedDoorSubmissions; // Persisted stable request IDs prevent repeated payment.
    TMap<FName,int32> RewardSources;
    int32 AvailableTalentPoints=0;
    TMap<FName,int32> LearnedTalentPaidCosts;
};
struct FOperationResult
{
    ECode Code=ECode::InvalidRequest;
    FName SubjectId;
    int32 PointDelta=0,AcceptedCountDelta=0,InventoryQuantity=0;
    bool bConsumeInventory=false,bCompletedNow=false,bCommittedThisCall=false;
};
class FModel;
/** Owner-epoch-bound, revision-bound, move-only, single-use complete candidate. */
class FPreparedChange final
{
public:
    FPreparedChange()=default;
    FPreparedChange(const FPreparedChange&)=delete;
    FPreparedChange& operator=(const FPreparedChange&)=delete;
    FPreparedChange(FPreparedChange&&) noexcept;
    FPreparedChange& operator=(FPreparedChange&&) noexcept;
    bool IsConsumed()const{return bConsumed;}
    int64 GetExpectedRevision()const{return ExpectedRevision;}
    const FState& GetCandidateState()const{return Candidate;}
    const FOperationResult& GetResult()const{return Result;}
private:
    friend class FModel;
    TWeakPtr<const int32> OwnerEpoch;
    TSharedPtr<const int32> PreparedRestoreEpoch;
    FState Candidate;
    FOperationResult Result;
    int64 ExpectedRevision=INDEX_NONE;
    bool bConsumed=true,bRestore=false;
    void Reset();
};

FName RelicRewardSource(FName RelicId);
FName DoorRewardSource(FName DoorId);
bool ValidateCatalog(const FCatalog&,FString& Error);
bool ValidateState(const FCatalog&,const FState&,FString& Error);
bool StateToJson(const FCatalog&,const FState&,FString& OutJson,FString& Error);
bool StateFromJson(const FCatalog&,const FString& Json,FState& OutState,FString& Error);

class FModel final
{
public:
    FModel()=default;
    FModel(const FModel&)=delete;
    FModel& operator=(const FModel&)=delete;
    /** Validated initialization/replacement only; success changes epoch and invalidates outstanding tokens. */
    bool Initialize(const FCatalog&,const FState&,FString& Error);
    bool IsReady()const{return bReady;}
    const FCatalog& GetCatalog()const{return Catalog;}
    const FState& GetState()const{return State;}
    EEntryStatus GetEntryStatus(FName EntryId)const;
    TArray<FName> BuildEntryOrder(EEntryFilter Filter,EEntrySort Sort)const;

    // bool means a complete new candidate exists. NoChange/AlreadyApplied/etc are explicit non-mutations.
    bool PrepareDiscover(FName EntryId,FName ClueId,bool bExplicitInteraction,FPreparedChange&,FOperationResult&)const;
    bool PrepareDiscoverBatch(const TArray<TPair<FName,FName>>& EntryClues,bool bExplicitInteraction,FPreparedChange&,FOperationResult&)const;
    bool PrepareSaveGuess(FName EntryId,const FString& PlayerInput,FPreparedChange&,FOperationResult&)const;
    bool PrepareSolveRelic(FName RelicId,const TArray<FName>& SubmittedAnswerIds,FPreparedChange&,FOperationResult&)const;
    bool PrepareGrantPoints(FName ConfiguredSourceId,FPreparedChange&,FOperationResult&)const;
    bool PrepareLearnTalent(FName TalentId,FPreparedChange&,FOperationResult&)const;
    bool PrepareForgetTalent(FName TalentId,FPreparedChange&,FOperationResult&)const;
    /** Wrong item returns true + WrongDoorItem + bConsumeInventory; root must atomically pay that inventory cost. */
    bool PrepareSubmitDoor(FName DoorId,FName SubmissionId,FName ItemId,int32 Quantity,FPreparedChange&,FOperationResult&)const;
    bool PrepareRestore(const FState&,FPreparedChange&,FOperationResult&)const;
    bool RestoreValidated(const FState&,FString& Error);
    /** Pure final revalidation, suitable before the root's no-callback joint inventory commit. */
    bool CanCommit(const FPreparedChange&)const;
    /** Concrete root coordinator only: call immediately after CanCommit with no intervening callback/mutation. */
    void AdoptPreparedUnchecked(FPreparedChange&&,FOperationResult&);
    /** No callbacks, world access or inventory changes. Failure also consumes the token. */
    bool Commit(FPreparedChange&&,FOperationResult&);
private:
    FCatalog Catalog;
    FState State;
    TSharedPtr<const int32> Epoch;
    bool bReady=false;
    bool MakePrepared(FState&& Candidate,FPreparedChange&,FOperationResult&,bool bRestore=false)const;
};
}
