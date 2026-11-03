#include "DWGatheringHitAudioSubsystem.h"
#include "DWAudioLibrary.h"
#include "AudioDevice.h"
#include "Engine/AssetManager.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Sound/SoundWave.h"

namespace
{
    constexpr double PreparationLifetimeSeconds=5.;
    constexpr double OptionalWaitBudgetSeconds=.2;
    bool Ready(const USoundWave* Wave)
    {return IsValid(Wave)&&Wave->GetPrecacheState()==ESoundWavePrecacheState::Done;}
}

void UDWGatheringHitAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    StartedAt=FPlatformTime::Seconds();
    // Explicitly cooked new settings asset in the already AlwaysCook Core/Data directory.
    LoadHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(
        FSoftObjectPath(TEXT("/Game/DoughWorld/Core/Data/DA_DWGatherHitAudio.DA_DWGatherHitAudio")),
        FStreamableDelegate::CreateUObject(this,&UDWGatheringHitAudioSubsystem::OnLoaded));
    if(!LoadHandle){bStopped=true;LogFailureOnce(TEXT("could not request settings; cosmetic audio disabled"));return;}
    Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UDWGatheringHitAudioSubsystem::TickPreparation),.02f);
}

void UDWGatheringHitAudioSubsystem::OnLoaded()
{
    if(bStopped||!LoadHandle.IsValid())return;
    Settings=Cast<UDWGatheringHitAudioSettings>(LoadHandle->GetLoadedAsset());
    if(!Settings){bStopped=true;LogFailureOnce(TEXT("settings missing; cosmetic audio disabled"));}
}

void UDWGatheringHitAudioSubsystem::LogFailureOnce(const TCHAR* Reason)
{
    if(bFailureLogged)return;
    bFailureLogged=true;
    UE_LOG(LogTemp,Warning,TEXT("DW gather hit audio: %s. Gameplay and save admission are unchanged."),Reason);
}

bool UDWGatheringHitAudioSubsystem::TickPreparation(float)
{
    if(bStopped)return false;
    const UWorld* World=GetGameInstance()?GetGameInstance()->GetWorld():nullptr;
    if(FPlatformTime::Seconds()-StartedAt>=PreparationLifetimeSeconds)
    {
        bStopped=true;
        LogFailureOnce(TEXT("optional preload/decode budget expired; unready voices remain silent"));
        if(LoadHandle.IsValid()&&!LoadHandle->HasLoadCompleted())LoadHandle->CancelHandle();
        return false;
    }
    if(World&&World->bIsTearingDown)return true; // GI can survive travel; deadline still expires normally.
    if(!Settings)return true;
    auto* Device=World?World->GetAudioDeviceRaw():nullptr;
    if(!Device)return true; // No audio device cannot become a mandatory loading task.
    if(!bPrecacheStarted)
    {
        bPrecacheStarted=true; // Before work; no repeating decode requests.
        for(auto* Wave:{Settings->AxeSound.Get(),Settings->PickaxeSound.Get()})
            // Join the engine-owned normal async precache; never reset an active/completed wave.
            // Force-full async recursion resets InProgress to NotStarted on UE5.8's audio thread.
            if(IsValid(Wave)&&Wave->GetPrecacheState()==ESoundWavePrecacheState::NotStarted)
                Device->Precache(Wave,false,false,false);
        if(!Settings->AxeSound||!Settings->PickaxeSound)LogFailureOnce(TEXT("a configured sound is missing; that voice stays silent"));
    }
    const bool AxeDone=!Settings->AxeSound||Ready(Settings->AxeSound);
    const bool PickaxeDone=!Settings->PickaxeSound||Ready(Settings->PickaxeSound);
    if(AxeDone&&PickaxeDone){bStopped=true;return false;}
    return true;
}

bool UDWGatheringHitAudioSubsystem::ShouldWaitOptionalPreparation(const UWorld* World)
{
    if(!World||World->bIsTearingDown||!World->GetAudioDeviceRaw()||bStopped)return false;
    if(Settings&&Settings->Volume<=0.f)return false;
    if(Settings&&Ready(Settings->AxeSound)&&Ready(Settings->PickaxeSound))return false;
    const double Now=FPlatformTime::Seconds();
    if(OptionalWaitStarted<0.)OptionalWaitStarted=Now;
    // One GI-wide finite optional budget. Repeated transitions never restart it.
    return Now-OptionalWaitStarted<OptionalWaitBudgetSeconds;
}

void UDWGatheringHitAudioSubsystem::PlayCommittedHit(const UObject* Context,FName ToolKind,FVector Location) const
{
    // No loads, precache, waits, event queues, or gameplay mutations on the hit path.
    const UWorld* World=IsValid(Context)?Context->GetWorld():nullptr;
    if(!World||!World->IsGameWorld()||World->bIsTearingDown||World->GetGameInstance()!=GetGameInstance()
        ||World->GetNetMode()==NM_DedicatedServer||!World->GetAudioDeviceRaw()||!IsValid(Settings))return;
    USoundWave* Wave=ToolKind==TEXT("Axe")?Settings->AxeSound.Get():ToolKind==TEXT("Pickaxe")?Settings->PickaxeSound.Get():nullptr;
    if(!Ready(Wave))return; // Drop an unready cosmetic event; never replay a stale hit later.
    UDWAudioLibrary::PlayWorldSound(Context,Wave,Location,Settings->Volume,Settings->Pitch,nullptr);
}

void UDWGatheringHitAudioSubsystem::Deinitialize()
{
    bStopped=true;
    FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
    if(LoadHandle.IsValid())LoadHandle->CancelHandle();
    LoadHandle.Reset();Settings=nullptr;
    Super::Deinitialize();
}
