#include "DWGatheringPersistenceOptIn.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"

const FDWGatheringMapOptIn* UDWGatheringPersistenceOptIn::Find(const FString& Map)
{
    const FString Canonical=UWorld::RemovePIEPrefix(Map);
    if(!FPackageName::IsValidLongPackageName(Canonical)||
       Canonical==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest")||
       Canonical==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GatheringTest"))return nullptr;
    const FDWGatheringMapOptIn* Found=nullptr;
    for(const auto& Entry:GetDefault<UDWGatheringPersistenceOptIn>()->Maps)
    {
        if(!Entry.bEnabled||Entry.MapPackage!=Canonical)continue;
        if(Found)return nullptr;
        Found=&Entry;
    }
    if(!Found||Found->ProfilePath.IsEmpty()||Found->AuditedAuthoredClassPaths.IsEmpty())return nullptr;
    TSet<FString> Seen;
    for(const FString& Class:Found->AuditedAuthoredClassPaths)
    {if(Class.IsEmpty()||Seen.Contains(Class))return nullptr;Seen.Add(Class);}
    return Found;
}
bool UDWGatheringPersistenceOptIn::ProfileAllowed(const FString& Map,const FString& Profile)
{const auto* C=Find(Map);return C&&!Profile.IsEmpty()&&C->ProfilePath==Profile;}
bool UDWGatheringPersistenceOptIn::ClassAllowed(const FString& Map,const FString& Class)
{const auto* C=Find(Map);return C&&!Class.IsEmpty()&&C->AuditedAuthoredClassPaths.Contains(Class);}
