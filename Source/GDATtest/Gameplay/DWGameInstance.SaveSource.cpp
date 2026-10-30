#include "DWGameInstance.h"
#include "DWSaveGame.h"
#include "DWSaveStorage.h"
#include "DWCombatEquipment.h"
#include "DWFormalLanguagePersistence.h"
#include "DWLanguageSubsystem.h"
#include "DWOpeningMapCompatibility.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

// Native session provenance, never serialized and never replaced by a list/other-slot read.
// Exact admitted bytes also identify Flow0, which deliberately has no playthrough GUID.
struct FDWActiveSlotSource final
{
 FString Slot,MapPackage,StorageIdentity;
 int32 Version=0;
 FDWAdventureFlowSave Flow;
 TArray<uint8> Bytes;
 FDWActiveSlotSource(const FString& InSlot,const UDWSaveGame& Save,TArray<uint8>&& InBytes,const FString& Storage)
  :Slot(InSlot),MapPackage(Save.MapPackage),StorageIdentity(Storage),Version(Save.Version),Flow(Save.AdventureFlow),Bytes(MoveTemp(InBytes)){}
 bool MatchesStorage(const FString& Storage)const{return StorageIdentity==Storage;}
 bool Matches(const UDWSaveGame& Save)const
 {
  return Version==Save.Version&&MapPackage==Save.MapPackage&&
   FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&Flow,&Save.AdventureFlow,PPF_None);
 }
};

namespace DWSlotSource
{
bool Fail(FString& Error,const TCHAR* Reason){Error=Reason;return false;}
bool SameFlow(const FDWAdventureFlowSave& A,const FDWAdventureFlowSave& B)
{return FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&A,&B,PPF_None);}
}

UDWSaveGame* UDWGameInstance::ReadSlotWithSource(const FString& Slot,TSharedPtr<const FDWActiveSlotSource>& Out,FString& Error)const
{
 Out.Reset();if(!CheckSaveStorageContext(Error))return nullptr;TArray<uint8> Bytes;
 const bool Read=bUseDocumentsStorage?
  !SaveDirectory.IsEmpty()&&FFileHelper::LoadFileToArray(Bytes,*(SaveDirectory/(Slot+TEXT(".sav")))):
  UGameplayStatics::LoadDataFromSlot(Bytes,Slot,0);
 if(!Read||Bytes.IsEmpty())
 {Error=TEXT("The existing slot could not be read; its source was not replaced.");return nullptr;}
 TStrongObjectPtr<UDWSaveGame> Raw(Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)));
 if(!Raw.Get()){Error=TEXT("The existing slot could not be decoded; its source was not replaced.");return nullptr;}
 UDWSaveGame* Saved=Raw.Get();
 if(DWOpeningMapCompatibility::NeedsReadProjection(*Saved))
 {
  if(!CombatRules){Error=TEXT("Opening alias requires valid combat rules.");return nullptr;}
  auto* Definitions=GetSubsystem<UDWLanguageSubsystem>();UDWSaveGame* Projected=nullptr;
  const auto Result=DWOpeningMapCompatibility::BuildReadCopy(*Saved,*CombatRules,
   Definitions&&Definitions->GetNativeModel()?&Definitions->GetNativeModel()->GetCatalog():nullptr,
   Definitions?Definitions->GetTrialConfig():TSharedPtr<FJsonObject>(),Projected,Error);
  if(Result==DWOpeningMapCompatibility::EReadProjection::Rejected)return nullptr;
  if(Result==DWOpeningMapCompatibility::EReadProjection::Projected)Saved=Projected;
 }
 if(!Saved){Error=TEXT("Opening alias did not return a readable slot.");return nullptr;}
 Out=MakeShared<FDWActiveSlotSource>(Slot,*Saved,MoveTemp(Bytes),GetSaveStorageIdentity());Error.Reset();return Saved;
}

USaveGame* UDWGameInstance::ReadSlot(const FString& Slot)const
{
 TSharedPtr<const FDWActiveSlotSource> Unadmitted;FString Error;
 auto* Saved=ReadSlotWithSource(Slot,Unadmitted,Error);
 if(!Saved)UE_LOG(LogTemp,Warning,TEXT("DW_SLOT_READ_REJECTED: %s"),*Error);
 return Saved; // Read-only callers never change ActiveSlotSource.
}

UDWSaveGame* UDWGameInstance::ReadSlotMatchingSource(int32 SlotIndex,const TSharedPtr<const FDWActiveSlotSource>& Expected,FString& Error)const
{
 if(!IsValidSlot(SlotIndex)||!Expected||Expected->StorageIdentity!=GetSaveStorageIdentity()||Expected->Slot!=MakeSlotName(SlotIndex))
 {Error=TEXT("There is no admitted persistent source for this slot.");return nullptr;}
 TSharedPtr<const FDWActiveSlotSource> Observed;
 TStrongObjectPtr<UDWSaveGame> Saved(ReadSlotWithSource(Expected->Slot,Observed,Error));
 if(!Saved.Get()||!Observed)return nullptr;
 if(Observed->Bytes!=Expected->Bytes||!Expected->Matches(*Saved))
 {Error=TEXT("The slot file or its complete owner/phase changed; the file was preserved.");return nullptr;}
 Error.Reset();return Saved.Get();
}

bool UDWGameInstance::HasActiveSlotSourceOwner()const
{
 return HasActiveSlot()&&ActiveSlotSource&&ActiveSlotSource->StorageIdentity==GetSaveStorageIdentity()&&ActiveSlotSource->Slot==MakeSlotName(ActiveSlot)&&
  ActiveSlotSource->Version==ActiveSaveVersion&&DWSlotSource::SameFlow(ActiveSlotSource->Flow,AdventureFlow);
}

UDWSaveGame* UDWGameInstance::ReadActiveSlotForCapture(TSharedPtr<const FDWActiveSlotSource>& Out,FString& Error)
{
 Out.Reset();
 if(!HasActiveSlotSourceOwner())
 {Error=TEXT("The active slot has no matching admitted owner/phase source.");return nullptr;}
 const auto Expected=ActiveSlotSource;const int32 Slot=ActiveSlot;const uint64 Generation=CombatSaveGeneration;
 TStrongObjectPtr<UDWSaveGame> Saved(ReadSlotMatchingSource(Slot,Expected,Error));
 if(!Saved.Get()||!ValidateSave(Saved.Get(),Slot))
 {if(Error.IsEmpty())Error=LastSaveError.ToString();return nullptr;}
 if(ActiveSlot!=Slot||CombatSaveGeneration!=Generation||ActiveSlotSource!=Expected||!HasActiveSlotSourceOwner())
 {Error=TEXT("The active source owner changed during validation.");return nullptr;}
 Out=Expected;Error.Reset();return Saved.Get();
}

bool UDWGameInstance::SetAdventureFlowOwner(const UDWSaveGame* Save,const TSharedPtr<const FDWActiveSlotSource>& Source)
{
 // A null Save with a known source is only the previous-owner rollback. It
 // restores admission metadata, not gameplay facts and never rereads/rebuilds a file.
 if((Save&&!Source)||(Source&&(Source->StorageIdentity!=GetSaveStorageIdentity()||!HasActiveSlot()||Source->Slot!=MakeSlotName(ActiveSlot)||(Save&&!Source->Matches(*Save)))))return false;
 FDWFormalLanguagePersistence::Suspend(*this);
 ActiveSlotSource=Source;ActiveSaveVersion=Source?Source->Version:0;
 AdventureFlow=Source?Source->Flow:FDWAdventureFlowSave{};FormalPlaythroughId.Reset();FormalStarterGrant.Reset();
 bOpeningStartupReady=false;OpeningDirector.Reset();OpeningStartupPlayer.Reset();OpeningStartupWorld.Reset();OpeningStartupLife={};
 OpeningStartupEpoch=OpeningStartupGeneration=0;OpeningStartupSlot=INDEX_NONE;
 if(AdventureFlow.Schema==1)
 {FormalPlaythroughId=AdventureFlow.PlaythroughId;FormalStarterGrant=MakeShared<FDWCombatStarterGrantState>();FormalStarterGrant->PlaythroughId=FormalPlaythroughId;FormalStarterGrant->bGranted=AdventureFlow.bStarterGranted;}
 return true;
}

bool UDWGameInstance::WriteNewSlot(UDWSaveGame& Save,int32 SlotIndex,TSharedPtr<const FDWActiveSlotSource>& Out,FString& Error)const
{
 Out.Reset();if(!CheckSaveStorageContext(Error))return false;
 if(!IsValidSlot(SlotIndex)||SaveDirectory.IsEmpty()||SlotExists(MakeSlotName(SlotIndex)))
  return DWSlotSource::Fail(Error,TEXT("New-game bootstrap requires a still-empty slot; nothing was overwritten."));
 TArray<uint8> Bytes;if(!UGameplayStatics::SaveGameToMemory(&Save,Bytes)||Bytes.IsEmpty())
  return DWSlotSource::Fail(Error,TEXT("New-game serialization failed."));
 if(!CheckSaveStorageContext(Error))return false;
 // No gameplay callback between the last empty check and the existing atomic file writer.
 if(SlotExists(MakeSlotName(SlotIndex))||!DWSaveStorage::WriteBytes(SaveDirectory/(MakeSlotName(SlotIndex)+TEXT(".sav")),Bytes))
  return DWSlotSource::Fail(Error,TEXT("Writing the new slot failed; no existing slot was admitted as its source."));
 Out=MakeShared<FDWActiveSlotSource>(MakeSlotName(SlotIndex),Save,MoveTemp(Bytes),GetSaveStorageIdentity());Error.Reset();return true;
}

bool UDWGameInstance::CommitActiveSlotSave(UDWSaveGame& Save,const TSharedPtr<const FDWActiveSlotSource>& Expected,
 const FDWAdventureFlowSave& ExpectedOwnerFlow,FString& Error)
{
 const int32 Slot=ActiveSlot;const uint64 Generation=CombatSaveGeneration;
 const auto Current=[&](){return bCombatSaveAlive&&IsValidSlot(Slot)&&ActiveSlot==Slot&&CombatSaveGeneration==Generation&&
  Expected&&ActiveSlotSource==Expected&&Expected->StorageIdentity==GetSaveStorageIdentity()&&Expected->Slot==MakeSlotName(Slot)&&Expected->Version==ActiveSaveVersion&&
  Save.Version==ActiveSaveVersion&&DWSlotSource::SameFlow(AdventureFlow,ExpectedOwnerFlow);};
 if(!Current())return DWSlotSource::Fail(Error,TEXT("The save source owner/version changed before commit."));
 TArray<uint8> Bytes;if(!UGameplayStatics::SaveGameToMemory(&Save,Bytes)||Bytes.IsEmpty())
  return DWSlotSource::Fail(Error,TEXT("Active-slot serialization failed; the source file was preserved."));
 // Serialize first, then re-read exactly the admitted source immediately before
 // replacement. Alias/decode/I/O failure is a rejection, never a missing-file bootstrap.
 TStrongObjectPtr<UDWSaveGame> Disk(ReadSlotMatchingSource(Slot,Expected,Error));
 if(!Disk.Get()||!ValidateSave(Disk.Get(),Slot)||!Current())
 {if(Error.IsEmpty())Error=LastSaveError.IsEmpty()?TEXT("The persistent source changed before commit."):LastSaveError.ToString();return false;}
 if(!CheckSaveStorageContext(Error))return false;
 if(!DWSaveStorage::WriteBytes(SaveDirectory/(Expected->Slot+TEXT(".sav")),Bytes))
  return DWSlotSource::Fail(Error,TEXT("Active-slot write failed; the admitted source token was retained."));
 // Existing atomic writer has no gameplay callback. Update only this same native
 // owner after a successful write; no scene/model/grant state is adopted here.
 ActiveSlotSource=MakeShared<FDWActiveSlotSource>(Expected->Slot,Save,MoveTemp(Bytes),GetSaveStorageIdentity());Error.Reset();return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEditorIterationSourceIdentityTest,"DoughWorld.EditorIteration.SourceProvenance",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEditorIterationSourceIdentityTest::RunTest(const FString&)
{
 TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());
 TArray<uint8> Bytes;Bytes.Add(10);Bytes.Add(20);const TArray<uint8> Original=Bytes;
 FDWActiveSlotSource Token(TEXT("SameSlot"),*Save,MoveTemp(Bytes),TEXT("Project:RoundA:SignatureA"));
 TestTrue(TEXT("Admitted source preserves exact bytes"),Token.Bytes==Original);
 TestTrue(TEXT("Source matches the same storage context"),Token.MatchesStorage(TEXT("Project:RoundA:SignatureA")));
 TestFalse(TEXT("Identical slot metadata cannot cross editor rounds"),Token.MatchesStorage(TEXT("Project:RoundB:SignatureA")));
 TestTrue(TEXT("Existing complete owner metadata validation is retained"),Token.Matches(*Save));
 const FString OriginalMap=Save->MapPackage;const int32 OriginalVersion=Save->Version;const auto OriginalFlow=Save->AdventureFlow;
 Save->MapPackage=TEXT("/Game/ChangedMap");TestFalse(TEXT("Changed owner map remains rejected"),Token.Matches(*Save));Save->MapPackage=OriginalMap;
 Save->Version=OriginalVersion==4?5:4;TestFalse(TEXT("Changed source version remains rejected"),Token.Matches(*Save));Save->Version=OriginalVersion;
 Save->AdventureFlow.PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 TestFalse(TEXT("Changed playthrough owner remains rejected"),Token.Matches(*Save));Save->AdventureFlow=OriginalFlow;
 Save->AdventureFlow.Phase=OriginalFlow.Phase==EDWAdventureOpeningPhase::Pending?EDWAdventureOpeningPhase::Completed:EDWAdventureOpeningPhase::Pending;
 TestFalse(TEXT("Changed source phase remains rejected"),Token.Matches(*Save));Save->AdventureFlow=OriginalFlow;
 TestTrue(TEXT("Restored complete owner facts match again"),Token.Matches(*Save));
 return true;
}
#endif
