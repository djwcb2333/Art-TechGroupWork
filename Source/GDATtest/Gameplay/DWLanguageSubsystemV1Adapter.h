#pragma once
#include "DWLanguageUIContract.h"
#include "DWInventoryComponent.h"

class UDWLanguageSubsystem;
class ADWPlayerCharacter;

/** Thin projection of Execution/UI公共接口契约.md v2.1; V1 filename retained for handoff continuity.
 * No gameplay authority or save model. Backend captures immutable inventory identity at each actual click.
 * Add only after the executor's DWLanguageSubsystem.h is present. Create one adapter per opened session. */
class FDWLanguageSubsystemV1Adapter final : public IDWLanguageUIAdapter
{
public:
    FDWLanguageSubsystemV1Adapter(UDWLanguageSubsystem* InSystem,ADWPlayerCharacter* InPlayer,TFunction<bool()> InIsEnglish);
    virtual FDWLanguageUISnapshot ReadSnapshot() const override;
    virtual FDWLanguageUIReply Request(const FDWLanguageUIRequest& Request) override;
private:
    TWeakObjectPtr<UDWLanguageSubsystem> System;
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TFunction<bool()> IsEnglish;
    FString SessionId;
    uint64 OwnerGeneration=0;
    FName ContextId;
    uint8 InitialPanel=0;
    mutable int64 LastSystemRevision=-1,LastInventoryRevision=-1,PresentationRevision=0;
    mutable bool bLastEnglish=false;
    mutable TMap<FName,FDWInventorySlotSnapshot> InventoryTokens;
    mutable FGuid BackendSelectionId;
    mutable TArray<FName> SelectedStackIds; // Mirrors ordered backend selection indices, never inventory.
    struct FCachedReply { FDWLanguageUIRequest Request; FDWLanguageUIReply Reply; };
    TMap<FGuid,FCachedReply> RepliedRequests;
};
