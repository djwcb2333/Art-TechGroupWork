#include "DWAudioLibrary.h"
#include "DWUserSettings.h"
#include "DWGameplayConfig.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
    bool IsGatherEvent(EDWAudioEvent Event)
    {
        switch (Event)
        {
        case EDWAudioEvent::GatherStart:
        case EDWAudioEvent::GatherLoop:
        case EDWAudioEvent::GatherSuccess:
        case EDWAudioEvent::GatherStop:
        case EDWAudioEvent::GatherFailed:
            return true;
        default:
            return false;
        }
    }

    bool ResolvePlayback(const UObject* Context, const UDWGameplayConfig* Config, EDWAudioEvent Event,
        USoundBase*& Sound, float& Volume, float& Pitch)
    {
        Sound = UDWAudioLibrary::GetEventSound(Config, Event);
        const UWorld* World = IsValid(Context) ? Context->GetWorld() : nullptr;
        if (!IsValid(Config) || !IsValid(Sound) || !World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer)
            return false;

        const bool bGathering = IsGatherEvent(Event);
        Volume = Event == EDWAudioEvent::UIClick ? Config->UIClickVolume
            : bGathering ? Config->GatherSoundVolume : Config->UIEventVolume;
        Pitch = Event == EDWAudioEvent::UIClick ? Config->UIClickPitch
            : bGathering ? Config->GatherSoundPitch : Config->UIEventPitch;
        Volume = FMath::IsFinite(Volume) ? FMath::Clamp(Volume, 0.f, 1.f) : 0.f;
        Pitch = FMath::IsFinite(Pitch) ? FMath::Clamp(Pitch, .5f, 2.f) : 1.f;
        return Volume > 0.f;
    }
}

USoundBase* UDWAudioLibrary::GetEventSound(const UDWGameplayConfig* Config, EDWAudioEvent Event)
{
    if (!IsValid(Config)) return nullptr;
    switch (Event)
    {
    case EDWAudioEvent::UIClick: return Config->UIClickSound;
    case EDWAudioEvent::UISelection: return Config->UISelectionSound;
    case EDWAudioEvent::UIFailed: return Config->UIFailedSound;
    case EDWAudioEvent::InventoryOpen: return Config->InventoryOpenSound;
    case EDWAudioEvent::InventoryClose: return Config->InventoryCloseSound;
    case EDWAudioEvent::CraftingOpen: return Config->CraftingOpenSound;
    case EDWAudioEvent::CraftingClose: return Config->CraftingCloseSound;
    case EDWAudioEvent::MenuOpen: return Config->MenuOpenSound;
    case EDWAudioEvent::MenuClose: return Config->MenuCloseSound;
    case EDWAudioEvent::TitleOpen: return Config->TitleOpenSound;
    case EDWAudioEvent::TitleClose: return Config->TitleCloseSound;
    case EDWAudioEvent::SaveSlotsOpen: return Config->SaveSlotsOpenSound;
    case EDWAudioEvent::SaveSlotsClose: return Config->SaveSlotsCloseSound;
    case EDWAudioEvent::PauseOpen: return Config->PauseOpenSound;
    case EDWAudioEvent::PauseClose: return Config->PauseCloseSound;
    case EDWAudioEvent::SettingsOpen: return Config->SettingsOpenSound;
    case EDWAudioEvent::SettingsClose: return Config->SettingsCloseSound;
    case EDWAudioEvent::DefeatOpen: return Config->DefeatOpenSound;
    case EDWAudioEvent::DefeatClose: return Config->DefeatCloseSound;
    case EDWAudioEvent::ItemUseSuccess: return Config->ItemUseSuccessSound;
    case EDWAudioEvent::ItemUseFailed: return Config->ItemUseFailedSound;
    case EDWAudioEvent::CraftSuccess: return Config->CraftSuccessSound;
    case EDWAudioEvent::CraftFailed: return Config->CraftFailedSound;
    case EDWAudioEvent::GatherStart: return Config->GatherStartSound;
    case EDWAudioEvent::GatherLoop: return Config->GatherLoopSound;
    case EDWAudioEvent::GatherSuccess:
    {
        // Compact the five slots before sampling: gaps never create silent picks.
        // Keep slot 1's serialized name so existing Data Assets remain compatible.
        USoundBase* Candidates[5];
        int32 Count = 0;
        for (USoundBase* Candidate : {Config->GatherSuccessSound.Get(), Config->GatherSuccessSound2.Get(),
            Config->GatherSuccessSound3.Get(), Config->GatherSuccessSound4.Get(), Config->GatherSuccessSound5.Get()})
        {
            if (IsValid(Candidate)) Candidates[Count++] = Candidate;
        }
        return Count > 0 ? Candidates[FMath::RandHelper(Count)] : nullptr;
    }
    case EDWAudioEvent::GatherStop:
    {
        // Match success/failure feedback: each populated slot has an equal chance.
        USoundBase* Candidates[5];
        int32 Count = 0;
        for (USoundBase* Candidate : {Config->GatherStopSound.Get(), Config->GatherStopSound2.Get(),
            Config->GatherStopSound3.Get(), Config->GatherStopSound4.Get(), Config->GatherStopSound5.Get()})
        {
            if (IsValid(Candidate)) Candidates[Count++] = Candidate;
        }
        return Count > 0 ? Candidates[FMath::RandHelper(Count)] : nullptr;
    }
    case EDWAudioEvent::GatherFailed:
    {
        // Match success feedback: each populated slot has an equal chance.
        // The caller still latches failures to one sound per held attempt.
        USoundBase* Candidates[5];
        int32 Count = 0;
        for (USoundBase* Candidate : {Config->GatherFailedSound.Get(), Config->GatherFailedSound2.Get(),
            Config->GatherFailedSound3.Get(), Config->GatherFailedSound4.Get(), Config->GatherFailedSound5.Get()})
        {
            if (IsValid(Candidate)) Candidates[Count++] = Candidate;
        }
        return Count > 0 ? Candidates[FMath::RandHelper(Count)] : nullptr;
    }
    default: return nullptr;
    }
}

bool UDWAudioLibrary::PlayEvent(const UObject* Context, const UDWGameplayConfig* Config, EDWAudioEvent Event)
{
    USoundBase* Sound = nullptr;
    float Volume = 0.f, Pitch = 1.f;
    if (!ResolvePlayback(Context, Config, Event, Sound, Volume, Pitch)) return false;
    UDWUserSettings::PlayFeedback(Context, Sound, Volume, Pitch);
    return true;
}

UAudioComponent* UDWAudioLibrary::CreateGatherLoop(const UObject* Context, const UDWGameplayConfig* Config)
{
    USoundBase* Sound = nullptr;
    float Volume = 0.f, Pitch = 1.f;
    if (!ResolvePlayback(Context, Config, EDWAudioEvent::GatherLoop, Sound, Volume, Pitch)) return nullptr;
    auto* Audio=UGameplayStatics::CreateSound2D(Context, Sound, Volume, Pitch, 0.f, nullptr, false, false); if(Audio){if(auto* S=UDWUserSettings::Resolve(Context))S->RouteAudio(Audio,EDWSoundCategory::SFX);Audio->Play();}return Audio;
}

namespace
{
    bool ResolveWorldSlot(const UObject* Context, USoundBase* Sound, FVector Location, float& Volume, float& Pitch)
    {
        const UWorld* World = IsValid(Context) ? Context->GetWorld() : nullptr;
        if (!IsValid(Sound) || !World || World->bIsTearingDown || World->GetNetMode() == NM_DedicatedServer || Location.ContainsNaN()) return false;
        Volume = FMath::IsFinite(Volume) ? FMath::Clamp(Volume, 0.f, 4.f) : 0.f;
        Pitch = FMath::IsFinite(Pitch) ? FMath::Clamp(Pitch, .125f, 4.f) : 1.f;
        return Volume > 0.f;
    }
}

bool UDWAudioLibrary::PlayWorldSound(const UObject* Context, USoundBase* Sound, FVector Location,
    float Volume, float Pitch, USoundAttenuation* Attenuation)
{
    if (!ResolveWorldSlot(Context, Sound, Location, Volume, Pitch)) return false;
    UAudioComponent* Audio = UGameplayStatics::SpawnSoundAtLocation(Context, Sound, Location,
        FRotator::ZeroRotator, Volume, Pitch, 0.f, Attenuation, nullptr, true);
    if (Audio){if(auto* S=UDWUserSettings::Resolve(Context))S->RouteAudio(Audio,EDWSoundCategory::SFX);Audio->SetUISound(false);}
    return IsValid(Audio);
}

UAudioComponent* UDWAudioLibrary::CreateWorldLoop(const UObject* Context, USoundBase* Sound,
    USceneComponent* AttachTo, FName SocketName, FVector Location, float Volume, float Pitch, USoundAttenuation* Attenuation)
{
    if (!ResolveWorldSlot(Context, Sound, Location, Volume, Pitch)) return nullptr;
    UAudioComponent* Audio = IsValid(AttachTo)
        ? UGameplayStatics::SpawnSoundAttached(Sound, AttachTo, SocketName, Location, FRotator::ZeroRotator,
            EAttachLocation::KeepRelativeOffset, true, Volume, Pitch, 0.f, Attenuation, nullptr, false)
        : UGameplayStatics::SpawnSoundAtLocation(Context, Sound, Location, FRotator::ZeroRotator,
            Volume, Pitch, 0.f, Attenuation, nullptr, false);
    if (Audio){if(auto* S=UDWUserSettings::Resolve(Context))S->RouteAudio(Audio,EDWSoundCategory::SFX);Audio->SetUISound(false);}
    return Audio;
}
