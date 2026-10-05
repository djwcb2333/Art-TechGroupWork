#include "DWUserSettings.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"
namespace { const TCHAR* Names[]={TEXT("Music"),TEXT("Voice"),TEXT("SFX")}; }
UDWUserSettings* UDWUserSettings::Resolve(const UObject* C){auto* GI=C?UGameplayStatics::GetGameInstance(C):nullptr;return GI?GI->GetSubsystem<UDWUserSettings>():nullptr;}
void UDWUserSettings::Initialize(FSubsystemCollectionBase& C)
{
 Super::Initialize(C);Classes.SetNum(3);
 for(int32 I=0;I<3;++I){GConfig->GetFloat(TEXT("DoughWorld.Audio"),Names[I],Volumes[I],GGameUserSettingsIni);Volumes[I]=FMath::IsFinite(Volumes[I])?FMath::Clamp(Volumes[I],0.f,1.f):1.f;}
 // Keep the existing preference key so old local settings remain compatible.
 GConfig->GetFloat(TEXT("DoughWorld.UserSettings"),TEXT("MasterVolume"),MasterVolume,GGameUserSettingsIni);
 MasterVolume=FMath::IsFinite(MasterVolume)?FMath::Clamp(MasterVolume,0.f,1.f):1.f;
 GConfig->GetFloat(TEXT("DoughWorld.Camera"),TEXT("SensitivityMultiplier"),CameraSensitivityMultiplier,GGameUserSettingsIni);
 CameraSensitivityMultiplier=FMath::IsFinite(CameraSensitivityMultiplier)
  ?FMath::Clamp(CameraSensitivityMultiplier,CameraSensitivityMinimum,CameraSensitivityMaximum):1.f;
 WorldLoadedHandle=FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this,&UDWUserSettings::HandleWorldLoaded);
 ApplyAudio();
}
USoundClass* UDWUserSettings::GetSoundClass(EDWSoundCategory C)
{
 const int32 I=FMath::Clamp(int32(C),0,2);
 if(!Classes[I]){const FString N=FString(TEXT("SC_DW_"))+Names[I];Classes[I]=LoadObject<USoundClass>(nullptr,*(TEXT("/Game/DoughWorld/Audio/Mixing/")+N+TEXT(".")+N));if(!Classes[I])Classes[I]=NewObject<USoundClass>(this,FName(*N));}
 return Classes[I];
}
float UDWUserSettings::GetCategoryVolume(EDWSoundCategory C)const{return Volumes[FMath::Clamp(int32(C),0,2)];}
float UDWUserSettings::GetMasterVolume()const{return MasterVolume;}
void UDWUserSettings::SetMasterVolume(float V,bool Save)
{
 MasterVolume=FMath::IsFinite(V)?FMath::Clamp(V,0.f,1.f):1.f;ApplyAudio();
 if(Save){GConfig->SetFloat(TEXT("DoughWorld.UserSettings"),TEXT("MasterVolume"),MasterVolume,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);}
}
float UDWUserSettings::GetCameraSensitivityMultiplier()const{return CameraSensitivityMultiplier;}
void UDWUserSettings::SetCameraSensitivityMultiplier(float V,bool Save)
{
 CameraSensitivityMultiplier=FMath::IsFinite(V)?FMath::Clamp(V,CameraSensitivityMinimum,CameraSensitivityMaximum):1.f;
 if(Save){GConfig->SetFloat(TEXT("DoughWorld.Camera"),TEXT("SensitivityMultiplier"),CameraSensitivityMultiplier,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);}
}
void UDWUserSettings::SetCategoryVolume(EDWSoundCategory C,float V,bool Save)
{
 const int32 I=FMath::Clamp(int32(C),0,2);Volumes[I]=FMath::IsFinite(V)?FMath::Clamp(V,0.f,1.f):1.f;ApplyAudio();
 if(Save){GConfig->SetFloat(TEXT("DoughWorld.Audio"),Names[I],Volumes[I],GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);}
}
void UDWUserSettings::ApplyAudio()
{
 ApplyAudioForWorld(GetWorld());
}
void UDWUserSettings::HandleWorldLoaded(UWorld* World)
{
 if(World&&World->GetGameInstance()==GetGameInstance())ApplyAudioForWorld(World);
}
void UDWUserSettings::ApplyAudioForWorld(UWorld* World)
{
 if(!World||!World->IsGameWorld()||World->bIsTearingDown||World->GetGameInstance()!=GetGameInstance())return;
 FAudioDeviceHandle AudioDevice=World->GetAudioDevice();if(!AudioDevice)return;
 if(AppliedAudioDevice&&AppliedAudioDevice.GetDeviceID()!=AudioDevice.GetDeviceID())
 {
  if(bPushed&&Mix)AppliedAudioDevice->PopSoundMixModifier(Mix);
  AppliedAudioDevice->SetTransientPrimaryVolume(1.f);
  bPushed=false;
 }
 AppliedAudioDevice=AudioDevice;
 // This persistent device gain covers present and future sounds, routed or otherwise.
 // UE reads it on every audio update; do not multiply master into the category a second time.
 AudioDevice->SetTransientPrimaryVolume(MasterVolume);
 if(!Mix){Mix=NewObject<USoundMix>(this);Mix->Duration=-1.f;Mix->FadeInTime=0.f;Mix->FadeOutTime=0.f;}
 if(!bPushed){AudioDevice->PushSoundMixModifier(Mix);bPushed=true;}
 // Final gain = device master * authored/component/fade gain * category gain.
 // Leave AudioComponent::VolumeMultiplier intact so gameplay fades keep working.
 for(int32 I=0;I<3;++I)AudioDevice->SetSoundMixClassOverride(Mix,GetSoundClass(EDWSoundCategory(I)),Volumes[I],1.f,0.f,true);
}
void UDWUserSettings::RouteAudio(UAudioComponent* A,EDWSoundCategory C)
{
 if(!IsValid(A))return;ApplyAudio();USoundClass* Cl=GetSoundClass(C);if(A->SoundClassOverride==Cl)return;
 const bool Playing=A->IsPlaying();const bool Auto=A->bAutoDestroy;
 if(Playing){A->bAutoDestroy=false;A->Stop();}A->SoundClassOverride=Cl;A->bAutoDestroy=Auto;if(Playing)A->Play();
}
UAudioComponent* UDWUserSettings::PlayFeedback(const UObject* C,USoundBase* S,float V,float P)
{
 if(!S)return nullptr;auto* A=UGameplayStatics::CreateSound2D(C,S,V,P,0.f,nullptr,false,true);
 if(A){if(auto* Settings=Resolve(C))Settings->RouteAudio(A,EDWSoundCategory::SFX);A->SetUISound(true);A->Play();}return A;
}
void UDWUserSettings::Deinitialize()
{
 FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(WorldLoadedHandle);WorldLoadedHandle.Reset();
 if(AppliedAudioDevice)
 {
  if(bPushed&&Mix)AppliedAudioDevice->PopSoundMixModifier(Mix);
  AppliedAudioDevice->SetTransientPrimaryVolume(1.f);
 }
 AppliedAudioDevice.Reset();bPushed=false;Super::Deinitialize();
}
