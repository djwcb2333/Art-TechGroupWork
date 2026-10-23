#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatDagger.h"
#include "DWCombatRules.h"
#include "DWCombatPresentationTiming.h"
#include "DWCombatEquipment.h"
#include "DWCombatTestWorld.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWDamageVolumeLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"
#include <limits>
namespace DWDaggerTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;
    FDWDaggerTuning T;FString Error;
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe{NewObject<UDWCombatAuthorityTestProbe>()};
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D15Dagger"));if(!World)return;
        P=Spawn<ADWPlayerCharacter>(FVector(0,0,500));
        if(P){P->SetActorTickEnabled(false);P->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);}
        if(GI->GetCombatRules()&&FDWDaggerTuning::Build(*GI->GetCombatRules(),T,Error))
        {FDWSpearTuning First;if(T.MakeSegmentTuning(0,First))T.Attack=First;}
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
    bool Ready()const{return P&&P->HasCombatVitalsReady()&&W()&&T.Attack.IsValid()&&!GI->HasActiveSlot();}
    bool Equip()
    {
        auto* A=UDWCombatAuthoritySubsystem::Get(P);const auto Action=A->BeginAction(P);
        FDWCombatEquipmentGrantRequest R{Action,A->CaptureActorLife(P),1,W()->GetCombatWeaponRestoreEpoch(),TEXT("DWWeapon_DualDaggers")};
        const bool Granted=FDWCombatEquipmentTransaction::Grant(*W(),R).bCommittedThisCall;A->EndAction(Action);
        if(!Granted||!W()->SelectWeapon(EDWAnimatedWeapon::DualDaggers))return false;
        Advance(W()->GetRemainingAttackLockSeconds()+.01);return true;
    }
    void Advance(double Dt){World->TimeSeconds+=static_cast<float>(Dt);W()->AdvanceAnimation(Dt);}
    FDWCombatInputPress Press(uint64 N)const{return {{{1,1,1},N,TEXT("LeftMouseButton"),true,false},TEXT("Primary")};}
    bool Start(uint64 N=10)
    {
        if(!W()->PrimaryPressed(P->GetActorLocation()+FVector(450,0,0)))return false;
        if(!W()->BindObservedInputPress(Press(N),false))return false;W()->PublishAcceptedWeaponStartEffects();return true;
    }
    ADWEnemyCharacter* Enemy(FVector Offset,EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::Legacy)
    {
        auto* E=Spawn<ADWEnemyCharacter>(P->GetActorLocation()+Offset);if(!E)return nullptr;
        E->SetActorTickEnabled(false);E->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        if(Profile!=EDWEnemyCombatProfile::Legacy){E->CombatProfile=Profile;if(!E->InitializeFormalCombat(*GI->GetCombatRules(),Error))return nullptr;}
        else{E->MaxHealth=300.f;E->SetHealthForLoad(300.f);}
        auto* Box=NewObject<UDWDamageHitboxComponent>(E);E->AddInstanceComponent(Box);Box->SetupAttachment(E->GetRootComponent());
        Box->SetBoxExtent(FVector(10));Box->RegisterComponent();Box->SetWorldLocation(E->GetActorLocation());
        E->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);return E;
    }
    void Engage(ADWEnemyCharacter* E)
    {
        auto* A=UDWCombatAuthoritySubsystem::Get(P);const auto Act=A->BeginAction(P);FDWCombatDamageCommand C;
        C.Identity={Act,1,DWCombatCommit::EActionDamageKind::Melee,1};C.Source=P;C.Target=E;C.TargetLife=A->CaptureActorLife(E);C.Amount=1.f;
        A->SubmitActionDamage(C);A->EndAction(Act);
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerRulesTest,"DoughWorld.Combat.Dagger.FormalRulesAndPhaseClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerRulesTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> R;FString Error;FDWDaggerTuning T;
    if(!TestTrue(TEXT("Official payload bridge"),FDWCombatRules::LoadProjectPayload(R,Error)&&FDWDaggerTuning::Build(*R,T,Error)))return false;
    TestEqual(TEXT("Damage14"),T.Attack.Damage,14.);TestEqual(TEXT("Independent120cm range"),T.Attack.RangeCm,120.);
    TestEqual(TEXT("No knockback"),T.Attack.KnockbackCm,0.);TestEqual(TEXT("Elite final fraction"),T.EliteMaxHealthFraction,.5);
    FDWSpearAttackPlan Plan;TestTrue(TEXT("Start yeast phase clock"),Plan.Start(T.Attack,EDWSpearForm::Yeast));
    double Used=0.,Left=0.;TestTrue(TEXT("Yeast windup uses configured1.25"),FMath::IsNearlyEqual(Plan.GetPhaseRemainingSeconds(),.06/1.25,1.e-10));
    Plan.AdvanceCurrentPhase(.06/1.25,Used,Left);Plan.EnterNextPhase(EDWSpearForm::Yeast);
    TestTrue(TEXT("Normal damage snapshots yeast1.1 at release"),FMath::IsNearlyEqual(Plan.GetDamageSnapshot(),15.4,1.e-8));
    Plan.AdvanceCurrentPhase(100.,Used,Left);Plan.EnterNextPhase(EDWSpearForm::Normal);
    TestEqual(TEXT("Recovery observes current form"),Plan.GetPhaseRemainingSeconds(),.14);
    const auto Phase=Plan.GetPhase();TestFalse(TEXT("NaN does not advance"),Plan.AdvanceCurrentPhase(std::numeric_limits<double>::quiet_NaN(),Used,Left));
    TestTrue(TEXT("Phase unchanged on invalid delta"),Plan.GetPhase()==Phase);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerMemoryTest,"DoughWorld.Combat.Dagger.PerEnemyCrossingUnequipRefreshAndRestore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerMemoryTest::RunTest(const FString&)
{
    FDWDaggerAwareness A,B;A.EquipmentChanged(false,true);B.EquipmentChanged(false,true);
    A.Observe(10.,true,true,false,3.);B.Observe(11.,true,true,false,3.);
    TestFalse(TEXT("First enemy expires independently"),A.IsProtected(13.,true,false));TestTrue(TEXT("Second retains its own second"),B.IsProtected(13.,true,false));
    A.Observe(14.,false,true,false,3.);A.Observe(15.,true,true,false,3.);TestTrue(TEXT("Exit/reentry regrants"),A.IsProtected(17.99,true,false));
    A.EquipmentChanged(true,false);A.EquipmentChanged(true,true);A.Observe(16.,true,true,false,3.);
    TestFalse(TEXT("Equip in place cannot regrant"),A.IsProtected(16.,true,false));
    A.Refresh(20.,3.,true,false);A.Refresh(21.,3.,true,false);TestEqual(TEXT("Refresh resets rather than adds"),A.Capture(22.).GraceRemainingSeconds,2.);
    FDWDaggerAwareness Loaded;TestTrue(TEXT("Restore exact remaining state"),Loaded.Restore(A.Capture(22.),100.));
    Loaded.Observe(100.,true,true,false,3.);TestEqual(TEXT("Load inside is not a new crossing"),Loaded.Capture(100.).GraceRemainingSeconds,2.);
    auto Invalid=Loaded.Capture(100.);Invalid.GraceRemainingSeconds=-1.;TestFalse(TEXT("Invalid save rejected"),Loaded.Restore(Invalid,100.));
    TestEqual(TEXT("Rejected restore preserves remaining"),Loaded.Capture(100.).GraceRemainingSeconds,2.);
    Loaded.BreakProtection();Loaded.Refresh(101.,3.,true,true);TestFalse(TEXT("Engaged cannot gain grace"),Loaded.IsProtected(101.,true,true));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerDiscoveryTest,"DoughWorld.Combat.Dagger.NativeDiscoveryGateAndRealEquipmentChanges",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerDiscoveryTest::RunTest(const FString&)
{
    DWDaggerTests::FFixture F;if(!F.Ready()||!F.Equip())return false;
    auto* E=F.Enemy(FVector(300,0,0),EDWEnemyCombatProfile::DoughZombie);if(!E)return false;
    auto* PC=F.Spawn<APlayerController>(FVector::ZeroVector);if(!PC)return false;PC->Possess(F.P);
    const FVector Center=E->GetActorLocation();F.P->SetActorLocation(Center-FVector(E->GetDaggerAlertRadius()+50.,0,0));E->SyncDaggerAwareness(F.P);
    F.P->SetActorLocation(Center-FVector(300,0,0));E->SyncDaggerAwareness(F.P);
    TestTrue(TEXT("Actual geometric entry starts per-enemy grace"),E->HasDaggerDiscoveryGrace(F.P));
    TestFalse(TEXT("Native detection query rejects during grace"),E->CanDetectTarget(F.P));E->Tick(0.f);
    TestFalse(TEXT("Actual formal AI does not engage during grace"),E->HasRememberedPlayerCombat());
    F.Advance(1.);const auto Saved=E->CaptureDaggerAwarenessSave();
    TestTrue(TEXT("Controlled D22 seam restores remaining only"),E->RestoreDaggerAwarenessSave(Saved,F.P));
    TestTrue(TEXT("Unequip valid"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed));TestFalse(TEXT("Protection lost immediately"),E->HasDaggerDiscoveryGrace(F.P));
    TestTrue(TEXT("Switch back valid"),F.W()->SelectWeapon(EDWAnimatedWeapon::DualDaggers));TestFalse(TEXT("Same position cannot reset protection"),E->HasDaggerDiscoveryGrace(F.P));
    E->Tick(0.f);TestTrue(TEXT("Unprotected actual AI engages"),E->HasRememberedPlayerCombat());
    F.P->SetActorLocation(Center-FVector(E->GetDaggerAlertRadius()+50.,0,0));E->SyncDaggerAwareness(F.P);
    F.P->SetActorLocation(Center-FVector(300,0,0));E->SyncDaggerAwareness(F.P);TestFalse(TEXT("Engaged reentry does not reset"),E->HasDaggerDiscoveryGrace(F.P));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerNearestTest,"DoughWorld.Combat.Dagger.OneNearestBackstabOtherTargetsNormal",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerNearestTest::RunTest(const FString&)
{
    DWDaggerTests::FFixture F;if(!F.Ready()||!F.Equip())return false;
    auto* Near=F.Enemy(FVector(75,-20,0));auto* Far=F.Enemy(FVector(115,20,0));auto* Outside=F.Enemy(FVector(0,180,0));
    if(!Near||!Far||!Outside||!F.Start())return false;
    TestTrue(TEXT("Art-free formal attack is enabled"),F.W()->AnimationSets.IsEmpty()&&F.W()->IsEnabled());
    TestFalse(TEXT("Legacy Backstab role cannot bypass authority"),F.W()->PlayRole(TEXT("Backstab"),F.P->GetActorLocation()));
    F.Advance(F.T.Attack.WindupSeconds);
    TestEqual(TEXT("Nearest qualifying ordinary enemy dies"),Near->Health,0.f);
    TestEqual(TEXT("Second eligible enemy receives only flat14"),Far->Health,286.f);
    TestEqual(TEXT("Outside independent cone untouched"),Outside->Health,300.f);
    F.Advance(F.T.Attack.ActiveSeconds*.5);TestEqual(TEXT("Repeated window cannot duplicate normal damage"),Far->Health,286.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerEliteTest,"DoughWorld.Combat.Dagger.EliteFinalDamageRefreshBeforeNoticeAndNoRepeatExecution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerEliteTest::RunTest(const FString&)
{
    DWDaggerTests::FFixture F;if(!F.Ready()||!F.Equip())return false;
    auto* Elite=F.Enemy(FVector(95,0,0),EDWEnemyCombatProfile::DoughElite);
    auto* Nearby=F.Enemy(FVector(300,200,0),EDWEnemyCombatProfile::DoughZombie);
    auto* Engaged=F.Enemy(FVector(400,-200,0),EDWEnemyCombatProfile::DoughZombie);
    auto* Outside=F.Enemy(FVector(1000,200,0),EDWEnemyCombatProfile::DoughZombie);
    if(!Elite||!Nearby||!Engaged||!Outside)return false;F.Engage(Engaged);
    int32 Calls=0;F.Probe->AnyCallback=[&](AActor* Target,float,AController*,AActor*)
    {
        if(Target!=Elite)return;++Calls;
        TestEqual(TEXT("Real damage listener sees final maxHP50percent without14"),Elite->Health,150.f);
        TestTrue(TEXT("Surviving elite already engaged before callback"),Elite->HasRememberedPlayerCombat());
        TestTrue(TEXT("Eligible nearby grace already refreshed before callback"),Nearby->HasDaggerDiscoveryGrace(F.P));
        TestFalse(TEXT("Engaged observer does not forget player"),Engaged->HasDaggerDiscoveryGrace(F.P));
        TestFalse(TEXT("Beyond victim-centered8m not refreshed"),Outside->HasDaggerDiscoveryGrace(F.P));
    };
    if(!F.Start())return false;F.Advance(F.T.Attack.WindupSeconds);TestEqual(TEXT("One committed final damage notice"),Calls,1);
    F.Probe->AnyCallback=nullptr;F.Advance(1.);F.W()->ApplyObservedInputRelease(F.Press(10));
    if(!F.Start(11))return false;F.Advance(F.T.Attack.WindupSeconds);
    TestEqual(TEXT("Already engaged elite receives normal14 on second swing"),Elite->Health,136.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerContinuationTest,"DoughWorld.Combat.Dagger.NewPressContinuationPauseAndSwitchCancel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerContinuationTest::RunTest(const FString&)
{
    DWDaggerTests::FFixture F;if(!F.Ready()||!F.Equip())return false;
    auto* E=F.Enemy(FVector(100,0,0));if(!E)return false;E->SetActorRotation(FRotator(0,180,0));
    if(!F.Start())return false;F.Advance(F.T.Attack.WindupSeconds);F.Advance(F.T.Attack.ActiveSeconds);
    TestFalse(TEXT("Held input cannot buffer another strike"),F.W()->PrimaryPressed(F.P->GetActorLocation()+FVector(450,0,0)));
    F.W()->ApplyObservedInputRelease(F.Press(10));TestTrue(TEXT("New click during recovery queues one continuation"),F.Start(11));
    auto* Pauser=F.Spawn<APlayerState>(FVector::ZeroVector);F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    const int32 Serial=F.W()->ActionSerial;TestFalse(TEXT("Pause cannot consume recovery or buffered click"),F.W()->AdvanceAnimation(100.));
    TestEqual(TEXT("No paused continuation"),F.W()->ActionSerial,Serial);F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    F.Advance(F.T.Attack.RecoverySeconds);TestEqual(TEXT("One accepted new click produces exactly one new action"),F.W()->ActionSerial,Serial+1);
    F.Advance(2.);TestEqual(TEXT("Two independent receipts each deal14"),E->Health,272.f);
    const int32 Completed=F.W()->ActionSerial;F.Advance(1.);TestEqual(TEXT("No hold-repeat after queued strike"),F.W()->ActionSerial,Completed);
    F.W()->ApplyObservedInputRelease(F.Press(11));if(!F.Start(12))return false;
    F.Advance(F.T.Attack.WindupSeconds);F.Advance(F.T.Attack.ActiveSeconds);F.W()->ApplyObservedInputRelease(F.Press(12));
    if(!F.Start(13))return false;TestTrue(TEXT("Valid equipment switch cancels queued continuation"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed));
    F.Advance(2.);TestEqual(TEXT("Already dealt damage remains; queued hit never occurs"),E->Health,258.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerLegacyAlertRegressionTest,"DoughWorld.Combat.Dagger.LegacyDamageKeepsAlertPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerLegacyAlertRegressionTest::RunTest(const FString&)
{
    DWDaggerTests::FFixture F;if(!F.Ready())return false;
    auto* E=F.Enemy(FVector(200,0,0));auto* PC=F.Spawn<APlayerController>(F.P->GetActorLocation());if(!E||!PC)return false;
    PC->Possess(F.P);E->AlertDuration=.7f;int32 Calls=0;
    F.Probe->AnyCallback=[&](AActor* Target,float,AController*,AActor*)
    {
        if(Target!=E)return;++Calls;
        TestTrue(TEXT("Core facts already block a reentrant discovery/backstab reset"),E->IsEngagedForDagger());
    };
    FDamageEvent Event;const float Applied=E->TakeDamage(1.f,Event,PC,F.P);
    TestEqual(TEXT("One real damage packet"),Applied,1.f);TestEqual(TEXT("One actual damage callback"),Calls,1);
    TestEqual(TEXT("Legacy first hit enters original Alert state"),E->State,EDWEnemyState::Alert);
    TestEqual(TEXT("Dedicated Alert animation remains available"),E->GetCombatAnimationState(),EDWCombatAnimationState::Alert);
    TestEqual(TEXT("Authored legacy alert duration unchanged"),E->AlertDuration,.7f);
    E->SetHealthForLoad(300.f);TestFalse(TEXT("Restore clears pending damage awareness"),E->IsEngagedForDagger());return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerSegmentMappingTest,"DoughWorld.Combat.Dagger.Feedback25CompleteSegmentMappingAndTuning",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerSegmentMappingTest::RunTest(const FString&)
{
    using namespace DWCombatPresentationTiming;TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWDaggerTuning T;
    if(!TestTrue(TEXT("Formal segment duration parameters load"),FDWCombatRules::LoadProjectPayload(Rules,Error)&&FDWDaggerTuning::Build(*Rules,T,Error)))return false;
    const double Source[]={0.,29./30.,28./15.,8./3.,3.85};
    for(int32 I=0;I<4;++I)
    {
        FDWSpearTuning Stage;if(!TestTrue(TEXT("Each segment has a valid single authority plan"),T.MakeSegmentTuning(I,Stage)))return false;
        TestTrue(TEXT("Default preserves the complete original group duration"),FMath::IsNearlyEqual(Stage.IntervalSeconds,Source[I+1]-Source[I],1.e-6));
        TestTrue(TEXT("Windup ratio remains .06/.30"),FMath::IsNearlyEqual(Stage.WindupSeconds/Stage.IntervalSeconds,.2,1.e-9));
        TestTrue(TEXT("Active ratio remains .10/.30"),FMath::IsNearlyEqual(Stage.ActiveSeconds/Stage.IntervalSeconds,1./3.,1.e-9));
        TestTrue(TEXT("Midpoint uses full group's source midpoint"),FMath::IsNearlyEqual(MapSegment(Stage.IntervalSeconds*.5,0.,Stage.IntervalSeconds,Source[I],Source[I+1]),(Source[I]+Source[I+1])*.5,1.e-9));
        T.SegmentDurationSeconds[I]*=.5;FDWSpearTuning Fast;TestTrue(TEXT("Future parameter change remains a valid complete segment"),T.MakeSegmentTuning(I,Fast));
        TestTrue(TEXT("Half duration halves hit timing without cropping source"),FMath::IsNearlyEqual(Fast.WindupSeconds,Stage.WindupSeconds*.5,1.e-9)&&FMath::IsNearlyEqual(Fast.ActiveSeconds,Stage.ActiveSeconds*.5,1.e-9));
        TestEqual(TEXT("Mapped endpoint remains authored endpoint after duration edit"),MapSegment(Fast.IntervalSeconds,0.,Fast.IntervalSeconds,Source[I],Source[I+1]),Source[I+1]);
        FDWSpearAttackPlan Plan;double Used=0.,Left=0.;TestTrue(TEXT("Changed duration uses the same gameplay authority plan"),Plan.Start(Fast,EDWSpearForm::Normal));
        Plan.AdvanceCurrentPhase(Fast.WindupSeconds,Used,Left);TestTrue(TEXT("Whole-group presentation reaches the same normalized windup boundary"),FMath::IsNearlyEqual(GetNormalizedProgress(Plan.GetSourceSeconds(),0.,Fast.IntervalSeconds),.2,1.e-9));
        TestTrue(TEXT("Changed windup enters one active damage window"),Plan.EnterNextPhase(EDWSpearForm::Normal));TestEqual(TEXT("Duration edits do not add damage or change its snapshot"),Plan.GetDamageSnapshot(),T.Attack.Damage);
    }
    TestEqual(TEXT("Zero source interval is safe"),MapSegment(5.,1.,1.,7.,9.),7.);
    TestEqual(TEXT("Underflow clamps to source start"),MapSegment(-1.,0.,1.,3.,4.),3.);
    TestEqual(TEXT("Overflow clamps to source end"),MapSegment(2.,0.,1.,3.,4.),4.);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDaggerWholeGroupRuntimeTest,"DoughWorld.Combat.Dagger.Feedback25OneClickOneFullGroupAndQueuedFourGroups",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDaggerWholeGroupRuntimeTest::RunTest(const FString&)
{
    DWDaggerTests::FFixture F;if(!F.Ready()||!F.Equip())return false;FDWDaggerTuning T;
    if(!FDWDaggerTuning::Build(*F.GI->GetCombatRules(),T,F.Error))return false;
    const double Source[]={0.,29./30.,28./15.,8./3.,3.85};
    TestTrue(TEXT("Start first group"),F.Start());const int32 Serial=F.W()->ActionSerial;F.W()->ApplyObservedInputRelease(F.Press(10));
    F.Advance(T.SegmentDurationSeconds[0]-.0001);TestTrue(TEXT("Single input remains in G1 until full group completes"),F.W()->IsBusy()&&F.W()->GetFormalDaggerComboStage()==1);
    F.Advance(.0002);TestFalse(TEXT("Single input ends after exactly one complete group"),F.W()->IsBusy()||F.W()->HasWeaponPresentation());
    TestTrue(TEXT("Single group source stops at G1 endpoint"),FMath::IsNearlyEqual(F.W()->GetElapsedSeconds(),Source[1],1.e-6));
    F.Advance(5.);TestEqual(TEXT("No hidden long source tail or automatic G2"),F.W()->ActionSerial,Serial);
    TestTrue(TEXT("Restart begins G1"),F.Start(20));uint64 PressId=20;
    for(int32 I=0;I<4;++I)
    {
        TestEqual(TEXT("Expected authored group is active"),F.W()->GetFormalDaggerComboStage(),I+1);
        if(I<3)
        {
            F.W()->ApplyObservedInputRelease(F.Press(PressId));++PressId;TestTrue(TEXT("One new press queues the next group in current windup"),F.Start(PressId));
            TestFalse(TEXT("Held repeat never adds a second queued group"),F.Start(PressId+100));
        }
        const int32 Before=F.W()->ActionSerial;
        const double Already=DWCombatPresentationTiming::MapSegment(F.W()->GetElapsedSeconds(),Source[I],Source[I+1],0.,T.SegmentDurationSeconds[I]);
        F.Advance(T.SegmentDurationSeconds[I]-Already-.0001);
        TestEqual(TEXT("Queue cannot cut current group before its endpoint"),F.W()->ActionSerial,Before);
        TestTrue(TEXT("Source reached this complete group's authored endpoint"),FMath::IsNearlyEqual(F.W()->GetElapsedSeconds(),Source[I+1],.001));
        F.Advance(.0002);TestEqual(TEXT("One queued click consumes only at whole-group completion"),F.W()->ActionSerial,Before+(I<3?1:0));
        if(I<3)TestTrue(TEXT("Next group starts at its own absolute source offset"),FMath::IsNearlyEqual(F.W()->GetElapsedSeconds(),Source[I+1],.001));
    }
    TestFalse(TEXT("Four supplied groups stop without a fifth synthetic input"),F.W()->IsBusy());return true;
}

#endif
