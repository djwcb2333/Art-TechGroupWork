#if WITH_DEV_AUTOMATION_TESTS
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatDagger.h"
#include "DWCombatEquipmentTestFixture.h"
#include "DWCombatTestWorld.h"
#include "DWDamageVolumeLibrary.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

// Original test names remain stable. The retired Actor.MeleeComboSteps route is now
// the formal dagger's one-new-press continuation, not a hidden legacy three-step input path.
namespace DWPlayerComboTests
{
struct FWorldScope
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;
    FDWDaggerTuning T;FString Error;uint64 Sequence=0;
    FWorldScope()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("DWPlayerComboFormal"));if(!World)return;
        P=Spawn<ADWPlayerCharacter>();P->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        FDWDaggerTuning::Build(*GI->GetCombatRules(),T,Error);
        {FDWSpearTuning First;if(T.MakeSegmentTuning(0,First))T.Attack=First;}
        DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*C(),*GI->GetCombatRules(),EDWAnimatedWeapon::DualDaggers,Error);
    }
    ~FWorldScope()
    {if(!World)return;for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
     World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
    template<class A>A* Spawn(FVector At=FVector(0,0,500))
    {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* R=World->SpawnActor<A>(A::StaticClass(),At,FRotator::ZeroRotator,S);if(R&&!R->HasActorBegunPlay())R->DispatchBeginPlay();return R;}
    UDWWeaponAnimationComponent* C()const{return P?P->WeaponAnimationComponent.Get():nullptr;}
    bool Ready()const{return P&&P->HasCombatVitalsReady()&&C()->GetCurrentEquipmentId()==TEXT("DWWeapon_DualDaggers")&&T.Attack.IsValid();}
    FVector Target()const{return P->GetActorLocation()+FVector(450,0,0);}
    FDWCombatInputPress Press(uint64 N)const{return {{{1,1,1},N,TEXT("LeftMouseButton"),true,false},TEXT("Primary")};}
    bool Start(){if(!P->PerformMeleeAttack(Target()))return false;return C()->BindObservedInputPress(Press(++Sequence),false);}
    void Release(){C()->ApplyObservedInputRelease(Press(Sequence));}
    void Advance(double Dt){World->TimeSeconds+=static_cast<float>(Dt);C()->AdvanceAnimation(Dt);}
    void Recovery(){Advance(T.Attack.WindupSeconds);Advance(T.Attack.ActiveSeconds);}
    double Cycle()const{return T.Attack.WindupSeconds+T.Attack.ActiveSeconds+T.Attack.RecoverySeconds;}
    ADWEnemyCharacter* Enemy()
    {
        auto* E=Spawn<ADWEnemyCharacter>(P->GetActorLocation()+FVector(90,0,0));E->MaxHealth=500.f;E->SetHealthForLoad(500.f);
        E->SetActorRotation(FRotator(0,180,0));E->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        auto* B=NewObject<UDWDamageHitboxComponent>(E);E->AddInstanceComponent(B);B->SetupAttachment(E->GetRootComponent());
        B->SetBoxExtent(FVector(12));B->RegisterComponent();B->SetWorldLocation(E->GetActorLocation());return E;
    }
    UDWWeaponAnimationSet* Source()
    {
        auto* Original=LoadObject<UDWWeaponAnimationSet>(nullptr,TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_DualDaggers.DA_DWHeroWeapon_DualDaggers"));
        const auto* Clip=Original?Original->FindClip(TEXT("Attack")):nullptr;
        auto* Animation=Clip?Cast<UAnimSequence>(Clip->Animation.Get()):nullptr;if(!Animation)return nullptr;
        auto* Set=NewObject<UDWWeaponAnimationSet>(P);Set->Weapon=EDWAnimatedWeapon::DualDaggers;Set->bApplyBasicDamage=false;
        FDWWeaponAnimationClip S;S.Role=TEXT("Attack");S.Animation=DuplicateObject<UAnimSequence>(Animation,Set);S.DurationSeconds=S.Animation->GetPlayLength();Set->Clips.Add(S);
        C()->AnimationSets={Set};return Set;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerComboQueueTest,"DoughWorld.Combat.PlayerCombo.InputQueueAndThreeStages",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerComboQueueTest::RunTest(const FString&)
{
    DWPlayerComboTests::FWorldScope F;if(!TestTrue(TEXT("Explicit owned/current formal dagger"),F.Ready()))return false;
    TestTrue(TEXT("One public melee press starts first strike"),F.Start());const int32 First=F.C()->ActionSerial;
    TestEqual(TEXT("Formal windup active"),F.C()->GetFormalDaggerPhase(),EDWSpearPhase::Windup);
    TestFalse(TEXT("Held press cannot queue in windup"),F.Start());F.Recovery();
    TestFalse(TEXT("Held press cannot queue in recovery"),F.Start());F.Release();
    TestTrue(TEXT("One new recovery press buffers one next strike"),F.Start());
    TestEqual(TEXT("Buffering is not an attack start"),F.C()->ActionSerial,First);
    TestFalse(TEXT("No second buffered strike"),F.Start());F.Advance(F.T.Attack.RecoverySeconds*.5);
    TestEqual(TEXT("Recovery must finish before continuation"),F.C()->ActionSerial,First);
    F.Advance(F.T.Attack.RecoverySeconds*.5);TestEqual(TEXT("Exactly second accepted action"),F.C()->ActionSerial,First+1);
    F.Advance(F.Cycle()+.01);TestEqual(TEXT("Holding never starts a third action"),F.C()->ActionSerial,First+1);
    TestFalse(TEXT("Completed chain is idle"),F.C()->IsBusy());F.Release();TestTrue(TEXT("Fresh third press accepted"),F.Start());
    TestEqual(TEXT("Exactly three new-input actions"),F.C()->ActionSerial,First+2);F.Advance(F.Cycle()+1.);
    TestEqual(TEXT("No synthetic repeat"),F.C()->ActionSerial,First+2);
    TestEqual(TEXT("Retired actor combo index remains unused"),F.P->CurrentMeleeComboStep,INDEX_NONE);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerComboResetTest,"DoughWorld.Combat.PlayerCombo.ResetCancelAndLegacyFallback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerComboResetTest::RunTest(const FString&)
{
    DWPlayerComboTests::FWorldScope F;if(!F.Ready())return false;auto* Set=F.Source();if(!TestNotNull(TEXT("Isolated source sequence"),Set))return false;
    TestTrue(TEXT("Start formal action"),F.Start());F.Advance(F.Cycle()+Set->Clips[0].Animation->GetPlayLength());F.Release();
    TestTrue(TEXT("One source group stops at its exact authored boundary"),FMath::IsNearlyEqual(F.C()->GetElapsedSeconds(),29./30.,1.e-6));
    const float TailEnd=F.C()->GetElapsedSeconds();F.Advance(.5);TestEqual(TEXT("Completed source remains frozen at final pose"),F.C()->GetElapsedSeconds(),TailEnd);
    TestTrue(TEXT("Late fresh input starts a new action"),F.Start());F.Recovery();F.Release();TestTrue(TEXT("Queue before cancel"),F.Start());
    const int32 Serial=F.C()->ActionSerial;const float Position=F.C()->GetElapsedSeconds();F.P->ClearHeldActions();
    TestFalse(TEXT("Cancellation drops gameplay and queue"),F.C()->IsBusy());F.Advance(1.);
    TestEqual(TEXT("Cancelled queue never starts"),F.C()->ActionSerial,Serial);
    TestEqual(TEXT("Cancelled source clock freezes"),F.C()->GetElapsedSeconds(),Position);
    TestTrue(TEXT("New post-cancel press accepted after retained lock"),F.Start());F.Recovery();F.Release();TestTrue(TEXT("Queue before actual death"),F.Start());
    const int32 BeforeDeath=F.C()->ActionSerial;FDamageEvent Event;F.P->TakeDamage(1000.f,Event,nullptr,nullptr);F.Advance(1.);
    TestTrue(TEXT("Actual authority death"),F.P->IsDead());TestFalse(TEXT("Death clears pending action"),F.C()->IsBusy());
    TestEqual(TEXT("Death cannot continue queued attack"),F.C()->ActionSerial,BeforeDeath);TestFalse(TEXT("Dead actor cannot attack"),F.Start());
    DWPlayerComboTests::FWorldScope Fresh;if(!Fresh.Ready())return false;Fresh.P->MeleeComboSteps.Reset();Fresh.P->bEnableMeleeCombo=false;Fresh.P->MeleeDamage=999.f;
    TestTrue(TEXT("Retired combo fields do not disable owned formal attack"),Fresh.Start());
    TestEqual(TEXT("No legacy phase fallback"),Fresh.P->AttackPhase,EDWCombatPhase::None);
    TestEqual(TEXT("Formal source identity remains selected"),Fresh.C()->GetCurrentEquipmentId(),FName(TEXT("DWWeapon_DualDaggers")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerComboDamageTest,"DoughWorld.Combat.PlayerCombo.LowRateDamageAndStepSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerComboDamageTest::RunTest(const FString&)
{
    DWPlayerComboTests::FWorldScope F;if(!F.Ready())return false;auto* E=F.Enemy();auto* Set=F.Source();if(!Set)return false;
    const float Damage=static_cast<float>(F.T.Attack.Damage);TestTrue(TEXT("First formal strike starts"),F.Start());
    Set->Damage=999.f;Set->DamageRange=1.f;F.P->MeleeDamage=888.f;
    TestEqual(TEXT("No immediate damage"),E->Health,500.f);
    // Adding the two fractional durations can leave the second phase one ULP short.
    // Consume the actual formal phase boundary, rather than changing production timing or adding a fudge delta.
    F.Advance(F.T.Attack.WindupSeconds);F.Advance(F.C()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds()));
    TestEqual(TEXT("Low-rate tick crosses active and submits one formal packet"),E->Health,500.f-Damage);
    TestEqual(TEXT("Source asset edits do not change real phase"),F.C()->GetFormalDaggerPhase(),EDWSpearPhase::Recovery);
    F.Release();TestTrue(TEXT("Queue second independent receipt"),F.Start());F.Advance(F.T.Attack.RecoverySeconds);
    TestEqual(TEXT("Next windup cannot damage early"),E->Health,500.f-Damage);
    F.Advance(F.T.Attack.WindupSeconds);TestEqual(TEXT("Same victim accepts the next action once"),E->Health,500.f-2*Damage);
    F.Advance(F.T.Attack.ActiveSeconds*.5);TestEqual(TEXT("Second window deduplicates target"),E->Health,500.f-2*Damage);
    F.P->CancelCombatActions();F.Advance(1.);TestEqual(TEXT("Cancellation creates no delayed packet"),E->Health,500.f-2*Damage);
    TestEqual(TEXT("Retired actor damage never applies"),F.P->AttackSerial,0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerComboAnimationTest,"DoughWorld.Combat.PlayerCombo.DynamicSequenceAndDisableQueue",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerComboAnimationTest::RunTest(const FString&)
{
    DWPlayerComboTests::FWorldScope F;if(!F.Ready())return false;auto* Set=F.Source();if(!Set)return false;
    auto* First=CastChecked<UAnimSequence>(Set->Clips[0].Animation.Get());auto* Second=DuplicateObject<UAnimSequence>(First,Set);
    TestTrue(TEXT("First source-bearing formal action"),F.Start());TestTrue(TEXT("Current action selects first clip"),F.P->GetCurrentMeleeAnimation()==First);
    Set->Clips[0].Animation=Second;TestTrue(TEXT("In-flight source snapshot is unchanged"),F.P->GetCurrentMeleeAnimation()==First);
    F.Recovery();F.Release();TestTrue(TEXT("One fresh press queues next action"),F.Start());F.Advance(F.T.Attack.RecoverySeconds);
    TestTrue(TEXT("New action selects second source without changing original asset"),F.P->GetCurrentMeleeAnimation()==Second);
    TestTrue(TEXT("New source starts at the second group absolute source boundary"),FMath::IsNearlyEqual(F.C()->GetElapsedSeconds(),29./30.,1.e-5));
    F.Recovery();F.Release();TestTrue(TEXT("Queue before equipment change"),F.Start());const int32 Serial=F.C()->ActionSerial;
    TestTrue(TEXT("Legal unequip cancels queue"),F.C()->SelectWeapon(EDWAnimatedWeapon::Unarmed));F.Advance(1.);
    TestEqual(TEXT("No third stage after unequip"),F.C()->ActionSerial,Serial);TestFalse(TEXT("Unequipped action remains inactive"),F.C()->IsBusy());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerComboPossessionTest,"DoughWorld.Combat.PlayerCombo.LossOfPossessionCancelsQueue",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerComboPossessionTest::RunTest(const FString&)
{
    DWPlayerComboTests::FWorldScope F;if(!F.Ready())return false;auto* PC=F.Spawn<APlayerController>();PC->Possess(F.P);
    TestTrue(TEXT("Player really possessed"),F.P->GetController()==PC);TestTrue(TEXT("Start before control loss"),F.Start());
    F.Recovery();F.Release();TestTrue(TEXT("Queue before unpossess"),F.Start());const int32 Serial=F.C()->ActionSerial;
    PC->UnPossess();TestFalse(TEXT("Unpossess cancels current formal action"),F.C()->IsBusy());
    F.Advance(1.);TestEqual(TEXT("No delayed buffered action after control loss"),F.C()->ActionSerial,Serial);
    TestEqual(TEXT("Ownership is not deleted by loss of control"),F.C()->GetCurrentEquipmentId(),FName(TEXT("DWWeapon_DualDaggers")));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerComboLateActiveBufferTest,"DoughWorld.Combat.PlayerCombo.LateActiveBufferPreservesDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerComboLateActiveBufferTest::RunTest(const FString&)
{
    DWPlayerComboTests::FWorldScope F;if(!F.Ready())return false;auto* E=F.Enemy();const float Damage=static_cast<float>(F.T.Attack.Damage);
    TestTrue(TEXT("Formal first strike"),F.Start());F.Advance(F.T.Attack.WindupSeconds);F.Release();
    TestEqual(TEXT("Active sample applied exact formal damage"),E->Health,500.f-Damage);
    TestTrue(TEXT("A fresh active-window press queues without interrupting the current group"),F.Start());
    TestEqual(TEXT("Rejected input leaves active phase intact"),F.C()->GetFormalDaggerPhase(),EDWSpearPhase::Active);
    const int32 Serial=F.C()->ActionSerial;F.Advance(F.T.Attack.ActiveSeconds);
    TestFalse(TEXT("Existing queue cannot acquire a second recovery request"),F.Start());TestEqual(TEXT("Queue does not start early"),F.C()->ActionSerial,Serial);
    TestFalse(TEXT("Second queued press rejected"),F.Start());TestEqual(TEXT("Damage stays deduplicated"),E->Health,500.f-Damage);
    F.Advance(F.T.Attack.RecoverySeconds*.9);TestEqual(TEXT("Almost-complete recovery still blocks next start"),F.C()->ActionSerial,Serial);
    F.Advance(F.T.Attack.RecoverySeconds*.1+.000001);TestEqual(TEXT("Completed recovery starts exactly one next action"),F.C()->ActionSerial,Serial+1);
    TestEqual(TEXT("Next windup has not hit"),E->Health,500.f-Damage);F.Advance(F.T.Attack.WindupSeconds);
    TestEqual(TEXT("Second action has independent one-hit receipt"),E->Health,500.f-2*Damage);
    F.Advance(1.);TestEqual(TEXT("No held synthetic third strike"),F.C()->ActionSerial,Serial+1);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerComboFullTailBufferTest,"DoughWorld.Combat.PlayerCombo.FullTailWindowAndImmediateGuard",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerComboFullTailBufferTest::RunTest(const FString&)
{
    DWPlayerComboTests::FWorldScope F;if(!F.Ready())return false;
    TestTrue(TEXT("Start"),F.Start());const int32 Serial=F.C()->ActionSerial;F.Release();
    TestTrue(TEXT("Immediate fresh press may queue without bypassing windup"),F.Start());
    TestEqual(TEXT("Rejected press preserves windup"),F.C()->GetFormalDaggerPhase(),EDWSpearPhase::Windup);
    F.Advance(F.T.Attack.WindupSeconds);TestFalse(TEXT("Active phase still rejects continuation"),F.Start());
    F.Advance(F.T.Attack.ActiveSeconds);TestFalse(TEXT("Whole group still holds exactly one queued press"),F.Start());
    TestEqual(TEXT("Acceptance does not restart current phase"),F.C()->GetFormalDaggerPhase(),EDWSpearPhase::Recovery);
    TestEqual(TEXT("Only one action has begun"),F.C()->ActionSerial,Serial);F.P->ClearHeldActions();F.Advance(1.);
    TestEqual(TEXT("Cancelled buffer never starts"),F.C()->ActionSerial,Serial);TestTrue(TEXT("New independent action"),F.Start());
    F.Release();TestTrue(TEXT("Next action may also buffer one future group"),F.Start());F.P->ClearHeldActions();F.Advance(2.);
    TestEqual(TEXT("Cancelled buffer cannot add another action"),F.C()->ActionSerial,Serial+1);return true;
}
#endif
