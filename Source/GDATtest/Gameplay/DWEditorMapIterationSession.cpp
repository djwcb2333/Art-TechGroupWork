#include "DWEditorMapIterationSession.h"
#include "DWEditorAuthoringSignature.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWSaveSettings.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#if WITH_EDITOR
#include "Editor.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"
#endif
#if WITH_EDITOR && PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#endif
bool FDWEditorMapIterationSession::ModeAllowed(bool Editor,bool PIE,bool Commandlet,bool QA,bool Enabled)
{return Editor&&PIE&&!Commandlet&&!QA&&Enabled;}
bool FDWEditorMapIterationSession::ShouldUse(const UDWGameInstance& GI,bool QA)
{
#if WITH_EDITOR
 return ModeAllowed(true,GI.GetWorld()&&GI.GetWorld()->WorldType==EWorldType::PIE,IsRunningCommandlet(),QA,GetDefault<UDWSaveSettings>()->bUseEditorMapIterationSaves);
#else
 return false;
#endif
}
FDWEditorMapIterationSession::~FDWEditorMapIterationSession()
{
#if WITH_EDITOR
 if(PreSaveHandle.IsValid())FCoreUObjectDelegates::OnObjectPreSave.Remove(PreSaveHandle);
#endif
#if WITH_EDITOR && PLATFORM_WINDOWS
 if(Lease)CloseHandle(static_cast<HANDLE>(Lease));
#endif
}
TSharedPtr<FDWEditorMapIterationSession> FDWEditorMapIterationSession::Start(const UDWGameInstance& GI,const UDWGameplayConfig& C,FString& Error)
{
#if WITH_EDITOR && PLATFORM_WINDOWS
 if(!GI.GetWorld()||GI.GetWorld()->WorldType!=EWorldType::PIE||!GEditor||IsRunningCommandlet())
 {Error=TEXT("Editor iteration requires a PIE context; no formal storage fallback.");return nullptr;}
 auto* AuthorWorld=GEditor->GetEditorWorldContext().World();
 if(!AuthorWorld||AuthorWorld->GetOutermost()->GetName().StartsWith(TEXT("/Temp/")))
 {Error=TEXT("请保存地图后再试玩：无法识别已保存的编辑器入口地图。");return nullptr;}
 auto Session=TSharedPtr<FDWEditorMapIterationSession>(new FDWEditorMapIterationSession());
 Session->EntryPackage=AuthorWorld->GetOutermost()->GetName();Session->Config=&C;
 FDWEditorAuthoringSignature Signature;if(!FDWEditorAuthoringSignature::Capture(Session->EntryPackage,C,Signature,Error))return nullptr;
 FString Project=FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()).ToLower();FPaths::NormalizeFilename(Project);
 FTCHARToUTF8 Utf8(*Project);uint8 Hash[20];FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Hash);const FString ProjectId=BytesToHex(Hash,20);
 const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("EditorMapIterations")/ProjectId);
 if(!IFileManager::Get().MakeDirectory(*Root,true)){Error=TEXT("Cannot create isolated editor iteration root; no formal fallback.");return nullptr;}
 HANDLE Handle=CreateFileW(*(Root/TEXT(".writer.lock")),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(Handle==INVALID_HANDLE_VALUE){Error=TEXT("本测试轮已有写入会话，请先结束另一轮 PIE；未读取正式存档。");return nullptr;}
 Session->Lease=Handle;
 FString Context;
 if(!DWEditorIterationRoundStore::MakeContextKey(Session->EntryPackage,C.GetPathName(),Context,Error))return nullptr;
 if(!DWEditorIterationRoundStore::Select(Root,ProjectId,Context,Signature,Session->Bound,Error))return nullptr;
 Session->SignatureCache=MoveTemp(Signature);
 const TWeakPtr<FDWEditorMapIterationSession> Weak=Session;
 Session->PreSaveHandle=FCoreUObjectDelegates::OnObjectPreSave.AddLambda([Weak](UObject* Object,FObjectPreSaveContext)
 {
  const auto P=Weak.Pin();if(!P||!Object)return;
  const FString Name=Object->GetOutermost()->GetName();
  if(P->SignatureCache.Files.Contains(Name))P->InvalidatedPackages.Add(Name);
 });
 UE_LOG(LogTemp,Display,TEXT("DW_EDITOR_ITERATION_BOUND identity=%s directory=%s context=%s entry=%s config=%s"),*Session->Identity(),*Session->Directory(),*Session->Bound.ContextKey,*Session->EntryPackage,*C.GetPathName());
 return Session;
#else
 Error=TEXT("Editor iteration storage is unavailable for this executable; no fallback.");return nullptr;
#endif
}
bool FDWEditorMapIterationSession::CheckCurrent(FString& Error)const
{
#if WITH_EDITOR
 if(!VersionFailure.IsEmpty()){Error=VersionFailure;return false;}
 if(!Config.IsValid()){Error=TEXT("Editor authoring configuration ended; test save preserved.");return false;}
 FDWEditorAuthoringSignature Current;
 if(!FDWEditorAuthoringSignature::Capture(EntryPackage,*Config.Get(),Current,Error,&SignatureCache,&InvalidatedPackages))return false;
 if(Current.Digest!=Bound.Signature)
 {VersionFailure=TEXT("作者地图或规则已改变，请结束当前 PIE、保存地图后再试玩；本轮存档未写入，旧轮保留。");Error=VersionFailure;return false;}
 SignatureCache=MoveTemp(Current);InvalidatedPackages.Reset();
 Error.Reset();return true;
#else
 Error=TEXT("Editor save context cannot be used in a packaged game.");return false;
#endif
}
