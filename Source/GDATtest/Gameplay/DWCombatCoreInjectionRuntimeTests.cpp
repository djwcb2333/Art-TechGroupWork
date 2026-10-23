#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatCoreInjection.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatRules.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWCombatRecoveryActors.h"
#include "DWCombatTestWorld.h"
#include "DWCombatItemRegistry.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipmentTestFixture.h"
#include "DWSaveGame.h"
#include "Animation/AnimSequence.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include <cmath>
#include <limits>

namespace DWCoreInjectionRuntimeTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> InventoryProbe{NewObject<UDWInventoryTransactionTestProbe>()};
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> DamageProbe{NewObject<UDWCombatAuthorityTestProbe>()};
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;UDWCombatAuthoritySubsystem* Authority=nullptr;
    FDWCoreInjectionConfig Config;FString Error;bool bReady=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D08CoreInjectionWorld"));if(!World)return;
        Player=Spawn<ADWPlayerCharacter>();if(!Player||!Player->HasCombatVitalsReady())return;
        Authority=UDWCombatAuthoritySubsystem::Get(Player);
        if(!Authority||!FDWCoreInjectionConfig::Build(*GI->GetCombatRules(),Config,Error))return;
        Player->WeaponAnimationComponent->bEnabled=false;
        auto* S=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error))return;
        S->CombatPlayerResources.Vitals.Water=Config.Vitals.Vitals.WaterMax;
        S->CombatPlayerResources.Vitals.Satiety=Config.Vitals.Vitals.SatietyMax;
        for(FName Core:{FDWCombatItemRegistry::MoldCoreId(),FDWCombatItemRegistry::YeastCoreId()})
        {FDWItemStack Stack;Stack.ItemId=Core;Stack.Quantity=Config.CoreCost*2;S->Inventory.Add(Stack);}
        bReady=Player->TryApplySaveData(S)&&!GI->HasActiveSlot();
        Player->Inventory->OnChanged.AddDynamic(InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
        Player->OnTakeAnyDamage.AddDynamic(DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
    }
    ~FFixture()
    {
        InventoryProbe->TestHook=nullptr;DamageProbe->AnyCallback=nullptr;
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
    int32 Slot(FName Id)const
    {const auto& Slots=Player->Inventory->GetSlots();for(int32 I=0;I<Slots.Num();++I)if(Slots[I].ItemId==Id)return I;return INDEX_NONE;}
    bool Start(FName Id){return Player->UseInventoryItem(Slot(Id));}
    void Advance(float Dt){World->TimeSeconds+=Dt;Player->TickGameplayClocks(Dt,false);}
    void Finish(){Advance(static_cast<float>(Config.DurationSeconds)+.001f);}
    UDWSaveGame* Capture(){auto* S=NewObject<UDWSaveGame>();Player->CaptureSaveData(S);return S;}
    bool LegacyAttack(float Duration)
    {
        auto* W=Player->WeaponAnimationComponent.Get();W->bEnabled=true;
        auto* Set=NewObject<UDWWeaponAnimationSet>(Player);Set->Weapon=EDWAnimatedWeapon::Spear;
        Set->bApplyBasicDamage=false;Set->bRepeatWhileHeld=false;Set->ChargeSeconds=0.f;
        FDWWeaponAnimationClip Clip;Clip.Role=TEXT("Attack");Clip.DurationSeconds=Duration;Clip.Animation=NewObject<UAnimSequence>(Set);
        Set->Clips={Clip};W->AnimationSets={Set};
        // Synthetic no-damage presentation clock; not a formal spear gameplay assertion.
        if(!DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*W,*GI->GetCombatRules(),EDWAnimatedWeapon::Spear,Error))return false;
        return W->SelectWeapon(EDWAnimatedWeapon::Spear)&&W->PrimaryPressed(Player->GetActorLocation()+FVector(500,0,0));
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionAtomicRuntimeTest,"DoughWorld.Combat.CoreInjectionRuntime.CoreFormSpecialBeforeDynamicNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionAtomicRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Isolated normal player and core stock"),F.bReady))return false;
    const FName Core=FDWCombatItemRegistry::YeastCoreId();const int32 Before=F.Player->Inventory->CountItem(Core);int32 Calls=0;
    F.InventoryProbe->TestHook=[&]()
    {
        ++Calls;auto* S=F.Capture();
        TestEqual(TEXT("Core deducted before actual dynamic notification"),F.Player->Inventory->CountItem(Core),Before-F.Config.CoreCost);
        TestTrue(TEXT("Form already committed"),S->CombatPlayerResources.Vitals.BaseForm==EDWCombatBaseForm::Yeast);
        TestEqual(TEXT("Full independent form duration at commit"),S->CombatPlayerResources.Vitals.FormRemainingSeconds,F.Config.Vitals.FormDurationSeconds);
        TestEqual(TEXT("Initial special at commit"),S->CombatPlayerResources.Vitals.Special,F.Config.Vitals.InitialFormSpecial);
        TestFalse(TEXT("No longer injecting"),F.Player->IsCoreInjecting());
        TestFalse(TEXT("Reentrant second core rejected"),F.Start(FDWCombatItemRegistry::MoldCoreId()));
        TestFalse(TEXT("Public authority cannot replay private completion boundary"),F.Authority->CommitCoreInjection(F.Player));
        F.Player->TickGameplayClocks(10.f,false);
        TestEqual(TEXT("Recursive clock cannot consume future form time"),F.Player->GetCombatFormRemaining(),F.Config.Vitals.FormDurationSeconds);
    };
    TestTrue(TEXT("Use starts injection only"),F.Start(Core));
    TestNull(TEXT("Missing injection art stays an explicit empty slot"),F.Player->GetCoreInjectionAnimationForPlayback());
    F.Advance(static_cast<float>(F.Config.DurationSeconds*.5));
    TestTrue(TEXT("Before completion still normal"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);
    TestEqual(TEXT("No upfront charge"),F.Player->Inventory->CountItem(Core),Before);TestEqual(TEXT("No upfront notification"),Calls,0);
    F.Finish();TestEqual(TEXT("One dynamic commit notice"),Calls,1);
    const double Remaining=F.Player->GetCombatFormRemaining();const double Special=F.Player->GetCombatSpecial();
    TestFalse(TEXT("Transformed use cannot refresh or change form"),F.Start(FDWCombatItemRegistry::MoldCoreId()));
    TestEqual(TEXT("No duration refresh"),F.Player->GetCombatFormRemaining(),Remaining);TestEqual(TEXT("No special refill"),F.Player->GetCombatSpecial(),Special);
    TestEqual(TEXT("No duplicate core charge"),F.Player->Inventory->CountItem(Core),Before-F.Config.CoreCost);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionCancellationRuntimeTest,"DoughWorld.Combat.CoreInjectionRuntime.MovementAttackDamageAndStaleStockCancelFree",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionCancellationRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const FName Core=FDWCombatItemRegistry::MoldCoreId();const int32 Before=F.Player->Inventory->CountItem(Core);
    TestTrue(TEXT("Movement request starts"),F.Start(Core));F.Advance(.1f);F.Player->MoveCameraRelative(1.f,0.f);
    TestFalse(TEXT("Movement cancels immediately"),F.Player->IsCoreInjecting());F.Finish();
    TestEqual(TEXT("Movement never costs a core"),F.Player->Inventory->CountItem(Core),Before);
    TestTrue(TEXT("Attack request starts"),F.Start(Core));TestTrue(TEXT("Accepted synthetic presentation action"),F.LegacyAttack(1.f));
    TestFalse(TEXT("Accepted attack cancels injection"),F.Player->IsCoreInjecting());F.Player->CancelCombatActions();F.Player->WeaponAnimationComponent->bEnabled=false;
    TestTrue(TEXT("Damage request starts"),F.Start(Core));FDamageEvent E;
    TestEqual(TEXT("Zero damage has no interruption"),F.Player->TakeDamage(0.f,E,nullptr,nullptr),0.f);
    TestTrue(TEXT("Zero damage preserves injection"),F.Player->IsCoreInjecting());int32 DamageCalls=0;
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*){++DamageCalls;TestFalse(TEXT("Cancellation fact precedes damage delegate"),F.Player->IsCoreInjecting());};
    TestTrue(TEXT("Positive actual damage"),F.Player->TakeDamage(1.f,E,nullptr,nullptr)>0.f);TestEqual(TEXT("Actual damage callback ran"),DamageCalls,1);
    TestTrue(TEXT("Stale stock request starts"),F.Start(Core));TestTrue(TEXT("External stock revision changes"),F.Player->Inventory->TryAddItem(FDWCombatItemRegistry::YeastCoreId(),1));
    F.Finish();TestFalse(TEXT("Stale slot token cannot commit"),F.Player->IsCoreInjecting());
    TestTrue(TEXT("All cancelled requests remain normal"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);
    TestEqual(TEXT("No aborted request charges core"),F.Player->Inventory->CountItem(Core),Before);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionPauseRuntimeTest,"DoughWorld.Combat.CoreInjectionRuntime.PauseFreezesAndCommittedPauseDoesNotRepay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionPauseRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    auto* PauseOwner=F.Spawn<APlayerState>();if(!PauseOwner)return false;const FName Core=FDWCombatItemRegistry::YeastCoreId();
    TestTrue(TEXT("Start"),F.Start(Core));F.Advance(.1f);const float Remaining=F.Player->GetCoreInjectionRemainingSeconds();
    F.World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);F.Player->TickGameplayClocks(10.f,false);
    TestEqual(TEXT("Pause keeps injection clock"),F.Player->GetCoreInjectionRemainingSeconds(),Remaining);
    TestEqual(TEXT("Paused use does not consume core"),F.Player->Inventory->CountItem(Core),F.Config.CoreCost*2);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);int32 Calls=0;
    F.InventoryProbe->TestHook=[&](){++Calls;F.World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);};
    F.Finish();TestEqual(TEXT("Pause in committed inventory notice"),Calls,1);
    TestTrue(TEXT("Committed form survives notification pause"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Yeast);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.Player->TickGameplayClocks(0.f,false);
    TestEqual(TEXT("Resume never repeats completed charge"),F.Player->Inventory->CountItem(Core),F.Config.CoreCost);
    TestEqual(TEXT("Resume never repeats commit notice"),Calls,1);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionRestoreRuntimeTest,"DoughWorld.Combat.CoreInjectionRuntime.RestoreCancelsPendingAndOldLifeCannotFinish",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionRestoreRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const FName Core=FDWCombatItemRegistry::MoldCoreId();TestTrue(TEXT("Start"),F.Start(Core));F.Advance(.1f);
    TStrongObjectPtr<UDWSaveGame> S(F.Capture());const auto Old=F.Authority->CaptureActorLife(F.Player);
    TestTrue(TEXT("Pending snapshot remains normal"),S->CombatPlayerResources.Vitals.BaseForm==EDWCombatBaseForm::Normal);
    TestTrue(TEXT("Restore strict snapshot"),F.Player->TryApplySaveData(S.Get()));
    TestTrue(TEXT("Restore changes life"),F.Authority->CaptureActorLife(F.Player).Life>Old.Life);
    TestFalse(TEXT("Unfinished action never serialized/resumed"),F.Player->IsCoreInjecting());F.Finish();
    TestEqual(TEXT("Old elapsed cannot charge after restore"),F.Player->Inventory->CountItem(Core),F.Config.CoreCost*2);
    int32 Calls=0;F.InventoryProbe->TestHook=[&](){++Calls;if(Calls==1)TestTrue(TEXT("Restore inside real commit notification"),F.Player->TryApplySaveData(S.Get()));};
    TestTrue(TEXT("New request"),F.Start(Core));F.Finish();
    TestEqual(TEXT("Core then restore notices are FIFO"),Calls,2);TestTrue(TEXT("Restored normal wins over stale form publication"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);
    TestEqual(TEXT("Restored stock wins"),F.Player->Inventory->CountItem(Core),F.Config.CoreCost*2);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionHC05RuntimeTest,"DoughWorld.Combat.CoreInjectionRuntime.SameInstantHungerThirstCancelBeforeCoreCommit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionHC05RuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    auto* S=F.Capture();auto& V=S->CombatPlayerResources.Vitals;const auto& C=F.Config.Vitals.Vitals;const double D=F.Config.DurationSeconds;
    if(!TestTrue(TEXT("Fixture can align resource periods"),D<C.WaterDamagePeriodSeconds&&D<C.SatietyDamagePeriodSeconds))return false;
    V.Water=C.WaterFloor;V.Satiety=C.SatietyFloor;
    V.WaterEmptyElapsedSeconds=C.WaterDamagePeriodSeconds-D;V.SatietyEmptyElapsedSeconds=C.SatietyDamagePeriodSeconds-D;
    V.WaterEmptyTimeCarry=D-(C.WaterDamagePeriodSeconds-V.WaterEmptyElapsedSeconds);
    V.SatietyEmptyTimeCarry=D-(C.SatietyDamagePeriodSeconds-V.SatietyEmptyElapsedSeconds);
    if(!TestTrue(TEXT("Set exact same-instant resource boundary"),F.Player->TryApplySaveData(S)))return false;
    int32 Calls=0;F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {++Calls;TestFalse(TEXT("Ready cancelled before damage callback"),F.Player->IsCoreInjecting());TestTrue(TEXT("No premature form grant"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);};
    const FName Core=FDWCombatItemRegistry::YeastCoreId();TestTrue(TEXT("Start"),F.Start(Core));F.Finish();
    TestEqual(TEXT("Both true damage callbacks"),Calls,2);TestEqual(TEXT("No completion cost after positive damage"),F.Player->Inventory->CountItem(Core),F.Config.CoreCost*2);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionExpiryRuntimeTest,"DoughWorld.Combat.CoreInjectionRuntime.ExpiryClearsSpecialPreservesOrdinaryWeaponAndCommittedSave",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionExpiryRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const FName Core=FDWCombatItemRegistry::MoldCoreId();TestTrue(TEXT("Start"),F.Start(Core));F.Finish();
    TStrongObjectPtr<UDWSaveGame> Committed(F.Capture());
    TestTrue(TEXT("Committed form saved"),Committed->CombatPlayerResources.Vitals.BaseForm==EDWCombatBaseForm::Mold);
    TestTrue(TEXT("Committed snapshot restores exactly once"),F.Player->TryApplySaveData(Committed.Get()));
    TestEqual(TEXT("Restore cannot repay core"),F.Player->Inventory->CountItem(Core),F.Config.CoreCost);
    TestTrue(TEXT("Ordinary synthetic attack starts"),F.LegacyAttack(static_cast<float>(F.Config.Vitals.FormDurationSeconds*3.)));
    F.Advance(static_cast<float>(F.Player->GetCombatFormRemaining())+.001f);
    TestTrue(TEXT("Independent time expires form"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);
    TestEqual(TEXT("Normal clears Special"),F.Player->GetCombatSpecial(),F.Config.Vitals.Vitals.NormalSpecialValue);
    TestTrue(TEXT("Expiry preserves ordinary attack"),F.Player->WeaponAnimationComponent->IsBusy());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionNestedFIFOTest,"DoughWorld.Combat.CoreInjectionRuntime.CoreNoticeNestedHealSharesTypedFIFO",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionNestedFIFOTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const FName Dough(TEXT("Dough"));const auto* Def=F.Player->GameplayConfig->GetItemDefinition(Dough);
    if(!TestTrue(TEXT("Existing heal item"),Def&&Def->bUsable&&Def->HealAmount>0.f)||!F.Player->Inventory->TryAddItem(Dough,1))return false;
    FDamageEvent E;F.Player->TakeDamage(F.Player->Health*.5f,E,nullptr,nullptr);const float Before=F.Player->Health;
    int32 Depth=0,MaxDepth=0,Calls=0;F.InventoryProbe->TestHook=[&]()
    {
        ++Depth;MaxDepth=FMath::Max(MaxDepth,Depth);++Calls;
        if(Calls==1)
        {
            TestTrue(TEXT("Actual second typed resource transaction"),F.Player->UseInventoryItem(F.Slot(Dough)));
            TestTrue(TEXT("Nested healing facts immediately visible"),F.Player->Health>Before);
            TestEqual(TEXT("Second notice queued, not recursively broadcast"),Calls,1);
        }
        --Depth;
    };
    TestTrue(TEXT("Start core"),F.Start(FDWCombatItemRegistry::YeastCoreId()));F.Finish();
    TestEqual(TEXT("Both committed resources publish"),Calls,2);TestEqual(TEXT("Single FIFO callback depth"),MaxDepth,1);
    TestEqual(TEXT("One heal consumed"),F.Player->Inventory->CountItem(Dough),0);TestTrue(TEXT("Healing does not undo committed form"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Yeast);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionBlockReloadTest,"DoughWorld.Combat.CoreInjectionRuntime.AcceptedBlockAndReloadCancelBeforeTheirOwnClocks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionBlockReloadTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    auto* W=F.Player->WeaponAnimationComponent.Get();W->bEnabled=true;
    const auto AddClip=[](UDWWeaponAnimationSet* Set,FName Role,float Duration)
    {FDWWeaponAnimationClip C;C.Role=Role;C.DurationSeconds=Duration;C.Animation=NewObject<UAnimSequence>(Set);Set->Clips.Add(C);};
    auto* Shield=NewObject<UDWWeaponAnimationSet>(F.Player);Shield->Weapon=EDWAnimatedWeapon::ShieldAxe;
    Shield->bApplyBasicDamage=false;AddClip(Shield,TEXT("Attack"),1.f);AddClip(Shield,TEXT("BlockEnter"),.2f);AddClip(Shield,TEXT("BlockHold"),.3f);
    W->AnimationSets={Shield};
    if(!TestTrue(TEXT("Strict explicitly owned/current shield fixture"),DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*W,*F.GI->GetCombatRules(),EDWAnimatedWeapon::ShieldAxe,F.Error)))return false;
    TestTrue(TEXT("Existing shield presentation"),W->SelectWeapon(EDWAnimatedWeapon::ShieldAxe));
    TestTrue(TEXT("Start core before block"),F.Start(FDWCombatItemRegistry::MoldCoreId()));
    TestTrue(TEXT("Accepted block"),W->SecondaryPressed());TestFalse(TEXT("Block cancels injection"),F.Player->IsCoreInjecting());
    F.Player->CancelCombatActions();double Reload=0.;
    if(!F.GI->GetCombatRules()->TryGetNumber(TEXT("weapon.shotgun.reload_one_s"),EDWCombatRuleUnit::Seconds,Reload,F.Error))return false;
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);if(!Binding)return false;
    auto* Shotgun=NewObject<UDWWeaponAnimationSet>(F.Player);Shotgun->Weapon=EDWAnimatedWeapon::Shotgun;Shotgun->bApplyBasicDamage=false;
    AddClip(Shotgun,TEXT("Attack"),1.f);AddClip(Shotgun,TEXT("ReloadOne"),static_cast<float>(Reload));W->AnimationSets={Shotgun};
    FDWCombatWeaponResourceSnapshot Resources;Resources.OwnedPermanentWeaponIds.Add(Binding->ItemId);Resources.CurrentEquipmentId=Binding->ItemId;
    if(!TestTrue(TEXT("Own shotgun without free shells"),W->RestoreCombatWeaponState(Resources,*F.GI->GetCombatRules(),F.Error))||
        !TestTrue(TEXT("Reserve shell"),F.Player->Inventory->TryAddItem(Binding->AmmoItemId,1))||
        !TestTrue(TEXT("Select owned shotgun"),W->SelectWeapon(EDWAnimatedWeapon::Shotgun)))return false;
    TestTrue(TEXT("Start core before reload"),F.Start(FDWCombatItemRegistry::MoldCoreId()));
    TestTrue(TEXT("Accepted reload"),W->RequestReload());TestFalse(TEXT("Reload cancels injection"),F.Player->IsCoreInjecting());
    TestEqual(TEXT("Both cancellations leave core stock"),F.Player->Inventory->CountItem(FDWCombatItemRegistry::MoldCoreId()),F.Config.CoreCost*2);
    TestEqual(TEXT("Reload still obeys its own delayed shell payment"),W->AmmoInMagazine,0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionDeathTest,"DoughWorld.Combat.CoreInjectionRuntime.DeathCancelsBeforeNoticeAndCommittedFormEnds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionDeathTest::RunTest(const FString& Parameters)
{
    using namespace DWCoreInjectionRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const FName Core=FDWCombatItemRegistry::YeastCoreId();TStrongObjectPtr<UDWSaveGame> Before(F.Capture());
    // D21 now moves unused stock into a bag. Activate an explicit isolated point;
    // restoring an old player-only snapshot must not erase these world facts.
    auto* Safe=F.Spawn<ADWCombatSafePoint>();auto* Recovery=UDWCombatRecoverySubsystem::Get(F.Player);
    if(!Safe||!Recovery)return false;
    Safe->SetActorLocation(F.Player->GetActorLocation());Safe->StablePointId=FGuid::NewGuid();Safe->ActivationRadiusCm=150.;
    if(!TestTrue(TEXT("Explicit isolated safe point"),Safe->ActivateForPlayer(F.Player)))return false;
    auto CheckBagCore=[&](int32 Expected,FGuid* OutId=nullptr)
    {
        FDWCombatRecoverySave State;
        if(!TestTrue(TEXT("Capture actual committed recovery facts"),Recovery->CaptureSave(State,F.Error)))return false;
        FDWDeathBagSave Bag;
        if(!TestTrue(TEXT("Current death bag exists"),Recovery->FindBag(State.CurrentDeathBagId,Bag)))return false;
        int32 Original=0,Remaining=0;
        for(const auto& Stack:Bag.Contents)if(Stack.ItemId==Core){Original+=Stack.Original;Remaining+=Stack.Remaining;}
        TestEqual(TEXT("Bag preserves exactly the unspent core quantity"),Original,Expected);
        TestEqual(TEXT("New bag has not silently consumed a core"),Remaining,Expected);
        if(OutId)*OutId=Bag.BagId;
        return Original==Expected&&Remaining==Expected;
    };
    TestTrue(TEXT("Start unfinished core"),F.Start(Core));F.Advance(.1f);int32 Calls=0;
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {++Calls;TestTrue(TEXT("Death facts first"),F.Player->IsDead());TestFalse(TEXT("No pending injection in death observer"),F.Player->IsCoreInjecting());
     TestTrue(TEXT("Death already normal"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);TestEqual(TEXT("Death clears special"),F.Player->GetCombatSpecial(),F.Config.Vitals.Vitals.NormalSpecialValue);};
    FDamageEvent E;TestTrue(TEXT("Fatal actual damage"),F.Player->TakeDamage(MAX_flt,E,nullptr,nullptr)>0.f);
    TestEqual(TEXT("Fatal dynamic delegate ran"),Calls,1);
    TestEqual(TEXT("Unused cores leave inventory for the committed death bag"),F.Player->Inventory->CountItem(Core),0);
    FGuid FirstBag;if(!CheckBagCore(F.Config.CoreCost*2,&FirstBag))return false;
    TestFalse(TEXT("Player-only restore cannot erase recovery facts"),F.Player->TryApplySaveData(Before.Get()));
    const auto DeadLife=F.Authority->CaptureActorLife(F.Player);
    if(!TestTrue(TEXT("Fresh life through controlled safe-point respawn"),F.Player->RequestCombatRespawn()))return false;
    TestTrue(TEXT("Respawn advances the death life"),F.Authority->CaptureActorLife(F.Player).Life>DeadLife.Life);
    Recovery->Tick(0.f);auto* PhysicalBag=Recovery->FindPhysicalBag(FirstBag);
    if(!TestNotNull(TEXT("Materialized original bag"),PhysicalBag)||
       !TestTrue(TEXT("Real capacity-checked collection restores unspent cores"),PhysicalBag->TryCollect(F.Player)))return false;
    TestEqual(TEXT("Cancelled injection did not charge before or after collection"),F.Player->Inventory->CountItem(Core),F.Config.CoreCost*2);
    TestTrue(TEXT("Complete new life injection"),F.Start(Core));F.Finish();
    TestTrue(TEXT("Form really committed before second death"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Yeast);
    const float HealthBeforeOverflow=F.Player->Health;
    TestEqual(TEXT("Overflow after Yeast multiplier is rejected"),F.Player->TakeDamage(MAX_flt,E,nullptr,nullptr),0.f);
    TestEqual(TEXT("Rejected overflow preserves health"),F.Player->Health,HealthBeforeOverflow);
    TestEqual(TEXT("Rejected overflow emits no death notice"),Calls,1);
    TestTrue(TEXT("Rejected overflow preserves committed form"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Yeast);
    const double Multiplier=F.Config.Vitals.YeastIncomingMultiplier;
    if(!TestTrue(TEXT("Positive finite incoming multiplier"),FMath::IsFinite(Multiplier)&&Multiplier>0.))return false;
    const double FiniteFatal=(static_cast<double>(F.Player->Health)-DWCombatPlayerSave::HealthFloor(F.Config.Vitals)+1.)*2./Multiplier;
    if(!TestTrue(TEXT("Representable finite fatal fixture"),FMath::IsFinite(FiniteFatal)&&FiniteFatal>0.&&FiniteFatal<=MAX_flt&&FiniteFatal*Multiplier<=MAX_flt))return false;
    TestTrue(TEXT("Second fatal actual damage"),F.Player->TakeDamage(static_cast<float>(FiniteFatal),E,nullptr,nullptr)>0.f);
    TestEqual(TEXT("Both fatal notifications ran"),Calls,2);
    TestEqual(TEXT("Second death also moves remaining cores out of inventory"),F.Player->Inventory->CountItem(Core),0);
    TestTrue(TEXT("Committed injection cost is not refunded into the second bag"),CheckBagCore(F.Config.CoreCost));return true;
}
#endif
