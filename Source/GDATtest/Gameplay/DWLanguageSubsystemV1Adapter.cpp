#include "DWLanguageSubsystemV1Adapter.h"
#include "DWLanguageSubsystem.h" // Supplied by the sole executor; v2.1 public contract.
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "Engine/Texture2D.h"

namespace
{
bool SameRequest(const FDWLanguageUIRequest& A,const FDWLanguageUIRequest& B)
{
    if(A.Kind!=B.Kind||A.SessionId!=B.SessionId||A.ExpectedRevision!=B.ExpectedRevision||A.TargetId!=B.TargetId
        ||A.Guess!=B.Guess||A.Answers.Num()!=B.Answers.Num()||A.Items.Num()!=B.Items.Num())return false;
    for(int32 I=0;I<A.Answers.Num();++I)
        if(A.Answers[I].SlotId!=B.Answers[I].SlotId||A.Answers[I].WordId!=B.Answers[I].WordId)return false;
    for(int32 I=0;I<A.Items.Num();++I)
        if(A.Items[I].StackId!=B.Items[I].StackId||A.Items[I].Quantity!=B.Items[I].Quantity)return false;
    return true;
}
}

FDWLanguageSubsystemV1Adapter::FDWLanguageSubsystemV1Adapter(UDWLanguageSubsystem* S,ADWPlayerCharacter* P,TFunction<bool()> InIsEnglish)
    :System(S),Player(P),IsEnglish(MoveTemp(InIsEnglish)),SessionId(FGuid::NewGuid().ToString())
{
    if(S){OwnerGeneration=S->GetSessionGeneration();ContextId=S->GetRequestedContextId();InitialPanel=static_cast<uint8>(S->GetRequestedPanel());}
}

FDWLanguageUISnapshot FDWLanguageSubsystemV1Adapter::ReadSnapshot() const
{
    FDWLanguageUISnapshot Out;Out.SessionId=SessionId;Out.bSessionValid=false;
    auto* S=System.Get();auto* P=Player.Get();
    if(!S||!P||P->IsDead()||!S->IsReady()||S->GetSessionGeneration()!=OwnerGeneration)return Out;
    Out.bSessionValid=S->GetRequestedPanel()!=EDWLanguagePanel::None
        &&S->GetRequestedContextId()==ContextId&&static_cast<uint8>(S->GetRequestedPanel())==InitialPanel;
    Out.bEnglish=IsEnglish?IsEnglish():false;
    auto* Inv=P->GetInventory();const int64 InvRevision=Inv?Inv->GetInventoryRevision():-1;
    if(LastInventoryRevision!=InvRevision||!Out.bSessionValid)
    {BackendSelectionId.Invalidate();SelectedStackIds.Empty();}
    if(LastSystemRevision!=S->GetRevision()||LastInventoryRevision!=InvRevision||bLastEnglish!=Out.bEnglish)
    {++PresentationRevision;LastSystemRevision=S->GetRevision();LastInventoryRevision=InvRevision;bLastEnglish=Out.bEnglish;}
    Out.Revision=PresentationRevision;Out.MaxGuessCharacters=S->GetGuessCharacterLimit();
    for(const auto& E:S->GetEntries(EDWLanguageFilter::All,EDWLanguageSort::OldestFirst))
    {
        FDWLanguageUIEntryView V;V.Id=E.EntryId;V.Kind=E.bCompound?EDWLanguageEntryKind::Compound:EDWLanguageEntryKind::Single;
        V.bDiscovered=E.Status!=EDWLanguageEntryStatus::Undiscovered;V.bCanEdit=E.bCanEdit;
        if(V.bDiscovered)
        {
            V.DiscoveryOrder=E.DiscoveryOrder;V.Glyph=S->GetGlyphTexture(E.EntryId);V.SavedGuess=E.Guess;
            V.PartIds=E.Parts;V.RelatedIds=E.RelatedEntries;
            for(const auto& C:E.Clues)
            {FDWLanguageUIClueView CV;CV.Id=C.ClueId;CV.Place=C.Location;CV.Carrier=C.Carrier;CV.Description=C.Description;CV.ObservedEntryId=C.DisplayedEntryId;CV.bFromCompound=C.bFromCompound;V.Clues.Add(CV);}
        }
        Out.Entries.Add(V);
    }
    FDWRelicPuzzleView R;
    if(InitialPanel==static_cast<uint8>(EDWLanguagePanel::Relic)&&S->GetRelic(ContextId,R))
    {
        Out.Puzzle.Id=R.RelicId;Out.Puzzle.Title=R.Title;Out.Puzzle.bAvailable=Out.bSessionValid;
        Out.Puzzle.bSolved=R.bSolved;Out.Puzzle.bCanSubmit=!R.bSolved;
        for(int32 I=0;I<R.GlyphEntries.Num();++I)
        {FDWLanguagePuzzleSlotView Slot;Slot.SlotId=FName(*FString::FromInt(I));Slot.EntryId=R.GlyphEntries[I];Slot.Glyph=S->GetGlyphTexture(Slot.EntryId);Out.Puzzle.Slots.Add(Slot);}
        for(const auto& C:R.Candidates){FDWLanguageWordView W;W.Id=C.MeaningId;W.Label=C.Label;Out.Puzzle.WordPool.Add(W);}
        if(R.bSolved)
        {
            Out.Puzzle.StoryChinese=R.StoryChinese;Out.Puzzle.StoryEnglish=R.StoryEnglish;
            Out.Puzzle.RewardDescription=FText::Format(FText::FromString(Out.bEnglish?TEXT("Configured reward: {0} talent points"):TEXT("配置奖励：{0} 天赋点")),FText::AsNumber(R.RewardPoints));
            // Public view lacks a separate reward receipt; bSolved alone is not a new grant.
        }
    }
    FDWLanguageMechanismView M;
    const bool bMechanism=InitialPanel==static_cast<uint8>(EDWLanguagePanel::Offering)||InitialPanel==static_cast<uint8>(EDWLanguagePanel::Trade);
    InventoryTokens.Empty();
    if(bMechanism&&S->GetMechanism(ContextId,M))
    {
        auto& E=Out.Exchange;E.TargetId=M.MechanismId;E.Title=M.Title;E.Mode=M.bTrade?EDWLanguageExchangeMode::NPCTrade:EDWLanguageExchangeMode::Offering;
        E.bAvailable=Out.bSessionValid;E.bCanSubmit=!M.bComplete&&M.Reason.IsEmpty();E.BlockedReason=M.Reason;E.InscriptionIds=M.GlyphEntries;
        E.RewardInscriptionIds=M.RewardGlyphEntries;E.UnitsPerSubmission=FMath::Max(1,M.UnitsPerSubmission);
        if(E.bAvailable)
        {
            for(FName Id:E.InscriptionIds)E.VisibleInscriptionGlyphs.Add(Id,S->GetGlyphTexture(Id));
            for(FName Id:E.RewardInscriptionIds)E.VisibleInscriptionGlyphs.Add(Id,S->GetGlyphTexture(Id));
        }
        // RequiredItemId/RewardItemId are never printed as translations of unread inscriptions.
        auto* GI=Cast<UDWGameInstance>(P->GetGameInstance());auto* Config=GI?GI->GetConfig():nullptr;
        if(Inv)for(int32 I=0;I<Inv->GetSlots().Num();++I)
        {
            FDWInventorySlotSnapshot Captured;if(!Inv->CaptureSlotSnapshot(I,Captured)||Captured.ItemId.IsNone()||Captured.Quantity<=0)continue;
            FDWLanguageInventoryView V;V.StackId=FName(*FString::Printf(TEXT("Slot_%d_Revision_%lld"),I,InvRevision));
            V.ItemId=Captured.ItemId;V.Quantity=Captured.Quantity;
            if(const auto* Item=Config?Config->GetItemDefinition(V.ItemId):nullptr)
            {V.Name=Out.bEnglish&&!Item->EnglishDisplayName.IsEmpty()?Item->EnglishDisplayName:Item->DisplayName;V.Icon=Item->Icon.Get();}
            else V.Name=FText::FromName(V.ItemId);
            InventoryTokens.Add(V.StackId,Captured);E.Inventory.Add(V);
        }
    }
    Out.TalentPoints=S->GetTalentPoints();
    for(const auto& T:S->GetTalents())
    {
        FDWLanguageTalentView V;V.Id=T.TalentId;V.Name=T.Title;V.Description=T.Description;V.Cost=T.Cost;
        V.bLearned=T.bLearned;V.bCanLearn=T.bConfigured&&T.bCanLearn;V.bCanForget=T.bConfigured&&T.bCanForget;
        V.LearnBlockedReason=T.Reason;V.ForgetBlockedReason=T.Reason;V.PrerequisiteIds=T.Prerequisites;Out.Talents.Add(V);
    }
    return Out;
}

FDWLanguageUIReply FDWLanguageSubsystemV1Adapter::Request(const FDWLanguageUIRequest& R)
{
    FDWLanguageUIReply Out;const FDWLanguageUISnapshot Current=ReadSnapshot();
    auto Reject=[&](const TCHAR* Code,const TCHAR* CN,const TCHAR* EN)
    {Out.ResultCode=Code;Out.Message=FText::FromString(Current.bEnglish?EN:CN);return Out;};
    if(!R.RequestId.IsValid()||R.SessionId!=SessionId||!Current.bSessionValid)
        return Reject(TEXT("InvalidSession"),TEXT("当前交互已结束"),TEXT("This interaction has ended"));
    if(const auto* Prior=RepliedRequests.Find(R.RequestId))
    {
        if(SameRequest(Prior->Request,R))return Prior->Reply;
        return Reject(TEXT("NonceMismatch"),TEXT("请求标识已用于其他内容"),TEXT("Request ID belongs to different content"));
    }
    if(R.ExpectedRevision!=Current.Revision)
        return Reject(TEXT("StaleSnapshot"),TEXT("内容已更新，请重新确认"),TEXT("Content changed. Please confirm again"));
    if(RepliedRequests.Num()>=512)
        return Reject(TEXT("ReopenRequired"),TEXT("请关闭并重新打开当前界面后继续"),TEXT("Close and reopen this panel to continue"));
    auto* S=System.Get();auto* P=Player.Get();if(!S||!P)return Reject(TEXT("Unavailable"),TEXT("玩法连接不可用"),TEXT("Game connection unavailable"));
    switch(R.Kind)
    {
    case EDWLanguageUIRequestKind::SaveGuess:Out.bAccepted=S->SaveGuess(R.TargetId,R.Guess,Out.Message);break;
    case EDWLanguageUIRequestKind::LearnTalent:Out.bAccepted=S->LearnTalent(R.TargetId,Out.Message);break;
    case EDWLanguageUIRequestKind::ForgetTalent:Out.bAccepted=S->ForgetTalent(R.TargetId,Out.Message);break;
    case EDWLanguageUIRequestKind::SubmitPuzzle:
    {
        if(R.TargetId!=Current.Puzzle.Id||R.Answers.Num()!=Current.Puzzle.Slots.Num())return Reject(TEXT("Incomplete"),TEXT("请完成每一项"),TEXT("Complete every slot"));
        TArray<FName> Meanings;
        for(int32 I=0;I<R.Answers.Num();++I)
        {
            if(R.Answers[I].SlotId!=Current.Puzzle.Slots[I].SlotId||R.Answers[I].WordId.IsNone())return Reject(TEXT("Incomplete"),TEXT("请完成每一项"),TEXT("Complete every slot"));
            Meanings.Add(R.Answers[I].WordId);
        }
        Out.bAccepted=S->SubmitRelic(R.TargetId,Meanings,Out.Message);break;
    }
    case EDWLanguageUIRequestKind::StageItem:
    case EDWLanguageUIRequestKind::UnstageItem:
    {
        if(R.TargetId!=Current.Exchange.TargetId||!Current.Exchange.bAvailable||!Current.Exchange.bCanSubmit
            ||R.Items.Num()!=1||R.Items[0].Quantity!=1)
            return Reject(TEXT("InvalidSelection"),TEXT("当前选择不可用"),TEXT("Selection unavailable"));
        const FName Stack=R.Items[0].StackId;
        if(R.Kind==EDWLanguageUIRequestKind::StageItem)
        {
            const auto* Captured=InventoryTokens.Find(Stack);auto* Inv=P->GetInventory();
            if(!Captured||!Inv||!Inv->IsSlotSnapshotCurrent(*Captured))
                return Reject(TEXT("InventoryChanged"),TEXT("背包已变化，请重新选择"),TEXT("Inventory changed. Select again"));
            if(Current.Exchange.Mode==EDWLanguageExchangeMode::Offering&&SelectedStackIds.Num()>=Current.Exchange.UnitsPerSubmission)
                return Reject(TEXT("SelectionFull"),TEXT("本次投物选择已满"),TEXT("This offering selection is full"));
            if(!BackendSelectionId.IsValid())BackendSelectionId=S->BeginItemSelection(ContextId,Out.Message);
            if(BackendSelectionId.IsValid())Out.bAccepted=S->SelectItem(BackendSelectionId,Captured->SlotIndex,Out.Message);
            if(Out.bAccepted)SelectedStackIds.Add(Stack);
        }
        else
        {
            const int32 Index=SelectedStackIds.FindLast(Stack);
            if(!BackendSelectionId.IsValid()||Index==INDEX_NONE)
                return Reject(TEXT("NothingSelected"),TEXT("本次没有选择该物品"),TEXT("This item is not selected"));
            Out.bAccepted=S->UnselectItem(BackendSelectionId,Index,Out.Message);
            if(Out.bAccepted)SelectedStackIds.RemoveAt(Index);
        }
        break;
    }
    case EDWLanguageUIRequestKind::SubmitExchange:
    {
        if(R.TargetId!=Current.Exchange.TargetId||R.Items.IsEmpty()||!BackendSelectionId.IsValid())
            return Reject(TEXT("EmptySelection"),TEXT("请先选择物品"),TEXT("Choose an item first"));
        TMap<FName,int32> Expected,Proposed;
        for(FName Id:SelectedStackIds)++Expected.FindOrAdd(Id);
        for(const auto& Item:R.Items)
        {
            if(Item.Quantity<=0||Proposed.Contains(Item.StackId))return Reject(TEXT("InvalidSelection"),TEXT("选择已变化，请重新选择"),TEXT("Selection changed. Select again"));
            Proposed.Add(Item.StackId,Item.Quantity);
        }
        if(Expected.Num()!=Proposed.Num())return Reject(TEXT("SelectionMismatch"),TEXT("选择已变化，请重新选择"),TEXT("Selection changed. Select again"));
        for(const auto& Pair:Expected)if(Proposed.FindRef(Pair.Key)!=Pair.Value)
            return Reject(TEXT("SelectionMismatch"),TEXT("选择已变化，请重新选择"),TEXT("Selection changed. Select again"));
        // The ONLY submission is the token captured at the user's actual selection clicks.
        if(Current.Exchange.Mode==EDWLanguageExchangeMode::Offering)
            Out.bAccepted=S->SubmitOffering(BackendSelectionId,Out.Message);
        else
        {Out.bAccepted=S->SubmitTrade(BackendSelectionId,Out.Message);Out.bCloseAfterSuccess=Out.bAccepted;Out.CloseDelaySeconds=1.f;}
        BackendSelectionId.Invalidate();SelectedStackIds.Empty();Out.bResetSelection=true;
        break;
    }
    }
    Out.ResultCode=Out.bAccepted?TEXT("Committed"):TEXT("Rejected");
    // Bounded, payload-bound duplicate replies. Never commit after the cache becomes full.
    FCachedReply Cached;Cached.Request=R;Cached.Reply=Out;RepliedRequests.Add(R.RequestId,MoveTemp(Cached));
    return Out;
}
