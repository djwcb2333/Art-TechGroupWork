#include "DWGameplayHUD.h"
#include "DWWorldTargetIndicator.h"
#include "UObject/StrongObjectPtr.h"
#include "DWWorldMapScreen.h"
#include "DWWorldMapEntry.h"
#include "DWMinimap.h"
#include "DWOpeningPrototype.h"
#include "EngineUtils.h"
#include "DWLanguageUIBridgeComponent.h"
#include "DWCombatRules.h"
#include "DWWorldEvent.h"
#include "DWUserSettings.h"
#include "DWLoadingTransition.h"
#include "DWGameplayWidget.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerController.h"
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
    WorldTargetIndicator=CreateWidget<UDWWorldTargetIndicator>(PC,UDWWorldTargetIndicator::StaticClass());
    if(WorldTargetIndicator){WorldTargetIndicator->InitializeIndicator(this);WorldTargetIndicator->AddToPlayerScreen(49);}
    WorldMapEntry=CreateWidget<UDWWorldMapEntry>(PC,UDWWorldMapEntry::StaticClass());
    if(WorldMapEntry){WorldMapEntry->InitializeEntry(this);WorldMapEntry->AddToPlayerScreen(56);}
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
    if(WorldTargetIndicator)WorldTargetIndicator->RemoveFromParent();WorldTargetIndicator=nullptr;
    if(WorldMapEntry)WorldMapEntry->RemoveFromParent();WorldMapEntry=nullptr;
    if(WorldMapScreen)WorldMapScreen->CloseMapView();WorldMapScreen=nullptr;
    if(auto* PC=Cast<ADWPlayerController>(GetOwningPlayerController());PC&&PC->OwnsWorldMapPause())PC->EndWorldMapPause();
    if(GameplayWidget)GameplayWidget->RemoveFromParent();GameplayWidget=nullptr;
    bMusicEnding=true;StopCombatMusic(false);ResetOpeningMusicLease();StopMusic();
    Super::EndPlay(Reason);
}
void ADWGameplayHUD::Tick(float Dt)
{
    if(OpeningMusicLease.IsValid()&&!HasOpeningMusicOwner()){ResetOpeningMusicLease();UpdateMusic();}
    TickOpeningMusicFade();TickCombatMusic();
    if(WorldMapEntry)
    {
        auto* PC=Cast<ADWPlayerController>(GetOwningPlayerController());
        APawn* MapPawn=nullptr;if(PC)MapPawn=PC->GetPawn();
        auto* Mini=MapPawn?MapPawn->FindComponentByClass<UDWMinimapComponent>():nullptr;
        const bool bVisible=Mini&&Mini->IsMinimapVisible()&&MenuPage==EDWMenuPage::None&&PC->CanOpenWorldMap();
        WorldMapEntry->UpdateEntry(bVisible,Mini?Mini->Diameter:0.f,Mini?Mini->BottomRightMargin:FVector2D::ZeroVector);
    }
    Super::Tick(Dt);const auto* P=GetOwningPlayerController()?Cast<ADWPlayerCharacter>(GetOwningPlayerController()->GetPawn()):nullptr;
    if(bSessionStarted&&P&&P->IsDead()&&MenuPage!=EDWMenuPage::Defeat&&MenuPage!=EDWMenuPage::SaveSlots)ShowDefeat();
}
bool ADWGameplayHUD::IsBlockingGameplay()const{return MenuPage!=EDWMenuPage::None||(GetGameInstance()&&GetGameInstance()->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive());}
bool ADWGameplayHUD::IsHandbookPage(EDWMenuPage P){return P==EDWMenuPage::Character||P==EDWMenuPage::Inventory||P==EDWMenuPage::Crafting||P==EDWMenuPage::Building||P==EDWMenuPage::LanguageJournal;}
void ADWGameplayHUD::ToggleHandbook(EDWMenuPage P)
{
 if(P==EDWMenuPage::Building)P=EDWMenuPage::Crafting; // Legacy callers open the real recipe page.
 if(!bSessionStarted||!IsHandbookPage(P)||(MenuPage!=EDWMenuPage::None&&!IsHandbookPage(MenuPage))||UDWWorldEventSubsystem::IsPlaying(this))return;
 if(MenuPage==P)if(auto* Bridge=UDWLanguageUIBridgeComponent::Find(GetOwningPlayerController());Bridge&&Bridge->TryCloseBookFromUser())return;
 ShowMenu(MenuPage==P?EDWMenuPage::None:P);
}
void ADWGameplayHUD::CycleHandbook(int32 Direction)
{
 if(!bSessionStarted||!IsHandbookPage(MenuPage))return;
 const EDWMenuPage Pages[]={EDWMenuPage::Character,EDWMenuPage::Inventory,EDWMenuPage::Crafting,EDWMenuPage::LanguageJournal};
 const int32 Count=UE_ARRAY_COUNT(Pages);
 for(int32 I=0;I<Count;++I)if(Pages[I]==MenuPage){ShowMenu(Pages[(I+(Direction<0?Count-1:1))%Count]);break;}
}
void ADWGameplayHUD::ToggleInventory(){ToggleHandbook(EDWMenuPage::Inventory);}
void ADWGameplayHUD::ToggleCrafting(){ToggleHandbook(EDWMenuPage::Crafting);}
void ADWGameplayHUD::ToggleWorldMap()
{
    auto* PC=Cast<ADWPlayerController>(GetOwningPlayerController());
    if(!PC||!PC->IsLocalController())return;
    if(MenuPage==EDWMenuPage::WorldMap){if(WorldMapScreen)WorldMapScreen->RequestClose();return;}
    if(!bSessionStarted||MenuPage!=EDWMenuPage::None||UDWWorldEventSubsystem::IsPlaying(this)||!PC->CanOpenWorldMap())return;
    if(!PC->BeginWorldMapPause())return;
    UDWWorldMapScreen* Screen=CreateWidget<UDWWorldMapScreen>(PC,UDWWorldMapScreen::StaticClass());
    FString Error;
    if(!Screen||!Screen->InitializeMap(this,Error))
    {if(Screen)Screen->CloseMapView();PC->EndWorldMapPause();ShowToast(FText::FromString(Error.IsEmpty()?TEXT("Map unavailable"):Error));return;}
    WorldMapScreen=Screen;Screen->AddToViewport(60);
    ShowMenu(EDWMenuPage::WorldMap);
    if(MenuPage!=EDWMenuPage::WorldMap){Screen->CloseMapView();WorldMapScreen=nullptr;PC->EndWorldMapPause();return;}
    if(!PC->MustPreserveWorldMapExternalInput())Screen->SetKeyboardFocus();
}
void ADWGameplayHUD::FinishWorldMapClose(UDWWorldMapScreen* Screen)
{
    if(!Screen||WorldMapScreen!=Screen)return;
    if(MenuPage==EDWMenuPage::WorldMap)ShowMenu(EDWMenuPage::None);
    if(WorldMapScreen!=Screen)return; // Normal path already released our map.
    // A journal/world-event guard can decline ShowMenu. Dispose only this map
    // and its owned pause; do not force release external input/pause leases.
    const TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
    const TWeakObjectPtr<APlayerController> WeakOwner(GetOwningPlayerController());
    const TStrongObjectPtr<ADWGameplayHUD> KeepAlive(this);
    WorldMapScreen=nullptr;Screen->CloseMapView();
    if(!IsValid(this)||!WeakWorld.IsValid()||WeakWorld->bIsTearingDown||GetWorld()!=WeakWorld.Get()||WorldMapScreen||!WeakOwner.IsValid()||GetOwningPlayerController()!=WeakOwner.Get())return;
    if(MenuPage==EDWMenuPage::WorldMap)
    {
        MenuPage=EDWMenuPage::None;
        if(GameplayWidget){GameplayWidget->ApplyMenuPage(MenuPage);GameplayWidget->RefreshFromHUD(true);}
    }
    if(auto* PC=Cast<ADWPlayerController>(GetOwningPlayerController()))
    {
        const bool Preserve=PC->MustPreserveWorldMapExternalInput();
        if(PC->OwnsWorldMapPause())PC->EndWorldMapPause();
        if(ShouldPauseWorldForMenu(MenuPage)&&!PC->IsPaused())PC->SetPause(true);
        const auto* Bridge=UDWLanguageUIBridgeComponent::Find(PC);
        if(MenuPage==EDWMenuPage::None&&!PC->IsPaused()&&!Preserve&&!UDWWorldEventSubsystem::IsPlaying(this)&&!(Bridge&&Bridge->HasModalLease()))
        {
            PC->PrepareCombatInputForSimulation();
            FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->ApplyDWGameAndUI(Mode);
            if(FSlateApplication::IsInitialized())FSlateApplication::Get().SetAllUserFocusToGameViewport();
        }
    }
}
void ADWGameplayHUD::TogglePause()
{
    if(MenuPage==EDWMenuPage::Settings){ReturnFromSettings();return;}if(MenuPage==EDWMenuPage::WorldMap){OpenSettings();return;}if(MenuPage==EDWMenuPage::SaveSlots){ShowTitle();return;}
    if(MenuPage==EDWMenuPage::Title||MenuPage==EDWMenuPage::Defeat)return;
    if(IsHandbookPage(MenuPage))if(auto* Bridge=UDWLanguageUIBridgeComponent::Find(GetOwningPlayerController());Bridge&&Bridge->TryCloseBookFromUser())return;
    ShowMenu(MenuPage==EDWMenuPage::None?EDWMenuPage::Pause:EDWMenuPage::None);
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
    return IsHandbookPage(Page)||Page==EDWMenuPage::Title||Page==EDWMenuPage::SaveSlots||Page==EDWMenuPage::Pause||Page==EDWMenuPage::Settings||Page==EDWMenuPage::WorldMap;
}
void ADWGameplayHUD::ShowMenu(EDWMenuPage Page)
{
    if(Page==EDWMenuPage::Building)Page=EDWMenuPage::Crafting; // Preserve enum / Blueprint compatibility.
    // The bridge owns journal transfers, origin return and terminal session cleanup.
    if(auto* Bridge=UDWLanguageUIBridgeComponent::Find(GetOwningPlayerController());Bridge&&Bridge->TryHandleHandbookMenu(Page))return;
    if(UDWWorldEventSubsystem::IsPlaying(this))return;
    const EDWMenuPage PreviousPage=MenuPage;
    // Map, settings overlay and unrelated pages form one state machine.
    const bool bReturningToMap=PreviousPage==EDWMenuPage::Settings&&SettingsReturnPage==EDWMenuPage::WorldMap&&Page==EDWMenuPage::WorldMap;
    if(Page==EDWMenuPage::WorldMap&&!WorldMapScreen)return;
    if(PreviousPage==EDWMenuPage::WorldMap&&Page!=EDWMenuPage::None&&Page!=EDWMenuPage::Settings&&Page!=EDWMenuPage::Defeat)return;
    const bool bLeavingMap=WorldMapScreen&&Page!=EDWMenuPage::WorldMap&&!(Page==EDWMenuPage::Settings&&PreviousPage==EDWMenuPage::WorldMap);
    if(WorldMapScreen&&(PreviousPage==EDWMenuPage::WorldMap||bReturningToMap))WorldMapScreen->SetSettingsOverlayActive(Page==EDWMenuPage::Settings);
    if(bLeavingMap){WorldMapScreen->CloseMapView();WorldMapScreen=nullptr;}
    MenuPage=Page;
    const bool bPause=ShouldPauseWorldForMenu(Page);
    if(auto* PC=GetOwningPlayerController())
    {
        if(Page!=EDWMenuPage::None)
            if(auto* Player=Cast<ADWPlayerCharacter>(PC->GetPawn()))Player->CancelHarvestInteraction();
        auto* CombatPC=Cast<ADWPlayerController>(PC);
        bool bPreserveExternalInput=CombatPC&&CombatPC->OwnsWorldMapPause()&&CombatPC->MustPreserveWorldMapExternalInput();
        if(bPause&&CombatPC)CombatPC->BeginCombatMenuSuspension();
        if(CombatPC&&CombatPC->OwnsWorldMapPause())
        {if(bLeavingMap){CombatPC->EndWorldMapPause();bPreserveExternalInput|=PC->IsPaused();if(bPause&&!PC->IsPaused())PC->SetPause(true);}}
        else PC->SetPause(bPause);
        if(!bPreserveExternalInput)PC->bShowMouseCursor=true;
        // Reconcile real paused releases on this call stack, before any resumed world advance.
        if(!bPause&&CombatPC&&!PC->IsPaused())CombatPC->PrepareCombatInputForSimulation();
        if(bPreserveExternalInput){}
        else if(Page==EDWMenuPage::None)
        {
            FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);if(CombatPC)CombatPC->ApplyDWGameAndUI(Mode);else PC->SetInputMode(Mode);
            if(FSlateApplication::IsInitialized()&&(!UDWLanguageUIBridgeComponent::Find(PC)||!UDWLanguageUIBridgeComponent::Find(PC)->HasModalLease()))FSlateApplication::Get().SetAllUserFocusToGameViewport();
        }
        else
        {
            FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            const TSharedPtr<SWidget> Focus=GameplayWidget?GameplayWidget->TakeWidget():TSharedPtr<SWidget>();
            Mode.SetWidgetToFocus(Focus);if(CombatPC)CombatPC->ApplyDWGameAndUI(Mode,Focus);else PC->SetInputMode(Mode);
        }
    }
    if(GameplayWidget){GameplayWidget->ApplyMenuPage(Page);GameplayWidget->RefreshFromHUD(true);}UpdateMusic();
    if(MenuPage==EDWMenuPage::WorldMap&&WorldMapScreen)
        if(const auto* PC=Cast<ADWPlayerController>(GetOwningPlayerController());PC&&!PC->MustPreserveWorldMapExternalInput())WorldMapScreen->SetKeyboardFocus();
    if(PreviousPage!=Page&&!(IsHandbookPage(PreviousPage)&&IsHandbookPage(Page)))
    {
        PlayPanelTransitionSound(PreviousPage,false);
        PlayPanelTransitionSound(Page,true);
    }
}
void ADWGameplayHUD::ShowToast(const FText& Text)
{Toast=Text;ToastUntil=FPlatformTime::Seconds()+4.;ImportantToastUntil=Text.IsEmpty()?0.:ToastUntil;}
void ADWGameplayHUD::ShowCombatToast(const FText& Text)
{
    const auto* GI=GetGameInstance<UDWGameInstance>();double Seconds=0.;FString Error;
    if(!GI||!GI->GetCombatRules()||!GI->GetCombatRules()->TryGetNumber(TEXT("hud.message_duration_s"),EDWCombatRuleUnit::Seconds,Seconds,Error)||Seconds<=0.)return;
    const double Now=FPlatformTime::Seconds();
    if(Text.IsEmpty()||Now<ImportantToastUntil)return;
    if((Now<ToastUntil&&Toast.EqualTo(Text))||(LastCombatToastAt>=0.&&Now-LastCombatToastAt<Seconds&&LastCombatToast.EqualTo(Text)))return;
    LastCombatToast=Text;LastCombatToastAt=Now;Toast=Text;ToastUntil=Now+Seconds;
}
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
bool ADWGameplayHUD::IsOpeningMusicWorld()const
{
    UWorld* World=GetWorld();if(!World)return false;
    if(Cast<ADWOpeningPrototypeGameMode>(World->GetAuthGameMode()))return true;
    for(TActorIterator<ADWOpeningPrototype> It(World);It;++It)if(!It->IsActorBeingDestroyed())return true;
    return false;
}
bool ADWGameplayHUD::HasOpeningMusicOwner()const
{
    const AActor* LeaseOwner=OpeningMusicOwner.Get();
    return OpeningMusicLease.IsValid()&&IsValid(LeaseOwner)&&!LeaseOwner->IsActorBeingDestroyed()&&LeaseOwner->GetWorld()==GetWorld();
}
void ADWGameplayHUD::ResetOpeningMusicLease()
{
    OpeningMusicLease.Invalidate();OpeningMusicOwner.Reset();OpeningMusicSound=nullptr;
    bOpeningMusicFading=false;bOpeningMusicFinishedPending=false;OpeningMusicGain=1.f;
    OpeningMusicVolumeMultiplier=OpeningMusicPitchMultiplier=1.f;
}
FGuid ADWGameplayHUD::AcquireOpeningMusic(AActor* InOwner,USoundBase* Sound,float VolumeMultiplier,float PitchMultiplier)
{
    const auto* PC=GetOwningPlayerController();
    if(bMusicEnding||IsActorBeingDestroyed()||!IsValid(InOwner)||InOwner->IsActorBeingDestroyed()||InOwner->GetWorld()!=GetWorld()||!PC||!PC->IsLocalController()||!FMath::IsFinite(VolumeMultiplier)||!FMath::IsFinite(PitchMultiplier)||VolumeMultiplier<0.f||PitchMultiplier<=0.f)return FGuid();
    if(HasOpeningMusicOwner()&&OpeningMusicOwner.Get()!=InOwner)return FGuid();
    if(CombatMusicComponent){StopCombatMusic(false);StopMusic();}
    if(bOpeningMusicFinishedPending){bOpeningMusicFinishedPending=false;StopMusic();}
    bOpeningMusicFading=false;OpeningMusicGain=1.f;
    if(!HasOpeningMusicOwner()){ResetOpeningMusicLease();OpeningMusicLease=FGuid::NewGuid();OpeningMusicOwner=InOwner;}
    OpeningMusicSound=Sound;OpeningMusicVolumeMultiplier=VolumeMultiplier;OpeningMusicPitchMultiplier=PitchMultiplier;
    UpdateMusic();return OpeningMusicLease;
}
bool ADWGameplayHUD::ReleaseOpeningMusic(const FGuid& Lease)
{
    if(!Lease.IsValid()||Lease!=OpeningMusicLease)return false;
    ResetOpeningMusicLease();if(!bMusicEnding)UpdateMusic();return true;
}
void ADWGameplayHUD::UpdateMusic()
{
    if(bMusicEnding||IsActorBeingDestroyed())return;
    if(OpeningMusicLease.IsValid()&&!HasOpeningMusicOwner())ResetOpeningMusicLease();
    const bool bOpening=HasOpeningMusicOwner();
    auto* GI=Cast<UDWGameInstance>(GetGameInstance());const auto* C=GI?GI->GetConfig():nullptr;
    USoundBase* Desired=bOpening?OpeningMusicSound.Get():(IsOpeningMusicWorld()?nullptr:(C?(bSessionStarted?C->GameplayBGM.Get():C->MenuBGM.Get()):nullptr));
    if(!bOpening&&!bSessionStarted&&!ShouldPlayMenuMusic())Desired=nullptr;
    const float Volume=(C?C->MusicVolume:1.f)*(bOpening?OpeningMusicVolumeMultiplier:1.f);
    const float Pitch=bOpening?OpeningMusicPitchMultiplier:1.f;
    if(CurrentMusic==Desired&&MusicComponent){MusicComponent->SetVolumeMultiplier(Volume*(bOpeningMusicFading?OpeningMusicGain:1.f));MusicComponent->SetPitchMultiplier(Pitch);return;}
    StopMusic();CurrentMusic=Desired;
    if(Desired)
    {
        // One component and the existing Music category; never play OpeningBGM beside GameplayBGM.
        MusicComponent=UGameplayStatics::CreateSound2D(this,Desired,Volume,Pitch,0.f,nullptr,false,false);
        if(auto* S=UDWUserSettings::Resolve(this))S->RouteAudio(MusicComponent,EDWSoundCategory::Music);
        if(MusicComponent)
        {
            MusicComponent->SetUISound(true);MusicComponent->OnAudioFinished.AddDynamic(this,&ADWGameplayHUD::HandleMusicFinished);
            // When changing profiles during combat, start the new base track only after combat ends.
            if(CombatMusicComponent)bBackgroundNotStarted=true;else MusicComponent->Play();
        }
    }
}
void ADWGameplayHUD::StopMusic()
{
    if(MusicComponent){MusicComponent->OnAudioFinished.RemoveAll(this);MusicComponent->Stop();MusicComponent->DestroyComponent();}
    MusicComponent=nullptr;CurrentMusic=nullptr;bOpeningMusicFinishedPending=false;bBackgroundPausedForCombat=false;bBackgroundNotStarted=false;
}
void ADWGameplayHUD::HandleMusicFinished()
{
    if(OpeningMusicLease.IsValid()&&!HasOpeningMusicOwner()){ResetOpeningMusicLease();UpdateMusic();return;}
    // A naturally completed opening cue must not loop back to its start during the exit fade.
    if(bOpeningMusicFading&&HasOpeningMusicOwner())
    {bOpeningMusicFading=false;bOpeningMusicFinishedPending=true;OpeningMusicSound=nullptr;return;}
    // A queued natural completion can arrive immediately after a combat pause request.
    if(CombatMusicComponent||bBackgroundPausedForCombat||bBackgroundNotStarted)
    {bBackgroundPausedForCombat=false;bBackgroundNotStarted=true;return;}
    if(!bMusicEnding&&MusicComponent&&CurrentMusic&&!IsActorBeingDestroyed()&&(HasOpeningMusicOwner()||(!IsOpeningMusicWorld()&&(bSessionStarted||ShouldPlayMenuMusic()))))MusicComponent->Play();
}

// Music reads already committed enemy engagement; it does not trigger detection, attacks or damage.
bool ADWEnemyCharacter::IsEngagedWithPlayerForMusic(const ADWPlayerCharacter* Player) const
{
    if(!IsValid(Player)||Player->IsDead()||Player->IsActorBeingDestroyed()||!IsAlive()||IsActorBeingDestroyed()||
       bCombatWorldStaged||bPlayerDeathReturn||TargetPawn.Get()!=Player)return false;
    if(CombatProfile==EDWEnemyCombatProfile::Legacy)
        return State==EDWEnemyState::Alert||State==EDWEnemyState::Chase||State==EDWEnemyState::Attack;
    return IsFormalCombatReady()&&FormalMemory.bEngaged;
}

float ADWGameplayHUD::GetMusicFadeSeconds() const
{
    const auto* GI=Cast<UDWGameInstance>(GetGameInstance());
    const auto* Config=GI?const_cast<UDWGameInstance*>(GI)->GetConfig():nullptr;
    const float S=Config?Config->CombatBGMFadeOutSeconds:1.5f;
    return FMath::IsFinite(S)?FMath::Clamp(S,0.f,30.f):1.5f;
}
void ADWGameplayHUD::BeginCombatFade(float Target,float Seconds)
{
    CombatFadeFrom=CombatMusicGain;CombatFadeTo=FMath::Clamp(Target,0.f,1.f);
    CombatFadeStart=FPlatformTime::Seconds();CombatFadeDuration=FMath::Max(0.f,Seconds);
    bCombatMusicFading=true;
}
void ADWGameplayHUD::StopCombatMusic(bool bResumeBackground)
{
    bCombatMusicFading=false;bCombatRestartPending=false;bCombatFinishedDuringFade=false;bCombatFailurePending=false;CombatMusicGain=1.f;
    if(CombatMusicComponent)
    {
        CombatMusicComponent->OnAudioFinished.RemoveAll(this);
        CombatMusicComponent->Stop();CombatMusicComponent->DestroyComponent();CombatMusicComponent=nullptr;
    }
    if(bResumeBackground&&MusicComponent)
    {
        if(bBackgroundNotStarted){MusicComponent->SetPaused(false);MusicComponent->Play();}
        else if(bBackgroundPausedForCombat)MusicComponent->SetPaused(false);
    }
    bBackgroundPausedForCombat=false;bBackgroundNotStarted=false;
}
void ADWGameplayHUD::HandleCombatMusicFinished()
{
    // Defer replay to the next HUD tick; never recurse inside an audio-finished callback.
    if(!bMusicEnding&&CombatMusicComponent)
    {
        const USoundBase* Sound=CombatMusicComponent->GetSound();
        const bool bLongSound=Sound&&FMath::IsFinite(Sound->GetDuration())&&Sound->GetDuration()>1.f;
        if(bLongSound&&FPlatformTime::Seconds()-CombatLastPlayStart<.2){bCombatFailurePending=true;return;}
        if(bCombatMusicFading&&CombatFadeTo<=0.f)bCombatFinishedDuringFade=true;else bCombatRestartPending=true;
    }
}
bool ADWGameplayHUD::FadeOpeningMusicToSilence(const FGuid& Lease)
{
    if(!Lease.IsValid()||Lease!=OpeningMusicLease||!HasOpeningMusicOwner())return false;
    if(!OpeningMusicSound||!MusicComponent){OpeningMusicSound=nullptr;return true;}
    if(bOpeningMusicFading)return true;
    OpeningMusicGain=1.f;OpeningFadeStart=FPlatformTime::Seconds();OpeningFadeDuration=GetMusicFadeSeconds();
    bOpeningMusicFading=true;return true;
}
void ADWGameplayHUD::TickOpeningMusicFade()
{
    if(bOpeningMusicFinishedPending){bOpeningMusicFinishedPending=false;StopMusic();return;}
    if(!bOpeningMusicFading)return;
    if(!HasOpeningMusicOwner()){bOpeningMusicFading=false;return;}
    const double Elapsed=FMath::Max(0.,FPlatformTime::Seconds()-OpeningFadeStart);
    OpeningMusicGain=OpeningFadeDuration>0.?float(FMath::Clamp(1.-Elapsed/OpeningFadeDuration,0.,1.)):0.f;
    if(MusicComponent)
    {
        auto* GI=Cast<UDWGameInstance>(GetGameInstance());const auto* C=GI?GI->GetConfig():nullptr;
        MusicComponent->SetVolumeMultiplier((C?C->MusicVolume:1.f)*OpeningMusicVolumeMultiplier*OpeningMusicGain);
    }
    if(OpeningMusicGain<=0.f){bOpeningMusicFading=false;OpeningMusicSound=nullptr;StopMusic();}
}
void ADWGameplayHUD::TickCombatMusic()
{
    UWorld* World=GetWorld();auto* PC=GetOwningPlayerController();
    if(bMusicEnding||!World||World->bIsTearingDown||!PC||!PC->IsLocalController())return;
    if(!bSessionStarted||HasOpeningMusicOwner()||IsOpeningMusicWorld())
    {
        if(CombatMusicComponent){StopCombatMusic(false);StopMusic();UpdateMusic();}
        return;
    }
    auto* GI=Cast<UDWGameInstance>(GetGameInstance());
    if(auto* Loading=GI?GI->GetSubsystem<UDWLoadingTransitionSubsystem>():nullptr;Loading&&Loading->IsTransitionActive())return;
    const auto* Player=Cast<ADWPlayerCharacter>(PC->GetPawn());
    const bool bDead=!IsValid(Player)||Player->IsDead()||Player->IsActorBeingDestroyed();
    bool bEngaged=false;
    // A paused world freezes enemy facts. Keep the last decision until play resumes, except death.
    if(!bDead&&World->IsPaused())bEngaged=CombatMusicComponent&&(!bCombatMusicFading||CombatFadeTo>0.f);
    else if(!bDead)
        for(TActorIterator<ADWEnemyCharacter> It(World);It;++It)
            if(It->IsEngagedWithPlayerForMusic(Player)){bEngaged=true;break;}
    auto* Config=GI?GI->GetConfig():nullptr;
    if(!bEngaged)bCombatStartBlocked=false;
    if(bCombatFailurePending)
    {
        StopCombatMusic(true);bCombatStartBlocked=bEngaged;
        UE_LOG(LogTemp,Warning,TEXT("Combat music failed to start or ended immediately; original music restored until the next encounter."));
        return;
    }
    // A natural completion may have arrived before the last enemy disengaged.
    // Never restart the track on this tick if combat has already ended.
    if(bCombatRestartPending&&!bEngaged){StopCombatMusic(true);return;}
    if(bCombatFinishedDuringFade)
    {
        bCombatFinishedDuringFade=false;
        if(!bEngaged){StopCombatMusic(true);return;}
        bCombatRestartPending=true; // A new engagement began after the outgoing clip ended.
    }
    if(bEngaged&&!bCombatStartBlocked&&!CombatMusicComponent&&Config&&IsValid(Config->CombatBGM))
    {
        UAudioComponent* NewAudio=UGameplayStatics::CreateSound2D(this,Config->CombatBGM,Config->MusicVolume,1.f,0.f,nullptr,false,false);
        if(NewAudio)
        {
            if(auto* Settings=UDWUserSettings::Resolve(this))Settings->RouteAudio(NewAudio,EDWSoundCategory::Music);
            NewAudio->SetUISound(true);NewAudio->OnAudioFinished.AddDynamic(this,&ADWGameplayHUD::HandleCombatMusicFinished);
            // A real pause preserves the decoder position; a zero-volume playing track would drift.
            if(MusicComponent&&!bBackgroundNotStarted){MusicComponent->SetPaused(true);bBackgroundPausedForCombat=true;}
            CombatMusicComponent=NewAudio;CombatMusicGain=1.f;bCombatMusicFading=false;CombatLastPlayStart=FPlatformTime::Seconds();NewAudio->Play();
        }
        else
        {
            bCombatStartBlocked=true;
            UE_LOG(LogTemp,Warning,TEXT("Combat music component unavailable; original music retained until the next encounter."));
        }
    }
    else if(bEngaged&&CombatMusicComponent&&bCombatMusicFading&&CombatFadeTo<=0.f)
        BeginCombatFade(1.f,FMath::Min(.25f,GetMusicFadeSeconds()));
    else if(!bEngaged&&CombatMusicComponent&&(!bCombatMusicFading||CombatFadeTo>0.f))
        BeginCombatFade(0.f,GetMusicFadeSeconds());
    if(!CombatMusicComponent)return;
    if(bCombatMusicFading)
    {
        const double Elapsed=FMath::Max(0.,FPlatformTime::Seconds()-CombatFadeStart);
        const float T=CombatFadeDuration>0.?float(FMath::Clamp(Elapsed/CombatFadeDuration,0.,1.)):1.f;
        CombatMusicGain=FMath::Lerp(CombatFadeFrom,CombatFadeTo,T);
        if(T>=1.f){bCombatMusicFading=false;if(CombatFadeTo<=0.f){StopCombatMusic(true);return;}}
    }
    CombatMusicComponent->SetVolumeMultiplier((Config?Config->MusicVolume:1.f)*CombatMusicGain);
    if(bCombatRestartPending)
    {
        bCombatRestartPending=false;CombatLastPlayStart=FPlatformTime::Seconds();CombatMusicComponent->Play();
    }
    // Play may fail without a finished delegate. Keep attempts bounded to one per encounter.
    if(CombatMusicComponent&&CombatMusicComponent->GetPlayState()==EAudioComponentPlayState::Stopped&&!bCombatFinishedDuringFade)
    {
        StopCombatMusic(true);bCombatStartBlocked=bEngaged;
        UE_LOG(LogTemp,Warning,TEXT("Combat music did not enter a playing state; original music restored until the next encounter."));
    }
}

void ADWGameplayHUD::NotifyItemAcquired(FName ItemId,int32 Amount)
{
 if(ItemId.IsNone()||Amount<=0)return;
 auto* GI=GetGameInstance<UDWGameInstance>();const auto* Config=GI?GI->GetConfig():nullptr;
 const auto* Definition=Config?Config->GetItemDefinition(ItemId):nullptr;
 const FText Name=Definition?UDWLocalizationLibrary::GetItemDisplayName(this,*Definition):FText::FromName(ItemId);
 PickupToast=FText::Format(FText::FromString(TEXT("{0} +{1}")),Name,Amount);
 PickupToastStarted=FPlatformTime::Seconds();
 const auto Safe=[](float V){return FMath::IsFinite(V)?FMath::Max(0.f,V):0.f;};
 PickupToastUntil=PickupToastStarted+Safe(C01PickupEnterSeconds)+Safe(C01PickupHoldSeconds)+Safe(C01PickupExitSeconds);
}
FText ADWGameplayHUD::GetPickupToast()const
{return FPlatformTime::Seconds()<PickupToastUntil?PickupToast:FText::GetEmpty();}

double ADWGameplayHUD::GetPickupToastAge()const{return FMath::Max(0.,FPlatformTime::Seconds()-PickupToastStarted);}
double ADWGameplayHUD::GetPickupToastRemaining()const{return FMath::Max(0.,PickupToastUntil-FPlatformTime::Seconds());}
double ADWGameplayHUD::GetToastRemaining()const{return FMath::Max(0.,ToastUntil-FPlatformTime::Seconds());}
