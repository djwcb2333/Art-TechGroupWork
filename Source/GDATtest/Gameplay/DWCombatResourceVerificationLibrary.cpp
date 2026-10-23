#include "DWCombatResourceVerificationLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatWorldPersistence.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWCombatEffectsSave.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWSaveGame.h"
#include "DWSaveStorage.h"
#include "DWWorldEvent.h"
#include "DWLoadingTransition.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#endif

namespace DWCombatResourceVerification
{
FString Encode(const TSharedRef<FJsonObject>& Object)
{FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Object,Writer);return Text;}
FString Reject(const FString& Reason)
{auto Object=MakeShared<FJsonObject>();Object->SetBoolField(TEXT("ok"),false);Object->SetStringField(TEXT("error"),Reason);return Encode(Object);}
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
const TCHAR* AllowedDirectory=TEXT("E:/GDAT/第一学期/AP6402 Art-Tech Collaboration/Codex Mission/UE_Whitebox_Delivery/CombatSystem_20261013/Execution_20261021/IsolatedSaves");
struct FContext
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWGameInstance> GI;
    TWeakObjectPtr<UWorld> World;
    FString Run,Prefix,Directory;
    int32 Slot=INDEX_NONE;
};
bool Exact(const FString& A,const FString& B){return A.Equals(B,ESearchCase::CaseSensitive);}
bool NormalizeAbsolute(FString& Path)
{
    if(Path.IsEmpty()||FPaths::IsRelative(Path))return false;
    FPaths::NormalizeDirectoryName(Path);
    return FPaths::CollapseRelativeDirectories(Path);
}
bool Gate(ADWPlayerCharacter* Player,FContext& Out,FString& Error)
{
    auto Fail=[&](const TCHAR* Reason){Error=Reason;return false;};
    if(!IsInGameThread()||!GIsEditor||!GEditor)return Fail(TEXT("Resource QA requires the editor game thread."));
    if(!IsValid(Player)||Player->IsActorBeingDestroyed()||!Player->HasActorBegunPlay())return Fail(TEXT("A live begun-play player is required."));
    auto* World=Player->GetWorld();
    if(!World||World->WorldType!=EWorldType::PIE||World->bIsTearingDown||GEditor->PlayWorld!=World||World->GetNetMode()!=NM_Standalone)
        return Fail(TEXT("Only the active standalone PIE world is allowed; editor preview and packaged/game worlds are rejected."));
    auto* GI=Player->GetGameInstance<UDWGameInstance>();
    if(!GI||GI->GetWorld()!=World||UGameplayStatics::GetPlayerPawn(World,0)!=Player||!GI->HasActiveSlot())
        return Fail(TEXT("Player zero, matching game instance, and an active isolated slot are required."));
    FString Run,Prefix,Directory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("DWResourceQARun="),Run)||Run.Len()!=2||Run[0]<'0'||Run[0]>'9'||Run[1]<'0'||Run[1]>'9')
        return Fail(TEXT("DWResourceQARun must contain exactly two ASCII digits, e.g. 02."));
    const FString ExpectedPrefix=TEXT("DW_QA_D03_FileRoundTrip_")+Run+TEXT("_");
    if(!FParse::Value(FCommandLine::Get(),TEXT("DWVerificationSavePrefix="),Prefix)||!Exact(Prefix,ExpectedPrefix)||
        !Exact(GI->GetResourceVerificationSavePrefix(),ExpectedPrefix))
        return Fail(TEXT("Both command line and the initialized game's actual QA prefix must match the requested run."));
    FString Actual=GI->GetSaveDirectory(),Allowed=AllowedDirectory;
    if(!FParse::Value(FCommandLine::Get(),TEXT("DWSaveTestDirectory="),Directory)||!NormalizeAbsolute(Directory)||
        !NormalizeAbsolute(Actual)||!NormalizeAbsolute(Allowed)||!Exact(Directory,Allowed)||!Exact(Actual,Allowed)||
        !IFileManager::Get().DirectoryExists(*Allowed))
        return Fail(TEXT("Both actual storage and CLI directory must equal the fixed existing Execution_20261021/IsolatedSaves directory."));
    if(!GI->GetCombatRules()||!Player->HasCombatVitalsReady()||Player->IsCombatRestoreInProgress()||GI->IsPlayerRestorePending()||
        !IsValid(Player->Inventory)||Player->Inventory->IsBeingDestroyed()||!IsValid(Player->WeaponAnimationComponent)||Player->WeaponAnimationComponent->IsBeingDestroyed())
        return Fail(TEXT("Player resources, inventory, rules and weapon component must be ready with no pending restore."));
    if(UDWWorldEventSubsystem::IsPlaying(Player))return Fail(TEXT("A world event is running."));
    if(const auto* Loading=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();Loading&&Loading->IsTransitionActive())
        return Fail(TEXT("A loading transition is running."));
    Out.Player=Player;Out.GI=GI;Out.World=World;Out.Run=Run;Out.Prefix=ExpectedPrefix;Out.Directory=Actual;Out.Slot=GI->GetActiveSlot();
    Error.Reset();return true;
}
bool Recheck(const FContext& Before,FString& Error)
{
    FContext After;
    if(!Gate(Before.Player.Get(),After,Error))return false;
    if(Before.GI!=After.GI||Before.World!=After.World||Before.Slot!=After.Slot||
        !Exact(Before.Run,After.Run)||!Exact(Before.Prefix,After.Prefix)||!Exact(Before.Directory,After.Directory))
    {Error=TEXT("The PIE player, storage context or active isolated slot changed during verification.");return false;}
    return true;
}
FString Unsigned(uint64 Value){return FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(Value));}
TSharedRef<FJsonObject> SnapshotJson(const FContext& C,const UDWSaveGame& Save,const TCHAR* Operation)
{
    auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("ok"),true);Result->SetStringField(TEXT("operation"),Operation);
    Result->SetStringField(TEXT("run"),C.Run);Result->SetStringField(TEXT("prefix"),C.Prefix);Result->SetStringField(TEXT("directory"),C.Directory);
    Result->SetBoolField(TEXT("world_paused"),C.World->IsPaused());Result->SetNumberField(TEXT("world_time_seconds"),C.World->GetTimeSeconds());
    Result->SetNumberField(TEXT("active_slot"),C.Slot);Result->SetStringField(TEXT("map"),Save.MapPackage);
    Result->SetNumberField(TEXT("save_version"),Save.Version);Result->SetNumberField(TEXT("resource_subblock_schema"),Save.CombatPlayerResources.SubblockSchema);
    FString Error;const bool ResourcesValid=DWCombatPlayerSave::Validate(*C.GI->GetCombatRules(),Save,Error);
    const bool InventoryValid=C.Player->Inventory->AreSlotsValid(Save.Inventory);
    Result->SetBoolField(TEXT("resource_snapshot_valid"),ResourcesValid);Result->SetStringField(TEXT("resource_validation_error"),Error);
    Result->SetBoolField(TEXT("inventory_valid"),InventoryValid);Result->SetBoolField(TEXT("ok"),ResourcesValid&&InventoryValid);
    Result->SetBoolField(TEXT("is_allowed_gameplay_map"),C.GI->IsAllowedGameplayMap(Save.MapPackage));
    Result->SetBoolField(TEXT("full_d22_verified"),false);
    if(!ResourcesValid||!InventoryValid)return Result; // Never serialize non-finite corrupt values into JSON.
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);
    const auto& Block=Save.CombatPlayerResources;int64 Reserve=0;
    for(const auto& Stack:Save.Inventory)if(Binding&&Stack.ItemId==Binding->AmmoItemId)Reserve+=Stack.Quantity;
    Result->SetStringField(TEXT("shotgun_item_id"),Binding?Binding->ItemId.ToString():TEXT("missing"));
    Result->SetStringField(TEXT("reserve_item_id"),Binding?Binding->AmmoItemId.ToString():TEXT("missing"));
    Result->SetBoolField(TEXT("owns_shotgun"),Binding&&Block.OwnedPermanentWeaponIds.Contains(Binding->ItemId));
    Result->SetNumberField(TEXT("loaded_shells"),Block.LoadedShotgunShells);Result->SetNumberField(TEXT("reserve_shells"),static_cast<double>(Reserve));
    TArray<TSharedPtr<FJsonValue>> Owned;for(FName Id:Block.OwnedPermanentWeaponIds)Owned.Add(MakeShared<FJsonValueString>(Id.ToString()));
    Result->SetArrayField(TEXT("owned_permanent_ids"),Owned);
    Result->SetStringField(TEXT("current_equipment_id"),Block.CurrentEquipmentId.ToString());
    const auto& V=Block.Vitals;auto Vitals=MakeShared<FJsonObject>();
    Vitals->SetNumberField(TEXT("resource_schema"),V.ResourceSchema);Vitals->SetNumberField(TEXT("rules_schema"),V.RulesCombatSchema);
    Vitals->SetNumberField(TEXT("health"),V.Health);Vitals->SetNumberField(TEXT("water"),V.Water-V.WaterDrainCarry);
    Vitals->SetNumberField(TEXT("satiety"),V.Satiety-V.SatietyDrainCarry);Vitals->SetNumberField(TEXT("special"),V.Special-V.SpecialGrowthCarry);
    const TCHAR* Form=V.BaseForm==EDWCombatBaseForm::Normal?TEXT("Normal"):V.BaseForm==EDWCombatBaseForm::Mold?TEXT("Mold"):V.BaseForm==EDWCombatBaseForm::Yeast?TEXT("Yeast"):TEXT("Invalid");
    Vitals->SetStringField(TEXT("base_form"),Form);Vitals->SetNumberField(TEXT("form_remaining_seconds"),V.FormRemainingSeconds-V.FormTimeCarry);
    Vitals->SetNumberField(TEXT("water_empty_elapsed_seconds"),V.WaterEmptyElapsedSeconds-V.WaterEmptyTimeCarry);
    Vitals->SetNumberField(TEXT("satiety_empty_elapsed_seconds"),V.SatietyEmptyElapsedSeconds-V.SatietyEmptyTimeCarry);
    Vitals->SetStringField(TEXT("dehydration_ordinal"),Unsigned(V.DehydrationOrdinal));Vitals->SetStringField(TEXT("starvation_ordinal"),Unsigned(V.StarvationOrdinal));
    Result->SetObjectField(TEXT("vitals"),Vitals);
    return Result;
}
TStrongObjectPtr<UDWSaveGame> Capture(const FContext& C)
{
    TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>(GetTransientPackage(),NAME_None,RF_Transient));
    C.Player->CaptureSaveData(Save.Get());Save->MapPackage=UWorld::RemovePIEPrefix(C.World->GetOutermost()->GetName());return Save;
}
FString ApplyFixture(ADWPlayerCharacter* Player,int32 Loaded,int32 Reserve,const TCHAR* Operation)
{
    FContext C;FString Error;if(!Gate(Player,C,Error))return Reject(Error);
    if(Player->IsDead())return Reject(TEXT("Fixture injection cannot revive a dead player."));
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);
    if(!Binding||Binding->AmmoItemId.IsNone())return Reject(TEXT("Explicit shotgun registration is missing."));
    auto Candidate=Capture(C);if(!Recheck(C,Error))return Reject(TEXT("Player lifecycle or isolation changed during capture."));
    Candidate->CombatPlayerResources.OwnedPermanentWeaponIds={Binding->ItemId};Candidate->CombatPlayerResources.CurrentEquipmentId=Binding->ItemId;Candidate->CombatPlayerResources.LoadedShotgunShells=Loaded;
    Candidate->Inventory.RemoveAll([&](const FDWItemStack& Stack){return Stack.ItemId==Binding->AmmoItemId;});
    const int32 Limit=Player->Inventory->GetEffectiveStackLimit(Binding->AmmoItemId);
    if(Limit<=0)return Reject(TEXT("Registered reserve ammo has no valid stack policy."));
    for(int32 Left=Reserve;Left>0;)
    {const int32 Add=FMath::Min(Left,Limit);Candidate->Inventory.Emplace(Binding->AmmoItemId,Add);Left-=Add;}
    if(!DWCombatPlayerSave::Validate(*C.GI->GetCombatRules(),*Candidate,Error))return Reject(Error);
    if(!Player->Inventory->AreSlotsValid(Candidate->Inventory))return Reject(TEXT("The fixed fixture does not fit the actual inventory policy; no changes were made."));
    if(!Recheck(C,Error))return Reject(Error);
    // This is the real atomic player save restore path, including its life rotation and notifications.
    if(!Player->TryApplySaveData(Candidate.Get()))return Reject(TEXT("Real TryApplySaveData rejected or was interrupted; inspect current state before retry."));
    if(!Recheck(C,Error))return Reject(TEXT("Isolation or lifecycle changed after the real restore; no further action taken."));
    auto Observed=Capture(C);if(!Recheck(C,Error))return Reject(TEXT("Player changed while reading restored facts."));
    auto Result=SnapshotJson(C,*Observed,Operation);
    const bool Matches=Observed->CombatPlayerResources.OwnedPermanentWeaponIds.Num()==1&&Observed->CombatPlayerResources.OwnedPermanentWeaponIds.Contains(Binding->ItemId)&&
        Observed->CombatPlayerResources.LoadedShotgunShells==Loaded&&Player->Inventory->CountItem(Binding->AmmoItemId)==Reserve;
    Result->SetBoolField(TEXT("fixture_matches"),Matches);Result->SetBoolField(TEXT("ok"),Matches&&Result->GetBoolField(TEXT("resource_snapshot_valid"))&&Result->GetBoolField(TEXT("inventory_valid")));
    Result->SetBoolField(TEXT("file_written_by_helper"),false);return Encode(Result);
}
#endif
}

FString UDWCombatResourceVerificationLibrary::ApplyInitialShotgunFixture(ADWPlayerCharacter* Player)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    return DWCombatResourceVerification::ApplyFixture(Player,2,7,TEXT("initial_fixture"));
#else
    return DWCombatResourceVerification::Reject(TEXT("Editor automation PIE resource verification only."));
#endif
}
FString UDWCombatResourceVerificationLibrary::ApplyMutatedShotgunFixture(ADWPlayerCharacter* Player)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    return DWCombatResourceVerification::ApplyFixture(Player,1,16,TEXT("mutated_fixture"));
#else
    return DWCombatResourceVerification::Reject(TEXT("Editor automation PIE resource verification only."));
#endif
}
FString UDWCombatResourceVerificationLibrary::ApplySpearVerificationFixture(ADWPlayerCharacter* Player)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    using namespace DWCombatResourceVerification;FContext C;FString Error;
    if(!Gate(Player,C,Error))return Reject(Error);
    if(Player->IsDead())return Reject(TEXT("Spear verification cannot revive a dead player."));
    const auto* Weapon=Player->WeaponAnimationComponent.Get();
    if(!Weapon->bEnableFormalSpearExplicitBinding||!Weapon->IsFormalSpearConfigured())
        return Reject(TEXT("The approved explicit spear source must already be configured; the fixture cannot bypass HC04."));
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Spear);
    if(!Binding||FDWCombatItemRegistry::GetItemRole(Binding->ItemId)!=EDWCombatItemRole::PermanentWeapon)
        return Reject(TEXT("Explicit permanent spear registration is missing."));
    auto Candidate=Capture(C);if(!Recheck(C,Error))return Reject(Error);
    Candidate->CombatPlayerResources.OwnedPermanentWeaponIds={Binding->ItemId};Candidate->CombatPlayerResources.CurrentEquipmentId=Binding->ItemId;
    Candidate->CombatPlayerResources.LoadedShotgunShells=0;
    // Preserve captured inventory, vitals and world snapshots. Do not grant or delete reserve ammo.
    if(!DWCombatPlayerSave::Validate(*C.GI->GetCombatRules(),*Candidate,Error))return Reject(Error);
    if(!Player->Inventory->AreSlotsValid(Candidate->Inventory))return Reject(TEXT("Captured inventory is invalid; no changes were made."));
    if(!Recheck(C,Error))return Reject(Error);
    if(!Player->TryApplySaveData(Candidate.Get()))return Reject(TEXT("Real spear fixture restore rejected or was interrupted; inspect before retry."));
    if(!Recheck(C,Error))return Reject(TEXT("Isolation or lifecycle changed after restore; no further action taken."));
    auto Observed=Capture(C);if(!Recheck(C,Error))return Reject(Error);
    auto Result=SnapshotJson(C,*Observed,TEXT("spear_fixture"));
    const bool Matches=Observed->CombatPlayerResources.OwnedPermanentWeaponIds.Num()==1&&
        Observed->CombatPlayerResources.OwnedPermanentWeaponIds.Contains(Binding->ItemId)&&Observed->CombatPlayerResources.LoadedShotgunShells==0;
    Result->SetStringField(TEXT("spear_item_id"),Binding->ItemId.ToString());
    Result->SetBoolField(TEXT("fixture_matches"),Matches);
    Result->SetBoolField(TEXT("formal_spear_configured"),Player->WeaponAnimationComponent->IsFormalSpearConfigured());
    Result->SetBoolField(TEXT("ok"),Matches&&Result->GetBoolField(TEXT("resource_snapshot_valid"))&&Result->GetBoolField(TEXT("inventory_valid")));
    Result->SetBoolField(TEXT("file_written_by_helper"),false);return Encode(Result);
#else
    return DWCombatResourceVerification::Reject(TEXT("Editor automation PIE resource verification only."));
#endif
}

FString UDWCombatResourceVerificationLibrary::ApplyWeaponMechanicsFixture(ADWPlayerCharacter* Player)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    using namespace DWCombatResourceVerification;FContext C;FString Error;
    if(!Gate(Player,C,Error))return Reject(Error);
    auto* Weapon=Player->WeaponAnimationComponent.Get();
    if(Player->IsDead()||Player->IsCoreInjecting()||Player->IsFormalDashing()||Player->IsFormallyRooted()||Weapon->IsBusy())
        return Reject(TEXT("Begin the isolated weapon fixture only from a living idle, unrooted player."));
    auto Candidate=Capture(C);if(!Recheck(C,Error))return Reject(Error);
    for(const auto Kind:{EDWAnimatedWeapon::Spear,EDWAnimatedWeapon::Greatsword,EDWAnimatedWeapon::ShieldAxe,
        EDWAnimatedWeapon::DualDaggers,EDWAnimatedWeapon::SporeGun,EDWAnimatedWeapon::Shotgun,EDWAnimatedWeapon::MoldBow})
    {
        const auto* Binding=FDWCombatItemRegistry::FindPresentation(Kind);
        if(!Binding||FDWCombatItemRegistry::GetItemRole(Binding->ItemId)!=EDWCombatItemRole::PermanentWeapon)
            return Reject(TEXT("A registered permanent weapon is missing."));
        Candidate->CombatPlayerResources.OwnedPermanentWeaponIds.AddUnique(Binding->ItemId);
    }
    Candidate->CombatPlayerResources.CurrentEquipmentId=NAME_None;
    Candidate->CombatPlayerResources.LoadedShotgunShells=0;
    // Fixed QA stock is not a gameplay starting loadout, recipe or acquisition rule.
    const TPair<FName,int32> Stock[]={ {TEXT("DWAmmo_Spore"),100},{TEXT("DWAmmo_MoldArrow"),100},
        {TEXT("DWAmmo_BreadShell"),100},{TEXT("Alcohol"),100},
        {FDWCombatItemRegistry::MoldCoreId(),2},{FDWCombatItemRegistry::YeastCoreId(),2}};
    for(const auto& Item:Stock)
    {
        Candidate->Inventory.RemoveAll([&](const FDWItemStack& Stack){return Stack.ItemId==Item.Key;});
        const int32 Limit=Player->Inventory->GetEffectiveStackLimit(Item.Key);
        if(Limit<=0)return Reject(TEXT("A registered QA item has no valid stack policy."));
        for(int32 Left=Item.Value;Left>0;){const int32 Add=FMath::Min(Left,Limit);Candidate->Inventory.Emplace(Item.Key,Add);Left-=Add;}
    }
    if(!DWCombatPlayerSave::Validate(*C.GI->GetCombatRules(),*Candidate,Error))return Reject(Error);
    if(!Player->Inventory->AreSlotsValid(Candidate->Inventory))return Reject(TEXT("The fixed QA supplies do not fit; player was not changed."));
    if(!Recheck(C,Error))return Reject(Error);
    if(!Player->TryApplySaveData(Candidate.Get()))return Reject(TEXT("Real atomic restore rejected or was interrupted; inspect before retry."));
    if(!Recheck(C,Error))return Reject(Error);
    auto Observed=Capture(C);if(!Recheck(C,Error))return Reject(Error);
    auto Result=SnapshotJson(C,*Observed,TEXT("all_weapon_mechanics_fixture"));
    bool Matches=Observed->CombatPlayerResources.CurrentEquipmentId.IsNone()&&Observed->CombatPlayerResources.LoadedShotgunShells==0;
    for(FName Id:Candidate->CombatPlayerResources.OwnedPermanentWeaponIds)Matches&=Observed->CombatPlayerResources.OwnedPermanentWeaponIds.Contains(Id);
    for(const auto& Item:Stock)Matches&=Player->Inventory->CountItem(Item.Key)==Item.Value;
    Result->SetBoolField(TEXT("fixture_matches"),Matches);Result->SetBoolField(TEXT("ok"),Matches&&Result->GetBoolField(TEXT("ok")));
    Result->SetBoolField(TEXT("file_written_by_helper"),false);return Encode(Result);
#else
    return DWCombatResourceVerification::Reject(TEXT("Editor automation PIE resource verification only."));
#endif
}

FString UDWCombatResourceVerificationLibrary::InspectPlayerResources(ADWPlayerCharacter* Player)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    using namespace DWCombatResourceVerification;FContext C;FString Error;if(!Gate(Player,C,Error))return Reject(Error);
    auto Save=Capture(C);if(!Recheck(C,Error))return Reject(Error);return Encode(SnapshotJson(C,*Save,TEXT("inspect_player")));
#else
    return DWCombatResourceVerification::Reject(TEXT("Editor automation PIE resource verification only."));
#endif
}
FString UDWCombatResourceVerificationLibrary::InspectActiveSlotResources(ADWPlayerCharacter* Player)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    using namespace DWCombatResourceVerification;FContext C;FString Error;if(!Gate(Player,C,Error))return Reject(Error);
    const FString Slot=C.Prefix+FString::FromInt(C.Slot+1);
    TStrongObjectPtr<UDWSaveGame> Save(Cast<UDWSaveGame>(DWSaveStorage::Read(C.Directory,Slot)));
    if(!Save.Get())return Reject(TEXT("The active isolated save file is missing or is not a readable UDWSaveGame."));
    if(!Recheck(C,Error))return Reject(Error);
    auto Result=SnapshotJson(C,*Save,TEXT("inspect_active_slot"));
    Result->SetStringField(TEXT("file"),FPaths::Combine(C.Directory,Slot+TEXT(".sav")));
    Result->SetBoolField(TEXT("file_written_by_helper"),false);return Encode(Result);
#else
    return DWCombatResourceVerification::Reject(TEXT("Editor automation PIE resource verification only."));
#endif
}

bool UDWCombatResourceVerificationLibrary::ApplyCombatTestAmmo(ADWPlayerCharacter* P)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    if(!IsValid(P)||P->IsActorBeingDestroyed()||!P->GiveCombatTestAmmo||P->CombatTestAmmoPerType<=0||
        P->CombatTestAmmoPerType>1000||!P->HasCombatVitalsReady()||P->IsDead()||P->IsCombatRestoreInProgress())return false;
    auto* W=P->GetWorld();auto* GI=P->GetGameInstance<UDWGameInstance>();auto* I=P->GetInventory();
    if(!W||W->bIsTearingDown||W->WorldType!=EWorldType::PIE||W->GetNetMode()!=NM_Standalone||!GI||GI->HasActiveSlot()||
        UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())!=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_CombatTest")||
        !I||!I->CanWriteInventory())return false;
    // One prepared inventory transaction preserves existing stock and unrelated items. No load/restore or disk I/O.
    TArray<FDWItemStack> Slots=I->GetSlots();
    for(FName Id:{FName(TEXT("DWAmmo_Spore")),FName(TEXT("DWAmmo_MoldArrow")),FName(TEXT("DWAmmo_BreadShell")),FName(TEXT("Alcohol"))})
    {
        const int32 Limit=I->GetEffectiveStackLimit(Id);if(Limit<=0)return false;
        int32 Left=FMath::Max(0,P->CombatTestAmmoPerType-I->CountItem(Id));
        for(auto& Slot:Slots)if(Slot.ItemId==Id&&Left>0)
        {const int32 Added=FMath::Min(Left,FMath::Max(0,Limit-Slot.Quantity));Slot.Quantity+=Added;Left-=Added;}
        while(Left>0){const int32 Added=FMath::Min(Left,Limit);Slots.Emplace(Id,Added);Left-=Added;}
    }
    FDWPreparedInventoryChange Prepared;
    if(!I->MakePrepared(MoveTemp(Slots),Prepared)||!I->CommitPrepared(MoveTemp(Prepared)))
    {UE_LOG(LogTemp,Warning,TEXT("CombatTest ammo did not fit; no inventory was changed."));return false;}
    UE_LOG(LogTemp,Display,TEXT("CombatTest user-requested stock: spore=%d arrow=%d shell=%d alcohol=%d; no save written."),
        I->CountItem(TEXT("DWAmmo_Spore")),I->CountItem(TEXT("DWAmmo_MoldArrow")),I->CountItem(TEXT("DWAmmo_BreadShell")),I->CountItem(TEXT("Alcohol")));
    return true;
#else
    return false;
#endif
}

FString UDWCombatResourceVerificationLibrary::InspectCompleteCombatSnapshot(ADWPlayerCharacter* Player,bool bReadActiveSlot)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    using namespace DWCombatResourceVerification;
    FContext C;FString Error;if(!Gate(Player,C,Error))return Reject(Error);
    const FString Slot=C.Prefix+FString::FromInt(C.Slot+1);
    TStrongObjectPtr<UDWSaveGame> Save(bReadActiveSlot
        ?Cast<UDWSaveGame>(DWSaveStorage::Read(C.Directory,Slot))
        :NewObject<UDWSaveGame>(GetTransientPackage(),NAME_None,RF_Transient));
    if(!Save.Get())return Reject(TEXT("The isolated active slot is missing or unreadable; no other path was tried."));
    if(!bReadActiveSlot&&!FDWCombatWorldPersistence::Capture(*Player,*Save,Error))return Reject(Error);
    if(!Recheck(C,Error)||!FDWCombatWorldPersistence::Validate(*C.GI->GetCombatRules(),*Save,Error))return Reject(Error);
    FDWCombatWorldObjectsSave Objects;FDWCombatEffectsSave Effects;
    const auto& Complete=Save->CombatWorld;const auto& Extended=Complete.Player;
    if(!Complete.bNewAdventure&&(!DWCombatWorldObjectsSave::Decode(Complete.Objects,Objects,Error)||
        !DWCombatEffectsSave::Decode(Complete.Effects,Effects,Error)))return Reject(Error);
    // Validation precedes numeric JSON serialization. This is observation, never a restore or file write.
    auto Result=SnapshotJson(C,*Save,bReadActiveSlot?TEXT("inspect_complete_active_slot"):TEXT("inspect_complete_live"));
    if(!Result->GetBoolField(TEXT("ok")))return Encode(Result);
    Result->RemoveField(TEXT("full_d22_verified"));
    Result->SetBoolField(TEXT("complete_snapshot_valid"),true);
    Result->SetStringField(TEXT("validation_scope"),TEXT("Schema and cross-reference validation only; not a completed disk round-trip test."));
    Result->SetStringField(TEXT("snapshot_source"),bReadActiveSlot?TEXT("isolated_active_slot"):TEXT("live_native_capture"));
    Result->SetBoolField(TEXT("file_written_by_helper"),false);
    if(bReadActiveSlot)Result->SetStringField(TEXT("file"),FPaths::Combine(C.Directory,Slot+TEXT(".sav")));
    Result->SetNumberField(TEXT("full_world_schema"),Complete.Schema);
    Result->SetBoolField(TEXT("new_adventure"),Complete.bNewAdventure);
    Result->SetBoolField(TEXT("has_captured_runtime_state"),!Complete.bNewAdventure);
    Result->SetNumberField(TEXT("world_bytes"),Complete.Objects.Num());Result->SetNumberField(TEXT("effects_bytes"),Complete.Effects.Num());
    FDWCombatPlayerVitalsConfig VitalsConfig;
    if(!FDWCombatPlayerVitalsConfig::Build(*C.GI->GetCombatRules(),VitalsConfig,Error))return Reject(Error);
    Result->SetBoolField(TEXT("player_dead"),Save->Health<=DWCombatPlayerSave::HealthFloor(VitalsConfig));
    Result->SetStringField(TEXT("saved_current_weapon"),Save->CombatPlayerResources.CurrentEquipmentId.ToString());
    int64 AlcoholCount=0;for(const auto& Stack:Save->Inventory)if(Stack.ItemId==TEXT("Alcohol"))AlcoholCount+=Stack.Quantity;
    Result->SetNumberField(TEXT("alcohol_count"),static_cast<double>(AlcoholCount));
    Result->SetStringField(TEXT("player_entity"),Unsigned(Extended.PlayerEntity));Result->SetStringField(TEXT("player_life"),Unsigned(Extended.PlayerLife));
    Result->SetStringField(TEXT("snapshot_epoch"),Unsigned(Effects.Authority.Ledger.Epoch));
    const auto* Authority=UDWCombatAuthoritySubsystem::Get(Player);
    Result->SetStringField(TEXT("live_epoch"),Unsigned(Authority?Authority->GetWorldEpoch():0));
    TArray<TSharedPtr<FJsonValue>> Events;
    for(FName Id:Save->CompletedWorldEvents)Events.Add(MakeShared<FJsonValueString>(Id.ToString()));
    Result->SetArrayField(TEXT("completed_event_ids"),Events);
    Result->SetNumberField(TEXT("visited_map_count"),Save->VisitedMaps.Num());

    auto World=MakeShared<FJsonObject>();int32 Living=0,Dead=0,Ended=0,AvailableCores=0;
    TArray<TSharedPtr<FJsonValue>> Enemies;
    for(const auto& Enemy:Objects.Enemies)
    {
        Living+=Enemy.Health>0.f;Dead+=Enemy.Health<=0.f;Ended+=Enemy.bEnded;
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("id"),Enemy.StableId);
        Item->SetNumberField(TEXT("health"),Enemy.Health);Item->SetBoolField(TEXT("ended"),Enemy.bEnded);
        Item->SetNumberField(TEXT("state"),Enemy.State);Item->SetNumberField(TEXT("phase"),Enemy.Phase);
        Item->SetNumberField(TEXT("phase_remaining_seconds"),Enemy.PhaseRemaining);
        Item->SetNumberField(TEXT("attack_cooldown_seconds"),Enemy.AttackCooldown);
        Item->SetStringField(TEXT("entity"),Unsigned(Enemy.Life.Entity));Item->SetStringField(TEXT("life"),Unsigned(Enemy.Life.Life));
        Enemies.Add(MakeShared<FJsonValueObject>(Item));
    }
    for(const auto& Core:Objects.CoreDrops)AvailableCores+=Core.Decision==EDWEnemyCoreDropDecision::SpawnPending||Core.Decision==EDWEnemyCoreDropDecision::Spawned;
    World->SetNumberField(TEXT("schema"),Complete.bNewAdventure?0:Objects.Schema);
    World->SetStringField(TEXT("map"),Objects.MapPackage);World->SetNumberField(TEXT("enemy_count"),Objects.Enemies.Num());
    World->SetNumberField(TEXT("living_enemy_count"),Living);World->SetNumberField(TEXT("dead_enemy_count"),Dead);
    World->SetNumberField(TEXT("ended_enemy_count"),Ended);World->SetNumberField(TEXT("nest_count"),Objects.NestClocks.Num());
    World->SetNumberField(TEXT("legacy_actor_count"),Save->WorldActors.Num());World->SetNumberField(TEXT("core_record_count"),Objects.CoreDrops.Num());
    World->SetNumberField(TEXT("available_core_record_count"),AvailableCores);World->SetArrayField(TEXT("enemies"),Enemies);
    const auto& Recovery=Objects.Recovery;int32 UnclaimedBags=0;
    TArray<TSharedPtr<FJsonValue>> Bags;
    for(const auto& Bag:Recovery.Bags)
    {
        UnclaimedBags+=!Bag.bClaimed;int64 Remaining=0;for(const auto& Stack:Bag.Contents)Remaining+=Stack.Remaining;
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("id"),Bag.BagId.ToString());
        Item->SetBoolField(TEXT("claimed"),Bag.bClaimed);Item->SetNumberField(TEXT("remaining_items"),static_cast<double>(Remaining));
        Bags.Add(MakeShared<FJsonValueObject>(Item));
    }
    World->SetNumberField(TEXT("death_bag_count"),Recovery.Bags.Num());World->SetNumberField(TEXT("unclaimed_death_bag_count"),UnclaimedBags);
    World->SetArrayField(TEXT("death_bags"),Bags);World->SetBoolField(TEXT("awaiting_respawn"),Recovery.bAwaitingRespawn);
    World->SetBoolField(TEXT("safe_point_active"),Recovery.SafePoint.bActive);World->SetStringField(TEXT("safe_point_map"),Recovery.SafePoint.MapPackage);
    Result->SetObjectField(TEXT("world_objects"),World);

    auto Released=MakeShared<FJsonObject>();int32 ActiveAreas=0;TArray<TSharedPtr<FJsonValue>> Areas,Bottles;
    for(const auto& Area:Effects.Areas)
    {
        ActiveAreas+=Area.bActive;auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("emitter"),Unsigned(Area.Owner.Emitter));
        Item->SetBoolField(TEXT("active"),Area.bActive);Item->SetNumberField(TEXT("ends_offset_seconds"),Area.EndsOffset);
        Item->SetNumberField(TEXT("tick_seconds"),Area.Period);Item->SetNumberField(TEXT("damage"),Area.Damage);Areas.Add(MakeShared<FJsonValueObject>(Item));
    }
    for(const auto& Bottle:Effects.Bottles)
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("emitter"),Unsigned(Bottle.Owner.Emitter));
        Item->SetNumberField(TEXT("flight_age_seconds"),Bottle.FlightAge);Item->SetNumberField(TEXT("flight_seconds"),Bottle.Shot.FlightSeconds);
        auto Position=MakeShared<FJsonObject>();Position->SetNumberField(TEXT("x"),Bottle.Position.X);
        Position->SetNumberField(TEXT("y"),Bottle.Position.Y);Position->SetNumberField(TEXT("z"),Bottle.Position.Z);Item->SetObjectField(TEXT("position_cm"),Position);
        Item->SetNumberField(TEXT("pending_seconds"),Bottle.PendingSeconds);Bottles.Add(MakeShared<FJsonValueObject>(Item));
    }
    Released->SetNumberField(TEXT("schema"),Complete.bNewAdventure?0:Effects.Schema);
    Released->SetNumberField(TEXT("projectile_count"),Effects.Spores.Num()+Effects.Arrows.Num()+Effects.Bottles.Num());
    Released->SetNumberField(TEXT("spore_count"),Effects.Spores.Num());Released->SetNumberField(TEXT("arrow_count"),Effects.Arrows.Num());
    Released->SetNumberField(TEXT("bottle_count"),Effects.Bottles.Num());Released->SetArrayField(TEXT("bottles"),Bottles);
    Released->SetNumberField(TEXT("area_count"),Effects.Areas.Num());Released->SetNumberField(TEXT("active_area_count"),ActiveAreas);
    Released->SetArrayField(TEXT("areas"),Areas);Released->SetNumberField(TEXT("target_phase_count"),Effects.TargetPhases.Num());
    int32 ActiveActions=0,ActiveEmitters=0,PendingReceipts=0;
    for(const auto& Action:Effects.Authority.Ledger.Actions)ActiveActions+=Action.Active;
    for(const auto& Emitter:Effects.Authority.Ledger.Emitters)ActiveEmitters+=Emitter.Active;
    for(const auto& Receipt:Effects.Authority.Ledger.Receipts)PendingReceipts+=Receipt.Receipt.State==DWCombatCommit::EReceiptState::Pending;
    Released->SetNumberField(TEXT("authority_actor_count"),Effects.Authority.Actors.Num());
    Released->SetNumberField(TEXT("active_action_count"),ActiveActions);Released->SetNumberField(TEXT("active_emitter_count"),ActiveEmitters);
    Released->SetNumberField(TEXT("receipt_count"),static_cast<double>(Effects.Authority.Ledger.Receipts.size()));
    Released->SetNumberField(TEXT("pending_receipt_count"),PendingReceipts);Result->SetObjectField(TEXT("effects"),Released);

    auto Defense=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Packets;
    for(const auto& Packet:Extended.ShieldPackets)
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("receipt"),Unsigned(Packet.Receipt));
        Item->SetNumberField(TEXT("remaining_seconds"),Packet.Remaining);Item->SetNumberField(TEXT("amount"),Packet.Amount);
        Packets.Add(MakeShared<FJsonValueObject>(Item));
    }
    Defense->SetNumberField(TEXT("packet_count"),Extended.ShieldPackets.Num());Defense->SetArrayField(TEXT("packets"),Packets);
    Defense->SetNumberField(TEXT("counter_charges"),Extended.Defense.CounterCharges);
    Defense->SetNumberField(TEXT("perfect_remaining_seconds"),Extended.Defense.PerfectRemaining);
    Defense->SetNumberField(TEXT("reopen_remaining_seconds"),Extended.Defense.ReopenRemaining);Result->SetObjectField(TEXT("shield"),Defense);
    auto Root=MakeShared<FJsonObject>();int32 LiveMines=0;for(const auto& Mine:Extended.Ring.Mines)LiveMines+=!Mine.bConsumed;
    Root->SetBoolField(TEXT("rooted"),Extended.bRooted);Root->SetStringField(TEXT("action"),Unsigned(Extended.RootAction));
    Root->SetBoolField(TEXT("ring_active"),Extended.Ring.bActive);Root->SetNumberField(TEXT("mine_count"),Extended.Ring.Mines.Num());
    Root->SetNumberField(TEXT("unconsumed_mine_count"),LiveMines);Root->SetNumberField(TEXT("mine_emitter_count"),Extended.MineEmitters.Num());
    Root->SetNumberField(TEXT("cooldown_remaining_seconds"),Extended.Ring.CooldownRemainingSeconds);Result->SetObjectField(TEXT("root_and_mines"),Root);
    auto Locks=MakeShared<FJsonObject>();Locks->SetNumberField(TEXT("weapon_seconds"),Extended.WeaponLockRemaining);
    Locks->SetNumberField(TEXT("dash_seconds"),Extended.DashCooldownRemaining);Result->SetObjectField(TEXT("locks"),Locks);
    if(!Recheck(C,Error))return Reject(Error);
    return Encode(Result);
#else
    return DWCombatResourceVerification::Reject(TEXT("Editor automation PIE resource verification only."));
#endif
}

