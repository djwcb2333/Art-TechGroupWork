#include "DWLanguagePlaytestGameMode.h"
#include "DWLanguageSubsystem.h"
#include "DWLanguageCarrier.h"
#include "DWLanguageUIBridgeComponent.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameplayHUD.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "DWCombatEquipment.h"
#include "DWStarterPolicy.h"
#include "DWGatheringProfile.h"
#include "DWGatheringComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWLocalizationLibrary.h"
#include "Dom/JsonObject.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ADWLanguagePlaytestGameMode::ADWLanguagePlaytestGameMode()
{
 static ConstructorHelpers::FClassFinder<APawn> Hero(TEXT("/Game/DoughWorld/Characters/Hero/Blueprints/BP_DWTopDownCharacter"));
 static ConstructorHelpers::FClassFinder<APlayerController> Controller(TEXT("/Game/DoughWorld/Core/Controllers/BP_DWPlayerController"));
 static ConstructorHelpers::FClassFinder<AHUD> UI(TEXT("/Game/DoughWorld/UI/HUD/BP_DWGameplayHUD"));
 if(Hero.Succeeded())DefaultPawnClass=Hero.Class;if(Controller.Succeeded())PlayerControllerClass=Controller.Class;if(UI.Succeeded())HUDClass=UI.Class;
}
void ADWLanguagePlaytestGameMode::StartPlay()
{
 Super::StartPlay();auto* GI=GetGameInstance<UDWGameInstance>();auto* S=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 auto* PC=GetWorld()?Cast<ADWPlayerController>(GetWorld()->GetFirstPlayerController()):nullptr;
 auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 FString Error;
 if(!GI||GI->HasActiveSlot()||!S||!P||!PC||!HUD||!S->StartPlaytest(P,Error)||!Cast<UDWGatheringProfile>(P->GetGameplayConfig()))
 {UE_LOG(LogTemp,Error,TEXT("LANG01 bootstrap rejected: %s"),*Error);return;}
 TArray<FDWItemStack> Starter;
 if(!DWStarterPolicy::ReadBasicBatch(S->GetTrialConfig(),Starter,Error))
 {UE_LOG(LogTemp,Error,TEXT("LANG01 starter rejected: %s"),*Error);return;}
 // Inventory, owned set and the per-playthrough receipt are one typed native commit.
 auto* Weapon=P->WeaponAnimationComponent.Get();
 if(!Weapon){UE_LOG(LogTemp,Error,TEXT("LANG01 weapon owner is missing."));return;}
 const auto Grant=FDWCombatEquipmentTransaction::GrantStarterBatch(*Weapon,Starter,S->GetPlaythroughId(),S->GetStarterGrantState(),DWStarterPolicy::BasicTools);
 if(!Grant.bCommittedThisCall&&!Grant.bAlreadyGranted)
 {UE_LOG(LogTemp,Error,TEXT("LANG01 initial acquisition failed; no partial starter grant accepted."));return;}
 HUD->SetSessionStarted(true);HUD->ClosePanels();PC->SetPause(false);
 if(!UDWLanguageUIBridgeComponent::Find(PC))
 {auto* Bridge=NewObject<UDWLanguageUIBridgeComponent>(PC,TEXT("LanguageUIBridge"));PC->AddInstanceComponent(Bridge);Bridge->RegisterComponent();}
 UDWGatheringComponent::Notify(P,TEXT("语言综合试玩：F交互，G查看文字，~图鉴；背包使用武器/精华，角色页学习天赋。新数值为可调试玩值。"),TEXT("Language trial: F interact, G read, ~ journal. Use weapons/essences from Inventory; learn talents on Character. New values are adjustable trial defaults."));
 for(TActorIterator<ADWLanguageCarrier> It(GetWorld());It;++It)It->RefreshCommittedPresentation();
 UE_LOG(LogTemp,Display,TEXT("LANG01_SESSION_READY: isolated playthrough %s; %d starter rows; no formal slot."),*S->GetPlaythroughId(),Starter.Num());
}
