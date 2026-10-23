#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatWorldObjectsPersistence.h"
#include "DWCombatEffectsPersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWDamageVolumeLibrary.h"
#include "DWCombatTestWorld.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerCharacter.h"
#include "DWSaveGame.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"
#include <limits>
namespace DWWorldSaveTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* W=nullptr;ADWPlayerCharacter* P=nullptr;ADWEnemyCharacter* E=nullptr;bool Ready=false;FString Error;
    FFixture()
    {
        W=DWCombatTestWorld::Create(GI,TEXT("D22WorldObjects"));if(!W)return;
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        P=W->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(80,0,110),FRotator::ZeroRotator,Params);
        if(!P)return;if(!P->HasActorBegunPlay())P->DispatchBeginPlay();
        auto* Save=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*Save,Error)||!P->TryApplySaveData(Save))return;
        E=W->SpawnActor<ADWEnemyCharacter>(ADWEnemyCharacter::StaticClass(),FVector(0,0,110),FRotator::ZeroRotator,Params);if(!E)return;
        E->CombatProfile=EDWEnemyCombatProfile::DoughElite;if(!E->HasActorBegunPlay())E->DispatchBeginPlay();
        if(!E->IsFormalCombatReady()&&!E->InitializeFormalCombat(*GI->GetCombatRules(),Error))return;
        Ready=UDWCombatRecoverySubsystem::Get(P)->BindNewPlayer(*P)&&!GI->HasActiveSlot();
    }
    ~FFixture(){if(!W)return;for(TActorIterator<AActor> It(W);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
    bool Capture(FDWCombatWorldObjectsSave& S,TArray<FDWPersistedActorState>& L,TArray<FDWCombatObjectBinding>& B){return FDWCombatWorldObjectsPersistence::Capture(*W,*P,S,L,B,Error);}
};
FDWCombatWorldObjectsSave Empty()
{FDWCombatWorldObjectsSave S;S.MapPackage=TEXT("/Game/QA/WorldSave");S.Recovery.Schema=1;S.Recovery.PlayerId=FGuid::NewGuid();return S;}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWorldCodecTest,"DoughWorld.Combat.WorldSave.StrictCodecRejectsCorruption",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWorldCodecTest::RunTest(const FString&)
{
    auto S=DWWorldSaveTests::Empty();FString Error;TArray<uint8> Bytes;TestTrue(TEXT("Explicit empty world encodes"),DWCombatWorldObjectsSave::Encode(S,Bytes,Error));
    FDWCombatWorldObjectsSave Decoded;TestTrue(TEXT("Roundtrip"),DWCombatWorldObjectsSave::Decode(Bytes,Decoded,Error));TestEqual(TEXT("Recovery identity"),Decoded.Recovery.PlayerId,S.Recovery.PlayerId);
    auto Bad=Bytes;Bad.Add(0);auto Kept=Decoded;TestFalse(TEXT("Trailing byte fails"),DWCombatWorldObjectsSave::Decode(Bad,Decoded,Error));TestEqual(TEXT("Decode failure retains output"),Decoded.Recovery.PlayerId,Kept.Recovery.PlayerId);
    Bad=Bytes;Bad.SetNum(Bad.Num()-1);TestFalse(TEXT("Truncated final value fails"),DWCombatWorldObjectsSave::Decode(Bad,Decoded,Error));
    Bad=Bytes;Bad[0]^=1;TestFalse(TEXT("Wrong magic fails"),DWCombatWorldObjectsSave::Decode(Bad,Decoded,Error));
    S.MapPackage=TEXT("/Game/QA/World|suffix");S.MapPackage[14]=TCHAR(0);TestTrue(TEXT("Actual embedded NUL fixture"),S.MapPackage.Len()>FCString::Strlen(*S.MapPackage));
    TestFalse(TEXT("Embedded NUL path rejected"),DWCombatWorldObjectsSave::Validate(S,Error));
    S=DWWorldSaveTests::Empty();S.NestClocks.Add({TEXT("nest:test"),std::numeric_limits<double>::infinity()});TestFalse(TEXT("Nonfinite clock rejected"),DWCombatWorldObjectsSave::Validate(S,Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWorldStaleTest,"DoughWorld.Combat.WorldSave.StalePreparedWorldChangesNothing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWorldStaleTest::RunTest(const FString&)
{
    DWWorldSaveTests::FFixture F;if(!TestTrue(TEXT("Isolated fixture"),F.Ready))return false;FDWCombatWorldObjectsSave S;TArray<FDWPersistedActorState>L;TArray<FDWCombatObjectBinding>B;
    if(!TestTrue(TEXT("Actual capture"),F.Capture(S,L,B)))return false;
    FDWCombatWorldObjectsRestorePlan World;if(!TestTrue(TEXT("Prepare scene objects"),FDWCombatWorldObjectsPersistence::PrepareWorldObjects(*F.W,*F.P,S,L,World,F.Error)))return false;
    auto* A=UDWCombatAuthoritySubsystem::Get(F.P);FDWCombatEffectsSave Effects;FDWCombatEffectsRestorePlan Plan;
    if(!TestTrue(TEXT("Capture authority"),FDWCombatEffectsPersistence::Capture(*A,World.LiveBindings(),Effects,F.Error))||
        !TestTrue(TEXT("Prepare identity"),FDWCombatEffectsPersistence::PrepareRestore(*A,Effects,World.LiveBindings(),World.RestoredBindings(),World.KeepActionIds(),Plan,F.Error))||
        !TestTrue(TEXT("Prepare world facts"),FDWCombatWorldObjectsPersistence::PrepareWorldFacts(World,Plan.Authority(),F.Error)))return false;
    const float Before=F.E->Health;F.E->Health=Before-1.f;
    TestFalse(TEXT("Same-life changed health invalidates candidate"),World.CanCommit(F.Error));TestEqual(TEXT("Rejected candidate does not restore old health"),F.E->Health,Before-1.f);TestFalse(TEXT("Player unchanged/living"),F.P->IsDead());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWorldEnemyPhaseTest,"DoughWorld.Combat.WorldSave.EnemyWindupAndActionSurviveNativeRestore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWorldEnemyPhaseTest::RunTest(const FString&)
{
    DWWorldSaveTests::FFixture F;if(!TestTrue(TEXT("Isolated fixture"),F.Ready))return false;
    // Native Player has no BP-authored UBox hurtbox. Formal enemy geometry
    // intentionally rejects capsule/decorative fallback; reproduce the real contract.
    TestFalse(TEXT("A native player without an explicit box is not a formal melee target"),F.E->TryStartMeleeAttack(F.P));
    auto* Hurtbox=NewObject<UDWDamageHitboxComponent>(F.P,TEXT("D22PhaseFixtureHurtbox"));
    F.P->AddInstanceComponent(Hurtbox);Hurtbox->SetupAttachment(F.P->GetRootComponent());
    Hurtbox->SetBoxExtent(FVector(30,30,60));Hurtbox->RegisterComponent();
    if(!TestTrue(TEXT("Formal windup accepted"),F.E->TryStartMeleeAttack(F.P)))return false;
    FDWCombatWorldObjectsSave S;TArray<FDWPersistedActorState>L;TArray<FDWCombatObjectBinding>B;if(!TestTrue(TEXT("Capture live windup"),F.Capture(S,L,B)))return false;
    auto* A=UDWCombatAuthoritySubsystem::Get(F.P);FDWCombatEffectsSave Effects;if(!TestTrue(TEXT("Capture matching authority"),FDWCombatEffectsPersistence::Capture(*A,B,Effects,F.Error)))return false;
    const auto Saved=S.Enemies[0];F.E->CancelMeleeAttack();FDWCombatWorldObjectsRestorePlan World;FDWCombatEffectsRestorePlan Plan;
    if(!TestTrue(TEXT("Prepare world"),FDWCombatWorldObjectsPersistence::PrepareWorldObjects(*F.W,*F.P,S,L,World,F.Error))||
        !TestTrue(TEXT("Prepare saved authority"),FDWCombatEffectsPersistence::PrepareRestore(*A,Effects,World.LiveBindings(),World.RestoredBindings(),World.KeepActionIds(),Plan,F.Error))||
        !TestTrue(TEXT("Crosscheck saved attack ownership"),FDWCombatWorldObjectsPersistence::PrepareWorldFacts(World,Plan.Authority(),F.Error))||
        !TestTrue(TEXT("Whole world current"),World.CanCommit(F.Error))||!TestTrue(TEXT("Effects current"),Plan.CanCommit(F.Error))||
        !TestTrue(TEXT("Acquire unique barrier"),A->BeginWorldRestoreBarrier(Plan.Authority(),F.Error)))return false;
    FDWCombatEffectsPersistence::CommitFactsUnchecked(Plan);FDWCombatWorldObjectsPersistence::AdoptNativeFacts(World);A->FinishWorldRestoreBarrier();
    TestTrue(TEXT("Publish restored poses"),FDWCombatWorldObjectsPersistence::Publish(World,F.Error));FDWCombatEffectsPersistence::PublishRestoredEffects(Plan);
    FDWCombatWorldObjectsSave After;TArray<FDWPersistedActorState>AL;TArray<FDWCombatObjectBinding>AB;if(!TestTrue(TEXT("Capture restored world"),F.Capture(After,AL,AB)))return false;
    const auto& Restored=After.Enemies[0];TestEqual(TEXT("Same phase"),Restored.Phase,Saved.Phase);TestEqual(TEXT("Same remaining phase time"),Restored.PhaseRemaining,Saved.PhaseRemaining);
    TestEqual(TEXT("Same logical action number"),Restored.MeleeAction.Id,Saved.MeleeAction.Id);TestTrue(TEXT("Fresh epoch"),Restored.MeleeAction.Epoch!=Saved.MeleeAction.Epoch);
    TestEqual(TEXT("No new attack accepted"),Restored.AttackSerial,Saved.AttackSerial);TestEqual(TEXT("Unspent hit preserved"),Restored.bAttackResolved,Saved.bAttackResolved);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWorldTombstoneTest,"DoughWorld.Combat.WorldSave.DestroyedFormalCorpseRemainsCommitted",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWorldTombstoneTest::RunTest(const FString&)
{
    DWWorldSaveTests::FFixture F;if(!TestTrue(TEXT("Isolated fixture"),F.Ready))return false;
    F.E->TakeDamage(F.E->Health*2.f,FDamageEvent(),nullptr,F.P);if(!TestFalse(TEXT("Real fatal authority commit"),F.E->IsAlive()))return false;
    const auto Drop=F.E->CaptureFormalCombatSave();TestTrue(TEXT("Explicit terminal elite drop decision"),Drop.DropDecision==EDWEnemyCoreDropDecision::Ineligible);
    F.E->Destroy();FDWCombatWorldObjectsSave S;TArray<FDWPersistedActorState>L;TArray<FDWCombatObjectBinding>B;
    if(!TestTrue(TEXT("Capture after corpse destruction"),F.Capture(S,L,B)))return false;
    TestEqual(TEXT("Death still represented"),S.Enemies.Num(),1);if(S.Enemies.Num()!=1)return false;
    TestTrue(TEXT("Explicit ended tombstone"),S.Enemies[0].bEnded);TestEqual(TEXT("Same death identity"),S.Enemies[0].Formal.CoreDropDeathId,Drop.CoreDropDeathId);TestEqual(TEXT("One terminal drop fact"),S.CoreDrops.Num(),1);
    auto Bad=S;Bad.Enemies.Reset();FString Error;TestFalse(TEXT("Drop cannot omit its destroyed enemy"),DWCombatWorldObjectsSave::Validate(Bad,Error));return true;
}
#endif
