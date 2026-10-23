#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatSword.h"
#include "DWCombatPlayerSave.h"
#include "DWSaveGame.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "DWCombatFeedbackComponent.h"
#include "Animation/AnimSequence.h"
#include "DWCombatRules.h"
#include "DWCombatEquipment.h"
#include "DWCombatTestWorld.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWDamageVolumeLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"
#include <limits>
namespace DWSwordTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;
    FDWSwordTuning T;FString Error;
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe{NewObject<UDWCombatAuthorityTestProbe>()};
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D14Sword"));if(!World)return;
        P=Spawn<ADWPlayerCharacter>(FVector(0,0,500));
        if(P){P->SetActorTickEnabled(false);P->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);}
        if(GI->GetCombatRules())FDWSwordTuning::Build(*GI->GetCombatRules(),T,Error);
    }
    ~FFixture()
    {
        Probe->AnyCallback=nullptr;if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class C>C* Spawn(FVector At)
    {FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* A=World->SpawnActor<C>(C::StaticClass(),At,FRotator::ZeroRotator,Params);if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;}
    UDWWeaponAnimationComponent* W()const{return P?P->WeaponAnimationComponent.Get():nullptr;}
    bool Ready()const{return P&&P->HasCombatVitalsReady()&&W()&&T.IsValid()&&!GI->HasActiveSlot();}
    bool Equip()
    {
        auto* A=UDWCombatAuthoritySubsystem::Get(P);const auto Action=A->BeginAction(P);
        FDWCombatEquipmentGrantRequest R{Action,A->CaptureActorLife(P),1,W()->GetCombatWeaponRestoreEpoch(),TEXT("DWWeapon_Greatsword")};
        const bool Granted=FDWCombatEquipmentTransaction::Grant(*W(),R).bCommittedThisCall;A->EndAction(Action);
        if(!Granted||!W()->SelectWeapon(EDWAnimatedWeapon::Greatsword))return false;
        Advance(W()->GetRemainingAttackLockSeconds()+.01);return true;
    }
    void Advance(double Dt){World->TimeSeconds+=static_cast<float>(Dt);W()->AdvanceAnimation(Dt);}
    FDWCombatInputPress Press(uint64 Sequence,FName Action=TEXT("Primary"),FName Key=TEXT("LeftMouseButton"))const
    {return {{{1,1,1},Sequence,Key,true,false},Action};}
    bool Start()
    {
        if(!W()->PrimaryPressed(P->GetActorLocation()+FVector(450,0,0)))return false;
        if(!W()->BindObservedInputPress(Press(10),false))return false;
        W()->PublishAcceptedWeaponStartEffects();return true;
    }
    bool ReleaseCharged()
    {W()->PrimaryReleased();return W()->AdvanceAnimation(0.);}
    bool ChargeAndRelease(){Advance(T.ChargeSeconds);return ReleaseCharged();}
    ADWEnemyCharacter* Enemy(FVector Offset)
    {
        auto* E=Spawn<ADWEnemyCharacter>(P->GetActorLocation()+Offset);if(!E)return nullptr;
        E->MaxHealth=300.f;E->SetHealthForLoad(300.f);E->SetActorTickEnabled(false);
        E->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        auto* Box=NewObject<UDWDamageHitboxComponent>(E);E->AddInstanceComponent(Box);Box->SetupAttachment(E->GetRootComponent());
        Box->SetBoxExtent(FVector(20));Box->RegisterComponent();Box->SetWorldLocation(E->GetActorLocation());
        E->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);return E;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordRulesClockTest,"DoughWorld.Combat.Greatsword.FormalFivePhasesAndDamageSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordRulesClockTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWSwordTuning T;
    if(!TestTrue(TEXT("Approved payload and sword bridge"),FDWCombatRules::LoadProjectPayload(Rules,Error)&&FDWSwordTuning::Build(*Rules,T,Error)))return false;
    double Meters=0.;Rules->TryGetNumber(TEXT("weapon.sword.range_m"),EDWCombatRuleUnit::Meters,Meters,Error);
    TestEqual(TEXT("Exactly one metres to centimetres conversion"),T.RangeCm,Meters*100.);
    FDWSwordAttackPlan P;TestTrue(TEXT("Start pure charge"),P.Start(T));double Used=0.,Left=0.,Total=0.;
    TestFalse(TEXT("No early active boundary"),P.EnterNextPhase(EDWSpearForm::Normal));
    P.AdvanceCurrentPhase(T.ChargeSeconds,Used,Left);Total+=Used;
    TestTrue(TEXT("Full charge remains a ready charge"),P.IsChargeReady()&&P.GetPhase()==EDWSwordPhase::Charge);
    TestFalse(TEXT("A timed boundary cannot automatically release"),P.EnterNextPhase(EDWSpearForm::Yeast));
    TestFalse(TEXT("Ready hold has no timed boundary"),FMath::IsFinite(P.GetRemainingSeconds()));
    P.AdvanceCurrentPhase(100.,Used,Left);
    TestTrue(TEXT("Waiting consumes no attack time and cannot reach a damage phase"),Used==0.&&Left==100.&&P.GetDamagePhase()==0);
    TestTrue(TEXT("Only explicit release enters slam"),P.ReleaseCharge(EDWSpearForm::Yeast)&&P.GetPhase()==EDWSwordPhase::Slam);
    TestEqual(TEXT("Damage freezes at first release"),P.GetDamage(),T.SlamDamage*T.YeastDamageMultiplier);
    P.AdvanceCurrentPhase(T.SlamSeconds,Used,Left);Total+=Used;P.EnterNextPhase(EDWSpearForm::Normal);
    TestEqual(TEXT("Gap unscaled"),P.GetRemainingSeconds(),T.GapSeconds);
    P.AdvanceCurrentPhase(T.GapSeconds,Used,Left);Total+=Used;P.EnterNextPhase(EDWSpearForm::Normal);
    TestEqual(TEXT("Sweep retains first-release multiplier after form ends"),P.GetDamage(),T.SweepDamage*T.YeastDamageMultiplier);
    TestEqual(TEXT("Sweep effective window unscaled"),P.GetRemainingSeconds(),T.SweepSeconds);
    P.AdvanceCurrentPhase(T.SweepSeconds,Used,Left);Total+=Used;P.EnterNextPhase(EDWSpearForm::Yeast);
    TestTrue(TEXT("Only recovery uses current yeast frequency"),FMath::IsNearlyEqual(P.GetRemainingSeconds(),T.RecoverySeconds/T.YeastRecoveryRate,1.e-10));
    P.AdvanceCurrentPhase(100.,Used,Left);Total+=Used;P.EnterNextPhase(EDWSpearForm::Normal);
    TestTrue(TEXT("Large delta ends once with carry"),P.GetPhase()==EDWSwordPhase::Complete&&Left>0.);
    TestTrue(TEXT("Current full normal rules total 3.52 seconds"),FMath::IsNearlyEqual(T.ChargeSeconds+T.SlamSeconds+T.GapSeconds+T.SweepSeconds+T.RecoverySeconds,3.52,1.e-9));
    FDWSwordAttackPlan Q;Q.Start(T);Used=7.;Left=8.;TestFalse(TEXT("NaN rejects without advancing"),Q.AdvanceCurrentPhase(std::numeric_limits<double>::quiet_NaN(),Used,Left));
    TestEqual(TEXT("Rejected output unchanged"),Used,7.);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordAutoReleaseTest,"DoughWorld.Combat.Greatsword.OwnershipNoArtGateAndOneComboPerHold",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordAutoReleaseTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!TestTrue(TEXT("Isolated real player ready"),F.Ready()))return false;
    TestFalse(TEXT("No free starter sword"),F.W()->SelectWeapon(EDWAnimatedWeapon::Greatsword));
    if(!TestTrue(TEXT("Explicit test grant and equipment selection"),F.Equip()))return false;
    TestTrue(TEXT("No animation set is a supported gameplay placeholder"),F.W()->AnimationSets.IsEmpty()&&F.W()->IsEnabled());
    TestFalse(TEXT("PlayRole cannot create legacy damage"),F.W()->PlayRole(TEXT("Attack"),F.P->GetActorLocation()));
    if(!TestTrue(TEXT("Accepted charge"),F.Start()))return false;
    F.Advance(F.T.ChargeSeconds);TestTrue(TEXT("Full hold waits for button up"),F.W()->IsFormalSwordChargeReady());
    const float ReadySource=F.W()->GetElapsedSeconds();F.Advance(5.);
    TestTrue(TEXT("Long held full charge remains harmless and busy"),F.W()->IsFormalSwordChargeReady()&&F.W()->IsBusy());
    TestEqual(TEXT("Ready presentation source stays frozen"),F.W()->GetElapsedSeconds(),ReadySource);
    TestFalse(TEXT("A held second press cannot restart"),F.W()->PrimaryPressed(F.P->GetActorLocation()+FVector(450,0,0)));
    TestTrue(TEXT("Matching physical release is recorded"),F.W()->ApplyObservedInputRelease(F.Press(10)));
    TestTrue(TEXT("Release queues a zero-time clock boundary"),F.W()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds())==0.);
    F.Advance(0.);TestTrue(TEXT("Release starts the slam through the gameplay clock"),F.W()->GetFormalSwordPhase()==EDWSwordPhase::Slam);
    F.Advance(F.T.SlamSeconds+F.T.GapSeconds+F.T.SweepSeconds+F.T.RecoverySeconds+.01);
    TestFalse(TEXT("Gameplay action completes without visual dependency"),F.W()->IsBusy());
    TestTrue(TEXT("Natural finish remains Complete rather than cancellation reset"),F.W()->GetFormalSwordPhase()==EDWSwordPhase::Complete);
    TestEqual(TEXT("Natural finish keeps its outgoing role for the existing blend"),F.W()->GetActiveRole(),FName(TEXT("Attack")));
    const int32 Serial=F.W()->ActionSerial;F.Advance(5.);TestEqual(TEXT("Held input cannot auto repeat"),F.W()->ActionSerial,Serial);
    TestTrue(TEXT("A new press can start the next combo after release"),F.W()->PrimaryPressed(F.P->GetActorLocation()+FVector(450,0,0)));
    TestFalse(TEXT("That new held request cannot restart itself"),F.W()->PrimaryPressed(F.P->GetActorLocation()+FVector(450,0,0)));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordMovementCancelTest,"DoughWorld.Combat.Greatsword.ChargeTurnsWithoutMovingAndPausedEarlyReleaseCancels",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordMovementCancelTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready()||!F.Equip()||!F.Start())return false;
    TestFalse(TEXT("Movement already down before charge cannot self-cancel"),F.W()->ApplyFormalSwordMovementPress(F.Press(9,TEXT("SwordMoveCancel"),TEXT("W"))));
    TestTrue(TEXT("Charge still locks movement"),F.W()->LocksMovement());
    TestFalse(TEXT("A later movement edge cannot cancel charge"),F.W()->ApplyFormalSwordMovementPress(F.Press(11,TEXT("SwordMoveCancel"),TEXT("W"))));
    const FVector Position=F.P->GetActorLocation();
    F.W()->UpdateFormalSwordChargeAim(Position+FVector(0,450,0));F.P->MoveCameraRelative(1.f,1.f);
    TestTrue(TEXT("Charging turns toward cursor target while staying movement locked"),F.P->GetActorForwardVector().Equals(FVector::RightVector,.001)&&F.W()->LocksMovement()&&F.P->GetActorLocation().Equals(Position));
    F.Advance(F.T.ChargeSeconds);F.W()->UpdateFormalSwordChargeAim(Position+FVector(-450,0,0));
    TestTrue(TEXT("Ready waiting still turns and cannot move"),F.W()->IsFormalSwordWaitingForRelease()&&F.P->GetActorForwardVector().Equals(-FVector::ForwardVector,.001)&&F.W()->LocksMovement());
    F.W()->CancelAction();if(!F.Start())return false;
    auto* Pauser=F.Spawn<APlayerState>(FVector::ZeroVector);F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    const double Left=F.W()->GetRemainingAttackLockSeconds();TestFalse(TEXT("Paused weapon clock refuses advance"),F.W()->AdvanceAnimation(100.));
    TestTrue(TEXT("Pause preserves charging phase"),F.W()->GetFormalSwordPhase()==EDWSwordPhase::Charge);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    TestTrue(TEXT("D02 matching release is processed before resumed clock"),F.W()->ApplyObservedInputRelease(F.Press(10)));
    F.Advance(10.);TestFalse(TEXT("No surprise slam after paused release"),F.W()->IsBusy());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordTwoStageDamageTest,"DoughWorld.Combat.Greatsword.IndependentHurtboxesGapAimAndPerStageReceipts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordTwoStageDamageTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready()||!F.Equip())return false;
    auto* Front=F.Enemy(FVector(220,0,0));auto* Side=F.Enemy(FVector(0,220,0));if(!Front||!Side||!F.Start())return false;
    F.ChargeAndRelease();TestEqual(TEXT("Slam hits its independent 80 degree target"),Front->Health,300.f-static_cast<float>(F.T.SlamDamage));
    TestEqual(TEXT("Side target outside slam is untouched"),Side->Health,300.f);
    F.W()->UpdateFormalSwordGapAim(F.P->GetActorLocation()+FVector(0,450,0));
    TestTrue(TEXT("Gap-only aim input cannot rotate an active slam"),F.P->GetActorForwardVector().Equals(FVector::ForwardVector,.001));
    F.Advance(F.T.SlamSeconds*.5);TestEqual(TEXT("Repeated active evaluation cannot duplicate first-stage damage"),Front->Health,300.f-static_cast<float>(F.T.SlamDamage));
    F.Advance(F.T.SlamSeconds*.5);TestTrue(TEXT("Gap is a distinct aim interval"),F.W()->IsFormalSwordGap());
    F.W()->UpdateFormalSwordGapAim(F.P->GetActorLocation()+FVector(0,450,0));F.Advance(F.T.GapSeconds);
    TestEqual(TEXT("Second independent 160 degree window uses new facing"),Side->Health,300.f-static_cast<float>(F.T.SweepDamage));
    TestEqual(TEXT("Old facing is outside the new sweep"),Front->Health,300.f-static_cast<float>(F.T.SlamDamage));
    F.W()->UpdateFormalSwordGapAim(F.P->GetActorLocation()+FVector(450,0,0));
    TestTrue(TEXT("Gap-only aim input cannot rotate an active sweep"),F.P->GetActorForwardVector().Equals(FVector::RightVector,.001));
    F.Advance(F.T.SweepSeconds);TestEqual(TEXT("Sweep duplicates are rejected by the same phase receipt"),Side->Health,300.f-static_cast<float>(F.T.SweepDamage));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordCancelAfterHitTest,"DoughWorld.Combat.Greatsword.AcceptedSwitchInsideDamageNoticeKeepsDamageAndOnlySwitchLock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordCancelAfterHitTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready()||!F.Equip())return false;auto* Enemy=F.Enemy(FVector(220,0,0));if(!Enemy||!F.Start())return false;
    int32 Calls=0;F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Calls;TestEqual(TEXT("Real dynamic listener sees committed slam health"),Enemy->Health,300.f-static_cast<float>(F.T.SlamDamage));
        TestTrue(TEXT("Legally owned empty-hand switch cancels followup"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed));
    };
    F.ChargeAndRelease();TestEqual(TEXT("One damage callback"),Calls,1);
    double SwitchLock=0.;FString Error;
    TestTrue(TEXT("Read the existing formal equipment switch lock"),DWCombatEquipment::BuildSwitchLock(*F.GI->GetCombatRules(),SwitchLock,Error));
    TestTrue(TEXT("Sword cancellation removes its cycle; the newly imposed switch lock remains"),FMath::IsNearlyEqual(F.W()->GetRemainingAttackLockSeconds(),SwitchLock,1.e-6));
    TestTrue(TEXT("Already committed hit still starts its independent knockback after the cancelling callback"),Enemy->bIsCombatKnockedBack);
    const FVector BeforePush=Enemy->GetActorLocation();Enemy->Tick(static_cast<float>(F.T.KnockbackSeconds));
    TestTrue(TEXT("Independent knockback still travels its formal unobstructed distance"),FMath::IsNearlyEqual(FVector::Dist2D(BeforePush,Enemy->GetActorLocation()),F.T.KnockbackCm,.01));
    F.Advance(10.);TestEqual(TEXT("No second hit after switch callback"),Enemy->Health,300.f-static_cast<float>(F.T.SlamDamage));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordGapCancelTest,"DoughWorld.Combat.Greatsword.GapMovementCancelsOnlyUnreleasedSweep",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordGapCancelTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready()||!F.Equip())return false;auto* Enemy=F.Enemy(FVector(220,0,0));if(!Enemy||!F.Start())return false;
    TestFalse(TEXT("Dash cannot cancel charge or actual striking phase"),F.W()->AllowsFormalSwordDashInterruption());
    F.ChargeAndRelease();F.Advance(F.T.SlamSeconds);TestTrue(TEXT("Gap allows qualified yeast dash request"),F.W()->AllowsFormalSwordDashInterruption());
    TestTrue(TEXT("Ordinary gap movement cancels remaining sweep"),F.W()->ApplyFormalSwordGapMovement());
    TestEqual(TEXT("Gap cancellation resets the remaining combo interval"),F.W()->GetRemainingAttackLockSeconds(),0.);F.Advance(10.);
    TestEqual(TEXT("First-stage damage remains and second never emits"),Enemy->Health,300.f-static_cast<float>(F.T.SlamDamage));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordCancellationResetTest,"DoughWorld.Combat.Greatsword.Feedback22CancellationResetsEverySwordPhase",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordCancellationResetTest::RunTest(const FString&)
{
    for(int32 Stage=0;Stage<6;++Stage)
    {
        DWSwordTests::FFixture F;if(!TestTrue(TEXT("Isolated sword fixture"),F.Ready()&&F.Equip()&&F.Start()))return false;
        if(Stage>=1)F.Advance(F.T.ChargeSeconds);
        if(Stage>=2)F.ReleaseCharged();
        if(Stage>=3)F.Advance(F.T.SlamSeconds);
        if(Stage>=4)F.Advance(F.T.GapSeconds);
        if(Stage>=5)F.Advance(F.T.SweepSeconds);
        const EDWSwordPhase Expected[]={EDWSwordPhase::Charge,EDWSwordPhase::Charge,EDWSwordPhase::Slam,EDWSwordPhase::Gap,EDWSwordPhase::Sweep,EDWSwordPhase::Recovery};
        TestEqual(TEXT("Ready wait is a distinct cancellable condition"),F.W()->IsFormalSwordChargeReady(),Stage==1);
        TestTrue(TEXT("Reached the cancellation phase"),F.W()->GetFormalSwordPhase()==Expected[Stage]);
        F.W()->CancelAction();
        TestTrue(TEXT("Cancelled plan is fully reset"),F.W()->GetFormalSwordPhase()==EDWSwordPhase::None);
        TestEqual(TEXT("No residual sword attack lock"),F.W()->GetRemainingAttackLockSeconds(),0.);
        TestEqual(TEXT("Outgoing elapsed time reset"),F.W()->GetElapsedSeconds(),0.f);
        TestTrue(TEXT("No outgoing sword role/sequence/tail"),F.W()->GetActiveRole().IsNone()&&!F.W()->GetActiveSequence()&&!F.W()->HasWeaponPresentation());
        TestTrue(TEXT("Fresh press immediately restarts from charge"),F.Start()&&F.W()->GetFormalSwordPhase()==EDWSwordPhase::Charge);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordOtherLockTest,"DoughWorld.Combat.Greatsword.Feedback22CancellationDoesNotClearOtherWeaponLocks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordOtherLockTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready()||!F.Equip()||!F.Start())return false;F.W()->CancelAction();
    if(!TestTrue(TEXT("Switch to empty hands"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed)))return false;
    const double SwitchLeft=F.W()->GetRemainingAttackLockSeconds();F.W()->CancelAction();
    TestEqual(TEXT("Idle generic cleanup preserves the switch lock"),F.W()->GetRemainingAttackLockSeconds(),SwitchLeft);
    F.Advance(SwitchLeft+.01);
    if(!TestTrue(TEXT("Start the independent unarmed action"),F.W()->PrimaryPressed(F.P->GetActorLocation()+FVector(450,0,0))))return false;
    const double UnarmedLeft=F.W()->GetRemainingAttackLockSeconds();TestTrue(TEXT("Unarmed action has a positive lock"),UnarmedLeft>0.);
    F.W()->CancelAction();TestEqual(TEXT("Cancelling another weapon retains its own established cooldown policy"),F.W()->GetRemainingAttackLockSeconds(),UnarmedLeft);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordDeathResetTest,"DoughWorld.Combat.Greatsword.Feedback22DeathCommitsSwordCancellationBeforeNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordDeathResetTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready()||!F.Equip()||!F.Start())return false;
    int32 Notices=0;F.P->OnTakeAnyDamage.AddDynamic(F.Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    F.Probe->AnyCallback=[&](AActor* Target,float,AController*,AActor*)
    {
        if(Target!=F.P)return;++Notices;
        TestTrue(TEXT("Fatal callback sees cancelled sword facts"),F.P->IsDead()&&F.W()->GetFormalSwordPhase()==EDWSwordPhase::None&&!F.W()->IsBusy());
        TestEqual(TEXT("Fatal callback sees no residual sword interval"),F.W()->GetRemainingAttackLockSeconds(),0.);
    };
    const float Requested=F.P->Health+1.f;const float Applied=F.P->TakeDamage(Requested,FDamageEvent(),nullptr,nullptr);
    TestTrue(TEXT("Real fatal damage committed"),Applied>0.f&&F.P->IsDead());
    TestEqual(TEXT("Real dynamic fatal callback ran exactly once"),Notices,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordReadyPauseTest,"DoughWorld.Combat.Greatsword.Feedback23ReadyWaitPauseAndRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordReadyPauseTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!TestTrue(TEXT("Ready fixture"),F.Ready()&&F.Equip()&&F.Start()))return false;
    auto* Enemy=F.Enemy(FVector(220,0,0));if(!TestNotNull(TEXT("Explicit target"),Enemy))return false;
    F.Advance(F.T.ChargeSeconds+50.);
    TestTrue(TEXT("Oversized charge delta cannot auto release"),F.W()->IsFormalSwordChargeReady());
    TestEqual(TEXT("Held ready has no damage"),Enemy->Health,300.f);
    const float Source=F.W()->GetElapsedSeconds();
    auto* Pause=F.Spawn<APlayerState>(FVector::ZeroVector);F.World->GetWorldSettings()->SetPauserPlayerState(Pause);
    TestFalse(TEXT("Pause refuses the weapon clock"),F.W()->AdvanceAnimation(100.));
    TestEqual(TEXT("Paused ready source remains unchanged"),F.W()->GetElapsedSeconds(),Source);
    TestFalse(TEXT("Ready cannot damage while paused"),F.W()->AdvanceAnimation(0.));
    TestEqual(TEXT("Still no paused damage"),Enemy->Health,300.f);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    TestTrue(TEXT("D02 observed release is applied before the resumed clock"),F.W()->ApplyObservedInputRelease(F.Press(10)));
    TestEqual(TEXT("Observed release requests a zero boundary"),F.W()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds()),0.);
    F.Advance(0.);
    TestTrue(TEXT("First resumed zero boundary enters slam"),F.W()->GetFormalSwordPhase()==EDWSwordPhase::Slam);
    TestEqual(TEXT("Resumed release commits one hit"),Enemy->Health,300.f-static_cast<float>(F.T.SlamDamage));
    F.Advance(0.);TestEqual(TEXT("Zero repeat cannot duplicate that hit"),Enemy->Health,300.f-static_cast<float>(F.T.SlamDamage));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordNormalizedImpactTest,"DoughWorld.Combat.Greatsword.SourcePhaseMappingAndNormalizedImpactCues",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordNormalizedImpactTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready())return false;
    auto* Set=NewObject<UDWWeaponAnimationSet>(F.W());Set->Weapon=EDWAnimatedWeapon::Greatsword;
    FDWWeaponAnimationClip Clip;Clip.Role=TEXT("Attack");Clip.DurationSeconds=227.f/60.f;Clip.Animation=NewObject<UAnimSequence>(Set);Set->Clips.Add(Clip);F.W()->AnimationSets.Add(Set);
    if(!F.Equip()||!F.Start())return false;
    F.Advance(F.T.ChargeSeconds*.5);
    TestTrue(TEXT("Half charge maps half authored charge regardless of source export length"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),.8,1.e-5));
    TestTrue(TEXT("AnimBP player getter and weapon sampler share the same source seconds"),FMath::IsNearlyEqual(static_cast<double>(F.P->GetMeleeAnimationElapsedSeconds()),F.W()->GetPresentationSourceSeconds(),1.e-5));
    F.Advance(F.T.ChargeSeconds*.5);F.ReleaseCharged();
    auto* Feedback=F.P->CombatFeedback.Get();if(!TestNotNull(TEXT("Real existing presentation component"),Feedback))return false;
    TestEqual(TEXT("Release does not shake before visual impact"),Feedback->SwordAttackCount,0);
    const double Marker=F.W()->SwordSlamFeedbackProgress;
    TestTrue(TEXT("Next native slice includes normalized visual marker"),FMath::IsNearlyEqual(F.W()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds()),F.T.SlamSeconds*Marker,1.e-8));
    F.Advance(F.T.SlamSeconds*Marker-1.e-5);TestEqual(TEXT("Not before impact"),Feedback->SwordAttackCount,0);
    F.Advance(1.e-5);TestEqual(TEXT("Exactly once at authored slam accent"),Feedback->SwordAttackCount,1);
    TestTrue(TEXT("Slam cue aligns with source 1.8 seconds"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),1.8,1.e-5));
    F.Advance(F.T.SlamSeconds*(1.-Marker)+F.T.GapSeconds);
    TestEqual(TEXT("Sweep entry waits for its own animation marker"),Feedback->SwordAttackCount,1);
    F.Advance(F.T.SweepSeconds*F.W()->SwordSweepFeedbackProgress);
    TestEqual(TEXT("Sweep gets a separate normalized cue"),Feedback->SwordAttackCount,2);
    TestTrue(TEXT("Sweep cue aligns source contact 2.633333 seconds"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),79./30.,1.e-5));
    F.Advance(F.T.SweepSeconds+F.T.RecoverySeconds);TestFalse(TEXT("Whole action ends"),F.W()->IsBusy());
    TestTrue(TEXT("Terminal source pose cannot jump backward or replay a tail"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),static_cast<double>(Clip.DurationSeconds),1.e-6));
    if(!F.Start())return false;F.W()->SwordSlamFeedbackProgress=.25;F.Advance(F.T.ChargeSeconds);F.ReleaseCharged();
    TestTrue(TEXT("Configured marker quarter follows the current phase duration"),FMath::IsNearlyEqual(F.W()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds()),F.T.SlamSeconds*.25,1.e-8));
    F.Advance(F.T.SlamSeconds*.25);TestEqual(TEXT("New action fires the configured marker once"),Feedback->SwordAttackCount,3);
    return true;
}

namespace DWSwordAuthoredHoldTests
{
UDWWeaponAnimationSet* AttachSource(DWSwordTests::FFixture& F,bool Duplicate=false)
{
    auto* Source=LoadObject<UDWWeaponAnimationSet>(nullptr,TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Greatsword.DA_DWHeroWeapon_Greatsword"));
    if(!Source)return nullptr;
    auto* Set=Duplicate?DuplicateObject<UDWWeaponAnimationSet>(Source,F.W()):Source;
    F.W()->AnimationSets.Add(Set);return Set;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordAuthoredHoldSamplingTest,"DoughWorld.Combat.Greatsword.AuthoredHoldSharesBonePropAndGeometryTime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordAuthoredHoldSamplingTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready())return false;auto* Set=DWSwordAuthoredHoldTests::AttachSource(F);
    const auto* Attack=Set?Set->FindClip(TEXT("Attack")):nullptr;const auto* Hold=Set?Set->FindClip(TEXT("ChargeHold")):nullptr;
    if(!TestTrue(TEXT("Existing authored Attack and one-second looping Hold are present"),Attack&&Hold&&Hold->Animation&&Hold->bLoop&&FMath::IsNearlyEqual(Hold->DurationSeconds,1.f)&&Hold->DamageEvents.IsEmpty()))return false;
    if(!F.Equip())return false;auto* Enemy=F.Enemy(FVector(220,0,0));if(!Enemy||!F.Start())return false;
    F.Advance(F.T.ChargeSeconds);
    TestTrue(TEXT("Real Ready still waits for release"),F.W()->IsFormalSwordWaitingForRelease());
    const FVector ReadyLocation=F.P->GetActorLocation();
    TestFalse(TEXT("A new W edge cannot cancel completed Ready"),F.W()->ApplyFormalSwordMovementPress(F.Press(11,TEXT("SwordMoveCancel"),TEXT("W"))));
    F.P->MoveCameraRelative(1.f,1.f);
    TestTrue(TEXT("Completed Ready discards movement without translating or cancelling"),F.P->GetActorLocation().Equals(ReadyLocation,1.e-6)&&F.P->GetPendingMovementInputVector().IsNearlyZero()&&F.W()->IsFormalSwordWaitingForRelease());
    TestEqual(TEXT("Gameplay role remains Attack"),F.W()->GetActiveRole(),FName(TEXT("Attack")));
    TestEqual(TEXT("Only selected presentation sequence changes"),F.W()->GetActiveSequence(),Hold->Animation.Get());
    TestEqual(TEXT("Authored Hold starts at source zero"),F.W()->GetPresentationSourceSeconds(),0.);
    const int32 Serial=F.W()->ActionSerial,Events=F.W()->DispatchedEventCount;
    const uint64 Boundary=F.W()->GetCombatBoundaryRevision();const auto Resource=F.W()->GetCombatWeaponResourceRevision();
    const float NativeElapsed=F.W()->GetElapsedSeconds();
    F.Advance(.25);
    TestTrue(TEXT("Quarter game second samples authored quarter source"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),.25,1.e-8));
    TestTrue(TEXT("Player AnimBP source shares exact Hold time"),FMath::IsNearlyEqual(static_cast<double>(F.P->GetMeleeAnimationElapsedSeconds()),.25,1.e-6));
    TestEqual(TEXT("AnimBP duration is the current whole authored Hold"),F.P->GetMeleeAnimationDuration(),Hold->DurationSeconds);
    TestTrue(TEXT("Sequence position maps the same whole Hold"),FMath::IsNearlyEqual(F.W()->GetPositionSeconds(),Hold->Animation->GetPlayLength()*.25,1.e-6));
    // Paint-only native component tick samples the exact same time without advancing any gameplay clock.
    F.W()->TickComponent(0.f,LEVELTICK_All,nullptr);
    const auto* Sample=Hold->VisualSamples.FindByPredicate([](const auto& V){return FMath::IsNearlyEqual(V.TimeSeconds,.25f,1.e-6f);});
    const auto* Geometry=Hold->GeometryFrames.FindByPredicate([](const auto& V){return FMath::IsNearlyEqual(V.TimeSeconds,.25f,1.e-6f);});
    UStaticMeshComponent* Prop=nullptr;UProceduralMeshComponent* Shape=nullptr;
    TInlineComponentArray<UStaticMeshComponent*> Props(F.P);for(auto* C:Props)if(C->GetFName()==FName(TEXT("WeaponPrimaryProxy")))Prop=C;
    TInlineComponentArray<UProceduralMeshComponent*> Shapes(F.P);for(auto* C:Shapes)if(C->GetFName()==FName(TEXT("WeaponPrimaryDeformation")))Shape=C;
    if(!TestTrue(TEXT("Current authored Hold has matching actual prop and geometry components"),Sample&&Geometry&&Prop&&Shape&&!Geometry->PrimaryVertices.IsEmpty()))return false;
    FTransform Expected=Set->PrimaryGrip*Sample->PrimaryTransform;Expected.SetScale3D(Expected.GetScale3D()*Set->VisualScale);
    TestTrue(TEXT("Visible prop transform uses Hold sample rather than frozen Attack"),Prop->GetRelativeTransform().Equals(Expected,1.e-3));
    auto* Section=Shape->GetProcMeshSection(0);
    TestTrue(TEXT("Procedural vertices use Hold geometry at the same source second"),Section&&Section->ProcVertexBuffer.Num()>0&&Section->ProcVertexBuffer[0].Position.Equals(Geometry->PrimaryVertices[0],1.e-3));
    F.Advance(.75);TestTrue(TEXT("Whole authored period wraps without extending the source"),FMath::IsNearlyZero(F.W()->GetPresentationSourceSeconds(),1.e-8));
    F.W()->SwordReadyLoopSeconds=2.;F.Advance(.5);
    TestTrue(TEXT("Two-second parameter retimes the complete original loop"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),.25,1.e-8));
    TestEqual(TEXT("Visual loop does not replace gameplay action serial"),F.W()->ActionSerial,Serial);
    TestEqual(TEXT("Visual loop does not advance native Attack elapsed"),F.W()->GetElapsedSeconds(),NativeElapsed);
    TestEqual(TEXT("Visual loop adds no gameplay boundary revision"),F.W()->GetCombatBoundaryRevision(),Boundary);
    TestEqual(TEXT("Visual loop emits no source gameplay event"),F.W()->DispatchedEventCount,Events);
    TestEqual(TEXT("Visual loop never spends weapon resources"),F.W()->GetCombatWeaponResourceRevision(),Resource);
    TestEqual(TEXT("Holding cannot cause either damage packet"),Enemy->Health,300.f);
    TestEqual(TEXT("Looping never repeats the full-charge cue"),F.P->CombatFeedback->ChargeReadyCount,1);
    auto* Pauser=F.Spawn<APlayerState>(FVector::ZeroVector);F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    TestFalse(TEXT("Paused weapon clock cannot move Hold"),F.W()->AdvanceAnimation(10.));
    TestTrue(TEXT("Paused Hold pose stays unchanged"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),.25,1.e-8));
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.W()->PrimaryReleased();
    TestEqual(TEXT("Queued release keeps Hold until committed Slam"),F.W()->GetActiveSequence(),Hold->Animation.Get());
    F.Advance(0.);
    TestTrue(TEXT("Committed release immediately enters existing Slam"),F.W()->GetFormalSwordPhase()==EDWSwordPhase::Slam);
    TestEqual(TEXT("Release restores existing Attack sequence"),F.W()->GetActiveSequence(),Attack->Animation.Get());
    TestTrue(TEXT("Release resumes exact original Attack 1.6-second seam"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),1.6,1.e-5));
    TestEqual(TEXT("Existing first damage still commits once"),Enemy->Health,300.f-static_cast<float>(F.T.SlamDamage));
    F.Advance(F.T.SlamSeconds);F.Advance(F.T.GapSeconds);
    TestEqual(TEXT("Existing automatic sweep still commits once"),Enemy->Health,300.f-static_cast<float>(F.T.SlamDamage+F.T.SweepDamage));
    F.Advance(F.T.SweepSeconds+F.T.RecoverySeconds+.000001);
    TestFalse(TEXT("Original combo still finishes"),F.W()->IsBusy());
    TestEqual(TEXT("Original combo still publishes exactly two impact cues"),F.P->CombatFeedback->SwordAttackCount,2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSwordAuthoredHoldLifecycleTest,"DoughWorld.Combat.Greatsword.AuthoredHoldCancelSwitchRestoreAndDeath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSwordAuthoredHoldLifecycleTest::RunTest(const FString&)
{
    DWSwordTests::FFixture F;if(!F.Ready())return false;auto* Set=DWSwordAuthoredHoldTests::AttachSource(F,true);
    if(!Set||!F.Equip())return false;
    const auto StartReady=[&](){if(!F.Start())return false;F.Advance(F.T.ChargeSeconds);return F.W()->IsFormalSwordWaitingForRelease();};
    if(!TestTrue(TEXT("Transient-clone source reaches Ready"),StartReady()))return false;
    F.Advance(.37);F.W()->CancelAction();
    TestFalse(TEXT("Cancel removes all Hold presentation"),F.W()->HasWeaponPresentation());
    if(!StartReady())return false;TestEqual(TEXT("New action cannot inherit old Hold phase"),F.W()->GetPresentationSourceSeconds(),0.);
    F.Advance(.2);TestTrue(TEXT("Actual switch away accepted"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed));
    TestFalse(TEXT("Switch leaves no Hold sequence"),F.W()->GetActiveSequence()!=nullptr);
    if(!F.W()->SelectWeapon(EDWAnimatedWeapon::Greatsword))return false;F.Advance(F.W()->GetRemainingAttackLockSeconds()+.001);
    if(!StartReady())return false;TestEqual(TEXT("Switch back begins original Hold at zero"),F.W()->GetPresentationSourceSeconds(),0.);
    F.Advance(.31);auto* Save=NewObject<UDWSaveGame>();
    if(!DWCombatPlayerSave::MakeNew(*F.GI->GetCombatRules(),*Save,F.Error))return false;
    F.P->CaptureSaveData(Save);
    if(!TestTrue(TEXT("Actual in-memory save restoration succeeds without a file or slot"),F.P->TryApplySaveData(Save)))return false;
    TestFalse(TEXT("Restore cancels active Hold as existing attack-save policy requires"),F.W()->HasWeaponPresentation());
    if(!StartReady())return false;TestEqual(TEXT("Restored next action starts Hold phase zero"),F.W()->GetPresentationSourceSeconds(),0.);
    // Only this test-owned clone is changed, never the real Data Asset, source art or animation.
    auto* Hold=Set->Clips.FindByPredicate([](const auto& C){return C.Role==TEXT("ChargeHold");});if(!Hold)return false;
    auto* Sequence=Hold->Animation.Get();Hold->Animation=nullptr;
    TestEqual(TEXT("Missing Hold animation safely falls back to unchanged Attack"),F.W()->GetPresentationClip().Role,FName(TEXT("Attack")));
    TestTrue(TEXT("Fallback keeps exact existing Ready Attack seam"),FMath::IsNearlyEqual(F.W()->GetPresentationSourceSeconds(),1.6,1.e-5));
    Hold->Animation=Sequence;const float Duration=Hold->DurationSeconds;Hold->DurationSeconds=0.f;
    TestEqual(TEXT("Invalid Hold duration never changes gameplay or divides by zero"),F.W()->GetPresentationClip().Role,FName(TEXT("Attack")));
    Hold->DurationSeconds=Duration;F.Advance(.2);
    FDamageEvent Lethal;F.P->TakeDamage(F.P->GetHealth()+1.f,Lethal,nullptr,nullptr);
    TestTrue(TEXT("Actual damage commits death"),F.P->IsDead());
    TestFalse(TEXT("Death cancels all active Hold presentation"),F.W()->HasWeaponPresentation());
    TestTrue(TEXT("Death cannot leave Hold sequence selected"),F.W()->GetActiveSequence()==nullptr);
    return true;
}
#endif
