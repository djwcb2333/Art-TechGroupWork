#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "DWGatheringHitAudioSubsystem.generated.h"

class USoundWave;
struct FStreamableHandle;

/** Dedicated cosmetic configuration; hard dependencies give the cooker an explicit chain. */
UCLASS(BlueprintType)
class GDATTEST_API UDWGatheringHitAudioSettings : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Gathering|Hit Audio") TObjectPtr<USoundWave> AxeSound;
    UPROPERTY(EditAnywhere, Category="Gathering|Hit Audio") TObjectPtr<USoundWave> PickaxeSound;
    UPROPERTY(EditAnywhere, Category="Gathering|Hit Audio", meta=(ClampMin="0",ClampMax="4")) float Volume=1.f;
    UPROPERTY(EditAnywhere, Category="Gathering|Hit Audio", meta=(ClampMin="0.125",ClampMax="4")) float Pitch=1.f;
};

/** Best-effort cosmetic work, never an authority or persistence admission prerequisite. */
UCLASS()
class GDATTEST_API UDWGatheringHitAudioSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    bool ShouldWaitOptionalPreparation(const UWorld* World);
    void PlayCommittedHit(const UObject* Context,FName ToolKind,FVector Location) const;
private:
    bool TickPreparation(float DeltaSeconds);
    void OnLoaded();
    void LogFailureOnce(const TCHAR* Reason);
    UPROPERTY(Transient) TObjectPtr<UDWGatheringHitAudioSettings> Settings;
    TSharedPtr<FStreamableHandle> LoadHandle;
    FTSTicker::FDelegateHandle Ticker;
    double StartedAt=0.;
    double OptionalWaitStarted=-1.;
    bool bPrecacheStarted=false;
    bool bStopped=false;
    bool bFailureLogged=false;
};
