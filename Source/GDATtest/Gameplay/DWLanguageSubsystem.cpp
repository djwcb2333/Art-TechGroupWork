#include "DWLanguageSubsystem.h"
#include "DWFormalLanguagePersistence.h"
#include "DWLanguageModel.h"
#include "DWCombatEquipment.h"
#include "DWLocalizationLibrary.h"
#include "DWPlayerCharacter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Guid.h"
#include "Engine/World.h"

namespace DWLanguageRequests
{
bool Fail(FString& Error,const TCHAR* Text){Error=Text;return false;}
FText Message(const UObject* Context,DWLanguage::ECode Code,int32 Limit=40)
{
    using DWLanguage::ECode;
    switch(Code)
    {
    case ECode::NoChange:return DWText(Context,TEXT("内容没有变化，已保存"),TEXT("Already saved; no changes."));
    case ECode::AlreadyCompleted:return DWText(Context,TEXT("这处残骸已解读，可回看剧情"),TEXT("Already decoded; the story can be reviewed."));
    case ECode::AlreadyApplied:return DWText(Context,TEXT("已学习，不能重复扣点"),TEXT("Already learned; no points were spent."));
    case ECode::IncompleteSolution:return DWText(Context,TEXT("请先填满全部槽位"),TEXT("Fill every slot before checking."));
    case ECode::IncorrectSolution:return DWText(Context,TEXT("答案不正确，请再试一次"),TEXT("That answer is incorrect. Try again."));
    case ECode::UnknownId:return DWText(Context,TEXT("条目或定义不存在"),TEXT("The entry or definition does not exist."));
    case ECode::GuessTooLong:return FText::Format(DWText(Context,TEXT("猜测最多{0}个字符，未保存"),TEXT("Guesses may contain at most {0} characters. Not saved.")),FText::AsNumber(Limit));
    case ECode::InsufficientPoints:return DWText(Context,TEXT("天赋点不足"),TEXT("Not enough talent points."));
    case ECode::PrerequisiteMissing:return DWText(Context,TEXT("需要先学习全部前置天赋"),TEXT("Learn every prerequisite first."));
    case ECode::DependentLearned:return DWText(Context,TEXT("只能遗忘末端节点，请先遗忘后续天赋"),TEXT("Only leaf nodes can be forgotten. Forget their dependents first."));
    case ECode::DisabledTalent:return DWText(Context,TEXT("该天赋尚未配置，暂不可学习"),TEXT("This talent is not configured and cannot be learned."));
    case ECode::StalePrepared:return DWText(Context,TEXT("状态已变化，请刷新后重试"),TEXT("The state changed. Refresh and try again."));
    default:return DWText(Context,TEXT("当前无法完成操作，未提交更改"),TEXT("The request is unavailable; no changes were committed."));
    }
}
bool ReadProgress(const DWLanguage::FCatalog& Catalog,const FString& Json,FString& Playthrough,DWLanguage::FState& State,FDWCombatStarterGrantState& Starter,FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||Root->Values.Num()!=4)
        return Fail(Error,TEXT("Invalid language progress wrapper."));
    double Version=0.;FString Id,StarterId;const TSharedPtr<FJsonObject>* Native=nullptr;const TSharedPtr<FJsonObject>* Grant=nullptr;FGuid Guid;bool Granted=false;
    if(!Root->TryGetNumberField(TEXT("schema_version"),Version)||Version!=1.||!Root->TryGetStringField(TEXT("playthrough_id"),Id)||
        !FGuid::ParseExact(Id,EGuidFormats::Digits,Guid)||!Guid.IsValid()||!Root->TryGetObjectField(TEXT("model"),Native)||!Native||!Native->IsValid()||
        !Root->TryGetObjectField(TEXT("starter_grant"),Grant)||!Grant||!Grant->IsValid()||(*Grant)->Values.Num()!=2||
        !(*Grant)->TryGetStringField(TEXT("playthrough_id"),StarterId)||StarterId!=Id||!(*Grant)->TryGetBoolField(TEXT("granted"),Granted)||!Granted)
        return Fail(Error,TEXT("Invalid progress version, stable playthrough ID or native state."));
    FString NativeJson;if(!FJsonSerializer::Serialize(Native->ToSharedRef(),TJsonWriterFactory<>::Create(&NativeJson))||!DWLanguage::StateFromJson(Catalog,NativeJson,State,Error))return false;
    Playthrough=MoveTemp(Id);Starter={};Starter.PlaythroughId=Playthrough;Starter.bGranted=true;Error.Reset();return true;
}
}

UDWLanguageSubsystem::~UDWLanguageSubsystem()=default;
void UDWLanguageSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);FString Error;
    bInitialized=LoadDefinitions(Error);
    if(!bInitialized)UE_LOG(LogTemp,Error,TEXT("Language definitions unavailable: %s"),*Error);
}
void UDWLanguageSubsystem::Deinitialize()
{
    bInitialized=false;FormalSession.Reset();++SessionGeneration;ActivePlayer.Reset();ActiveCarrier.Reset();Selections.Reset();StarterGrant.Reset();RequestedPanel=EDWLanguagePanel::None;
    RequestedEntry=RequestedContext=NAME_None;PlaythroughId.Reset();GlyphCache.Reset();Model.Reset();
    EntryMetadata.Reset();ClueMetadata.Reset();RelicMetadata.Reset();TalentMetadata.Reset();Definitions.Reset();TrialConfig.Reset();
    OnProgressChanged.Clear();OnPanelRequested.Clear();Super::Deinitialize();
}
bool UDWLanguageSubsystem::IsTrialWorld()const
{
    const UWorld* World=GetWorld();if(!World||World->bIsTearingDown)return false;
    return UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest");
}
bool UDWLanguageSubsystem::IsFormalSession()const{return FDWFormalLanguagePersistence::IsCurrent(*this);}
bool UDWLanguageSubsystem::IsReady()const
{
    const auto* Player=ActivePlayer.Get();return bInitialized&&Model&&Model->IsReady()&&((IsTrialWorld()&&!FormalSession.IsValid())||IsFormalSession())&&IsValid(Player)&&
        !Player->IsActorBeingDestroyed()&&Player->GetWorld()==GetWorld()&&Player->GetGameInstance()==GetGameInstance();
}
bool UDWLanguageSubsystem::StartPlaytest(ADWPlayerCharacter* Player,FString& Error)
{
    if(!IsInGameThread()||!IsTrialWorld()||!IsValid(Player)||Player->IsActorBeingDestroyed()||Player->GetWorld()!=GetWorld()||
        Player->GetGameInstance()!=GetGameInstance())return DWLanguageRequests::Fail(Error,TEXT("Language trial requires the exact trial map and its current valid player."));
    if(!bInitialized||!Model||!Model->IsReady())
    {if(!LoadDefinitions(Error))return false;bInitialized=true;}
    if(FormalSession)
    {
        // A deliberate formal -> trial transition starts a separate trial owner.
        auto Fresh=MakeUnique<DWLanguage::FModel>();if(!Fresh->Initialize(Model->GetCatalog(),{},Error))return false;
        Model=MoveTemp(Fresh);FormalSession.Reset();StarterGrant.Reset();PlaythroughId.Reset();TrialContainerVersion=4;++SessionGeneration;
    }
    // Pawn replacement/respawn never creates a new playthrough or clears paid-cost/reward facts.
    ActivePlayer=Player;if(PlaythroughId.IsEmpty())PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
    if(!StarterGrant){StarterGrant=MakeShared<FDWCombatStarterGrantState>();StarterGrant->PlaythroughId=PlaythroughId;}
    if(StarterGrant->PlaythroughId.IsEmpty()&&!StarterGrant->bGranted&&StarterGrant->Revision==0)StarterGrant->PlaythroughId=PlaythroughId;
    if(StarterGrant->PlaythroughId!=PlaythroughId)return DWLanguageRequests::Fail(Error,TEXT("Starter receipt does not belong to the current playthrough."));
    Error.Reset();return true;
}
FDWCombatStarterGrantState& UDWLanguageSubsystem::GetStarterGrantState()
{
    if(!StarterGrant){StarterGrant=MakeShared<FDWCombatStarterGrantState>();StarterGrant->PlaythroughId=PlaythroughId;}
    return *StarterGrant;
}
bool UDWLanguageSubsystem::SaveGuess(FName Id,const FString& Text,FText& OutMessage)
{
    auto* Player=ActivePlayer.Get();
    if(!IsReady()||bChanging||!Player||Player->IsDead()||Player->IsCombatRestoreInProgress())
    {OutMessage=DWText(this,TEXT("当前无法编辑笔记"),TEXT("Notes cannot be edited right now."));return false;}
    TGuardValue<bool> Guard(bChanging,true);DWLanguage::FPreparedChange Plan;DWLanguage::FOperationResult Result;
    if(!Model->PrepareSaveGuess(Id,Text,Plan,Result))
    {OutMessage=DWLanguageRequests::Message(this,Result.Code,GetGuessCharacterLimit());return Result.Code==DWLanguage::ECode::NoChange;}
    if(!Model->Commit(MoveTemp(Plan),Result)){OutMessage=DWLanguageRequests::Message(this,Result.Code);return false;}
    OutMessage=DWText(this,TEXT("猜测已保存"),TEXT("Guess saved."));PublishProgress();return true;
}
bool UDWLanguageSubsystem::SubmitRelic(FName Id,const TArray<FName>& Meanings,FText& OutMessage)
{
    auto* Player=ActivePlayer.Get();
    if(!IsReady()||bChanging||!Player||Player->IsDead()||Player->IsCombatRestoreInProgress())
    {OutMessage=DWText(this,TEXT("当前无法解读残骸"),TEXT("The relic cannot be decoded right now."));return false;}
    if(!CheckContext(Id,EDWLanguageCarrierKind::Relic,OutMessage))return false;
    TGuardValue<bool> Guard(bChanging,true);DWLanguage::FPreparedChange Plan;DWLanguage::FOperationResult Result;
    if(!Model->PrepareSolveRelic(Id,Meanings,Plan,Result))
    {OutMessage=DWLanguageRequests::Message(this,Result.Code);return Result.Code==DWLanguage::ECode::AlreadyCompleted;}
    if(!CheckContext(Id,EDWLanguageCarrierKind::Relic,OutMessage))return false;
    if(!Model->Commit(MoveTemp(Plan),Result)){OutMessage=DWLanguageRequests::Message(this,Result.Code);return false;}
    OutMessage=FText::Format(DWText(this,TEXT("解读完成，获得{0}点天赋点"),TEXT("Decoded. Gained {0} talent points.")),FText::AsNumber(Result.PointDelta));
    PublishProgress();return true;
}
bool UDWLanguageSubsystem::LearnTalent(FName Id,FText& OutMessage)
{
    auto* Player=ActivePlayer.Get();auto* A01=Player?Player->CharacterA01.Get():nullptr;
    if(!IsInGameThread()||!IsReady()||bChanging||!Player||Player->IsDead()||Player->IsCombatRestoreInProgress()||!A01||!A01->IsEnabled())
    {OutMessage=DWText(this,TEXT("当前无法学习天赋"),TEXT("Talents cannot be learned right now."));return false;}
    TGuardValue<bool> Guard(bChanging,true);DWLanguage::FPreparedChange Plan;DWLanguage::FOperationResult Result;
    if(!Model->PrepareLearnTalent(Id,Plan,Result)){OutMessage=DWLanguageRequests::Message(this,Result.Code);return false;}
    const uint64 Revision=A01->GetResourceRevision();const double Health=Player->GetHealth();
    TSet<FName> Learned;for(const auto& Pair:Plan.GetCandidateState().LearnedTalentPaidCosts)Learned.Add(Pair.Key);
    FDWCharacterA01Config Config;FString Error;auto Character=A01->GetRuntime();Character.SyncCurrentHealth(Health);
    if(!A01->BuildConfigForLearned(Learned,Config,Error)||!Character.ApplyConfig(Config))
    {OutMessage=DWText(this,TEXT("天赋属性配置无效，未扣点"),TEXT("Invalid derived talent configuration; no points were spent."));return false;}
    if(!Model->CanCommit(Plan)||!A01->CanAdoptRuntime(Revision,Health))
    {OutMessage=DWLanguageRequests::Message(this,DWLanguage::ECode::StalePrepared);return false;}
    // One final no-callback joint commit. Neither publication can precede the other model's facts.
    Model->AdoptPreparedUnchecked(MoveTemp(Plan),Result);A01->AdoptRuntimeUnchecked(MoveTemp(Character));
    OutMessage=DWText(this,TEXT("已学习天赋"),TEXT("Talent learned."));PublishProgress();return true;
}
bool UDWLanguageSubsystem::ForgetTalent(FName Id,FText& OutMessage)
{
    auto* Player=ActivePlayer.Get();auto* A01=Player?Player->CharacterA01.Get():nullptr;
    if(!IsInGameThread()||!IsReady()||bChanging||!Player||Player->IsDead()||Player->IsCombatRestoreInProgress()||!A01||!A01->IsEnabled())
    {OutMessage=DWText(this,TEXT("当前无法遗忘天赋"),TEXT("Talents cannot be forgotten right now."));return false;}
    if(Id==TEXT("b3")&&HasTalent(Id)&&A01->IsDurationTalentContributing())
    {OutMessage=DWText(this,TEXT("本次接种正在使用该时长加成，请结束形态后再遗忘"),TEXT("This inoculation uses the duration bonus. Finish the current form before forgetting it."));return false;}
    if(Id==TEXT("a1")&&!A01->CanForgetExtraShortcut())
    {OutMessage=DWText(this,TEXT("第四快捷格装备状态暂不可变更，未遗忘且未退款"),TEXT("The fourth-slot equipment state cannot change right now; no refund was made."));return false;}
    TGuardValue<bool> Guard(bChanging,true);DWLanguage::FPreparedChange Plan;DWLanguage::FOperationResult Result;
    if(!Model->PrepareForgetTalent(Id,Plan,Result)){OutMessage=DWLanguageRequests::Message(this,Result.Code);return false;}
    const uint64 Revision=A01->GetResourceRevision();const double Health=Player->GetHealth();
    TSet<FName> Learned;for(const auto& Pair:Plan.GetCandidateState().LearnedTalentPaidCosts)Learned.Add(Pair.Key);
    FDWCharacterA01Config Config;FString Error;auto Character=A01->GetRuntime();Character.SyncCurrentHealth(Health);
    if(!A01->BuildConfigForLearned(Learned,Config,Error)||!Character.ApplyConfig(Config))
    {OutMessage=DWText(this,TEXT("遗忘后的属性配置无效，未更改节点或点数"),TEXT("Invalid resulting configuration; no talent or point change was made."));return false;}
    if(!Model->CanCommit(Plan)||!A01->CanAdoptRuntime(Revision,Health)||(Id==TEXT("a1")&&!A01->CanForgetExtraShortcut()))
    {OutMessage=DWLanguageRequests::Message(this,DWLanguage::ECode::StalePrepared);return false;}
    Model->AdoptPreparedUnchecked(MoveTemp(Plan),Result);A01->AdoptRuntimeUnchecked(MoveTemp(Character));
    if(Id==TEXT("a1"))A01->OnExtraShortcutRemoved(); // Native facts only; bag contents/capacity and old cooldowns survive.
    OutMessage=FText::Format(DWText(this,TEXT("已遗忘，退回实际支付的{0}点"),TEXT("Forgotten. Refunded the actual {0} points paid.")),FText::AsNumber(Result.PointDelta));PublishProgress();return true;
}
void UDWLanguageSubsystem::RequestJournal(FName Id)
{
    if(!IsReady()||(!Id.IsNone()&&!Model->GetCatalog().Entries.Contains(Id)))return;
    RequestedPanel=EDWLanguagePanel::Journal;RequestedEntry=Id;OnPanelRequested.Broadcast();
}
void UDWLanguageSubsystem::RequestTalents()
{if(!IsReady())return;RequestedPanel=EDWLanguagePanel::Talents;RequestedEntry=NAME_None;OnPanelRequested.Broadcast();}
void UDWLanguageSubsystem::ClearPanelRequest()
{
    const bool Changed=RequestedPanel!=EDWLanguagePanel::None||!RequestedEntry.IsNone();RequestedPanel=EDWLanguagePanel::None;RequestedEntry=NAME_None;
    // Keep outer interaction context. ReturnToInteraction/CloseInteraction are owned by the root coordinator.
    if(Changed)OnPanelRequested.Broadcast();
}
void UDWLanguageSubsystem::PublishProgress()
{
    // Joint talent/character and reward/payment facts have already committed. Notifications never derive/write resources.
    OnProgressChanged.Broadcast();
}
bool UDWLanguageSubsystem::CaptureProgressJson(FString& OutJson,FString& Error)const
{
    if(!IsTrialWorld()||FormalSession||!bInitialized||!Model||!Model->IsReady()||PlaythroughId.IsEmpty()||!StarterGrant||!StarterGrant->bGranted||StarterGrant->PlaythroughId!=PlaythroughId)
        return DWLanguageRequests::Fail(Error,TEXT("No complete initialized language playthrough/starter transaction to capture."));
    FString NativeJson;if(!DWLanguage::StateToJson(Model->GetCatalog(),Model->GetState(),NativeJson,Error))return false;
    TSharedPtr<FJsonObject> Native;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NativeJson),Native)||!Native)return DWLanguageRequests::Fail(Error,TEXT("Native language state could not be wrapped."));
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schema_version"),1);Root->SetStringField(TEXT("playthrough_id"),PlaythroughId);Root->SetObjectField(TEXT("model"),Native);
    auto Grant=MakeShared<FJsonObject>();Grant->SetStringField(TEXT("playthrough_id"),StarterGrant->PlaythroughId);Grant->SetBoolField(TEXT("granted"),StarterGrant->bGranted);Root->SetObjectField(TEXT("starter_grant"),Grant);
    FString Json;if(!FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json)))return DWLanguageRequests::Fail(Error,TEXT("Language progress capture failed."));
    OutJson=MoveTemp(Json);Error.Reset();return true;
}
bool UDWLanguageSubsystem::ValidateProgressJson(const FString& Json,FString& Error)const
{
    if(!bInitialized||!Model||!Model->IsReady())return DWLanguageRequests::Fail(Error,TEXT("Language definitions are unavailable."));
    FString Id;DWLanguage::FState State;FDWCombatStarterGrantState Starter;return DWLanguageRequests::ReadProgress(Model->GetCatalog(),Json,Id,State,Starter,Error);
}
bool UDWLanguageSubsystem::RestoreProgressJson(const FString& Json,FString& Error)
{
    if(!IsTrialWorld()||FormalSession||!bInitialized||!Model||!Model->IsReady()||bChanging)return DWLanguageRequests::Fail(Error,TEXT("Language state is unavailable or already changing."));
    FString Id;DWLanguage::FState State;FDWCombatStarterGrantState Starter;if(!DWLanguageRequests::ReadProgress(Model->GetCatalog(),Json,Id,State,Starter,Error))return false;
    auto NewStarter=MakeShared<FDWCombatStarterGrantState>(MoveTemp(Starter));
    TGuardValue<bool> Guard(bChanging,true);if(!Model->RestoreValidated(State,Error))return false;
    PlaythroughId=MoveTemp(Id);StarterGrant=MoveTemp(NewStarter);RequestedPanel=EDWLanguagePanel::None;RequestedEntry=RequestedContext=NAME_None;ActiveCarrier.Reset();Selections.Reset();
    // No publication here: the root restores inventory/character/mechanisms before PublishAfterRestore.
    Error.Reset();return true;
}
