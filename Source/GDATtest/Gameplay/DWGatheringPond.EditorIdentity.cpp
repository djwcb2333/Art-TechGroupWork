#include "DWGatheringPond.h"
#if WITH_EDITOR
#include "DWGatheringPersistenceOptIn.h"
#include "DWGatheringWorld.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CoreGlobals.h"

bool ADWGatheringPond::CanPrepareNewEditorPondIdentity() const
{
    const UWorld* W=GetWorld();
    if(!W||W->WorldType!=EWorldType::Editor||W->bIsTearingDown||IsTemplate()||IsActorBeingDestroyed()||
        HasAnyFlags(RF_Transient|RF_ClassDefaultObject|RF_ArchetypeObject)||
        GIsReconstructingBlueprintInstances||GIsReinstancing.load()||
        GetClass()->HasAnyClassFlags(CLASS_NewerVersionExists)||
        GetClass()->GetName().StartsWith(TEXT("REINST_"))||GetClass()->GetName().StartsWith(TEXT("SKEL_"))||
        GetLevel()!=W->PersistentLevel)return false;
    const FString Map=W->GetOutermost()->GetName();
    return UDWGatheringPersistenceOptIn::ProfileAllowed(Map,TEXT("/Game/DoughWorld/Core/Data/DA_DoughWorldGameplay.DA_DoughWorldGameplay"))&&
        UDWGatheringPersistenceOptIn::ClassAllowed(Map,GetClass()->GetPathName());
}

void ADWGatheringPond::PrepareNewEditorPondIdentity(bool bInitializeNewAuthorship)
{
    if(!CanPrepareNewEditorPondIdentity())return;
    auto IsUsed=[this](FName Id)
    {
        for(TActorIterator<ADWGatheringPond> It(GetWorld());It;++It)
            if(*It!=this&&!It->IsActorBeingDestroyed()&&It->PersistentId==Id)return true;
        for(TActorIterator<ADWGatheringInteractable> It(GetWorld());It;++It)
            if(!It->IsActorBeingDestroyed()&&It->PersistentId==Id)return true;
        return false;
    };
    // A non-reflected member starts empty on each newly constructed duplicate.
    // Reuse it if creation/import callbacks both run; never re-key the source actor.
    if(PreparedNewEditorPondId.IsNone()||IsUsed(PreparedNewEditorPondId))
        do{PreparedNewEditorPondId=FName(*(TEXT("DWPond_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)));}while(IsUsed(PreparedNewEditorPondId));
    if(PersistentId==PreparedNewEditorPondId)return;
    const bool WasEmpty=PersistentId.IsNone();
    Modify();PersistentId=PreparedNewEditorPondId;
    // Only pristine creation may initialize authorship. Duplication/import preserves the copied choice.
    if(bInitializeNewAuthorship&&WasEmpty)bAuthoredPersistence=true;
    MarkPackageDirty();
}

void ADWGatheringPond::PostActorCreated()
{
    Super::PostActorCreated();
    if(PersistentId.IsNone())PrepareNewEditorPondIdentity(true);
}
void ADWGatheringPond::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
    Super::PostDuplicate(DuplicateMode);
    if(DuplicateMode!=EDuplicateMode::PIE)PrepareNewEditorPondIdentity(false);
}
void ADWGatheringPond::PostEditImport()
{
    Super::PostEditImport();PrepareNewEditorPondIdentity(false);
}
#endif
