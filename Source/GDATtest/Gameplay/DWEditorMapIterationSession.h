#pragma once
#include "CoreMinimal.h"
#include "DWEditorIterationRoundStore.h"
#include "DWEditorAuthoringSignature.h"
class UDWGameInstance;
class UDWGameplayConfig;
/** Immutable storage binding for one PIE GI; a changed authoring version rejects rather than switches. */
class FDWEditorMapIterationSession
{
public:
 ~FDWEditorMapIterationSession();
 static bool ShouldUse(const UDWGameInstance&,bool ExplicitQA);
 static bool ModeAllowed(bool EditorBuild,bool PIE,bool Commandlet,bool ExplicitQA,bool Enabled);
 static TSharedPtr<FDWEditorMapIterationSession> Start(const UDWGameInstance&,const UDWGameplayConfig&,FString& Error);
 bool CheckCurrent(FString& Error)const;
 const FString& Directory()const{return Bound.Directory;}
 const FString& Identity()const{return Bound.StorageIdentity;}
private:
 FDWEditorMapIterationSession()=default;
 FString EntryPackage;
 TWeakObjectPtr<const UDWGameplayConfig> Config;
 FDWEditorIterationRound Bound;
 mutable FDWEditorAuthoringSignature SignatureCache;
 mutable TSet<FString> InvalidatedPackages;
 mutable FString VersionFailure;
 FDelegateHandle PreSaveHandle;
 void* Lease=nullptr;
};
