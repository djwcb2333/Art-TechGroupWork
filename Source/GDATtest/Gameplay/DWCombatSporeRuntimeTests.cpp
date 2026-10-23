#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatSporeGun.h"
#include "DWCombatSporeProjectile.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatEquipmentTestFixture.h"
#include "DWCombatTestWorld.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWDamageVolumeLibrary.h"
#include "DWSaveGame.h"
#include "Components/BoxComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
namespace DWSporeTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> InventoryProbe{NewObject<UDWInventoryTransactionTestProbe>()};
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> DamageProbe{NewObject<UDWCombatAuthorityTestProbe>()};
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;UDWWeaponAnimationComponent* Weapon=nullptr;
    const FDWCombatWeaponBinding* Binding=nullptr;FDWSporeGunConfig Config;FString Error;bool bReady=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D16SporeWorld"));if(!World)return;
        Player=Spawn<ADWPlayerCharacter>(FVector(0,0,500));if(!Player)return;Weapon=Player->WeaponAnimationComponent;
        Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::SporeGun);
        bReady=Binding&&FDWSporeGunConfig::Build(*GI->GetCombatRules(),Config,Error)&&Restore(EDWCombatBaseForm::Normal,8);
        if(bReady)Player->Inventory->OnChanged.AddDynamic(InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    }
    ~FFixture()
    {
        InventoryProbe->TestHook=nullptr;DamageProbe->AnyCallback=nullptr;DamageProbe->PointCallback=nullptr;
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn(FVector Position)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<T>(T::StaticClass(),Position,FRotator::ZeroRotator,Params);
        if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    bool Restore(EDWCombatBaseForm Form,int32 Ammo)
    {
        auto* S=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error))return false;
        S->PlayerTransform=FTransform(FRotator::ZeroRotator,FVector(0,0,500));S->bHasPlayerTransform=true;
        S->CombatPlayerResources.OwnedPermanentWeaponIds={Binding->ItemId};S->CombatPlayerResources.CurrentEquipmentId=Binding->ItemId;
        auto& V=S->CombatPlayerResources.Vitals;V.BaseForm=Form;
        FDWCombatPlayerVitalsConfig C;if(!FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),C,Error))return false;
        V.Water=C.Vitals.WaterMax;V.Satiety=C.Vitals.SatietyMax;
        if(Form!=EDWCombatBaseForm::Normal){V.FormRemainingSeconds=C.FormDurationSeconds;V.Special=C.InitialFormSpecial;}
        S->bYeastForm=Form==EDWCombatBaseForm::Yeast;S->Transformation=static_cast<float>(V.Special-V.SpecialGrowthCarry);
        if(Ammo>0){FDWItemStack Stack;Stack.ItemId=Binding->AmmoItemId;Stack.Quantity=Ammo;S->Inventory.Add(Stack);}
        return Player->TryApplySaveData(S)&&!GI->HasActiveSlot();
    }
    ADWEnemyCharacter* Enemy(FVector Position)
    {
        auto* E=Spawn<ADWEnemyCharacter>(Position);if(!E)return nullptr;
        E->SetActorTickEnabled(false);E->Health=E->MaxHealth=1000.f;
        auto* Hitbox=NewObject<UDWDamageHitboxComponent>(E);Hitbox->SetupAttachment(E->GetRootComponent());Hitbox->SetBoxExtent(FVector(35,35,80));
        E->AddInstanceComponent(Hitbox);Hitbox->RegisterComponent();return E;
    }
    AActor* Wall(FVector Position,FVector Extent)
    {
        auto* A=Spawn<AActor>(Position);auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);A->AddInstanceComponent(B);
        B->SetBoxExtent(Extent);B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);B->SetCollisionObjectType(ECC_WorldStatic);
        B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();A->SetActorLocation(Position);return A;
    }
    TArray<ADWCombatSporeProjectile*> Bullets()const
    {TArray<ADWCombatSporeProjectile*> Out;for(TActorIterator<ADWCombatSporeProjectile> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&It->IsReleased()&&!It->IsSpent())Out.Add(*It);return Out;}
    void Advance(float Dt)
    {
        World->TimeSeconds+=Dt;Player->TickGameplayClocks(Dt,false);
        for(auto* B:Bullets())if(IsValid(B)&&!B->IsActorBeingDestroyed())B->Tick(Dt);
    }
    bool Press(){return Weapon->PrimaryPressed(Player->GetActorLocation()+FVector(1000,0,0));}
    void SingleShot(){Advance(static_cast<float>(Config.WindupSeconds+.001));Weapon->PrimaryReleased();}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeAtomicRuntimeTest,"DoughWorld.Combat.SporeGunRuntime.NoTargetPauseAndAtomicAmmo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeAtomicRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWSporeTests;FFixture F;if(!TestTrue(TEXT("Owned weapon, isolated resources, no clip requirement"),F.bReady))return false;
    auto* Pause=F.Spawn<APlayerState>(FVector::ZeroVector);int32 Notices=0;const int32 Before=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);
    F.InventoryProbe->TestHook=[&]()
    {
        ++Notices;TestEqual(TEXT("Ammo committed before real delegate"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),Before-F.Config.AmmoPerShot);
        const auto Receipt=UDWCombatAuthoritySubsystem::Get(F.Player)->FindReceipt(F.Weapon->GetLastWeaponResourceReceipt());
        TestTrue(TEXT("Receipt terminal before actual inventory callback"),Receipt&&Receipt->State==DWCombatCommit::EReceiptState::Committed);
        TestEqual(TEXT("Native released bullet already exists"),F.Bullets().Num(),1);
        TestFalse(TEXT("Same callback cannot recursively advance attack clocks"),F.Weapon->AdvanceAnimation(5.));
        F.World->GetWorldSettings()->SetPauserPlayerState(Pause);F.Weapon->PrimaryReleased();
    };
    TestTrue(TEXT("No target still accepts attack"),F.Press());F.Advance(static_cast<float>(F.Config.WindupSeconds*.5));
    TestEqual(TEXT("No pre-release cost"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),Before);
    F.Advance(static_cast<float>(F.Config.WindupSeconds));TestEqual(TEXT("Exactly one actual notification"),Notices,1);
    F.InventoryProbe->TestHook=nullptr;F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.Advance(0.f);F.Advance(.5f);
    TestEqual(TEXT("Pause release stops repetition without double debit"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),Before-F.Config.AmmoPerShot);
    TestTrue(TEXT("New action may start"),F.Press());F.Player->CancelCombatActions();F.Advance(.5f);
    TestEqual(TEXT("Cancelled windup is free"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),Before-F.Config.AmmoPerShot);
    TestTrue(TEXT("Empty saved stock is a strict restore"),F.Restore(EDWCombatBaseForm::Normal,0));F.Advance(.5f);
    TestFalse(TEXT("No ammunition means no attack"),F.Press());TestEqual(TEXT("No free magazine"),F.Weapon->AmmoInMagazine,0);
    TestTrue(TEXT("Stock restored for held-input path"),F.Restore(EDWCombatBaseForm::Normal,4));F.Advance(.5f);
    TestTrue(TEXT("Hold starts"),F.Press());F.Advance(static_cast<float>(F.Config.WindupSeconds+F.Config.RecoverySeconds+.001));
    F.Weapon->UpdateFormalSporeGapAim(F.Player->GetActorLocation()+FVector(0,1000,0));
    F.Advance(static_cast<float>(F.Config.IntervalSeconds-F.Config.WindupSeconds-F.Config.RecoverySeconds+F.Config.WindupSeconds+.001));
    TestEqual(TEXT("Continuous hold creates two independently paid shots"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),4-2*F.Config.AmmoPerShot);
    bool Reaimed=false;for(auto* Bullet:F.Bullets())Reaimed|=Bullet->GetFlightDirection().Equals(FVector::RightVector,1.e-8);
    TestTrue(TEXT("Next cycle samples the aim changed during the unlocked gap"),Reaimed);
    F.Weapon->PrimaryReleased();F.Advance(.5f);
    TestEqual(TEXT("Release prevents third cycle"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),4-2*F.Config.AmmoPerShot);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeCollisionRuntimeTest,"DoughWorld.Combat.SporeGunRuntime.FirstWallAndExactlyOneTarget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeCollisionRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWSporeTests;
    {
        FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
        auto* E=F.Enemy(FVector(500,0,500));F.Wall(FVector(250,0,500),FVector(20,150,200));if(!E)return false;
        TestTrue(TEXT("Wall-blocked target still permits manual straight fire"),F.Press());F.SingleShot();
        const auto Bullets=F.Bullets();if(!TestEqual(TEXT("One bullet"),Bullets.Num(),1))return false;
        TestNull(TEXT("Wall target was not automatically locked"),Bullets[0]->GetLockedTarget());F.Advance(.4f);
        TestEqual(TEXT("First wall stops damage"),E->Health,1000.f);TestEqual(TEXT("No projectile beyond wall"),F.Bullets().Num(),0);
    }
    {
        FFixture F;if(!TestTrue(TEXT("Second fixture"),F.bReady))return false;
        auto* Near=F.Enemy(FVector(300,0,500));auto* Far=F.Enemy(FVector(600,0,500));if(!Near||!Far)return false;int32 Any=0,Point=0;
        Near->OnTakeAnyDamage.AddDynamic(F.DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
        Near->OnTakePointDamage.AddDynamic(F.DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Point);
        F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*){++Any;};
        F.DamageProbe->PointCallback=[&](AActor*,float,AController*,AActor*){++Point;};
        TestTrue(TEXT("Shot starts"),F.Press());F.SingleShot();F.Advance(.5f);
        TestEqual(TEXT("Nearest body takes one explicit packet"),Near->Health,1000.f-static_cast<float>(F.Config.Shot(EDWSporeForm::Normal).Damage));
        TestEqual(TEXT("No penetration"),Far->Health,1000.f);TestEqual(TEXT("Actual Any delegate once"),Any,1);TestEqual(TEXT("Actual point delegate once"),Point,1);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeLockRuntimeTest,"DoughWorld.Combat.SporeGunRuntime.LockLossDoesNotRetarget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeLockRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWSporeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    auto* Target=F.Enemy(FVector(600,200,500));auto* Other=F.Enemy(FVector(650,-200,500));F.Enemy(FVector(-100,0,500));
    if(!Target||!Other)return false;TestTrue(TEXT("Shoot"),F.Press());F.SingleShot();auto Bullets=F.Bullets();
    if(!TestEqual(TEXT("Bullet exists"),Bullets.Num(),1))return false;auto* B=Bullets[0];
    TestEqual(TEXT("Closest front hostile chosen"),B->GetLockedTarget(),Target);F.Advance(.02f);const FVector Heading=B->GetFlightDirection();
    Target->Destroy();F.Advance(.02f);TestNull(TEXT("Lock retired permanently"),B->GetLockedTarget());
    TestTrue(TEXT("Lost target keeps current heading"),B->GetFlightDirection().Equals(Heading,1.e-9));
    TestEqual(TEXT("Other target not substituted"),Other->Health,1000.f);F.Advance(2.f);
    TestEqual(TEXT("Path budget ends bullet rather than infinite homing"),F.Bullets().Num(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeReleasedRuntimeTest,"DoughWorld.Combat.SporeGunRuntime.ReleasedSnapshotSurvivesSourceRestore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeReleasedRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWSporeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady)||!TestTrue(TEXT("Yeast form"),F.Restore(EDWCombatBaseForm::Yeast,8)))return false;
    auto* E=F.Enemy(FVector(500,0,500));if(!E)return false;TestTrue(TEXT("Shoot yeast snapshot"),F.Press());F.SingleShot();auto Bullets=F.Bullets();
    if(!TestEqual(TEXT("One released bullet"),Bullets.Num(),1))return false;
    const auto Snapshot=Bullets[0]->GetShotSnapshot();const auto Emitter=Bullets[0]->GetEmitter();
    // D22 is not integrated yet: a full-world load must not discard a released emitter.
    // Retain this historical test name and strengthen it with explicit no-side-effect rejection.
    const auto BeforeLife=UDWCombatAuthoritySubsystem::Get(F.Player)->CaptureActorLife(F.Player);
    const float BeforeHP=F.Player->Health;const int32 BeforeAmmo=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);
    auto* Captured=NewObject<UDWSaveGame>();F.Player->CaptureSaveData(Captured);
    TestTrue(TEXT("Capture marks released world state as incomplete"),Captured->CombatPlayerResources.bRequiresReleasedEffectsSchema);
    TestFalse(TEXT("Incomplete captured world cannot validate"),DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*Captured,F.Error));
    TestFalse(TEXT("World restore rejects before changing live effects"),F.Restore(EDWCombatBaseForm::Normal,7));
    TestEqual(TEXT("Rejected restore preserves player life"),UDWCombatAuthoritySubsystem::Get(F.Player)->CaptureActorLife(F.Player).Life,BeforeLife.Life);
    TestEqual(TEXT("Rejected restore preserves health"),F.Player->Health,BeforeHP);
    TestEqual(TEXT("Rejected restore preserves paid inventory"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),BeforeAmmo);
    FDamageEvent Death;F.Player->TakeDamage(10000.f,Death,nullptr,nullptr);
    TestTrue(TEXT("Actual source death ends its action and form"),F.Player->IsDead());
    TestEqual(TEXT("Already released range remains yeast"),Snapshot.RangeCm,F.Config.YeastRangeCm);
    TestEqual(TEXT("Emitter identity is retained"),Bullets[0]->GetEmitter().Id,Emitter.Id);
    F.Advance(.5f);TestEqual(TEXT("Old released bullet is still a legitimate hit"),E->Health,1000.f-static_cast<float>(Snapshot.Damage));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeNeutralRuntimeTest,"DoughWorld.Combat.SporeGunRuntime.ManualBulletCanHitNeutralButNeverAutoLocks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeNeutralRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWSporeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady)||!TestTrue(TEXT("Mold form"),F.Restore(EDWCombatBaseForm::Mold,8)))return false;
    auto* Neutral=F.Enemy(FVector(400,0,500));if(!Neutral)return false;
    Neutral->CombatProfile=EDWEnemyCombatProfile::MoldGrunt;
    if(!TestTrue(TEXT("Formal neutral profile initialized"),Neutral->InitializeFormalCombat(*F.GI->GetCombatRules(),F.Error)))return false;
    TestFalse(TEXT("Same faction has no remembered hostility"),Neutral->IsHostileToCombatPlayer(F.Player));const float Before=Neutral->Health;
    TestTrue(TEXT("Player may fire manually"),F.Press());F.SingleShot();auto Bullets=F.Bullets();
    if(!TestEqual(TEXT("One shot"),Bullets.Num(),1))return false;
    TestNull(TEXT("Neutral was not an automatic target"),Bullets[0]->GetLockedTarget());F.Advance(.4f);
    TestEqual(TEXT("Manual straight collision deals one damage snapshot"),Neutral->Health,Before-static_cast<float>(F.Config.Shot(EDWSporeForm::MoldMobile).Damage));
    TestTrue(TEXT("The struck neutral remembers this actual combat"),Neutral->HasRememberedPlayerCombat());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeFeedback23CancelTest,"DoughWorld.Combat.SporeGunRuntime.Feedback23ReleaseAndMovementResetWithoutRefund",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeFeedback23CancelTest::RunTest(const FString&)
{
    using namespace DWSporeTests;
    for(int32 Case=0;Case<4;++Case)
    {
        FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady&&F.Press()))return false;
        const int32 Stock=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);
        const bool Released=Case>=2;
        if(Released)F.Advance(static_cast<float>(F.Config.WindupSeconds+.001));
        else F.Advance(static_cast<float>(F.Config.WindupSeconds*.5));
        const double LockBeforeCancel=F.Weapon->GetRemainingAttackLockSeconds();
        if(Case%2==0)F.Weapon->PrimaryReleased();else F.Player->MoveCameraRelative(1,0);
        TestFalse(TEXT("Release or movement ends current action in windup/recovery"),F.Weapon->IsBusy());
        TestTrue(TEXT("Spore plan is fully reset"),F.Weapon->GetFormalSporePhase()==EDWSporePhase::None);
        TestEqual(TEXT("Only paid-shot interval survives cancellation"),F.Weapon->GetRemainingAttackLockSeconds(),Released?LockBeforeCancel:0.);
        TestEqual(TEXT("Elapsed source resets"),F.Weapon->GetElapsedSeconds(),0.f);
        TestTrue(TEXT("Outgoing visual role is cleared"),F.Weapon->GetActiveRole().IsNone());
        TestEqual(TEXT("Only a released shot consumes inventory"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),Stock-(Released?F.Config.AmmoPerShot:0));
        TestEqual(TEXT("Committed projectile survives cancellation"),F.Bullets().Num(),Released?1:0);
        if(Released)
        {
            TestFalse(TEXT("A paid shot cannot bypass its held-fire cycle deadline"),F.Press());
            F.Advance(static_cast<float>(LockBeforeCancel+.0001));
        }
        TestTrue(TEXT("Fresh press restarts when the paid-shot deadline allows it"),F.Press());F.Weapon->PrimaryReleased();
        F.Advance(.5f);
        TestEqual(TEXT("Old windup and fresh cancelled shot cannot create late debits"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),Stock-(Released?F.Config.AmmoPerShot:0));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeFeedback25CadenceTest,"DoughWorld.Combat.SporeGunRuntime.Feedback25TapAndHeldSharePaidShotCadence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeFeedback25CadenceTest::RunTest(const FString&)
{
    using namespace DWSporeTests;
    for(const auto Form:{EDWCombatBaseForm::Normal,EDWCombatBaseForm::Yeast})
    {
        int32 HeldShots=0;
        {
            FFixture F;if(!TestTrue(TEXT("Held fixture"),F.bReady&&F.Restore(Form,8)&&F.Press()))return false;
            const int32 Before=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);
            for(int32 Frame=0;Frame<600;++Frame)F.Advance(.001f);
            HeldShots=(Before-F.Player->Inventory->CountItem(F.Binding->AmmoItemId))/F.Config.AmmoPerShot;
            TestTrue(TEXT("Held-fire baseline actually paid multiple shots"),HeldShots>=2);F.Weapon->PrimaryReleased();
        }
        {
            FFixture F;if(!TestTrue(TEXT("Rapid-tap fixture"),F.bReady&&F.Restore(Form,8)))return false;
            const int32 Before=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);
            int32 Previous=Before;TArray<double> ShotTimes;
            F.InventoryProbe->TestHook=[&]()
            {
                const int32 Current=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);
                if(Current<Previous){ShotTimes.Add(F.World->GetTimeSeconds());Previous=Current;}
            };
            for(int32 Frame=0;Frame<600;++Frame)
            {
                if(!F.Weapon->IsBusy())F.Press(); // Rejected early requests must not mutate paid resources.
                const int32 Stock=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);F.Advance(.001f);
                if(F.Player->Inventory->CountItem(F.Binding->AmmoItemId)<Stock)F.Weapon->PrimaryReleased();
            }
            const int32 TappedShots=(Before-F.Player->Inventory->CountItem(F.Binding->AmmoItemId))/F.Config.AmmoPerShot;
            TestEqual(TEXT("Repeated cancellation cannot fire more paid shots than held input"),TappedShots,HeldShots);
            const double Rate=F.Config.Rate(Form==EDWCombatBaseForm::Yeast?EDWSporeForm::Yeast:EDWSporeForm::Normal);
            for(int32 I=1;I<ShotTimes.Num();++I)
                TestTrue(TEXT("Each paid tap respects the formal interval at the current form rate"),ShotTimes[I]-ShotTimes[I-1]>=F.Config.IntervalSeconds/Rate-.002);
            F.InventoryProbe->TestHook=nullptr;F.Weapon->PrimaryReleased();
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSporeFeedback25SwitchTest,"DoughWorld.Combat.SporeGunRuntime.Feedback25SwitchAndRejectedSwordCannotClearPaidCadence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSporeFeedback25SwitchTest::RunTest(const FString&)
{
    using namespace DWSporeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    if(!TestTrue(TEXT("Own sword before live requests"),DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*F.Weapon,*F.GI->GetCombatRules(),EDWAnimatedWeapon::Greatsword,F.Error))||
        !TestTrue(TEXT("Restore current spore without dropping ownership"),DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*F.Weapon,*F.GI->GetCombatRules(),EDWAnimatedWeapon::SporeGun,F.Error)))return false;
    TestTrue(TEXT("Paid spore action starts"),F.Press());F.SingleShot();
    const double PaidLock=F.Weapon->GetRemainingAttackLockSeconds();TestTrue(TEXT("Paid shot leaves formal wait"),PaidLock>0.);
    TestTrue(TEXT("Legal switch may cancel visuals"),F.Weapon->SelectWeapon(EDWAnimatedWeapon::Greatsword));
    const double SwitchedLock=F.Weapon->GetRemainingAttackLockSeconds();TestTrue(TEXT("Switch preserves or extends the paid-shot lock"),SwitchedLock>=PaidLock);
    TestFalse(TEXT("Locked sword request cannot acquire a sword action"),F.Press());F.Weapon->CancelAction();
    TestEqual(TEXT("Cleanup without an actual sword action cannot invoke sword's clear-lock policy"),F.Weapon->GetRemainingAttackLockSeconds(),SwitchedLock);
    TestTrue(TEXT("Switch back to the owned spore"),F.Weapon->SelectWeapon(EDWAnimatedWeapon::SporeGun));
    TestTrue(TEXT("Return switch still preserves the paid shot"),F.Weapon->GetRemainingAttackLockSeconds()>=PaidLock);
    TestFalse(TEXT("Tap cannot fire through an equipment switch"),F.Press());
    F.Advance(static_cast<float>(F.Weapon->GetRemainingAttackLockSeconds()+.0001));
    TestTrue(TEXT("Formal lock expiry admits a new shot windup"),F.Press());F.Weapon->PrimaryReleased();return true;
}
#endif
