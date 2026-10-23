#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatPlayerSave.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWWeaponResourceTestProbe.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWEnemyCharacter.h"
#include "DWSaveGame.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace DWPlayerResourcesRuntimeTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI{NewObject<UDWGameInstance>(GEngine)};
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> DamageProbe{NewObject<UDWCombatAuthorityTestProbe>()};
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> InventoryProbe{NewObject<UDWInventoryTransactionTestProbe>()};
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;UDWCombatAuthoritySubsystem* Authority=nullptr;
    FString Error;bool bReady=false;
    FFixture()
    {
        // Init may create a directory or import legacy saves. Force its explicit QA branch before Init.
        const FString Previous=FCommandLine::Get();
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/D03PlayerResources"));
        const FString Isolated=FString::Printf(TEXT("-DWVerificationSavePrefix=DW_QA_D03_ -DWSaveTestDirectory=\"%s\" %s"),*Directory,*Previous);
        FCommandLine::Set(*Isolated);
        GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("D03ResourceWorld")),GetTransientPackage());
        FCommandLine::Set(*Previous);
        World=GI->GetWorld();if(!World||!GI->GetCombatRules())return;
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);
        Player=Spawn<ADWPlayerCharacter>();if(!Player)return;
        Player->WeaponAnimationComponent->bEnabled=false;
        Authority=UDWCombatAuthoritySubsystem::Get(Player);
        bReady=Authority&&Player->HasCombatVitalsReady()&&!GI->HasActiveSlot()&&GI->GetSaveDirectory()==Directory;
    }
    ~FFixture()
    {
        DamageProbe->AnyCallback=nullptr;InventoryProbe->TestHook=nullptr;
        if(!World)return;
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T> T* Spawn()
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        T* A=World->SpawnActor<T>(T::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,P);
        if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    UDWSaveGame* NewSave()
    {
        auto* S=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error))return nullptr;return S;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerVitalsAuthorityPairTest,"DoughWorld.Combat.PlayerResources.SameInstantFactsAndTypedDedup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerVitalsAuthorityPairTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerResourcesRuntimeTests;FFixture F;if(!TestTrue(TEXT("Isolated real player initialized"),F.bReady))return false;
    auto* Save=F.NewSave();if(!TestNotNull(TEXT("New strict save"),Save))return false;
    FDWCombatPlayerVitalsConfig C;if(!FDWCombatPlayerVitalsConfig::Build(*F.GI->GetCombatRules(),C,F.Error))return false;
    auto& V=Save->CombatPlayerResources.Vitals;V.Water=C.Vitals.WaterFloor;V.Satiety=C.Vitals.SatietyFloor;
    V.WaterEmptyElapsedSeconds=C.Vitals.WaterDamagePeriodSeconds*.5;
    V.SatietyEmptyElapsedSeconds=C.Vitals.SatietyDamagePeriodSeconds*.5;
    if(!TestTrue(TEXT("Restore partial clocks"),F.Player->TryApplySaveData(Save)))return false;
    const auto Life=F.Authority->CaptureActorLife(F.Player);
    const float Before=F.Player->Health;const float Amount=(Before-DWCombatPlayerSave::HealthFloor(C))*.1f;
    const float Expected=(Before-Amount)-Amount;int32 Calls=0;
    F.Player->OnTakeAnyDamage.AddDynamic(F.DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Calls;auto* Captured=NewObject<UDWSaveGame>();F.Player->CaptureSaveData(Captured);
        TestEqual(TEXT("Both same-instant health facts visible to first callback"),Captured->Health,Expected);
        TestEqual(TEXT("Save captures same authoritative health"),Captured->CombatPlayerResources.Vitals.Health,static_cast<double>(Expected));
    };
    FDWCombatVitalsDamagePair Pair;Pair.Target=F.Player;Pair.WorldEpoch=F.Authority->GetWorldEpoch();Pair.SourceLife=Life;
    Pair.DehydrationOrdinal=11;Pair.StarvationOrdinal=17;Pair.DehydrationAmount=Pair.StarvationAmount=Amount;
    F.Authority->SubmitVitalsDamagePair(Pair);
    TestEqual(TEXT("Two channels notify separately"),Calls,2);TestEqual(TEXT("Positive facts revise health twice"),F.Player->GetCombatDamageRevision(),uint64(2));
    F.Authority->SubmitVitalsDamagePair(Pair);TestEqual(TEXT("Retry neither channel twice"),Calls,2);
    if(!TestTrue(TEXT("Strict restore starts new life"),F.Player->TryApplySaveData(Save)))return false;
    F.Authority->SubmitVitalsDamagePair(Pair);TestEqual(TEXT("Old source life never recaptured on retry"),Calls,2);
    TestEqual(TEXT("Restore does not count as damage"),F.Player->GetCombatDamageRevision(),uint64(2));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerDamageAuthorityTest,"DoughWorld.Combat.PlayerResources.EnemyMeleeAndFatalFacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerDamageAuthorityTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerResourcesRuntimeTests;using namespace DWCombatCommit;FFixture F;
    if(!TestTrue(TEXT("Isolated player ready"),F.bReady))return false;
    auto* Enemy=F.Spawn<ADWEnemyCharacter>();if(!TestNotNull(TEXT("Enemy"),Enemy))return false;
    FDWCombatDamageCommand C;C.Source=Enemy;C.Target=F.Player;C.TargetLife=F.Authority->CaptureActorLife(F.Player);
    C.Identity={F.Authority->BeginAction(Enemy),1,EActionDamageKind::EnemyMelee,1};C.Amount=F.Player->Health*.1f;
    const auto First=F.Authority->SubmitActionDamage(C);
    TestTrue(TEXT("Enemy melee now uses Player sole authority"),First.AppliedThisCall>0.f);
    TestEqual(TEXT("Repeated hit packet is inert"),F.Authority->SubmitActionDamage(C).AppliedThisCall,0.f);
    int32 Calls=0;F.Player->OnTakeAnyDamage.AddDynamic(F.DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Calls;TestTrue(TEXT("Death health fact precedes delegates"),F.Player->IsDead());
        TestTrue(TEXT("Death clears form before delegates"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);
        TestEqual(TEXT("Death clears special before delegates"),F.Player->GetCombatSpecial(),0.);
        auto* S=NewObject<UDWSaveGame>();F.Player->CaptureSaveData(S);
        TestEqual(TEXT("Death snapshot is coherent"),S->CombatPlayerResources.Vitals.Health,static_cast<double>(S->Health));
    };
    FDamageEvent Event;TestTrue(TEXT("Legacy native entry also reaches same authority"),F.Player->TakeDamage(MAX_flt,Event,nullptr,Enemy)>0.f);
    TestEqual(TEXT("One fatal callback"),Calls,1);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerAtomicRestoreTest,"DoughWorld.Combat.PlayerResources.InventoryOwnedLoadedRestoreIsAtomic",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerAtomicRestoreTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerResourcesRuntimeTests;FFixture F;if(!TestTrue(TEXT("Isolated player ready"),F.bReady))return false;
    auto* Save=F.NewSave();if(!TestNotNull(TEXT("Save candidate"),Save))return false;
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);if(!Binding)return false;
    Save->CombatPlayerResources.OwnedPermanentWeaponIds={Binding->ItemId};Save->CombatPlayerResources.LoadedShotgunShells=1;
    FDWItemStack Stack;Stack.ItemId=Binding->AmmoItemId;Stack.Quantity=2;Save->Inventory={Stack};
    FDWCombatPlayerVitalsConfig C;FDWCombatPlayerVitalsConfig::Build(*F.GI->GetCombatRules(),C,F.Error);
    Save->CombatPlayerResources.Vitals.BaseForm=EDWCombatBaseForm::Yeast;
    Save->CombatPlayerResources.Vitals.FormRemainingSeconds=C.FormDurationSeconds*.5;
    Save->CombatPlayerResources.Vitals.Special=C.InitialFormSpecial;
    Save->bYeastForm=true;Save->Transformation=static_cast<float>(C.InitialFormSpecial);
    int32 Notices=0;
    F.Player->Inventory->OnChanged.AddDynamic(F.InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.InventoryProbe->TestHook=[&]()
    {
        ++Notices;auto* Captured=NewObject<UDWSaveGame>();F.Player->CaptureSaveData(Captured);
        TestEqual(TEXT("Inventory observer sees loaded shells restored"),Captured->CombatPlayerResources.LoadedShotgunShells,1);
        TestEqual(TEXT("Inventory observer sees reserve restored"),F.Player->Inventory->CountItem(Binding->AmmoItemId),2);
        TestTrue(TEXT("Inventory observer sees owned state restored"),Captured->CombatPlayerResources.OwnedPermanentWeaponIds.Contains(Binding->ItemId));
        TestTrue(TEXT("Inventory observer sees form restored"),Captured->CombatPlayerResources.Vitals.BaseForm==EDWCombatBaseForm::Yeast);
    };
    if(!TestTrue(TEXT("Valid snapshot adopted"),F.Player->TryApplySaveData(Save)))return false;
    TestEqual(TEXT("One final inventory notification"),Notices,1);
    const auto Life=F.Authority->CaptureActorLife(F.Player);const auto Revision=F.Player->Inventory->GetInventoryRevision();
    const auto WeaponRevision=F.Player->WeaponAnimationComponent->GetCombatWeaponResourceRevision();const float Health=F.Player->Health;
    auto* Bad=DuplicateObject<UDWSaveGame>(Save,GetTransientPackage());Bad->Inventory[0].Quantity=-1;
    Bad->Health=DWCombatPlayerSave::NativeHealth((Health+C.Vitals.HealthFloor)*.5,C);Bad->CombatPlayerResources.Vitals.Health=Bad->Health;
    Bad->CombatPlayerResources.LoadedShotgunShells=0;Bad->CombatPlayerResources.OwnedPermanentWeaponIds.Reset();
    TestFalse(TEXT("Invalid inventory rejects entire restore"),F.Player->TryApplySaveData(Bad));
    TestEqual(TEXT("Health unchanged"),F.Player->Health,Health);TestEqual(TEXT("Inventory revision unchanged"),F.Player->Inventory->GetInventoryRevision(),Revision);
    TestEqual(TEXT("Weapon revision unchanged"),F.Player->WeaponAnimationComponent->GetCombatWeaponResourceRevision(),WeaponRevision);
    TestEqual(TEXT("Loaded unchanged"),F.Player->WeaponAnimationComponent->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
    TestTrue(TEXT("Owned unchanged"),F.Player->WeaponAnimationComponent->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Binding->ItemId));
    TestTrue(TEXT("Form unchanged"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Yeast);
    TestEqual(TEXT("Life unchanged"),F.Authority->CaptureActorLife(F.Player).Life,Life.Life);
    TestEqual(TEXT("No notification on invalid snapshot"),Notices,1);
    Bad=NewObject<UDWSaveGame>();TestFalse(TEXT("Missing new save block rejected"),F.Player->TryApplySaveData(Bad));
    TestEqual(TEXT("No active slot ever opened"),F.GI->GetActiveSlot(),INDEX_NONE);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerLegacyHealAtomicTest,"DoughWorld.Combat.PlayerResources.LegacyDoughHealFactsBeforeInventoryNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerLegacyHealAtomicTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerResourcesRuntimeTests;FFixture F;if(!TestTrue(TEXT("Isolated player ready"),F.bReady))return false;
    // Clone configuration only inside the transient fixture, preserving production asset values.
    auto* Config=DuplicateObject<UDWGameplayConfig>(F.Player->GameplayConfig,F.Player);
    Config->ItemTable=nullptr;FDWItemDefinition Item;Item.ItemId=TEXT("D03TestHeal");Item.bUsable=true;Item.HealAmount=7.f;Config->Items.Add(Item);
    F.Player->GameplayConfig=Config;F.Player->Inventory->InitializeInventory(Config);
    F.Player->Inventory->TryAddItem(Item.ItemId,1);const float Before=F.Player->Health*.5f;F.Player->Health=Before;
    int32 Calls=0;F.Player->Inventory->OnChanged.AddDynamic(F.InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.InventoryProbe->TestHook=[&](){++Calls;TestEqual(TEXT("Inventory callback already sees healing"),F.Player->Health,Before+7.f);TestEqual(TEXT("Consumed exactly once"),F.Player->Inventory->CountItem(Item.ItemId),0);};
    TestTrue(TEXT("Legacy configured heal remains usable"),F.Player->UseInventoryItem(0));
    TestEqual(TEXT("One inventory callback"),Calls,1);TestFalse(TEXT("Empty exact slot cannot replay"),F.Player->UseInventoryItem(0));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerResourceClockPauseTest,"DoughWorld.Combat.PlayerResources.ClockPauseBypassAndCommittedSave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerResourceClockPauseTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerResourcesRuntimeTests;FFixture F;if(!TestTrue(TEXT("Isolated player ready"),F.bReady))return false;
    auto* Save=F.NewSave();if(!Save)return false;
    FDWCombatPlayerVitalsConfig C;if(!FDWCombatPlayerVitalsConfig::Build(*F.GI->GetCombatRules(),C,F.Error))return false;
    const float Step=static_cast<float>(FMath::Min(C.Vitals.WaterDamagePeriodSeconds,C.Vitals.SatietyDamagePeriodSeconds)*.125);
    auto& V=Save->CombatPlayerResources.Vitals;V.Water=C.Vitals.WaterFloor;V.Satiety=C.Vitals.SatietyFloor;
    V.WaterEmptyElapsedSeconds=C.Vitals.WaterDamagePeriodSeconds-Step;
    V.SatietyEmptyElapsedSeconds=C.Vitals.SatietyDamagePeriodSeconds-Step;
    V.BaseForm=EDWCombatBaseForm::Yeast;V.FormRemainingSeconds=C.FormDurationSeconds;V.Special=C.InitialFormSpecial;
    Save->bYeastForm=true;Save->Transformation=static_cast<float>(C.InitialFormSpecial);
    if(!TestTrue(TEXT("Restore partial resource clocks and yeast"),F.Player->TryApplySaveData(Save)))return false;
    const float Before=F.Player->Health;
    auto* PauseOwner=F.Spawn<APlayerState>();F.World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);
    F.Player->TickGameplayClocks(3600.f,false);FDamageEvent Event;
    TestEqual(TEXT("Direct damage entry freezes while paused"),F.Player->TakeDamage(10.f,Event,nullptr,nullptr),0.f);
    TestEqual(TEXT("Pause does not consume form duration"),F.Player->GetCombatFormRemaining(),C.FormDurationSeconds);
    TestEqual(TEXT("Pause does not consume health"),F.Player->Health,Before);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    const float Floor=DWCombatPlayerSave::HealthFloor(C);
    const float Expected=FMath::Max(Floor,FMath::Max(Floor,Before-static_cast<float>(C.Vitals.WaterDamagePerEvent))-static_cast<float>(C.Vitals.SatietyDamagePerEvent));
    int32 Calls=0;F.Player->OnTakeAnyDamage.AddDynamic(F.DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Calls;auto* Captured=NewObject<UDWSaveGame>();F.Player->CaptureSaveData(Captured);
        TestEqual(TEXT("Both bypass packets committed before callback"),F.Player->Health,Expected);
        TestEqual(TEXT("Water tick saved exactly once"),Captured->CombatPlayerResources.Vitals.DehydrationOrdinal,uint64(1));
        TestEqual(TEXT("Food tick saved exactly once"),Captured->CombatPlayerResources.Vitals.StarvationOrdinal,uint64(1));
        TestTrue(TEXT("Resource snapshot valid during first damage callback"),DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*Captured,F.Error));
    };
    F.Player->TickGameplayClocks(Step,false);
    TestEqual(TEXT("No paused time catch-up; only configured due hits"),F.Player->Health,Expected);
    TestEqual(TEXT("Positive periodic channels notify once each"),Calls,(C.Vitals.WaterDamagePerEvent>0.?1:0)+(C.Vitals.SatietyDamagePerEvent>0.?1:0));
    TestTrue(TEXT("Independent form advanced only resumed delta"),FMath::IsNearlyEqual(F.Player->GetCombatFormRemaining(),C.FormDurationSeconds-Step,1.e-6));return true;
}

#endif
