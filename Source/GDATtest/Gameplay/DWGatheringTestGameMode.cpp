#include "DWGatheringTestGameMode.h"
#include "DWGatheringProfile.h"
#include "DWGatheringComponent.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameplayHUD.h"
#include "DWInventoryComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
void ADWGatheringTestGameMode::StartPlay()
{
    Super::StartPlay();
    StartTestSession();
}
void ADWGatheringTestGameMode::StartTestSession()
{
    auto* GI=GetGameInstance<UDWGameInstance>();
    auto* Profile=GI?Cast<UDWGatheringProfile>(GI->GetConfig()):nullptr;
    auto* PC=GetWorld()?Cast<ADWPlayerController>(GetWorld()->GetFirstPlayerController()):nullptr;
    auto* Player=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
    auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
    if(!Profile||!PC||!Player||!HUD||GI->HasActiveSlot())return;
    HUD->SetSessionStarted(true);HUD->ClosePanels();PC->SetPause(false);
    auto* Bag=Player->GetInventory();
    bool Empty=Bag!=nullptr;
    if(Bag)for(const auto& Slot:Bag->GetSlots())if(Slot.Quantity>0){Empty=false;break;}
    if(Empty)for(const auto& Item:Profile->StarterItems)Bag->TryAddItem(Item.ItemId,Item.Quantity);
    if(auto* Gathering=Player->FindComponentByClass<UDWGatheringComponent>())Gathering->TryUseTool(TEXT("SimpleSugarAxe"));
    UDWGatheringComponent::Notify(Player,TEXT("资源测试：F 采集，背包中使用斧或镐切换工具。退出试玩后重置。"),TEXT("Gathering test: F to interact; Use an axe or pickaxe from Inventory. Resets after play."));
}
