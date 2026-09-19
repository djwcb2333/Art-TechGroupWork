#include "DWGameplayHUD.h"
#include "DWWorldEvent.h"
#include "DWUserSettings.h"
#include "DWLoadingTransition.h"
#include "DWGameplayWidget.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWLocalizationLibrary.h"
#include "DWAudioLibrary.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "TimerManager.h"

ADWGameplayHUD::ADWGameplayHUD()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;PrimaryActorTick.TickInterval=.15f;
}
void ADWGameplayHUD::BeginPlay()
{
    Super::BeginPlay();auto* PC=GetOwningPlayerController();if(!PC||!PC->IsLocalController())return;
    if(!GameUIFont)GameUIFont=LoadObject<UFont>(nullptr,TEXT("/Game/DoughWorld/UI/Shared/Fonts/F_DWHandDrawn.F_DWHandDrawn"));
    if(!WidgetClass)WidgetClass=LoadClass<UDWGameplayWidget>(nullptr,TEXT("/Game/DoughWorld/UI/WBP_DWGameplay.WBP_DWGameplay_C"));
    if(WidgetClass)
    {
        GameplayWidget=CreateWidget<UDWGameplayWidget>(PC,WidgetClass);
        if(GameplayWidget){GameplayWidget->InitializeScreen(this);GameplayWidget->AddToViewport(50);}
    }
    else UE_LOG(LogTemp,Error,TEXT("DoughWorld UI: WBP_DWGameplay is missing. Run DWUIAuthoringLibrary.CreatePrototypeUIAssets in the editor."));
    const auto* GI=Cast<UDWGameInstance>(GetGameInstance());
    if(bShowTitleOnStart||!GI||!GI->HasActiveSlot())
    {
        ShowTitle();
        // Let the possessed pawn supply one rendered camera frame before applying
        // the menu pause policy. UI blocks gameplay input during initialization.
        PC->SetPause(false);
        const TWeakObjectPtr<ADWGameplayHUD> WeakHUD(this);
        GetWorld()->GetTimerManager().SetTimerForNextTick([WeakHUD]()
        {
            if(!WeakHUD.IsValid())return;
            WeakHUD->GetWorld()->GetTimerManager().SetTimerForNextTick([WeakHUD]()
            {
                if(!WeakHUD.IsValid()||WeakHUD->bSessionStarted)return;
                const EDWMenuPage Page=WeakHUD->MenuPage;
                if(Page==EDWMenuPage::Title||Page==EDWMenuPage::SaveSlots||Page==EDWMenuPage::Settings)
                    if(auto* OwnerPC=WeakHUD->GetOwningPlayerController())OwnerPC->SetPause(WeakHUD->ShouldPauseWorldForMenu(Page));
            });
        });
    }
    else{bSessionStarted=true;ShowMenu(EDWMenuPage::None);}
}
void ADWGameplayHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    if(GameplayWidget)GameplayWidget->RemoveFromParent();GameplayWidget=nullptr;
    StopMusic();
    Super::EndPlay(Reason);
}
void ADWGameplayHUD::Tick(float Dt)
{
    Super::Tick(Dt);const auto* P=GetOwningPlayerController()?Cast<ADWPlayerCharacter>(GetOwningPlayerController()->GetPawn()):nullptr;
    if(bSessionStarted&&P&&P->IsDead()&&MenuPage!=EDWMenuPage::Defeat&&MenuPage!=EDWMenuPage::SaveSlots)ShowDefeat();
}
bool ADWGameplayHUD::IsBlockingGameplay()const{return MenuPage!=EDWMenuPage::None||(GetGameInstance()&&GetGameInstance()->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive());}
void ADWGameplayHUD::ToggleInventory(){if(!bSessionStarted||MenuPage==EDWMenuPage::Defeat||MenuPage==EDWMenuPage::Title||MenuPage==EDWMenuPage::SaveSlots)return;ShowMenu(MenuPage==EDWMenuPage::Inventory?EDWMenuPage::None:EDWMenuPage::Inventory);}
void ADWGameplayHUD::ToggleCrafting(){if(!bSessionStarted||MenuPage==EDWMenuPage::Defeat||MenuPage==EDWMenuPage::Title||MenuPage==EDWMenuPage::SaveSlots)return;ShowMenu(MenuPage==EDWMenuPage::Crafting?EDWMenuPage::None:EDWMenuPage::Crafting);}
void ADWGameplayHUD::TogglePause()
{
    if(MenuPage==EDWMenuPage::Settings){ReturnFromSettings();return;}if(MenuPage==EDWMenuPage::SaveSlots){ShowTitle();return;}
    if(MenuPage==EDWMenuPage::Title||MenuPage==EDWMenuPage::Defeat)return;ShowMenu(MenuPage==EDWMenuPage::None?EDWMenuPage::Pause:EDWMenuPage::None);
}
void ADWGameplayHUD::ShowTitle(){bSessionStarted=false;ShowMenu(EDWMenuPage::Title);}
void ADWGameplayHUD::ShowDefeat(){ShowMenu(EDWMenuPage::Defeat);}
void ADWGameplayHUD::ClosePanels(){ShowMenu(EDWMenuPage::None);}
void ADWGameplayHUD::SetSessionStarted(bool Value){bSessionStarted=Value;}
void ADWGameplayHUD::OpenSettings(){SettingsReturnPage=MenuPage;ShowMenu(EDWMenuPage::Settings);}
void ADWGameplayHUD::ReturnFromSettings(){ShowMenu(SettingsReturnPage);}
void ADWGameplayHUD::RefreshPanels(){if(GameplayWidget)GameplayWidget->RefreshFromHUD(true);}
bool ADWGameplayHUD::ShouldPauseWorldForMenu(EDWMenuPage Page) const
{
    return Page==EDWMenuPage::Title||Page==EDWMenuPage::SaveSlots||Page==EDWMenuPage::Pause||Page==EDWMenuPage::Settings;
}
void ADWGameplayHUD::ShowMenu(EDWMenuPage Page)
{
    if(UDWWorldEventSubsystem::IsPlaying(this))return;
    const EDWMenuPage PreviousPage=MenuPage;
    MenuPage=Page;
    const bool bPause=ShouldPauseWorldForMenu(Page);
    if(auto* PC=GetOwningPlayerController())
    {
        if(Page!=EDWMenuPage::None)
            if(auto* Player=Cast<ADWPlayerCharacter>(PC->GetPawn()))Player->CancelHarvestInteraction();
        PC->SetPause(bPause);PC->bShowMouseCursor=true;
        if(Page==EDWMenuPage::None)
        {
            FInputModeGameOnly Mode;Mode.SetConsumeCaptureMouseDown(false);PC->SetInputMode(Mode);
            if(FSlateApplication::IsInitialized())FSlateApplication::Get().SetAllUserFocusToGameViewport();
        }
        else
        {
            FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            if(GameplayWidget)Mode.SetWidgetToFocus(GameplayWidget->TakeWidget());PC->SetInputMode(Mode);
        }
    }
    if(GameplayWidget){GameplayWidget->ApplyMenuPage(Page);GameplayWidget->RefreshFromHUD(true);}UpdateMusic();
    if(PreviousPage!=Page)
    {
        PlayPanelTransitionSound(PreviousPage,false);
        PlayPanelTransitionSound(Page,true);
    }
}
void ADWGameplayHUD::ShowToast(const FText& Text){Toast=Text;ToastUntil=FPlatformTime::Seconds()+4.;}
FText ADWGameplayHUD::GetToast()const{return FPlatformTime::Seconds()<ToastUntil?UDWLocalizationLibrary::TranslateLabel(this,Toast):FText();}
void ADWGameplayHUD::PlayClickSound(){auto* GI=Cast<UDWGameInstance>(GetGameInstance());UDWAudioLibrary::PlayEvent(this,GI?GI->GetConfig():nullptr,EDWAudioEvent::UIClick);}
void ADWGameplayHUD::PlayPanelTransitionSound(EDWMenuPage Page,bool bOpening)
{
    if(Page==EDWMenuPage::None)return;
    auto* GI=Cast<UDWGameInstance>(GetGameInstance());const auto* Config=GI?GI->GetConfig():nullptr;if(!Config)return;
    EDWAudioEvent Event=bOpening?EDWAudioEvent::MenuOpen:EDWAudioEvent::MenuClose;
    switch(Page)
    {
        case EDWMenuPage::Title:Event=bOpening?EDWAudioEvent::TitleOpen:EDWAudioEvent::TitleClose;break;
        case EDWMenuPage::SaveSlots:Event=bOpening?EDWAudioEvent::SaveSlotsOpen:EDWAudioEvent::SaveSlotsClose;break;
        case EDWMenuPage::Inventory:Event=bOpening?EDWAudioEvent::InventoryOpen:EDWAudioEvent::InventoryClose;break;
        case EDWMenuPage::Crafting:Event=bOpening?EDWAudioEvent::CraftingOpen:EDWAudioEvent::CraftingClose;break;
        case EDWMenuPage::Pause:Event=bOpening?EDWAudioEvent::PauseOpen:EDWAudioEvent::PauseClose;break;
        case EDWMenuPage::Settings:Event=bOpening?EDWAudioEvent::SettingsOpen:EDWAudioEvent::SettingsClose;break;
        case EDWMenuPage::Defeat:Event=bOpening?EDWAudioEvent::DefeatOpen:EDWAudioEvent::DefeatClose;break;
        default:break;
    }
    // Choose one cue for each event; a configured panel cue never also plays its fallback.
    if(!UDWAudioLibrary::GetEventSound(Config,Event))Event=bOpening?EDWAudioEvent::MenuOpen:EDWAudioEvent::MenuClose;
    UDWAudioLibrary::PlayEvent(this,Config,Event);
}
void ADWGameplayHUD::UpdateMusic()
{
    auto* GI=Cast<UDWGameInstance>(GetGameInstance());const auto* C=GI?GI->GetConfig():nullptr;USoundBase* Desired=C?(bSessionStarted?C->GameplayBGM.Get():C->MenuBGM.Get()):nullptr;
    if(!bSessionStarted&&!ShouldPlayMenuMusic())Desired=nullptr;
    if(CurrentMusic==Desired&&MusicComponent)return;
    StopMusic();CurrentMusic=Desired;
    if(Desired)
    {
        // Set the UI flag before playback so the first frame also plays in a paused menu.
        MusicComponent=UGameplayStatics::CreateSound2D(this,Desired,C->MusicVolume,1.f,0.f,nullptr,false,false);
        if(auto* S=UDWUserSettings::Resolve(this))S->RouteAudio(MusicComponent,EDWSoundCategory::Music);
        if(MusicComponent){MusicComponent->SetUISound(true);MusicComponent->OnAudioFinished.AddDynamic(this,&ADWGameplayHUD::HandleMusicFinished);MusicComponent->Play();}
    }
}
void ADWGameplayHUD::StopMusic()
{
    if(MusicComponent){MusicComponent->OnAudioFinished.RemoveAll(this);MusicComponent->Stop();MusicComponent->DestroyComponent();}
    MusicComponent=nullptr;CurrentMusic=nullptr;
}
void ADWGameplayHUD::HandleMusicFinished()
{
    if(MusicComponent&&CurrentMusic&&!IsActorBeingDestroyed()&&(bSessionStarted||ShouldPlayMenuMusic()))MusicComponent->Play();
}
