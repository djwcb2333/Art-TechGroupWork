#include "DWLanguageSubsystem.h"
#include "DWLanguageModel.h"
#include "DWCombatEquipment.h"
#include "DWLanguageCombatArena.h"
#include "EngineUtils.h"
#include "DWCombatWorldPersistence.h"
#include "DWPlayerCharacter.h"
#include "DWSaveGame.h"
#include "DWSaveStorage.h"
#include "DWLocalizationLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

namespace DWLanguageSave
{
bool Path(FString& Out,FString& Error)
{
 FString Name=TEXT("Trial_Current");FString Requested;
 if(FParse::Value(FCommandLine::Get(),TEXT("DWLanguageSaveNamespace="),Requested))
 {
  if(!Requested.StartsWith(TEXT("DW_QA_"))||Requested.Len()>80){Error=TEXT("Invalid isolated language verification namespace.");return false;}
  for(TCHAR C:Requested)if(!FChar::IsAlnum(C)&&C!=TEXT('_')){Error=TEXT("Invalid isolated verification filename.");return false;}
  Name=Requested;
 }
 FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("LanguagePlaytest"));FPaths::NormalizeDirectoryName(Directory);
 Out=Directory/(Name+TEXT(".sav"));return true;
}
}
bool UDWLanguageSubsystem::SaveTrial(FText& Message)
{
 if(!IsTrialWorld()||IsFormalSession()||!IsReady()||bChanging||!ActivePlayer.IsValid()||!GetStarterGrantState().bGranted)
 {Message=DWText(this,TEXT("试玩尚未完成初始化，未保存"),TEXT("The trial is not fully initialized; nothing was saved."));return false;}
 for(TActorIterator<ADWLanguageCombatArena> It(GetWorld());It;++It)if(It->IsTrainingActive())
 {Message=DWText(this,TEXT("请先从控制台退出隔离训练，再保存试玩。"),TEXT("Exit isolated training at its console before saving the trial."));return false;}
 TGuardValue<bool> Guard(bChanging,true);auto* Save=NewObject<UDWSaveGame>();FString Error,Filename;
 if(!FDWCombatWorldPersistence::Capture(*ActivePlayer.Get(),*Save,Error)||Save->LanguageTrialSchema!=1||
  Save->LanguageProgressJson.IsEmpty()||Save->A01StateJson.IsEmpty())
 {Message=FText::FromString(Error.IsEmpty()?TEXT("完整语言/角色快照尚不可保存；未写入任何文件。"):Error);return false;}
 TArray<uint8> Bytes;if(!UGameplayStatics::SaveGameToMemory(Save,Bytes)||Bytes.IsEmpty()||!DWLanguageSave::Path(Filename,Error))
 {Message=DWText(this,TEXT("试玩序列化失败，未保存"),TEXT("Trial serialization failed; nothing was saved."));return false;}
 if(!DWSaveStorage::WriteBytes(Filename,Bytes))
 {Message=DWText(this,TEXT("独立试玩文件写入失败；原试玩记录保留"),TEXT("Writing the isolated trial file failed; the previous record was retained."));return false;}
 Message=DWText(this,TEXT("试玩已保存（独立于冒险存档）"),TEXT("Trial saved separately from adventure saves."));return true;
}
bool UDWLanguageSubsystem::LoadTrial(FText& Message)
{
 if(!IsTrialWorld()||IsFormalSession()||!IsReady()||bChanging||!ActivePlayer.IsValid())
 {Message=DWText(this,TEXT("请先进入语言试玩地图"),TEXT("Enter the language playtest map first."));return false;}
 FString Error,Filename;if(!DWLanguageSave::Path(Filename,Error)){Message=FText::FromString(Error);return false;}
 const int64 Size=IFileManager::Get().FileSize(*Filename);
 if(Size<=0||Size>64*1024*1024){Message=DWText(this,TEXT("尚无有效的独立试玩记录"),TEXT("No valid isolated trial record is available."));return false;}
 TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Filename))
 {Message=DWText(this,TEXT("试玩文件读取失败，当前进度不变"),TEXT("The trial file could not be read; current progress is unchanged."));return false;}
 auto* Save=Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
 if(!Save||Save->LanguageTrialSchema!=1||Save->MapPackage!=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest"))
 {Message=DWText(this,TEXT("文件不属于此语言试玩，当前进度不变"),TEXT("This file is not a language trial; current progress is unchanged."));return false;}
 // World persistence validates and prepares every participant before the one native adoption barrier.
 // Do not hold bChanging: the prepared language participant must be admitted by the aggregate restore.
 bool Committed=false;
 if(!FDWCombatWorldPersistence::Restore(*ActivePlayer.Get(),*Save,Committed,Error))
 {Message=FText::FromString((Committed?TEXT("试玩数据已采用，但表现刷新未完成："):TEXT("试玩未载入，当前进度不变："))+Error);return false;}
 Message=DWText(this,TEXT("独立试玩已载入"),TEXT("Isolated trial loaded."));return true;
}
