#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatEquipment.h"
#include "DWCombatUnarmed.h"
#include "DWCombatTestWorld.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatItemRegistry.h"
#include "DWWeaponResourceTestProbe.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWSaveGame.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

namespace DWEquipmentTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;FString Error;
    FDWCombatUnarmedTuning Unarmed;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("EquipmentUnarmed"));if(!World)return;
        P=Spawn<ADWPlayerCharacter>();if(P&&GI->GetCombatRules())FDWCombatUnarmedTuning::Build(*GI->GetCombatRules(),Unarmed,Error);
    }
    ~FFixture()
    {
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn(FVector At=FVector(0,0,500))
    {FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* A=World->SpawnActor<T>(T::StaticClass(),At,FRotator::ZeroRotator,Params);if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;}
    UDWWeaponAnimationComponent* W()const{return P?P->WeaponAnimationComponent.Get():nullptr;}
    bool Ready()const{return P&&P->HasCombatVitalsReady()&&W()&&W()->IsEnabled()&&Unarmed.Attack.IsValid()&&!GI->HasActiveSlot();}
    FVector Target()const{return P->GetActorLocation()+FVector(400,0,0);}
    bool Punch(){if(!W()->PrimaryPressed(Target()))return false;W()->PublishAcceptedWeaponStartEffects();return true;}
    void Advance(double Dt){World->TimeSeconds+=static_cast<float>(Dt);W()->AdvanceAnimation(Dt);}
    FDWCombatEquipmentGrantRequest Request(FName Id,uint64 Ordinal=1)const
    {auto* A=UDWCombatAuthoritySubsystem::Get(P);return {A->BeginAction(P),A->CaptureActorLife(P),Ordinal,W()->GetCombatWeaponRestoreEpoch(),Id};}
    void SyntheticSet(EDWAnimatedWeapon Kind)
    {
        auto* Set=NewObject<UDWWeaponAnimationSet>(P);Set->Weapon=Kind;Set->bApplyBasicDamage=false;Set->AttackIntervalSeconds=2.f;
        FDWWeaponAnimationClip Clip;Clip.Role=TEXT("Attack");Clip.DurationSeconds=2.f;Clip.Animation=NewObject<UAnimSequence>(Set);Set->Clips.Add(Clip);
        Clip.Role=TEXT("Raise");Set->Clips.Add(Clip);W()->AnimationSets.Add(Set);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEquipmentOwnershipTest,"DoughWorld.Combat.Equipment.EmptyOwnershipAndAllSevenSelectionGates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEquipmentOwnershipTest::RunTest(const FString&)
{
    DWEquipmentTests::FFixture F;if(!TestTrue(TEXT("New isolated player supports formal empty-hand gameplay"),F.Ready()))return false;
    TestEqual(TEXT("Existing serialized enum values remain stable"),static_cast<int32>(EDWAnimatedWeapon::Alcohol),7);
    TestEqual(TEXT("Unarmed appended without moving old values"),static_cast<int32>(EDWAnimatedWeapon::Unarmed),8);
    TestTrue(TEXT("No starter ownership/equipment"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty()&&F.W()->GetCurrentEquipmentId().IsNone());
    int32 Permanent=0;
    for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())if(Binding.ItemRole==EDWCombatItemRole::PermanentWeapon)
    {++Permanent;TestFalse(*FString::Printf(TEXT("Unowned %s cannot select"),*Binding.ItemId.ToString()),F.W()->SelectWeapon(Binding.Presentation));}
    TestEqual(TEXT("All seven permanent identities checked"),Permanent,7);
    TestFalse(TEXT("No stock cannot newly select bottle"),F.W()->SelectWeapon(EDWAnimatedWeapon::Alcohol));
    TestFalse(TEXT("Unrecognized enum cannot select"),F.W()->SelectWeapon(static_cast<EDWAnimatedWeapon>(255)));
    TestFalse(TEXT("Direct animation hook cannot create an unarmed attack"),F.W()->PlayRole(TEXT("Attack"),F.Target()));
    TestFalse(TEXT("Direct bottle API cannot bypass equipment/source event"),F.P->ThrowAlcoholAt(F.Target()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEquipmentGrantTest,"DoughWorld.Combat.Equipment.GrantReceiptIsAtomicAndDoesNotEquipOrGiveAmmo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEquipmentGrantTest::RunTest(const FString&)
{
    using namespace DWCombatCommit;DWEquipmentTests::FFixture F;if(!F.Ready())return false;
    const auto Request=F.Request(TEXT("DWWeapon_Shotgun"));const int64 Before=F.W()->GetCombatWeaponResourceRevision();
    auto* Bag=F.P->GetInventory();const int64 BagBefore=Bag->GetInventoryRevision();int32 Notices=0,InventoryNotices=0;
    TStrongObjectPtr<UDWWeaponResourceTestProbe> Probe(NewObject<UDWWeaponResourceTestProbe>());
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> InventoryProbe(NewObject<UDWInventoryTransactionTestProbe>());
    InventoryProbe->TestHook=[&]()
    {
        ++InventoryNotices;
        TestEqual(TEXT("Inventory listener sees exactly one granted weapon"),Bag->CountItem(Request.PermanentItemId),1);
        TestTrue(TEXT("Ownership is committed before inventory callback"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Request.PermanentItemId));
        TestEqual(TEXT("Weapon revision committed before inventory callback"),F.W()->GetCombatWeaponResourceRevision(),Before+1);
        TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());F.P->CaptureSaveData(Save.Get());
        const auto* Saved=Save->Inventory.FindByPredicate([&](const FDWItemStack& S){return S.ItemId==Request.PermanentItemId;});
        TestTrue(TEXT("Callback save captures bag and ownership together"),Saved&&Saved->Quantity==1&&Save->CombatPlayerResources.OwnedPermanentWeaponIds.Contains(Request.PermanentItemId));
        auto Reentrant=Request;Reentrant.OperationOrdinal=2;
        const auto Duplicate=FDWCombatEquipmentTransaction::Grant(*F.W(),Reentrant);
        TestTrue(TEXT("Reentrant fresh receipt rejects already-owned without another bag item"),!Duplicate.bCommittedThisCall&&Duplicate.bAlreadyOwned);
    };
    Probe->TestHook=[&](int32 Loaded,int64 Revision,bool bReload)
    {
        ++Notices;TestFalse(TEXT("Grant is not reload"),bReload);TestEqual(TEXT("Loaded remains zero"),Loaded,0);
        TestEqual(TEXT("One revision before notice"),Revision,Before+1);
        TestEqual(TEXT("Inventory notice precedes unified weapon notice"),InventoryNotices,1);
        TestEqual(TEXT("Weapon listener sees granted bag item"),Bag->CountItem(Request.PermanentItemId),1);
        TestTrue(TEXT("Listener captures owned and empty-hand as one fact"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Request.PermanentItemId)&&F.W()->GetCurrentEquipmentId().IsNone());
        TestEqual(TEXT("No free reserve"),Bag->CountItem(TEXT("DWAmmo_BreadShell")),0);
    };
    Bag->OnChanged.AddDynamic(InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.W()->OnWeaponResourceChanged.AddDynamic(Probe.Get(),&UDWWeaponResourceTestProbe::OnResources);
    const auto First=FDWCombatEquipmentTransaction::Grant(*F.W(),Request);TestTrue(TEXT("Unique explicit grant commits"),First.bCommittedThisCall);
    const auto Repeat=FDWCombatEquipmentTransaction::Grant(*F.W(),Request);TestFalse(TEXT("Repeated receipt cannot grant again"),Repeat.bCommittedThisCall);
    TestEqual(TEXT("Single inventory notification"),InventoryNotices,1);TestEqual(TEXT("Single weapon notification"),Notices,1);
    TestEqual(TEXT("Duplicate leaves weapon revision"),F.W()->GetCombatWeaponResourceRevision(),Before+1);
    TestEqual(TEXT("Duplicate leaves bag revision"),Bag->GetInventoryRevision(),BagBefore+1);
    TestEqual(TEXT("Duplicate leaves exactly one weapon in bag"),Bag->CountItem(Request.PermanentItemId),1);
    auto Bottle=F.Request(TEXT("Alcohol"));TestFalse(TEXT("Stock cannot be granted as permanent"),FDWCombatEquipmentTransaction::Grant(*F.W(),Bottle).bCommittedThisCall);
    Bag->OnChanged.RemoveDynamic(InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);InventoryProbe->TestHook=nullptr;
    F.W()->OnWeaponResourceChanged.RemoveDynamic(Probe.Get(),&UDWWeaponResourceTestProbe::OnResources);Probe->TestHook=nullptr;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(F.P);Authority->EndAction(Request.Action);Authority->EndAction(Bottle.Action);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEquipmentGrantFullBagTest,"DoughWorld.Combat.Equipment.GrantFullBagRejectsBagAndOwnershipTogether",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEquipmentGrantFullBagTest::RunTest(const FString&)
{
    DWEquipmentTests::FFixture F;if(!F.Ready())return false;auto* Bag=F.P->GetInventory();const auto* Config=F.GI->GetConfig();
    if(!Config||Config->MaxInventorySlots<1||Bag->GetEffectiveStackLimit(TEXT("Flour"))<1)return false;
    TArray<FDWItemStack> Full;Full.Init(FDWItemStack(TEXT("Flour"),Bag->GetEffectiveStackLimit(TEXT("Flour"))),Config->MaxInventorySlots);
    if(!Bag->SetSlots(Full))return false;
    const int64 BagBefore=Bag->GetInventoryRevision(),WeaponBefore=F.W()->GetCombatWeaponResourceRevision();
    int32 InventoryNotices=0,WeaponNotices=0;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> I(NewObject<UDWInventoryTransactionTestProbe>());I->TestHook=[&](){++InventoryNotices;};
    TStrongObjectPtr<UDWWeaponResourceTestProbe> W(NewObject<UDWWeaponResourceTestProbe>());W->TestHook=[&](int32,int64,bool){++WeaponNotices;};
    Bag->OnChanged.AddDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.W()->OnWeaponResourceChanged.AddDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);
    const auto Request=F.Request(TEXT("DWWeapon_Shotgun"));const auto Result=FDWCombatEquipmentTransaction::Grant(*F.W(),Request);
    TestFalse(TEXT("A full bag cannot grant ownership alone"),Result.bCommittedThisCall);
    TestFalse(TEXT("Rejected identity remains unowned"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Request.PermanentItemId));
    TestEqual(TEXT("No weapon bag item created"),Bag->CountItem(Request.PermanentItemId),0);
    TestEqual(TEXT("Bag revision unchanged"),Bag->GetInventoryRevision(),BagBefore);TestEqual(TEXT("Weapon revision unchanged"),F.W()->GetCombatWeaponResourceRevision(),WeaponBefore);
    TestEqual(TEXT("No rejected inventory callback"),InventoryNotices,0);TestEqual(TEXT("No rejected weapon callback"),WeaponNotices,0);
    bool Exact=Bag->GetSlots().Num()==Full.Num();for(int32 Index=0;Exact&&Index<Full.Num();++Index)Exact=Bag->GetSlots()[Index].ItemId==Full[Index].ItemId&&Bag->GetSlots()[Index].Quantity==Full[Index].Quantity;
    TestTrue(TEXT("Every existing stack survives unchanged"),Exact);
    const auto Repeat=FDWCombatEquipmentTransaction::Grant(*F.W(),Request);TestFalse(TEXT("Rejected receipt retry cannot grant later"),Repeat.bCommittedThisCall);
    Bag->OnChanged.RemoveDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);I->TestHook=nullptr;
    F.W()->OnWeaponResourceChanged.RemoveDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);W->TestHook=nullptr;
    UDWCombatAuthoritySubsystem::Get(F.P)->EndAction(Request.Action);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEquipmentGrantRestoreReentryTest,"DoughWorld.Combat.Equipment.GrantInventoryListenerRestoreWinsWithoutStaleWeaponNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEquipmentGrantRestoreReentryTest::RunTest(const FString&)
{
    DWEquipmentTests::FFixture F;if(!F.Ready())return false;auto* Bag=F.P->GetInventory();
    int32 InventoryNotices=0,WeaponNotices=0;bool Restored=false;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> I(NewObject<UDWInventoryTransactionTestProbe>());
    I->TestHook=[&]()
    {
        ++InventoryNotices;TestEqual(TEXT("Bag commit exists before listener restore"),Bag->CountItem(TEXT("DWWeapon_Shotgun")),1);
        TestTrue(TEXT("Owned commit exists before listener restore"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(TEXT("DWWeapon_Shotgun")));
        // Deliberate isolated native restore: its new epoch must win over the old queued presentation.
        FDWCombatWeaponResourceSnapshot Empty;Restored=F.W()->RestoreCombatWeaponState(Empty,*F.GI->GetCombatRules(),F.Error);
    };
    TStrongObjectPtr<UDWWeaponResourceTestProbe> W(NewObject<UDWWeaponResourceTestProbe>());W->TestHook=[&](int32,int64,bool){++WeaponNotices;};
    Bag->OnChanged.AddDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.W()->OnWeaponResourceChanged.AddDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);
    const auto Request=F.Request(TEXT("DWWeapon_Shotgun"));const auto Result=FDWCombatEquipmentTransaction::Grant(*F.W(),Request);
    TestTrue(TEXT("Original fixed fact commit completed before reentry"),Result.bCommittedThisCall);TestTrue(TEXT("Listener restore accepted"),Restored);
    TestEqual(TEXT("One inventory notification"),InventoryNotices,1);TestEqual(TEXT("No stale old-epoch weapon notice"),WeaponNotices,0);
    TestTrue(TEXT("Listener's later weapon state remains authoritative"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty());
    TestEqual(TEXT("Grant does not rewrite bag after listeners"),Bag->CountItem(TEXT("DWWeapon_Shotgun")),1);
    Bag->OnChanged.RemoveDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);I->TestHook=nullptr;
    F.W()->OnWeaponResourceChanged.RemoveDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);W->TestHook=nullptr;
    UDWCombatAuthoritySubsystem::Get(F.P)->EndAction(Request.Action);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEquipmentSwitchTest,"DoughWorld.Combat.Equipment.ValidBusySwitchCancelsUncommittedAndKeepsLaterLock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEquipmentSwitchTest::RunTest(const FString&)
{
    DWEquipmentTests::FFixture F;if(!F.Ready())return false;
    const auto Request=F.Request(TEXT("DWWeapon_DualDaggers"));if(!FDWCombatEquipmentTransaction::Grant(*F.W(),Request).bCommittedThisCall)return false;
    F.SyntheticSet(EDWAnimatedWeapon::DualDaggers);
    if(!TestTrue(TEXT("Formal unarmed startup"),F.Punch()))return false;
    const double Lock=F.W()->GetRemainingAttackLockSeconds();const int32 Serial=F.W()->ActionSerial;
    TestFalse(TEXT("Invalid switch does not cancel"),F.W()->SelectWeapon(EDWAnimatedWeapon::Greatsword));
    TestTrue(TEXT("Same accepted action remains"),F.W()->IsFormalUnarmedActive()&&F.W()->ActionSerial==Serial);
    TestEqual(TEXT("Invalid switch leaves lock"),F.W()->GetRemainingAttackLockSeconds(),Lock);
    TestTrue(TEXT("Valid switch immediately accepted in windup"),F.W()->SelectWeapon(EDWAnimatedWeapon::DualDaggers));
    TestFalse(TEXT("Unfinished punch cancelled"),F.W()->IsBusy());
    TestTrue(TEXT("Earlier attack's later lock retained"),F.W()->GetRemainingAttackLockSeconds()>=Lock);
    TestFalse(TEXT("PlayRole cannot bypass switch/attack lock"),F.W()->PlayRole(TEXT("Attack"),F.Target()));
    TestFalse(TEXT("New attack cannot bypass old lock"),F.W()->PrimaryPressed(F.Target()));
    F.Advance(Lock+.01);TestTrue(TEXT("Owned equipped weapon starts after lock"),F.W()->PrimaryPressed(F.Target()));
    TestTrue(TEXT("Busy legacy presentation also switches to unarmed"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed));
    TestFalse(TEXT("No previous action remains"),F.W()->IsBusy());
    UDWCombatAuthoritySubsystem::Get(F.P)->EndAction(Request.Action);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWUnarmedRuntimeTest,"DoughWorld.Combat.Unarmed.FormalTimingSingleClickOnePacketWithoutAnimation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWUnarmedRuntimeTest::RunTest(const FString&)
{
    DWEquipmentTests::FFixture F;if(!F.Ready())return false;auto* Enemy=F.Spawn<ADWEnemyCharacter>(F.P->GetActorLocation()+FVector(60,0,0));if(!Enemy)return false;
    Enemy->Health=1000.f;const float Before=Enemy->Health;int32 Notices=0;
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe(NewObject<UDWCombatAuthorityTestProbe>());
    Probe->AnyCallback=[&](AActor*,float,AController*,AActor*){++Notices;};Enemy->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    TestNull(TEXT("No source animation required for formal timing"),F.W()->GetActiveSequence());
    if(!TestTrue(TEXT("Single input accepted"),F.Punch()))return false;
    TestFalse(TEXT("No second press can create parallel attack"),F.W()->PrimaryPressed(F.Target()));
    F.Advance(F.Unarmed.Attack.WindupSeconds*.5);TestEqual(TEXT("No windup damage"),Enemy->Health,Before);
    F.Advance(F.Unarmed.Attack.IntervalSeconds);TestEqual(TEXT("One configured weak punch"),Enemy->Health,Before-static_cast<float>(F.Unarmed.Attack.Damage));
    TestEqual(TEXT("Exactly one target receipt/notification"),Notices,1);TestFalse(TEXT("Recovery finishes"),F.W()->IsBusy());
    F.Advance(F.Unarmed.Attack.IntervalSeconds*3.);TestEqual(TEXT("Held primary never repeats unarmed"),Notices,1);
    Enemy->OnTakeAnyDamage.RemoveDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);Probe->AnyCallback=nullptr;return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWUnarmedPauseGeometryTest,"DoughWorld.Combat.Unarmed.PauseAndIndependentHurtboxLOS",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWUnarmedPauseGeometryTest::RunTest(const FString&)
{
    DWEquipmentTests::FFixture F;if(!F.Ready())return false;
    auto* Behind=F.Spawn<ADWEnemyCharacter>(F.P->GetActorLocation()+FVector(-160,0,0));
    auto* Front=F.Spawn<ADWEnemyCharacter>(F.P->GetActorLocation()+FVector(100,0,0));if(!Behind||!Front)return false;
    auto* Wall=F.Spawn<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(4,200,200));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();Box->SetWorldLocation(F.P->GetActorLocation()+FVector(45,0,0));
    const float B=Behind->Health,H=Front->Health;auto* Pauser=F.Spawn<APlayerState>();if(!F.Punch())return false;
    F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);F.W()->AdvanceAnimation(F.Unarmed.Attack.IntervalSeconds*4.);
    TestEqual(TEXT("Paused windup stays at zero"),F.W()->GetElapsedSeconds(),0.f);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.Advance(F.Unarmed.Attack.IntervalSeconds);
    TestEqual(TEXT("Independent target hurtbox behind player misses"),Behind->Health,B);TestEqual(TEXT("Solid wall blocks target hurtbox despite overlap range"),Front->Health,H);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEquipmentSaveTest,"DoughWorld.Combat.Equipment.CurrentOwnedLoadedInventoryRestoreIsAtomic",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEquipmentSaveTest::RunTest(const FString&)
{
    DWEquipmentTests::FFixture F;if(!F.Ready())return false;
    FDWCombatWeaponResourceSnapshot Snapshot;Snapshot.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_Shotgun"));
    Snapshot.CurrentEquipmentId=TEXT("DWWeapon_Shotgun");Snapshot.LoadedShotgunShells=2;
    if(!F.W()->RestoreCombatWeaponState(Snapshot,*F.GI->GetCombatRules(),F.Error)||!F.P->GetInventory()->TryAddItem(TEXT("DWAmmo_BreadShell"),7))return false;
    TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());F.P->CaptureSaveData(Save.Get());
    const auto Revision=F.W()->GetCombatWeaponResourceRevision();const auto Epoch=F.W()->GetCombatWeaponRestoreEpoch();const float Health=F.P->Health;
    Save->CombatPlayerResources.CurrentEquipmentId=TEXT("DWWeapon_Greatsword");
    TestFalse(TEXT("Unowned current equipment rejects whole restore"),F.P->TryApplySaveData(Save.Get()));
    TestEqual(TEXT("Invalid restore preserves revision"),F.W()->GetCombatWeaponResourceRevision(),Revision);
    TestEqual(TEXT("Invalid restore preserves epoch"),F.W()->GetCombatWeaponRestoreEpoch(),Epoch);
    TestEqual(TEXT("Invalid restore preserves health"),F.P->Health,Health);
    TestEqual(TEXT("Invalid restore preserves loaded"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,2);
    TestEqual(TEXT("Invalid restore preserves reserve"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),7);
    Save->CombatPlayerResources.CurrentEquipmentId=Snapshot.CurrentEquipmentId;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());int32 Notices=0;
    Probe->TestHook=[&]()
    {
        ++Notices;TestEqual(TEXT("Inventory listener sees selected mirror adopted"),F.W()->SelectedWeapon,EDWAnimatedWeapon::Shotgun);
        TestEqual(TEXT("Inventory listener sees current identity adopted"),F.W()->GetCurrentEquipmentId(),Snapshot.CurrentEquipmentId);
        TestEqual(TEXT("Inventory listener sees full loaded amount"),F.W()->AmmoInMagazine,2);
    };
    F.P->GetInventory()->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Real in-memory save restoration"),F.P->TryApplySaveData(Save.Get()));TestEqual(TEXT("One native inventory restore notice"),Notices,1);
    F.P->GetInventory()->OnChanged.RemoveDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);Probe->TestHook=nullptr;
    Save->CombatPlayerResources.SubblockSchema=1;TestFalse(TEXT("Old missing-current schema explicitly rejected, not guessed"),F.P->TryApplySaveData(Save.Get()));
    return true;
}
#endif
