#include "DWGatheringWorldPersistence.h"
#include "DWRetiredItemPolicy.h"
#include "DWLootBeaconSubsystem.h"
#include "DWGatheringWorld.h"
#include "DWGatheringComponent.h"
#include "DWBuildingDemolitionCost.h"
#include "DWGatheringProfile.h"
#include "DWGatheringPond.h"
#include "DWGatheringPondPersistence.h"
#include "DWGatheringPersistenceOptIn.h"
#include "DWGatheringPresentationComponent.h"
#include "DWPickupPresentationComponent.h"
#include "DWPlaceableBuilding.h"
#include "DWBuildingDefinition.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatEffectsPersistence.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
FString MapOf(const UWorld& W){return UWorld::RemovePIEPrefix(W.GetOutermost()->GetName());}
FString PathOf(const AActor& A){return UWorld::RemovePIEPrefix(A.GetPathName());}
bool Current(const AActor* A,const UWorld* W){return IsValid(A)&&!A->IsActorBeingDestroyed()&&A->GetWorld()==W&&W&&!W->bIsTearingDown;}
bool SameLife(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B){return A.Entity==B.Entity&&A.Life==B.Life;}
bool HasItem(const TArray<FDWItemStack>& Bag,FName Id){return Bag.ContainsByPredicate([&](const auto& X){return X.ItemId==Id&&X.Quantity>0;});}
bool Equal(const FDWGatheringWorldSave& A,const FDWGatheringWorldSave& B)
{return FDWGatheringWorldSave::StaticStruct()->CompareScriptStruct(&A,&B,PPF_None);}
bool EmptyLegacy(const FDWGatheringWorldSave& S)
{return S.Schema==0&&S.MapPackage.IsEmpty()&&S.ActiveTool.IsNone()&&S.ToolCooldownRemaining==0.&&S.Actors.IsEmpty()&&S.Ponds.IsEmpty();}
bool SupportedActor(const ADWGatheringInteractable& A,FString& Error)
{
    if(A.PersistentId.IsNone()||A.PersistentId.ToString().Len()>160||A.PersistentId.ToString().Contains(TEXT("UEDPIE_"))||
        A.PersistentId.ToString().Contains(TEXT("/"))||A.PersistentId.ToString().Contains(TEXT(":")))
    {Error=TEXT("Gathering actor requires an explicit stable id without a PIE prefix.");return false;}
    if(Cast<ADWGatheringResource>(&A))
    {if(A.bRuntimeSpawned){Error=TEXT("Runtime-spawned gathering resources are not supported by LANG-01.");return false;}return true;}
    if(Cast<ADWGatheringPickup>(&A)||Cast<ADWGatheringStation>(&A))return true;
    Error=TEXT("Unsupported gathering actor kind; the complete save remains protected.");return false;
}
FString ResourceAuthorityId(const FString& Map,FName Id)
{return TEXT("gathering:resource:")+Map+TEXT(":")+Id.ToString();}
bool IsGatheringAuthorityId(const FString& Id)
{return Id.StartsWith(TEXT("gathering:"),ESearchCase::IgnoreCase);}
bool ResourceMatches(const FDWGatheringActorSave& Row,const ADWGatheringResource* R,UWorld* W,FString& Error)
{
    if(!Current(R,W)||!R->HasActorBegunPlay()||R->IsStaged()||R->bRuntimeSpawned||Row.bRuntimeSpawned||
        Row.Kind!=EDWGatheringSavedKind::Resource||R->PersistentId!=Row.Id||R->GetClass()->GetPathName()!=Row.ClassPath||PathOf(*R)!=Row.AuthoredPath)
    {Error=TEXT("Gathering authority resource does not match its authored id, map, class or path.");return false;}
    return true;
}
bool MergeAuthorityBindings(UWorld* W,const TArray<FDWCombatObjectBinding>& Base,const TArray<FDWCombatObjectBinding>& Added,
    TArray<FDWCombatObjectBinding>& Out,FString& Error)
{
    auto Candidate=Base;Candidate.Append(Added);TSet<FString> Names;TSet<TWeakObjectPtr<AActor>> Actors;
    for(const auto& V:Candidate)
    {
        if(V.StableId.IsEmpty()||V.StableId.Len()>4096||V.StableId.Len()!=FCString::Strlen(*V.StableId)||
            !Current(V.Actor.Get(),W)||!V.Actor->HasAuthority()||Names.Contains(V.StableId)||Actors.Contains(V.Actor))
        {Error=TEXT("Invalid, duplicate or ambiguous composite gathering authority binding.");return false;}
        Names.Add(V.StableId);Actors.Add(V.Actor);
    }
    Out=MoveTemp(Candidate);return true;
}
FSoftObjectPath PersistentMaterial(UMaterialInterface* M)
{
    if(auto* Dynamic=Cast<UMaterialInstanceDynamic>(M))M=Dynamic->Parent;
    return M&&M->GetOutermost()!=GetTransientPackage()&&!M->HasAnyFlags(RF_Transient)?FSoftObjectPath(M):FSoftObjectPath();
}
}

struct FDWGatheringPreparedActor
{
    FDWGatheringActorSave Saved;
    TWeakObjectPtr<ADWGatheringInteractable> Actor;
    TStrongObjectPtr<UStaticMesh> PickupMesh;
    TArray<TStrongObjectPtr<UMaterialInterface>> Materials;
};
struct FDWGatheringPreparedAuthority
{
    FDWCombatActorIdentitySave Saved;
    TWeakObjectPtr<ADWGatheringResource> Actor;
};
struct FDWGatheringPreparedPond
{
    FDWGatheringPondSave Saved;
    TWeakObjectPtr<ADWGatheringPond> Actor;
};
struct FDWGatheringRestoreState
{
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    TWeakObjectPtr<UDWGatheringComponent> Gathering;
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority;
    uint64 OldEpoch=0,NewEpoch=0;
    DWCombatCommit::FEntityLife OldLife,NewLife;
    double At=0.;int64 InventoryRevision=0;
    FDWGatheringWorldSave Before,Saved;
    TArray<TWeakObjectPtr<ADWGatheringInteractable>> Original,Staged,Retire;
    TArray<FDWGatheringPreparedActor> Actors;
    TArray<FDWGatheringPreparedPond> Ponds;
    TArray<FDWGatheringPreparedAuthority> ResourceAuthority;
    bool bAuthorityBindingsPrepared=false;
    bool bNoOp=false,bAdopted=false,bPublished=false;
};
FDWGatheringRestorePlan::FDWGatheringRestorePlan()=default;
FDWGatheringRestorePlan::~FDWGatheringRestorePlan(){DiscardStagedObjects();}
FDWGatheringRestorePlan::FDWGatheringRestorePlan(FDWGatheringRestorePlan&&)=default;
FDWGatheringRestorePlan& FDWGatheringRestorePlan::operator=(FDWGatheringRestorePlan&& Other)
{if(this!=&Other){DiscardStagedObjects();State=MoveTemp(Other.State);}return *this;}
void FDWGatheringRestoreStateDeleter::operator()(FDWGatheringRestoreState* P)const{delete P;}
void FDWGatheringRestorePlan::DiscardStagedObjects()
{
    if(!State||State->bAdopted)return;
    for(auto A:State->Staged)if(A.IsValid()&&!A->IsActorBeingDestroyed())A->Destroy();
    State->Staged.Reset();
}
bool FDWGatheringRestorePlan::CanCommit(FString& Error)const
{
    if(!State||State->bAdopted){Error=TEXT("Gathering restore plan unavailable or already adopted.");return false;}
    const auto& C=*State;if(C.bNoOp){Error.Reset();return true;}
    if(!Current(C.Player.Get(),C.World.Get())||!C.Inventory.IsValid()||!C.Gathering.IsValid()||!C.Authority.IsValid()||
        C.World->GetTimeSeconds()!=C.At||C.Player->GetInventory()!=C.Inventory.Get()||
        C.Inventory->GetInventoryRevision()!=C.InventoryRevision||UDWCombatAuthoritySubsystem::Get(C.Player.Get())!=C.Authority.Get()||
        C.Authority->GetWorldEpoch()!=C.OldEpoch||!SameLife(C.Authority->FindRegisteredActorLife(C.Player.Get()),C.OldLife))
    {Error=TEXT("Gathering restore world, life, inventory or clock changed during preparation.");return false;}
    for(auto A:C.Original)if(!Current(A.Get(),C.World.Get())||A->IsStaged())
    {Error=TEXT("Original gathering actor ended or changed staging during preparation.");return false;}
    for(auto A:C.Staged)if(!Current(A.Get(),C.World.Get())||!A->IsStaged())
    {Error=TEXT("A staged gathering candidate ended or was published prematurely.");return false;}
    for(const auto& Row:C.Actors)
    {
        if(!Current(Row.Actor.Get(),C.World.Get())){Error=TEXT("Prepared gathering actor no longer exists.");return false;}
        if(Row.Saved.Kind==EDWGatheringSavedKind::Pickup&&!Row.Saved.PickupMesh.IsNull()&&!Row.PickupMesh.IsValid())
        {Error=TEXT("Prepared pickup mesh is unavailable at commit.");return false;}
    }
    for(const auto& Row:C.Ponds)if(!Current(Row.Actor.Get(),C.World.Get()))
    {Error=TEXT("Prepared authored pond ended before aggregate adoption.");return false;}
    FDWGatheringWorldSave Now;
    if(!FDWGatheringWorldPersistence::CaptureInternal(*C.Player.Get(),Now,Error)||!Equal(Now,C.Before))
    {if(Error.IsEmpty())Error=TEXT("Gathering scene facts changed before aggregate adoption.");return false;}
    Error.Reset();return true;
}

bool FDWGatheringWorldPersistence::IsSupportedMap(const FString& Map)
{return UWorld::RemovePIEPrefix(Map)==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest")||UDWGatheringPersistenceOptIn::Find(Map)!=nullptr;}
bool FDWGatheringWorldPersistence::IsApprovedProfilePath(const FString& Path)
{return Path==TEXT("/Game/DoughWorld/Core/Data/DA_DWLanguagePlaytest.DA_DWLanguagePlaytest");}
bool FDWGatheringWorldPersistence::IsApprovedProfileForMap(const FString& Map,const FString& Path)
{if(UDWGatheringPersistenceOptIn::Find(Map))return UDWGatheringPersistenceOptIn::ProfileAllowed(Map,Path);return IsApprovedProfilePath(Path);}
bool FDWGatheringWorldPersistence::IsApprovedBuildingIdentity(FName Item,const FString& Definition,const FString& Class)
{
    // This fixed LANG-01 subset must be re-audited if any of these three Blueprint lifecycles changes.
    struct FApproved{const TCHAR* Item;const TCHAR* Definition;const TCHAR* Class;};
    static const FApproved Approved[]={
        {TEXT("CraftingTable"),TEXT("/Game/DoughWorld/Core/Data/DA_DWBuilding_CraftingTable.DA_DWBuilding_CraftingTable"),TEXT("/Game/DoughWorld/Gameplay/Buildings/BP_DWBuilding_CraftingTable.BP_DWBuilding_CraftingTable_C")},
        {TEXT("Furnace"),TEXT("/Game/DoughWorld/Core/Data/DA_DWBuilding_Furnace.DA_DWBuilding_Furnace"),TEXT("/Game/DoughWorld/Gameplay/Buildings/BP_DWBuilding_Furnace.BP_DWBuilding_Furnace_C")},
        {TEXT("FermentationVat"),TEXT("/Game/DoughWorld/Core/Data/DA_DWBuilding_FermentationVat.DA_DWBuilding_FermentationVat"),TEXT("/Game/DoughWorld/Gameplay/Buildings/BP_DWBuilding_FermentationVat.BP_DWBuilding_FermentationVat_C")}};
    for(const auto& A:Approved)if(Item==FName(A.Item))return Definition==A.Definition&&Class==A.Class;
    return false;
}
bool FDWGatheringWorldPersistence::MakeNewAdventure(const FString& Map,FDWGatheringWorldSave& Out,FString& Error)
{
    FDWGatheringWorldSave S;
    if(IsSupportedMap(Map)){S.Schema=UDWGatheringPersistenceOptIn::Find(Map)?2:1;S.MapPackage=UWorld::RemovePIEPrefix(Map);}
    if(!Validate(S,Map,{},true,Error))return false;Out=MoveTemp(S);return true;
}
bool FDWGatheringWorldPersistence::Validate(const FDWGatheringWorldSave& S,const FString& Map,
    const TArray<FDWItemStack>& Inventory,bool bNewAdventure,FString& Error)
{
    if(!IsSupportedMap(Map))
    {if(!EmptyLegacy(S)){Error=TEXT("Non-LANG maps cannot adopt a LANG gathering block.");return false;}Error.Reset();return true;}
    const int32 ExpectedSchema=UDWGatheringPersistenceOptIn::Find(Map)?2:1;
    if(S.Schema!=ExpectedSchema||S.MapPackage!=UWorld::RemovePIEPrefix(Map)||!FMath::IsFinite(S.ToolCooldownRemaining)||
        S.ToolCooldownRemaining<0.||S.ToolCooldownRemaining>86400.||S.Actors.Num()>16384)
    {Error=TEXT("Missing/invalid LANG gathering schema, map, cooldown or population.");return false;}
    if(bNewAdventure&&(!S.ActiveTool.IsNone()||S.ToolCooldownRemaining!=0.||!S.Actors.IsEmpty()||!S.Ponds.IsEmpty()))
    {Error=TEXT("A new LANG adventure cannot contain previously running gathering facts.");return false;}
    if(!S.ActiveTool.IsNone()&&!HasItem(Inventory,S.ActiveTool))
    {Error=TEXT("Saved active gathering tool is not present in the same inventory snapshot.");return false;}
    TSet<FName> Ids;
    for(const auto& A:S.Actors)
    {
        if(A.Id.IsNone()||Ids.Contains(A.Id)||A.Id.ToString().Len()>160||A.Id.ToString().Contains(TEXT("UEDPIE_"))||A.Id.ToString().Contains(TEXT("/"))||A.Id.ToString().Contains(TEXT(":"))||
            A.ClassPath.IsEmpty()||!A.Transform.IsValid()||!A.MeshTransform.IsValid()||
            (!A.bRuntimeSpawned&&(A.AuthoredPath.IsEmpty()||A.AuthoredPath.Contains(TEXT("UEDPIE_"))))||
            (A.bRuntimeSpawned&&!A.AuthoredPath.IsEmpty()))
        {Error=TEXT("Invalid/duplicate gathering id, transform, class or authored path.");return false;}Ids.Add(A.Id);
        if(S.Schema==2&&!A.bRuntimeSpawned&&!UDWGatheringPersistenceOptIn::ClassAllowed(S.MapPackage,A.ClassPath))
        {Error=TEXT("Authored gathering class is outside the audited map capability.");return false;}
        if(A.Kind==EDWGatheringSavedKind::Resource)
        {
            if(A.bRuntimeSpawned||!FMath::IsFinite(A.MaxDurability)||!FMath::IsFinite(A.Durability)||A.MaxDurability<1.||
                A.Durability<0.||A.Durability>A.MaxDurability||A.bDropsReleased!=(A.Durability==0.)||A.RequiredTool.IsNone()||A.Drops.Num()>256)
            {Error=TEXT("Resource snapshot is invalid or still has uncommitted pending drops; wait for gathering to finish.");return false;}
            for(const auto& D:A.Drops)if(D.ItemId.IsNone()||D.Quantity<=0){Error=TEXT("Invalid saved resource reward definition.");return false;}
        }
        else if(A.Kind==EDWGatheringSavedKind::Pickup)
        {
            if(A.ItemId.IsNone()||A.Quantity<0||A.PickupMaterials.Num()>64||
                (A.bRuntimeSpawned&&A.ClassPath!=ADWGatheringPickup::StaticClass()->GetPathName()))
            {Error=TEXT("Invalid saved pickup or unapproved runtime pickup class.");return false;}
        }
        else if(A.Kind==EDWGatheringSavedKind::Station)
        {
            if((A.bRuntimeSpawned&&A.bDemolished)||A.DemolitionCostOrigin>4||A.DemolitionCostInputs.Num()>256||
               ((A.DemolitionCostOrigin==0||A.DemolitionCostOrigin==2)&&(!A.DemolitionCostRecipe.IsNone()||!A.DemolitionCostInputs.IsEmpty()))||
               ((A.DemolitionCostOrigin==1||A.DemolitionCostOrigin==3||A.DemolitionCostOrigin==4)&&(A.DemolitionCostRecipe.IsNone()||A.DemolitionCostInputs.IsEmpty())))
            {Error=TEXT("Invalid saved building cost snapshot metadata.");return false;}
            if(A.ItemId.IsNone()||A.StationId.IsNone()||(A.bPlaceableBuilding&&A.BuildingDefinition.IsNull())||
                (A.bRuntimeSpawned&&!A.bPlaceableBuilding&&A.ClassPath!=ADWGatheringStation::StaticClass()->GetPathName()))
            {Error=TEXT("Invalid station or unapproved runtime station class.");return false;}
        }
        else{Error=TEXT("Unknown gathering save actor kind.");return false;}
    }
    if(S.Schema==1&&!S.Ponds.IsEmpty()){Error=TEXT("Legacy gathering schema cannot carry pond facts.");return false;}
    if(S.Ponds.Num()>4096){Error=TEXT("Pond population exceeds limit.");return false;}
    for(const auto& Pond:S.Ponds)
    {
        if(S.Schema!=2||!FDWGatheringPondPersistence::Validate(Pond,Error)||Ids.Contains(Pond.Id)||
           !Pond.AuthoredPath.StartsWith(S.MapPackage+TEXT("."))||
           !UDWGatheringPersistenceOptIn::ClassAllowed(S.MapPackage,Pond.ClassPath))
        {if(Error.IsEmpty())Error=TEXT("Pond identity/class conflicts with the audited gathering world.");return false;}
        Ids.Add(Pond.Id);
    }
    Error.Reset();return true;
}
bool FDWGatheringWorldPersistence::ValidateForConfig(const UDWGameplayConfig& Config,const FDWGatheringWorldSave& S,
    const TArray<FDWItemStack>& Inventory,FString& Error)
{
    if(S.Schema==0){if(!EmptyLegacy(S)){Error=TEXT("Legacy gathering block contains hidden state.");return false;}Error.Reset();return true;}
    if(!IsApprovedProfileForMap(S.MapPackage,Config.GetPathName()))
    {Error=TEXT("Only the dedicated audited LANG-01 gathering profile may load a nonempty gathering block.");return false;}
    if(!Validate(S,S.MapPackage,Inventory,false,Error)||!Config.ValidateGatheringDefinitions(Error))return false;
    if(!S.ActiveTool.IsNone()&&!Config.Tools.Contains(S.ActiveTool))
    {Error=TEXT("Saved active tool is absent from the current gathering profile.");return false;}
    for(const auto& A:S.Actors)
    {
        if(A.Kind==EDWGatheringSavedKind::Resource)
        {for(const auto& D:A.Drops)if(!Config.IsInventoryItem(D.ItemId)||!Config.GetItemDefinition(D.ItemId))
            {Error=TEXT("Saved resource reward no longer exists in this inventory profile.");return false;}}
        else if(A.Kind==EDWGatheringSavedKind::Pickup)
        {if(!(DWRetiredItemPolicy::IsRetired(A.ItemId)&&A.Quantity==0)&&(!Config.IsInventoryItem(A.ItemId)||!Config.GetItemDefinition(A.ItemId)))
            {Error=TEXT("Saved pickup item no longer exists in this inventory profile.");return false;}}
        else
        {
            if(A.DemolitionCostOrigin==1||A.DemolitionCostOrigin==3||A.DemolitionCostOrigin==4)
            {
                FDWBuildingDemolitionCost Cost;Cost.RecipeId=A.DemolitionCostRecipe;Cost.Inputs=A.DemolitionCostInputs;
                if(!FDWBuildingDemolitionCost::Validate(Config,A.ItemId,Cost,Error))return false;
            }
            const auto* D=Config.GetBuildingDefinition(A.ItemId);
            if(!D||!D->ValidateDefinition(Error)||D->StationId!=A.StationId||
                !IsApprovedBuildingIdentity(A.ItemId,D->GetPathName(),D->ActorClass.ToSoftObjectPath().ToString())||
                (A.bPlaceableBuilding&&(FSoftObjectPath(D)!=A.BuildingDefinition||D->ActorClass.ToSoftObjectPath().ToString()!=A.ClassPath))||
                (!A.bPlaceableBuilding&&A.ClassPath!=ADWGatheringStation::StaticClass()->GetPathName()))
            {Error=TEXT("Saved building/station differs from the current registered definition.");return false;}
        }
    }
    Error.Reset();return true;
}
bool FDWGatheringWorldPersistence::SupportsWorld(UWorld& W,FString& Error)
{
    const auto Reject=[&](const FString& Reason,const AActor* Actor=nullptr)
    {
        const FString Detail=Actor?FString::Printf(TEXT("%s [actor=%s class=%s]"),*Reason,*Actor->GetPathName(),*Actor->GetClass()->GetPathName()):Reason;
        Error=Detail;return false;
    };
    if(!IsSupportedMap(MapOf(W))||W.bIsTearingDown)
    {Error=TEXT("Gathering persistence is only installed for the LANG-01 map.");return Reject(Error);}
    if(UDWGatheringPersistenceOptIn::Find(MapOf(W))&&
       (W.IsPartitionedWorld()||W.WorldComposition||!W.GetStreamingLevels().IsEmpty()||W.GetLevels().Num()!=1||W.GetLevels()[0]!=W.PersistentLevel))
    {Error=TEXT("Opt-in gathering save supports one fully resident non-partitioned persistent level only.");return Reject(Error);}
    auto* GI=W.GetGameInstance<UDWGameInstance>();const auto* Config=GI?UDWGameplayConfig::AsGatheringConfig(GI->GetConfig()):nullptr;
    if(!Config||!IsApprovedProfileForMap(MapOf(W),Config->GetPathName())||!Config->ValidateGatheringDefinitions(Error))
    {if(Error.IsEmpty())Error=TEXT("The dedicated audited LANG-01 profile is not active.");return Reject(Error);}
    TSet<FName> Ids;
    for(TActorIterator<ADWGatheringInteractable> It(&W);It;++It)
    {
        if(It->IsActorBeingDestroyed()||It->IsStaged())continue;
        if(!SupportedActor(**It,Error))return Reject(Error,*It);
        if(UDWGatheringPersistenceOptIn::Find(MapOf(W))&&!It->bRuntimeSpawned&&
            !UDWGatheringPersistenceOptIn::ClassAllowed(MapOf(W),It->GetClass()->GetPathName()))
        {Error=TEXT("Authored gathering class has not been audited for this map.");return Reject(Error,*It);}
        // PERMANENT representation only. Never inspect BeginPlay, visual readiness, durability,
        // pending rewards or collection effects here: those are retryable Capture/Prepare boundaries.
        if(auto* Pickup=Cast<ADWGatheringPickup>(*It))
        {
            if(Pickup->bRuntimeSpawned&&Pickup->GetClass()!=ADWGatheringPickup::StaticClass())
            {Error=TEXT("Runtime pickup class is outside the audited native subset.");return Reject(Error,*It);}
        }
        else if(auto* Station=Cast<ADWGatheringStation>(*It))
        {
            auto* Building=Cast<ADWPlaceableBuilding>(Station);
            auto* Definition=Building?Building->Definition.Get():Config->GetBuildingDefinition(Station->BuildingItemId);
            const FName Item=Definition?Definition->BuildingItemId:NAME_None;
            if(!Definition||!Definition->ValidateDefinition(Error)||
                !IsApprovedBuildingIdentity(Item,Definition->GetPathName(),Definition->ActorClass.ToSoftObjectPath().ToString())||
                (Building&&Definition->ActorClass.ToSoftObjectPath().ToString()!=Building->GetClass()->GetPathName())||
                (!Building&&Station->GetClass()!=ADWGatheringStation::StaticClass()))
            {Error=TEXT("Building type/definition is outside the three audited LANG-01 Blueprint identities.");return Reject(Error,*It);}
            // Derived BuildingItemId/StationId and bVisualsReady are assigned by BeginPlay's
            // RebuildBuildingVisuals; an earlier actor's publication must not permanently reject them.
        }
        if(Ids.Contains(It->PersistentId)){Error=TEXT("Duplicate live gathering stable identity.");return Reject(Error,*It);}Ids.Add(It->PersistentId);
    }
    for(TActorIterator<ADWGatheringPond> It(&W);It;++It)if(!It->IsActorBeingDestroyed())
    {
        if(It->PersistentId.IsNone())
        {Error=TEXT("Authored pond is missing its explicit persistent ID.");return Reject(Error,*It);}
        if(Ids.Contains(It->PersistentId))
        {Error=TEXT("Duplicate pond/gathering persistent ID: ")+It->PersistentId.ToString();return Reject(Error,*It);}
        if(!UDWGatheringPersistenceOptIn::Find(MapOf(W))||!It->bAuthoredPersistence||
           !UDWGatheringPersistenceOptIn::ClassAllowed(MapOf(W),It->GetClass()->GetPathName()))
        {Error=TEXT("Pond is not an explicitly authored, unique, audited participant; original protection remains.");return Reject(Error,*It);}
        Ids.Add(It->PersistentId);
    }
    Error.Reset();return true;
}
bool FDWGatheringWorldPersistence::Capture(ADWPlayerCharacter& P,FDWGatheringWorldSave& Out,FString& Error)
{return CaptureInternal(P,Out,Error);}
bool FDWGatheringWorldPersistence::CaptureInternal(ADWPlayerCharacter& P,FDWGatheringWorldSave& Out,FString& Error)
{
    UWorld* W=P.GetWorld();if(!Current(&P,W)){Error=TEXT("Gathering capture needs a live player/world.");return false;}
    FDWGatheringWorldSave S;
    if(!IsSupportedMap(MapOf(*W)))
    {
        for(TActorIterator<ADWGatheringInteractable> It(W);It;++It)if(!It->IsActorBeingDestroyed()&&!It->IsStaged())
        {Error=TEXT("Unregistered gathering world remains protected from partial saves.");return false;}
        for(TActorIterator<ADWGatheringPond> It(W);It;++It)if(!It->IsActorBeingDestroyed())
        {Error=TEXT("Unregistered pond world remains protected from partial saves.");return false;}
        Out=MoveTemp(S);Error.Reset();return true;
    }
    if(!SupportsWorld(*W,Error)||!P.GetInventory())return false;
    const auto* Config=UDWGameplayConfig::AsGatheringConfig(P.GetGameplayConfig());
    auto* G=P.FindComponentByClass<UDWGatheringComponent>();
    if(!Config||!G||!G->CaptureGatheringPersistence(S.ActiveTool,S.ToolCooldownRemaining,Error))return false;
    S.Schema=UDWGatheringPersistenceOptIn::Find(MapOf(*W))?2:1;S.MapPackage=MapOf(*W);
    for(TActorIterator<ADWGatheringInteractable> It(W);It;++It)
    {
        auto* A=*It;if(A->IsActorBeingDestroyed()||A->IsStaged())continue;
        if(!A->HasActorBegunPlay()||!A->Mesh||!A->GetActorTransform().IsValid()||!A->Mesh->GetRelativeTransform().IsValid())
        {Error=TEXT("Gathering participant initialization is still in progress; retry saving after BeginPlay.");return false;}
        FDWGatheringActorSave R;R.Id=A->PersistentId;R.bRuntimeSpawned=A->bRuntimeSpawned;
        R.AuthoredPath=R.bRuntimeSpawned?FString():PathOf(*A);R.ClassPath=A->GetClass()->GetPathName();
        R.Transform=A->GetActorTransform();R.MeshTransform=A->Mesh->GetRelativeTransform();
        if(auto* Resource=Cast<ADWGatheringResource>(A))
        {
            if(!Resource->CanCaptureGatheringPersistence(Error))return false;
            R.Kind=EDWGatheringSavedKind::Resource;R.MaxDurability=Resource->MaxDurability;R.Durability=Resource->Durability;
            R.bDropsReleased=Resource->bDropsReleased;R.RequiredTool=Resource->RequiredTool;R.Drops=Resource->Drops;
        }
        else if(auto* Pickup=Cast<ADWGatheringPickup>(A))
        {
            if(!Pickup->CanCaptureGatheringPersistence(Error))return false;
            R.Kind=EDWGatheringSavedKind::Pickup;R.ItemId=Pickup->ItemId;R.Quantity=Pickup->Quantity;
            R.PickupMesh=FSoftObjectPath(Pickup->Mesh->GetStaticMesh());
            for(int32 I=0;I<Pickup->Mesh->GetNumMaterials();++I)R.PickupMaterials.Add(PersistentMaterial(Pickup->Mesh->GetMaterial(I)));
        }
        else if(auto* Station=Cast<ADWGatheringStation>(A))
        {
            R.Kind=EDWGatheringSavedKind::Station;R.ItemId=Station->BuildingItemId;R.StationId=Station->StationId;R.bEnabled=Station->bStationEnabled;
            R.DemolitionCostOrigin=Station->DemolitionCostOrigin;R.DemolitionCostRecipe=Station->DemolitionCostRecipe;
            R.DemolitionCostInputs=Station->DemolitionCostInputs;R.bDemolished=Station->bDemolished;
            if(auto* B=Cast<ADWPlaceableBuilding>(A)){R.bPlaceableBuilding=true;R.BuildingDefinition=FSoftObjectPath(B->Definition.Get());
                if(!B->HasCompleteBuildingVisuals()){Error=TEXT("Placed building visuals have not completed staging.");return false;}}
        }
        else{Error=TEXT("Unsupported gathering participant during capture.");return false;}
        S.Actors.Add(MoveTemp(R));
    }
    S.Actors.Sort([](const auto& A,const auto& B){return A.Id.LexicalLess(B.Id);});
    if(S.Schema==2)for(TActorIterator<ADWGatheringPond> It(W);It;++It)if(!It->IsActorBeingDestroyed())
    {FDWGatheringPondSave Pond;if(!FDWGatheringPondPersistence::Capture(**It,Pond,Error))return false;S.Ponds.Add(MoveTemp(Pond));}
    S.Ponds.Sort([](const auto& A,const auto& B){return A.Id.LexicalLess(B.Id);});
    if(!Validate(S,S.MapPackage,P.GetInventory()->GetSlots(),false,Error)||!ValidateForConfig(*Config,S,P.GetInventory()->GetSlots(),Error))return false;
    Out=MoveTemp(S);Error.Reset();return true;
}

bool FDWGatheringWorldPersistence::ValidateAuthorityReferences(const FDWGatheringWorldSave& S,
    const FDWCombatAuthoritySave& Authority,FString& Error)
{
    if(!DWCombatEffectsSave::ValidateAuthority(Authority,Error))return false;
    if(S.Schema==0)
    {
        if(!EmptyLegacy(S)){Error=TEXT("Legacy gathering block cannot conceal authority facts.");return false;}
        for(const auto& A:Authority.Actors)if(IsGatheringAuthorityId(A.StableId))
        {Error=TEXT("Legacy/non-LANG gathering cannot carry typed authority references.");return false;}
        Error.Reset();return true;
    }
    if((S.Schema!=1&&S.Schema!=2)||S.MapPackage!=UWorld::RemovePIEPrefix(S.MapPackage)||!IsSupportedMap(S.MapPackage))
    {Error=TEXT("Typed gathering authority requires the exact supported map/schema.");return false;}
    TSet<FName> Seen;TMap<FString,const FDWGatheringActorSave*> Resources;
    for(const auto& Row:S.Actors)
    {
        if(Row.Id.IsNone()||Seen.Contains(Row.Id)){Error=TEXT("Duplicate/empty gathering authority source id.");return false;}Seen.Add(Row.Id);
        if(Row.Kind!=EDWGatheringSavedKind::Resource)continue;
        if(Row.bRuntimeSpawned||Row.ClassPath.IsEmpty()||Row.AuthoredPath.IsEmpty()||Row.Id.ToString().Len()>160||
            Row.Id.ToString().Contains(TEXT("UEDPIE_"))||Row.Id.ToString().Contains(TEXT("/"))||Row.Id.ToString().Contains(TEXT(":")))
        {Error=TEXT("Typed gathering authority requires a stable authored Resource row.");return false;}
        Resources.Add(ResourceAuthorityId(S.MapPackage,Row.Id),&Row);
    }
    for(const auto& A:Authority.Actors)if(IsGatheringAuthorityId(A.StableId))
    {
        // ValidateAuthority already proves one actor per entity and exact Ledger.Lives agreement.
        if(!Resources.Contains(A.StableId)||A.Kind!=EDWCombatSavedActorKind::Other||A.bDead||A.bEnded||!A.Life.Entity||!A.Life.Life||!A.RestoreRevision)
        {Error=TEXT("Typed gathering authority record has no unique same-map Resource row or valid native life.");return false;}
    }
    Error.Reset();return true; // No reverse requirement: never-touched Resources remain unregistered.
}
bool FDWGatheringWorldPersistence::AppendCaptureAuthorityBindings(ADWPlayerCharacter& P,const FDWGatheringWorldSave& S,
    TArray<FDWCombatObjectBinding>& InOut,FString& Error)
{
    if(S.Schema==0){if(!EmptyLegacy(S)){Error=TEXT("Invalid legacy gathering binding source.");return false;}Error.Reset();return true;}
    auto* W=P.GetWorld();auto* Authority=UDWCombatAuthoritySubsystem::Get(&P);
    if(!Current(&P,W)||!Authority||(S.Schema!=1&&S.Schema!=2)||S.MapPackage!=MapOf(*W))
    {Error=TEXT("Gathering authority export requires its live same-map LANG capture context.");return false;}
    if(!SupportsWorld(*W,Error))return false;
    TMap<FName,ADWGatheringResource*> Live;
    for(TActorIterator<ADWGatheringResource> It(W);It;++It)if(!It->IsActorBeingDestroyed()&&!It->IsStaged())
    {if(Live.Contains(It->PersistentId)){Error=TEXT("Duplicate live Resource authority source.");return false;}Live.Add(It->PersistentId,*It);}
    TArray<FDWCombatObjectBinding> Added;int32 ResourceCount=0;
    for(const auto& Row:S.Actors)if(Row.Kind==EDWGatheringSavedKind::Resource)
    {
        ++ResourceCount;auto* R=Live.FindRef(Row.Id);if(!ResourceMatches(Row,R,W,Error))return false;
        const auto Life=Authority->FindRegisteredActorLife(R);
        if((Life.Entity==0)!=(Life.Life==0)){Error=TEXT("Partial Resource authority identity.");return false;}
        if(Life.Entity)Added.Add({ResourceAuthorityId(S.MapPackage,Row.Id),R});
    }
    if(ResourceCount!=Live.Num()){Error=TEXT("Resource population changed since gathering capture.");return false;}
    TArray<FDWCombatObjectBinding> Merged;if(!MergeAuthorityBindings(W,InOut,Added,Merged,Error))return false;
    InOut=MoveTemp(Merged);Error.Reset();return true;
}
bool FDWGatheringRestorePlan::AppendAuthorityBindings(const FDWCombatAuthoritySave& SavedAuthority,
    TArray<FDWCombatObjectBinding>& Live,TArray<FDWCombatObjectBinding>& Restored,FString& Error)
{
    if(!State||State->bAdopted||State->bAuthorityBindingsPrepared)
    {Error=TEXT("Gathering authority bindings are unavailable or already prepared.");return false;}
    auto& C=*State;
    if(!FDWGatheringWorldPersistence::ValidateAuthorityReferences(C.Saved,SavedAuthority,Error))return false;
    if(C.bNoOp){C.bAuthorityBindingsPrepared=true;Error.Reset();return true;}
    if(!CanCommit(Error))return false; // WorldPlan.CanCommit is deliberately not called before PrepareWorldFacts.
    auto CompositeLive=Live;
    if(!FDWGatheringWorldPersistence::AppendCaptureAuthorityBindings(*C.Player.Get(),C.Before,CompositeLive,Error))return false;
    TArray<FDWCombatObjectBinding> Added;TArray<FDWGatheringPreparedAuthority> Expected;
    for(const auto& A:SavedAuthority.Actors)if(IsGatheringAuthorityId(A.StableId))
    {
        const auto* Row=C.Actors.FindByPredicate([&](const auto& V)
        {return V.Saved.Kind==EDWGatheringSavedKind::Resource&&ResourceAuthorityId(C.Saved.MapPackage,V.Saved.Id)==A.StableId;});
        auto* R=Row?Cast<ADWGatheringResource>(Row->Actor.Get()):nullptr;
        if(!Row||!ResourceMatches(Row->Saved,R,C.World.Get(),Error))return false;
        // Saved registration is authoritative even if this new world's same actor has never been hit.
        Added.Add({A.StableId,R});Expected.Add({A,R});
    }
    TArray<FDWCombatObjectBinding> CompositeRestored;
    if(!MergeAuthorityBindings(C.World.Get(),Restored,Added,CompositeRestored,Error))return false;
    Live=MoveTemp(CompositeLive);Restored=MoveTemp(CompositeRestored);C.ResourceAuthority=MoveTemp(Expected);
    C.bAuthorityBindingsPrepared=true;Error.Reset();return true;
}
bool FDWGatheringRestorePlan::ValidateRestoredAuthority(const FDWCombatAuthorityRestorePlan& Identity,FString& Error)const
{
    if(!State||State->bAdopted||!State->bAuthorityBindingsPrepared)
    {Error=TEXT("Gathering authority mapping was not prepared.");return false;}
    const auto& C=*State;if(C.bNoOp){Error.Reset();return true;}
    if(!Identity.NewEpoch()){Error=TEXT("Gathering authority destination epoch is unavailable.");return false;}
    for(const auto& Row:C.ResourceAuthority)
    {
        if(!Current(Row.Actor.Get(),C.World.Get())||Identity.FindActor(Row.Saved.StableId)!=Row.Actor.Get()||
            Identity.FindActor(Row.Saved.Life.Entity)!=Row.Actor.Get()||!SameLife(Identity.FindLife(Row.Saved.Life.Entity),Row.Saved.Life)||
            Identity.FindRestoreRevision(Row.Saved.Life.Entity)!=Row.Saved.RestoreRevision)
        {Error=TEXT("Prepared Resource binding does not preserve saved native life/restore revision.");return false;}
    }
    Error.Reset();return true;
}

bool FDWGatheringWorldPersistence::Prepare(ADWPlayerCharacter& P,const FDWGatheringWorldSave& S,
    const TArray<FDWItemStack>& SavedInventory,FDWGatheringRestorePlan& Out,FString& Error)
{
    auto* W=P.GetWorld();if(!Current(&P,W)||!Validate(S,MapOf(*W),SavedInventory,false,Error))return false;
    FDWGatheringRestorePlan Plan;Plan.State.Reset(new FDWGatheringRestoreState);auto& C=*Plan.State;
    if(S.Schema==0){FDWGatheringWorldSave Empty;if(!CaptureInternal(P,Empty,Error))return false;C.bNoOp=true;Out=MoveTemp(Plan);Error.Reset();return true;}
    auto* Config=UDWGameplayConfig::AsGatheringConfig(P.GetGameplayConfig());
    auto* G=P.FindComponentByClass<UDWGatheringComponent>();auto* I=P.GetInventory();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    if(!Config||!G||!I||!A||!ValidateForConfig(*Config,S,SavedInventory,Error))return false;
    C.World=W;C.Player=&P;C.Inventory=I;C.Gathering=G;C.Authority=A;C.At=W->GetTimeSeconds();C.OldEpoch=A->GetWorldEpoch();
    C.OldLife=A->FindRegisteredActorLife(&P);C.InventoryRevision=I->GetInventoryRevision();C.Saved=S;
    if(!CaptureInternal(P,C.Before,Error))return false;
    TMap<FName,ADWGatheringPond*> LivePonds;
    for(TActorIterator<ADWGatheringPond> It(W);It;++It)if(!It->IsActorBeingDestroyed())
    {if(LivePonds.Contains(It->PersistentId)){Error=TEXT("Duplicate live pond identity.");return false;}LivePonds.Add(It->PersistentId,*It);}
    if(LivePonds.Num()!=S.Ponds.Num()){Error=TEXT("Authored pond population changed; original save preserved.");return false;}
    for(const auto& Row:S.Ponds)
    {
        auto* Pond=LivePonds.FindRef(Row.Id);
        if(!Pond||!FDWGatheringPondPersistence::MatchAuthored(*Pond,Row,Error))
        {if(Error.IsEmpty())Error=TEXT("Saved authored pond is missing.");return false;}
        FDWGatheringPreparedPond Prepared;Prepared.Actor=Pond;Prepared.Saved=Row;C.Ponds.Add(MoveTemp(Prepared));
    }
    TMap<FName,ADWGatheringInteractable*> Live;
    for(TActorIterator<ADWGatheringInteractable> It(W);It;++It)if(!It->IsActorBeingDestroyed()&&!It->IsStaged())
    {Live.Add(It->PersistentId,*It);C.Original.Add(*It);}
    TSet<FName> Used;
    for(const auto& Row:S.Actors)
    {
        auto* Actor=Live.FindRef(Row.Id);
        if(Actor)
        {
            if(Actor->bRuntimeSpawned!=Row.bRuntimeSpawned||Actor->GetClass()->GetPathName()!=Row.ClassPath||
                (!Row.bRuntimeSpawned&&PathOf(*Actor)!=Row.AuthoredPath))
            {Error=TEXT("Saved gathering identity differs from the authored/runtime actor.");return false;}
        }
        else if(!Row.bRuntimeSpawned){Error=TEXT("An authored gathering participant is missing from the loaded map.");return false;}
        if(!Actor)
        {
            if(Row.Kind==EDWGatheringSavedKind::Pickup)
                Actor=W->SpawnActorDeferred<ADWGatheringPickup>(ADWGatheringPickup::StaticClass(),Row.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
            else if(Row.Kind==EDWGatheringSavedKind::Station)
            {
                auto* Definition=Config->GetBuildingDefinition(Row.ItemId);
                UClass* Class=Row.bPlaceableBuilding?Definition->ActorClass.LoadSynchronous():ADWGatheringStation::StaticClass();
                if(!Class||Class->HasAnyClassFlags(CLASS_Abstract)||!Class->IsChildOf(ADWGatheringStation::StaticClass()))
                {Error=TEXT("Registered saved building class could not be loaded.");return false;}
                Actor=W->SpawnActorDeferred<ADWGatheringStation>(Class,Row.Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                if(auto* B=Cast<ADWPlaceableBuilding>(Actor))B->Definition=Definition;
            }
            if(!Actor){Error=TEXT("Could not stage a saved gathering object.");return false;}
            C.Staged.Add(Actor);Actor->SetStaged(true);Actor->bRuntimeSpawned=true;Actor->PersistentId=Row.Id;
            if(auto* Q=Cast<ADWGatheringPickup>(Actor)){Q->ItemId=Row.ItemId;Q->Quantity=0;Q->bPopOnSpawn=false;}
            if(auto* T=Cast<ADWGatheringStation>(Actor)){T->ConfigureBuilding(Row.ItemId);T->bStationEnabled=false;}
            Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false);Actor->SetActorTickEnabled(false);
            Actor->FinishSpawning(Row.Transform);
            if(!Current(Actor,W)||!Actor->IsStaged()){Error=TEXT("Saved gathering candidate ended or escaped staging during construction.");return false;}
            if(!Actor->HasActorBegunPlay())Actor->DispatchBeginPlay();
            if(!Current(Actor,W)||!Actor->IsStaged()){Error=TEXT("Saved gathering candidate ended or escaped staging during BeginPlay.");return false;}
            Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false);Actor->SetActorTickEnabled(false);
        }
        FDWGatheringPreparedActor Prepared;Prepared.Actor=Actor;Prepared.Saved=Row;
        if(Row.Kind==EDWGatheringSavedKind::Resource)
        {
            auto* R=Cast<ADWGatheringResource>(Actor);
            if(!R||R->MaxDurability!=Row.MaxDurability||R->RequiredTool!=Row.RequiredTool||R->Drops.Num()!=Row.Drops.Num()||
                !R->CanPrepareGatheringPersistence(Row.bDropsReleased,Error))return false;
            for(int32 Index=0;Index<Row.Drops.Num();++Index)if(Row.Drops[Index].ItemId!=R->Drops[Index].ItemId||Row.Drops[Index].Quantity!=R->Drops[Index].Quantity)
            {Error=TEXT("Saved resource reward definition changed.");return false;}
        }
        else if(Row.Kind==EDWGatheringSavedKind::Pickup)
        {
            if(!Cast<ADWGatheringPickup>(Actor)){Error=TEXT("Saved pickup resolved to another actor kind.");return false;}
            if(!Row.PickupMesh.IsNull())
            {Prepared.PickupMesh.Reset(Cast<UStaticMesh>(Row.PickupMesh.TryLoad()));if(!Prepared.PickupMesh.IsValid()){Error=TEXT("Saved pickup mesh is unavailable.");return false;}}
            for(const auto& M:Row.PickupMaterials)
            {auto* Material=M.IsNull()?nullptr:Cast<UMaterialInterface>(M.TryLoad());if(!M.IsNull()&&!Material){Error=TEXT("Saved pickup material is unavailable.");return false;}Prepared.Materials.Add(TStrongObjectPtr<UMaterialInterface>(Material));}
        }
        else
        {
            auto* T=Cast<ADWGatheringStation>(Actor);auto* B=Cast<ADWPlaceableBuilding>(Actor);
            if(!T||(B!=nullptr)!=Row.bPlaceableBuilding||(B&&(!B->HasCompleteBuildingVisuals()||FSoftObjectPath(B->Definition.Get())!=Row.BuildingDefinition)))
            {Error=TEXT("Saved station/building did not complete the registered construction.");return false;}
            // Only a restored, validated, GUID-identified runtime building can receive a
            // legacy estimate. This does NOT establish what a historical player paid.
            if(Row.bRuntimeSpawned&&(Row.DemolitionCostOrigin==0||Row.DemolitionCostOrigin==2))
            {
                FGuid Parsed;FDWBuildingDemolitionCost Estimate;FString EstimateError;
                if(FGuid::ParseExact(Row.Id.ToString(),EGuidFormats::Digits,Parsed)&&Parsed.IsValid()&&
                   FDWBuildingDemolitionCost::ResolveUnique(*Config,Row.ItemId,Estimate,EstimateError))
                {Prepared.Saved.DemolitionCostOrigin=4;Prepared.Saved.DemolitionCostRecipe=Estimate.RecipeId;
                 Prepared.Saved.DemolitionCostInputs=MoveTemp(Estimate.Inputs);}
            }
        }
        Used.Add(Row.Id);C.Actors.Add(MoveTemp(Prepared));
        if(!Plan.CanCommit(Error))return false;
    }
    for(const auto& Pair:Live)if(!Used.Contains(Pair.Key))
    {
        if(!Pair.Value->bRuntimeSpawned){Error=TEXT("The map has an authored gathering actor absent from this save.");return false;}
        C.Retire.Add(Pair.Value);
    }
    if(!Plan.CanCommit(Error))return false;Out=MoveTemp(Plan);Error.Reset();return true;
}
void FDWGatheringWorldPersistence::AdoptNativeFacts(FDWGatheringRestorePlan& Plan)
{
    check(Plan.State&&!Plan.State->bAdopted);auto& C=*Plan.State;
    if(C.bNoOp){C.bAdopted=true;return;}
    check(C.Authority.IsValid()&&UDWCombatAuthoritySubsystem::Get(C.Player.Get())==nullptr);
    C.NewEpoch=C.Authority->GetWorldEpoch();C.NewLife=C.Authority->FindRegisteredActorLife(C.Player.Get());
    for(auto& Row:C.Actors)
    {
        auto* A=Row.Actor.Get();check(A);A->PersistentId=Row.Saved.Id;A->bRuntimeSpawned=Row.Saved.bRuntimeSpawned;
        if(auto* R=Cast<ADWGatheringResource>(A))R->AdoptResourceState(Row.Saved.Durability,Row.Saved.bDropsReleased,-1.f);
        else if(auto* Q=Cast<ADWGatheringPickup>(A))Q->AdoptGatheringPersistenceFacts(Row.Saved.ItemId,Row.Saved.Quantity);
        else if(auto* T=Cast<ADWGatheringStation>(A))
        {T->BuildingItemId=Row.Saved.ItemId;T->StationId=Row.Saved.StationId;T->bStationEnabled=Row.Saved.bEnabled;
         T->DemolitionCostOrigin=Row.Saved.DemolitionCostOrigin;T->DemolitionCostRecipe=Row.Saved.DemolitionCostRecipe;
         T->DemolitionCostInputs=Row.Saved.DemolitionCostInputs;T->bDemolished=Row.Saved.bDemolished;}
        A->SetStaged(false);
    }
    for(auto& Row:C.Ponds){auto* Pond=Row.Actor.Get();check(Pond);Pond->AdoptPondPersistenceFacts(Row.Saved.RemainingWater);}
    for(auto A:C.Retire)if(A.IsValid())A->SetStaged(true);
    C.Gathering->AdoptGatheringPersistenceFacts(C.Saved.ActiveTool,C.Saved.ToolCooldownRemaining);
    C.bAdopted=true;
}
bool FDWGatheringWorldPersistence::Publish(FDWGatheringRestorePlan& Plan,FString& Error)
{
    if(!Plan.State||!Plan.State->bAdopted||Plan.State->bPublished){Error=TEXT("Gathering publication unavailable or already completed.");return false;}
    auto& C=*Plan.State;if(C.bNoOp){C.bPublished=true;Error.Reset();return true;}
    const auto StillCurrent=[&]()
    {
        if(Current(C.Player.Get(),C.World.Get())&&C.Authority.IsValid()&&C.Gathering.IsValid()&&
            UDWCombatAuthoritySubsystem::Get(C.Player.Get())==C.Authority.Get()&&C.Authority->GetWorldEpoch()==C.NewEpoch&&
            SameLife(C.Authority->FindRegisteredActorLife(C.Player.Get()),C.NewLife))return true;
        Error=TEXT("Gathering publication interrupted after aggregate adoption; automatic replay is forbidden.");return false;
    };
    if(!StillCurrent())return false;
    // Retired actors were staged inside the fact barrier. Suppress their cosmetic
    // beams immediately after the barrier, before other presentation callbacks.
    if(auto* V=C.World->GetSubsystem<UDWLootBeaconSubsystem>())
        for(auto A:C.Retire){if(A.IsValid())V->SuspendBeacon(A.Get());if(!StillCurrent())return false;}
    for(auto& Row:C.Actors)
    {
        auto* A=Row.Actor.Get();if(!Current(A,C.World.Get()))return false;
        // Reset old cosmetic caches before applying the saved transform, never before adoption.
        if(auto* R=Cast<ADWGatheringResource>(A))R->ResetGatheringPersistencePresentation();
        else if(auto* Q=Cast<ADWGatheringPickup>(A))Q->ResetGatheringPersistencePresentation();
        if(!StillCurrent()||!Current(A,C.World.Get()))return false;
        A->SetActorTransform(Row.Saved.Transform);if(!StillCurrent()||!Current(A,C.World.Get()))return false;
        A->Mesh->SetRelativeTransform(Row.Saved.MeshTransform);if(!StillCurrent()||!Current(A,C.World.Get()))return false;
        if(auto* R=Cast<ADWGatheringResource>(A))
        {if(!R->PublishGatheringPersistencePresentation(Error))return false;}
        else if(auto* Q=Cast<ADWGatheringPickup>(A))
        {
            if(Row.PickupMesh.IsValid())Q->Mesh->SetStaticMesh(Row.PickupMesh.Get());
            if(!StillCurrent()||!Current(Q,C.World.Get()))return false;
            for(int32 I=0;I<Row.Materials.Num();++I)if(Row.Materials[I].IsValid())
            {Q->Mesh->SetMaterial(I,Row.Materials[I].Get());if(!StillCurrent()||!Current(Q,C.World.Get()))return false;}
            Q->MeshRestLocation=Q->Mesh->GetRelativeLocation();
            Q->RefreshPickup();
        }
        else A->PublishInteractionState();
        if(!StillCurrent())return false;
    }
    for(auto& Row:C.Ponds)
    {auto* Pond=Row.Actor.Get();if(!Current(Pond,C.World.Get())||!StillCurrent())return false;
     Pond->PublishPondPersistenceFacts();if(!StillCurrent()||!Current(Pond,C.World.Get()))return false;}
    for(auto A:C.Retire)
    {if(A.IsValid()&&!A->IsActorBeingDestroyed())A->Destroy();if(!StillCurrent())return false;}
    C.Gathering->PublishGatheringPersistencePresentation();if(!StillCurrent())return false;
    C.Staged.Reset();C.bPublished=true;Error.Reset();return true;
}
