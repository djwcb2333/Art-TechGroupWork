#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatMoldRoot.h"
#include "DWCombatFeedbackComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatRecoveryActors.h"
#include "DWCombatTestWorld.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipmentTestFixture.h"
#include "DWWeaponEventReentryTestProbe.h"
#include "DWSaveGame.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace DWMoldRootRuntimeTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> DamageProbe{NewObject<UDWCombatAuthorityTestProbe>()};
    TStrongObjectPtr<UDWWeaponEventReentryTestProbe> EventProbe{NewObject<UDWWeaponEventReentryTestProbe>()};
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;UDWCombatAuthoritySubsystem* Authority=nullptr;
    FDWMoldRootConfig RootConfig;FDWCombatPlayerVitalsConfig VitalsConfig;FString Error;bool bReady=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D10MoldRoot"));if(!World)return;
        auto* Floor=Spawn<AActor>();if(!Floor)return;
        auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(10000,10000,50));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
        Floor->SetActorLocation(FVector(0,0,-50));
        Player=Spawn<ADWPlayerCharacter>();if(!Player||!Player->HasCombatVitalsReady())return;
        Player->SetActorLocation(FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.));
        Authority=UDWCombatAuthoritySubsystem::Get(Player);Player->WeaponAnimationComponent->bEnabled=false;
        if(!Authority||!FDWMoldRootConfig::Build(*GI->GetCombatRules(),RootConfig,Error)||
            !FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),VitalsConfig,Error))return;
        bReady=Restore(RootConfig.StartCost+30.)&&!GI->HasActiveSlot();
        Player->OnTakeAnyDamage.AddDynamic(DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
        Player->WeaponAnimationComponent->OnAnimationGameplayEvent.AddDynamic(EventProbe.Get(),&UDWWeaponEventReentryTestProbe::HandleAnimation);
    }
    ~FFixture()
    {
        DamageProbe->AnyCallback=nullptr;EventProbe->AnimationCallback=nullptr;
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn()
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<T>(T::StaticClass(),FVector(5000,5000,500),FRotator::ZeroRotator,P);
        if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    UDWSaveGame* Save(double Special,double Remaining=-1.)
    {
        auto* S=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error))return nullptr;
        auto& V=S->CombatPlayerResources.Vitals;V.BaseForm=EDWCombatBaseForm::Mold;
        V.FormRemainingSeconds=Remaining<0.?VitalsConfig.FormDurationSeconds:Remaining;V.Special=Special;
        V.Water=VitalsConfig.Vitals.WaterMax;V.Satiety=VitalsConfig.Vitals.SatietyMax;
        S->Transformation=static_cast<float>(Special);S->bYeastForm=false;return S;
    }
    bool Restore(double Special,double Remaining=-1.){auto* S=Save(Special,Remaining);return S&&Player->TryApplySaveData(S);}
    void Advance(float Dt){World->TimeSeconds+=Dt;Player->TickGameplayClocks(Dt,false);}
    bool Attack(float Duration=2.f,bool Markers=false)
    {
        auto* W=Player->WeaponAnimationComponent.Get();W->bEnabled=true;
        auto* Set=NewObject<UDWWeaponAnimationSet>(Player);Set->Weapon=EDWAnimatedWeapon::Spear;
        Set->bApplyBasicDamage=false;Set->bRepeatWhileHeld=false;Set->ChargeSeconds=0.f;
        FDWWeaponAnimationClip Clip;Clip.Role=TEXT("Attack");Clip.DurationSeconds=Duration;Clip.Animation=NewObject<UAnimSequence>(Set);
        if(Markers){FDWWeaponDamageEvent E;E.TimeSeconds=.125f;Clip.DamageEvents.Add(E);E.TimeSeconds=.25f;Clip.DamageEvents.Add(E);}
        Set->Clips={Clip};W->AnimationSets={Set};
        // Synthetic no-damage presentation clock; not a formal spear gameplay assertion.
        if(!DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*W,*GI->GetCombatRules(),EDWAnimatedWeapon::Spear,Error))return false;
        return W->SelectWeapon(EDWAnimatedWeapon::Spear)&&W->PrimaryPressed(Player->GetActorLocation()+FVector(500,0,0));
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootEntryRuntimeTest,"DoughWorld.Combat.MoldRoot.RuntimeQualifiedFeeAndInvalidEntryPreservesAttack",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootEntryRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWMoldRootRuntimeTests;FFixture F;if(!TestTrue(TEXT("Isolated floor and strict Mold state"),F.bReady))return false;
    TestTrue(TEXT("Equal fee restore"),F.Restore(F.RootConfig.StartCost+F.RootConfig.SpecialFloor));
    TestTrue(TEXT("Unfinished attack"),F.Attack());const int32 Serial=F.Player->WeaponAnimationComponent->ActionSerial;
    TestFalse(TEXT("Exact fee cannot create zero-duration root"),F.Player->TryToggleFormalRoot());
    TestTrue(TEXT("Failed E preserves attack"),F.Player->WeaponAnimationComponent->IsBusy());
    TestEqual(TEXT("Failed E preserves action identity"),F.Player->WeaponAnimationComponent->ActionSerial,Serial);
    TestTrue(TEXT("Positive budget restore"),F.Restore(F.RootConfig.StartCost+30.));
    F.Player->WeaponAnimationComponent->bEnabled=false;
    TestTrue(TEXT("Qualified ground enters immediately"),F.Player->TryToggleFormalRoot());
    TestEqual(TEXT("Fee paid once"),F.Player->GetCombatSpecial(),30.);
    TestTrue(TEXT("Root active at return"),F.Player->IsFormallyRooted());
    FDWMoldRootIdentity I;TestTrue(TEXT("Current episode has complete identity"),F.Player->GetFormalRootIdentity(I));
    const auto Receipt=F.Authority->FindReceipt(I.Receipt);
    TestTrue(TEXT("Fee receipt committed before effects"),Receipt&&Receipt->State==DWCombatCommit::EReceiptState::Committed);
    TestFalse(TEXT("Public authority cannot replay private accepted E"),F.Authority->CommitFormalRootEntry(F.Player));
    TestEqual(TEXT("Replay does not debit"),F.Player->GetCombatSpecial(),30.);
    TestTrue(TEXT("Manual E exits immediately"),F.Player->TryToggleFormalRoot());
    TestFalse(TEXT("No visual exit grace"),F.Player->IsFormallyRooted());
    TestEqual(TEXT("Exit does not cost"),F.Player->GetCombatSpecial(),30.);
    F.Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
    TestFalse(TEXT("Airborne re-entry rejected"),F.Player->TryToggleFormalRoot());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootMovementRuntimeTest,"DoughWorld.Combat.MoldRoot.RuntimeFixedPositionDamageAndOrdinaryKnockbackImmunity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootMovementRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWMoldRootRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    TestTrue(TEXT("Enter"),F.Player->TryToggleFormalRoot());const FVector Anchor=F.Player->GetActorLocation();
    TestTrue(TEXT("Actual movement mode disabled"),F.Player->GetCharacterMovement()->MovementMode==MOVE_None);
    F.Player->MoveCameraRelative(1.f,1.f);F.Player->StartActionJump();F.Player->StartForwardRoll();
    F.Player->GetCharacterMovement()->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("WASD/jump/legacy roll cannot move rooted capsule"),F.Player->GetActorLocation().Equals(Anchor,1.e-6));
    TestTrue(TEXT("Ordinary attack allowed in stance"),F.Attack());int32 Calls=0;
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {++Calls;TestTrue(TEXT("Damage delegate sees rooted facts"),F.Player->IsFormallyRooted());TestTrue(TEXT("Ordinary injury preserves weapon"),F.Player->WeaponAnimationComponent->IsBusy());};
    auto* Enemy=F.Spawn<ADWEnemyCharacter>();if(!TestNotNull(TEXT("Damage source"),Enemy))return false;
    FDWCombatDamageCommand C;C.Source=Enemy;C.Target=F.Player;C.TargetLife=F.Authority->CaptureActorLife(F.Player);
    C.Identity={F.Authority->BeginAction(Enemy),1,DWCombatCommit::EActionDamageKind::EnemyMelee,1};
    C.Amount=10.f;C.KnockbackDirection=FVector::ForwardVector;C.KnockbackDistance=200.f;C.KnockbackSeconds=.2f;
    const auto R=F.Authority->SubmitActionDamage(C);
    TestEqual(TEXT("Rooted incoming replaces Mold multiplier"),R.AppliedThisCall,static_cast<float>(10.*F.RootConfig.IncomingMultiplier));
    TestEqual(TEXT("Real dynamic damage callback ran"),Calls,1);
    F.Player->Tick(.1f);TestTrue(TEXT("Explicit ordinary knockback rejected by actual stance"),F.Player->GetActorLocation().Equals(Anchor,1.e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootClockRuntimeTest,"DoughWorld.Combat.MoldRoot.RuntimeExhaustionSliceViewAndOrdinaryAttackContinuation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootClockRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWMoldRootRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const double Net=F.RootConfig.SpendPerSecond-F.RootConfig.SpecialGainPerSecond;
    if(!TestTrue(TEXT("Formal default net consumption positive"),Net>0.))return false;
    TestTrue(TEXT("Quarter-second budget"),F.Restore(F.RootConfig.StartCost+F.RootConfig.SpecialFloor+Net*.25));
    TestTrue(TEXT("Enter"),F.Player->TryToggleFormalRoot());TestTrue(TEXT("Attack after entry"),F.Attack(2.f,true));
    int32 Calls=0;F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32)
    {
        ++Calls;
        if(Calls==1)TestTrue(TEXT("Earlier event remains rooted"),F.Player->IsFormallyRooted());
        if(Calls==2)
        {
            TestFalse(TEXT("Exact exhaustion event observes mobile Mold"),F.Player->IsFormallyRooted());
            TestTrue(TEXT("Paid slice retains the previous rooted interval"),F.Player->IsFormallyRootedAtFrameOffset(0.));
            TestFalse(TEXT("Paid slice endpoint withdrew root"),F.Player->IsFormallyRootedAtFrameOffset(.125));
            TestEqual(TEXT("Exhaustion clamps the unique Special exactly"),F.Player->GetCombatSpecial(),F.RootConfig.SpecialFloor);
        }
    };
    F.Advance(.25f);TestEqual(TEXT("Both real animation delegates ran"),Calls,2);
    TestFalse(TEXT("Root exhausted"),F.Player->IsFormallyRooted());
    TestTrue(TEXT("Weapon survives automatic exit"),F.Player->WeaponAnimationComponent->IsBusy());
    TestTrue(TEXT("Movement immediately restored after boundary"),F.Player->GetCharacterMovement()->MovementMode==MOVE_Walking);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootSameTimeRuntimeTest,"DoughWorld.Combat.MoldRoot.RuntimeWithdrawalBeforeSameInstantDamageAndPause",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootSameTimeRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWMoldRootRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    auto* PauseOwner=F.Spawn<APlayerState>();if(!PauseOwner)return false;
    const double Net=F.RootConfig.SpendPerSecond-F.RootConfig.SpecialGainPerSecond;
    auto* S=F.Save(F.RootConfig.StartCost+Net*.25+F.RootConfig.SpecialFloor);if(!S)return false;
    S->CombatPlayerResources.Vitals.Water=F.VitalsConfig.Vitals.WaterFloor;
    S->CombatPlayerResources.Vitals.WaterEmptyElapsedSeconds=F.VitalsConfig.Vitals.WaterDamagePeriodSeconds-.25;
    TestTrue(TEXT("Strict water-boundary fixture"),F.Player->TryApplySaveData(S));TestTrue(TEXT("Enter"),F.Player->TryToggleFormalRoot());
    int32 Calls=0;float Nested=0.f;
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Calls;TestFalse(TEXT("Root withdrew before same-time damage notice"),F.Player->IsFormallyRooted());
        if(Calls==1)
        {
            FDamageEvent E;Nested=F.Player->TakeDamage(10.f,E,nullptr,nullptr);
            F.World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);
        }
    };
    F.Advance(.25f);
    TestEqual(TEXT("Same-time followup uses mobile Mold incoming"),Nested,static_cast<float>(10.*F.VitalsConfig.MoldIncomingMultiplier));
    TestEqual(TEXT("Both real notifications"),Calls,2);
    const double Special=F.Player->GetCombatSpecial(),Remaining=F.Player->GetCombatFormRemaining();
    F.Player->TickGameplayClocks(5.f,false);TestEqual(TEXT("Pause preserves Special"),F.Player->GetCombatSpecial(),Special);
    TestEqual(TEXT("Pause preserves form duration"),F.Player->GetCombatFormRemaining(),Remaining);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.Advance(0.f);
    TestEqual(TEXT("Resume cannot repay root fee"),F.Player->GetCombatSpecial(),Special);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootLifetimeRuntimeTest,"DoughWorld.Combat.MoldRoot.RuntimeExpiryDeathAndControlledRestoreInvalidateIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootLifetimeRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWMoldRootRuntimeTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    TestTrue(TEXT("Near expiry fixture"),F.Restore(F.RootConfig.StartCost+30.,.25));
    TestTrue(TEXT("Enter"),F.Player->TryToggleFormalRoot());FDWMoldRootIdentity Old;F.Player->GetFormalRootIdentity(Old);
    TestTrue(TEXT("Attack after entry"),F.Attack());F.Advance(.25f);
    TestFalse(TEXT("Expiry withdraws immediately"),F.Player->IsFormallyRooted());
    TestTrue(TEXT("Expiry preserves ordinary attack"),F.Player->WeaponAnimationComponent->IsBusy());
    TestTrue(TEXT("Expiry is Normal"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);
    TestTrue(TEXT("Restore next independent Mold life"),F.Restore(F.RootConfig.StartCost+30.));
    F.Player->WeaponAnimationComponent->bEnabled=false;TestTrue(TEXT("New root"),F.Player->TryToggleFormalRoot());
    FDWMoldRootIdentity New;TestTrue(TEXT("New identity"),F.Player->GetFormalRootIdentity(New));
    TestTrue(TEXT("Life and episode are not reused"),New.SourceLife.Life>Old.SourceLife.Life&&New.RootSerial>Old.RootSerial&&New.Receipt.Id!=Old.Receipt.Id);
    auto* Safe=F.Spawn<ADWCombatSafePoint>();if(!Safe)return false;
    Safe->SetActorLocation(F.Player->GetActorLocation());Safe->StablePointId=FGuid::NewGuid();Safe->ActivationRadiusCm=150.;
    if(!TestTrue(TEXT("Explicit isolated safe point"),Safe->ActivateForPlayer(F.Player)))return false;
    int32 Calls=0;F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {++Calls;TestFalse(TEXT("Fatal callback sees no root"),F.Player->IsFormallyRooted());FDWMoldRootIdentity I;TestFalse(TEXT("Fatal callback has no active root identity"),F.Player->GetFormalRootIdentity(I));};
    FDamageEvent E;const float Fatal=static_cast<float>((F.Player->Health-F.VitalsConfig.Vitals.HealthFloor)/F.RootConfig.IncomingMultiplier+10.);
    TestTrue(TEXT("Finite lethal damage"),F.Player->TakeDamage(Fatal,E,nullptr,nullptr)>0.f);
    TestEqual(TEXT("Actual fatal notification"),Calls,1);
    TestFalse(TEXT("Player-only restore cannot discard committed recovery facts"),F.Restore(F.RootConfig.StartCost+30.));
    const auto DeadLife=F.Authority->CaptureActorLife(F.Player);
    if(!TestTrue(TEXT("Controlled safe-point respawn from death"),F.Player->RequestCombatRespawn()))return false;
    TestTrue(TEXT("Respawn advances the death life"),F.Authority->CaptureActorLife(F.Player).Life>DeadLife.Life);
    TestFalse(TEXT("Respawn does not forge a root or ring"),F.Player->IsFormallyRooted());
    FDWMoldRootIdentity AfterRespawn;
    TestFalse(TEXT("Expired root identity remains invalid after respawn"),F.Player->GetFormalRootIdentity(AfterRespawn));
    TestTrue(TEXT("Respawn returns to Normal"),F.Player->GetCombatBaseForm()==EDWCombatBaseForm::Normal);
    TestTrue(TEXT("Controlled respawn restores walking"),F.Player->GetCharacterMovement()->MovementMode==MOVE_Walking);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootFeedbackCommittedTest,"DoughWorld.Combat.MoldRoot.RuntimeFeedbackSlotsFollowCommittedEntryAndExit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootFeedbackCommittedTest::RunTest(const FString&)
{
    using namespace DWMoldRootRuntimeTests;FFixture F;if(!TestTrue(TEXT("Existing isolated real floor/Mold fixture"),F.bReady))return false;
    auto* C=F.Player->CombatFeedback.Get();if(!TestNotNull(TEXT("Existing feedback component"),C))return false;
    TestTrue(TEXT("All root art/audio slots intentionally start empty"),!C->RootEnterSound&&!C->RootLoopSound&&!C->RootExitSound&&!C->RootEnterNiagara&&!C->RootLoopNiagara&&!C->RootExitNiagara);
    C->TickComponent(.1f,LEVELTICK_All,nullptr);TestFalse(TEXT("Unrooted tick cannot trigger presentation"),C->IsMoldRootFeedbackBound());
    const double Before=F.Player->GetCombatSpecial();TestTrue(TEXT("Real public E transaction enters root"),F.Player->TryToggleFormalRoot());
    TestEqual(TEXT("One committed entry schedules one presentation event"),C->RootEnterCount,1);TestEqual(TEXT("One loop state binding"),C->RootLoopBindCount,1);
    TestTrue(TEXT("Feedback tracks the real committed stance"),C->IsMoldRootFeedbackBound());
    TestTrue(TEXT("None slots never create pretend resources"),C->GetRootLoopAudioComponent()==nullptr&&C->GetRootLoopVFXComponent()==nullptr);
    for(int I=0;I<5;++I)C->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Ticks never replay root enter"),C->RootEnterCount,1);TestEqual(TEXT("Ticks never duplicate binding"),C->RootLoopBindCount,1);
    TestEqual(TEXT("Feedback cannot spend special"),F.Player->GetCombatSpecial(),Before-F.RootConfig.StartCost);
    TestTrue(TEXT("Real public E exits root"),F.Player->TryToggleFormalRoot());TestFalse(TEXT("Exit clears loop binding"),C->IsMoldRootFeedbackBound());
    TestEqual(TEXT("Exactly one ordinary exit presentation"),C->RootExitCount,1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootFeedbackRebuildTest,"DoughWorld.Combat.MoldRoot.RuntimeFeedbackReconstructionNeverReplaysEnter",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootFeedbackRebuildTest::RunTest(const FString&)
{
    using namespace DWMoldRootRuntimeTests;FFixture F;if(!TestTrue(TEXT("Existing isolated root fixture"),F.bReady&&F.Player->TryToggleFormalRoot()))return false;
    // New presenter attached to an already committed stance exercises the same derivation used after restore.
    // This is presentation reconstruction, not a file-load or full-world-persistence assertion.
    auto* Rebuilt=NewObject<UDWCombatFeedbackComponent>(F.Player);F.Player->AddInstanceComponent(Rebuilt);Rebuilt->RegisterComponent();
    const double Special=F.Player->GetCombatSpecial();Rebuilt->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Current valid root identity reconstructs loop binding"),Rebuilt->IsMoldRootFeedbackBound());
    TestEqual(TEXT("Reconstruction has one binding"),Rebuilt->RootLoopBindCount,1);TestEqual(TEXT("Reconstruction never replays enter"),Rebuilt->RootEnterCount,0);
    TestEqual(TEXT("No reconstruction fee"),F.Player->GetCombatSpecial(),Special);
    auto* Pauser=F.Spawn<APlayerState>();F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    Rebuilt->TickComponent(100.f,LEVELTICK_All,nullptr);TestTrue(TEXT("Pause preserves binding"),Rebuilt->IsMoldRootFeedbackBound());
    TestEqual(TEXT("Pause cannot replay enter"),Rebuilt->RootEnterCount,0);TestEqual(TEXT("Pause cannot rebind"),Rebuilt->RootLoopBindCount,1);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    TestTrue(TEXT("Real root exit"),F.Player->TryToggleFormalRoot());Rebuilt->TickComponent(.1f,LEVELTICK_All,nullptr);
    TestFalse(TEXT("Derived presenter cleans up when facts exit"),Rebuilt->IsMoldRootFeedbackBound());
    TestEqual(TEXT("Passive cleanup does not invent a second exit event"),Rebuilt->RootExitCount,0);
    Rebuilt->DestroyComponent();return true;
}
#endif
