#include "DWLanguageUIBridgeComponent.h"
#include "DWPlayerController.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayHUD.h"
#include "DWGameplayWidget.h"
#include "DWLanguageSubsystem.h"
#include "DWGameInstance.h"
#include "DWLanguageCarrier.h"
#include "DWLanguageUIScreen.h"
#include "DWLanguageSubsystemV1Adapter.h"
#include "DWCharacterA01Component.h"
#include "DWGatheringComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWLocalizationLibrary.h"
#include "DWWorldEvent.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Input/Events.h"
#include "UObject/StrongObjectPtr.h"
class FDWLanguageModalState
{
public:
 FGuid Token;
 TWeakObjectPtr<UWorld> World;
 TWeakObjectPtr<ADWPlayerCharacter> Player;
 TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority;
 DWCombatCommit::FEntityLife Life;
 uint64 Epoch=0,LanguageGeneration=0;
 TWeakObjectPtr<ADWGameplayHUD> OwnerHUD;
 TWeakObjectPtr<UDWGameplayWidget> OwnerWidget;
 TWeakObjectPtr<UDWLanguageSubsystem> LanguageOwner;
 EDWLanguagePanel LanguagePanel=EDWLanguagePanel::None;
 FName LanguageContext,LanguageEntry;
 bool bUnderlyingPaused=false,bUnderlyingCursor=true;
 FCanUnpause UnderlyingCanUnpause;
 FInputModeGameAndUI UnderlyingMode;
 TWeakPtr<SWidget> UnderlyingFocus,ModalFocus;
};
UDWLanguageUIBridgeComponent::UDWLanguageUIBridgeComponent(){PrimaryComponentTick.bCanEverTick=false;ReturnBookPage=BookAuxReturnPage=RoutingBookPage=EDWMenuPage::None;}
UDWLanguageUIBridgeComponent::~UDWLanguageUIBridgeComponent()=default;
UDWLanguageUIBridgeComponent* UDWLanguageUIBridgeComponent::Find(const APlayerController* PC)
{return IsValid(PC)?PC->FindComponentByClass<UDWLanguageUIBridgeComponent>():nullptr;}
ADWPlayerController* UDWLanguageUIBridgeComponent::Controller()const{return Cast<ADWPlayerController>(GetOwner());}
UDWLanguageSubsystem* UDWLanguageUIBridgeComponent::Language()const
{const auto* PC=Controller();return PC&&PC->GetGameInstance()?PC->GetGameInstance()->GetSubsystem<UDWLanguageSubsystem>():nullptr;}
bool UDWLanguageUIBridgeComponent::HasModalLease()const{return Lease&&Lease->Token.IsValid();}
bool UDWLanguageUIBridgeComponent::CanOpenMenus()const
{
 const auto* PC=Controller();const auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
 if(!P||P->IsDead()||P->IsCombatRestoreInProgress()||UDWWorldEventSubsystem::IsPlaying(this))return false;
 const auto* A=P->FindComponentByClass<UDWCharacterA01Component>();return !A||!A->IsEnabled()||A->CanOpenMenus();
}
bool UDWLanguageUIBridgeComponent::CanUseShortcuts()const
{
 if(HasModalLease())return false;
 const auto* PC=Controller();const auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
 if(!P||P->IsDead()||P->IsCombatRestoreInProgress())return false;
 const auto* A=P->FindComponentByClass<UDWCharacterA01Component>();return !A||!A->IsEnabled()||A->CanUseShortcuts();
}
bool UDWLanguageUIBridgeComponent::IsTrialReady()const
{const auto* L=Language();return L&&L->IsReady();}
void UDWLanguageUIBridgeComponent::BeginPlay()
{
 Super::BeginPlay();
 if(auto* L=Language())L->OnPanelRequested.AddUniqueDynamic(this,&UDWLanguageUIBridgeComponent::HandlePanelRequested);
 const TWeakObjectPtr<UDWLanguageUIBridgeComponent> WeakThis(this);
 RealTimeTick=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis](float Dt)
 {return WeakThis.IsValid()&&WeakThis->TickRealTime(Dt);}),.1f);
}
void UDWLanguageUIBridgeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 bEnding=true;
 if(RealTimeTick.IsValid())FTSTicker::GetCoreTicker().RemoveTicker(RealTimeTick);RealTimeTick.Reset();
 if(auto* L=Language())L->OnPanelRequested.RemoveDynamic(this,&UDWLanguageUIBridgeComponent::HandlePanelRequested);
 CloseForLifecycle();Super::EndPlay(Reason);
}
void UDWLanguageUIBridgeComponent::Notice(const FText& Message)const
{if(!Message.IsEmpty())if(const auto* PC=Controller())if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->Notify(Message);}
bool UDWLanguageUIBridgeComponent::RequestJournal(FName Entry)
{
 if(!IsTrialReady())return false;if(!CanOpenMenus())return true;
 if(Screen&&Screen->IsLanguageUIActive()){Screen->OpenJournal(Entry);return true;}
 const TWeakObjectPtr<UDWLanguageUIBridgeComponent> Self(this);
 Language()->RequestJournal(Entry);
 // Broadcast is synchronous, but the presenter can reject activation. Only consume
 // HUD routing if the journal actually mounted on this still-current book.
 if(!Self.IsValid())return true;
 const bool bMounted=Self->IsBookBindingCurrent()&&Self->BookHUD.IsValid()&&Self->BookHUD->GetMenuPage()==EDWMenuPage::LanguageJournal;
 if(!bMounted)UE_LOG(LogTemp,Warning,TEXT("Journal request did not mount; presenting unavailable book state."));
 return bMounted;
}
bool UDWLanguageUIBridgeComponent::RequestTalents()
{
 if(!IsTrialReady()||!CanOpenMenus())return false;
 if(Screen&&Screen->IsLanguageUIActive())
  return Screen->IsJournalInBook()&&Screen->OpenTalentsFromBook();
 const TWeakObjectPtr<UDWLanguageUIBridgeComponent> Self(this);
 Language()->RequestTalents();
 // The request broadcasts synchronously and can fail to acquire a modal lease.
 return Self.IsValid()&&Self->HasModalLease()&&Self->Screen&&Self->Screen->IsLanguageUIActive();
}
bool UDWLanguageUIBridgeComponent::TryHandlePrimaryInteraction()
{
 auto* PC=Controller();auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
 if(!IsTrialReady()||!P||HasModalLease()||!CanOpenMenus())return false;
 if(P->GatheringComponent&&P->GatheringComponent->IsGatheringEnabled()&&P->GatheringComponent->HasAvailableInteractionTarget())return false;
 ADWLanguageCarrier* Best=nullptr;double Distance=TNumericLimits<double>::Max();
 for(TActorIterator<ADWLanguageCarrier> It(GetWorld());It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&It->IsInReach(P))
 {const double D=FVector::DistSquared2D(P->GetActorLocation(),It->GetActorLocation());if(D<Distance){Distance=D;Best=*It;}}
 if(!Best)return false;FText Message;Language()->Interact(Best,false,Message);Notice(Message);return true;
}
bool UDWLanguageUIBridgeComponent::TryReadGlyph()
{
 auto* PC=Controller();auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
 if(!IsTrialReady()||!P||HasModalLease()||!CanOpenMenus())return false;
 FText Message;const bool Done=Language()->TryInteractNearest(P,true,Message);Notice(Message);return Done;
}
bool UDWLanguageUIBridgeComponent::GetWorldPromptKeys(FKey& OutPrimary,FKey& OutRead)const
{
 OutPrimary=FKey();OutRead=FKey();const auto* PC=Controller();
 if(!PC||!IsTrialReady())return false;
 OutPrimary=PC->GetAppliedActionKey(EDWInputAction::Harvest);
 OutRead=PC->GetAppliedActionKey(EDWInputAction::ReadGlyph);return true;
}
FGuid UDWLanguageUIBridgeComponent::AcquireModal(TSharedRef<SWidget> Focus)
{
 auto* PC=Controller();auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
 auto* H=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(P);
 if(bEnding||HasModalLease()||!PC||!PC->IsLocalPlayerController()||!P||!CanOpenMenus()||!IsTrialReady()||
    !GetWorld()||GetWorld()->bIsTearingDown||!H||!H->GameUIFont||!H->IsSessionStarted()||!A||
    (H->GetMenuPage()!=EDWMenuPage::None&&!ADWGameplayHUD::IsHandbookPage(H->GetMenuPage())))return FGuid();
 auto Prepared=MakeShared<FDWLanguageModalState>();
 if(!PC->GetDWTrackedGameAndUI(Prepared->UnderlyingMode,Prepared->UnderlyingFocus))
 {Notice(DWText(this,TEXT("请先关闭当前界面，再打开语言界面。"),TEXT("Close the current interface before opening this panel.")));return FGuid();}
 const auto Life=A->FindRegisteredActorLife(P);if(!Life.Entity||!Life.Life)return FGuid();
 Prepared->Token=FGuid::NewGuid();Prepared->World=GetWorld();Prepared->Player=P;Prepared->Authority=A;
 Prepared->OwnerHUD=H;Prepared->OwnerWidget=H->GetGameplayWidget();Prepared->LanguageOwner=Language();Prepared->LanguageGeneration=Language()->GetSessionGeneration();
 Prepared->LanguagePanel=Language()->GetRequestedPanel();Prepared->LanguageContext=Language()->GetRequestedContextId();Prepared->LanguageEntry=Language()->GetRequestedEntryId();
 Prepared->Life=Life;Prepared->Epoch=A->GetWorldEpoch();Prepared->bUnderlyingPaused=PC->IsPaused();
 Prepared->bUnderlyingCursor=PC->bShowMouseCursor;Prepared->ModalFocus=Focus;
 Lease=MoveTemp(Prepared);++InputOwnershipSerial;const FGuid Token=Lease->Token;PC->BeginCombatMenuSuspension();P->SetHarvestHeld(false);
 if(!PC->ApplyLanguagePause(true)||!IsLeaseIdentityCurrent())
 {ReleaseModal(Token);return FGuid();}
 ApplyModalInput();return IsLeaseIdentityCurrent()&&Lease->Token==Token?Token:FGuid();
}
bool UDWLanguageUIBridgeComponent::IsLeaseIdentityCurrent()const
{
 if(!HasModalLease())return false;const auto* PC=Controller();const auto* P=Lease->Player.Get();const auto* A=Lease->Authority.Get();
 if(!PC||!P||P->IsDead()||P->IsCombatRestoreInProgress()||PC->GetPawn()!=P||!Lease->World.IsValid()||
    Lease->World->bIsTearingDown||GetWorld()!=Lease->World.Get()||!A||A!=UDWCombatAuthoritySubsystem::Get(P)||A->GetWorldEpoch()!=Lease->Epoch)return false;
 auto* H=Cast<ADWGameplayHUD>(PC->GetHUD());const auto* L=Language();
 if(!H||H!=Lease->OwnerHUD.Get()||!H->GetGameplayWidget()||H->GetGameplayWidget()!=Lease->OwnerWidget.Get()||
    !L||L!=Lease->LanguageOwner.Get()||L->GetSessionGeneration()!=Lease->LanguageGeneration)return false;
 const auto Life=A->FindRegisteredActorLife(P);return Life.Entity==Lease->Life.Entity&&Life.Life==Lease->Life.Life;
}
void UDWLanguageUIBridgeComponent::ApplyModalInput()
{if(auto* PC=Controller();PC&&HasModalLease()){PC->bShowMouseCursor=true;PC->ApplyLanguageUIOnly(Lease->ModalFocus.Pin());}}
void UDWLanguageUIBridgeComponent::RecordUnderlyingPause(bool Paused,const FCanUnpause& Delegate)
{if(HasModalLease()&&!bPreserveUnderlying){Lease->bUnderlyingPaused=Paused;Lease->UnderlyingCanUnpause=Delegate;}}
void UDWLanguageUIBridgeComponent::RecordUnderlyingGameAndUI(const FInputModeGameAndUI& Mode,TSharedPtr<SWidget> Focus,bool Cursor)
{
 if(!HasModalLease()){++InputOwnershipSerial;return;}if(bPreserveUnderlying){ApplyModalInput();return;}Lease->UnderlyingMode=Mode;Lease->UnderlyingMode.SetWidgetToFocus(TSharedPtr<SWidget>());
 Lease->UnderlyingFocus=Focus;Lease->bUnderlyingCursor=Cursor;ApplyModalInput();
}
void UDWLanguageUIBridgeComponent::ReleaseModal(FGuid Token)
{
 if(!HasModalLease()||Token!=Lease->Token)return;
 // A matching token owns restoration even if the backend generation has changed.
 // Retire the lease before callbacks; a new lease or external mode invalidates this serial.
 uint64 RestoreSerial=InputOwnershipSerial;
 TSharedPtr<FDWLanguageModalState> Released=MoveTemp(Lease);Released->Token.Invalidate();auto* PC=Controller();
 const FText LastNotice=Screen&&Screen->GetUIContext().IsValid()?Screen->GetUIContext()->NoticeText:FText::GetEmpty();
 const bool SameWorld=PC&&Released->World.IsValid()&&PC->GetWorld()==Released->World.Get()&&!Released->World->bIsTearingDown;
 const bool SameLife=SameWorld&&Released->Player.IsValid()&&PC->GetPawn()==Released->Player.Get()&&!Released->Player->IsDead()&&
    Released->Authority.IsValid()&&Released->Authority->GetWorldEpoch()==Released->Epoch&&
    Released->Authority->FindRegisteredActorLife(Released->Player.Get()).Entity==Released->Life.Entity&&
    Released->Authority->FindRegisteredActorLife(Released->Player.Get()).Life==Released->Life.Life;
 // Close only the captured backend request, before pause/focus callbacks can start another one.
 // Generation alone does not distinguish RequestJournal/Interact calls within one backend session.
 if(!bEnding&&SameWorld&&!HasModalLease())
  if(auto* L=Released->LanguageOwner.Get();L&&L==Language()&&L->GetSessionGeneration()==Released->LanguageGeneration&&
     L->GetRequestedPanel()==Released->LanguagePanel&&L->GetRequestedContextId()==Released->LanguageContext&&
     L->GetRequestedEntryId()==Released->LanguageEntry)L->CloseInteraction();
 const auto CanRestoreOwnedState=[this,PC,Released,&RestoreSerial]()
 {
  const auto* H=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
  return Controller()==PC&&PC&&!HasModalLease()&&InputOwnershipSerial==RestoreSerial&&
   Released->World.IsValid()&&PC->GetWorld()==Released->World.Get()&&!Released->World->bIsTearingDown&&
   H&&H==Released->OwnerHUD.Get()&&H->GetGameplayWidget()&&H->GetGameplayWidget()==Released->OwnerWidget.Get();
 };
 if(CanRestoreOwnedState())
 {
  // Pause delegates may reenter UI code. Restore pause first, then recheck ownership.
  if(Released->bUnderlyingPaused)PC->ApplyLanguagePause(true,Released->UnderlyingCanUnpause);
  else if(auto* Settings=PC->GetWorldSettings();Settings&&Settings->GetPauserPlayerState()==PC->PlayerState)
   PC->ApplyLanguagePause(false,Released->UnderlyingCanUnpause);
  if(CanRestoreOwnedState())
  {
   TSharedPtr<SWidget> Focus=SameLife?Released->UnderlyingFocus.Pin():TSharedPtr<SWidget>();
   if(Focus&&(!FSlateApplication::IsInitialized()||!FSlateApplication::Get().FindWidgetWindow(Focus.ToSharedRef()).IsValid()))Focus.Reset();
   Released->UnderlyingMode.SetWidgetToFocus(Focus);PC->PrepareCombatInputForSimulation();
   if(CanRestoreOwnedState())
   {
    // The controller reports every known mode, including this restoration. Admit exactly that one report.
    ++RestoreSerial;PC->bShowMouseCursor=Released->bUnderlyingCursor;
    PC->ApplyDWGameAndUI(Released->UnderlyingMode,Focus);
   }
   // ApplyDWGameAndUI already applies valid focus. Only the empty-focus fallback is needed.
   if(CanRestoreOwnedState()&&!Focus&&FSlateApplication::IsInitialized())FSlateApplication::Get().SetAllUserFocusToGameViewport();
   if(CanRestoreOwnedState()&&SameLife)Notice(LastNotice);
  }
 }
 RefreshWorldPrompts();
}
void UDWLanguageUIBridgeComponent::CloseForExternalInputMode(){++InputOwnershipSerial;CloseForLifecycle();}
void UDWLanguageUIBridgeComponent::CloseForLifecycle()
{
 if(bClosing)return;TGuardValue<bool> Guard(bClosing,true);
 TSharedPtr<SWidget> RetiringRoot=MoveTemp(ScreenSlateRoot);
 const TStrongObjectPtr<UDWLanguageUIScreen> RetiringScreen(Screen.Get());
 if(RetiringScreen.Get()){RetiringScreen->CloseImmediately();RetiringScreen->RemoveFromParent();}
 if(Screen==RetiringScreen.Get())Screen=nullptr;
 if(bBookPresented)LeaveJournalBook(false); // Activation may fail before Screen marks its book state.
 if(HasModalLease())ReleaseModal(Lease->Token);
 RetiringRoot.Reset(); // NativeDestruct can now only repeat an already retired screen close.
}
void UDWLanguageUIBridgeComponent::HandlePanelRequested()
{
 auto* L=Language();if(bEnding||bClosing||!L||!IsTrialReady())return;
 const auto Panel=L->GetRequestedPanel();if(Panel==EDWLanguagePanel::None)return;
 if(Screen&&Screen->IsLanguageUIActive()){if(Panel==EDWLanguagePanel::Journal)Screen->OpenJournal(L->GetRequestedEntryId());return;}
 auto* PC=Controller();auto* H=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;auto* P=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
 if(!PC||!H||!H->GameUIFont||!P||!CanOpenMenus())return;
 if(Screen||ScreenSlateRoot)CloseForLifecycle();
 Screen=CreateWidget<UDWLanguageUIScreen>(PC,UDWLanguageUIScreen::StaticClass());if(!Screen)return;
 const TStrongObjectPtr<UDWLanguageUIScreen> ActivatingScreen(Screen.Get());
 const uint64 ActivationGeneration=L->GetSessionGeneration();const FName ActivationContext=L->GetRequestedContextId();
 Screen->ExistingGameFont=H->GameUIFont;Screen->JournalToggleKey=PC->GetAppliedActionKey(EDWInputAction::LanguageJournal);
 Screen->JournalAlternateToggleKey=PC->GetAppliedActionKey(EDWInputAction::LanguageJournalAlternate);
 // A direct journal has no viewport parent. Keep its SObjectWidget rooted for this session.
 const TSharedRef<SWidget> ActivatingRoot=ActivatingScreen->TakeWidget();
 if(Screen!=ActivatingScreen.Get())return;
 ScreenSlateRoot=ActivatingRoot;
 if(Panel!=EDWLanguagePanel::Journal)Screen->AddToViewport(200);
 const TWeakObjectPtr<UDWLanguageUIBridgeComponent> WeakThis(this);const TWeakObjectPtr<ADWPlayerController> WeakPC(PC);
 auto Adapter=MakeShared<FDWLanguageSubsystemV1Adapter>(L,P,[WeakPC](){return WeakPC.IsValid()&&UDWLocalizationLibrary::GetLanguage(WeakPC.Get())==EDWGameLanguage::English;});
 const TWeakObjectPtr<UDWLanguageUIScreen> WeakScreen(ActivatingScreen.Get());
 FDWLanguageUIHostServices Host;
 Host.AcquireModal=[WeakThis,WeakScreen](TSharedRef<SWidget> Focus){return WeakThis.IsValid()&&WeakScreen.IsValid()&&WeakThis->Screen==WeakScreen.Get()?WeakThis->AcquireModal(Focus):FGuid();};
 Host.ReleaseModal=[WeakThis](FGuid Token){if(WeakThis.IsValid())WeakThis->ReleaseModal(Token);};
 Host.RefreshWorldPrompts=[WeakThis](){if(WeakThis.IsValid())WeakThis->RefreshWorldPrompts();};
 Host.PresentJournalInBook=[WeakThis,WeakScreen](TSharedRef<SWidget> Content){return WeakThis.IsValid()&&WeakScreen.IsValid()&&WeakThis->Screen==WeakScreen.Get()&&WeakThis->PresentJournalInBook(Content);};
 Host.LeaveJournalBook=[WeakThis,WeakScreen](bool Resume){return WeakThis.IsValid()&&WeakScreen.IsValid()&&WeakThis->Screen==WeakScreen.Get()&&WeakThis->LeaveJournalBook(Resume);};
 Host.SetBookAuxiliaryVisible=[WeakThis,WeakScreen](bool Visible){return WeakThis.IsValid()&&WeakScreen.IsValid()&&WeakThis->Screen==WeakScreen.Get()&&WeakThis->SetBookAuxiliaryVisible(Visible);};
 const auto Page=Panel==EDWLanguagePanel::Relic?EDWLanguageUIScreenPage::Puzzle:
  (Panel==EDWLanguagePanel::Offering||Panel==EDWLanguagePanel::Trade)?EDWLanguageUIScreenPage::Exchange:
  Panel==EDWLanguagePanel::Talents?EDWLanguageUIScreenPage::Talents:EDWLanguageUIScreenPage::Journal;
 const bool SameActivation=Screen==ActivatingScreen.Get()&&Controller()==PC&&PC->GetPawn()==P&&
  Language()==L&&L->GetSessionGeneration()==ActivationGeneration&&L->GetRequestedPanel()==Panel&&L->GetRequestedContextId()==ActivationContext;
 if(!SameActivation||!ActivatingScreen->Activate(Adapter,MoveTemp(Host),Page,L->GetRequestedEntryId()))
 {
  TGuardValue<bool> ClosingGuard(bClosing,true);
  ActivatingScreen->CloseImmediately();ActivatingScreen->RemoveFromParent();
  if(Screen==ActivatingScreen.Get()){Screen=nullptr;ScreenSlateRoot.Reset();}
  if(Language()==L&&L->GetSessionGeneration()==ActivationGeneration&&L->GetRequestedPanel()==Panel&&L->GetRequestedContextId()==ActivationContext)L->CloseInteraction();
 }
}
bool UDWLanguageUIBridgeComponent::TickRealTime(float)
{
 if(bEnding)return false;
 if(HasModalLease()&&!IsLeaseIdentityCurrent()){CloseForLifecycle();return true;}
 if(HasModalLease())if(auto* PC=Controller();PC&&!PC->bShowMouseCursor){Lease->bUnderlyingCursor=false;PC->bShowMouseCursor=true;}
 if(Screen&&!Screen->IsLanguageUIActive())CloseForLifecycle();return true;
}
void UDWLanguageUIBridgeComponent::RefreshWorldPrompts()
{if(GetWorld()&&!GetWorld()->bIsTearingDown)for(TActorIterator<ADWLanguageCarrier> It(GetWorld());It;++It)if(IsValid(*It))It->RefreshCommittedPresentation();}

bool UDWLanguageUIBridgeComponent::IsBookBindingCurrent()const
{
 const auto* PC=Controller();auto* H=BookHUD.Get();auto* W=BookOwner.Get();
 return bBookPresented&&PC&&H&&W&&PC->GetHUD()==H&&H->GetGameplayWidget()==W&&IsLeaseIdentityCurrent();
}
bool UDWLanguageUIBridgeComponent::RouteBookPage(EDWMenuPage Page,bool Preserve)
{
 if(!IsBookBindingCurrent()||bExternalBookNavigation)return false;
 auto* H=BookHUD.Get();const FGuid Token=Lease->Token;const auto PreviousScreen=Screen;
 TGuardValue<EDWMenuPage> Target(RoutingBookPage,Page);
 TGuardValue<bool> Routing(bRoutingBook,true);TGuardValue<bool> RestoreGuard(bPreserveUnderlying,Preserve);
 H->ShowMenu(Page); // Reentrant HUD bridge hook sees bRoutingBook and applies only the normal page body.
 // ShowMenu invokes Blueprint callbacks. They may retire this lease or replace the HUD.
 return !bExternalBookNavigation&&IsBookBindingCurrent()&&Lease->Token==Token&&Screen==PreviousScreen&&H->GetMenuPage()==Page;
}
bool UDWLanguageUIBridgeComponent::FocusBook()
{
 if(!IsBookBindingCurrent())return false;
 auto* W=BookOwner.Get();const FGuid Token=Lease->Token;const auto PreviousScreen=Screen;
 const TSharedRef<SWidget> Focus=W->TakeWidget();
 if(!IsBookBindingCurrent()||Lease->Token!=Token||Screen!=PreviousScreen)return false;
 Lease->ModalFocus=Focus;ApplyModalInput();
 if(!IsBookBindingCurrent()||Lease->Token!=Token||Screen!=PreviousScreen)return false;
 if(FSlateApplication::IsInitialized())FSlateApplication::Get().SetUserFocus(0,Focus,EFocusCause::SetDirectly);
 return IsBookBindingCurrent()&&Lease->Token==Token&&Screen==PreviousScreen;
}
bool UDWLanguageUIBridgeComponent::PresentJournalInBook(TSharedRef<SWidget> Content)
{
 auto* PC=Controller();auto* H=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 auto* W=H?H->GetGameplayWidget():nullptr;
 if(!W||!IsLeaseIdentityCurrent()||!CanOpenMenus())return false;
 const FGuid Token=Lease->Token;const auto PreviousScreen=Screen;
 if(bBookPresented&&!IsBookBindingCurrent()){CloseForLifecycle();return false;}
 if(!bBookPresented)
 {
  if(!W->MountLanguageJournal(Content))return false;
  if(!IsLeaseIdentityCurrent()||Lease->Token!=Token||Screen!=PreviousScreen){W->UnmountLanguageJournal();return false;}
  ReturnBookPage=H->GetMenuPage();BookOwner=W;BookHUD=H;bBookPresented=true;bExternalBookNavigation=false;
 }
 // Presentation only: RequestedPanel/context and the original restoration baseline remain unchanged.
 if(!RouteBookPage(EDWMenuPage::LanguageJournal,true)||!FocusBook()){CloseForLifecycle();return false;}
 return true;
}
bool UDWLanguageUIBridgeComponent::LeaveJournalBook(bool Resume)
{
 if(!bBookPresented)return !Resume;
 bool CanResume=Resume&&IsLeaseIdentityCurrent()&&CanOpenMenus();
 if(CanResume)
 {
  auto* L=Language();FText Message;
  if(!L)CanResume=false;
  else if(L->GetRequestedPanel()==EDWLanguagePanel::Relic||L->GetRequestedPanel()==EDWLanguagePanel::Offering||L->GetRequestedPanel()==EDWLanguagePanel::Trade)
   CanResume=L->ReturnToInteraction(Message); // Performs authoritative carrier/distance/epoch/life validation.
  if(!CanResume)Notice(Message);
 }
 // Keep the binding while the guarded route validates Blueprint callbacks, then detach it.
 if(!bEnding&&!bExternalBookNavigation&&IsBookBindingCurrent())
 {if(!RouteBookPage(CanResume?ReturnBookPage:EDWMenuPage::None,CanResume))CanResume=false;}
 else CanResume=false;
 if(auto* W=BookOwner.Get())W->UnmountLanguageJournal();
 bBookPresented=false;bExternalBookNavigation=false;
 BookOwner.Reset();BookHUD.Reset();
 if(CanResume&&Screen&&IsLeaseIdentityCurrent())
 {
  const FGuid Token=Lease->Token;const auto PreviousScreen=Screen;const auto Focus=Screen->TakeWidget();
  CanResume=IsLeaseIdentityCurrent()&&Lease->Token==Token&&Screen==PreviousScreen;
  if(CanResume){Lease->ModalFocus=Focus;ApplyModalInput();CanResume=IsLeaseIdentityCurrent()&&Lease->Token==Token&&Screen==PreviousScreen;}
 }
 return !Resume||CanResume;
}
bool UDWLanguageUIBridgeComponent::SetBookAuxiliaryVisible(bool Visible)
{
 auto* H=BookHUD.Get();if(!IsBookBindingCurrent()||!H||!Screen||!CanOpenMenus())return false;
 const FGuid Token=Lease->Token;const auto PreviousScreen=Screen;
 if(Visible)
 {
  BookAuxReturnPage=H->GetMenuPage();if(!RouteBookPage(EDWMenuPage::None,true))return false;
  if(!IsBookBindingCurrent()||Lease->Token!=Token||Screen!=PreviousScreen)return false;
  const auto Focus=Screen->TakeWidget();
  if(!IsBookBindingCurrent()||Lease->Token!=Token||Screen!=PreviousScreen)return false;
  Lease->ModalFocus=Focus;ApplyModalInput();
 }
 else if(!RouteBookPage(BookAuxReturnPage,true)||!FocusBook())return false;
 return IsBookBindingCurrent()&&Lease->Token==Token&&Screen==PreviousScreen;
}
bool UDWLanguageUIBridgeComponent::TryHandleHandbookMenu(EDWMenuPage Page)
{
 if(bRoutingBook)
 {
  // A different page requested by a Blueprint callback is a new outer navigation,
  // not part of our visual transfer. Record its input/pause as the restoration target.
  if(Page!=RoutingBookPage){bExternalBookNavigation=true;bPreserveUnderlying=false;}
  return false;
 }
 if(bEnding||bClosing)return false;
 if(Page==EDWMenuPage::LanguageJournal)return RequestJournal();
 if(!bBookPresented||!Screen||!HasModalLease())return false;
 if(Screen->IsClosingPresentation())return true;
 // Generic ClosePanels also precedes core use, equipment and building placement.
 // Only explicit user-back entry points may resume the originating puzzle.
 if(Page==EDWMenuPage::None){CloseForLifecycle();return false;}
 if(ADWGameplayHUD::IsHandbookPage(Page)){if(!RouteBookPage(Page,true)||!FocusBook())CloseForLifecycle();return true;}
 CloseForLifecycle();return false; // Explicit non-book menu takes ownership after this language lease ends.
}
bool UDWLanguageUIBridgeComponent::TryCloseBookFromUser()
{
 if(!bBookPresented||!Screen||!HasModalLease())return false;
 if(!Screen->IsClosingPresentation())Screen->CloseTopLayer();return true;
}
bool UDWLanguageUIBridgeComponent::TryHandleBookKey(const FKeyEvent& Event)
{
 if(!bBookPresented||!Screen||!HasModalLease())return false;
 const auto Focus=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetKeyboardFocusedWidget():TSharedPtr<SWidget>();
 const bool Editing=Focus.IsValid()&&(Focus->GetTypeAsString().Contains(TEXT("EditableText"))||Focus->GetTypeAsString().Contains(TEXT("SpinBox")));
 if(Editing)return false; // IME/text receives Escape and remapped punctuation before book navigation.
 if(Event.GetKey()==EKeys::Escape){if(!Event.IsRepeat())TryCloseBookFromUser();return true;}
 auto* PC=Controller();auto* H=BookHUD.Get();
 if(PC&&(Event.GetKey()==PC->GetAppliedActionKey(EDWInputAction::LanguageJournal)||Event.GetKey()==PC->GetAppliedActionKey(EDWInputAction::LanguageJournalAlternate)))
 {if(!Event.IsRepeat()){if(H&&H->GetMenuPage()==EDWMenuPage::LanguageJournal)Screen->CloseTopLayer();else Screen->OpenJournal();}return true;}
 return false;
}
bool UDWLanguageUIBridgeComponent::CanUseTrialStorage()const
{
 const auto* L=Language();return !bEnding&&!bClosing&&CanOpenMenus()&&L&&L->IsReady()&&L->IsTrialWorld()&&!L->IsFormalSession();
}
bool UDWLanguageUIBridgeComponent::SaveTrialFromMenu()
{
 if(!CanUseTrialStorage())return false;FText Message;const bool Saved=Language()->SaveTrial(Message);Notice(Message);return Saved;
}
bool UDWLanguageUIBridgeComponent::LoadTrialFromMenu()
{
 if(!CanUseTrialStorage())return false;
 CloseForLifecycle(); // Retire adapter, drafts, drags and lease before restore can change generation.
 if(!CanUseTrialStorage())return false;FText Message;const bool Loaded=Language()->LoadTrial(Message);Notice(Message);return Loaded;
}
