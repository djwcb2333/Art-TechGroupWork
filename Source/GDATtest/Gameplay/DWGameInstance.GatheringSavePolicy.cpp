#include "DWGameInstance.h"
#include "DWGatheringProfile.h"
#include "DWLocalizationLibrary.h"
#include "Engine/World.h"

// Admission only. No partial saves, D22 transaction edits, or SaveGame schema changes.
bool UDWGameInstance::RejectGatheringPreviewPersistence(const FString& RequestedMap)
{
    const FString Map=UWorld::RemovePIEPrefix(RequestedMap.IsEmpty()
        ?(GetWorld()?GetWorld()->GetOutermost()->GetName():FString())
        :RequestedMap);
    if(Map.IsEmpty())return false;
    // The exact preview map remains closed if its config override fails to load.
    const bool Preview=Map==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GatheringTest")||
        Cast<UDWGatheringProfile>(GetConfigForMap(Map))!=nullptr;
    if(!Preview)return false;
    Fail(DWText(this,
        TEXT("资源测试图仅供本次试玩，暂不支持创建、保存、加载或跨地图携带进度；退出试玩后资源、水塘、建筑和测试背包会重置。原有正式存档不会被覆盖。"),
        TEXT("This gathering map is a temporary playtest. Creating, saving, loading, or travelling with progress is disabled. Resources, ponds, buildings, and the test inventory reset after the playtest. Existing saves are preserved.")));
    return true;
}
