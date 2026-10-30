#pragma once
#include "CoreMinimal.h"
#include "DWLanguageTypes.generated.h"

UENUM(BlueprintType) enum class EDWLanguageFilter:uint8 { All,Glyph,Compound };
UENUM(BlueprintType) enum class EDWLanguageSort:uint8 { UnrecordedFirst,NewestFirst,OldestFirst };
UENUM(BlueprintType) enum class EDWLanguageEntryStatus:uint8 { Undiscovered,Unrecorded,Recorded };
UENUM(BlueprintType) enum class EDWLanguagePanel:uint8 { None,Journal,Relic,Offering,Trade,Talents };
UENUM(BlueprintType) enum class EDWLanguageCarrierKind:uint8 { Inscription,Relic,Offering,Trade,WaterSource,Talents };
USTRUCT(BlueprintType) struct FDWLanguageClueView
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FName ClueId;
 UPROPERTY(BlueprintReadOnly) FName EntryId;
 UPROPERTY(BlueprintReadOnly) FName DisplayedEntryId;
 UPROPERTY(BlueprintReadOnly) FText Location;
 UPROPERTY(BlueprintReadOnly) FText Carrier;
 UPROPERTY(BlueprintReadOnly) FText Description;
 UPROPERTY(BlueprintReadOnly) bool bFromCompound=false;
};
USTRUCT(BlueprintType) struct FDWLanguageEntryView
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FName EntryId;
 UPROPERTY(BlueprintReadOnly) EDWLanguageEntryStatus Status=EDWLanguageEntryStatus::Undiscovered;
 UPROPERTY(BlueprintReadOnly) bool bCompound=false;
 UPROPERTY(BlueprintReadOnly) bool bCanEdit=false;
 UPROPERTY(BlueprintReadOnly) FString Guess;
 UPROPERTY(BlueprintReadOnly) int32 DiscoveryOrder=0;
 UPROPERTY(BlueprintReadOnly) TArray<FName> Parts;
 UPROPERTY(BlueprintReadOnly) TArray<FName> RelatedEntries;
 UPROPERTY(BlueprintReadOnly) TArray<FDWLanguageClueView> Clues;
};
USTRUCT(BlueprintType) struct FDWLanguageMeaningView
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FName MeaningId;
 UPROPERTY(BlueprintReadOnly) FText Label;
};
USTRUCT(BlueprintType) struct FDWRelicPuzzleView
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FName RelicId;
 UPROPERTY(BlueprintReadOnly) TArray<FName> GlyphEntries;
 UPROPERTY(BlueprintReadOnly) TArray<FDWLanguageMeaningView> Candidates;
 UPROPERTY(BlueprintReadOnly) bool bSolved=false;
 UPROPERTY(BlueprintReadOnly) FText Title;
 UPROPERTY(BlueprintReadOnly) FText Story;
 UPROPERTY(BlueprintReadOnly) FText StoryChinese;
 UPROPERTY(BlueprintReadOnly) FText StoryEnglish;
 UPROPERTY(BlueprintReadOnly) int32 RewardPoints=0;
};
USTRUCT(BlueprintType) struct FDWTalentNodeView
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FName TalentId;
 UPROPERTY(BlueprintReadOnly) FText Title;
 UPROPERTY(BlueprintReadOnly) FText Description;
 UPROPERTY(BlueprintReadOnly) FText Reason;
 UPROPERTY(BlueprintReadOnly) TArray<FName> Prerequisites;
 UPROPERTY(BlueprintReadOnly) int32 Cost=0;
 UPROPERTY(BlueprintReadOnly) int32 PaidCost=0;
 UPROPERTY(BlueprintReadOnly) bool bConfigured=false;
 UPROPERTY(BlueprintReadOnly) bool bLearned=false;
 UPROPERTY(BlueprintReadOnly) bool bCanLearn=false;
 UPROPERTY(BlueprintReadOnly) bool bCanForget=false;
};
USTRUCT(BlueprintType) struct FDWLanguageMechanismView
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FName MechanismId;
 UPROPERTY(BlueprintReadOnly) FName RequiredItemId;
 UPROPERTY(BlueprintReadOnly) FName RewardItemId;
 UPROPERTY(BlueprintReadOnly) int32 RequiredCount=0;
 UPROPERTY(BlueprintReadOnly) int32 SubmittedCount=0;
 UPROPERTY(BlueprintReadOnly) int32 UnitsPerSubmission=1;
 UPROPERTY(BlueprintReadOnly) int32 RewardCount=0;
 UPROPERTY(BlueprintReadOnly) bool bComplete=false;
 UPROPERTY(BlueprintReadOnly) bool bTrade=false;
 UPROPERTY(BlueprintReadOnly) FText Title;
 UPROPERTY(BlueprintReadOnly) FText Reason;
 UPROPERTY(BlueprintReadOnly) TArray<FName> GlyphEntries;
 UPROPERTY(BlueprintReadOnly) TArray<FName> RewardGlyphEntries;
};
