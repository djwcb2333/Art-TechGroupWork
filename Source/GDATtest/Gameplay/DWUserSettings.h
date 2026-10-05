#pragma once
#include "CoreMinimal.h"
#include "AudioDeviceHandle.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DWUserSettings.generated.h"
class USoundClass; class USoundMix; class UAudioComponent; class USoundBase; class UWorld;
UENUM(BlueprintType)
enum class EDWSoundCategory:uint8 { Music, Voice, SFX };
/** Local preferences, independent of adventure saves. Sound classes preserve per-effect fades. */
UCLASS()
class GDATTEST_API UDWUserSettings:public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 /** Total gain for this game's audio device, including sounds outside our category routing. */
 UFUNCTION(BlueprintPure,Category="DoughWorld|Settings") float GetMasterVolume() const;
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Settings") void SetMasterVolume(float Value,bool bSave=true);
 UFUNCTION(BlueprintPure,Category="DoughWorld|Settings") float GetCategoryVolume(EDWSoundCategory Category) const;
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Settings") void SetCategoryVolume(EDWSoundCategory Category,float Value,bool bSave=true);
 /** Local mouse-camera multiplier, separate from authored Hero sensitivity and adventure saves. */
 UFUNCTION(BlueprintPure,Category="DoughWorld|Settings") float GetCameraSensitivityMultiplier() const;
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Settings") void SetCameraSensitivityMultiplier(float Value,bool bSave=true);
 static constexpr float CameraSensitivityMinimum=0.25f;
 static constexpr float CameraSensitivityMaximum=3.f;
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Settings") void ApplyAudio();
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Audio") void RouteAudio(UAudioComponent* Audio,EDWSoundCategory Category);
 UFUNCTION(BlueprintPure,Category="DoughWorld|Audio") USoundClass* GetSoundClass(EDWSoundCategory Category);
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Audio",meta=(WorldContext="Context")) static UAudioComponent* PlayFeedback(const UObject* Context,USoundBase* Sound,float Volume=1.f,float Pitch=1.f);
 static UDWUserSettings* Resolve(const UObject* Context);
private:
 UPROPERTY(Transient) TArray<TObjectPtr<USoundClass>> Classes;
 UPROPERTY(Transient) TObjectPtr<USoundMix> Mix;
 float Volumes[3]={1.f,1.f,1.f};
 float MasterVolume=1.f;
 float CameraSensitivityMultiplier=1.f;
 FAudioDeviceHandle AppliedAudioDevice;
 FDelegateHandle WorldLoadedHandle;
 bool bPushed=false;
 void ApplyAudioForWorld(UWorld* World);
 void HandleWorldLoaded(UWorld* World);
};
