#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatHUDModel.h"
#include "Engine/DamageEvents.h"
#include "DWCombatRules.h"
#include "DWCombatTestWorld.h"
#include "DWCombatPlayerSave.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWUserSettings.h"
#include "DWGameplayWidget.h"
#include "DWSaveGame.h"
#include "Components/CheckBox.h"
#include "Components/VerticalBox.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"
#include <cmath>
#include <limits>

namespace DWCombatHUDTests
{
struct FIsolatedUserIni
{
    FString Before=GGameUserSettingsIni,Path;
    FIsolatedUserIni()
    {
        const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/CombatFixtures"));
        IFileManager::Get().MakeDirectory(*Folder,true);
        Path=Folder/(TEXT("D20_UserPreferences_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".ini"));
        // UE 5.8 SetString/SetBool only changes an existing config cache entry.
        GConfig->Add(Path,FConfigFile());
        GGameUserSettingsIni=Path; // Never saves or deletes the real user preference file.
    }
    ~FIsolatedUserIni(){GGameUserSettingsIni=Before;}
};
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;
    FDWCombatHUDConfig Config;FString Error;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D20CombatHUD"));if(!World)return;
        Player=Spawn<ADWPlayerCharacter>();if(GI->GetCombatRules())FDWCombatHUDConfig::Build(*GI->GetCombatRules(),Config,Error);
    }
    ~FFixture()
    {
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn()
    {FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<T>(T::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,P);if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;}
    bool Ready()const{return Player&&Player->HasCombatVitalsReady()&&Config.MessageSeconds>0.&&!GI->HasActiveSlot();}
    UDWSaveGame* Candidate(EDWCombatBaseForm Form=EDWCombatBaseForm::Normal,double Special=0.,double Remaining=0.)
    {
        auto* S=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error))return nullptr;
        auto& V=S->CombatPlayerResources.Vitals;V.BaseForm=Form;V.Special=Special;V.FormRemainingSeconds=Remaining;
        S->Transformation=static_cast<float>(Special);S->bYeastForm=Form==EDWCombatBaseForm::Yeast;return S;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWHUDThresholdTest,"DoughWorld.Combat.HUD.FormalResourceThresholds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWHUDThresholdTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWCombatHUDConfig C;
    if(!TestTrue(TEXT("Formal HUD rules load"),FDWCombatRules::LoadProjectPayload(Rules,Error)&&FDWCombatHUDConfig::Build(*Rules,C,Error)))return false;
    const double Threshold=C.Dash.SpecialFloor+C.Dash.MinimumBudget();
    TestEqual(TEXT("Exactly funded minimum dash is eligible"),DWCombatHUD::DashResourceReason(C.Dash,EDWCombatBaseForm::Yeast,Threshold,C.Dash.MinimumSeconds,0.),EDWCombatHUDReason::None);
    TestEqual(TEXT("One representable value below budget remains insufficient"),DWCombatHUD::DashResourceReason(C.Dash,EDWCombatBaseForm::Yeast,std::nextafter(Threshold,-std::numeric_limits<double>::infinity()),C.Dash.MinimumSeconds,0.),EDWCombatHUDReason::SpecialForDash);
    TestEqual(TEXT("Form duration has its own strict lower boundary"),DWCombatHUD::DashResourceReason(C.Dash,EDWCombatBaseForm::Yeast,C.Dash.SpecialMax,std::nextafter(C.Dash.MinimumSeconds,0.),0.),EDWCombatHUDReason::FormTooShort);
    TestEqual(TEXT("Cooldown reason is separate"),DWCombatHUD::DashResourceReason(C.Dash,EDWCombatBaseForm::Yeast,C.Dash.SpecialMax,C.Dash.FormDuration,.01),EDWCombatHUDReason::Cooldown);
    const double RootThreshold=C.Root.StartCost+C.Root.SpecialFloor;
    TestEqual(TEXT("Exactly root entry cost is not enough"),DWCombatHUD::RootResourceReason(C.Root,EDWCombatBaseForm::Mold,RootThreshold,true),EDWCombatHUDReason::SpecialForRoot);
    TestEqual(TEXT("Above cost still needs ground"),DWCombatHUD::RootResourceReason(C.Root,EDWCombatBaseForm::Mold,RootThreshold+1.,false),EDWCombatHUDReason::NotGrounded);
    TestEqual(TEXT("Qualified root resource/ground status"),DWCombatHUD::RootResourceReason(C.Root,EDWCombatBaseForm::Mold,RootThreshold+1.,true),EDWCombatHUDReason::None);
    TestEqual(TEXT("Nonzero floors normalize from floor"),DWCombatHUD::Resource(30.,10.,50.).Fraction,.5);
    TestEqual(TEXT("Invalid resources never draw NaN fill"),DWCombatHUD::Resource(std::numeric_limits<double>::quiet_NaN(),0.,50.).Fraction,0.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWHUDSnapshotTest,"DoughWorld.Combat.HUD.SnapshotIsReadOnlyAndFormTimeIndependent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWHUDSnapshotTest::RunTest(const FString&)
{
    DWCombatHUDTests::FFixture F;if(!TestTrue(TEXT("Isolated player ready"),F.Ready()))return false;
    auto* Save=F.Candidate(EDWCombatBaseForm::Yeast,F.Config.Player.Vitals.SpecialFloor,F.Config.FormWarningSeconds);
    if(!TestTrue(TEXT("Legal zero-special active-form snapshot restores"),Save&&F.Player->TryApplySaveData(Save)))return false;
    FDWCombatHUDSnapshot Before,After;TestTrue(TEXT("Read HUD"),DWCombatHUD::Build(F.Player,F.Config,Before));
    TestEqual(TEXT("Special can be empty without erasing form timer"),Before.Special.Value,F.Config.Player.Vitals.SpecialFloor);
    TestEqual(TEXT("Timer from independent source"),Before.FormRemaining,F.Config.FormWarningSeconds);
    TestTrue(TEXT("Warning uses configured boundary"),Before.bFormWarning);
    auto* Pauser=F.Spawn<APlayerState>();F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    const auto Revision=F.Player->GetCombatDamageRevision();
    for(int32 I=0;I<30;++I)TestTrue(TEXT("Paused read succeeds"),DWCombatHUD::Build(F.Player,F.Config,After));
    TestEqual(TEXT("Paint reads do not advance form time"),After.FormRemaining,Before.FormRemaining);
    TestEqual(TEXT("Paint reads do not drain water"),After.Water.Value,Before.Water.Value);
    TestEqual(TEXT("Paint reads never cause damage"),F.Player->GetCombatDamageRevision(),Revision);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWHUDAmmoTest,"DoughWorld.Combat.HUD.EquipmentReserveAndLoadedUseCommittedFacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWHUDAmmoTest::RunTest(const FString&)
{
    DWCombatHUDTests::FFixture F;if(!F.Ready())return false;auto* Save=F.Candidate();
    Save->CombatPlayerResources.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_Shotgun"));
    Save->CombatPlayerResources.CurrentEquipmentId=TEXT("DWWeapon_Shotgun");Save->CombatPlayerResources.LoadedShotgunShells=2;
    if(!TestTrue(TEXT("Explicit legal owned/equipped fixture"),F.Player->TryApplySaveData(Save)))return false;
    TestTrue(TEXT("Reserve granted only by fixture"),F.Player->Inventory->TryAddItem(TEXT("DWAmmo_BreadShell"),7));
    FDWCombatHUDSnapshot S;TestTrue(TEXT("HUD reads"),DWCombatHUD::Build(F.Player,F.Config,S));
    TestEqual(TEXT("Current identity"),S.EquipmentId,FName(TEXT("DWWeapon_Shotgun")));
    TestEqual(TEXT("Reserve is inventory quantity"),S.Reserve,7);TestEqual(TEXT("Loaded is authoritative weapon snapshot"),S.Loaded,2);
    TestEqual(TEXT("Capacity is rule value"),S.MagazineCapacity,F.Config.ShotgunCapacity);TestTrue(TEXT("Shotgun exposes magazine"),S.bUsesMagazine);
    TestEqual(TEXT("Loaded shells mean no dry-fire message even without refill"),S.AmmoReason,EDWCombatHUDReason::None);
    auto Weapon=F.Player->WeaponAnimationComponent->GetCombatWeaponStateSnapshot();Weapon.LoadedShotgunShells=0;
    TestTrue(TEXT("Empty magazine fixture"),F.Player->WeaponAnimationComponent->RestoreCombatWeaponState(Weapon,*F.GI->GetCombatRules(),F.Error));
    DWCombatHUD::Build(F.Player,F.Config,S);TestEqual(TEXT("Empty loaded with reserve asks reload"),S.AmmoReason,EDWCombatHUDReason::EmptyMagazine);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWHUDPreferenceTest,"DoughWorld.Combat.HUD.PreferencePersistsOutsideAdventure",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWHUDPreferenceTest::RunTest(const FString&)
{
    DWCombatHUDTests::FIsolatedUserIni Ini;
    {
        DWCombatHUDTests::FFixture F;if(!F.Ready())return false;auto* Settings=UDWUserSettings::Resolve(F.Player);
        if(!TestNotNull(TEXT("User settings subsystem"),Settings))return false;
        TestTrue(TEXT("Missing local preference defaults on"),Settings->GetShowEnemyHealth());Settings->SetShowEnemyHealth(false,true);
        TestTrue(TEXT("Isolated preference file exists on disk"),FPaths::FileExists(Ini.Path));FConfigFile Disk;Disk.Read(Ini.Path);
        bool Value=true;TestTrue(TEXT("Persisted dedicated key"),Disk.GetBool(TEXT("DoughWorld.CombatHUD"),TEXT("ShowEnemyHealth"),Value));TestFalse(TEXT("Saved preference is off"),Value);
        auto* Save=F.Candidate();TestTrue(TEXT("Adventure restoration succeeds"),F.Player->TryApplySaveData(Save));
        TestFalse(TEXT("Adventure restore does not re-enable enemy health"),Settings->GetShowEnemyHealth());
    }
    {
        DWCombatHUDTests::FFixture Fresh;if(!Fresh.Ready())return false;
        TestFalse(TEXT("New game instance reloads saved off preference"),UDWUserSettings::Resolve(Fresh.Player)->GetShowEnemyHealth());
    }
    return true; // Dedicated test INI retained; real preferences and adventure slots were not touched.
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWHUDEnemyFilterTest,"DoughWorld.Combat.HUD.EnemyOnlyAndDaggerCircleIndependentOfHealthSetting",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWHUDEnemyFilterTest::RunTest(const FString&)
{
    DWCombatHUDTests::FFixture F;if(!F.Ready())return false;auto* Enemy=F.Spawn<ADWEnemyCharacter>();auto* NonCombat=F.Spawn<AActor>();
    TestTrue(TEXT("Living combat enemy can have a bar"),DWCombatHUD::ShouldShowEnemyHealth(Enemy,F.Player,true));
    TestFalse(TEXT("Preference off hides it"),DWCombatHUD::ShouldShowEnemyHealth(Enemy,F.Player,false));
    TestFalse(TEXT("Generic NPC/actor never inferred to be an enemy"),DWCombatHUD::ShouldShowEnemyHealth(NonCombat,F.Player,true));
    TestFalse(TEXT("Player never gets enemy bar"),DWCombatHUD::ShouldShowEnemyHealth(F.Player,F.Player,true));
    TestFalse(TEXT("Unarmed has no dagger circle"),DWCombatHUD::ShouldShowDaggerCircle(Enemy,F.Player));
    auto* Save=F.Candidate();Save->CombatPlayerResources.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_DualDaggers"));Save->CombatPlayerResources.CurrentEquipmentId=TEXT("DWWeapon_DualDaggers");
    if(!TestTrue(TEXT("Legally equip dagger fixture"),F.Player->TryApplySaveData(Save)))return false;
    TestTrue(TEXT("Dagger circle does not depend on health preference"),DWCombatHUD::ShouldShowDaggerCircle(Enemy,F.Player));
    Enemy->SetHealthForLoad(0.f);TestFalse(TEXT("Dead enemies have no health bar"),DWCombatHUD::ShouldShowEnemyHealth(Enemy,F.Player,true));
    TestFalse(TEXT("Dead enemies have no dagger circle"),DWCombatHUD::ShouldShowDaggerCircle(Enemy,F.Player));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWHUDWidgetTest,"DoughWorld.Combat.HUD.ExistingSettingsBookGetsOneLocalCheckbox",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWHUDWidgetTest::RunTest(const FString&)
{
    DWCombatHUDTests::FIsolatedUserIni Ini;DWCombatHUDTests::FFixture F;if(!F.Ready())return false;
    auto Class=LoadClass<UDWGameplayWidget>(nullptr,TEXT("/Game/DoughWorld/UI/WBP_DWGameplay.WBP_DWGameplay_C"));
    if(!TestNotNull(TEXT("Existing authored WBP"),Class))return false;
    auto* Widget=CreateWidget<UDWGameplayWidget>(F.GI.Get(),Class);if(!TestNotNull(TEXT("Transient runtime widget"),Widget))return false;
    TStrongObjectPtr<UDWGameplayWidget> Keep(Widget);
    auto* General=Cast<UVerticalBox>(Widget->GetWidgetFromName(TEXT("SettingsBookGeneralPage")));
    if(!TestNotNull(TEXT("Actual existing General container"),General))return false;
    const int32 Before=General->GetChildrenCount();Widget->InitializeCombatSettings();
    TestEqual(TEXT("Only one row appended"),General->GetChildrenCount(),Before+1);Widget->InitializeCombatSettings();
    TestEqual(TEXT("Second initialization is idempotent"),General->GetChildrenCount(),Before+1);
    auto* Check=Cast<UCheckBox>(Widget->GetWidgetFromName(TEXT("CombatEnemyHealthCheck")));
    if(!TestNotNull(TEXT("Real runtime checkbox entry"),Check))return false;
    Check->OnCheckStateChanged.Broadcast(false);
    TestFalse(TEXT("Real checkbox delegate updates independent preference"),UDWUserSettings::Resolve(F.Player)->GetShowEnemyHealth());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWHUDShotgunOverheadTest,"DoughWorld.Combat.HUD.ShotgunOverheadReadsCurrentShellCycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWHUDShotgunOverheadTest::RunTest(const FString&)
{
    DWCombatHUDTests::FFixture F;if(!TestTrue(TEXT("Isolated no-slot HUD fixture ready"),F.Ready()))return false;
    auto* Save=F.Candidate();if(!Save)return false;
    Save->CombatPlayerResources.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_Shotgun"));
    Save->CombatPlayerResources.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_Spear"));
    Save->CombatPlayerResources.CurrentEquipmentId=TEXT("DWWeapon_Shotgun");Save->CombatPlayerResources.LoadedShotgunShells=1;
    if(!TestTrue(TEXT("Explicit owned shotgun snapshot restores"),F.Player->TryApplySaveData(Save)))return false;
    auto* W=F.Player->WeaponAnimationComponent.Get();auto* I=F.Player->GetInventory();
    if(!W||!I||!TestTrue(TEXT("Reserve exists only in fixture"),I->TryAddItem(TEXT("DWAmmo_BreadShell"),3)))return false;
    TestTrue(TEXT("Selected living shotgun owner qualifies for overhead"),DWCombatHUD::ShouldShowShotgunOverhead(F.Player));
    if(!TestTrue(TEXT("Actual native per-shell reload starts"),W->RequestReload()))return false;
    FDWCombatHUDSnapshot S;
    TestTrue(TEXT("Read initial reload"),DWCombatHUD::Build(F.Player,F.Config,S));
    TestTrue(TEXT("Ring reflects native reload phase"),S.bShotgunReloading);
    TestEqual(TEXT("Capacity follows formal rule, not six painted constants"),S.MagazineCapacity,F.Config.ShotgunCapacity);
    TestEqual(TEXT("Initial loaded shells remain committed fact"),S.Loaded,1);
    TestEqual(TEXT("Current shell begins at zero"),S.ShotgunReloadProgress,0.);
    const double Duration=W->GetFormalShotgunReloadDurationSeconds();
    if(!TestTrue(TEXT("Current native shell duration is valid"),Duration>0.))return false;
    const auto Advance=[&](double Seconds){F.World->TimeSeconds+=static_cast<float>(Seconds);W->AdvanceAnimation(Seconds);};
    Advance(Duration*.5);DWCombatHUD::Build(F.Player,F.Config,S);
    TestTrue(TEXT("Half actual shell time shows half ring"),FMath::IsNearlyEqual(S.ShotgunReloadProgress,.5,1.e-8));
    TestEqual(TEXT("Unfinished shell does not become highlighted"),S.Loaded,1);
    const auto WeaponRevision=W->GetCombatWeaponResourceRevision(),InventoryRevision=I->GetInventoryRevision();
    const double Remaining=S.ShotgunReloadRemainingSeconds;
    for(int32 Read=0;Read<12;++Read)DWCombatHUD::Build(F.Player,F.Config,S);
    TestEqual(TEXT("Paint reads cannot advance shell clock"),S.ShotgunReloadRemainingSeconds,Remaining);
    TestEqual(TEXT("Paint reads cannot spend reserve"),I->GetInventoryRevision(),InventoryRevision);
    TestEqual(TEXT("Paint reads cannot commit magazine"),W->GetCombatWeaponResourceRevision(),WeaponRevision);
    auto* Pauser=F.Spawn<APlayerState>();F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    TestFalse(TEXT("Paused gameplay hides overhead"),DWCombatHUD::ShouldShowShotgunOverhead(F.Player));
    DWCombatHUD::Build(F.Player,F.Config,S);TestEqual(TEXT("Paused inspection keeps progress"),S.ShotgunReloadRemainingSeconds,Remaining);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    Advance(Remaining);DWCombatHUD::Build(F.Player,F.Config,S);
    TestEqual(TEXT("Completed native transaction highlights second shell"),S.Loaded,2);
    TestEqual(TEXT("Exactly one reserve shell paid"),I->CountItem(TEXT("DWAmmo_BreadShell")),2);
    TestTrue(TEXT("Reload continues into next shell"),S.bShotgunReloading);
    TestEqual(TEXT("Each shell has a new empty progress ring"),S.ShotgunReloadProgress,0.);
    Advance(Duration*.25);W->CancelAction();DWCombatHUD::Build(F.Player,F.Config,S);
    TestFalse(TEXT("Cancellation removes active ring"),S.bShotgunReloading);
    TestEqual(TEXT("Cancelled ring has no residual progress"),S.ShotgunReloadProgress,0.);
    TestEqual(TEXT("Cancellation preserves completed highlighted shells"),S.Loaded,2);
    TestEqual(TEXT("Unfinished next shell is not paid"),I->CountItem(TEXT("DWAmmo_BreadShell")),2);
    TestTrue(TEXT("Actual weapon switch accepted"),W->SelectWeapon(EDWAnimatedWeapon::Spear));
    TestFalse(TEXT("Switching away hides shotgun overhead"),DWCombatHUD::ShouldShowShotgunOverhead(F.Player));
    auto Identity=W->GetCombatWeaponStateSnapshot();Identity.CurrentEquipmentId=TEXT("DWWeapon_Shotgun");
    if(!TestTrue(TEXT("Legal fixture re-equips for death filter"),W->RestoreCombatWeaponState(Identity,*F.GI->GetCombatRules(),F.Error)))return false;
    F.Player->SetActorHiddenInGame(true);TestFalse(TEXT("Hidden pawn hides overhead"),DWCombatHUD::ShouldShowShotgunOverhead(F.Player));
    F.Player->SetActorHiddenInGame(false);FDamageEvent LethalEvent;
    F.Player->TakeDamage(F.Player->GetHealth()+1.f,LethalEvent,nullptr,nullptr);
    TestTrue(TEXT("Actual lethal damage committed player death"),F.Player->IsDead());
    TestFalse(TEXT("Death hides overhead"),DWCombatHUD::ShouldShowShotgunOverhead(F.Player));
    return true;
}
#endif
