#include "DWLanguageSubsystem.h"
#include "DWLanguageModel.h"
#include "DWCharacterA01SaveBridge.h"
#include "DWPlayerCharacter.h"
#include "DWLocalizationLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Texture2D.h"

namespace DWLanguageData
{
using FObject=TSharedPtr<FJsonObject>;
bool Error(FString& Out,const TCHAR* Text){Out=Text;return false;}
bool Load(const FString& Path,FObject& Out,FString& OutError)
{
    FString Text;if(!FFileHelper::LoadFileToString(Text,*Path))return Error(OutError,*FString::Printf(TEXT("Cannot read language trial data: %s"),*Path));
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)||!Out.IsValid())return Error(OutError,*FString::Printf(TEXT("Invalid language trial JSON: %s"),*Path));return true;
}
bool Id(const FObject& O,const TCHAR* Field,FName& Out)
{FString Text;if(!O||!O->TryGetStringField(Field,Text)||Text.IsEmpty())return false;Out=FName(*Text);return !Out.IsNone();}
bool Integer(const FObject& O,const TCHAR* Field,int32& Out)
{double V=0.;if(!O||!O->TryGetNumberField(Field,V)||!FMath::IsFinite(V)||FMath::FloorToDouble(V)!=V||V<MIN_int32||V>MAX_int32)return false;Out=static_cast<int32>(V);return true;}
bool Enabled(const FObject& O,bool& Out)
{Out=true;return !O->HasField(TEXT("enabled"))||O->TryGetBoolField(TEXT("enabled"),Out);}
bool Names(const FObject& O,const TCHAR* Field,TArray<FName>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!O||!O->TryGetArrayField(Field,Values))return false;
    for(const auto& V:*Values){FString Text;if(!V||!V->TryGetString(Text)||Text.IsEmpty())return false;FName Name(*Text);if(Name.IsNone())return false;Out.Add(Name);}return true;
}
bool Objects(const FObject& O,const TCHAR* Field,TArray<FObject>& Out)
{
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!O||!O->TryGetArrayField(Field,Values))return false;
    for(const auto& V:*Values){if(!V||V->Type!=EJson::Object||!V->AsObject())return false;Out.Add(V->AsObject());}return true;
}
FObject Object(const FObject& O,const TCHAR* Field)
{const auto V=O?O->TryGetField(Field):nullptr;return V&&V->Type==EJson::Object?V->AsObject():nullptr;}
FString Text(const FObject& O,const TCHAR* Field)
{FString Value;if(O)O->TryGetStringField(Field,Value);return Value;}
FText Localized(const UObject* Context,const FObject& O,const TCHAR* Field,const FString& Fallback=FString())
{
    const auto Both=Object(O,Field);const bool English=UDWLocalizationLibrary::GetLanguage(Context)==EDWGameLanguage::English;
    if(Both)
    {
        FString Value;if(Both->TryGetStringField(English?TEXT("en"):TEXT("zh"),Value)&&!Value.IsEmpty())return FText::FromString(Value);
        if(Both->TryGetStringField(English?TEXT("zh"):TEXT("en"),Value))return FText::FromString(Value);
    }
    FString Value;if(O&&O->TryGetStringField(Field,Value)&&!Value.IsEmpty())return FText::FromString(Value);
    return FText::FromString(Fallback);
}
double Parameter(const FObject& Trial,const FString& Key,double Fallback)
{
    const auto Params=Object(Trial,TEXT("parameters"));TSharedPtr<FJsonValue> Value=Params?Params->TryGetField(Key):nullptr;if(!Value)return Fallback;
    if(Value->Type==EJson::Object){const auto O=Value->AsObject();if(!O)return Fallback;Value=O->TryGetField(TEXT("value"));if(!Value)return Fallback;}
    double Number=0.;if(Value->TryGetNumber(Number)&&FMath::IsFinite(Number))return Number;
    bool Flag=false;return Value->TryGetBool(Flag)?(Flag?1.:0.):Fallback;
}
bool CountParameter(const FObject& Trial,const FString& Key,int32& Out)
{const double N=Parameter(Trial,Key,-1.);if(!FMath::IsFinite(N)||N<0.||N>MAX_int32||FMath::FloorToDouble(N)!=N)return false;Out=static_cast<int32>(N);return true;}
FText Reason(const UObject* Context,DWLanguage::ECode Code)
{
    using DWLanguage::ECode;
    switch(Code)
    {
    case ECode::DisabledTalent:return DWText(Context,TEXT("未配置，暂不可学习"),TEXT("Not configured; unavailable."));
    case ECode::PrerequisiteMissing:return DWText(Context,TEXT("需要满足全部前置天赋"),TEXT("Learn every prerequisite first."));
    case ECode::InsufficientPoints:return DWText(Context,TEXT("天赋点不足"),TEXT("Not enough talent points."));
    case ECode::DependentLearned:return DWText(Context,TEXT("先遗忘已学习的后续节点"),TEXT("Forget learned dependent nodes first."));
    case ECode::AlreadyApplied:return DWText(Context,TEXT("已学习"),TEXT("Already learned."));
    case ECode::UnknownId:return DWText(Context,TEXT("未知天赋"),TEXT("Unknown talent."));
    case ECode::NoChange:return DWText(Context,TEXT("尚未学习"),TEXT("Not learned."));
    case ECode::Ready:return FText::GetEmpty();
    default:return DWText(Context,TEXT("当前状态不可操作"),TEXT("Unavailable in the current state."));
    }
}
FText TalentDescription(const UDWLanguageSubsystem* S,FName Id,const FObject& Metadata)
{
    auto Number=[&](const TCHAR* Key,double Fallback){return FText::AsNumber(S->Parameter(Key,Fallback));};
    if(Id==TEXT("a1"))return FText::Format(DWText(S,TEXT("新增{0}个主手快捷格"),TEXT("Adds {0} main-hand quick slot.")),Number(TEXT("talent.a1.extra_mainhand_slots"),1.));
    if(Id==TEXT("a2"))return FText::Format(DWText(S,TEXT("饱腹上限+{0}"),TEXT("Satiety maximum +{0}.")),Number(TEXT("talent.a2.satiety_max_add"),20.));
    if(Id==TEXT("a3"))return FText::Format(DWText(S,TEXT("水分上限+{0}"),TEXT("Hydration maximum +{0}.")),Number(TEXT("talent.a3.hydration_max_add"),20.));
    if(Id==TEXT("a4"))return FText::Format(DWText(S,TEXT("最大生命上限+{0}"),TEXT("Maximum health +{0}.")),Number(TEXT("talent.a4.base_health_max_add"),20.));
    if(Id==TEXT("a5")||Id==TEXT("a6"))return DWText(S,TEXT("尚未配置；禁用且不扣点"),TEXT("Not configured; disabled and costs no points."));
    if(Id==TEXT("b1"))return DWText(S,TEXT("酵母不再增加饱腹消耗，只移除此形态的额外部分"),TEXT("Removes only the extra satiety drain from Yeast form."));
    if(Id==TEXT("b2"))return DWText(S,TEXT("霉菌不再增加水分消耗，只移除此形态的额外部分"),TEXT("Removes only the extra hydration drain from Mold form."));
    if(Id==TEXT("b3"))return FText::Format(DWText(S,TEXT("接种时长+{0}秒；遗忘还需满足当期贡献限制"),TEXT("Inoculation duration +{0} seconds; forgetting also checks its active contribution.")),Number(TEXT("talent.b3.duration_add_s"),60.));
    if(Id==TEXT("b4"))return FText::Format(DWText(S,TEXT("储气上限+{0}"),TEXT("Gas maximum +{0}.")),Number(TEXT("talent.b4.gas_max_add"),30.));
    if(Id==TEXT("b5"))return FText::Format(DWText(S,TEXT("腐化上限+{0}"),TEXT("Corruption maximum +{0}.")),Number(TEXT("talent.b5.corruption_max_add"),30.));
    if(Id==TEXT("b6"))return FText::Format(DWText(S,TEXT("接种损失最多为最大生命上限的{0}%"),TEXT("Inoculation loss is capped at {0}% of maximum health.")),FText::AsNumber(S->Parameter(TEXT("talent.b6.max_health_cap_loss_fraction"),.4)*100.));
    return Localized(S,Metadata,TEXT("effect_text"));
}
}

bool UDWLanguageSubsystem::LoadDefinitions(FString& Error)
{
    using namespace DWLanguageData;DWLanguage::FCatalog Catalog;FObject NewDefinitions,NewTrial;
    const FString Root=FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat");
    if(!Load(Root/TEXT("DWLanguageDefinitions.json"),NewDefinitions,Error)||!Load(Root/TEXT("DWA01TrialDefaults.json"),NewTrial,Error))return false;
    TMap<FName,FObject> NewEntries,NewClues,NewRelics,NewTalents;TArray<FObject> Rows;
    int32 GuessLimit=40;if(!CountParameter(NewTrial,TEXT("language.guess_max_chars"),GuessLimit)||GuessLimit<1)return DWLanguageData::Error(Error,TEXT("Missing/invalid language.guess_max_chars."));
    Catalog.MaxGuessCharacters=GuessLimit;
    TArray<FName> KnownItems;if(!Names(NewTrial,TEXT("runtime_known_item_ids"),KnownItems)||KnownItems.IsEmpty())return DWLanguageData::Error(Error,TEXT("Missing runtime_known_item_ids; root must supply the actual item catalog."));
    for(FName Item:KnownItems){if(Catalog.KnownItemIds.Contains(Item))return DWLanguageData::Error(Error,TEXT("Duplicate runtime known item identity."));Catalog.KnownItemIds.Add(Item);}
    if(!Objects(NewDefinitions,TEXT("entries"),Rows))return DWLanguageData::Error(Error,TEXT("Invalid language entries array."));
    for(const auto& O:Rows)
    {
        FName Entry;bool On=true;FString Kind;TArray<FName> Parts;
        if(!Id(O,TEXT("id"),Entry)||!Enabled(O,On))return DWLanguageData::Error(Error,TEXT("Invalid entry identity/enabled flag."));if(!On)continue;
        if(NewEntries.Contains(Entry)||!O->TryGetStringField(TEXT("kind"),Kind)||!Names(O,TEXT("parts"),Parts))return DWLanguageData::Error(Error,TEXT("Duplicate/invalid entry kind or ordered parts."));
        DWLanguage::FEntryDefinition Definition;
        if(Kind==TEXT("root")||Kind==TEXT("single"))
        {if(Parts.Num()!=1||Parts[0]!=Entry)return DWLanguageData::Error(Error,TEXT("Single entry parts must identify itself exactly once."));}
        else if(Kind==TEXT("compound")){Definition.Kind=DWLanguage::EEntryKind::Compound;Definition.Components=Parts;}
        else return DWLanguageData::Error(Error,TEXT("Unknown language entry kind."));
        Catalog.Entries.Add(Entry,MoveTemp(Definition));NewEntries.Add(Entry,O);
    }
    Rows.Reset();if(!Objects(NewDefinitions,TEXT("clues"),Rows))return DWLanguageData::Error(Error,TEXT("Invalid language clues array."));
    for(const auto& O:Rows)
    {
        FName Clue,Entry;bool On=true;if(!Id(O,TEXT("id"),Clue)||!Enabled(O,On))return DWLanguageData::Error(Error,TEXT("Invalid clue identity/enabled flag."));if(!On)continue;
        if(!Id(O,TEXT("entry_id"),Entry)||!Catalog.Entries.Contains(Entry)||NewClues.Contains(Clue))return DWLanguageData::Error(Error,TEXT("Unknown entry or duplicate clue identity."));
        Catalog.Entries.FindChecked(Entry).AllowedClueIds.Add(Clue);NewClues.Add(Clue,O);
    }
    TArray<FObject> Meanings;if(!Objects(NewDefinitions,TEXT("candidate_meanings"),Meanings))return DWLanguageData::Error(Error,TEXT("Invalid candidate meanings."));
    TSet<FName> MeaningIds;
    for(const auto& O:Meanings){FName Meaning;if(!Id(O,TEXT("id"),Meaning)||MeaningIds.Contains(Meaning)||Text(O,TEXT("zh")).IsEmpty()||Text(O,TEXT("en")).IsEmpty())return DWLanguageData::Error(Error,TEXT("Invalid or duplicate bilingual meaning identity."));MeaningIds.Add(Meaning);}
    Rows.Reset();if(!Objects(NewDefinitions,TEXT("relics"),Rows))return DWLanguageData::Error(Error,TEXT("Invalid relic array."));
    for(const auto& O:Rows)
    {
        FName Relic;bool On=true;if(!Id(O,TEXT("id"),Relic)||!Enabled(O,On))return DWLanguageData::Error(Error,TEXT("Invalid relic identity/enabled flag."));if(!On)continue;
        DWLanguage::FRelicDefinition Definition;TArray<FName> Pool;int32 Count=0;
        if(NewRelics.Contains(Relic)||!Names(O,TEXT("answer_ids"),Definition.AnswerIds)||!Names(O,TEXT("candidate_pool_ids"),Pool)||
            !Integer(O,TEXT("skill_points"),Definition.RewardPoints)||!Integer(O,TEXT("candidate_count"),Count))return DWLanguageData::Error(Error,TEXT("Invalid relic answers, reward or candidate count."));
        for(FName Meaning:Pool){if(!MeaningIds.Contains(Meaning)||Definition.CandidateAnswerIds.Contains(Meaning))return DWLanguageData::Error(Error,TEXT("Unknown/duplicate relic candidate meaning."));Definition.CandidateAnswerIds.Add(Meaning);}
        TSet<FName> Required;for(FName Answer:Definition.AnswerIds){if(!Catalog.Entries.Contains(Answer))return DWLanguageData::Error(Error,TEXT("Relic inscription uses unknown glyph entry."));Required.Add(Answer);}
        if(Count<Required.Num()||Count>Pool.Num())return DWLanguageData::Error(Error,TEXT("Relic pool size cannot include every answer."));
        Catalog.Relics.Add(Relic,MoveTemp(Definition));NewRelics.Add(Relic,O);
    }
    Rows.Reset();if(!Objects(NewDefinitions,TEXT("mechanism_examples"),Rows))return DWLanguageData::Error(Error,TEXT("Invalid mechanism array."));
    TSet<FName> Mechanisms;
    for(const auto& O:Rows)
    {
        FName Mechanism;bool On=true;if(!Id(O,TEXT("id"),Mechanism)||!Enabled(O,On))return DWLanguageData::Error(Error,TEXT("Invalid mechanism identity/enabled flag."));if(!On)continue;
        DWLanguage::FDoorDefinition Door;
        if(Mechanisms.Contains(Mechanism)||!Id(O,TEXT("required_item_id"),Door.RequiredItemId)||
            (!Integer(O,TEXT("required_total"),Door.RequiredCount)&&!Integer(O,TEXT("required_count"),Door.RequiredCount)))return DWLanguageData::Error(Error,TEXT("Invalid mechanism item/count or duplicate ID."));
        if(O->HasField(TEXT("units_per_submission"))&&!Integer(O,TEXT("units_per_submission"),Door.MaxUnitsPerSubmission))return DWLanguageData::Error(Error,TEXT("Invalid offering units per submission."));
        if(O->HasField(TEXT("skill_points"))&&!Integer(O,TEXT("skill_points"),Door.RewardPoints))return DWLanguageData::Error(Error,TEXT("Invalid mechanism point reward."));
        Mechanisms.Add(Mechanism);Catalog.Doors.Add(Mechanism,Door);
    }
    Rows.Reset();if(!Objects(NewTrial,TEXT("talents"),Rows))return DWLanguageData::Error(Error,TEXT("Invalid talent array."));
    for(const auto& O:Rows)
    {
        FName Talent;DWLanguage::FTalentDefinition Definition;FString CostParameter,Mode;
        if(!Id(O,TEXT("id"),Talent)||NewTalents.Contains(Talent)||!O->TryGetBoolField(TEXT("enabled"),Definition.bEnabled)||
            !Names(O,TEXT("prerequisites"),Definition.Prerequisites)||!O->TryGetStringField(TEXT("cost_parameter"),CostParameter)||
            !CountParameter(NewTrial,CostParameter,Definition.Cost))return DWLanguageData::Error(Error,TEXT("Invalid talent identity/cost/prerequisites."));
        if(O->TryGetStringField(TEXT("prerequisites_mode"),Mode)&&Mode!=TEXT("all"))return DWLanguageData::Error(Error,TEXT("Talent prerequisites must use all mode."));
        Catalog.Talents.Add(Talent,Definition);NewTalents.Add(Talent,O);
    }
    if(const auto Trade=Object(NewTrial,TEXT("runtime_trade")))
    {
        FName Input,Output;int32 InputCount=0,OutputCount=0;
        if(!Id(Trade,TEXT("required_item_id"),Input)||!Id(Trade,TEXT("reward_item_id"),Output)||!Catalog.KnownItemIds.Contains(Input)||!Catalog.KnownItemIds.Contains(Output)||
            !Integer(Trade,TEXT("required_count"),InputCount)||!Integer(Trade,TEXT("reward_count"),OutputCount)||InputCount<1||OutputCount<1)
            return DWLanguageData::Error(Error,TEXT("Invalid runtime NPC inventory trade."));
    }
    auto NewModel=MakeUnique<DWLanguage::FModel>();const DWLanguage::FState Existing=Model&&Model->IsReady()?Model->GetState():DWLanguage::FState{};
    if(!NewModel->Initialize(Catalog,Existing,Error))return false;
    if(!DWCharacterA01SaveBridge::BindDefinitions(Catalog,NewTrial,Error))return false;
    // No partial publication if any input/schema/state validation fails.
    Definitions=MoveTemp(NewDefinitions);TrialConfig=MoveTemp(NewTrial);EntryMetadata=MoveTemp(NewEntries);ClueMetadata=MoveTemp(NewClues);
    RelicMetadata=MoveTemp(NewRelics);TalentMetadata=MoveTemp(NewTalents);Model=MoveTemp(NewModel);GlyphCache.Reset();Error.Reset();return true;
}

int64 UDWLanguageSubsystem::GetRevision()const{return Model&&Model->IsReady()?Model->GetState().Revision:0;}
int32 UDWLanguageSubsystem::GetGuessCharacterLimit()const{return Model&&Model->IsReady()?Model->GetCatalog().MaxGuessCharacters:40;}
int32 UDWLanguageSubsystem::GetTalentPoints()const{return IsReady()?Model->GetState().AvailableTalentPoints:0;}
bool UDWLanguageSubsystem::HasTalent(FName Id)const{return Model&&Model->IsReady()&&((IsTrialWorld()&&!FormalSession)||IsFormalSession())&&Model->GetState().LearnedTalentPaidCosts.Contains(Id);}
bool UDWLanguageSubsystem::IsRuntimeConsumable(FName Id)const
{const TSharedPtr<FJsonObject>* Uses=nullptr;const TSharedPtr<FJsonObject>* Row=nullptr;return IsReady()&&TrialConfig&&TrialConfig->TryGetObjectField(TEXT("runtime_consumables"),Uses)&&Uses&&(*Uses)->TryGetObjectField(Id.ToString(),Row)&&Row&&Row->IsValid();}
double UDWLanguageSubsystem::Parameter(const FString& Id,double Fallback)const{return DWLanguageData::Parameter(TrialConfig,Id,Fallback);}

TArray<FDWLanguageEntryView> UDWLanguageSubsystem::GetEntries(EDWLanguageFilter Filter,EDWLanguageSort Sort)const
{
    TArray<FDWLanguageEntryView> Views;if(!IsReady())return Views;
    const auto NativeFilter=Filter==EDWLanguageFilter::Glyph?DWLanguage::EEntryFilter::Singles:(Filter==EDWLanguageFilter::Compound?DWLanguage::EEntryFilter::Compounds:DWLanguage::EEntryFilter::All);
    const auto NativeSort=Sort==EDWLanguageSort::NewestFirst?DWLanguage::EEntrySort::NewestFirst:(Sort==EDWLanguageSort::OldestFirst?DWLanguage::EEntrySort::DiscoveryOrder:DWLanguage::EEntrySort::UnrecordedFirst);
    for(FName Id:Model->BuildEntryOrder(NativeFilter,NativeSort)){FDWLanguageEntryView View;if(GetEntry(Id,View))Views.Add(MoveTemp(View));}return Views;
}
bool UDWLanguageSubsystem::GetEntry(FName Id,FDWLanguageEntryView& Out)const
{
    using namespace DWLanguageData;Out={};if(!IsReady())return false;const auto* Definition=Model->GetCatalog().Entries.Find(Id);if(!Definition)return false;
    Out.EntryId=Id;Out.bCompound=Definition->Kind==DWLanguage::EEntryKind::Compound;const auto Status=Model->GetEntryStatus(Id);
    Out.Status=Status==DWLanguage::EEntryStatus::Undiscovered?EDWLanguageEntryStatus::Undiscovered:(Status==DWLanguage::EEntryStatus::Recorded?EDWLanguageEntryStatus::Recorded:EDWLanguageEntryStatus::Unrecorded);
    if(Status==DWLanguage::EEntryStatus::Undiscovered)return true;
    const auto& State=Model->GetState();const auto& Entry=State.Entries.FindChecked(Id);Out.bCanEdit=true;Out.Guess=Entry.Guess;
    Out.DiscoveryOrder=static_cast<int32>(FMath::Min<int64>(MAX_int32,Entry.FirstDiscoveryOrder));Out.Parts=Definition->Components;
    auto AddClue=[&](FName Clue,FName Displayed,bool FromCompound)
    {
        const auto* Raw=ClueMetadata.Find(Clue);if(!Raw)return;FDWLanguageClueView View;View.ClueId=Clue;View.EntryId=Id;View.DisplayedEntryId=Displayed;View.bFromCompound=FromCompound;
        View.Location=Localized(this,*Raw,TEXT("location_name"));View.Carrier=Localized(this,*Raw,TEXT("carrier_name"));View.Description=Localized(this,*Raw,TEXT("description"));
        if(FromCompound)View.Description=FText::Format(DWText(this,TEXT("出现在组合 {0} 中。{1}"),TEXT("Appears in compound {0}. {1}")),FText::FromName(Displayed),View.Description);
        Out.Clues.Add(MoveTemp(View));
    };
    for(FName Clue:Entry.ClueIds)AddClue(Clue,Id,false);
    for(FName Word:Entry.AppearedInCompounds)if(const auto* Source=State.Entries.Find(Word))for(FName Clue:Source->ClueIds)AddClue(Clue,Word,true);
    const auto Ordered=Model->BuildEntryOrder(DWLanguage::EEntryFilter::Compounds,DWLanguage::EEntrySort::DiscoveryOrder);
    for(FName Word:Ordered)
    {
        if(Word==Id||!State.Entries.Contains(Word))continue;const auto& Parts=Model->GetCatalog().Entries.FindChecked(Word).Components;
        bool Related=Parts.Contains(Id);if(Out.bCompound)for(FName Part:Definition->Components)Related|=Parts.Contains(Part);
        if(Related)Out.RelatedEntries.Add(Word);
    }
    return true;
}
UTexture2D* UDWLanguageSubsystem::GetGlyphTexture(FName Id)const
{
    if(!EntryMetadata.Contains(Id))return nullptr;if(const auto* Cached=GlyphCache.Find(Id))return Cached->Get();
    const auto& Metadata=EntryMetadata.FindChecked(Id);FString Path=DWLanguageData::Text(Metadata,TEXT("texture_path"));
    if(Path.IsEmpty())Path=FString::Printf(TEXT("/Game/DoughWorld/UI/Image/T_Luwa_%s.T_Luwa_%s"),*Id.ToString(),*Id.ToString());
    UTexture2D* Texture=LoadObject<UTexture2D>(nullptr,*Path);if(Texture)const_cast<UDWLanguageSubsystem*>(this)->GlyphCache.Add(Id,Texture);return Texture;
}
bool UDWLanguageSubsystem::GetRelic(FName Id,FDWRelicPuzzleView& Out)const
{
    using namespace DWLanguageData;Out={};if(!IsReady())return false;const auto* Definition=Model->GetCatalog().Relics.Find(Id);const auto* Raw=RelicMetadata.Find(Id);if(!Definition||!Raw)return false;
    Out.RelicId=Id;Out.bSolved=Model->GetState().CompletedRelics.Contains(Id);Out.GlyphEntries=Definition->AnswerIds;Out.RewardPoints=Definition->RewardPoints;
    Out.Title=Localized(this,*Raw,TEXT("display_name"),Id.ToString());if(Out.bSolved)
    {
        Out.Story=Localized(this,*Raw,TEXT("story"));const auto Story=Object(*Raw,TEXT("story"));
        Out.StoryChinese=FText::FromString(Text(Story,TEXT("zh")));Out.StoryEnglish=FText::FromString(Text(Story,TEXT("en")));
        Out.RewardPoints=Model->GetState().CompletedRelics.FindChecked(Id).AwardedPoints;
    }
    TArray<FName> Pool;Names(*Raw,TEXT("candidate_pool_ids"),Pool);int32 Count=Pool.Num();Integer(*Raw,TEXT("candidate_count"),Count);
    TArray<FName> Selected,Extra;for(FName Answer:Definition->AnswerIds)Selected.AddUnique(Answer);for(FName Candidate:Pool)if(!Selected.Contains(Candidate))Extra.Add(Candidate);
    FRandomStream Random(static_cast<int32>(GetTypeHash(PlaythroughId+TEXT("/")+Id.ToString())));
    auto Shuffle=[&](TArray<FName>& Array){for(int32 I=Array.Num()-1;I>0;--I)Array.Swap(I,Random.RandRange(0,I));};Shuffle(Extra);
    for(FName Candidate:Extra){if(Selected.Num()>=Count)break;Selected.Add(Candidate);}Shuffle(Selected);
    TArray<FObject> Meanings;Objects(Definitions,TEXT("candidate_meanings"),Meanings);const bool English=UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English;
    for(FName Candidate:Selected)for(const auto& Meaning:Meanings)
    {FName MeaningId;if(DWLanguageData::Id(Meaning,TEXT("id"),MeaningId)&&MeaningId==Candidate){FDWLanguageMeaningView View;View.MeaningId=Candidate;View.Label=FText::FromString(Text(Meaning,English?TEXT("en"):TEXT("zh")));Out.Candidates.Add(MoveTemp(View));break;}}
    return Out.Candidates.Num()==Selected.Num();
}
bool UDWLanguageSubsystem::GetMechanism(FName Id,FDWLanguageMechanismView& Out)const
{
    using namespace DWLanguageData;Out={};if(!IsReady())return false;
    if(const auto* Door=Model->GetCatalog().Doors.Find(Id))
    {
        Out.MechanismId=Id;Out.RequiredItemId=Door->RequiredItemId;Out.RequiredCount=Door->RequiredCount;Out.UnitsPerSubmission=Door->MaxUnitsPerSubmission;
        Out.Title=DWText(this,TEXT("铭文洞口"),TEXT("Inscribed opening"));if(const auto* Progress=Model->GetState().DoorProgress.Find(Id)){Out.SubmittedCount=Progress->AcceptedCount;Out.bComplete=Progress->bCompleted;}
        TArray<FObject> Rows;Objects(Definitions,TEXT("mechanism_examples"),Rows);
        for(const auto& Raw:Rows){FName Found;if(DWLanguageData::Id(Raw,TEXT("id"),Found)&&Found==Id){Names(Raw,TEXT("parts"),Out.GlyphEntries);Names(Raw,TEXT("reward_glyph_entries"),Out.RewardGlyphEntries);Out.Title=Localized(this,Raw,TEXT("display_name"),Out.Title.ToString());DWLanguageData::Id(Raw,TEXT("reward_item_id"),Out.RewardItemId);Integer(Raw,TEXT("reward_count"),Out.RewardCount);break;}}
        if(Out.bComplete)Out.Reason=DWText(this,TEXT("机关已完成，不再接收物品"),TEXT("Completed; no further items are accepted."));return true;
    }
    const auto Trade=Object(TrialConfig,TEXT("runtime_trade"));FName TradeId(TEXT("LanguageTrade_Test01"));if(Trade)DWLanguageData::Id(Trade,TEXT("id"),TradeId);
    if(!Trade||Id!=TradeId)return false;
    Out.MechanismId=Id;Out.bTrade=true;DWLanguageData::Id(Trade,TEXT("required_item_id"),Out.RequiredItemId);DWLanguageData::Id(Trade,TEXT("reward_item_id"),Out.RewardItemId);
    Integer(Trade,TEXT("required_count"),Out.RequiredCount);Integer(Trade,TEXT("reward_count"),Out.RewardCount);Names(Trade,TEXT("glyph_entries"),Out.GlyphEntries);Names(Trade,TEXT("reward_glyph_entries"),Out.RewardGlyphEntries);
    Out.Title=Localized(this,Trade,TEXT("display_name"),DWText(this,TEXT("补给交换"),TEXT("Supply trade")).ToString());return true;
}
TArray<FDWTalentNodeView> UDWLanguageSubsystem::GetTalents()const
{
    using namespace DWLanguageData;TArray<FDWTalentNodeView> Out;if(!IsReady())return Out;
    TArray<FObject> Rows;if(!Objects(TrialConfig,TEXT("talents"),Rows))return Out;
    for(const auto& Raw:Rows)
    {
        FName Talent;if(!DWLanguageData::Id(Raw,TEXT("id"),Talent))continue;const auto* Definition=Model->GetCatalog().Talents.Find(Talent);if(!Definition)continue;
        FDWTalentNodeView View;View.TalentId=Talent;View.Title=Localized(this,Raw,TEXT("display_name"),Talent.ToString());View.Description=TalentDescription(this,Talent,Raw);
        View.Prerequisites=Definition->Prerequisites;View.Cost=Definition->Cost;View.bConfigured=Definition->bEnabled&&Talent!=TEXT("a5")&&Talent!=TEXT("a6");
        if(const auto* Paid=Model->GetState().LearnedTalentPaidCosts.Find(Talent)){View.bLearned=true;View.PaidCost=*Paid;}
        DWLanguage::FPreparedChange Plan;DWLanguage::FOperationResult LearnResult,ForgetResult;
        View.bCanLearn=Model->PrepareLearnTalent(Talent,Plan,LearnResult);View.bCanForget=Model->PrepareForgetTalent(Talent,Plan,ForgetResult);
        View.Reason=Reason(this,View.bLearned?ForgetResult.Code:LearnResult.Code);
        const auto* P=ActivePlayer.Get();const auto* A01=P?P->CharacterA01.Get():nullptr;
        if(!A01||!A01->IsEnabled()){View.bCanLearn=View.bCanForget=false;View.Reason=DWText(this,TEXT("角色状态尚未就绪"),TEXT("Character state is not ready."));}
        else if(Talent==TEXT("b3")&&View.bLearned&&A01->IsDurationTalentContributing())
        {View.bCanForget=false;View.Reason=DWText(this,TEXT("本次接种使用了该加时，结束形态后可遗忘"),TEXT("The current inoculation uses this bonus; finish the form before forgetting it."));}
        else if(Talent==TEXT("a1")&&View.bLearned&&!A01->CanForgetExtraShortcut())
        {View.bCanForget=false;View.Reason=DWText(this,TEXT("第四快捷格装备状态暂不可变更"),TEXT("The fourth-slot equipment state cannot change right now."));}
        Out.Add(MoveTemp(View));
    }
    return Out;
}
