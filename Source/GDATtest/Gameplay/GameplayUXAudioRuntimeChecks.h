#pragma once

// Development QA helper. Only the parent runner includes this file; it is not a gameplay system.
// Preferences are temporarily changed without saving, then restored by Finish on the game thread.
#include "DWUserSettings.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Templates/Atomic.h"
#include "Templates/SharedPointer.h"

namespace DWGameplayUXAudioQA
{
static FString Normalize(FString Path)
{
    Path=FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

static bool IsInside(const FString& Path,FString Directory)
{
    Directory=Normalize(Directory);
    if(!Directory.EndsWith(TEXT("/")))Directory+=TEXT("/");
    return Path.StartsWith(Directory,ESearchCase::IgnoreCase);
}

static bool IsIsolated(FString& Reason,FString* ActualINI=nullptr)
{
    if(ActualINI)ActualINI->Reset();
    FString RequestedINI;
    if(!FParse::Param(FCommandLine::Get(),TEXT("DWGameplayUXQA"))
        ||!FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),RequestedINI)
        ||RequestedINI.IsEmpty()||FPaths::IsRelative(RequestedINI))
    {
        Reason=TEXT("Missing explicit QA flag or absolute GameUserSettingsINI override; no preferences changed.");
        return false;
    }
    // UE 5.8 may use a logical config name here rather than its physical INI filename.
    // Validate the loaded branch's destination; never reinterpret the logical key as a path.
    const FConfigFile* ActiveConfig=GConfig?GConfig->FindConfigFile(GGameUserSettingsIni):nullptr;
    if(!ActiveConfig||!ActiveConfig->Branch||ActiveConfig->Branch->IniPath.IsEmpty()
        ||FPaths::IsRelative(ActiveConfig->Branch->IniPath))
    {
        Reason=TEXT("Active GameUserSettings config has no absolute physical branch INI path; no preferences changed.");
        return false;
    }
    const FString INI=Normalize(RequestedINI);
    const FString ActiveINI=Normalize(ActiveConfig->Branch->IniPath);
    const FString Name=FPaths::GetCleanFilename(INI);
    if(!INI.Equals(ActiveINI,ESearchCase::IgnoreCase)
        ||!Name.StartsWith(TEXT("DW_QA_"))||!Name.EndsWith(TEXT(".ini"),ESearchCase::IgnoreCase)
        ||IsInside(INI,FPaths::ProjectDir())||IsInside(INI,FPaths::ProjectSavedDir()))
    {
        Reason=TEXT("INI must match the active override, use a DW_QA_*.ini filename, and be outside the project.");
        return false;
    }
    Reason=INI;
    if(ActualINI)*ActualINI=ActiveINI;
    return true;
}

struct FDeviceSample
{
    TAtomic<bool> bReady{false};
    Audio::FDeviceId DeviceID=0;
    float TransientVolume=-1.f;
};
}

struct FDWGameplayUXAudioCheck
{
    // This outer state stays on the game thread. Only the inner thread-safe sample crosses threads.
    TSharedPtr<DWGameplayUXAudioQA::FDeviceSample,ESPMode::ThreadSafe> Sample;
    TWeakObjectPtr<UDWUserSettings> Settings;
    TWeakObjectPtr<UWorld> World;
    FString GuardDetail;
    float OriginalMaster=1.f;
    float OriginalCategories[3]={1.f,1.f,1.f};
    float ReferenceCategories[3]={.37f,.59f,.83f};
    bool bIsolated=false;
    bool bChanged=false;
    bool bFinished=false;

    bool IsReady()const{return !bIsolated||!Sample||Sample->bReady.Load();}
};

static TSharedPtr<FDWGameplayUXAudioCheck> DWBeginAudioUXRuntimeChecks(UDWUserSettings* Settings,UWorld* World)
{
    check(IsInGameThread());
    auto State=MakeShared<FDWGameplayUXAudioCheck>();
    State->bIsolated=DWGameplayUXAudioQA::IsIsolated(State->GuardDetail);
    State->Settings=Settings;State->World=World;
    if(!State->bIsolated||!IsValid(Settings)||!IsValid(World)
        ||!World->GetGameInstance()||Settings!=World->GetGameInstance()->GetSubsystem<UDWUserSettings>())
        return State;
    FAudioDeviceHandle Device=World->GetAudioDevice();
    if(!Device)return State;
    State->OriginalMaster=Settings->GetMasterVolume();
    for(int32 I=0;I<3;++I)
    {
        State->OriginalCategories[I]=Settings->GetCategoryVolume(EDWSoundCategory(I));
        Settings->SetCategoryVolume(EDWSoundCategory(I),State->ReferenceCategories[I],false);
    }
    Settings->SetMasterVolume(0.f,false);
    State->bChanged=true;
    State->Sample=MakeShared<DWGameplayUXAudioQA::FDeviceSample,ESPMode::ThreadSafe>();
    const auto Sample=State->Sample;
    // Setter commands and this sample are ordered on the audio thread. Do not touch JSON/Check here.
    FAudioThread::RunCommandOnAudioThread([Sample,Device]()mutable
    {
        Sample->DeviceID=Device.GetDeviceID();
        Sample->TransientVolume=Device->GetTransientPrimaryVolume();
        Sample->bReady.Store(true);
    });
    return State;
}

static void DWFinishAudioUXRuntimeChecks(const TSharedPtr<FDWGameplayUXAudioCheck>& State,
    UDWUserSettings* Settings,UWorld* World,TFunctionRef<void(const FString&,bool,const FString&)> Check)
{
    check(IsInGameThread());
    if(!State){Check(TEXT("Audio QA has state"),false,TEXT("Begin returned no state"));return;}
    if(State->bFinished)return;
    if(!State->IsReady())return;
    Check(TEXT("Audio QA uses isolated settings INI"),State->bIsolated,State->GuardDetail);
    const bool bContext=State->Settings.Get()==Settings&&State->World.Get()==World&&IsValid(Settings)&&IsValid(World);
    Check(TEXT("Audio mute check keeps its original game context"),bContext,TEXT("Expected the same settings subsystem and World between Begin and Finish"));
    if(bContext&&State->bChanged)
    {
        bool bCategoriesKept=true;
        FString Detail;
        for(int32 I=0;I<3;++I)
        {
            const float Actual=Settings->GetCategoryVolume(EDWSoundCategory(I));
            bCategoriesKept&=FMath::IsNearlyEqual(Actual,State->ReferenceCategories[I]);
            Detail+=FString::Printf(TEXT("category%d=%.3f "),I,Actual);
        }
        Check(TEXT("Master zero preserves independent category settings"),
            FMath::IsNearlyZero(Settings->GetMasterVolume())&&bCategoriesKept,Detail);
        const FAudioDeviceHandle CurrentDevice=World->GetAudioDevice();
        const bool bSample=State->Sample&&State->Sample->bReady.Load()&&CurrentDevice;
        Check(TEXT("Master zero reaches current World audio device"),
            bSample&&State->Sample->DeviceID==CurrentDevice.GetDeviceID()&&FMath::IsNearlyZero(State->Sample->TransientVolume),
            bSample?FString::Printf(TEXT("World device %u, sampled device %u, transient gain %.4f; audio-thread gain sampled, not a waveform recording"),
                CurrentDevice.GetDeviceID(),State->Sample->DeviceID,State->Sample->TransientVolume):TEXT("No ready current-device sample"));
    }
    else if(State->bIsolated&&bContext)
        Check(TEXT("Audio device available for mute test"),false,TEXT("No audio device; mute test was not performed"));
    // Restore even if the runner changed World unexpectedly; this never writes the isolated INI.
    if(State->bChanged)
    {
        if(auto* OriginalSettings=State->Settings.Get())
        {
            for(int32 I=0;I<3;++I)OriginalSettings->SetCategoryVolume(EDWSoundCategory(I),State->OriginalCategories[I],false);
            OriginalSettings->SetMasterVolume(State->OriginalMaster,false);
            bool bRestored=FMath::IsNearlyEqual(OriginalSettings->GetMasterVolume(),State->OriginalMaster);
            for(int32 I=0;I<3;++I)bRestored&=FMath::IsNearlyEqual(OriginalSettings->GetCategoryVolume(EDWSoundCategory(I)),State->OriginalCategories[I]);
            Check(TEXT("Audio mute test restores original preferences"),bRestored,TEXT("Restored in memory without saving probe values"));
        }
    }
    State->bFinished=true;
}

static void DWCheckAudioUXSettingsPersistence(UDWUserSettings* Settings,
    TFunctionRef<void(const FString&,bool,const FString&)> Check)
{
    check(IsInGameThread());
    FString GuardDetail,ActualINI;
    const bool bIsolated=DWGameplayUXAudioQA::IsIsolated(GuardDetail,&ActualINI);
    Check(TEXT("Audio persistence uses isolated settings INI"),bIsolated,GuardDetail);
    Check(TEXT("Audio persistence has its settings subsystem"),IsValid(Settings),TEXT("Expected the current GameInstance settings subsystem"));
    if(!bIsolated||!IsValid(Settings))return;
    // Read actual disk data; querying GConfig would only re-read its in-memory cache.
    FConfigFile Disk;Disk.Read(ActualINI);
    auto Compare=[&](const TCHAR* Section,const TCHAR* Key,float Expected)
    {
        float Actual=-1.f;const bool bKey=Disk.GetFloat(Section,Key,Actual);
        Check(FString::Printf(TEXT("Preference persisted: %s/%s"),Section,Key),
            bKey&&FMath::IsFinite(Actual)&&FMath::IsNearlyEqual(Actual,Expected,1.e-4f),
            FString::Printf(TEXT("key=%s disk=%.4f current=%.4f"),bKey?TEXT("present"):TEXT("missing"),Actual,Expected));
    };
    Compare(TEXT("DoughWorld.UserSettings"),TEXT("MasterVolume"),Settings->GetMasterVolume());
    const TCHAR* Keys[]={TEXT("Music"),TEXT("Voice"),TEXT("SFX")};
    for(int32 I=0;I<3;++I)Compare(TEXT("DoughWorld.Audio"),Keys[I],Settings->GetCategoryVolume(EDWSoundCategory(I)));
    Compare(TEXT("DoughWorld.Camera"),TEXT("SensitivityMultiplier"),Settings->GetCameraSensitivityMultiplier());
}
