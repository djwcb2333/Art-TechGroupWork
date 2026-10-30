#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/PlayerController.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "DWLanguageUIBridgeComponent.generated.h"
class ADWPlayerController;
class UDWLanguageSubsystem;
class UDWLanguageUIScreen;
class FDWLanguageModalState;
class UDWGameplayWidget;
class ADWGameplayHUD;
struct FKeyEvent;
enum class EDWMenuPage : uint8;
/** PC-owned bridge; the one modal lease owns only its pause/input changes. */
UCLASS(ClassGroup=(DoughWorld),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWLanguageUIBridgeComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UDWLanguageUIBridgeComponent();
 virtual ~UDWLanguageUIBridgeComponent();
 static UDWLanguageUIBridgeComponent* Find(const APlayerController* Controller);
 bool HasModalLease()const;
 bool CanOpenMenus()const;
 bool CanUseShortcuts()const;
 bool IsTrialReady()const;
 bool RequestJournal(FName EntryId=NAME_None);
 bool RequestTalents();
 bool TryHandleHandbookMenu(EDWMenuPage Page);
 bool TryCloseBookFromUser();
 bool TryHandleBookKey(const FKeyEvent& Event);
 bool CanUseTrialStorage()const;
 bool SaveTrialFromMenu();
 bool LoadTrialFromMenu();
 /** A real target consumes F even on failure, so one press never falls through to another action. */
 bool TryHandlePrimaryInteraction();
 bool TryReadGlyph();
 /** Read-only presentation data. Invalid/unbound applied keys are kept invalid. */
 bool GetWorldPromptKeys(FKey& OutPrimary,FKey& OutRead)const;
 void RecordUnderlyingPause(bool bPause,const FCanUnpause& Delegate);
 void RecordUnderlyingGameAndUI(const FInputModeGameAndUI& Mode,TSharedPtr<SWidget> Focus,bool bCursor);
 void CloseForExternalInputMode();
 void CloseForLifecycle();
protected:
 virtual void BeginPlay()override;
 virtual void EndPlay(const EEndPlayReason::Type Reason)override;
private:
 ADWPlayerController* Controller()const;
 UDWLanguageSubsystem* Language()const;
 UFUNCTION() void HandlePanelRequested();
 FGuid AcquireModal(TSharedRef<SWidget> Focus);
 void ReleaseModal(FGuid Token);
 bool TickRealTime(float DeltaSeconds);
 bool IsLeaseIdentityCurrent()const;
 void ApplyModalInput();
 bool PresentJournalInBook(TSharedRef<SWidget> Content);
 bool LeaveJournalBook(bool bResumeOuter);
 bool SetBookAuxiliaryVisible(bool bVisible);
 bool RouteBookPage(EDWMenuPage Page,bool bPreserveRestoreState);
 bool FocusBook();
 bool IsBookBindingCurrent()const;
 void RefreshWorldPrompts();
 void Notice(const FText& Message)const;
 UPROPERTY(Transient) TObjectPtr<UDWLanguageUIScreen> Screen;
 TSharedPtr<FDWLanguageModalState> Lease;
 FTSTicker::FDelegateHandle RealTimeTick;
 bool bEnding=false,bClosing=false;
 // The bridge owns the root; the screen and its callbacks never own this SObjectWidget.
 // Reset during lifecycle teardown, before the owning actor reaches garbage collection.
 TSharedPtr<SWidget> ScreenSlateRoot;
 uint64 InputOwnershipSerial=0;
 bool bRoutingBook=false,bPreserveUnderlying=false,bBookPresented=false,bExternalBookNavigation=false;
 EDWMenuPage ReturnBookPage;
 EDWMenuPage BookAuxReturnPage;
 EDWMenuPage RoutingBookPage;
 TWeakObjectPtr<UDWGameplayWidget> BookOwner;
 TWeakObjectPtr<ADWGameplayHUD> BookHUD;
};
