#pragma once
#include "CoreMinimal.h"
#include "UObject/GCObject.h"
class FPreviewScene;
class FReferenceCollector;
class UObject;

struct FDWAttackVfxEntryOption
{
    FGuid Id;
    FText Label;
};

/** Editor-side adapter contract ONLY. Concrete implementation waits for Combat's
 * pure profile/visual-frame API. Never construct a gameplay Actor/World/GI. */
class IDWAttackVfxAuthoringBridge
{
public:
    virtual ~IDWAttackVfxAuthoringBridge()=default;
    virtual UObject* GetProfile() const=0;
    // Must be the pure runtime VFX profile, never Hero CDO/WeaponAnimationSet/Rules.
    virtual bool IsVisualOnlyProfile() const=0;
    virtual TArray<FDWAttackVfxEntryOption> Entries() const=0;
    virtual FGuid SelectedEntry() const=0;
    virtual void SelectEntry(FGuid Id)=0;
    // Caller owns FScopedTransaction/Profile.Modify/PostEditChange.
    virtual FGuid AddVisualEntry()=0;
    virtual bool RemoveVisualEntry(FGuid Id)=0;
    virtual bool ValidateForSave(FString& Error) const=0;
    virtual int32 RepairEditorEntryIds()=0;
    virtual void RebindAfterPropertyChangeOrUndo()=0;

    // Source seconds and shared runtime visual resolver; preview rate is not saved gameplay timing.
    virtual double SourceDurationSeconds() const=0;
    virtual bool PopulateTransientPreview(FPreviewScene& Scene,FString& Error)=0;
    virtual void ClearTransientPreview()=0;
    virtual void ResetPreviewSimulation()=0;
    // Exactly one fixed step; adapter sets pose with no Notifies, then shared frame,
    // then particles. Calls no gameplay Tick/PlayRole/damage/event-commit functions.
    virtual bool StepPreview(double SourceSeconds,float FixedDelta,FString& Error)=0;
    virtual FBox PreviewBounds() const=0;
    virtual bool ReadSelectedWorldTransform(FTransform& Out) const=0;
    // Shared resolver's parent frame -> local transform; reject singular parents.
    // Update selected preview component/marker immediately at the frozen pose.
    // Existing world-space particles may need replay after drag; disclose this limit.
    // Do not change source Skeleton/socket/PrimaryGrip or duplicate transform formula.
    virtual bool WriteSelectedWorldTransform(const FTransform& World,FString& Error)=0;
    virtual void CollectReferences(FReferenceCollector& Collector)=0;
    virtual FText SimulationLimitations() const=0;
};
