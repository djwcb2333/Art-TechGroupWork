#if WITH_DEV_AUTOMATION_TESTS
#include "DWPlayerCharacter.h"
#include "DWYeastDashTestPlayer.h"
#include "DWCombatTestWorld.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatPlayerSave.h"
#include "DWWeaponAnimationComponent.h"
#include "DWEnemyCharacter.h"
#include "DWSaveGame.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include <cmath>
namespace DWYeastDashRuntimeTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWSaveGame> Save{NewObject<UDWSaveGame>()};
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe{NewObject<UDWCombatAuthorityTestProbe>()};
    UWorld* World=nullptr;ADWYeastDashTestPlayer* P=nullptr;FDWYeastDashConfig C;FDWCombatPlayerVitalsConfig V;FString Error;bool Ready=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D09DashFixture"));if(!World)return;
        P=Spawn<ADWYeastDashTestPlayer>();if(!P||!P->HasCombatVitalsReady()||!FDWYeastDashConfig::Build(*GI->GetCombatRules(),C,Error)||
            !FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),V,Error))return;
        P->CaptureSaveData(Save.Get());auto& S=Save->CombatPlayerResources.Vitals;
        S.BaseForm=EDWCombatBaseForm::Yeast;S.FormRemainingSeconds=V.FormDurationSeconds;S.FormTimeCarry=0.;S.Special=V.Vitals.SpecialMax;S.SpecialGrowthCarry=0.;
        Save->bYeastForm=true;Save->Transformation=static_cast<float>(S.Special);
        Ready=P->TryApplySaveData(Save.Get())&&!GI->HasActiveSlot();
        if(Ready)P->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    }
    ~FFixture()
    {
        Probe->AnyCallback=nullptr;if(P)P->DashStartedCallback=nullptr;if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn(FVector Location=FVector(0,0,500))
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<T>(T::StaticClass(),Location,FRotator::ZeroRotator,Params);if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    static FDWCombatInputPress Press(uint64 Sequence)
    {FDWCombatInputPress I;I.Edge.Context={1,1,1};I.Edge.Sequence=Sequence;I.Edge.Key=TEXT("SpaceBar");I.Edge.bDown=true;I.Action=TEXT("Dash");return I;}
    bool Start(uint64 Sequence=1)
    {if(!P->TryStartFormalDash(Press(Sequence),FVector(1,0,0),FVector(0,1,0)))return false;P->PublishFormalDashStartEffects();return true;}
    void Advance(float Dt){World->TimeSeconds+=Dt;P->TickGameplayClocks(Dt,false);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalDashShortRuntimeTest,"DoughWorld.Combat.YeastDash.RuntimeShortPressAndWeaponExclusion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalDashShortRuntimeTest::RunTest(const FString&)
{
    using namespace DWYeastDashRuntimeTests;FFixture F;if(!TestTrue(TEXT("Isolated ready player"),F.Ready))return false;
    const FVector Start=F.P->GetActorLocation();const double Before=F.P->GetCombatSpecial();
    F.P->PerformDash();TestFalse(TEXT("Blueprint compatibility entry cannot fabricate physical press"),F.P->IsFormalDashing());
    if(!TestTrue(TEXT("Native authenticated input fixture accepts"),F.Start()))return false;
    TestTrue(TEXT("Only start fee committed"),FMath::IsNearlyEqual(F.P->GetCombatSpecial(),Before-F.C.StartCost,1.e-10));
    TestFalse(TEXT("Direct melee cannot run during dash"),F.P->PerformMeleeAttack(Start+FVector(500,0,0)));
    TestFalse(TEXT("Invalid duplicate press cannot pay again"),F.Start());
    F.P->ApplyFormalDashRelease(FFixture::Press(1));F.Advance(static_cast<float>(F.C.MinimumSeconds));
    TestFalse(TEXT("Short press finishes at minimum"),F.P->IsFormalDashing());
    TestTrue(TEXT("Continuous movement covers minimum distance"),FMath::IsNearlyEqual((F.P->GetActorLocation()-Start).Size2D(),F.C.SpeedCmPerSecond*F.C.MinimumSeconds,.01));
    TestTrue(TEXT("Net resource rate, without charging gate twice"),FMath::IsNearlyEqual(F.P->GetCombatSpecial(),Before-F.C.MinimumBudget(),1.e-5));
    TestTrue(TEXT("Cooldown starts at finished segment"),FMath::IsNearlyEqual(F.P->GetFormalDashCooldown(),F.C.CooldownSeconds,1.e-8));
    TestFalse(TEXT("Fresh press during cooldown rejected"),F.Start(2));F.Advance(static_cast<float>(F.C.CooldownSeconds+.01));
    TestFalse(TEXT("Held/replayed request does not auto-start"),F.Start(2));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalDashWallRuntimeTest,"DoughWorld.Combat.YeastDash.RuntimeWallAndExplicitControl",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalDashWallRuntimeTest::RunTest(const FString&)
{
    using namespace DWYeastDashRuntimeTests;FFixture F;if(!F.Ready)return false;
    auto* Wall=F.Spawn<AActor>();auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(10,500,500));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();Box->SetWorldLocation(F.P->GetActorLocation()+FVector(300,0,0));
    const FVector Start=F.P->GetActorLocation();if(!F.Start())return false;F.Advance(static_cast<float>(F.C.MinimumSeconds));
    TestFalse(TEXT("Wall stops dash before minimum"),F.P->IsFormalDashing());
    TestTrue(TEXT("Capsule sweep cannot cross wall"),F.P->GetActorLocation().X<=Start.X+300.-10.-F.P->GetCapsuleComponent()->GetScaledCapsuleRadius()+.1);
    TestTrue(TEXT("No minimum-distance compensation through wall"),(F.P->GetActorLocation()-Start).Size2D()<F.C.SpeedCmPerSecond*F.C.MinimumSeconds);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalDashPauseRuntimeTest,"DoughWorld.Combat.YeastDash.ResourcePausePreservesPaidMotionAndRealRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalDashPauseRuntimeTest::RunTest(const FString&)
{
    using namespace DWYeastDashRuntimeTests;FFixture F;if(!F.Ready)return false;
    if(F.V.Vitals.WaterDamagePerEvent<=0.){AddInfo(TEXT("No configured dehydration event; callback scenario not applicable."));return true;}
    const float Dt=.125f;auto& V=F.Save->CombatPlayerResources.Vitals;
    V.Water=F.V.Vitals.WaterFloor;V.WaterEmptyElapsedSeconds=F.V.Vitals.WaterDamagePeriodSeconds-Dt;
    V.WaterEmptyTimeCarry=Dt-(F.V.Vitals.WaterDamagePeriodSeconds-V.WaterEmptyElapsedSeconds);
    if(V.WaterEmptyElapsedSeconds<0.||!F.P->TryApplySaveData(F.Save.Get()))return false;
    auto* Pause=F.Spawn<APlayerState>();F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*){F.World->GetWorldSettings()->SetPauserPlayerState(Pause);};
    const FVector Start=F.P->GetActorLocation();if(!F.Start())return false;
    F.Advance(Dt);if(!TestTrue(TEXT("Resource callback paused"),F.World->IsPaused()))return false;
    const double Special=F.P->GetCombatSpecial(),Form=F.P->GetCombatFormRemaining();
    TestTrue(TEXT("Motion held pending after resource-time debit"),F.P->GetActorLocation().Equals(Start));
    F.P->TickGameplayClocks(1.f,false);TestEqual(TEXT("Pause spends no further special"),F.P->GetCombatSpecial(),Special);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    TestTrue(TEXT("Native seam replays already-observed release before resume"),F.P->ApplyFormalDashRelease(FFixture::Press(1)));
    F.P->TickGameplayClocks(0.f,false);
    TestTrue(TEXT("Paid motion catches up with zero new resource time"),FMath::IsNearlyEqual((F.P->GetActorLocation()-Start).Size2D(),F.C.SpeedCmPerSecond*Dt,.01));
    TestEqual(TEXT("No double debit"),F.P->GetCombatSpecial(),Special);TestEqual(TEXT("No double form time"),F.P->GetCombatFormRemaining(),Form);
    TestTrue(TEXT("Short release still owes the minimum movement segment"),F.P->IsFormalDashing());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalDashContextRuntimeTest,"DoughWorld.Combat.YeastDash.ContextResetRetainsCooldownAndLegacyRollCannotBypass",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalDashContextRuntimeTest::RunTest(const FString&)
{
    using namespace DWYeastDashRuntimeTests;FFixture F;if(!F.Ready)return false;
    F.P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);F.P->StartForwardRoll();
    TestFalse(TEXT("Direct inherited roll cannot bypass formal player's gate"),F.P->bIsRolling);
    auto* Template=F.Spawn<ATopDownActionCharacter>(FVector(0,1500,500));Template->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Template->StartForwardRoll();TestTrue(TEXT("Independent legacy TopDown template retains its roll"),Template->bIsRolling);
    FDWCombatInputLedger Input;FDWCombatInputEdge Edge;FDWCombatInputPress Press;
    TestTrue(TEXT("Real ledger context"),Input.ResetContext({1,1,1}));Input.ObserveKey(Input.GetContext(),TEXT("SpaceBar"),true,false,Edge);
    if(!Input.TryConsumePress(Edge,TEXT("Dash"),Press)||!F.P->TryStartFormalDash(Press,FVector::ForwardVector,FVector::RightVector))return false;
    F.P->CancelCombatActions();const double Cooldown=F.P->GetFormalDashCooldown();
    TestTrue(TEXT("Real life context rotation"),Input.ResetContext({1,2,1}));
    Input.ObserveKey(Input.GetContext(),TEXT("SpaceBar"),false,false,Edge);Input.ObserveKey(Input.GetContext(),TEXT("SpaceBar"),true,false,Edge);
    if(!Input.TryConsumePress(Edge,TEXT("Dash"),Press))return false;
    TestFalse(TEXT("New life context cannot clear current cooldown"),F.P->TryStartFormalDash(Press,FVector::ForwardVector,FVector::RightVector));
    TestEqual(TEXT("CD preserved across context"),F.P->GetFormalDashCooldown(),Cooldown);
    F.Advance(static_cast<float>(Cooldown+.01));
    // A newer adapter context can legally begin at a smaller edge number. It gets a native monotonic request ordinal.
    auto Fresh=FFixture::Press(1);Fresh.Edge.Context={1,2,2};
    TestTrue(TEXT("New binding context lower sequence is accepted"),F.P->TryStartFormalDash(Fresh,FVector::ForwardVector,FVector::RightVector));
    F.P->CancelCombatActions();F.Advance(static_cast<float>(Cooldown+.01));
    TestFalse(TEXT("Old context cannot be replayed after newer binding"),F.P->TryStartFormalDash(Press,FVector::ForwardVector,FVector::RightVector));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalDashDamageRuntimeTest,"DoughWorld.Combat.YeastDash.OrdinaryDamageContinuesExplicitKnockbackStops",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalDashDamageRuntimeTest::RunTest(const FString&)
{
    using namespace DWYeastDashRuntimeTests;FFixture F;if(!F.Ready||!F.Start())return false;
    const float Before=F.P->Health;
    UGameplayStatics::ApplyDamage(F.P,1.f,nullptr,nullptr,UDamageType::StaticClass());
    TestTrue(TEXT("Dash grants no invulnerability"),F.P->Health<Before);
    TestTrue(TEXT("Ordinary damage does not end the dash"),F.P->IsFormalDashing());
    auto* Enemy=F.Spawn<ADWEnemyCharacter>(FVector(0,1500,500));auto* A=UDWCombatAuthoritySubsystem::Get(F.P);if(!A||!Enemy)return false;
    FDWCombatDamageCommand Command;Command.Identity={A->BeginAction(Enemy),1,DWCombatCommit::EActionDamageKind::EnemyMelee,1};
    Command.Source=Enemy;Command.Target=F.P;Command.TargetLife=A->CaptureActorLife(F.P);Command.Amount=1.f;
    Command.KnockbackDirection=FVector::RightVector;Command.KnockbackDistance=50.f;Command.KnockbackSeconds=.2f;
    const auto Applied=A->SubmitActionDamage(Command);
    TestTrue(TEXT("Explicit enemy damage committed"),Applied.AppliedThisCall>0.f);
    TestTrue(TEXT("Knockback facts stop dash before notices"),!F.P->IsFormalDashing()&&F.P->IsCombatKnockedBack());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalDashCrossingRuntimeTest,"DoughWorld.Combat.YeastDash.EnemyPassThroughOneHitAndFlatGround",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalDashCrossingRuntimeTest::RunTest(const FString&)
{
    using namespace DWYeastDashRuntimeTests;FFixture F;if(!F.Ready)return false;
    const FVector Start=F.P->GetActorLocation();
    auto* Floor=F.Spawn<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(10000,10000,10));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionResponseToAllChannels(ECR_Block);
    Box->RegisterComponent();Box->SetWorldLocation(Start-FVector(0,0,F.P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+12.));
    auto* Enemy=F.Spawn<ADWEnemyCharacter>(Start+FVector(150,0,0));if(!Enemy)return false;
    Enemy->Health=FMath::Max(1000.f,static_cast<float>(F.C.Damage*3.));
    const float Before=Enemy->Health;
    if(!F.Start())return false;
    F.Advance(static_cast<float>(300./F.C.SpeedCmPerSecond));
    TestTrue(TEXT("Flat floor does not stop horizontal dash"),F.P->IsFormalDashing());
    TestTrue(TEXT("Solid enemy capsule is pass-through during dash"),F.P->GetActorLocation().X>Enemy->GetActorLocation().X);
    TestTrue(TEXT("Enemy receives configured independent dash damage"),FMath::IsNearlyEqual(Enemy->Health,Before-static_cast<float>(F.C.Damage),.001f));
    const float Once=Enemy->Health;
    // Move the runtime target into the next segment; it remains the same actor/life.
    Enemy->SetActorLocation(F.P->GetActorLocation()+FVector(80,0,0));F.Advance(static_cast<float>(160./F.C.SpeedCmPerSecond));
    TestEqual(TEXT("Same enemy cannot receive a second packet in this dash"),Enemy->Health,Once);
    TestFalse(TEXT("Temporary movement ignore restored after sweep"),F.P->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Enemy));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalDashStartPauseRuntimeTest,"DoughWorld.Combat.YeastDash.StartNotificationPausesAfterAcceptedHold",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalDashStartPauseRuntimeTest::RunTest(const FString&)
{
    using namespace DWYeastDashRuntimeTests;FFixture F;if(!F.Ready)return false;
    FDWCombatInputLedger Input;FDWCombatInputEdge Edge;FDWCombatInputPress Press;int32 Notices=0;bool HeldAtNotice=false;
    auto* Pause=F.Spawn<APlayerState>();
    F.P->DashStartedCallback=[&]()
    {
        ++Notices;HeldAtNotice=Input.IsHeld(TEXT("Dash"));Input.BeginSuspension();
        F.World->GetWorldSettings()->SetPauserPlayerState(Pause);
    };
    F.P->PublishFormalDashStartEffects();TestEqual(TEXT("No committed action cannot fabricate a startup notice"),Notices,0);
    Input.ResetContext({1,1,1});Input.ObserveKey(Input.GetContext(),TEXT("SpaceBar"),true,false,Edge);
    if(!Input.TryConsumePress(Edge,TEXT("Dash"),Press)||!F.P->TryStartFormalDash(Press,FVector::ForwardVector,FVector::RightVector))return false;
    TestEqual(TEXT("Native accepted commit does not publish early"),Notices,0);
    if(!Input.ActivateHold(Press))return false;
    F.P->PublishFormalDashStartEffects();
    TestTrue(TEXT("Real OnDashStarted callback sees accepted hold before pausing"),HeldAtNotice&&Notices==1&&F.World->IsPaused());
    TestTrue(TEXT("Already-paid dash survives its startup pause"),F.P->IsFormalDashing());
    const double Special=F.P->GetCombatSpecial(),Form=F.P->GetCombatFormRemaining();
    F.P->TickGameplayClocks(1.f,false);TestEqual(TEXT("Startup pause freezes resource time"),F.P->GetCombatSpecial(),Special);
    TestEqual(TEXT("Startup pause freezes form time"),F.P->GetCombatFormRemaining(),Form);
    Input.ObserveKey(Input.GetContext(),TEXT("SpaceBar"),false,false,Edge);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    const auto Releases=Input.ResumeAndDrainReleases();if(!TestEqual(TEXT("Actual observed Up survives menu"),Releases.Num(),1))return false;
    F.P->ApplyFormalDashRelease(Releases[0].Press);F.P->PublishFormalDashStartEffects();
    TestEqual(TEXT("Presentation is once-only after resume"),Notices,1);
    const FVector Start=F.P->GetActorLocation();F.Advance(static_cast<float>(F.C.MinimumSeconds));
    TestFalse(TEXT("Released startup still completes minimum once"),F.P->IsFormalDashing());
    TestTrue(TEXT("Minimum movement is not lost to startup pause"),FMath::IsNearlyEqual((F.P->GetActorLocation()-Start).Size2D(),F.C.MinimumSeconds*F.C.SpeedCmPerSecond,.01));
    F.Advance(static_cast<float>(F.C.CooldownSeconds+.01));
    if(!TestTrue(TEXT("Second native accepted action awaits its publication"),F.P->TryStartFormalDash(FFixture::Press(2),FVector::ForwardVector,FVector::RightVector)))return false;
    TestTrue(TEXT("Runtime restore invalidates old publish qualification"),F.P->TryApplySaveData(F.Save.Get()));
    F.P->PublishFormalDashStartEffects();TestEqual(TEXT("Old life cannot publish again"),Notices,1);return true;
}
#endif
