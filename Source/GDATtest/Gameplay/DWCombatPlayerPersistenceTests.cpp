#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatPlayerPersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatEquipmentTestFixture.h"
#include "DWCombatShotgun.h"
#include "DWCombatRules.h"
#include "DWCombatTestWorld.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWWeaponResourceTestProbe.h"
#include "DWSaveGame.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include <limits>
namespace DWPlayerPersistenceTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;
    UDWCombatAuthoritySubsystem* A=nullptr;TArray<FDWCombatObjectBinding> Bindings;
    TStrongObjectPtr<UDWSaveGame> Save{NewObject<UDWSaveGame>()};FDWCombatEffectsSave Effects;
    FDWCombatEffectsRestorePlan EffectPlan;FDWCombatPlayerRestorePlan PlayerPlan;FString Error;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D22PlayerPersistence"));if(!World)return;
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        P=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Spawn);
        if(!P)return;if(!P->HasActorBegunPlay())P->DispatchBeginPlay();A=UDWCombatAuthoritySubsystem::Get(P);
        Bindings.Add({TEXT("Player"),P});if(A)A->CaptureActorLife(P);
    }
    ~FFixture()
    {if(!World)return;for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
     GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
    UDWWeaponAnimationComponent* W()const{return P?P->WeaponAnimationComponent.Get():nullptr;}
    bool Capture()
    {
        return A&&FDWCombatEffectsPersistence::Capture(*A,Bindings,Effects,Error)&&
            FDWCombatPlayerPersistence::CaptureBase(*P,*Save,Error)&&
            FDWCombatPlayerPersistence::Capture(*P,Bindings,Save->CombatWorld.Player,Error)&&
            FDWCombatPlayerPersistence::Validate(*GI->GetCombatRules(),*Save,Effects,Error);
    }
    bool Prepare()
    {return FDWCombatEffectsPersistence::PrepareRestore(*A,Effects,Bindings,Bindings,{},EffectPlan,Error)&&
        FDWCombatPlayerPersistence::Prepare(*P,*Save,Effects,EffectPlan.Authority(),PlayerPlan,Error);}
    bool Commit()
    {
        if(!PlayerPlan.CanCommit(Error)||!EffectPlan.CanCommit(Error)||!A->BeginWorldRestoreBarrier(EffectPlan.Authority(),Error))return false;
        FDWCombatEffectsPersistence::CommitFactsUnchecked(EffectPlan);FDWCombatPlayerPersistence::AdoptNativeFacts(PlayerPlan);
        A->FinishWorldRestoreBarrier();FDWCombatEffectsPersistence::PublishRestoredEffects(EffectPlan);return true;
    }
    void Advance(double Dt){World->TimeSeconds+=static_cast<float>(Dt);W()->AdvanceAnimation(Dt);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWD22PlayerBaseTest,"DoughWorld.Combat.Persistence.Player.CompleteBaseAndMalformedState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWD22PlayerBaseTest::RunTest(const FString&)
{
    using namespace DWPlayerPersistenceTests;FFixture F;if(!TestTrue(TEXT("Complete isolated capture"),F.Capture())){AddError(F.Error);return false;}
    TestFalse(TEXT("No active user slot"),F.GI->HasActiveSlot());
    TestEqual(TEXT("Authoritative health captured"),F.Save->CombatPlayerResources.Vitals.Health,double(F.P->Health));
    TestTrue(TEXT("No invented starting equipment"),F.Save->CombatPlayerResources.CurrentEquipmentId.IsNone()&&F.Save->CombatPlayerResources.OwnedPermanentWeaponIds.IsEmpty());
    TestFalse(TEXT("Complete candidate has no old missing-world guard"),F.Save->CombatPlayerResources.bRequiresReleasedEffectsSchema||F.Save->CombatPlayerResources.bRequiresRecoveryStateSchema||F.Save->CombatPlayerResources.bRequiresShieldStateSchema);
    const float Before=F.P->Health;
    for(int Case=0;Case<5;++Case)
    {
        TStrongObjectPtr<UDWSaveGame> Bad(DuplicateObject<UDWSaveGame>(F.Save.Get(),GetTransientPackage()));
        if(Case==0)Bad->CombatWorld.Player.WeaponLockRemaining=std::numeric_limits<double>::quiet_NaN();
        if(Case==1)Bad->CombatPlayerResources.CurrentEquipmentId=TEXT("DWWeapon_DualDaggers");
        if(Case==2)Bad->CombatWorld.Player.Defense.CounterCharges=MAX_int32;
        if(Case==3){Bad->CombatWorld.Player.bRooted=true;Bad->CombatWorld.Player.RootAction=1;Bad->CombatWorld.Player.RootReceipt=1;}
        if(Case==4)Bad->CombatPlayerResources.bRequiresShieldStateSchema=true;
        TestFalse(FString::Printf(TEXT("Malformed case %d rejected before mutation"),Case),FDWCombatPlayerPersistence::Validate(*F.GI->GetCombatRules(),*Bad,F.Effects,F.Error));
        TestEqual(TEXT("Reject does not change live health"),F.P->Health,Before);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWD22PlayerStaleTest,"DoughWorld.Combat.Persistence.Player.PreparedRevisionAndTimeGuard",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWD22PlayerStaleTest::RunTest(const FString&)
{
    using namespace DWPlayerPersistenceTests;
    for(int Case=0;Case<3;++Case)
    {
        FFixture F;if(!TestTrue(TEXT("Prepare stable candidate"),F.Capture()&&F.Prepare())){AddError(F.Error);return false;}
        TestTrue(TEXT("Fresh candidate commit eligible"),F.PlayerPlan.CanCommit(F.Error));
        if(Case==0)TestTrue(TEXT("Real inventory mutation"),F.P->Inventory->TryAddItem(TEXT("Alcohol"),1));
        if(Case==1)F.World->TimeSeconds+=.01f;
        if(Case==2)TestTrue(TEXT("Actual new unarmed action"),F.P->PerformMeleeAttack(FVector(400,0,500)));
        TestFalse(FString::Printf(TEXT("Stale revision/time/action %d rejects adoption"),Case),F.PlayerPlan.CanCommit(F.Error));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWD22PlayerCommitTest,"DoughWorld.Combat.Persistence.Player.NativeCommitBeforeNotifications",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWD22PlayerCommitTest::RunTest(const FString&)
{
    using namespace DWPlayerPersistenceTests;FFixture F;
    if(!TestTrue(TEXT("Explicit shotgun ownership/current fixture"),DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*F.W(),*F.GI->GetCombatRules(),EDWAnimatedWeapon::Shotgun,F.Error)))return false;
    auto Weapon=F.W()->GetCombatWeaponStateSnapshot();Weapon.LoadedShotgunShells=2;
    if(!TestTrue(TEXT("Legal initial loaded fact"),F.W()->RestoreCombatWeaponState(Weapon,*F.GI->GetCombatRules(),F.Error))||
        !TestTrue(TEXT("Real reserve"),F.P->Inventory->TryAddItem(TEXT("DWAmmo_BreadShell"),3)))return false;
    FDWShotgunConfig T;if(!FDWShotgunConfig::Build(*F.GI->GetCombatRules(),T,F.Error))return false;
    TestTrue(TEXT("Actual committed shot before save"),F.W()->PrimaryPressed(FVector(400,0,500)));F.Advance(T.Windup+.001);
    TestEqual(TEXT("Shot paid once"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
    if(!TestTrue(TEXT("Capture while recovery is unfinished"),F.Capture())){AddError(F.Error);return false;}
    const double Lock=F.Save->CombatWorld.Player.WeaponLockRemaining;const uint64 OldEpoch=F.A->GetWorldEpoch();
    F.W()->CancelAction();F.P->Health=43.f;
    if(!TestTrue(TEXT("Prepare complete state"),F.Prepare())){AddError(F.Error);return false;}
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());int Notices=0;
    Probe->TestHook=[&]()
    {
        ++Notices;TestEqual(TEXT("Listener sees saved HP"),F.P->Health,F.Save->Health);
        TestEqual(TEXT("Listener sees committed loaded shell count"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
        TestEqual(TEXT("Reserve snapshot preserved"),F.P->Inventory->CountItem(TEXT("DWAmmo_BreadShell")),3);
        TestFalse(TEXT("Unfinished shot is not resumed"),F.W()->IsBusy());TestTrue(TEXT("Listener sees new world epoch"),F.A->GetWorldEpoch()!=OldEpoch);
    };
    F.P->Inventory->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Single barrier adopts all native facts"),F.Commit());TestEqual(TEXT("Adoption emits no inventory callback"),Notices,0);
    TestFalse(TEXT("Player remains gated before world publication"),F.P->IsCombatActionAllowed());
    TestTrue(TEXT("Saved weapon lock retained"),FMath::IsNearlyEqual(F.W()->GetRemainingAttackLockSeconds(),Lock,1.e-6));
    TestTrue(TEXT("Position and visuals publish while gated"),FDWCombatPlayerPersistence::Publish(F.PlayerPlan,F.Error));
    TestEqual(TEXT("Visual publication emits no inventory callback"),Notices,0);TestFalse(TEXT("Gate remains closed"),F.P->IsCombatActionAllowed());
    TestTrue(TEXT("Aggregate final publication releases gate"),FDWCombatPlayerPersistence::FinishPublish(F.PlayerPlan,F.Error));
    TestEqual(TEXT("One final inventory notice"),Notices,1);TestTrue(TEXT("Player input eligible again"),F.P->IsCombatActionAllowed());
    TestFalse(TEXT("Consumed player plan cannot commit twice"),F.PlayerPlan.CanCommit(F.Error));
    F.Advance(Lock+.01);TestEqual(TEXT("Old recovery cannot refund or shoot a second shell"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWD22PlayerNoticeDestroyTest,"DoughWorld.Combat.Persistence.Player.FinalNoticeDestroyStopsContinuation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWD22PlayerNoticeDestroyTest::RunTest(const FString&)
{
    using namespace DWPlayerPersistenceTests;FFixture F;
    if(!TestTrue(TEXT("Prepare and adopt isolated empty state"),F.Capture()&&F.Prepare()&&F.Commit()&&FDWCombatPlayerPersistence::Publish(F.PlayerPlan,F.Error))){AddError(F.Error);return false;}
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Inventory(NewObject<UDWInventoryTransactionTestProbe>());
    TStrongObjectPtr<UDWWeaponResourceTestProbe> Weapon(NewObject<UDWWeaponResourceTestProbe>());int I=0,W=0;
    Inventory->TestHook=[&](){++I;F.P->Destroy();};Weapon->TestHook=[&](int32,int64,bool){++W;};
    F.P->Inventory->OnChanged.AddDynamic(Inventory.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.W()->OnWeaponResourceChanged.AddDynamic(Weapon.Get(),&UDWWeaponResourceTestProbe::OnResources);
    TestFalse(TEXT("Committed restore reports interrupted publication, not rollback"),FDWCombatPlayerPersistence::FinishPublish(F.PlayerPlan,F.Error));
    TestEqual(TEXT("Actual first notice ran"),I,1);TestTrue(TEXT("Owner actually destroyed"),F.P->IsActorBeingDestroyed());
    TestEqual(TEXT("No stale weapon continuation after destruction"),W,0);TestFalse(TEXT("No duplicate adoption"),F.PlayerPlan.CanCommit(F.Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWD22DashCooldownRestoreTest,"DoughWorld.Combat.Persistence.Player.DashCooldownWithoutRestartOrRefund",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWD22DashCooldownRestoreTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWYeastDashConfig C;
    if(!FDWCombatRules::LoadProjectPayload(Rules,Error)||!FDWYeastDashConfig::Build(*Rules,C,Error))return false;
    FDWYeastDashPlan Active;TestTrue(TEXT("Pure active paid-motion fixture"),Active.Start(C,C.SpecialMax,C.FormDuration,FVector2D(1,0),FVector2D(1,0),1));
    auto Captured=Active;TestTrue(TEXT("Snapshot cancellation enters normal cooldown"),Captured.End(EDWYeastDashEnd::Cancelled));
    TestTrue(TEXT("Capture does not cancel live dash"),Active.IsActive());
    FDWYeastDashPlan Restored;TestTrue(TEXT("Fresh plan accepts configured remaining cooldown"),Restored.RestoreCooldown(C,Captured.CooldownSeconds()));
    TestFalse(TEXT("Restore never resumes held movement"),Restored.IsActive()||Restored.IsHeld());
    TestEqual(TEXT("No recreated input ticket"),Restored.OriginPress(),uint64(0));
    TestEqual(TEXT("Remaining cooldown retained"),Restored.CooldownSeconds(),C.CooldownSeconds);
    TestFalse(TEXT("Over-limit cooldown rejected atomically"),Restored.RestoreCooldown(C,C.CooldownSeconds+1.));
    TestEqual(TEXT("Invalid restore leaves prior cooldown"),Restored.CooldownSeconds(),C.CooldownSeconds);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWD22PlayerKnockbackRestoreTest,"DoughWorld.Combat.Persistence.Player.KnockbackStopsWithoutDamageOrPositionRefund",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWD22PlayerKnockbackRestoreTest::RunTest(const FString&)
{
    using namespace DWPlayerPersistenceTests;FFixture F;if(!F.P||!F.A)return false;
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Enemy=F.World->SpawnActor<ADWEnemyCharacter>(ADWEnemyCharacter::StaticClass(),FVector(-1000,0,500),FRotator::ZeroRotator,Spawn);
    if(!Enemy)return false;if(!Enemy->HasActorBegunPlay())Enemy->DispatchBeginPlay();F.Bindings.Add({TEXT("KBSource"),Enemy});
    FDWCombatDamageCommand Command;Command.Source=Enemy;Command.Target=F.P;Command.TargetLife=F.A->CaptureActorLife(F.P);Command.Amount=5.f;
    Command.Identity={F.A->BeginAction(Enemy),1,DWCombatCommit::EActionDamageKind::EnemyMelee,1};
    Command.KnockbackDirection=FVector::ForwardVector;Command.KnockbackDistance=120.f;Command.KnockbackSeconds=.2f;
    const auto Hit=F.A->SubmitActionDamage(Command);F.A->EndAction(Command.Identity.Action);
    TestEqual(TEXT("Real enemy packet commits damage once"),Hit.AppliedThisCall,5.f);
    const FVector Before=F.P->GetActorLocation();F.World->TimeSeconds+=.05f;F.P->Tick(.05f);
    TestTrue(TEXT("Actual Sweep has moved part of accepted knockback"),FVector::Dist2D(Before,F.P->GetActorLocation())>29.9);
    TestTrue(TEXT("Remaining knockback still active at capture"),F.P->IsCombatKnockedBack());
    if(!TestTrue(TEXT("Capture is allowed during stable active knockback"),F.Capture())){AddError(F.Error);return false;}
    TestTrue(TEXT("Capture itself leaves live knockback running"),F.P->IsCombatKnockedBack());
    const FVector SavedPosition=F.Save->PlayerTransform.GetLocation();const float SavedHealth=F.Save->Health;
    if(!TestTrue(TEXT("Prepare/adopt captured displacement boundary"),F.Prepare()&&F.Commit())){AddError(F.Error);return false;}
    TestFalse(TEXT("Native adopt stops only remaining knockback"),F.P->IsCombatKnockedBack());
    TestEqual(TEXT("Already received damage is retained"),F.P->Health,SavedHealth);
    TestEqual(TEXT("Damage is neither refunded nor applied again"),F.P->Health,95.f);
    if(!TestTrue(TEXT("Publish completes saved position and clears gate"),FDWCombatPlayerPersistence::Publish(F.PlayerPlan,F.Error)&&FDWCombatPlayerPersistence::FinishPublish(F.PlayerPlan,F.Error))){AddError(F.Error);return false;}
    TestTrue(TEXT("Already moved position is retained"),F.P->GetActorLocation().Equals(SavedPosition,.01));
    F.World->TimeSeconds+=.2f;F.P->Tick(.2f);
    TestTrue(TEXT("Old remaining movement never resumes"),F.P->GetActorLocation().Equals(SavedPosition,.01));
    TestEqual(TEXT("No second health settlement after restore"),F.P->Health,SavedHealth);return true;
}
#endif
