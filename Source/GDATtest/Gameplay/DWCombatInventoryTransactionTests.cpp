#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatRules.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWWeaponResourceTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatShotgun.h"
#include "Animation/AnimSequence.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "DWCombatTestWorld.h"
#include "UObject/StrongObjectPtr.h"

namespace DWCombatInventoryTests
{
    const FName Shell(TEXT("DWAmmo_BreadShell"));
    const FName Shotgun(TEXT("DWWeapon_Shotgun"));
    struct FFixture
    {
        TStrongObjectPtr<UDWGameInstance> CombatTestGI;
        UWorld* World=nullptr;
        ADWPlayerCharacter* Player=nullptr;
        TSharedPtr<const FDWCombatRules> Rules;
        bool bReady=false;
        FString Error;
        FFixture(int32 Loaded,int32 Stock,bool bOwned=true)
        {
            if(!GEngine||!FDWCombatRules::LoadProjectPayload(Rules,Error))return;
            World=DWCombatTestWorld::Create(CombatTestGI,TEXT("DWCombatInventoryTransactionTests"));
            if(!World)return;
            FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Player=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Spawn);
            if(Player&&!Player->HasActorBegunPlay())Player->DispatchBeginPlay();
            if(!Player)return;
            if(Stock>0&&!Bag()->TryAddItem(Shell,Stock))return;
            auto* Set=NewObject<UDWWeaponAnimationSet>(Player);
            Set->Weapon=EDWAnimatedWeapon::Shotgun;Set->MagazineCapacity=6;Set->AttackIntervalSeconds=.6f;Set->bApplyBasicDamage=false;
            AddClip(Set,TEXT("Attack"),.3f);
            // Two authored events deliberately reference one shot: one resource operation, not two shells.
            FDWWeaponDamageEvent Event;Event.TimeSeconds=.1f;Set->Clips[0].DamageEvents={Event,Event};
            AddClip(Set,TEXT("ReloadOne"),.55f);
            Component()->AnimationSets.Add(Set);
            FDWCombatWeaponResourceSnapshot Snapshot;
            if(bOwned){Snapshot.OwnedPermanentWeaponIds.Add(Shotgun);Snapshot.CurrentEquipmentId=Shotgun;}
            Snapshot.LoadedShotgunShells=Loaded;
            if(!Component()->RestoreCombatWeaponState(Snapshot,*Rules,Error))return;
            if(bOwned&&!Component()->SelectWeapon(EDWAnimatedWeapon::Shotgun))return;
            bReady=UDWCombatAuthoritySubsystem::Get(Player)!=nullptr;
        }
        ~FFixture()
        {
            if(!World)return;
            for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            CombatTestGI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
        }
        UDWWeaponAnimationComponent* Component()const{return Player?Player->WeaponAnimationComponent.Get():nullptr;}
        UDWInventoryComponent* Bag()const{return Player?Player->GetInventory():nullptr;}
        void AddClip(UDWWeaponAnimationSet* Set,FName Role,float Duration)
        {FDWWeaponAnimationClip Clip;Clip.Role=Role;Clip.DurationSeconds=Duration;Clip.Animation=NewObject<UAnimSequence>(Set);Set->Clips.Add(Clip);}
        void Advance(float Seconds){World->TimeSeconds+=Seconds;Component()->AdvanceAnimation(Seconds);}
        FVector Target()const{return Player->GetActorLocation()+FVector(400,0,0);}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAtomicReloadFactsTest,"DoughWorld.InventoryTransactions.Reload.AtomicFactsBeforeRealNotification",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAtomicReloadFactsTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInventoryTests;
    FFixture F(0,1);if(!TestTrue(TEXT("Isolated reserve-backed fixture"),F.bReady))return false;
    auto* C=F.Component();const int64 InventoryRevision=F.Bag()->GetInventoryRevision();const int64 WeaponRevision=C->GetCombatWeaponResourceRevision();
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    int32 Notices=0;
    Probe->TestHook=[&]()
    {
        ++Notices;
        TestEqual(TEXT("Actual dynamic inventory listener sees reserve already removed"),F.Bag()->CountItem(Shell),0);
        TestEqual(TEXT("Listener sees canonical loaded shell already added"),C->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
        TestEqual(TEXT("Listener sees display mirror already added"),C->AmmoInMagazine,1);
        TestEqual(TEXT("Inventory revision already committed"),F.Bag()->GetInventoryRevision(),InventoryRevision+1);
        TestEqual(TEXT("Weapon revision already committed"),C->GetCombatWeaponResourceRevision(),WeaponRevision+1);
        const auto Receipt=UDWCombatAuthoritySubsystem::Get(F.Player)->FindReceipt(C->GetLastWeaponResourceReceipt());
        TestTrue(TEXT("Listener sees final receipt, never pending"),Receipt&&Receipt->State==DWCombatCommit::EReceiptState::Committed);
        C->AdvanceAnimation(.01f); // Real synchronous reentry at the same completed ReloadOne.
        TestEqual(TEXT("Reentered completion cannot add another shell"),C->AmmoInMagazine,1);
    };
    F.Bag()->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Start reload"),C->RequestReload());F.Advance(.549f);
    TestEqual(TEXT("Before exact completion reserve retained"),F.Bag()->CountItem(Shell),1);
    TestEqual(TEXT("Before exact completion no loaded shell"),C->AmmoInMagazine,0);
    F.Advance(.002f);
    TestEqual(TEXT("One native commit emits one inventory notification"),Notices,1);
    TestFalse(TEXT("No stock stops reload"),C->IsBusy());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAtomicReloadReentryTest,"DoughWorld.InventoryTransactions.Reload.CallbackCannotAdvanceReplacementClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAtomicReloadReentryTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInventoryTests;
    FFixture F(0,2);if(!TestTrue(TEXT("Fixture"),F.bReady))return false;auto* C=F.Component();
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> InventoryProbe(NewObject<UDWInventoryTransactionTestProbe>());
    TStrongObjectPtr<UDWWeaponResourceTestProbe> ResourceProbe(NewObject<UDWWeaponResourceTestProbe>());
    int32 InventoryNotices=0;bool bInOuter=false,bNestedNoticeDuringOuter=false;TArray<int32> Payloads;
    InventoryProbe->TestHook=[&]()
    {
        ++InventoryNotices;
        if(InventoryNotices==1)
        {
            bInOuter=true;C->CancelAction();
            TestTrue(TEXT("A new legitimate reload starts during outer notification"),C->RequestReload());
            TestFalse(TEXT("Callback cannot recursively advance the replacement clock"),C->AdvanceAnimation(.55));
            TestEqual(TEXT("Replacement has not paid a new shell"),C->AmmoInMagazine,1);
            TestEqual(TEXT("Replacement clock receives no old carry"),C->GetElapsedSeconds(),0.f);
            TestEqual(TEXT("Only original inventory notice has occurred"),InventoryNotices,1);
            bInOuter=false;
        }
        else bNestedNoticeDuringOuter|=bInOuter;
    };
    ResourceProbe->TestHook=[&](int32 Loaded,int64,bool bReload){TestTrue(TEXT("Reload typed notification"),bReload);Payloads.Add(Loaded);};
    F.Bag()->OnChanged.AddDynamic(InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    C->OnWeaponResourceChanged.AddDynamic(ResourceProbe.Get(),&UDWWeaponResourceTestProbe::OnResources);
    TestTrue(TEXT("Outer start"),C->RequestReload());F.Advance(.55f);
    TestEqual(TEXT("Exactly one shell before a new explicit advance"),C->AmmoInMagazine,1);
    TestEqual(TEXT("Reserve remains for the replacement"),F.Bag()->CountItem(Shell),1);
    TestEqual(TEXT("One typed result before new time"),Payloads.Num(),1);
    F.Advance(.55f);
    TestFalse(TEXT("No callback recursively publishes another inventory notice"),bNestedNoticeDuringOuter);
    TestEqual(TEXT("Exactly two committed inventory notifications"),InventoryNotices,2);
    TestEqual(TEXT("Two typed resource results"),Payloads.Num(),2);
    if(Payloads.Num()==2){TestEqual(TEXT("Outer committed snapshot first"),Payloads[0],1);TestEqual(TEXT("Later replacement snapshot follows"),Payloads[1],2);}
    TestEqual(TEXT("Reserve conserved"),F.Bag()->CountItem(Shell),0);TestEqual(TEXT("Loaded conserved"),C->AmmoInMagazine,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWReloadCancelAndStockTest,"DoughWorld.InventoryTransactions.Reload.CancelEmptyFullAndOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWReloadCancelAndStockTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInventoryTests;
    FFixture F(0,2);if(!TestTrue(TEXT("Fixture"),F.bReady))return false;auto* C=F.Component();
    TestTrue(TEXT("Begin partial shell"),C->RequestReload());F.Advance(.2f);C->CancelAction();F.Advance(1.f);
    TestEqual(TEXT("Cancelled incomplete shell does not consume"),F.Bag()->CountItem(Shell),2);TestEqual(TEXT("Cancelled shell not loaded"),C->AmmoInMagazine,0);
    TestTrue(TEXT("New reload"),C->RequestReload());F.Advance(.55f);F.Advance(.2f);C->CancelAction();
    TestEqual(TEXT("Already finished shell retained"),C->AmmoInMagazine,1);TestEqual(TEXT("Only finished shell consumed"),F.Bag()->CountItem(Shell),1);
    C->AnimationSets[0]->Clips[1].DurationSeconds=.7f;
    // Source animation is presentation; it neither changes nor blocks the formal .55s transaction.
    TestTrue(TEXT("Different source duration does not gate the formal reload"),C->RequestReload());
    TestEqual(TEXT("Starting does not retime the authored source clip"),C->AnimationSets[0]->Clips[1].DurationSeconds,.7f);
    TestEqual(TEXT("Starting does not consume reserves"),F.Bag()->CountItem(Shell),1);
    F.Advance(.549f);TestEqual(TEXT("Formal deadline still gates the last shell"),F.Bag()->CountItem(Shell),1);
    F.Advance(.002f);TestEqual(TEXT("Formal completion moves exactly one shell"),C->AmmoInMagazine,2);
    TestEqual(TEXT("Source remains authored after completion"),C->AnimationSets[0]->Clips[1].DurationSeconds,.7f);
    FFixture Empty(0,0);if(!TestTrue(TEXT("Empty fixture"),Empty.bReady))return false;
    TestFalse(TEXT("No reserve refuses reload"),Empty.Component()->RequestReload());
    TestFalse(TEXT("No reserve and empty magazine refuses fire/auto reload"),Empty.Component()->PrimaryPressed(Empty.Target()));
    FFixture Full(6,2);if(!TestTrue(TEXT("Full fixture"),Full.bReady))return false;
    TestFalse(TEXT("Full magazine refuses reload"),Full.Component()->RequestReload());TestEqual(TEXT("Full refusal leaves reserves"),Full.Bag()->CountItem(Shell),2);
    FFixture Unowned(0,2,false);if(!TestTrue(TEXT("Unowned fixture"),Unowned.bReady))return false;
    TestFalse(TEXT("Animation asset alone is not ownership"),Unowned.Component()->SelectWeapon(EDWAnimatedWeapon::Shotgun));
    Unowned.Component()->SelectedWeapon=EDWAnimatedWeapon::Shotgun;
    TestFalse(TEXT("Direct legacy enum write cannot bypass ownership"),Unowned.Component()->RequestReload());
    TestEqual(TEXT("Unowned reserves not consumed"),Unowned.Bag()->CountItem(Shell),2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMagazineReceiptTest,"DoughWorld.InventoryTransactions.Reload.MagazineShotReceiptAndCooldown",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMagazineReceiptTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInventoryTests;
    FFixture F(2,3);if(!TestTrue(TEXT("Fixture"),F.bReady))return false;auto* C=F.Component();
    const int64 InventoryRevision=F.Bag()->GetInventoryRevision();
    TStrongObjectPtr<UDWWeaponResourceTestProbe> Probe(NewObject<UDWWeaponResourceTestProbe>());
    int32 Shots=0;
    Probe->TestHook=[&](int32 Loaded,int64 Revision,bool bReload)
    {
        TestFalse(TEXT("Typed shot result"),bReload);++Shots;
        TestEqual(TEXT("Only loaded ammo consumed"),Loaded,1);TestEqual(TEXT("Canonical state before notice"),C->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
        TestEqual(TEXT("Canonical revision before notice"),C->GetCombatWeaponResourceRevision(),Revision);
        const auto Receipt=UDWCombatAuthoritySubsystem::Get(F.Player)->FindReceipt(C->GetLastWeaponResourceReceipt());
        TestTrue(TEXT("Shot has committed WeaponUse receipt"),Receipt&&Receipt->State==DWCombatCommit::EReceiptState::Committed);
    };
    C->OnWeaponResourceChanged.AddDynamic(Probe.Get(),&UDWWeaponResourceTestProbe::OnResources);
    FDWShotgunConfig Shot;FString Error;if(!TestTrue(TEXT("Formal shot clock"),FDWShotgunConfig::Build(*F.Rules,Shot,Error)))return false;
    TestTrue(TEXT("Fire explicitly loaded shotgun"),C->PrimaryPressed(F.Target()));
    F.Advance(static_cast<float>(Shot.Windup+.001));
    TestEqual(TEXT("Two events for one shot consume one shell"),C->AmmoInMagazine,1);TestEqual(TEXT("One committed shot notification"),Shots,1);
    TestEqual(TEXT("Shot does not also consume reserve"),F.Bag()->CountItem(Shell),3);
    TestEqual(TEXT("Shot does not fake inventory revision"),F.Bag()->GetInventoryRevision(),InventoryRevision);
    C->CancelAction();
    TestFalse(TEXT("Cancel does not clear accepted attack cooldown"),C->PrimaryPressed(F.Target()));
    TestFalse(TEXT("Unrestricted PlayRole cannot create a shot"),C->PlayRole(TEXT("Attack"),F.Target()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWReloadCallbackCancelTest,"DoughWorld.InventoryTransactions.Reload.NotificationCancelDestroyAndMigration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWReloadCallbackCancelTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatInventoryTests;
    FFixture F(0,3);if(!TestTrue(TEXT("Fixture"),F.bReady))return false;auto* C=F.Component();
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    Probe->TestHook=[&](){C->CancelAction();};
    F.Bag()->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Start cancellation case"),C->RequestReload());F.Advance(2.f);
    TestEqual(TEXT("Callback cancellation stops carried future shells"),C->AmmoInMagazine,1);TestEqual(TEXT("No refund and no extra carried consumption"),F.Bag()->CountItem(Shell),2);
    TestFalse(TEXT("Cancelled playback not restarted by old continuation"),C->IsBusy());
    Probe->TestHook=[&](){F.Player->Destroy();};
    TestTrue(TEXT("Start destruction case"),C->RequestReload());F.Advance(.55f);
    TestTrue(TEXT("Listener actually destroyed owner"),F.Player->IsActorBeingDestroyed());
    TestEqual(TEXT("Committed shell is not rolled back after destruction"),C->GetCombatWeaponStateSnapshot().LoadedShotgunShells,2);
    FFixture Migration(0,0);if(!TestTrue(TEXT("Migration fixture"),Migration.bReady))return false;
    auto Owned=Migration.Component()->GetCombatWeaponStateSnapshot();
    Owned.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_SporeGun"));Owned.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_MoldBow"));
    FString Error;if(!TestTrue(TEXT("Explicit permanent ownership, no reserve grant"),Migration.Component()->RestoreCombatWeaponState(Owned,*Migration.Rules,Error)))return false;
    for(EDWAnimatedWeapon Kind:{EDWAnimatedWeapon::SporeGun,EDWAnimatedWeapon::MoldBow})
    {
        auto* Set=NewObject<UDWWeaponAnimationSet>(Migration.Player);Set->Weapon=Kind;Set->MagazineCapacity=8;
        Migration.AddClip(Set,TEXT("Attack"),.3f);Migration.Component()->AnimationSets.Add(Set);
        TestTrue(TEXT("Owned resource weapon selection remains available"),Migration.Component()->SelectWeapon(Kind));
        Migration.Advance(static_cast<float>(Migration.Component()->GetRemainingAttackLockSeconds()+.01));
        TestEqual(TEXT("Selection never fills a temporary magazine"),Migration.Component()->AmmoInMagazine,0);
        TestFalse(TEXT("Implemented reserve-backed projectile refuses free fire"),Migration.Component()->PrimaryPressed(Migration.Target()));
        TestFalse(TEXT("Resource projectile cannot bypass via PlayRole"),Migration.Component()->PlayRole(TEXT("Attack"),Migration.Target()));
    }
    return true;
}
#endif
