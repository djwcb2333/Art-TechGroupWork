#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatRules.h"
#include "DWCombatTestWorld.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWWeaponResourceTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipmentTestFixture.h"
#include "Animation/AnimSequence.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include <cmath>

namespace DWWeaponClockTests
{
const FName Shell(TEXT("DWAmmo_BreadShell")),Shotgun(TEXT("DWWeapon_Shotgun")),Dough(TEXT("Dough"));
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> InventoryProbe{NewObject<UDWInventoryTransactionTestProbe>()};
    TStrongObjectPtr<UDWWeaponResourceTestProbe> ResourceProbe{NewObject<UDWWeaponResourceTestProbe>()};
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;
    FDWCombatPlayerVitalsConfig Config;FString Error;float ReloadSeconds=0.f;bool bReady=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D12WeaponClockBoundary"));if(!World)return;
        Player=Spawn<ADWPlayerCharacter>();if(!Player||!Player->HasCombatVitalsReady())return;
        bReady=FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),Config,Error)&&!GI->HasActiveSlot();
    }
    ~FFixture()
    {
        InventoryProbe->TestHook=nullptr;ResourceProbe->TestHook=nullptr;
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T> T* Spawn()
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<T>(T::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,P);
        if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    UDWWeaponAnimationComponent* W()const{return Player->WeaponAnimationComponent.Get();}
    UDWInventoryComponent* Bag()const{return Player->GetInventory();}
    FVector Target()const{return Player->GetActorLocation()+FVector(400,0,0);}
    void AddClip(UDWWeaponAnimationSet* Set,FName Role,float Duration)
    {FDWWeaponAnimationClip Clip;Clip.Role=Role;Clip.DurationSeconds=Duration;Clip.Animation=NewObject<UAnimSequence>(Set);Set->Clips.Add(Clip);}
    bool ShotgunSetup(int32 Stock)
    {
        double Reload=0.;if(!bReady||!GI->GetCombatRules()->TryGetNumber(TEXT("weapon.shotgun.reload_one_s"),EDWCombatRuleUnit::Seconds,Reload,Error))return false;
        ReloadSeconds=static_cast<float>(Reload);if(!Bag()->TryAddItem(Shell,Stock))return false;
        auto* Set=NewObject<UDWWeaponAnimationSet>(Player);Set->Weapon=EDWAnimatedWeapon::Shotgun;
        Set->bApplyBasicDamage=false;AddClip(Set,TEXT("Attack"),.3f);AddClip(Set,TEXT("ReloadOne"),ReloadSeconds);
        W()->AnimationSets={Set};FDWCombatWeaponResourceSnapshot Snapshot;Snapshot.OwnedPermanentWeaponIds.Add(Shotgun);Snapshot.CurrentEquipmentId=Shotgun;
        return W()->RestoreCombatWeaponState(Snapshot,*GI->GetCombatRules(),Error)&&W()->SelectWeapon(EDWAnimatedWeapon::Shotgun);
    }
    bool LegacySetup(float Duration,float EventTime,bool Repeat,float Interval)
    {
        if(!bReady)return false;auto* Set=NewObject<UDWWeaponAnimationSet>(Player);
        Set->Weapon=EDWAnimatedWeapon::Spear;Set->bApplyBasicDamage=false;Set->bRepeatWhileHeld=Repeat;Set->ChargeSeconds=0.f;
        Set->AttackIntervalSeconds=Interval;AddClip(Set,TEXT("Attack"),Duration);
        FDWWeaponDamageEvent Event;Event.TimeSeconds=EventTime;Set->Clips[0].DamageEvents={Event};W()->AnimationSets={Set};
        // Own and select the synthetic event clock through the strict native snapshot contract.
        // No formal spear binding/damage is exercised by these source-clock tests.
        if(!DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*W(),*GI->GetCombatRules(),EDWAnimatedWeapon::Spear,Error))return false;
        return W()->SelectWeapon(EDWAnimatedWeapon::Spear)&&W()->PrimaryPressed(Target());
    }
    void AdvancePlayer(float Seconds){World->TimeSeconds+=Seconds;Player->TickGameplayClocks(Seconds,false);}
    bool AdvanceDirect(double Seconds){World->TimeSeconds+=Seconds;return W()->AdvanceAnimation(Seconds);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponReloadPauseBoundaryTest,"DoughWorld.Combat.WeaponClock.ReloadNotificationPauseResumesWithoutRepayment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponReloadPauseBoundaryTest::RunTest(const FString& Parameters)
{
    using namespace DWWeaponClockTests;FFixture F;if(!TestTrue(TEXT("Isolated owned shotgun with two reserves"),F.ShotgunSetup(2)))return false;
    auto* PauseOwner=F.Spawn<APlayerState>();if(!TestNotNull(TEXT("Pause owner"),PauseOwner))return false;
    int32 InventoryNotices=0;TArray<int32> LoadedNotices;TArray<uint64> ReceiptIds;
    F.InventoryProbe->TestHook=[&]()
    {
        ++InventoryNotices;ReceiptIds.Add(F.W()->GetLastWeaponResourceReceipt().Id);
        TestEqual(TEXT("Committed facts visible before actual dynamic callback"),F.W()->AmmoInMagazine,InventoryNotices);
        if(InventoryNotices==1)F.World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);
    };
    F.ResourceProbe->TestHook=[&](int32 Loaded,int64,bool bReload){TestTrue(TEXT("Reload notification"),bReload);LoadedNotices.Add(Loaded);};
    F.Bag()->OnChanged.AddDynamic(F.InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.W()->OnWeaponResourceChanged.AddDynamic(F.ResourceProbe.Get(),&UDWWeaponResourceTestProbe::OnResources);
    TestTrue(TEXT("Begin reload"),F.W()->RequestReload());F.AdvancePlayer(F.ReloadSeconds*2.f);
    TestEqual(TEXT("Pause stops after exactly one shell"),F.W()->AmmoInMagazine,1);TestEqual(TEXT("One reserve remains"),F.Bag()->CountItem(Shell),1);
    TestEqual(TEXT("First shell published once"),InventoryNotices,1);TestTrue(TEXT("Completed shell awaits only transition"),F.W()->IsBusy());
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    TestEqual(TEXT("Transition is due without consuming resource time"),F.W()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds()),0.);
    const uint64 Before=F.W()->GetCombatBoundaryRevision();F.Player->TickGameplayClocks(0.f,false);
    TestTrue(TEXT("Zero boundary makes progress"),F.W()->GetCombatBoundaryRevision()>Before);
    TestEqual(TEXT("Resume transition never pays completed shell again"),InventoryNotices,1);TestEqual(TEXT("Next shell starts at zero"),F.W()->GetElapsedSeconds(),0.f);
    F.AdvancePlayer(F.ReloadSeconds);
    TestEqual(TEXT("Exactly two paid shells"),F.W()->AmmoInMagazine,2);TestEqual(TEXT("Reserves conserved"),F.Bag()->CountItem(Shell),0);
    TestEqual(TEXT("Two real inventory notifications"),InventoryNotices,2);TestEqual(TEXT("Two typed snapshots"),LoadedNotices.Num(),2);
    if(LoadedNotices.Num()==2){TestEqual(TEXT("First snapshot"),LoadedNotices[0],1);TestEqual(TEXT("Second snapshot"),LoadedNotices[1],2);}
    TestTrue(TEXT("Distinct completed shells have distinct receipts"),ReceiptIds.Num()==2&&ReceiptIds[0]!=0&&ReceiptIds[1]!=ReceiptIds[0]);
    TestFalse(TEXT("No stock ends reload"),F.W()->IsBusy());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponTypedResourceFIFOTest,"DoughWorld.Combat.WeaponClock.ReloadListenerHealingUsesSharedTypedFIFO",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponTypedResourceFIFOTest::RunTest(const FString& Parameters)
{
    using namespace DWWeaponClockTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.ShotgunSetup(1)))return false;
    const auto* Definition=F.Player->GameplayConfig->GetItemDefinition(Dough);
    if(!TestTrue(TEXT("Existing configured usable Dough"),Definition&&Definition->bUsable&&Definition->HealAmount>0.f))return false;
    if(!TestTrue(TEXT("Reserve one real Dough"),F.Bag()->TryAddItem(Dough,1)))return false;
    const float Before=DWCombatPlayerSave::NativeHealth((F.Config.Vitals.HealthFloor+F.Config.Vitals.HealthMax)*.5,F.Config);
    const float Expected=DWCombatPlayerSave::NativeHealth(static_cast<double>(Before)+Definition->HealAmount,F.Config);
    if(!TestTrue(TEXT("Fixture has room for positive healing"),Expected>Before))return false;F.Player->Health=Before;
    int32 Calls=0,Depth=0,MaxDepth=0;TArray<FName> Order;
    F.InventoryProbe->TestHook=[&]()
    {
        ++Depth;MaxDepth=FMath::Max(MaxDepth,Depth);++Calls;
        Order.Add(Calls==1?FName(TEXT("ReloadInventory")):FName(TEXT("HealInventory")));
        if(Calls==1)
        {
            TestEqual(TEXT("Outer shell fact is final"),F.W()->AmmoInMagazine,1);
            const int32 Index=F.Bag()->GetSlots().IndexOfByPredicate([](const FDWItemStack& Slot){return Slot.ItemId==Dough&&Slot.Quantity>0;});
            TestTrue(TEXT("Distinct real healing transaction is accepted during reload notification"),Index!=INDEX_NONE&&F.Player->UseInventoryItem(Index));
            TestEqual(TEXT("Nested typed heal facts are synchronous"),F.Player->Health,Expected);
            TestEqual(TEXT("Nested item adoption is synchronous"),F.Bag()->CountItem(Dough),0);
            TestEqual(TEXT("Nested notice waits in the authority FIFO"),Calls,1);
        }
        else TestEqual(TEXT("Queued heal listener sees committed health"),F.Player->Health,Expected);
        --Depth;
    };
    F.ResourceProbe->TestHook=[&](int32 Loaded,int64,bool bReload)
    {TestTrue(TEXT("Outer typed weapon snapshot remains reload"),bReload);TestEqual(TEXT("Outer typed loaded snapshot"),Loaded,1);Order.Add(TEXT("ReloadWeapon"));};
    F.Bag()->OnChanged.AddDynamic(F.InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.W()->OnWeaponResourceChanged.AddDynamic(F.ResourceProbe.Get(),&UDWWeaponResourceTestProbe::OnResources);
    TestTrue(TEXT("Begin reload"),F.W()->RequestReload());F.AdvancePlayer(F.ReloadSeconds);
    TestEqual(TEXT("Two actual inventory notices"),Calls,2);TestEqual(TEXT("Delegates never recursively publish"),MaxDepth,1);
    TestEqual(TEXT("All expected notices were observed"),Order.Num(),3);
    if(Order.Num()==3){TestEqual(TEXT("Outer inventory first"),Order[0],FName(TEXT("ReloadInventory")));TestEqual(TEXT("Outer typed snapshot second"),Order[1],FName(TEXT("ReloadWeapon")));TestEqual(TEXT("Nested healing notice last"),Order[2],FName(TEXT("HealInventory")));}
    TestEqual(TEXT("Exactly one reserve spent"),F.Bag()->CountItem(Shell),0);TestEqual(TEXT("Exactly one loaded shell"),F.W()->AmmoInMagazine,1);
    TestEqual(TEXT("Exactly one Dough consumed"),F.Bag()->CountItem(Dough),0);TestEqual(TEXT("Healing remains committed"),F.Player->Health,Expected);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponSubFloatBoundaryTest,"DoughWorld.Combat.WeaponClock.SubFloatSliceNeverFiresSourceEventEarly",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponSubFloatBoundaryTest::RunTest(const FString& Parameters)
{
    using namespace DWWeaponClockTests;FFixture F;if(!TestTrue(TEXT("Synthetic source fixture"),F.LegacySetup(1.f,.5f,false,1.f)))return false;
    const double Gap=std::ldexp(1.,-30),Before=.5-Gap;
    TestEqual(TEXT("Fixture exposes float upward rounding"),static_cast<float>(Before),.5f);
    TestTrue(TEXT("Advance to true instant before event"),F.AdvanceDirect(Before));TestEqual(TEXT("No future event"),F.W()->DispatchedEventCount,0);
    TestEqual(TEXT("Next boundary retains sub-float gap"),F.W()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds()),Gap);
    TestTrue(TEXT("Advance remaining exact gap"),F.AdvanceDirect(Gap));TestEqual(TEXT("Event fires once at actual boundary"),F.W()->DispatchedEventCount,1);
    TestTrue(TEXT("Zero retry succeeds"),F.W()->AdvanceAnimation(0.));TestEqual(TEXT("No event replay"),F.W()->DispatchedEventCount,1);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponRepeatSliceTimeTest,"DoughWorld.Combat.WeaponClock.LegacyRepeatUsesSliceEndInsteadOfWorldFrameEnd",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponRepeatSliceTimeTest::RunTest(const FString& Parameters)
{
    using namespace DWWeaponClockTests;FFixture F;if(!TestTrue(TEXT("Synthetic held repeat fixture"),F.LegacySetup(.25f,.125f,true,.5f)))return false;
    F.AdvancePlayer(1.375f);
    TestEqual(TEXT("Only events at .125, .625 and 1.125 in this frame"),F.W()->DispatchedEventCount,3);
    TestEqual(TEXT("Third clip waits at terminal pose"),F.W()->GetElapsedSeconds(),.25f);
    TestEqual(TEXT("Next repeat begins at 1.5, no frame-end carry duplication"),F.W()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds()),.125);
    return true;
}
#endif
