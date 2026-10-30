#include "DWLanguageSubsystem.h"
#include "DWRetiredItemPolicy.h"
#include "DWLanguageModel.h"
#include "DWLanguageCarrier.h"
#include "DWLanguageCombatArena.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCharacterA01Component.h"
#include "DWLocalizationLibrary.h"
#include "DWGatheringComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace DWLanguageWorld
{
struct FIdentity
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    TWeakObjectPtr<ADWLanguageCarrier> Carrier;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority;
    FName StableId,ContextId;
    EDWLanguageCarrierKind Kind=EDWLanguageCarrierKind::Inscription;
    DWCombatCommit::FEntityLife Life;
    uint64 Epoch=0;
};
bool Capture(ADWPlayerCharacter* P,ADWLanguageCarrier* C,FIdentity& Out)
{
    auto* W=IsValid(P)?P->GetWorld():nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(P);
    if(!W||W->bIsTearingDown||!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||P->IsCombatRestoreInProgress()||
        !IsValid(C)||C->IsActorBeingDestroyed()||C->GetWorld()!=W||!C->IsInReach(P)||!P->GetInventory()||!A)return false;
    const auto Life=A->FindRegisteredActorLife(P);if(!Life.Entity||!Life.Life)return false;
    Out.Player=P;Out.Inventory=P->GetInventory();Out.Carrier=C;Out.World=W;Out.Authority=A;
    Out.StableId=C->StableId;Out.ContextId=C->GetContextId();Out.Kind=C->Kind;Out.Life=Life;Out.Epoch=A->GetWorldEpoch();return true;
}
bool Current(const FIdentity& I)
{
    auto* P=I.Player.Get();auto* W=I.World.Get();auto* A=I.Authority.Get();auto* C=I.Carrier.Get();
    if(!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||P->IsCombatRestoreInProgress()||!W||W->bIsTearingDown||
        P->GetWorld()!=W||P->GetInventory()!=I.Inventory.Get()||!I.Inventory.IsValid()||!IsValid(C)||C->IsActorBeingDestroyed()||
        C->GetWorld()!=W||C->StableId!=I.StableId||C->GetContextId()!=I.ContextId||C->Kind!=I.Kind||
        !A||UDWCombatAuthoritySubsystem::Get(P)!=A||A->GetWorldEpoch()!=I.Epoch)return false;
    const auto Life=A->FindRegisteredActorLife(P);return Life.Entity==I.Life.Entity&&Life.Life==I.Life.Life;
}
FText Stale(const UObject* O)
{return DWText(O,TEXT("选择或交互状态已变化，请重新选择；未扣物品。"),TEXT("The selection or interaction changed. Select again; no items were spent."));}
FText Unavailable(const UObject* O)
{return DWText(O,TEXT("当前无法交互；未提交更改。"),TEXT("Interaction is unavailable; no changes were committed."));}
bool SameMechanism(const FDWLanguageMechanismView& A,const FDWLanguageMechanismView& B)
{return A.MechanismId==B.MechanismId&&A.RequiredItemId==B.RequiredItemId&&A.RequiredCount==B.RequiredCount&&
    A.UnitsPerSubmission==B.UnitsPerSubmission&&A.RewardItemId==B.RewardItemId&&A.RewardCount==B.RewardCount&&A.bTrade==B.bTrade;}
}

struct FDWLanguageSelectionRequest
{
    DWLanguageWorld::FIdentity Identity;
    FDWLanguageMechanismView Mechanism;
    int64 InventoryRevision=INDEX_NONE,ModelRevision=INDEX_NONE;
    TArray<FDWInventorySlotSnapshot> Units; // Each immutable snapshot selects ONE unit, not its entire stack.
    bool bSubmitted=false,bResult=false,bClosed=false;
    FText Message;
};
/** Opaque epoch-owned UI requests. Restore clears this entire store; old IDs are never reconstructed. */
class FDWLanguageSelectionStore
{
public:
    TMap<FGuid,TSharedPtr<FDWLanguageSelectionRequest>> Requests;
    TMap<FName,double> WaterSourceNextUse; // Game-time cooldown, allocated before the joint fact boundary.
    DWLanguageWorld::FIdentity ActiveContext;
    bool bHasContext=false;
};

void UDWLanguageSubsystem::InvalidateLifeContextUnchecked()
{
 if(Selections){Selections->Requests.Reset();Selections->bHasContext=false;Selections->ActiveContext={};}
 ActiveCarrier.Reset();RequestedPanel=EDWLanguagePanel::None;RequestedEntry=RequestedContext=NAME_None;
 // Preserve per-source game-time cooldowns. Only old-life requests/context expire.
}

/** The only extra inventory friendship. No public arbitrary commit or suppression window exists. */
class FDWLanguageInventoryTransaction
{
public:
    static bool PrepareConsume(UDWInventoryComponent& I,const FDWInventorySlotSnapshot& Slot,int32 Count,FName Returned,int32 ReturnCount,FDWPreparedInventoryChange& Out)
    {
        if(Count<1||Count>Slot.Quantity||ReturnCount<0||(ReturnCount>0&&Returned.IsNone()))return false;
        TArray<FDWInventorySlotSnapshot> Units;for(int32 N=0;N<Count;++N)Units.Add(Slot);TArray<FDWItemStack> Candidate;
        return DebitedSlots(I,Units,Candidate)&&(ReturnCount==0||I.AddToSlots(Candidate,Returned,ReturnCount))&&I.MakePrepared(MoveTemp(Candidate),Out);
    }
    static bool PrepareDebit(UDWInventoryComponent& I,const TArray<FDWInventorySlotSnapshot>& Units,
        FDWPreparedInventoryChange& Out)
    {
        TArray<FDWItemStack> Candidate;
        return DebitedSlots(I,Units,Candidate)&&I.MakePrepared(MoveTemp(Candidate),Out);
    }
    static bool PrepareTrade(UDWInventoryComponent& I,const TArray<FDWInventorySlotSnapshot>& Units,
        const FDWLanguageMechanismView& Trade,FDWPreparedInventoryChange& Out,bool& OutBagFull)
    {
        OutBagFull=false;
        const auto* Config=I.GetConfig();FDWPreparedInventoryChange CapacityOnly;
        // PDF policy: a full bag rejects before payment. Never use consumed inputs to create output capacity.
        if(!Config||I.GetSlots().Num()>=Config->MaxInventorySlots||!I.PrepareAddItem(Trade.RewardItemId,Trade.RewardCount,CapacityOnly))
        {OutBagFull=true;return false;}
        if(Units.Num()!=Trade.RequiredCount)return false;
        for(const auto& Unit:Units)if(Unit.ItemId!=Trade.RequiredItemId)return false;
        TArray<FDWItemStack> Candidate;
        return DebitedSlots(I,Units,Candidate)&&I.AddToSlots(Candidate,Trade.RewardItemId,Trade.RewardCount)&&I.MakePrepared(MoveTemp(Candidate),Out);
    }
    static bool CommitOffering(UDWInventoryComponent& I,FDWPreparedInventoryChange&& Inventory,
        DWLanguage::FModel& Model,DWLanguage::FPreparedChange&& Language,DWLanguage::FOperationResult& Result)
    {
        if(!I.ValidatePreparedForCommit(Inventory)||!Model.CanCommit(Language))return false;
        // No callbacks, allocation, scene operations or second inventory owner between these two adopts.
        I.AdoptPreparedUnchecked(MoveTemp(Inventory));Model.AdoptPreparedUnchecked(MoveTemp(Language),Result);return true;
    }
    static bool CommitInventory(UDWInventoryComponent& I,FDWPreparedInventoryChange&& Plan)
    {if(!I.ValidatePreparedForCommit(Plan))return false;I.AdoptPreparedUnchecked(MoveTemp(Plan));return true;}
    static bool CommitWater(UDWInventoryComponent& I,FDWPreparedInventoryChange&& Inventory,
        UDWCharacterA01Component& A01,FDWCharacterA01Runtime&& Refill,uint64 Revision,double Health)
    {
        if(!I.ValidatePreparedForCommit(Inventory)||!A01.CanAdoptRuntime(Revision,Health))return false;
        I.AdoptPreparedUnchecked(MoveTemp(Inventory));A01.AdoptRuntimeUnchecked(MoveTemp(Refill));return true;
    }
private:
    static bool DebitedSlots(UDWInventoryComponent& I,const TArray<FDWInventorySlotSnapshot>& Units,TArray<FDWItemStack>& Out)
    {
        if(!I.CanWriteInventory()||Units.IsEmpty())return false;
        TMap<int32,int32> Counts;
        for(const auto& S:Units)
        {
            if(!I.IsSlotSnapshotCurrent(S))return false;
            int32& Count=Counts.FindOrAdd(S.SlotIndex);if(Count>=S.Quantity)return false;++Count;
        }
        TArray<FDWItemStack> Candidate=I.GetSlots();TArray<int32> Indices;Counts.GetKeys(Indices);Indices.Sort(TGreater<int32>());
        for(int32 Index:Indices)
        {
            if(!Candidate.IsValidIndex(Index)||Candidate[Index].Quantity<Counts.FindChecked(Index))return false;
            Candidate[Index].Quantity-=Counts.FindChecked(Index);if(Candidate[Index].Quantity==0)Candidate.RemoveAt(Index);
        }
        Out=MoveTemp(Candidate);return true;
    }
};

bool UDWLanguageSubsystem::CheckContext(FName Id,EDWLanguageCarrierKind Kind,FText& Message)const
{
    auto* P=ActivePlayer.Get();auto* C=ActiveCarrier.Get();
    const auto* A01=IsValid(P)?P->FindComponentByClass<UDWCharacterA01Component>():nullptr;
    if(!IsReady()||!IsValid(P)||P->IsDead()||P->IsCombatRestoreInProgress()||!Selections||!Selections->bHasContext||
        !DWLanguageWorld::Current(Selections->ActiveContext)||Selections->ActiveContext.Carrier.Get()!=C||
        (A01&&A01->IsEnabled()&&!A01->CanOpenMenus())||!IsValid(C)||C->IsActorBeingDestroyed()||
        C->GetWorld()!=GetWorld()||C->Kind!=Kind||C->GetContextId()!=Id||RequestedContext!=Id||!C->IsInReach(P))
    {Message=DWText(this,TEXT("当前交互对象已失效或超出范围；未提交更改。"),TEXT("The interaction is no longer available or is out of reach; nothing was committed."));return false;}
    return true;
}
bool UDWLanguageSubsystem::TryInteractNearest(ADWPlayerCharacter* Player,bool bLanguageOnly,FText& OutMessage)
{
    if(!IsReady()||Player!=ActivePlayer.Get()||bChanging||!GetWorld()||GetWorld()->IsPaused())return false;
    ADWLanguageCarrier* Best=nullptr;double Distance=TNumericLimits<double>::Max();
    for(TActorIterator<ADWLanguageCarrier> It(GetWorld());It;++It)
    {
        if(It->IsActorBeingDestroyed()||!It->IsInReach(Player))continue;
        const double D=FVector::DistSquared2D(It->GetActorLocation(),Player->GetActorLocation());
        if(D<Distance||(D==Distance&&Best&&It->StableId.LexicalLess(Best->StableId))){Best=*It;Distance=D;}
    }
    return Best&&Interact(Best,bLanguageOnly,OutMessage);
}
bool UDWLanguageSubsystem::Interact(ADWLanguageCarrier* Carrier,bool bLanguageOnly,FText& OutMessage)
{
    using namespace DWLanguageWorld;auto* P=ActivePlayer.Get();FIdentity Identity;
    if(!IsReady()||bChanging||!GetWorld()||GetWorld()->IsPaused()||!Capture(P,Carrier,Identity))
    {OutMessage=Unavailable(this);return false;}
    const auto* Ability=P->FindComponentByClass<UDWCharacterA01Component>();
    if(Ability&&Ability->IsEnabled()&&!Ability->CanOpenMenus())
    {OutMessage=DWText(this,TEXT("请先结束当前喷气动作再交互。"),TEXT("Finish the current jet ability before interacting."));return false;}
    if(auto* Arena=Cast<ADWLanguageCombatArena>(Carrier))
    {if(bLanguageOnly)return false;return Arena->InteractTraining(P,OutMessage);}
    if(Carrier->Kind==EDWLanguageCarrierKind::Talents)
    {if(bLanguageOnly)return false;RequestTalents();OutMessage=DWText(this,TEXT("已打开天赋。"),TEXT("Talents opened."));return true;}
    TStrongObjectPtr<UDWLanguageSubsystem> KeepAlive(this);TGuardValue<bool> Guard(bChanging,true);
    if(!Selections)Selections=MakeShared<FDWLanguageSelectionStore>();
    if(Carrier->Kind==EDWLanguageCarrierKind::WaterSource&&!bLanguageOnly)
    {
        auto* A01=P->FindComponentByClass<UDWCharacterA01Component>();
        const double Hydration=Parameter(TEXT("language.water_source.hydration_per_use"),25.);
        const double Cooldown=Parameter(TEXT("language.water_source.cooldown_s"),1.);
        const double Quantity=Parameter(TEXT("language.water_source.water_per_use"),1.);
        if(!A01||!A01->IsEnabled()||!FMath::IsFinite(Hydration)||Hydration<0.||!FMath::IsFinite(Cooldown)||Cooldown<0.||!FMath::IsFinite(Quantity)||Quantity<1||Quantity>MAX_int32||FMath::FloorToDouble(Quantity)!=Quantity)
        {OutMessage=DWText(this,TEXT("供水的角色资源或试玩参数尚未就绪，未取水。"),TEXT("Water-source character resources or trial parameters are not ready; nothing was taken."));return false;}
        if(!Selections)Selections=MakeShared<FDWLanguageSelectionStore>();const TSharedPtr<FDWLanguageSelectionStore> Store=Selections;
        double& NextUse=Store->WaterSourceNextUse.FindOrAdd(Carrier->StableId);
        if(GetWorld()->GetTimeSeconds()<NextUse)
        {OutMessage=DWText(this,TEXT("供水正在冷却，请稍后再取。"),TEXT("The supply is cooling down. Try again shortly."));return false;}
        FDWPreparedInventoryChange Inventory;FDWCharacterA01Runtime Refill;
        const uint64 Revision=A01->GetResourceRevision();const double Health=P->GetHealth();
        if(!P->GetInventory()->PrepareAddItem(TEXT("Water"),static_cast<int32>(Quantity),Inventory)||!A01->PrepareSurvivalRefill(Hydration,0.,Refill))
        {OutMessage=DWText(this,TEXT("背包空间或补水条件不足，未取水且未改变水分。"),TEXT("Inventory space or refill conditions were unavailable; no item or hydration change was made."));return false;}
        if(!Current(Identity)||!Carrier->IsInReach(P)||Selections!=Store||
            !FDWLanguageInventoryTransaction::CommitWater(*P->GetInventory(),MoveTemp(Inventory),*A01,MoveTemp(Refill),Revision,Health))
        {OutMessage=Stale(this);return false;}
        NextUse=GetWorld()->GetTimeSeconds()+Cooldown; // All three facts exist before the first observer.
        OutMessage=FText::Format(DWText(this,TEXT("已取得{0}份水并补充水分。"),TEXT("Received {0} Water and restored hydration.")),FText::AsNumber(static_cast<int32>(Quantity)));
        const int64 Expected=GetRevision();const TWeakObjectPtr<UDWLanguageSubsystem> WeakThis(this);
        Identity.Inventory->OnChanged.Broadcast();
        if(WeakThis.IsValid()&&Selections==Store&&Current(Identity)&&GetRevision()==Expected)PublishProgress();
        return true; // No discovery, guess, finite stock debit or post-callback fact mutation.
    }
    const FName Context=Carrier->GetContextId();FDWLanguageMechanismView Mechanism;
    EDWLanguagePanel Panel=EDWLanguagePanel::Journal;
    if(!bLanguageOnly)
    {
        if(Carrier->Kind==EDWLanguageCarrierKind::Relic)
        {if(!Model->GetCatalog().Relics.Contains(Context)){OutMessage=Unavailable(this);return false;}Panel=EDWLanguagePanel::Relic;}
        else if(Carrier->Kind==EDWLanguageCarrierKind::Offering||Carrier->Kind==EDWLanguageCarrierKind::Trade)
        {
            if(!GetMechanism(Context,Mechanism)||Mechanism.bTrade!=(Carrier->Kind==EDWLanguageCarrierKind::Trade))
            {OutMessage=Unavailable(this);return false;}
            Panel=Mechanism.bTrade?EDWLanguagePanel::Trade:EDWLanguagePanel::Offering;
        }
    }
    DWLanguage::FPreparedChange Discovery;DWLanguage::FOperationResult Result;bool Changed=false;
    TArray<TPair<FName,FName>> EntryClues;
    if(Carrier->Kind==EDWLanguageCarrierKind::Relic)
    {
        FDWRelicPuzzleView R;if(!GetRelic(Context,R))return false;
        for(FName Id:R.GlyphEntries)EntryClues.Emplace(Id,FName(*(Context.ToString()+TEXT("_")+Id.ToString())));
    }
    else if(Carrier->Kind==EDWLanguageCarrierKind::Trade)
    {
        if(!GetMechanism(Context,Mechanism))return false;TArray<FName> Seen=Mechanism.GlyphEntries;
        for(FName Id:Mechanism.RewardGlyphEntries)Seen.AddUnique(Id);
        for(FName Id:Seen)EntryClues.Emplace(Id,FName(*(Context.ToString()+TEXT("_")+Id.ToString())));
    }
    else if(Carrier->Kind==EDWLanguageCarrierKind::Offering)
    {
        const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
        if(Definitions&&Definitions->TryGetArrayField(TEXT("mechanism_examples"),Rows))for(const auto& Row:*Rows)
        {auto O=Row->AsObject();FString Id,Entry;if(O&&O->TryGetStringField(TEXT("id"),Id)&&FName(*Id)==Context&&O->TryGetStringField(TEXT("entry_id"),Entry))
        {EntryClues.Emplace(FName(*Entry),FName(*(Context.ToString()+TEXT("_")+Entry)));break;}}
    }
    else if(!Carrier->EntryId.IsNone()&&!Carrier->ClueId.IsNone())EntryClues.Emplace(Carrier->EntryId,Carrier->ClueId);
    if(EntryClues.IsEmpty())
    {OutMessage=DWText(this,TEXT("该载体缺少真实条目或线索配置，未发现文字。"),TEXT("This carrier has no configured entry/clue; no inscription was discovered."));return false;}
    Changed=Model->PrepareDiscoverBatch(EntryClues,true,Discovery,Result);
    if(!Changed&&Result.Code!=DWLanguage::ECode::NoChange&&Result.Code!=DWLanguage::ECode::AlreadyApplied)
    {OutMessage=Unavailable(this);return false;}
    if(!Current(Identity)||!Carrier->IsInReach(P)||(Changed&&!Model->CanCommit(Discovery)))
    {OutMessage=Stale(this);return false;}
    if(Changed)Model->AdoptPreparedUnchecked(MoveTemp(Discovery),Result);
    // Invalidate only unfinished old-context selections. Completed request receipts remain deduplicated in this epoch.
    if(Selections)for(auto& Pair:Selections->Requests)if(!Pair.Value->bSubmitted)Pair.Value->bClosed=true;
    Selections->ActiveContext=Identity;Selections->bHasContext=true;
    ActiveCarrier=Carrier;RequestedContext=Context;RequestedEntry=Panel==EDWLanguagePanel::Journal?EntryClues[0].Key:NAME_None;RequestedPanel=Panel;
    OutMessage=Changed?DWText(this,TEXT("已发现文字，含义由你记录。"),TEXT("Inscription discovered. Record your own guess.")):
        DWText(this,TEXT("已打开文字交互。"),TEXT("Inscription interaction opened."));
    const int64 Expected=GetRevision();const TWeakObjectPtr<UDWLanguageSubsystem> WeakThis(this);
    if(Changed)PublishProgress();
    if(WeakThis.IsValid()&&Current(Identity)&&GetRevision()==Expected&&ActiveCarrier.Get()==Carrier&&RequestedContext==Context&&RequestedPanel==Panel)
        OnPanelRequested.Broadcast();
    return true;
}

FGuid UDWLanguageSubsystem::BeginItemSelection(FName Id,FText& OutMessage)
{
    using namespace DWLanguageWorld;FDWLanguageMechanismView View;FIdentity Identity;
    if(!IsReady()||!GetMechanism(Id,View)||View.bComplete||
        !CheckContext(Id,View.bTrade?EDWLanguageCarrierKind::Trade:EDWLanguageCarrierKind::Offering,OutMessage)||
        !Capture(ActivePlayer.Get(),ActiveCarrier.Get(),Identity))
    {if(OutMessage.IsEmpty())OutMessage=Unavailable(this);return {};}
    if(RequestedPanel!=(View.bTrade?EDWLanguagePanel::Trade:EDWLanguagePanel::Offering))
    {OutMessage=DWText(this,TEXT("请先返回当前机关或交易面板。"),TEXT("Return to the current mechanism or trade first."));return {};}
    if(!Selections)Selections=MakeShared<FDWLanguageSelectionStore>();
    auto Request=MakeShared<FDWLanguageSelectionRequest>();Request->Identity=Identity;Request->Mechanism=View;
    Request->InventoryRevision=Identity.Inventory->GetInventoryRevision();Request->ModelRevision=GetRevision();
    FGuid IdToken;do{IdToken=FGuid::NewGuid();}while(Selections->Requests.Contains(IdToken));
    Selections->Requests.Add(IdToken,Request);OutMessage=DWText(this,TEXT("请选择实际背包中的物品，每次选择1件。"),TEXT("Select actual inventory slots, one unit per click."));return IdToken;
}
bool UDWLanguageSubsystem::SelectItem(FGuid RequestId,int32 Slot,FText& OutMessage)
{
    using namespace DWLanguageWorld;const auto R=Selections?Selections->Requests.FindRef(RequestId):nullptr;
    if(!R||R->bClosed||R->bSubmitted||bChanging||!IsReady()||!Current(R->Identity)||R->InventoryRevision!=R->Identity.Inventory->GetInventoryRevision()||R->ModelRevision!=GetRevision()||
        !CheckContext(R->Identity.ContextId,R->Identity.Kind,OutMessage))
    {OutMessage=Stale(this);return false;}
    const int32 Max=R->Mechanism.bTrade?R->Mechanism.RequiredCount:R->Mechanism.UnitsPerSubmission;
    if(Max<1||R->Units.Num()>=Max){OutMessage=DWText(this,TEXT("本次选择数量已达到要求。"),TEXT("The selection already contains the required number of units."));return false;}
    FDWInventorySlotSnapshot Snapshot;
    if(!R->Identity.Inventory->CaptureSlotSnapshot(Slot,Snapshot))
    {OutMessage=DWText(this,TEXT("该格没有可选择的物品。"),TEXT("There is no selectable item in that slot."));return false;}
    int32 Count=0;for(const auto& Unit:R->Units)if(Unit.SlotIndex==Slot)++Count;
    if(Count>=Snapshot.Quantity){OutMessage=DWText(this,TEXT("该格数量不足。"),TEXT("There are not enough units in that slot."));return false;}
    R->Units.Add(Snapshot);OutMessage=DWText(this,TEXT("已选择1件，尚未扣除。"),TEXT("Selected one unit; nothing has been spent."));return true;
}
bool UDWLanguageSubsystem::UnselectItem(FGuid RequestId,int32 Index,FText& OutMessage)
{
    using namespace DWLanguageWorld;const auto R=Selections?Selections->Requests.FindRef(RequestId):nullptr;
    if(!R||R->bClosed||R->bSubmitted||bChanging||!IsReady()||!Current(R->Identity)||R->InventoryRevision!=R->Identity.Inventory->GetInventoryRevision()||
        R->ModelRevision!=GetRevision()||!CheckContext(R->Identity.ContextId,R->Identity.Kind,OutMessage)||!R->Units.IsValidIndex(Index))
    {OutMessage=Stale(this);return false;}
    R->Units.RemoveAt(Index);OutMessage=DWText(this,TEXT("已取消选择；背包没有变化。"),TEXT("Selection removed; the inventory was unchanged."));return true;
}

bool UDWLanguageSubsystem::SubmitOffering(FGuid RequestId,FText& OutMessage)
{
    using namespace DWLanguageWorld;const TSharedPtr<FDWLanguageSelectionStore> Store=Selections;const auto R=Store?Store->Requests.FindRef(RequestId):nullptr;
    if(!RequestId.IsValid()||!R||!Current(R->Identity)){OutMessage=Stale(this);return false;}
    if(R->bSubmitted){OutMessage=R->Message;return R->bResult;}
    if(bChanging){OutMessage=Unavailable(this);return false;}
    const auto Finish=[&](bool Ok,const FText& Message){R->bSubmitted=true;R->bResult=Ok;R->Message=Message;OutMessage=Message;return Ok;};
    if(R->bClosed||!IsReady()||R->Mechanism.bTrade||RequestedPanel!=EDWLanguagePanel::Offering||
        R->ModelRevision!=GetRevision()||R->InventoryRevision!=R->Identity.Inventory->GetInventoryRevision()||
        !CheckContext(R->Identity.ContextId,EDWLanguageCarrierKind::Offering,OutMessage))return Finish(false,Stale(this));
    TStrongObjectPtr<UDWLanguageSubsystem> KeepAlive(this);TGuardValue<bool> Guard(bChanging,true);
    FDWLanguageMechanismView Now;
    if(!GetMechanism(R->Identity.ContextId,Now)||!SameMechanism(Now,R->Mechanism))return Finish(false,Stale(this));
    const auto* Door=Model->GetCatalog().Doors.Find(R->Identity.ContextId);
    if(!Door||Door->RewardPoints!=0)
        return Finish(false,DWText(this,TEXT("该洞口奖励配置无效；洞口仅开门，不发额外点数或物品。"),TEXT("Invalid offering reward configuration: this opening only unlocks the gate; it awards no points or items.")));
    if(R->Units.Num()>Door->MaxUnitsPerSubmission||R->Units.IsEmpty())
        return Finish(false,DWText(this,TEXT("请按本次要求重新选择投入数量；未扣物品。"),TEXT("Select the required submission quantity again; no items were spent.")));
    const FName Item=R->Units[0].ItemId;for(const auto& Unit:R->Units)if(Unit.ItemId!=Item)return Finish(false,Stale(this));
    DWLanguage::FPreparedChange Language;DWLanguage::FOperationResult Result;
    if(!Model->PrepareSubmitDoor(R->Identity.ContextId,FName(*RequestId.ToString(EGuidFormats::Digits)),Item,R->Units.Num(),Language,Result))
    {
        if(Result.Code==DWLanguage::ECode::AlreadyCompleted)return Finish(false,DWText(this,TEXT("机关已完成，不再接收物品。"),TEXT("Completed; no further items are accepted.")));
        return Finish(false,DWText(this,TEXT("本次投入无法提交，未扣物品。"),TEXT("This offering could not be submitted; no items were spent.")));
    }
    FDWPreparedInventoryChange Inventory;auto* Bag=R->Identity.Inventory.Get();
    if(Result.PointDelta!=0||!Result.bConsumeInventory||Result.InventoryQuantity!=R->Units.Num()||
        !FDWLanguageInventoryTransaction::PrepareDebit(*Bag,R->Units,Inventory)||!Current(R->Identity)||
        !CheckContext(R->Identity.ContextId,EDWLanguageCarrierKind::Offering,OutMessage)||
        !FDWLanguageInventoryTransaction::CommitOffering(*Bag,MoveTemp(Inventory),*Model,MoveTemp(Language),Result))return Finish(false,Stale(this));
    const FText Message=Result.Code==DWLanguage::ECode::WrongDoorItem?
        DWText(this,TEXT("投入的物品不匹配，已消耗且不退还；机关进度未增加。"),TEXT("The item did not match. It was consumed and will not be refunded; the mechanism did not progress.")):
        (Result.bCompletedNow?DWText(this,TEXT("机关已完成，通路已打开。"),TEXT("The mechanism is complete and the passage is open.")):
        DWText(this,TEXT("已投入，正确物品的累计进度已更新。"),TEXT("Offering accepted; the accumulated progress was updated.")));
    Finish(true,Message);const int64 Expected=GetRevision();const TWeakObjectPtr<UDWLanguageSubsystem> WeakThis(this);
    Bag->OnChanged.Broadcast();
    if(WeakThis.IsValid()&&Selections==Store&&Current(R->Identity)&&GetRevision()==Expected)PublishProgress();
    return true;
}

bool UDWLanguageSubsystem::SubmitTrade(FGuid RequestId,FText& OutMessage)
{
    using namespace DWLanguageWorld;const TSharedPtr<FDWLanguageSelectionStore> Store=Selections;const auto R=Store?Store->Requests.FindRef(RequestId):nullptr;
    if(!RequestId.IsValid()||!R||!Current(R->Identity)){OutMessage=Stale(this);return false;}
    if(R->bSubmitted){OutMessage=R->Message;return R->bResult;}
    if(bChanging){OutMessage=Unavailable(this);return false;}
    const auto Finish=[&](bool Ok,const FText& Message){R->bSubmitted=true;R->bResult=Ok;R->Message=Message;OutMessage=Message;return Ok;};
    if(R->bClosed||!IsReady()||!R->Mechanism.bTrade||RequestedPanel!=EDWLanguagePanel::Trade||R->ModelRevision!=GetRevision()||
        R->InventoryRevision!=R->Identity.Inventory->GetInventoryRevision()||!CheckContext(R->Identity.ContextId,EDWLanguageCarrierKind::Trade,OutMessage))return Finish(false,Stale(this));
    TStrongObjectPtr<UDWLanguageSubsystem> KeepAlive(this);TGuardValue<bool> Guard(bChanging,true);FDWLanguageMechanismView Now;
    if(!GetMechanism(R->Identity.ContextId,Now)||!SameMechanism(Now,R->Mechanism)||Now.RequiredItemId.IsNone()||
        Now.RequiredCount<1||Now.RewardItemId.IsNone()||Now.RewardCount<1||
        DWRetiredItemPolicy::IsRetired(Now.RequiredItemId)||DWRetiredItemPolicy::IsRetired(Now.RewardItemId))return Finish(false,Stale(this));
    FDWPreparedInventoryChange Inventory;bool BagFull=false;auto* Bag=R->Identity.Inventory.Get();
    if(!FDWLanguageInventoryTransaction::PrepareTrade(*Bag,R->Units,Now,Inventory,BagFull))
        return Finish(false,BagFull?DWText(this,TEXT("背包已满，请先整理；未扣任何物品。"),TEXT("The inventory is full. Make room first; no items were spent.")):
            DWText(this,TEXT("交换物品或数量不符合要求，未扣除物品。"),TEXT("The selected items or quantity do not match; no items were spent.")));
    if(!Current(R->Identity)||!CheckContext(R->Identity.ContextId,EDWLanguageCarrierKind::Trade,OutMessage)||
        !FDWLanguageInventoryTransaction::CommitInventory(*Bag,MoveTemp(Inventory)))return Finish(false,Stale(this));
    // NPC trades may repeat through a NEW request. This epoch's request receipt is adopted before any observer runs.
    Finish(true,DWText(this,TEXT("交换完成。"),TEXT("Trade completed.")));
    Store->bHasContext=false;ActiveCarrier.Reset();RequestedContext=RequestedEntry=NAME_None;RequestedPanel=EDWLanguagePanel::None;
    const int64 Expected=GetRevision();const TWeakObjectPtr<UDWLanguageSubsystem> WeakThis(this);
    Bag->OnChanged.Broadcast();
    if(!WeakThis.IsValid()||Selections!=Store||!Current(R->Identity)||GetRevision()!=Expected)return true;
    PublishProgress();
    if(WeakThis.IsValid()&&Selections==Store&&Current(R->Identity)&&GetRevision()==Expected&&RequestedPanel==EDWLanguagePanel::None&&RequestedContext.IsNone())OnPanelRequested.Broadcast();
    return true;
}
bool UDWLanguageSubsystem::ReturnToInteraction(FText& OutMessage)
{
    auto* C=ActiveCarrier.Get();
    if(!IsValid(C)||!CheckContext(C->GetContextId(),C->Kind,OutMessage)){CloseInteraction();return false;}
    if(C->Kind==EDWLanguageCarrierKind::Relic)RequestedPanel=EDWLanguagePanel::Relic;
    else if(C->Kind==EDWLanguageCarrierKind::Offering)RequestedPanel=EDWLanguagePanel::Offering;
    else if(C->Kind==EDWLanguageCarrierKind::Trade)RequestedPanel=EDWLanguagePanel::Trade;
    else{OutMessage=DWText(this,TEXT("没有可返回的外层机关。"),TEXT("There is no outer mechanism to return to."));CloseInteraction();return false;}
    RequestedEntry=NAME_None;OutMessage=DWText(this,TEXT("已返回当前交互。"),TEXT("Returned to the current interaction."));OnPanelRequested.Broadcast();return true;
}
void UDWLanguageSubsystem::CloseInteraction()
{
    if(Selections)for(auto& Pair:Selections->Requests)if(!Pair.Value->bSubmitted)Pair.Value->bClosed=true;
    const bool Changed=RequestedPanel!=EDWLanguagePanel::None||!RequestedContext.IsNone()||!RequestedEntry.IsNone()||ActiveCarrier.IsValid();
    if(Selections)Selections->bHasContext=false;
    ActiveCarrier.Reset();RequestedContext=RequestedEntry=NAME_None;RequestedPanel=EDWLanguagePanel::None;
    if(Changed)OnPanelRequested.Broadcast();
}

bool ADWPlayerCharacter::HandleTrialConsumable(const FDWInventorySlotSnapshot& Slot,bool& Handled)
{
    Handled=false;if(DWRetiredItemPolicy::IsRetired(Slot.ItemId)){Handled=true;return false;}
    auto* A01=FindComponentByClass<UDWCharacterA01Component>();auto* GI=GetGameInstance();auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
    const auto Config=L?L->GetTrialConfig():nullptr;const TSharedPtr<FJsonObject>* Consumables=nullptr;const TSharedPtr<FJsonObject>* Definition=nullptr;
    if(!A01||!A01->IsEnabled()||!Config||!Config->TryGetObjectField(TEXT("runtime_consumables"),Consumables)||!Consumables||!(*Consumables)->TryGetObjectField(Slot.ItemId.ToString(),Definition))return false;
    Handled=true;if(!Definition||!(*Definition)||!Inventory||IsDead()||bRestoringSave||bGameplayActionInProgress||!Inventory->IsSlotSnapshotCurrent(Slot))return false;
    double Water=0.,Satiety=0.,Consume=0.,ReturnCount=0.;FString Returned;
    if(!(*Definition)->TryGetNumberField(TEXT("hydration"),Water)||!(*Definition)->TryGetNumberField(TEXT("satiety"),Satiety)||!(*Definition)->TryGetNumberField(TEXT("consume"),Consume)||
        !FMath::IsFinite(Water)||Water<0.||!FMath::IsFinite(Satiety)||Satiety<0.||!FMath::IsFinite(Consume)||Consume<1||Consume>Slot.Quantity||FMath::FloorToDouble(Consume)!=Consume)return false;
    if((*Definition)->HasField(TEXT("return_item")))
    {if(!(*Definition)->TryGetStringField(TEXT("return_item"),Returned)||Returned.IsEmpty()||!(*Definition)->TryGetNumberField(TEXT("return_count"),ReturnCount)||!FMath::IsFinite(ReturnCount)||ReturnCount<1||ReturnCount>MAX_int32||FMath::FloorToDouble(ReturnCount)!=ReturnCount)return false;}
    FDWPreparedInventoryChange Bag;FDWCharacterA01Runtime Refill;
    const uint64 Revision=A01->GetResourceRevision();const double ExpectedHealth=GetHealth();
    if(!FDWLanguageInventoryTransaction::PrepareConsume(*Inventory,Slot,static_cast<int32>(Consume),FName(*Returned),static_cast<int32>(ReturnCount),Bag)||!A01->PrepareSurvivalRefill(Water,Satiety,Refill))return false;
    TStrongObjectPtr<ADWPlayerCharacter> Keep(this);TGuardValue<bool> Guard(bGameplayActionInProgress,true);
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);const uint64 Epoch=Authority?Authority->GetWorldEpoch():0;const auto Life=Authority?Authority->FindRegisteredActorLife(this):DWCombatCommit::FEntityLife{};
    if(!Authority||!Life.Entity||!Life.Life||!FDWLanguageInventoryTransaction::CommitWater(*Inventory,MoveTemp(Bag),*A01,MoveTemp(Refill),Revision,ExpectedHealth))return false;
    const TWeakObjectPtr<UDWInventoryComponent> OriginalInventory(Inventory.Get());Inventory->OnChanged.Broadcast();
    if(!IsActorBeingDestroyed()&&!IsDead()&&!bRestoringSave&&Inventory==OriginalInventory.Get()&&UDWCombatAuthoritySubsystem::Get(this)==Authority&&Authority->GetWorldEpoch()==Epoch)
    {const auto Now=Authority->FindRegisteredActorLife(this);if(Now.Entity==Life.Entity&&Now.Life==Life.Life)UDWGatheringComponent::Notify(this,TEXT("已使用物品并补充生存资源。"),TEXT("Item consumed and survival resources restored."));}
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
namespace DWLanguageInventoryTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameplayConfig> Config;
    TStrongObjectPtr<UDWInventoryComponent> Inventory;
    DWLanguage::FModel Model;
    FString Error;bool Ready=false;
    FFixture(int32 Slots=4)
    {
        Config.Reset(NewObject<UDWGameplayConfig>());Config->MaxInventorySlots=Slots;Config->MaxStackSize=40;
        Inventory.Reset(NewObject<UDWInventoryComponent>());Inventory->InitializeInventory(Config.Get());
        DWLanguage::FCatalog C;C.KnownItemIds={TEXT("Water"),TEXT("Flour"),TEXT("Dough")};
        DWLanguage::FDoorDefinition D;D.RequiredItemId=TEXT("Water");D.RequiredCount=2;D.MaxUnitsPerSubmission=1;D.RewardPoints=0;
        C.Doors.Add(TEXT("Door_Test"),D);C.PointSources.Add(TEXT("Test_Independent"),1);
        Ready=Model.Initialize(C,DWLanguage::FState{},Error);
    }
    TArray<FDWInventorySlotSnapshot> Select(int32 Slot,int32 Count)
    {FDWInventorySlotSnapshot S;TArray<FDWInventorySlotSnapshot> Out;if(Inventory->CaptureSlotSnapshot(Slot,S))for(int32 I=0;I<Count;++I)Out.Add(S);return Out;}
};
FDWLanguageMechanismView Trade()
{FDWLanguageMechanismView V;V.bTrade=true;V.RequiredItemId=TEXT("Water");V.RequiredCount=2;V.RewardItemId=TEXT("Flour");V.RewardCount=1;return V;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageOfferingJointTest,
    "DoughWorld.Language.Inventory.OfferingWrongItemPaysOnceAndCommitsBothFacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageOfferingJointTest::RunTest(const FString&)
{
    using namespace DWLanguageInventoryTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
    if(!F.Inventory->TryAddItem(TEXT("Dough"),2))return false;
    auto Units=F.Select(0,1);FDWPreparedInventoryChange I;DWLanguage::FPreparedChange L;DWLanguage::FOperationResult R;
    if(!TestTrue(TEXT("Prepare wrong-item model payment"),F.Model.PrepareSubmitDoor(TEXT("Door_Test"),TEXT("Request_1"),TEXT("Dough"),1,L,R))||
        !TestTrue(TEXT("Prepare exact snapshot debit"),FDWLanguageInventoryTransaction::PrepareDebit(*F.Inventory.Get(),Units,I)))return false;
    TestTrue(TEXT("Both facts adopt without an intermediate public inventory commit"),FDWLanguageInventoryTransaction::CommitOffering(*F.Inventory.Get(),MoveTemp(I),F.Model,MoveTemp(L),R));
    TestEqual(TEXT("Wrong offering really consumes exactly one"),F.Inventory->CountItem(TEXT("Dough")),1);
    TestEqual(TEXT("Wrong offering does not increase accepted progress"),F.Model.GetState().DoorProgress.FindChecked(TEXT("Door_Test")).AcceptedCount,0);
    TestEqual(TEXT("Door never grants talent points"),F.Model.GetState().AvailableTalentPoints,0);
    TestFalse(TEXT("Persisted stable submission cannot prepare another payment"),F.Model.PrepareSubmitDoor(TEXT("Door_Test"),TEXT("Request_1"),TEXT("Dough"),1,L,R));
    TestTrue(TEXT("Replay is explicit"),R.Code==DWLanguage::ECode::AlreadyApplied);
    TestEqual(TEXT("Replay leaves inventory intact"),F.Inventory->CountItem(TEXT("Dough")),1);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageOfferingStaleTest,
    "DoughWorld.Language.Inventory.EitherRevisionChangeRejectsBeforeJointAdoption",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageOfferingStaleTest::RunTest(const FString&)
{
    using namespace DWLanguageInventoryTests;FFixture F;if(!F.Ready||!F.Inventory->TryAddItem(TEXT("Water"),3))return false;
    FDWPreparedInventoryChange I;DWLanguage::FPreparedChange L;DWLanguage::FOperationResult R;
    if(!F.Model.PrepareSubmitDoor(TEXT("Door_Test"),TEXT("Request_I"),TEXT("Water"),1,L,R)||
        !FDWLanguageInventoryTransaction::PrepareDebit(*F.Inventory.Get(),F.Select(0,1),I))return false;
    F.Inventory->TryAddItem(TEXT("Flour"),1);
    TestFalse(TEXT("Inventory revision invalidates the whole payment"),FDWLanguageInventoryTransaction::CommitOffering(*F.Inventory.Get(),MoveTemp(I),F.Model,MoveTemp(L),R));
    TestEqual(TEXT("Water not spent"),F.Inventory->CountItem(TEXT("Water")),3);
    TestFalse(TEXT("Door unchanged"),F.Model.GetState().DoorProgress.Contains(TEXT("Door_Test")));
    if(!F.Model.PrepareSubmitDoor(TEXT("Door_Test"),TEXT("Request_M"),TEXT("Water"),1,L,R)||
        !FDWLanguageInventoryTransaction::PrepareDebit(*F.Inventory.Get(),F.Select(0,1),I))return false;
    DWLanguage::FPreparedChange Other;DWLanguage::FOperationResult OtherResult;
    if(!F.Model.PrepareGrantPoints(TEXT("Test_Independent"),Other,OtherResult)||!F.Model.Commit(MoveTemp(Other),OtherResult))return false;
    TestFalse(TEXT("Model revision also invalidates the whole payment"),FDWLanguageInventoryTransaction::CommitOffering(*F.Inventory.Get(),MoveTemp(I),F.Model,MoveTemp(L),R));
    TestEqual(TEXT("Stale model cannot partially debit water"),F.Inventory->CountItem(TEXT("Water")),3);
    TestFalse(TEXT("Door still unchanged"),F.Model.GetState().DoorProgress.Contains(TEXT("Door_Test")));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageTradeAtomicTest,
    "DoughWorld.Language.Inventory.TradeExactSnapshotCountAndOutputOneCommit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageTradeAtomicTest::RunTest(const FString&)
{
    using namespace DWLanguageInventoryTests;FFixture F;if(!F.Ready||!F.Inventory->TryAddItem(TEXT("Water"),3))return false;
    const auto Units=F.Select(0,2);FDWPreparedInventoryChange Plan;bool Full=false;const int64 Before=F.Inventory->GetInventoryRevision();
    TestTrue(TEXT("Two clicks reference the same immutable stack snapshot"),FDWLanguageInventoryTransaction::PrepareTrade(*F.Inventory.Get(),Units,Trade(),Plan,Full));
    TestFalse(TEXT("Output capacity exists before payment"),Full);
    TestTrue(TEXT("Trade commits once"),FDWLanguageInventoryTransaction::CommitInventory(*F.Inventory.Get(),MoveTemp(Plan)));
    TestEqual(TEXT("Exactly two inputs removed"),F.Inventory->CountItem(TEXT("Water")),1);
    TestEqual(TEXT("Exactly one configured output added"),F.Inventory->CountItem(TEXT("Flour")),1);
    TestEqual(TEXT("Only one inventory revision was committed"),F.Inventory->GetInventoryRevision(),Before+1);
    TestFalse(TEXT("Consumed token cannot commit again"),FDWLanguageInventoryTransaction::CommitInventory(*F.Inventory.Get(),MoveTemp(Plan)));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageTradeFailureTest,
    "DoughWorld.Language.Inventory.FullBagAndWrongTradeNeverPay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageTradeFailureTest::RunTest(const FString&)
{
    using namespace DWLanguageInventoryTests;FFixture FullBag(1);if(!FullBag.Ready||!FullBag.Inventory->TryAddItem(TEXT("Water"),2))return false;
    FDWPreparedInventoryChange Plan;bool Full=false;const int64 Before=FullBag.Inventory->GetInventoryRevision();
    TestFalse(TEXT("Cannot spend inputs merely to manufacture output space"),FDWLanguageInventoryTransaction::PrepareTrade(*FullBag.Inventory.Get(),FullBag.Select(0,2),Trade(),Plan,Full));
    TestTrue(TEXT("Full bag is reported before payment"),Full);
    TestEqual(TEXT("Full-bag rejection preserves inputs"),FullBag.Inventory->CountItem(TEXT("Water")),2);
    TestEqual(TEXT("Full-bag rejection preserves revision"),FullBag.Inventory->GetInventoryRevision(),Before);
    FFixture Wrong;if(!Wrong.Ready||!Wrong.Inventory->TryAddItem(TEXT("Dough"),2))return false;
    Full=false;TestFalse(TEXT("Wrong selected input rejects"),FDWLanguageInventoryTransaction::PrepareTrade(*Wrong.Inventory.Get(),Wrong.Select(0,2),Trade(),Plan,Full));
    TestFalse(TEXT("Wrong input is not a capacity failure"),Full);
    TestEqual(TEXT("Wrong trade does not swallow input"),Wrong.Inventory->CountItem(TEXT("Dough")),2);
    TestEqual(TEXT("Wrong trade grants no output"),Wrong.Inventory->CountItem(TEXT("Flour")),0);return true;
}
#endif
