#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Containers/Ticker.h"
#include "DWLanguageUIContract.h"
#include "DWLanguageUIScreen.generated.h"

class SDWLanguageJournalPanel;
class SDWLanguagePuzzlePanel;
class SDWLanguageExchangePanel;
class SDWTalentPanel;
class SWidgetSwitcher;
class STextBlock;
class UDWUIBounceComponent;
class UDWUIOffscreenComponent;
class UFont;

UENUM(BlueprintType)
enum class EDWLanguageUIScreenPage : uint8 { Journal, Puzzle, Exchange, Talents };

/** The existing controller/HUD owns these leases, not this presentation widget.
 * Acquire must set UI-only input, block gameplay actions and pause game time atomically.
 * Release removes ONLY this lease and restores the previous outer menu/focus state.
 * Never implement release as an unconditional SetGamePaused(false). */
struct FDWLanguageUIHostServices
{
    TFunction<FGuid(TSharedRef<SWidget>)> AcquireModal;
    TFunction<void(FGuid)> ReleaseModal;
    TFunction<void()> RefreshWorldPrompts;
    TFunction<bool(TSharedRef<SWidget>)> PresentJournalInBook;
    TFunction<bool(bool)> LeaveJournalBook; // true = resume originating mechanism; false = end book browsing.
    TFunction<bool(bool)> SetBookAuxiliaryVisible; // Temporarily show talents without changing the backend context.
};

/** Independent candidate host. No WBP regeneration; executor adds this above the current HUD.
 * Caller retains shared ownership of Adapter. All gameplay rules remain outside the UI. */
UCLASS()
class GDATTEST_API UDWLanguageUIScreen : public UUserWidget
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Language UI|Font") TObjectPtr<UFont> ExistingGameFont;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Language UI|Input") FKey JournalToggleKey;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Language UI|Input") FKey JournalAlternateToggleKey;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Language UI|Motion") float EnterSeconds = .32f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Language UI|Motion") float ExitSeconds = .2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Language UI|Layout") FVector2D DesignSize = FVector2D(1160, 650);

    UDWLanguageUIScreen(const FObjectInitializer& ObjectInitializer);
    bool Activate(TSharedRef<IDWLanguageUIAdapter> Adapter, FDWLanguageUIHostServices Services,
        EDWLanguageUIScreenPage Page, FName InitialEntry = NAME_None);
    void OpenJournal(FName EntryId = NAME_None);
    bool OpenTalentsFromBook();
    bool IsJournalInBook() const { return bJournalInBook; }
    bool IsClosingPresentation() const { return bClosing || CloseAt > 0.; }
    void CloseTopLayer();
    void CloseImmediately();
    bool IsLanguageUIActive() const { return ModalLease.IsValid(); }
    TSharedPtr<FDWLanguageUIContext> GetUIContext() const { return Context; }
    virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativeDestruct() override;
    virtual FReply NativeOnPreviewKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply NativeOnKeyDown(const FGeometry&, const FKeyEvent&) override;
    virtual FReply NativeOnKeyUp(const FGeometry&, const FKeyEvent&) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry&, const FPointerEvent&) override;
private:
    void BuildPanels();
    void RefreshPanels();
    void ReleaseLease();
    void StartTick();
    bool TickPresentation(float DeltaSeconds);
    bool IsTextEntryFocused() const;
    void HandleReply(const FDWLanguageUIRequest&, const FDWLanguageUIReply&);
    FDWLanguageUIHostServices Host;
    FGuid ModalLease;
    TSharedPtr<FDWLanguageUIContext> Context;
    TSharedPtr<SWidgetSwitcher> Switcher;
    TSharedPtr<SWidget> BookJournalContent;
    TSharedPtr<STextBlock> NoticeLabel;
    TSharedPtr<SDWLanguageJournalPanel> Journal;
    TSharedPtr<SDWLanguagePuzzlePanel> Puzzle;
    TSharedPtr<SDWLanguageExchangePanel> Exchange;
    TSharedPtr<SDWTalentPanel> Talents;
    UPROPERTY(Transient) TObjectPtr<UDWUIBounceComponent> Bounce;
    UPROPERTY(Transient) TObjectPtr<UDWUIOffscreenComponent> Offscreen;
    FTSTicker::FDelegateHandle TickHandle;
    EDWLanguageUIScreenPage BasePage = EDWLanguageUIScreenPage::Journal;
    bool bNestedJournal = false;
    bool bJournalInBook = false;
    bool bBookAuxiliary = false;
    bool bRefreshPending = false;
    bool bClosing = false;
    bool bClosingImmediately = false;
    double CloseAt = 0.;
    double RemoveAt = 0.;
    FString ClosingSession;
};
