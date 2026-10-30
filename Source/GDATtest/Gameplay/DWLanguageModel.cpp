#include "DWLanguageModel.h"
#include "DWRetiredItemPolicy.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "String/LexFromString.h"
#include <initializer_list>

namespace DWLanguageDetail
{
using namespace DWLanguage;
bool Fail(FString& Error,const TCHAR* Message){Error=Message;return false;}
bool DisabledId(FName Id){return Id==TEXT("a5")||Id==TEXT("a6");}
bool TextLength(const FString& Text,int32& Out)
{
    Out=0;
    for(int32 I=0;I<Text.Len();++I)
    {
        const uint32 C=static_cast<uint32>(Text[I]);if(!C)return false;
        if(C>=0xD800&&C<=0xDBFF)
        {
            if(++I>=Text.Len())return false;
            const uint32 Low=static_cast<uint32>(Text[I]);if(Low<0xDC00||Low>0xDFFF)return false;
        }
        else if(C>=0xDC00&&C<=0xDFFF)return false;
        ++Out;
    }
    return true;
}
template<class T>TArray<FName> SortedKeys(const TMap<FName,T>& Map)
{TArray<FName> Keys;Map.GetKeys(Keys);Keys.Sort(FNameLexicalLess());return Keys;}
bool UniqueNames(const TArray<FName>& Values)
{TSet<FName> Seen;for(FName Id:Values){if(Id.IsNone()||Seen.Contains(Id))return false;Seen.Add(Id);}return true;}
bool Grant(FState& State,FName Source,int32 Amount,FOperationResult& Result)
{
    if(Source.IsNone()||Amount<0||State.RewardSources.Contains(Source))return false;
    const int64 Points=static_cast<int64>(State.AvailableTalentPoints)+Amount;
    if(Points>MAX_int32)return false;
    State.RewardSources.Add(Source,Amount);State.AvailableTalentPoints=static_cast<int32>(Points);Result.PointDelta=Amount;return true;
}
bool Reject(FPreparedChange& Plan,FOperationResult& Result,ECode Code,FName Subject=NAME_None)
{Plan=FPreparedChange();Result={};Result.Code=Code;Result.SubjectId=Subject;return false;}
}

namespace DWLanguage
{
FName RelicRewardSource(FName Id){return Id.IsNone()?NAME_None:FName(*(TEXT("Relic/")+Id.ToString()));}
FName DoorRewardSource(FName Id){return Id.IsNone()?NAME_None:FName(*(TEXT("Door/")+Id.ToString()));}

void FPreparedChange::Reset()
{OwnerEpoch.Reset();PreparedRestoreEpoch.Reset();Candidate={};Result={};ExpectedRevision=INDEX_NONE;bConsumed=true;bRestore=false;}
FPreparedChange::FPreparedChange(FPreparedChange&& Other) noexcept{*this=MoveTemp(Other);}
FPreparedChange& FPreparedChange::operator=(FPreparedChange&& Other) noexcept
{
    if(this!=&Other)
    {
        OwnerEpoch=MoveTemp(Other.OwnerEpoch);PreparedRestoreEpoch=MoveTemp(Other.PreparedRestoreEpoch);Candidate=MoveTemp(Other.Candidate);Result=Other.Result;
        ExpectedRevision=Other.ExpectedRevision;bConsumed=Other.bConsumed;bRestore=Other.bRestore;Other.Reset();
    }
    return *this;
}

bool ValidateCatalog(const FCatalog& C,FString& Error)
{
    using namespace DWLanguageDetail;Error.Reset();
    if(C.MaxGuessCharacters<1||C.MaxGuessCharacters>2048||C.MaxTalentDepth<1||C.MaxTalentDepth>256)
        return Fail(Error,TEXT("Invalid guess limit or talent depth limit."));
    for(const auto& Pair:C.Entries)
    {
        const auto& E=Pair.Value;
        if(Pair.Key.IsNone()||(E.Kind!=EEntryKind::Single&&E.Kind!=EEntryKind::Compound))return Fail(Error,TEXT("Invalid entry identity/kind."));
        for(FName Clue:E.AllowedClueIds)if(Clue.IsNone())return Fail(Error,TEXT("Empty clue identity."));
        if(E.Kind==EEntryKind::Single&&!E.Components.IsEmpty())return Fail(Error,TEXT("Single entry has components."));
        if(E.Kind==EEntryKind::Compound)
        {
            if(E.Components.IsEmpty())return Fail(Error,TEXT("Compound has no ordered components."));
            for(FName Id:E.Components)
            {const auto* Part=C.Entries.Find(Id);if(!Part||Part->Kind!=EEntryKind::Single)return Fail(Error,TEXT("Compound references an unknown or non-single component."));}
        }
    }
    TSet<FName> ReservedSources;
    for(const auto& Pair:C.Relics)
    {
        const auto& R=Pair.Value;if(Pair.Key.IsNone()||R.AnswerIds.IsEmpty()||R.RewardPoints<0)return Fail(Error,TEXT("Invalid relic definition."));
        for(FName Id:R.CandidateAnswerIds)if(Id.IsNone())return Fail(Error,TEXT("Empty candidate answer identity."));
        for(FName Id:R.AnswerIds)if(Id.IsNone()||!R.CandidateAnswerIds.Contains(Id))return Fail(Error,TEXT("Relic candidate pool omits a required answer."));
        ReservedSources.Add(RelicRewardSource(Pair.Key));
    }
    for(FName Id:C.KnownItemIds)if(Id.IsNone())return Fail(Error,TEXT("Empty known item identity."));
    for(const auto& Pair:C.Doors)
    {
        const auto& D=Pair.Value;
        if(Pair.Key.IsNone()||!C.KnownItemIds.Contains(D.RequiredItemId)||D.RequiredCount<1||
            D.MaxUnitsPerSubmission<1||D.MaxUnitsPerSubmission>D.RequiredCount||D.RewardPoints<0)
            return Fail(Error,TEXT("Invalid door definition or unknown required item."));
        ReservedSources.Add(DoorRewardSource(Pair.Key));
    }
    for(const auto& Pair:C.PointSources)
        if(Pair.Key.IsNone()||Pair.Value<0||ReservedSources.Contains(Pair.Key))return Fail(Error,TEXT("Invalid or colliding additional point source."));
    for(const auto& Pair:C.Talents)
    {
        if(Pair.Key.IsNone()||Pair.Value.Cost<0||(DisabledId(Pair.Key)&&Pair.Value.bEnabled)||!UniqueNames(Pair.Value.Prerequisites))
            return Fail(Error,TEXT("Invalid talent cost, identity or disabled a5/a6 definition."));
        for(FName Prerequisite:Pair.Value.Prerequisites)
            if(Prerequisite==Pair.Key||!C.Talents.Contains(Prerequisite))return Fail(Error,TEXT("Unknown/self talent prerequisite."));
    }
    // Iterative topological validation: even a maliciously deep chain cannot recurse on the C++ stack.
    TMap<FName,int32> Remaining,Depths;TMap<FName,TArray<FName>> Children;TArray<FName> Queue;
    for(const auto& Pair:C.Talents)
    {
        Remaining.Add(Pair.Key,Pair.Value.Prerequisites.Num());Depths.Add(Pair.Key,1);
        if(Pair.Value.Prerequisites.IsEmpty())Queue.Add(Pair.Key);
        for(FName P:Pair.Value.Prerequisites)Children.FindOrAdd(P).Add(Pair.Key);
    }
    for(int32 Index=0;Index<Queue.Num();++Index)
        if(const auto* Next=Children.Find(Queue[Index]))for(FName Child:*Next)
        {
            int32& Depth=Depths.FindChecked(Child);Depth=FMath::Max(Depth,Depths.FindChecked(Queue[Index])+1);
            if(Depth>C.MaxTalentDepth)return Fail(Error,TEXT("Talent graph maximum depth exceeded."));
            if(--Remaining.FindChecked(Child)==0)Queue.Add(Child);
        }
    if(Queue.Num()!=C.Talents.Num())return Fail(Error,TEXT("Talent graph contains a cycle."));
    return true;
}

bool ValidateState(const FCatalog& C,const FState& S,FString& Error)
{
    using namespace DWLanguageDetail;if(!ValidateCatalog(C,Error))return false;
    if(S.Version!=StateVersion||S.Revision<0||S.Revision==MAX_int64||S.NextDiscoveryOrder<1||S.AvailableTalentPoints<0)
        return Fail(Error,TEXT("Invalid state version, revision, discovery counter or points."));
    if(S.NextDiscoveryOrder!=static_cast<int64>(S.Entries.Num())+1)return Fail(Error,TEXT("Discovery sequence contains gaps."));
    TSet<int64> Orders;
    for(const auto& Pair:S.Entries)
    {
        const auto* Definition=C.Entries.Find(Pair.Key);const auto& E=Pair.Value;int32 Length=0;
        if(!Definition||E.FirstDiscoveryOrder<1||E.FirstDiscoveryOrder>=S.NextDiscoveryOrder||Orders.Contains(E.FirstDiscoveryOrder)||
            !TextLength(E.Guess,Length)||Length>C.MaxGuessCharacters||E.Guess!=E.Guess.TrimStartAndEnd()||
            !UniqueNames(E.ClueIds)||!UniqueNames(E.AppearedInCompounds)||(E.ClueIds.IsEmpty()&&E.AppearedInCompounds.IsEmpty()))
            return Fail(Error,TEXT("Invalid entry state, note, discovery source or duplicate order."));
        Orders.Add(E.FirstDiscoveryOrder);
        for(FName Clue:E.ClueIds)if(!Definition->AllowedClueIds.Contains(Clue))return Fail(Error,TEXT("Unknown Entry+Clue pair."));
        for(FName Compound:E.AppearedInCompounds)
        {
            const auto* Word=C.Entries.Find(Compound);
            if(Definition->Kind!=EEntryKind::Single||!Word||Word->Kind!=EEntryKind::Compound||!Word->Components.Contains(Pair.Key)||!S.Entries.Contains(Compound))
                return Fail(Error,TEXT("Unknown or inconsistent composition discovery source."));
        }
        if(Definition->Kind==EEntryKind::Compound)
            for(FName Part:Definition->Components)
            {const auto* Found=S.Entries.Find(Part);if(!Found||!Found->AppearedInCompounds.Contains(Pair.Key))return Fail(Error,TEXT("Discovered compound is missing its component discovery facts."));}
    }
    for(const auto& Pair:S.CompletedRelics)
    {
        const auto& R=Pair.Value;const auto* Award=S.RewardSources.Find(R.RewardSourceId);
        if(!C.Relics.Contains(Pair.Key)||R.RewardSourceId!=RelicRewardSource(Pair.Key)||R.AwardedPoints<0||!Award||*Award!=R.AwardedPoints)
            return Fail(Error,TEXT("Relic completion and historical point award disagree."));
    }
    TMap<FName,int64> DoorAccepted;TMap<FName,int32> DoorCompletions;
    TMap<FName,TArray<FDoorSubmission>> AcceptedHistory;
    for(const auto& Pair:S.ProcessedDoorSubmissions)
    {
        const auto& R=Pair.Value;const auto* D=C.Doors.Find(R.DoorId);const auto* Progress=S.DoorProgress.Find(R.DoorId);
        if(Pair.Key.IsNone()||!D||!Progress||!C.KnownItemIds.Contains(R.ItemId)||R.Quantity<1||R.Quantity>D->MaxUnitsPerSubmission||
            R.AcceptedAfter<0||R.AcceptedAfter>D->RequiredCount||R.bAccepted!=(R.ItemId==D->RequiredItemId)||
            (R.bCompletedNow&&(!R.bAccepted||R.AcceptedAfter!=D->RequiredCount)))
            return Fail(Error,TEXT("Invalid persisted door submission receipt."));
        if(R.bAccepted){DoorAccepted.FindOrAdd(R.DoorId)+=R.Quantity;AcceptedHistory.FindOrAdd(R.DoorId).Add(R);}
        if(R.bCompletedNow)++DoorCompletions.FindOrAdd(R.DoorId);
    }
    for(auto& Pair:AcceptedHistory)
    {
        Pair.Value.Sort([](const FDoorSubmission& A,const FDoorSubmission& B){return A.AcceptedAfter<B.AcceptedAfter;});
        int64 Sum=0;for(const auto& R:Pair.Value){Sum+=R.Quantity;if(Sum!=R.AcceptedAfter)return Fail(Error,TEXT("Door accepted history is not cumulative."));}
    }
    for(const auto& Pair:S.DoorProgress)
    {
        const auto* D=C.Doors.Find(Pair.Key);const auto& P=Pair.Value;
        if(!D||P.AcceptedCount<0||P.AcceptedCount>D->RequiredCount||DoorAccepted.FindRef(Pair.Key)!=P.AcceptedCount||
            P.bCompleted!=(P.AcceptedCount==D->RequiredCount)||DoorCompletions.FindRef(Pair.Key)!=(P.bCompleted?1:0))
            return Fail(Error,TEXT("Door progress, completion and payment receipts disagree."));
        if(P.bCompleted)
        {
            const auto* Award=S.RewardSources.Find(P.RewardSourceId);
            if(P.RewardSourceId!=DoorRewardSource(Pair.Key)||P.AwardedPoints<0||!Award||*Award!=P.AwardedPoints)
                return Fail(Error,TEXT("Door completion and reward source disagree."));
        }
        else if(!P.RewardSourceId.IsNone()||P.AwardedPoints!=0)return Fail(Error,TEXT("Incomplete door contains a reward."));
    }
    int64 TotalRewards=0,TotalPaid=0;
    for(const auto& Pair:S.RewardSources)
    {
        bool Known=C.PointSources.Contains(Pair.Key);
        for(const auto& Relic:S.CompletedRelics)Known|=Relic.Value.RewardSourceId==Pair.Key;
        for(const auto& Door:S.DoorProgress)Known|=Door.Value.bCompleted&&Door.Value.RewardSourceId==Pair.Key;
        if(Pair.Key.IsNone()||!Known||Pair.Value<0)return Fail(Error,TEXT("Unknown point source or negative award."));
        TotalRewards+=Pair.Value;if(TotalRewards>MAX_int32)return Fail(Error,TEXT("Point award total overflows."));
    }
    for(const auto& Pair:S.LearnedTalentPaidCosts)
    {
        const auto* T=C.Talents.Find(Pair.Key);
        if(!T||!T->bEnabled||DisabledId(Pair.Key)||Pair.Value<0)return Fail(Error,TEXT("Unknown/disabled learned talent or negative actual cost."));
        for(FName Prerequisite:T->Prerequisites)if(!S.LearnedTalentPaidCosts.Contains(Prerequisite))return Fail(Error,TEXT("Learned talent lacks a prerequisite."));
        TotalPaid+=Pair.Value;if(TotalPaid>MAX_int32)return Fail(Error,TEXT("Actual talent cost total overflows."));
    }
    if(TotalRewards!=TotalPaid+S.AvailableTalentPoints)return Fail(Error,TEXT("Talent points do not conserve recorded awards and actual payments."));
    Error.Reset();return true;
}

bool FModel::Initialize(const FCatalog& InCatalog,const FState& InState,FString& Error)
{
    if(!ValidateState(InCatalog,InState,Error))return false;
    FCatalog NewCatalog=InCatalog;FState NewState=InState;TSharedPtr<const int32> NewEpoch=MakeShared<int32>(0);
    Catalog=MoveTemp(NewCatalog);State=MoveTemp(NewState);Epoch=MoveTemp(NewEpoch);bReady=true;return true;
}
EEntryStatus FModel::GetEntryStatus(FName Id)const
{const auto* E=State.Entries.Find(Id);return !E?EEntryStatus::Undiscovered:(E->Guess.IsEmpty()?EEntryStatus::Unrecorded:EEntryStatus::Recorded);}
TArray<FName> FModel::BuildEntryOrder(EEntryFilter Filter,EEntrySort Sort)const
{
    TArray<FName> Out;if(!bReady)return Out;
    for(const auto& Pair:Catalog.Entries)
        if(Filter==EEntryFilter::All||(Filter==EEntryFilter::Singles&&Pair.Value.Kind==EEntryKind::Single)||
            (Filter==EEntryFilter::Compounds&&Pair.Value.Kind==EEntryKind::Compound))Out.Add(Pair.Key);
    Out.Sort([&](FName A,FName B)
    {
        const auto* EA=State.Entries.Find(A);const auto* EB=State.Entries.Find(B);
        if(Sort==EEntrySort::UnrecordedFirst)
        {
            auto Rank=[](const FEntryState* E){return !E?1:(E->Guess.IsEmpty()?0:2);};
            const int32 RA=Rank(EA),RB=Rank(EB);if(RA!=RB)return RA<RB;
        }
        else if(bool(EA)!=bool(EB))return EA!=nullptr;
        if(EA&&EB&&EA->FirstDiscoveryOrder!=EB->FirstDiscoveryOrder)
            return Sort==EEntrySort::NewestFirst?EA->FirstDiscoveryOrder>EB->FirstDiscoveryOrder:EA->FirstDiscoveryOrder<EB->FirstDiscoveryOrder;
        return FNameLexicalLess()(A,B);
    });
    return Out;
}
bool FModel::MakePrepared(FState&& Candidate,FPreparedChange& Plan,FOperationResult& Result,bool Restore)const
{
    Plan.Reset();
    if(!bReady||State.Revision>=MAX_int64-1){Result.Code=ECode::ArithmeticOverflow;return false;}
    Candidate.Revision=State.Revision+1;FString Error;
    if(!ValidateState(Catalog,Candidate,Error)){Result.Code=ECode::InvalidState;return false;}
    Plan.OwnerEpoch=Epoch;Plan.ExpectedRevision=State.Revision;Plan.Candidate=MoveTemp(Candidate);Plan.Result=Result;
    Plan.bConsumed=false;Plan.bRestore=Restore;if(Restore)Plan.PreparedRestoreEpoch=MakeShared<int32>(0);return true;
}
bool FModel::PrepareDiscover(FName Id,FName Clue,bool Explicit,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidRequest,Id);
    const auto* Definition=Catalog.Entries.Find(Id);if(!bReady||!Definition||!Definition->AllowedClueIds.Contains(Clue))return Reject(Plan,Result,ECode::UnknownId,Id);
    if(!Explicit)return Reject(Plan,Result,ECode::InteractionRequired,Id);
    const auto* Existing=State.Entries.Find(Id);if(Existing&&Existing->ClueIds.Contains(Clue))return Reject(Plan,Result,ECode::AlreadyApplied,Id);
    FState Candidate=State;
    auto Discover=[&](FName Entry)->FEntryState&
    {
        auto& E=Candidate.Entries.FindOrAdd(Entry);if(!E.FirstDiscoveryOrder)E.FirstDiscoveryOrder=Candidate.NextDiscoveryOrder++;
        return E;
    };
    Discover(Id).ClueIds.Add(Clue);
    for(FName Part:Definition->Components)Discover(Part).AppearedInCompounds.AddUnique(Id);
    Result.Code=ECode::Ready;return MakePrepared(MoveTemp(Candidate),Plan,Result);
}
bool FModel::PrepareSaveGuess(FName Id,const FString& Input,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidRequest,Id);
    if(!bReady||!Catalog.Entries.Contains(Id))return Reject(Plan,Result,ECode::UnknownId,Id);
    const auto* Existing=State.Entries.Find(Id);if(!Existing)return false;
    const FString Guess=Input.TrimStartAndEnd();int32 Length=0;
    if(!TextLength(Guess,Length))return false;
    if(Length>Catalog.MaxGuessCharacters)return Reject(Plan,Result,ECode::GuessTooLong,Id);
    if(Existing->Guess==Guess)return Reject(Plan,Result,ECode::NoChange,Id);
    FState Candidate=State;Candidate.Entries.FindChecked(Id).Guess=Guess;Result.Code=ECode::Ready;return MakePrepared(MoveTemp(Candidate),Plan,Result);
}
bool FModel::PrepareDiscoverBatch(const TArray<TPair<FName,FName>>& Pairs,bool Explicit,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidRequest,NAME_None);
    if(!bReady||Pairs.IsEmpty())return false;
    if(!Explicit)return Reject(Plan,Result,ECode::InteractionRequired,NAME_None);
    for(const auto& Pair:Pairs)
    {const auto* D=Catalog.Entries.Find(Pair.Key);if(!D||!D->AllowedClueIds.Contains(Pair.Value))return Reject(Plan,Result,ECode::UnknownId,Pair.Key);}
    FState Candidate=State;bool Changed=false;
    auto Discover=[&](FName Id)->FEntryState&
    {auto& E=Candidate.Entries.FindOrAdd(Id);if(!E.FirstDiscoveryOrder)E.FirstDiscoveryOrder=Candidate.NextDiscoveryOrder++;return E;};
    for(const auto& Pair:Pairs)
    {
        auto& E=Discover(Pair.Key);if(E.ClueIds.Contains(Pair.Value))continue;
        E.ClueIds.Add(Pair.Value);Changed=true;
        for(FName Part:Catalog.Entries.FindChecked(Pair.Key).Components)Discover(Part).AppearedInCompounds.AddUnique(Pair.Key);
    }
    if(!Changed)return Reject(Plan,Result,ECode::AlreadyApplied,NAME_None);
    Result.Code=ECode::Ready;return MakePrepared(MoveTemp(Candidate),Plan,Result);
}
bool FModel::PrepareSolveRelic(FName Id,const TArray<FName>& Answers,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidRequest,Id);const auto* R=Catalog.Relics.Find(Id);
    if(!bReady||!R)return Reject(Plan,Result,ECode::UnknownId,Id);
    if(State.CompletedRelics.Contains(Id))return Reject(Plan,Result,ECode::AlreadyCompleted,Id);
    if(Answers.Num()<R->AnswerIds.Num()||Answers.Contains(NAME_None))return Reject(Plan,Result,ECode::IncompleteSolution,Id);
    if(Answers.Num()!=R->AnswerIds.Num())return false;
    for(FName Answer:Answers)if(!R->CandidateAnswerIds.Contains(Answer))return Reject(Plan,Result,ECode::UnknownId,Id);
    if(Answers!=R->AnswerIds)return Reject(Plan,Result,ECode::IncorrectSolution,Id);
    FState Candidate=State;const FName Source=RelicRewardSource(Id);
    if(!Grant(Candidate,Source,R->RewardPoints,Result))return Reject(Plan,Result,ECode::ArithmeticOverflow,Id);
    Candidate.CompletedRelics.Add(Id,{Source,R->RewardPoints});Result.Code=ECode::Ready;Result.bCompletedNow=true;
    return MakePrepared(MoveTemp(Candidate),Plan,Result);
}
bool FModel::PrepareGrantPoints(FName Source,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidRequest,Source);const auto* Amount=Catalog.PointSources.Find(Source);
    if(!bReady||!Amount)return Reject(Plan,Result,ECode::UnknownId,Source);
    if(State.RewardSources.Contains(Source))return Reject(Plan,Result,ECode::AlreadyApplied,Source);
    FState Candidate=State;if(!Grant(Candidate,Source,*Amount,Result))return Reject(Plan,Result,ECode::ArithmeticOverflow,Source);
    Result.Code=ECode::Ready;return MakePrepared(MoveTemp(Candidate),Plan,Result);
}
bool FModel::PrepareLearnTalent(FName Id,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidRequest,Id);const auto* T=Catalog.Talents.Find(Id);
    if(!bReady||!T)return Reject(Plan,Result,ECode::UnknownId,Id);
    if(!T->bEnabled||DisabledId(Id))return Reject(Plan,Result,ECode::DisabledTalent,Id);
    if(State.LearnedTalentPaidCosts.Contains(Id))return Reject(Plan,Result,ECode::AlreadyApplied,Id);
    for(FName P:T->Prerequisites)if(!State.LearnedTalentPaidCosts.Contains(P))return Reject(Plan,Result,ECode::PrerequisiteMissing,Id);
    if(State.AvailableTalentPoints<T->Cost)return Reject(Plan,Result,ECode::InsufficientPoints,Id);
    FState Candidate=State;Candidate.AvailableTalentPoints-=T->Cost;Candidate.LearnedTalentPaidCosts.Add(Id,T->Cost);
    Result.Code=ECode::Ready;Result.PointDelta=-T->Cost;return MakePrepared(MoveTemp(Candidate),Plan,Result);
}
bool FModel::PrepareForgetTalent(FName Id,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidRequest,Id);
    if(!bReady||!Catalog.Talents.Contains(Id))return Reject(Plan,Result,ECode::UnknownId,Id);
    const auto* Paid=State.LearnedTalentPaidCosts.Find(Id);if(!Paid)return Reject(Plan,Result,ECode::NoChange,Id);
    for(const auto& Learned:State.LearnedTalentPaidCosts)
        if(Catalog.Talents.FindChecked(Learned.Key).Prerequisites.Contains(Id))return Reject(Plan,Result,ECode::DependentLearned,Id);
    const int64 Points=static_cast<int64>(State.AvailableTalentPoints)+*Paid;if(Points>MAX_int32)return Reject(Plan,Result,ECode::ArithmeticOverflow,Id);
    FState Candidate=State;Candidate.AvailableTalentPoints=static_cast<int32>(Points);Candidate.LearnedTalentPaidCosts.Remove(Id);
    Result.Code=ECode::Ready;Result.PointDelta=*Paid;return MakePrepared(MoveTemp(Candidate),Plan,Result);
}
bool FModel::PrepareSubmitDoor(FName Id,FName Submission,FName Item,int32 Quantity,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidRequest,Id);const auto* D=Catalog.Doors.Find(Id);
    if(!bReady||!D||!Catalog.KnownItemIds.Contains(Item)||DWRetiredItemPolicy::IsRetired(Item))return Reject(Plan,Result,ECode::UnknownId,Id);
    if(Submission.IsNone()||Quantity<1||Quantity>D->MaxUnitsPerSubmission)return false;
    if(const auto* Previous=State.ProcessedDoorSubmissions.Find(Submission))
        return Reject(Plan,Result,Previous->DoorId==Id&&Previous->ItemId==Item&&Previous->Quantity==Quantity?ECode::AlreadyApplied:ECode::InvalidRequest,Id);
    const auto* Existing=State.DoorProgress.Find(Id);if(Existing&&Existing->bCompleted)return Reject(Plan,Result,ECode::AlreadyCompleted,Id);
    const int32 Before=Existing?Existing->AcceptedCount:0;const bool Correct=Item==D->RequiredItemId;
    if(Correct&&static_cast<int64>(Before)+Quantity>D->RequiredCount)return Reject(Plan,Result,ECode::ExcessQuantity,Id);
    FState Candidate=State;auto& Progress=Candidate.DoorProgress.FindOrAdd(Id);
    if(Correct)Progress.AcceptedCount+=Quantity;
    const bool Complete=Correct&&Progress.AcceptedCount==D->RequiredCount;
    if(Complete)
    {
        const FName Source=DoorRewardSource(Id);if(!Grant(Candidate,Source,D->RewardPoints,Result))return Reject(Plan,Result,ECode::ArithmeticOverflow,Id);
        Progress.bCompleted=true;Progress.RewardSourceId=Source;Progress.AwardedPoints=D->RewardPoints;
    }
    Candidate.ProcessedDoorSubmissions.Add(Submission,{Id,Item,Quantity,Progress.AcceptedCount,Correct,Complete});
    Result.Code=Correct?ECode::Ready:ECode::WrongDoorItem;Result.bConsumeInventory=true;Result.InventoryQuantity=Quantity;
    Result.AcceptedCountDelta=Correct?Quantity:0;Result.bCompletedNow=Complete;return MakePrepared(MoveTemp(Candidate),Plan,Result);
}
bool FModel::PrepareRestore(const FState& Saved,FPreparedChange& Plan,FOperationResult& Result)const
{
    using namespace DWLanguageDetail;Reject(Plan,Result,ECode::InvalidState);FString Error;
    if(!bReady||!ValidateState(Catalog,Saved,Error))return false;
    FState Candidate=Saved;Result.Code=ECode::Ready;return MakePrepared(MoveTemp(Candidate),Plan,Result,true);
}
bool FModel::RestoreValidated(const FState& Saved,FString& Error)
{
    if(!bReady||!ValidateState(Catalog,Saved,Error))return false;
    FPreparedChange Plan;FOperationResult Result;
    if(!PrepareRestore(Saved,Plan,Result)||!Commit(MoveTemp(Plan),Result))return DWLanguageDetail::Fail(Error,TEXT("Restore cannot commit at the current revision."));
    Error.Reset();return true;
}
bool FModel::CanCommit(const FPreparedChange& Plan)const
{
    if(!bReady||Plan.bConsumed||Plan.OwnerEpoch.Pin()!=Epoch||Plan.ExpectedRevision!=State.Revision||
        State.Revision>=MAX_int64-1||Plan.Candidate.Revision!=State.Revision+1)return false;
    FString Error;return ValidateState(Catalog,Plan.Candidate,Error);
}
bool FModel::Commit(FPreparedChange&& Plan,FOperationResult& Result)
{
    if(!CanCommit(Plan)){Plan.Reset();Result={};Result.Code=ECode::StalePrepared;return false;}
    AdoptPreparedUnchecked(MoveTemp(Plan),Result);return true;
}
void FModel::AdoptPreparedUnchecked(FPreparedChange&& Plan,FOperationResult& Result)
{
    check(bReady&&!Plan.bConsumed&&Plan.OwnerEpoch.Pin()==Epoch&&Plan.ExpectedRevision==State.Revision&&Plan.Candidate.Revision==State.Revision+1);
    // Every allocation and full validation happened in prepare/CanCommit. No callbacks or world access.
    Result=Plan.Result;Result.bCommittedThisCall=true;State=MoveTemp(Plan.Candidate);
    if(Plan.bRestore)Epoch=MoveTemp(Plan.PreparedRestoreEpoch);Plan.Reset();
}
}

namespace DWLanguageJson
{
using namespace DWLanguage;
using FObject=TSharedPtr<FJsonObject>;
bool Fields(const FObject& O,std::initializer_list<const TCHAR*> Names)
{if(!O||O->Values.Num()!=static_cast<int32>(Names.size()))return false;for(const TCHAR* N:Names)if(!O->HasField(N))return false;return true;}
bool Name(const FObject& O,const TCHAR* Field,FName& Out)
{FString S;if(!O->TryGetStringField(Field,S)||S.IsEmpty())return false;Out=FName(*S);return !Out.IsNone();}
bool Int(const FObject& O,const TCHAR* Field,int32& Out)
{
    double V=0.;if(!O->TryGetNumberField(Field,V)||!FMath::IsFinite(V)||V<MIN_int32||V>MAX_int32||FMath::FloorToDouble(V)!=V)return false;
    Out=static_cast<int32>(V);return true;
}
bool Int64(const FObject& O,const TCHAR* Field,int64& Out)
{FString S;return O->TryGetStringField(Field,S)&&LexTryParseString(Out,*S)&&LexToString(Out)==S;}
TArray<TSharedPtr<FJsonValue>> Names(const TArray<FName>& Values)
{TArray<TSharedPtr<FJsonValue>> A;for(FName Id:Values)A.Add(MakeShared<FJsonValueString>(Id.ToString()));return A;}
bool Names(const FObject& O,const TCHAR* Field,TArray<FName>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!O->TryGetArrayField(Field,Values))return false;
    for(const auto& V:*Values){FString S;if(!V||!V->TryGetString(S)||S.IsEmpty())return false;const FName Id(*S);if(Id.IsNone())return false;Out.Add(Id);}return true;
}
bool Array(const FObject& O,const TCHAR* Field,TFunctionRef<bool(const FObject&)> Read)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!O->TryGetArrayField(Field,Values))return false;
    for(const auto& V:*Values){if(!V||V->Type!=EJson::Object||!Read(V->AsObject()))return false;}return true;
}
FObject Row(FName Id){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),Id.ToString());return O;}
}

namespace DWLanguage
{
bool StateToJson(const FCatalog& C,const FState& S,FString& OutJson,FString& Error)
{
    using namespace DWLanguageDetail;using namespace DWLanguageJson;if(!ValidateState(C,S,Error))return false;
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("version"),S.Version);
    // Exact decimal strings preserve int64 revisions/orders beyond JSON double precision.
    Root->SetStringField(TEXT("revision"),LexToString(S.Revision));Root->SetStringField(TEXT("next_discovery_order"),LexToString(S.NextDiscoveryOrder));
    Root->SetNumberField(TEXT("points"),S.AvailableTalentPoints);
    TArray<TSharedPtr<FJsonValue>> Entries,Relics,Doors,Submissions,Rewards,Learned;
    for(FName Id:SortedKeys(S.Entries))
    {
        const auto& E=S.Entries.FindChecked(Id);auto O=Row(Id);O->SetStringField(TEXT("order"),LexToString(E.FirstDiscoveryOrder));
        O->SetStringField(TEXT("guess"),E.Guess);O->SetArrayField(TEXT("clues"),Names(E.ClueIds));O->SetArrayField(TEXT("compounds"),Names(E.AppearedInCompounds));Entries.Add(MakeShared<FJsonValueObject>(O));
    }
    for(FName Id:SortedKeys(S.CompletedRelics))
    {const auto& R=S.CompletedRelics.FindChecked(Id);auto O=Row(Id);O->SetStringField(TEXT("source"),R.RewardSourceId.ToString());O->SetNumberField(TEXT("award"),R.AwardedPoints);Relics.Add(MakeShared<FJsonValueObject>(O));}
    for(FName Id:SortedKeys(S.DoorProgress))
    {
        const auto& D=S.DoorProgress.FindChecked(Id);auto O=Row(Id);O->SetNumberField(TEXT("accepted"),D.AcceptedCount);O->SetBoolField(TEXT("completed"),D.bCompleted);
        O->SetStringField(TEXT("source"),D.RewardSourceId.IsNone()?FString():D.RewardSourceId.ToString());O->SetNumberField(TEXT("award"),D.AwardedPoints);Doors.Add(MakeShared<FJsonValueObject>(O));
    }
    for(FName Id:SortedKeys(S.ProcessedDoorSubmissions))
    {
        const auto& D=S.ProcessedDoorSubmissions.FindChecked(Id);auto O=Row(Id);O->SetStringField(TEXT("door"),D.DoorId.ToString());O->SetStringField(TEXT("item"),D.ItemId.ToString());
        O->SetNumberField(TEXT("quantity"),D.Quantity);O->SetNumberField(TEXT("accepted_after"),D.AcceptedAfter);O->SetBoolField(TEXT("accepted"),D.bAccepted);O->SetBoolField(TEXT("completed_now"),D.bCompletedNow);Submissions.Add(MakeShared<FJsonValueObject>(O));
    }
    for(FName Id:SortedKeys(S.RewardSources)){auto O=Row(Id);O->SetNumberField(TEXT("amount"),S.RewardSources.FindChecked(Id));Rewards.Add(MakeShared<FJsonValueObject>(O));}
    for(FName Id:SortedKeys(S.LearnedTalentPaidCosts)){auto O=Row(Id);O->SetNumberField(TEXT("paid"),S.LearnedTalentPaidCosts.FindChecked(Id));Learned.Add(MakeShared<FJsonValueObject>(O));}
    Root->SetArrayField(TEXT("entries"),MoveTemp(Entries));Root->SetArrayField(TEXT("relics"),MoveTemp(Relics));Root->SetArrayField(TEXT("doors"),MoveTemp(Doors));
    Root->SetArrayField(TEXT("submissions"),MoveTemp(Submissions));Root->SetArrayField(TEXT("rewards"),MoveTemp(Rewards));Root->SetArrayField(TEXT("learned"),MoveTemp(Learned));
    FString Json;if(!FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json)))return Fail(Error,TEXT("Language state JSON serialization failed."));
    OutJson=MoveTemp(Json);Error.Reset();return true;
}
bool StateFromJson(const FCatalog& C,const FString& Json,FState& Out,FString& Error)
{
    using namespace DWLanguageDetail;using namespace DWLanguageJson;FObject Root;FState Candidate;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||
        !Fields(Root,{TEXT("version"),TEXT("revision"),TEXT("next_discovery_order"),TEXT("points"),TEXT("entries"),TEXT("relics"),TEXT("doors"),TEXT("submissions"),TEXT("rewards"),TEXT("learned")})||
        !Int(Root,TEXT("version"),Candidate.Version)||!Int64(Root,TEXT("revision"),Candidate.Revision)||
        !Int64(Root,TEXT("next_discovery_order"),Candidate.NextDiscoveryOrder)||!Int(Root,TEXT("points"),Candidate.AvailableTalentPoints))
        return Fail(Error,TEXT("Invalid language JSON root/version/counters."));
    bool Valid=Array(Root,TEXT("entries"),[&](const FObject& O)
    {
        FName Id;FEntryState E;
        if(!Fields(O,{TEXT("id"),TEXT("order"),TEXT("guess"),TEXT("clues"),TEXT("compounds")})||!Name(O,TEXT("id"),Id)||Candidate.Entries.Contains(Id)||
            !Int64(O,TEXT("order"),E.FirstDiscoveryOrder)||!O->TryGetStringField(TEXT("guess"),E.Guess)||!Names(O,TEXT("clues"),E.ClueIds)||!Names(O,TEXT("compounds"),E.AppearedInCompounds))return false;
        Candidate.Entries.Add(Id,MoveTemp(E));return true;
    });
    Valid=Valid&&Array(Root,TEXT("relics"),[&](const FObject& O)
    {
        FName Id;FRelicCompletion R;
        if(!Fields(O,{TEXT("id"),TEXT("source"),TEXT("award")})||!Name(O,TEXT("id"),Id)||Candidate.CompletedRelics.Contains(Id)||!Name(O,TEXT("source"),R.RewardSourceId)||!Int(O,TEXT("award"),R.AwardedPoints))return false;
        Candidate.CompletedRelics.Add(Id,R);return true;
    });
    Valid=Valid&&Array(Root,TEXT("doors"),[&](const FObject& O)
    {
        FName Id;FString Source;FDoorProgress D;
        if(!Fields(O,{TEXT("id"),TEXT("accepted"),TEXT("completed"),TEXT("source"),TEXT("award")})||!Name(O,TEXT("id"),Id)||Candidate.DoorProgress.Contains(Id)||
            !Int(O,TEXT("accepted"),D.AcceptedCount)||!O->TryGetBoolField(TEXT("completed"),D.bCompleted)||!O->TryGetStringField(TEXT("source"),Source)||!Int(O,TEXT("award"),D.AwardedPoints))return false;
        D.RewardSourceId=Source.IsEmpty()?NAME_None:FName(*Source);Candidate.DoorProgress.Add(Id,D);return true;
    });
    Valid=Valid&&Array(Root,TEXT("submissions"),[&](const FObject& O)
    {
        FName Id;FDoorSubmission D;
        if(!Fields(O,{TEXT("id"),TEXT("door"),TEXT("item"),TEXT("quantity"),TEXT("accepted_after"),TEXT("accepted"),TEXT("completed_now")})||
            !Name(O,TEXT("id"),Id)||Candidate.ProcessedDoorSubmissions.Contains(Id)||!Name(O,TEXT("door"),D.DoorId)||!Name(O,TEXT("item"),D.ItemId)||
            !Int(O,TEXT("quantity"),D.Quantity)||!Int(O,TEXT("accepted_after"),D.AcceptedAfter)||!O->TryGetBoolField(TEXT("accepted"),D.bAccepted)||!O->TryGetBoolField(TEXT("completed_now"),D.bCompletedNow))return false;
        Candidate.ProcessedDoorSubmissions.Add(Id,D);return true;
    });
    Valid=Valid&&Array(Root,TEXT("rewards"),[&](const FObject& O)
    {FName Id;int32 Amount=0;if(!Fields(O,{TEXT("id"),TEXT("amount")})||!Name(O,TEXT("id"),Id)||Candidate.RewardSources.Contains(Id)||!Int(O,TEXT("amount"),Amount))return false;Candidate.RewardSources.Add(Id,Amount);return true;});
    Valid=Valid&&Array(Root,TEXT("learned"),[&](const FObject& O)
    {FName Id;int32 Paid=0;if(!Fields(O,{TEXT("id"),TEXT("paid")})||!Name(O,TEXT("id"),Id)||Candidate.LearnedTalentPaidCosts.Contains(Id)||!Int(O,TEXT("paid"),Paid))return false;Candidate.LearnedTalentPaidCosts.Add(Id,Paid);return true;});
    if(!Valid)return Fail(Error,TEXT("Invalid language JSON row/type/duplicate stable ID."));
    if(!ValidateState(C,Candidate,Error))return false;Out=MoveTemp(Candidate);Error.Reset();return true;
}
}
