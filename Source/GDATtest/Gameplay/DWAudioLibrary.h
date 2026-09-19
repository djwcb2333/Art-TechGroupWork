#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWAudioLibrary.generated.h"

class UAudioComponent;
class UDWGameplayConfig;
class USoundBase;
class USceneComponent;
class USoundAttenuation;

/** One event per UI/harvest transition. No automatic sound fallback or operation side effects. */
UENUM(BlueprintType)
enum class EDWAudioEvent : uint8
{
    UIClick,
    UISelection,
    UIFailed,
    InventoryOpen,
    InventoryClose,
    CraftingOpen,
    CraftingClose,
    MenuOpen,
    MenuClose,
    TitleOpen,
    TitleClose,
    SaveSlotsOpen,
    SaveSlotsClose,
    PauseOpen,
    PauseClose,
    SettingsOpen,
    SettingsClose,
    DefeatOpen,
    DefeatClose,
    ItemUseSuccess,
    ItemUseFailed,
    CraftSuccess,
    CraftFailed,
    GatherStart,
    GatherLoop,
    GatherSuccess,
    GatherStop,
    GatherFailed
};

/** Shared, local 2D feedback. This library does not grant items or execute UI actions. */
UCLASS()
class GDATTEST_API UDWAudioLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Returns only the requested slot. Empty slots do not silently substitute a different sound. */
    UFUNCTION(BlueprintPure, Category="Dough World|Audio")
    static USoundBase* GetEventSound(const UDWGameplayConfig* Config, EDWAudioEvent Event);

    /** Plays once. False means no valid sound/world or muted volume; it is not an operation failure. */
    UFUNCTION(BlueprintCallable, Category="Dough World|Audio", meta=(WorldContext="Context"))
    static bool PlayEvent(const UObject* Context, const UDWGameplayConfig* Config, EDWAudioEvent Event);

    /**
     * Creates and starts the held-harvest sound with auto-destroy disabled.
     * The player must own the returned component and stop/destroy it on release, cancellation or EndPlay.
     * For a non-looping source, bind OnAudioFinished and replay only while harvesting remains valid.
     */
    UFUNCTION(BlueprintCallable, Category="Dough World|Audio", meta=(WorldContext="Context"))
    static UAudioComponent* CreateGatherLoop(const UObject* Context, const UDWGameplayConfig* Config);

    /** Optional gameplay slot. Uses the engine audio path and the existing master-volume setting. An empty slot stays silent. */
    UFUNCTION(BlueprintCallable, Category="Dough World|Audio", meta=(WorldContext="Context"))
    static bool PlayWorldSound(const UObject* Context, USoundBase* Sound, FVector Location,
        float Volume = 1.f, float Pitch = 1.f, USoundAttenuation* Attenuation = nullptr);

    /**
     * Starts one reusable, non-auto-destroyed audio component. Location is relative when attached,
     * otherwise world space. The caller owns stop/destruction and replay of non-looping source clips.
     * No SoundWave or SoundCue asset is modified. Gameplay audio respects normal game pause.
     */
    UFUNCTION(BlueprintCallable, Category="Dough World|Audio", meta=(WorldContext="Context"))
    static UAudioComponent* CreateWorldLoop(const UObject* Context, USoundBase* Sound,
        USceneComponent* AttachTo, FName SocketName, FVector Location,
        float Volume = 1.f, float Pitch = 1.f, USoundAttenuation* Attenuation = nullptr);
};
