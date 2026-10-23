#include "DWCombatTestDirector.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWGameplayHUD.h"
#include "DWInventoryComponent.h"
#include "DWLoadingTransition.h"
#include "DWLocalizationLibrary.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"

ADWCombatTestDirector::ADWCombatTestDirector()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=true;
    // The normal title screen can pause the world before its player and HUD are both ready.
    // A nonzero tick interval would prevent this bootstrap from advancing while paused.
    PrimaryActorTick.bTickEvenWhenPaused=true;
    PrimaryActorTick.TickInterval=0.f;
    PrimaryActorTick.bAllowTickOnDedicatedServer=false;
}

void ADWCombatTestDirector::BeginPlay()
{
    Super::BeginPlay();
    TryStartTestSession();
}

void ADWCombatTestDirector::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    TryStartTestSession();
}

void ADWCombatTestDirector::TryStartTestSession()
{
    if(!bAutoStartCombatTest||bTestSessionStarted)
    {
        SetActorTickEnabled(false);
        return;
    }
    UWorld* TestWorld=GetWorld();
    if(!TestWorld||!TestWorld->IsGameWorld()||TestWorld->bIsTearingDown)return;
    UDWGameInstance* GameInstance=TestWorld->GetGameInstance<UDWGameInstance>();
    if(!GameInstance)return;
    const FString MapPackage=UWorld::RemovePIEPrefix(TestWorld->GetOutermost()->GetName());
    const UDWGameplayConfig* Config=GameInstance->GetConfig();
    if(GameInstance->IsAllowedGameplayMap(MapPackage)||(Config&&MapPackage==Config->MainMenuMap))
    {
        UE_LOG(LogTemp,Warning,TEXT("Combat Test Director refused to auto-start on a normal gameplay/menu map: %s"),*MapPackage);
        SetActorTickEnabled(false);
        return;
    }
    if(GameInstance->IsPlayerRestorePending()||UDWWorldEventSubsystem::IsPlaying(this))return;
    if(const auto* Loading=GameInstance->GetSubsystem<UDWLoadingTransitionSubsystem>();Loading&&Loading->IsTransitionActive())return;
    auto* PlayerController=Cast<ADWPlayerController>(TestWorld->GetFirstPlayerController());
    if(!PlayerController||!PlayerController->IsLocalPlayerController())return;
    auto* TestPlayer=Cast<ADWPlayerCharacter>(PlayerController->GetPawn());
    auto* GameplayHUD=Cast<ADWGameplayHUD>(PlayerController->GetHUD());
    if(!TestPlayer||!TestPlayer->HasActorBegunPlay()||TestPlayer->IsDead()||!TestPlayer->GetInventory()||
        !GameplayHUD||!GameplayHUD->HasActorBegunPlay()||!GameplayHUD->GetGameplayWidget())return;

    // Mark and stop before invoking UI/inventory callbacks. Repeated ticks or another director
    // must not keep closing the player's pause menu or repeatedly grant consumables.
    bTestSessionStarted=true;
    SetActorTickEnabled(false);
    if(GameplayHUD->IsSessionStarted())return;
    TestPlayer->bDrawCombatDebug=bEnablePlayerCombatDebug;
    GameplayHUD->SetSessionStarted(true);
    GameplayHUD->ClosePanels();
    const int32 AlcoholAmount=FMath::Clamp(StartingAlcoholAmount,0,1000);
    if(bGrantStartingAlcohol&&AlcoholAmount>0)
    {
        bStartingAlcoholGranted=TestPlayer->GetInventory()->TryAddItem(TEXT("Alcohol"),AlcoholAmount);
        if(!bStartingAlcoholGranted)
            UE_LOG(LogTemp,Warning,TEXT("Combat Test Director could not grant %d alcohol; the existing inventory was kept."),AlcoholAmount);
    }
    if(bShowStartupMessage)
        GameplayHUD->Notify(DWText(this,TEXT("战斗测试已开始。死亡后停止并重新播放关卡。"),TEXT("Combat test started. After defeat, stop and play this level again.")));
    UE_LOG(LogTemp,Log,TEXT("Combat Test Director started %s without creating a save slot."),*MapPackage);
}
