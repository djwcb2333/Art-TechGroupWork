#pragma once

// External LANG-UI candidate. The executor supplies an adapter to the authoritative game system.
// Never put correct translations, recipe conditions, secret requirements or unrevealed rewards here.
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Fonts/SlateFontInfo.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StrongObjectPtr.h"

class SWidget;
class UTexture2D;

enum class EDWLanguageEntryKind : uint8 { Single, Compound };
enum class EDWLanguageExchangeMode : uint8 { Offering, NPCTrade };
enum class EDWLanguageUIRequestKind : uint8
{
    SaveGuess, SubmitPuzzle, StageItem, UnstageItem, SubmitExchange, LearnTalent, ForgetTalent
};

struct FDWLanguageUIClueView
{
    FName Id;
    FText Place;
    FText Carrier;
    FText Description; // Actual observed scene only; never a standard translation.
    FName ObservedEntryId;
    bool bFromCompound = false;
};

struct FDWLanguageUIEntryView
{
    FName Id;
    EDWLanguageEntryKind Kind = EDWLanguageEntryKind::Single;
    bool bDiscovered = false;
    bool bCanEdit = false;
    FText EditBlockedReason;
    int64 DiscoveryOrder = 0;
    TSoftObjectPtr<UTexture2D> Glyph;
    FString SavedGuess; // Only the player's saved personal note, independent for each entry.
    TArray<FName> PartIds; // Authored order, duplicates deliberately preserved.
    TArray<FName> RelatedIds; // Only relationships the player may currently see.
    TArray<FDWLanguageUIClueView> Clues;
};

struct FDWLanguagePuzzleSlotView
{
    FName SlotId;
    FName EntryId;
    TSoftObjectPtr<UTexture2D> Glyph; // Visible on the current physical relic; not a hidden journal answer.
};
struct FDWLanguageWordView { FName Id; FText Label; };
struct FDWLanguagePuzzleView
{
    FName Id;
    FText Title;
    bool bAvailable = false;
    bool bSolved = false;
    bool bCanSubmit = true;
    FText BlockedReason;
    TArray<FDWLanguagePuzzleSlotView> Slots;
    TArray<FDWLanguageWordView> WordPool;
    FText StoryChinese; // Adapter MUST leave both stories/rewards empty until solved.
    FText StoryEnglish;
    FText RewardDescription;
    bool bRewardGranted = false; // Receipt only; the UI does not grant this reward.
};

struct FDWLanguageInventoryView
{
    FName StackId; // Stable stack identity, not the current list index.
    FName ItemId;
    FText Name;
    TSoftObjectPtr<UTexture2D> Icon;
    int32 Quantity = 0;
    bool bCanSelect = true;
    FText BlockedReason;
};
struct FDWLanguageExchangeView
{
    FName TargetId;
    EDWLanguageExchangeMode Mode = EDWLanguageExchangeMode::Offering;
    FText Title;
    bool bAvailable = false;
    bool bCanSubmit = true;
    int32 UnitsPerSubmission = 1;
    FText BlockedReason;
    TArray<FName> InscriptionIds;
    TArray<FName> RewardInscriptionIds; // NPC's publicly shown reward inscription, not its translation.
    TMap<FName,TSoftObjectPtr<UTexture2D>> VisibleInscriptionGlyphs; // Physical inscriptions on this active mechanism only.
    TArray<FDWLanguageInventoryView> Inventory;
    FText Receipt; // Server/gameplay result only, never a UI-simulated success.
};

struct FDWLanguageTalentView
{
    FName Id;
    FText Name;
    FText Description;
    TSoftObjectPtr<UTexture2D> Icon;
    int32 Cost = 0;
    bool bLearned = false;
    bool bCanLearn = false;
    bool bCanForget = false;
    FText LearnBlockedReason;
    FText ForgetBlockedReason;
    TArray<FName> PrerequisiteIds;
    bool bHasCanvasPosition = false;
    FVector2D CanvasPosition = FVector2D::ZeroVector; // Optional normalized [0,1] authored layout.
};

struct FDWLanguageUISnapshot
{
    FString SessionId; // Changes on target/session transition; never reuse stale drafts across sessions.
    int64 Revision = 0;
    bool bSessionValid = false;
    bool bEnglish = false;
    int32 MaxGuessCharacters = 40; // Unicode scalar count; clamp to >=1 in the presentation layer.
    TArray<FDWLanguageUIEntryView> Entries;
    FDWLanguagePuzzleView Puzzle;
    FDWLanguageExchangeView Exchange;
    int32 TalentPoints = 0;
    TArray<FDWLanguageTalentView> Talents;

    const FDWLanguageUIEntryView* FindEntry(FName Id) const
    {
        return Entries.FindByPredicate([Id](const FDWLanguageUIEntryView& E) { return E.Id == Id; });
    }
};

struct FDWLanguageSlotAnswer { FName SlotId; FName WordId; };
struct FDWLanguageItemQuantity { FName StackId; int32 Quantity = 0; };
struct FDWLanguageUIRequest
{
    EDWLanguageUIRequestKind Kind = EDWLanguageUIRequestKind::SaveGuess;
    FString SessionId;
    int64 ExpectedRevision = 0;
    FGuid RequestId; // Idempotency key. Adapter must reject duplicate/stale target requests safely.
    FName TargetId; // Entry / puzzle / trade target / talent ID, depending on Kind.
    FString Guess;
    TArray<FDWLanguageSlotAnswer> Answers; // Complete ordered slot assignment; repeated words allowed.
    TArray<FDWLanguageItemQuantity> Items; // Proposed quantities only; UI never pre-deducts.
};
struct FDWLanguageUIReply
{
    bool bAccepted = false;
    FName ResultCode;
    FText Message;
    bool bCloseAfterSuccess = false;
    bool bResetSelection = false; // The backend selection token has retired; clear presentation staging.
    float CloseDelaySeconds = 1.f;
};

class IDWLanguageUIAdapter
{
public:
    virtual ~IDWLanguageUIAdapter() = default;
    virtual FDWLanguageUISnapshot ReadSnapshot() const = 0;
    // Executor validates session, revision, IDs, quantities, costs and idempotency; commits atomically.
    virtual FDWLanguageUIReply Request(const FDWLanguageUIRequest& Request) = 0;
};

struct FDWLanguageUIContext : public TSharedFromThis<FDWLanguageUIContext>
{
    TSharedPtr<IDWLanguageUIAdapter> Adapter;
    TFunction<FSlateFontInfo(int32)> FontProvider; // Inject existing CN/EN font. No replacement font asset.
    TFunction<void(FName)> OnOpenJournal;
    TFunction<void()> OnCloseLayer; // Host owns modal nesting, pause, input mode and focus restoration.
    TFunction<void(const FText&)> OnNotice;
    TFunction<void(const FDWLanguageUIRequest&, const FDWLanguageUIReply&)> OnReply;
    FDWLanguageUISnapshot Snapshot;
    FText NoticeText;
    bool bAcceptRequests = false; // Enabled only after the host owns a valid input/pause lease.

    const FDWLanguageUISnapshot& GetSnapshot() const { return Snapshot; }
    void RefreshSnapshot();
    FDWLanguageUIReply Request(FDWLanguageUIRequest Request);
    void OpenJournal(FName EntryId) const;
    void CloseLayer() const;
    void Notice(const FText& Text);
    FSlateFontInfo Font(int32 Size) const;
    TSharedRef<SWidget> Glyph(const TSoftObjectPtr<UTexture2D>& Asset, FVector2D Size, FLinearColor Tint = FLinearColor::White);
    TSharedRef<SWidget> EntryGlyph(FName EntryId, FVector2D Size);
private:
    TMap<FSoftObjectPath, TSharedPtr<FSlateBrush>> GlyphBrushes;
    TArray<TStrongObjectPtr<UTexture2D>> GlyphTextures;
};
