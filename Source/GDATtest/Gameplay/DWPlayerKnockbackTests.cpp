#if WITH_DEV_AUTOMATION_TESTS
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatOverlapTestProbe.h"
#include "DWGameInstance.h"
#include "DWSaveGame.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipmentTestFixture.h"
#include "DWWeaponAnimationSet.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace DWPlayerKnockbackTests
{
struct FScope
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;ADWEnemyCharacter* Enemy=nullptr;
    FScope()
    {
        if(!GEngine)return;const FString Saved=FCommandLine::Get();
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/D06_Knockback")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
        FCommandLine::Set(*FString::Printf(TEXT("-DWVerificationSavePrefix=DW_QA_D06_KB_ -DWSaveTestDirectory=\"%s\""),*Directory));
        GI.Reset(NewObject<UDWGameInstance>(GEngine));GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWKnockbackTest")),GetTransientPackage());
        FCommandLine::Set(*Saved);World=GI->GetWorld();if(!World)return;
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        Player=Spawn<ADWPlayerCharacter>(FVector(0,0,500));Enemy=Spawn<ADWEnemyCharacter>(FVector(-1000,0,500));
    }
    ~FScope(){if(World){World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
    template<class T>T* Spawn(FVector P)
    {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* A=World->SpawnActor<T>(T::StaticClass(),P,FRotator::ZeroRotator,S);if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;}
    bool Ready()const{return Player&&Enemy&&Player->HasCombatVitalsReady();}
    float Hit(float Distance,float Duration,FVector Direction=FVector::ForwardVector)
    {
        auto* A=UDWCombatAuthoritySubsystem::Get(Player);if(!A)return 0.f;
        FDWCombatDamageCommand C;C.Source=Enemy;C.Target=Player;C.TargetLife=A->CaptureActorLife(Player);C.Amount=1.f;
        C.Identity={A->BeginAction(Enemy),1,DWCombatCommit::EActionDamageKind::EnemyMelee,1};
        C.KnockbackDirection=Direction;C.KnockbackDistance=Distance;C.KnockbackSeconds=Duration;
        const auto R=A->SubmitActionDamage(C);A->EndAction(C.Identity.Action);return R.AppliedThisCall;
    }
    void Advance(float Dt){World->TimeSeconds+=Dt;Player->Tick(Dt);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerKBStrongestTest,"DoughWorld.Combat.PlayerKnockback.StrongestPauseAndWeaponContinuation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerKBStrongestTest::RunTest(const FString&)
{
    DWPlayerKnockbackTests::FScope F;if(!TestTrue(TEXT("Isolated initialized player"),F.Ready()))return false;
    auto* C=F.Player->WeaponAnimationComponent.Get();auto* Set=NewObject<UDWWeaponAnimationSet>(F.Player);
    Set->Weapon=EDWAnimatedWeapon::Spear;Set->bApplyBasicDamage=false;
    // A legacy presentation-only clock has no damage/resource events. Formal daggers now
    // correctly disallow PlayRole, so they cannot stand in for a ten-second synthetic clock.
    FDWWeaponAnimationClip Clip;Clip.Role=TEXT("Attack");Clip.DurationSeconds=10.f;Clip.Animation=NewObject<UAnimSequence>(Set);Set->Clips.Add(Clip);
    C->AnimationSets.Reset();C->AnimationSets.Add(Set);C->bEnabled=true;FString EquipmentError;
    if(!TestTrue(TEXT("Strict owned/current synthetic clock fixture"),DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*C,*F.GI->GetCombatRules(),EDWAnimatedWeapon::Spear,EquipmentError)))return false;
    TestTrue(TEXT("Start synthetic non-resource weapon clock"),C->PlayRole(Clip.Role,F.Player->GetActorLocation()+FVector(300,0,0)));
    const int32 Serial=C->ActionSerial;
    TestEqual(TEXT("First hit"),F.Hit(60,.2f),1.f);TestEqual(TEXT("Stronger hit"),F.Hit(150,.35f,FVector::RightVector),1.f);
    TestEqual(TEXT("Weaker hit still damages independently"),F.Hit(100,.2f),1.f);
    TestEqual(TEXT("No universal injury invulnerability"),F.Player->Health,97.f);
    TestTrue(TEXT("Knockback does not cancel weapon"),C->IsBusy());TestEqual(TEXT("No replacement weapon action"),C->ActionSerial,Serial);
    const FVector Before=F.Player->GetActorLocation();auto* Pauser=F.Spawn<APlayerState>(FVector::ZeroVector);
    F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);F.Player->Tick(.8f);
    TestTrue(TEXT("Paused delta cannot advance displacement"),F.Player->GetActorLocation().Equals(Before));
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.Advance(.35f);
    TestTrue(TEXT("Only strongest 150 cm direction survives, not 310 cm sum"),F.Player->GetActorLocation().Equals(Before+FVector(0,150,0),.01));
    TestFalse(TEXT("Exact duration completes"),F.Player->IsCombatKnockedBack());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerKBWallRollTest,"DoughWorld.Combat.PlayerKnockback.WallTruncatesAndPaidDashInterruptedBeforeNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerKBWallRollTest::RunTest(const FString&)
{
    DWPlayerKnockbackTests::FScope F;if(!TestTrue(TEXT("Ready"),F.Ready()))return false;
    // Real in-memory resource restore establishes Yeast; no production slot is created or written.
    FString Error;FDWCombatPlayerVitalsConfig Vitals;FDWYeastDashConfig Dash;
    if(!TestTrue(TEXT("Formal rules available"),F.GI->GetCombatRules()!=nullptr)||
        !TestTrue(TEXT("Formal vitals configuration"),FDWCombatPlayerVitalsConfig::Build(*F.GI->GetCombatRules(),Vitals,Error))||
        !TestTrue(TEXT("Formal dash configuration"),FDWYeastDashConfig::Build(*F.GI->GetCombatRules(),Dash,Error)))return false;
    TStrongObjectPtr<UDWSaveGame> DashSave(NewObject<UDWSaveGame>());F.Player->CaptureSaveData(DashSave.Get());
    auto& SavedVitals=DashSave->CombatPlayerResources.Vitals;
    SavedVitals.BaseForm=EDWCombatBaseForm::Yeast;SavedVitals.FormRemainingSeconds=Vitals.FormDurationSeconds;
    SavedVitals.FormTimeCarry=0.;SavedVitals.Special=Vitals.Vitals.SpecialMax;SavedVitals.SpecialGrowthCarry=0.;
    DashSave->bYeastForm=true;DashSave->Transformation=static_cast<float>(SavedVitals.Special);
    if(!TestTrue(TEXT("Real isolated Yeast restore"),F.Player->TryApplySaveData(DashSave.Get())))return false;
    FDWCombatInputLedger Input;FDWCombatInputEdge Edge;FDWCombatInputPress Press;
    Input.ResetContext({1,1,1});Input.ObserveKey(Input.GetContext(),TEXT("SpaceBar"),true,false,Edge);
    const double SpecialBefore=F.Player->GetCombatSpecial();
    if(!TestTrue(TEXT("Physical press ticket consumed once"),Input.TryConsumePress(Edge,TEXT("Dash"),Press))||
        !TestTrue(TEXT("Formal paid dash really starts"),F.Player->TryStartFormalDash(Press,FVector::ForwardVector,FVector::RightVector))||
        !TestTrue(TEXT("Accepted hold precedes presentation"),Input.ActivateHold(Press)))return false;
    F.Player->PublishFormalDashStartEffects();
    if(!TestTrue(TEXT("Formal dash active before knockback"),F.Player->IsFormalDashing()))return false;
    TestTrue(TEXT("Configured start fee was actually committed"),FMath::IsNearlyEqual(F.Player->GetCombatSpecial(),SpecialBefore-Dash.StartCost,1.e-10));
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe(NewObject<UDWCombatAuthorityTestProbe>());int32 Notices=0;
    Probe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {++Notices;TestFalse(TEXT("Paid dash native fact ended before damage callback"),F.Player->IsFormalDashing());TestFalse(TEXT("Legacy movement mirror also ended before callback"),F.Player->bIsRolling);TestTrue(TEXT("Displacement fact visible before callback"),F.Player->IsCombatKnockedBack());};
    F.Player->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    auto* Wall=F.Spawn<AActor>(FVector(100,0,500));auto* Box=NewObject<UBoxComponent>(Wall);Wall->AddInstanceComponent(Box);Wall->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5,200,200));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();Wall->SetActorLocation(FVector(100,0,500));const FVector Before=F.Player->GetActorLocation();
    const float HealthBeforeHit=F.Player->Health;
    const float ExpectedAfterHit=HealthBeforeHit-static_cast<float>(Vitals.YeastIncomingMultiplier);
    TestTrue(TEXT("Hit admitted with configured Yeast incoming multiplier"),FMath::IsNearlyEqual(F.Hit(150,.35f),HealthBeforeHit-ExpectedAfterHit,1.e-4f));F.Advance(.35f);
    const double Travel=FVector::Dist2D(Before,F.Player->GetActorLocation());
    TestTrue(TEXT("Swept capsule stops at wall"),Travel>0.&&Travel<100.);TestFalse(TEXT("Blocked knockback ends"),F.Player->IsCombatKnockedBack());
    TestTrue(TEXT("Wall adds no extra damage"),FMath::IsNearlyEqual(F.Player->Health,ExpectedAfterHit,1.e-4f));TestEqual(TEXT("One actual damage notice"),Notices,1);
    F.Player->OnTakeAnyDamage.RemoveDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);Probe->AnyCallback=nullptr;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerKBRestoreTest,"DoughWorld.Combat.PlayerKnockback.SweepOverlapRestoreStopsOldTick",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerKBRestoreTest::RunTest(const FString&)
{
    DWPlayerKnockbackTests::FScope F;if(!TestTrue(TEXT("Ready"),F.Ready()))return false;
    TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());F.Player->CaptureSaveData(Save.Get());
    F.Player->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    auto* Trigger=F.Spawn<AActor>(FVector(100,0,500));auto* Box=NewObject<UBoxComponent>(Trigger);Trigger->AddInstanceComponent(Box);Trigger->SetRootComponent(Box);
    Box->SetBoxExtent(FVector(5,200,200));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionObjectType(ECC_WorldDynamic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);Box->SetGenerateOverlapEvents(true);Box->RegisterComponent();Trigger->SetActorLocation(FVector(100,0,500));
    TStrongObjectPtr<UDWCombatOverlapTestProbe> Probe(NewObject<UDWCombatOverlapTestProbe>());int32 Restores=0;
    Probe->Callback=[&](AActor*,AActor* Other){if(Other==F.Player){++Restores;TestTrue(TEXT("Real in-memory full player restore during Sweep"),F.Player->TryApplySaveData(Save.Get()));}};
    Trigger->OnActorBeginOverlap.AddDynamic(Probe.Get(),&UDWCombatOverlapTestProbe::Overlap);
    F.Hit(120,.2f);F.Advance(.2f);
    TestEqual(TEXT("One actual overlap restore"),Restores,1);TestEqual(TEXT("New health retained"),F.Player->Health,Save->Health);
    TestFalse(TEXT("Old movement cannot reactivate after restore"),F.Player->IsCombatKnockedBack());
    TestTrue(TEXT("Saved transform retained when old Tick returns"),F.Player->GetActorTransform().Equals(Save->PlayerTransform,.01));
    Trigger->OnActorBeginOverlap.RemoveDynamic(Probe.Get(),&UDWCombatOverlapTestProbe::Overlap);Probe->Callback=nullptr;
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWKBLateStrongerTest,"DoughWorld.Combat.PlayerKnockback.LateSameFrameStrongerRespectsTotalTravel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWKBLateStrongerTest::RunTest(const FString&)
{
    DWPlayerKnockbackTests::FScope F;if(!TestTrue(TEXT("Ready"),F.Ready()))return false;
    const FVector Before=F.Player->GetActorLocation();const uint64 Frame=GFrameCounter;
    F.Hit(100.f,.2f);F.Advance(.04f);TestTrue(TEXT("Initial player displacement really advances"),FVector::Dist2D(Before,F.Player->GetActorLocation())>19.9);
    F.Hit(120.f,.2f);F.Advance(.2f);
    TestTrue(TEXT("Player total path length stays 120 rather than 140"),FMath::IsNearlyEqual(FVector::Dist2D(Before,F.Player->GetActorLocation()),120.,.01));
    F.Enemy->CombatProfile=EDWEnemyCombatProfile::MoldGrunt;FString Error;
    if(!TestTrue(TEXT("Formal enemy initialized"),F.Enemy->InitializeFormalCombat(*F.GI->GetCombatRules(),Error)))return false;
    const FVector EnemyBefore=F.Enemy->GetActorLocation();F.Enemy->ApplyCombatKnockback(FVector::ForwardVector,100.f,.2f);
    F.World->TimeSeconds+=.04f;F.Enemy->Tick(.04f);F.Enemy->ApplyCombatKnockback(FVector::ForwardVector,120.f,.2f);
    F.World->TimeSeconds+=.2f;F.Enemy->Tick(.2f);
    TestTrue(TEXT("Enemy total path length also stays 120"),FMath::IsNearlyEqual(FVector::Dist2D(EnemyBefore,F.Enemy->GetActorLocation()),120.,.01));
    TestEqual(TEXT("Fixture did not advance engine frame identity"),GFrameCounter,Frame);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalFatalIdentityTest,"DoughWorld.Combat.FormalEnemy.FatalAttributionFrozenBeforeSourceEndPlay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalFatalIdentityTest::RunTest(const FString&)
{
    DWPlayerKnockbackTests::FScope F;if(!TestTrue(TEXT("Ready"),F.Ready()))return false;
    F.Enemy->CombatProfile=EDWEnemyCombatProfile::MoldGrunt;FString Error;
    if(!TestTrue(TEXT("Formal enemy initialized"),F.Enemy->InitializeFormalCombat(*F.GI->GetCombatRules(),Error)))return false;
    auto* A=UDWCombatAuthoritySubsystem::Get(F.Player);const auto SourceLife=A->CaptureActorLife(F.Player);
    FDWCombatDamageCommand C;C.Source=F.Player;C.Target=F.Enemy;C.TargetLife=A->CaptureActorLife(F.Enemy);C.Amount=100.f;
    C.Identity={A->BeginAction(F.Player),1,DWCombatCommit::EActionDamageKind::Melee,1};
    const auto Pending=A->AdmitActionDamage(C);TestEqual(TEXT("Admission is pending"),Pending.Receipt.State,DWCombatCommit::EReceiptState::Pending);
    F.Player->Destroy();
    TestEqual(TEXT("Already-admitted hit can settle after source EndPlay"),A->ResolvePendingDamage(Pending.Receipt.Handle,DWCombatCommit::EOutcome::Committed).AppliedThisCall,100.f);
    const auto Save=F.Enemy->CaptureFormalCombatSave();
    TestEqual(TEXT("Fatal source entity preserved from admission"),Save.CreditedPlayerEntityId,SourceLife.Entity);
    TestEqual(TEXT("Fatal source old life preserved, not recaptured"),Save.CreditedPlayerLife,SourceLife.Life);
    TestEqual(TEXT("Fatal receipt preserved"),Save.FatalReceiptId,Pending.Receipt.Handle.Id);
    TestTrue(TEXT("D07 terminal decision committed before notifications; physical actor materializes later"),Save.DropDecision==EDWEnemyCoreDropDecision::Failed||Save.DropDecision==EDWEnemyCoreDropDecision::SpawnPending);
    return true;
}
#endif
