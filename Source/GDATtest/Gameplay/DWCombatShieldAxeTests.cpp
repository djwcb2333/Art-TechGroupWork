#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatShieldAxe.h"
#include "DWCombatTestWorld.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatPlayerSave.h"
#include "DWSaveGame.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace DWShieldTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;ADWEnemyCharacter* Front=nullptr;
    FDWShieldAxeTuning C;FString Error;bool Ready=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D13Shield"));if(!World)return;
        P=Spawn<ADWPlayerCharacter>();Front=Spawn<ADWEnemyCharacter>(FVector(100,0,500));
        if(!P||!Front||!P->HasCombatVitalsReady()||!FDWShieldAxeTuning::Build(*GI->GetCombatRules(),C,Error))return;
        FDWCombatWeaponResourceSnapshot S;S.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_ShieldAxe"));S.CurrentEquipmentId=TEXT("DWWeapon_ShieldAxe");
        Ready=W()->RestoreCombatWeaponState(S,*GI->GetCombatRules(),Error)&&W()->IsEnabled()&&!GI->HasActiveSlot();
    }
    ~FFixture()
    {
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn(FVector At=FVector(0,0,500))
    {FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* Actor=World->SpawnActor<T>(T::StaticClass(),At,FRotator::ZeroRotator,Params);if(Actor&&!Actor->HasActorBegunPlay())Actor->DispatchBeginPlay();return Actor;}
    UDWWeaponAnimationComponent* W()const{return P?P->WeaponAnimationComponent.Get():nullptr;}
    UDWCombatAuthoritySubsystem* A()const{return UDWCombatAuthoritySubsystem::Get(P);}
    FDWCombatDamageResult Hit(float Amount,ADWEnemyCharacter* Source=nullptr)
    {
        Source=Source?Source:Front;FDWCombatDamageCommand Command;
        Command.Identity={A()->BeginAction(Source),1,DWCombatCommit::EActionDamageKind::EnemyMelee,1};Command.Source=Source;Command.Target=P;
        Command.TargetLife=A()->CaptureActorLife(P);Command.Amount=Amount;Command.KnockbackDirection=(P->GetActorLocation()-Source->GetActorLocation()).GetSafeNormal2D();
        Command.KnockbackDistance=60.f;Command.KnockbackSeconds=.2f;
        const auto Result=A()->SubmitActionDamage(Command);A()->EndAction(Command.Identity.Action);return Result;
    }
    bool Raise(){if(!W()->SecondaryPressed())return false;W()->PublishAcceptedWeaponStartEffects();return true;}
    void Advance(double Dt)
    {if(!World->IsPaused())World->TimeSeconds+=static_cast<float>(Dt);P->TickGameplayClocks(static_cast<float>(Dt),false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldPureWindowTest,"DoughWorld.Combat.ShieldAxe.WindowCooldownAndSnapshotContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldPureWindowTest::RunTest(const FString&)
{
    DWShieldTests::FFixture F;if(!TestTrue(TEXT("Real rules configured without animation"),F.Ready))return false;
    FDWShieldAxeDefense D;if(!D.Configure(F.C))return false;
    TestTrue(TEXT("New raise at t=10"),D.Raise(10.,0));TestTrue(TEXT("Inclusive before window"),D.CanPerfectAt(10.+F.C.ParryBeforeSeconds));
    TestFalse(TEXT("Long hold does not refresh perfect"),D.CanPerfectAt(10.+F.C.ParryBeforeSeconds+.001));
    TestTrue(TEXT("Typed front projectile eligible"),D.PrepareImpact(10.,EDWShieldIncomingKind::PhysicalProjectile,FVector::ForwardVector,FVector::ForwardVector).bEligible);
    TestFalse(TEXT("Unknown incoming class is not blockable"),D.PrepareImpact(10.,static_cast<EDWShieldIncomingKind>(255),FVector::ForwardVector,FVector::ForwardVector).bEligible);
    TestFalse(TEXT("Back-facing projectile not eligible"),D.PrepareImpact(10.,EDWShieldIncomingKind::PhysicalProjectile,FVector::ForwardVector,-FVector::ForwardVector).bEligible);
    D.Release();TestTrue(TEXT("Re-raise inside cooldown still normal guard"),D.Raise(10.+F.C.ReopenCooldownSeconds*.5,0));
    TestFalse(TEXT("No new perfect before reopen CD"),D.CanPerfectAt(10.+F.C.ReopenCooldownSeconds*.5));
    D.RecordParry();TestEqual(TEXT("Counter set, not stacked"),D.GetCounterCharges(),F.C.CounterHits);D.RecordParry();TestEqual(TEXT("Again still exact configured count"),D.GetCounterCharges(),F.C.CounterHits);
    auto Save=D.Capture(10.+F.C.ReopenCooldownSeconds*.5);FDWShieldAxeDefense Loaded;Loaded.Configure(F.C);
    TestTrue(TEXT("Native relative snapshot validates"),Loaded.Restore(Save,100.));TestFalse(TEXT("Restore never restores held input"),Loaded.IsHeld());
    TestEqual(TEXT("Counter preserved in DTO"),Loaded.GetCounterCharges(),F.C.CounterHits);
    Save.ReopenRemainingSeconds=-1.;TestFalse(TEXT("Bad snapshot rejected atomically"),Loaded.Restore(Save,100.));
    TestEqual(TEXT("Bad restore does not clear counter"),Loaded.GetCounterCharges(),F.C.CounterHits);
    Loaded.NotifyBaseForm(2);TestEqual(TEXT("Base-form change clears counter"),Loaded.GetCounterCharges(),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldLateFatalTest,"DoughWorld.Combat.ShieldAxe.LateParryCancelsPendingFatalWithoutRewindingHealth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldLateFatalTest::RunTest(const FString&)
{
    DWShieldTests::FFixture F;if(!F.Ready)return false;const float Before=F.P->Health;
    const auto Hit=F.Hit(Before*2.f);
    TestEqual(TEXT("Hit stays pending"),Hit.Receipt.State,DWCombatCommit::EReceiptState::Pending);
    TestEqual(TEXT("No temporary health subtraction"),F.P->Health,Before);TestFalse(TEXT("No premature death"),F.P->IsDead());
    TestFalse(TEXT("No premature displacement"),F.P->IsCombatKnockedBack());
    F.Advance(F.C.ParryAfterSeconds*.5);TestTrue(TEXT("New effective raise within after window"),F.Raise());
    const auto Receipt=F.A()->FindReceipt(Hit.Receipt.Handle);if(!TestTrue(TEXT("Receipt remains inspectable"),Receipt.has_value()))return false;
    TestEqual(TEXT("Only pending impact rejected"),Receipt->State,DWCombatCommit::EReceiptState::Rejected);
    TestEqual(TEXT("Counter granted"),F.W()->GetShieldCounterCharges(),F.C.CounterHits);
    F.Advance(F.C.ParryAfterSeconds*2.);TestEqual(TEXT("No delayed ghost hit"),F.P->Health,Before);TestFalse(TEXT("No delayed knockback"),F.P->IsCombatKnockedBack());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldOrdinaryTest,"DoughWorld.Combat.ShieldAxe.OrdinaryBlockSnapshotBackAndVitalsRemainIndependent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldOrdinaryTest::RunTest(const FString&)
{
    DWShieldTests::FFixture F;if(!F.Ready||!F.Raise())return false;
    F.Advance(F.C.ParryBeforeSeconds+.01);const float Before=F.P->Health;
    const auto Front=F.Hit(20.f);TestEqual(TEXT("Long hold front impact deferred"),Front.Receipt.State,DWCombatCommit::EReceiptState::Pending);
    auto* Back=F.Spawn<ADWEnemyCharacter>(FVector(-100,0,500));const auto Rear=F.Hit(3.f,Back);
    TestTrue(TEXT("Back hit commits immediately"),Rear.AppliedThisCall>0.f);
    FDWCombatVitalsDamagePair Pair;Pair.Target=F.P;Pair.SourceLife=F.A()->CaptureActorLife(F.P);Pair.WorldEpoch=F.A()->GetWorldEpoch();Pair.DehydrationOrdinal=1;Pair.DehydrationAmount=2.f;
    F.A()->SubmitVitalsDamagePair(Pair);const float BeforePending=F.P->Health;
    TestTrue(TEXT("Pending front packet grants no global invulnerability"),FMath::IsNearlyEqual(BeforePending,Before-5.f,.001f));
    F.W()->SecondaryReleased();F.Advance(F.C.ParryAfterSeconds+.01);
    const float Blocked=20.f*static_cast<float>(1.-F.C.BlockReduction);
    TestTrue(TEXT("Release cannot change frozen ordinary block amount"),FMath::IsNearlyEqual(F.P->Health,BeforePending-Blocked,.001f));
    TestEqual(TEXT("No pending packages remain"),F.A()->GetPendingShieldDamageCount(F.P),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldPauseSwitchTest,"DoughWorld.Combat.ShieldAxe.PendingSurvivesPauseSwitchAndSourceDeath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldPauseSwitchTest::RunTest(const FString&)
{
    DWShieldTests::FFixture F;if(!F.Ready)return false;const float Before=F.P->Health;const auto Hit=F.Hit(7.f);
    auto* Pauser=F.Spawn<APlayerState>();F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);F.Advance(2.);
    TestEqual(TEXT("Paused deadline freezes"),F.P->Health,Before);TestEqual(TEXT("Paused pending retained"),F.A()->GetPendingShieldDamageCount(F.P),1);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);TestTrue(TEXT("Switch to empty hand"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed));
    F.Front->Destroy();F.Advance(F.C.ParryAfterSeconds+.01);
    TestTrue(TEXT("Committed impact cannot be washed by switching or source exit"),FMath::IsNearlyEqual(F.P->Health,Before-7.f,.001f));
    const auto Receipt=F.A()->FindReceipt(Hit.Receipt.Handle);if(!Receipt)return false;
    TestEqual(TEXT("Original receipt committed once"),Receipt->State,DWCombatCommit::EReceiptState::Committed);
    F.Advance(1.);TestTrue(TEXT("No repeated finalization"),FMath::IsNearlyEqual(F.P->Health,Before-7.f,.001f));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldAxeCounterTest,"DoughWorld.Combat.ShieldAxe.CounterConsumedAtSwingOnceForWhiffOrMultipleTargets",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldAxeCounterTest::RunTest(const FString&)
{
    DWShieldTests::FFixture F;if(!F.Ready||!F.Raise())return false;
    F.Hit(1.f);F.W()->SecondaryReleased();const FVector Aim=F.P->GetActorLocation()+FVector(500,0,0);
    if(!TestTrue(TEXT("Formal swing without animation"),F.W()->PrimaryPressed(Aim)))return false;
    F.W()->PublishAcceptedWeaponStartEffects();F.W()->CancelAction();
    TestEqual(TEXT("Cancelled windup does not spend counter"),F.W()->GetShieldCounterCharges(),F.C.CounterHits);
    F.Advance(F.C.Attack.IntervalSeconds+.02);
    F.Front->Health=1000.f;auto* Second=F.Spawn<ADWEnemyCharacter>(FVector(100,50,500));if(!Second)return false;Second->Health=1000.f;
    if(!F.W()->PrimaryPressed(Aim))return false;F.W()->PublishAcceptedWeaponStartEffects();F.Advance(F.C.Attack.IntervalSeconds+.02);
    const float Damage=static_cast<float>(F.C.Attack.Damage*F.C.CounterMultiplier);
    TestTrue(TEXT("First target uses reinforced damage"),FMath::IsNearlyEqual(F.Front->Health,1000.f-Damage,.001f));
    TestTrue(TEXT("Second target shares same reinforced packet"),FMath::IsNearlyEqual(Second->Health,1000.f-Damage,.001f));
    TestEqual(TEXT("Multiple targets consume one charge"),F.W()->GetShieldCounterCharges(),F.C.CounterHits-1);
    F.Front->SetActorLocation(FVector(5000,0,500));Second->SetActorLocation(FVector(5000,100,500));
    if(!F.W()->PrimaryPressed(Aim))return false;F.W()->PublishAcceptedWeaponStartEffects();F.Advance(F.C.Attack.IntervalSeconds+.02);
    TestEqual(TEXT("Whiff consumes one charge at active entry"),F.W()->GetShieldCounterCharges(),F.C.CounterHits-2);
    TestTrue(TEXT("Switch clears the remainder"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed));TestEqual(TEXT("No cross-weapon counter"),F.W()->GetShieldCounterCharges(),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldTakeoverTest,"DoughWorld.Combat.ShieldAxe.GuardTakesOverSwingPreservesLockAndRequiresRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldTakeoverTest::RunTest(const FString&)
{
    DWShieldTests::FFixture F;if(!F.Ready)return false;const FVector Aim=F.P->GetActorLocation()+FVector(400,0,0);
    if(!F.W()->PrimaryPressed(Aim))return false;F.W()->PublishAcceptedWeaponStartEffects();const double Lock=F.W()->GetRemainingAttackLockSeconds();
    TestTrue(TEXT("Raise can take over unfinished swing"),F.Raise());TestFalse(TEXT("Swing ended"),F.W()->IsFormalAxeActive());
    TestTrue(TEXT("Original attack lock retained"),F.W()->GetRemainingAttackLockSeconds()>=Lock);
    TestTrue(TEXT("Guard locks movement independently of animation"),F.W()->LocksMovement());
    TestFalse(TEXT("Holding right does not reopen perfect"),F.W()->SecondaryPressed());
    F.Advance(F.C.Attack.IntervalSeconds+.01);TestFalse(TEXT("Left cannot automatically lower held guard"),F.W()->PrimaryPressed(Aim));
    F.W()->SecondaryReleased();TestFalse(TEXT("Release ends defense immediately"),F.W()->IsBlocking());
    TestTrue(TEXT("After release and original lock expiry attack allowed"),F.W()->PrimaryPressed(Aim));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldSaveGateTest,"DoughWorld.Combat.ShieldAxe.UnfinishedSaveSchemaCannotErasePendingOrCounter",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldSaveGateTest::RunTest(const FString&)
{
    DWShieldTests::FFixture F;if(!F.Ready)return false;
    TStrongObjectPtr<UDWSaveGame> Clean(NewObject<UDWSaveGame>());F.P->CaptureSaveData(Clean.Get());
    if(!TestTrue(TEXT("Clean resource capture validates"),DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*Clean,F.Error)))return false;
    const auto Hit=F.Hit(7.f);const auto Life=F.A()->CaptureActorLife(F.P);const float Before=F.P->Health;
    TStrongObjectPtr<UDWSaveGame> Pending(NewObject<UDWSaveGame>());F.P->CaptureSaveData(Pending.Get());
    TestTrue(TEXT("Capture marks missing shield schema"),Pending->CombatPlayerResources.bRequiresShieldStateSchema);
    TestFalse(TEXT("Existing file-write validator rejects partial snapshot"),DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*Pending,F.Error));
    TestFalse(TEXT("Loading old clean data cannot wash pending hit"),F.P->TryApplySaveData(Clean.Get()));
    TestEqual(TEXT("Rejected restore keeps health"),F.P->Health,Before);
    TestEqual(TEXT("Rejected restore keeps life"),F.A()->CaptureActorLife(F.P).Life,Life.Life);
    TestEqual(TEXT("Rejected restore keeps pending packet"),F.A()->GetPendingShieldDamageCount(F.P),1);
    if(!F.Raise())return false;
    const auto Receipt=F.A()->FindReceipt(Hit.Receipt.Handle);if(!Receipt)return false;
    TestEqual(TEXT("Packet can still be parried after refused restore"),Receipt->State,DWCombatCommit::EReceiptState::Rejected);
    TestFalse(TEXT("Loading old data cannot erase counter qualification"),F.P->TryApplySaveData(Clean.Get()));
    TestEqual(TEXT("Counter intact after refused restore"),F.W()->GetShieldCounterCharges(),F.C.CounterHits);
    F.P->CaptureSaveData(Pending.Get());TestFalse(TEXT("Counter capture also cannot pass write validator"),DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*Pending,F.Error));
    F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed);F.Advance(F.C.ReopenCooldownSeconds+.01);
    F.P->CaptureSaveData(Pending.Get());TestTrue(TEXT("Settled state is no longer save-blocked"),DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*Pending,F.Error));
    return true; // Only transient objects; no slot is opened, created, written or deleted.
}

namespace DWShieldLegacyRouteTests
{
bool ConfigureActualLegacySwing(DWShieldTests::FFixture& F,float Damage=20.f)
{
    if(!F.Ready||F.Front->CombatProfile!=EDWEnemyCombatProfile::Legacy)return false;
    F.Front->bRequireLineOfSight=false;F.Front->bShowCombatDebug=false;F.Front->PatrolRadius=0.f;
    F.Front->AttackVariants.Reset();F.Front->AttackDamage=Damage;F.Front->AttackRange=150.f;
    F.Front->AttackWindup=.04f;F.Front->AttackActiveDuration=.3f;F.Front->AttackRecovery=.1f;
    F.Front->AttackInterval=1.2f;return true;
}
void AdvanceActualEnemy(DWShieldTests::FFixture& F,float Seconds)
{F.World->TimeSeconds+=Seconds;F.Front->Tick(Seconds);}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldLegacyOrdinaryTest,"DoughWorld.Combat.ShieldAxe.LegacyEnemyActualSwingUsesOrdinaryBlockAndOneReceipt",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldLegacyOrdinaryTest::RunTest(const FString&)
{
    using namespace DWShieldLegacyRouteTests;DWShieldTests::FFixture F;
    if(!TestTrue(TEXT("Legacy enemy and formal player fixture"),ConfigureActualLegacySwing(F)))return false;
    if(!TestTrue(TEXT("Actual guard accepted"),F.Raise()))return false;
    F.Advance(F.C.ParryBeforeSeconds+.01);const float Before=F.P->Health;
    TestTrue(TEXT("Public legacy attack start accepts the real player hurtbox"),F.Front->TryStartMeleeAttack(F.P));
    AdvanceActualEnemy(F,.041f);
    TestEqual(TEXT("Actual old enemy enters its active phase"),F.Front->AttackPhase,EDWCombatPhase::Active);
    TestEqual(TEXT("Actual hit is held by the formal after-window"),F.A()->GetPendingShieldDamageCount(F.P),1);
    TestEqual(TEXT("No early subtraction before the after-window"),F.P->Health,Before);
    AdvanceActualEnemy(F,.01f);
    TestEqual(TEXT("Repeated actual active ticks retain one pending impact"),F.A()->GetPendingShieldDamageCount(F.P),1);
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe(NewObject<UDWCombatAuthorityTestProbe>());int32 Notices=0;
    F.P->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    Probe->AnyCallback=[&](AActor* Target,float,AController*,AActor*){if(Target==F.P){++Notices;F.Front->CancelMeleeAttack();}};
    F.Advance(F.C.ParryAfterSeconds+.01);
    const float Expected=Before-20.f*static_cast<float>(1.-F.C.BlockReduction);
    TestTrue(TEXT("Actual legacy melee receives configured ordinary reduction"),FMath::IsNearlyEqual(F.P->Health,Expected,.001f));
    TestEqual(TEXT("Reduced damage notifies exactly once despite reentrant source cancellation"),Notices,1);
    TestEqual(TEXT("Source cancellation has ended the actual swing"),F.Front->AttackPhase,EDWCombatPhase::None);
    F.Advance(1.);TestEqual(TEXT("No delayed second payment after source cancellation"),F.P->Health,Expected);
    TestEqual(TEXT("No pending receipt remains"),F.A()->GetPendingShieldDamageCount(F.P),0);
    F.P->OnTakeAnyDamage.RemoveDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);Probe->AnyCallback=nullptr;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldLegacyPerfectTest,"DoughWorld.Combat.ShieldAxe.LegacyEnemyActualSwingSupportsBothPerfectWindows",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldLegacyPerfectTest::RunTest(const FString&)
{
    using namespace DWShieldLegacyRouteTests;
    for(const bool After:{false,true})
    {
        DWShieldTests::FFixture F;if(!TestTrue(TEXT("Real legacy fixture"),ConfigureActualLegacySwing(F)))return false;
        const float Before=F.P->Health;
        if(!After&&!TestTrue(TEXT("Before-window guard accepted"),F.Raise()))return false;
        if(!TestTrue(TEXT("Public actual melee accepted"),F.Front->TryStartMeleeAttack(F.P)))return false;
        AdvanceActualEnemy(F,.041f);
        if(After)
        {
            TestEqual(TEXT("An actual unguarded front impact remains pending"),F.A()->GetPendingShieldDamageCount(F.P),1);
            F.Advance(F.C.ParryAfterSeconds*.5);
            if(!TestTrue(TEXT("Late guard within actual after-window accepted"),F.Raise()))return false;
        }
        TestEqual(TEXT("Actual melee perfect causes no health subtraction"),F.P->Health,Before);
        TestEqual(TEXT("Actual melee perfect grants exact formal counter count"),F.W()->GetShieldCounterCharges(),F.C.CounterHits);
        TestEqual(TEXT("Perfect rejects the one actual impact"),F.A()->GetPendingShieldDamageCount(F.P),0);
        AdvanceActualEnemy(F,.01f);F.Advance(F.C.ParryAfterSeconds+.02);
        TestEqual(TEXT("Active repeats and expired window cannot replay perfect damage"),F.P->Health,Before);
        TestEqual(TEXT("Repeated active frames do not stack counter"),F.W()->GetShieldCounterCharges(),F.C.CounterHits);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShieldLegacyClassifyTest,"DoughWorld.Combat.ShieldAxe.LegacyEnemyFixDoesNotMakeBackOrGenericDamageBlockable",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShieldLegacyClassifyTest::RunTest(const FString&)
{
    using namespace DWShieldLegacyRouteTests;DWShieldTests::FFixture F;
    if(!TestTrue(TEXT("Legacy fixture"),ConfigureActualLegacySwing(F,3.f)))return false;
    F.Front->SetActorLocation(FVector(-100,0,500));const float Before=F.P->Health;
    if(!TestTrue(TEXT("Front-facing player raises"),F.Raise())||!TestTrue(TEXT("Real rear enemy swing accepted"),F.Front->TryStartMeleeAttack(F.P)))return false;
    AdvanceActualEnemy(F,.041f);
    TestEqual(TEXT("A rear actual melee hit is still full damage"),F.P->Health,Before-3.f);
    TestEqual(TEXT("Rear hit never becomes shield pending"),F.A()->GetPendingShieldDamageCount(F.P),0);
    TestEqual(TEXT("Rear hit grants no perfect counter"),F.W()->GetShieldCounterCharges(),0);
    F.Front->CancelMeleeAttack();F.Front->SetActorLocation(FVector(100,0,500));
    const float Applied=UGameplayStatics::ApplyDamage(F.P,4.f,nullptr,F.Front,UDamageType::StaticClass());
    TestEqual(TEXT("Unknown ApplyDamage from an enemy stays unclassified"),Applied,4.f);
    TestEqual(TEXT("Unknown damage remains full instead of being guessed as melee"),F.P->Health,Before-7.f);
    TestEqual(TEXT("Generic damage still grants no counter"),F.W()->GetShieldCounterCharges(),0);
    return true;
}

#endif
