#pragma once
#include "CoreMinimal.h"
class UDWGameplayConfig;
struct FDWEditorAuthoringFileDigest
{
 FString Filename,Digest;
 int64 Size=0;
 FDateTime Modified;
};
struct FDWEditorAuthoringSignature
{
 FString Digest,ManifestJson;
 TMap<FString,FDWEditorAuthoringFileDigest> Files;
 static bool Capture(const FString& EntryPackage,const UDWGameplayConfig&,FDWEditorAuthoringSignature&,FString& Error,
     const FDWEditorAuthoringSignature* Cache=nullptr,const TSet<FString>* InvalidatedPackages=nullptr);
};
