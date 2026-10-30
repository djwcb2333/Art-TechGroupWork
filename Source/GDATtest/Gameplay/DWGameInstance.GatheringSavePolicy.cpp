#include "DWGameInstance.h"
#include "DWGatheringWorldPersistence.h"
#include "DWGatheringProfile.h"
#include "DWGatheringWorld.h"
#include "DWGatheringPond.h"
#include "DWLocalizationLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void UDWGameInstance::MarkUnsupportedGatheringPersistence(UWorld* World)
{
    if(World&&World==GetWorld()&&World->IsGameWorld()&&!World->bIsTearingDown)
    {
        FString SupportError;
        // A supported committed participant is not an unsupported-world mark. Unknown types remain sticky.
        if(FDWGatheringWorldPersistence::IsSupportedMap(UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()))&&
            FDWGatheringWorldPersistence::SupportsWorld(*World,SupportError))return;
        if(UnsupportedGatheringPersistenceWorld.Get()!=World||UnsupportedGatheringPersistenceReason.IsEmpty())
        {
            UnsupportedGatheringPersistenceWorld=World;
            UnsupportedGatheringPersistenceReason=SupportError.IsEmpty()?TEXT("Gathering world is not registered for complete persistence: ")+World->GetOutermost()->GetName():SupportError;
            UE_LOG(LogTemp,Warning,TEXT("DW_GATHERING_FIRST_REJECTION: %s"),*UnsupportedGatheringPersistenceReason);
        }
    }
}

// Admission only: the unified item/recipe configuration is save-compatible, but new
// gathering actors are not yet participants in the complete D22 world transaction.
bool UDWGameInstance::RejectGatheringPreviewPersistence(const FString& RequestedMap)
{
    UWorld* World=GetWorld();
    const FString Current=World?UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()):FString();
    const FString Map=UWorld::RemovePIEPrefix(RequestedMap.IsEmpty()?Current:RequestedMap);
    if(Map.IsEmpty())return false;
    const bool Preview=Map==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GatheringTest");
    if(Preview)
    {
        Fail(DWText(this,
            TEXT("资源测试图仅供试玩，暂不支持创建、保存、加载或跨地图携带进度；退出后测试场景会重置。原有正式存档不会被覆盖。"),
            TEXT("This gathering test map does not support creating, saving, loading, or travelling with progress. Its test world resets after play. Existing saves are preserved.")));
        return true;
    }
    if(FDWGatheringWorldPersistence::IsSupportedMap(Map))
    {
        FString SupportError;
        if(World&&Map==Current)
        {
            if(UnsupportedGatheringPersistenceWorld.Get()!=World&&FDWGatheringWorldPersistence::SupportsWorld(*World,SupportError))return false;
            if(UnsupportedGatheringPersistenceWorld.Get()==World)SupportError=UnsupportedGatheringPersistenceReason;
            if(!SupportError.IsEmpty())UE_LOG(LogTemp,Warning,TEXT("LANG gathering save admission: %s"),*SupportError);
            Fail(FText::FromString(DWText(this,TEXT("场景存档准入失败，原文件保留。具体原因："),TEXT("World persistence admission rejected; source preserved. Reason: ")).ToString()+SupportError));
            return true;
        }
        // Target admission only. ValidateSave/D22 still require the complete schema-1 block;
        // Prepare also matches the exact authored population before any adoption.
        auto* Profile=UDWGameplayConfig::AsGatheringConfig(GetConfigForMap(Map));
        if(Profile&&FDWGatheringWorldPersistence::IsApprovedProfileForMap(Map,Profile->GetPathName())&&Profile->ValidateGatheringDefinitions(SupportError))return false;
        Fail(DWText(this,TEXT("语言试玩的完整采集存档配置不可用或无效，已保留原文件。"),
            TEXT("The language playtest's complete gathering save profile is unavailable or invalid. The original file was preserved.")));return true;
    }
    bool Unsupported=World&&Map==Current&&UnsupportedGatheringPersistenceWorld.Get()==World;
    if(World&&Map==Current&&!Unsupported)
    {
        // Also catches authored actors before their first BeginPlay. Staged failed candidates
        // and the plain transient preview never count as committed world state.
        for(TActorIterator<ADWGatheringInteractable> It(World);It;++It)
            if(!It->IsStaged()&&!It->IsActorBeingDestroyed()){Unsupported=true;break;}
        if(!Unsupported)for(TActorIterator<ADWGatheringPond> It(World);It;++It)
            if(!It->IsActorBeingDestroyed()){Unsupported=true;break;}
        if(Unsupported)MarkUnsupportedGatheringPersistence(World);
    }
    if(!Unsupported)return false;
    Fail(FText::FromString(DWText(this,TEXT("场景尚未支持完整采集存档，原文件保留。具体原因："),TEXT("World gathering persistence is unsupported; source preserved. Reason: ")).ToString()+UnsupportedGatheringPersistenceReason));
    return true;
}
