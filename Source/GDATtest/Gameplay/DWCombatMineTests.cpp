#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatMines.h"
#include "DWCombatMine.h"
#include "DWCombatMineSubsystem.h"
#include "DWCombatMoldRoot.h"
#include "DWCombatRules.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatPlayerSave.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWGameInstance.h"
#include "DWSaveGame.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DWMineTests
{
struct FWorld
{
    TStrongObjectPtr<UDWGameInstance> GI;TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe;
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;FString Error;
    FWorld()
    {
        if(!GEngine)return;const FString Original=FCommandLine::Get();
        const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/D11_Mines")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
        FCommandLine::Set(*FString::Printf(TEXT("-DWVerificationSavePrefix=DW_QA_D11_ -DWSaveTestDirectory=\"%s\""),*Path));
        GI.Reset(NewObject<UDWGameInstance>(GEngine));GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWMineTest")),GetTransientPackage());
        FCommandLine::Set(*Original);World=GI->GetWorld();Probe.Reset(NewObject<UDWCombatAuthorityTestProbe>());
        if(World){FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();}
    }
    ~FWorld()
    {
        if(Probe.IsValid())Probe->AnyCallback=nullptr;
        if(World){World->GetWorldSettings()->SetPauserPlayerState(nullptr);World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
    }
    template<class T>T* Spawn(FVector P=FVector::ZeroVector)
    {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;return World->SpawnActor<T>(T::StaticClass(),P,FRotator::ZeroRotator,S);}
    AActor* Box(FVector Center,FVector Extent)
    {
        auto* Actor=Spawn<AActor>(Center);auto* C=NewObject<UBoxComponent>(Actor);Actor->SetRootComponent(C);Actor->AddInstanceComponent(C);
        C->SetBoxExtent(Extent);C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);C->SetCollisionResponseToAllChannels(ECR_Block);
        C->SetCollisionObjectType(ECC_WorldStatic);C->SetGenerateOverlapEvents(false);C->RegisterComponent();Actor->SetActorLocation(Center);return Actor;
    }
    bool MakePlayer()
    {
        Player=Spawn<ADWPlayerCharacter>(FVector(0,0,100));if(!Player||!Player->HasCombatVitalsReady())return false;
        Player->SetActorLocation(FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.));
        Player->SetActorTickEnabled(false);return Form(EDWCombatBaseForm::Mold);
    }
    bool Form(EDWCombatBaseForm Form)
    {
        TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());Player->CaptureSaveData(Save.Get());
        auto& V=Save->CombatPlayerResources.Vitals;FDWCombatPlayerVitalsConfig C;
        if(!FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),C,Error))return false;
        V.BaseForm=Form;V.FormRemainingSeconds=Form==EDWCombatBaseForm::Normal?0.:C.FormDurationSeconds;
        V.Special=Form==EDWCombatBaseForm::Normal?C.Vitals.NormalSpecialValue:C.Vitals.SpecialMax;
        V.FormTimeCarry=V.SpecialGrowthCarry=0.;Save->Transformation=static_cast<float>(V.Special);Save->bYeastForm=Form==EDWCombatBaseForm::Yeast;
        if(!DWCombatPlayerSave::Validate(*GI->GetCombatRules(),*Save,Error)||!Player->TryApplySaveData(Save.Get()))return false;
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);return true;
    }
    UDWCombatMineSubsystem* Mines() const{return UDWCombatMineSubsystem::Get(World);}
    TArray<ADWCombatMine*> Physical() const
    {TArray<ADWCombatMine*> Out;for(TActorIterator<ADWCombatMine> It(World);It;++It)if(!It->IsActorBeingDestroyed())Out.Add(*It);return Out;}
    ADWEnemyCharacter* Enemy(FVector Position,EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::DoughZombie)
    {
        auto* E=Spawn<ADWEnemyCharacter>(Position);if(!E)return nullptr;E->CombatProfile=Profile;
        if(!E->InitializeFormalCombat(*GI->GetCombatRules(),Error))return nullptr;E->SetActorTickEnabled(false);
        return E;
    }
    void PlaceAtMine(ADWEnemyCharacter& E,const ADWCombatMine& M,double X=0.)
    {E.SetActorLocation(M.GetActorLocation()+FVector(X,0,E.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+1.));}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMineRuleSnapshotTest,"DoughWorld.Combat.Mines.FormalUnitsGeometryAndSnapshotRejectsDuplicates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMineRuleSnapshotTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWMoldMineConfig C;
    if(!TestTrue(TEXT("Load rule payload"),FDWCombatRules::LoadProjectPayload(Rules,Error))||!TestTrue(TEXT("Ten mine parameters"),FDWMoldMineConfig::Build(*Rules,C,Error)))return false;
    double Source=0.;Rules->TryGetNumber(TEXT("mold.mine.ring_radius_m"),EDWCombatRuleUnit::Meters,Source,Error);
    TestEqual(TEXT("Meters converted once"),C.RingRadiusCm,Source*100.);
    for(int32 I=1;I<=C.Count;++I)TestTrue(TEXT("Uniform full-circle point stays at configured radius"),FMath::IsNearlyEqual(C.Point(FVector::ZeroVector,I).Size2D(),C.RingRadiusCm,1.e-7));
    auto Bad=C;Bad.Damage=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("Nonfinite cannot reach authority"),Bad.IsValid());
    FDWMoldMineRingSave S;S.CooldownRemainingSeconds=17.;TestTrue(TEXT("Committed remaining cooldown survives later tuning, no default clamp"),DWMoldMines::ValidateSnapshot(S,Error));
    S.bActive=true;S.RingId=FGuid::NewGuid();FDWMoldMineSave M;M.MineId=FGuid::NewGuid();M.Ordinal=1;S.Mines.Add(M);
    TestTrue(TEXT("Stable one-mine snapshot"),DWMoldMines::ValidateSnapshot(S,Error));S.Mines.Add(M);
    TestFalse(TEXT("Duplicate physical ID/ordinal rejects snapshot"),DWMoldMines::ValidateSnapshot(S,Error));
    S.Mines.Pop();S.HitFormalEnemies={FGuid::NewGuid()};const FGuid Repeated=S.HitFormalEnemies[0];S.HitFormalEnemies.Add(Repeated);
    TestFalse(TEXT("Duplicate whole-ring hit identity rejects snapshot"),DWMoldMines::ValidateSnapshot(S,Error));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMineGroundTest,"DoughWorld.Combat.Mines.ActualRootFullPartialAndNoGroundNoRefund",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMineGroundTest::RunTest(const FString&)
{
    for(int32 Case=0;Case<3;++Case)
    {
        DWMineTests::FWorld F;if(!TestNotNull(TEXT("Isolated world"),F.World))return false;
        // Full floor; half-plane plus root island; root island only. No artificial point relocation.
        if(Case==0)F.Box(FVector(0,0,-10),FVector(600,600,10));
        else if(Case==1){F.Box(FVector(250,0,-10),FVector(250,600,10));F.Box(FVector(0,0,-10),FVector(60,60,10));}
        else F.Box(FVector(0,0,-10),FVector(60,60,10));
        if(!TestTrue(TEXT("Ready real Mold player"),F.MakePlayer()))return false;
        FDWMoldMineConfig C;FDWMoldRootConfig R;FDWMoldMineConfig::Build(*F.GI->GetCombatRules(),C,F.Error);FDWMoldRootConfig::Build(*F.GI->GetCombatRules(),R,F.Error);
        const double Before=F.Player->GetCombatSpecial();if(!TestTrue(TEXT("Actual paid root entry"),F.Player->TryToggleFormalRoot()))return false;
        const int32 N=F.Mines()->GetLiveMineCount(F.Player);
        if(Case==0)TestEqual(TEXT("Full legal floor creates configured count"),N,C.Count);
        else if(Case==1)TestTrue(TEXT("Cliff creates only legal subset"),N>0&&N<C.Count);
        else TestEqual(TEXT("No legal ring points creates zero entities"),N,0);
        TestEqual(TEXT("Valid root fee remains even with no mine"),F.Player->GetCombatSpecial(),Before-R.StartCost);
        TestEqual(TEXT("Cooldown iff an actual entity began play"),F.Mines()->GetCooldownRemaining(F.Player)>0.,N>0);
        FDWMoldRootIdentity Identity;F.Player->GetFormalRootIdentity(Identity);F.Mines()->OnRootCommitted(F.Player,Identity);
        TestEqual(TEXT("Replayed entry callback cannot refill or retry"),F.Mines()->GetLiveMineCount(F.Player),N);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMineFactionRingDedupTest,"DoughWorld.Combat.Mines.NeutralAndAlreadyHitDoNotConsumeOtherMines",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMineFactionRingDedupTest::RunTest(const FString&)
{
    DWMineTests::FWorld F;if(!F.World)return false;F.Box(FVector(0,0,-10),FVector(600,600,10));
    if(!F.MakePlayer()||!F.Player->TryToggleFormalRoot())return false;auto Mines=F.Physical();if(!TestTrue(TEXT("Two physical mines exist"),Mines.Num()>=2))return false;
    auto* Neutral=F.Enemy(FVector(1000,0,100),EDWEnemyCombatProfile::MoldGrunt);auto* Enemy=F.Enemy(FVector(1200,0,100));if(!Neutral||!Enemy)return false;
    F.PlaceAtMine(*Neutral,*Mines[0]);TestFalse(TEXT("Same-faction unprovoked enemy does not trigger"),Mines[0]->TryTrigger());
    const int32 Count=F.Mines()->GetLiveMineCount(F.Player);F.PlaceAtMine(*Enemy,*Mines[0]);const float Health=Enemy->Health;
    FDWMoldMineConfig C;FDWMoldMineConfig::Build(*F.GI->GetCombatRules(),C,F.Error);
    TestTrue(TEXT("True hostile triggers one actual mine"),Mines[0]->TryTrigger());TestEqual(TEXT("Independent ability damage, no root multiplier"),Enemy->Health,Health-static_cast<float>(C.Damage));
    TestEqual(TEXT("Only triggered mine consumed"),F.Mines()->GetLiveMineCount(F.Player),Count-1);TestFalse(TEXT("Same physical emitter cannot detonate twice"),Mines[0]->TryTrigger());
    F.PlaceAtMine(*Enemy,*Mines[1]);TestFalse(TEXT("Previously hit entity cannot waste next mine"),Mines[1]->TryTrigger());
    TestEqual(TEXT("Ring-wide hit stays one"),Enemy->Health,Health-static_cast<float>(C.Damage));
    FDWMoldMineRingSave S;TestTrue(TEXT("Stable ring capture"),F.Mines()->CaptureSave(F.Player,S,F.Error));
    TestEqual(TEXT("One persistent formal target identity across the entire ring"),S.HitFormalEnemies.Num(),1);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMineBatchReentryTest,"DoughWorld.Combat.Mines.AllVictimFactsBeforeFirstDynamicUnrootNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMineBatchReentryTest::RunTest(const FString&)
{
    DWMineTests::FWorld F;if(!F.World)return false;F.Box(FVector(0,0,-10),FVector(600,600,10));
    if(!F.MakePlayer()||!F.Player->TryToggleFormalRoot())return false;auto Mines=F.Physical();if(Mines.IsEmpty())return false;
    auto* E1=F.Enemy(FVector(1000,0,100));auto* E2=F.Enemy(FVector(1200,0,100));if(!E1||!E2)return false;
    F.PlaceAtMine(*E1,*Mines[0]);F.PlaceAtMine(*E2,*Mines[0],100.);const float H1=E1->Health,H2=E2->Health;
    FDWMoldMineConfig C;FDWMoldMineConfig::Build(*F.GI->GetCombatRules(),C,F.Error);int32 Calls=0;
    F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Calls;TestEqual(TEXT("First listener already sees first victim fact"),E1->Health,H1-static_cast<float>(C.Damage));
        TestEqual(TEXT("First listener already sees second victim fact"),E2->Health,H2-static_cast<float>(C.Damage));
        if(Calls==1){TestTrue(TEXT("Dynamic listener deliberately unroots"),F.Player->TryToggleFormalRoot());TestEqual(TEXT("All root emitters retired inside unroot facts"),F.Mines()->GetLiveMineCount(F.Player),0);}
    };
    E1->OnTakeAnyDamage.AddDynamic(F.Probe.Get(),&UDWCombatAuthorityTestProbe::Any);E2->OnTakeAnyDamage.AddDynamic(F.Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    TestTrue(TEXT("One explosion committed"),Mines[0]->TryTrigger());TestEqual(TEXT("Both real dynamic damage notifications survive source unroot"),Calls,2);
    TestFalse(TEXT("Root ended by first callback"),F.Player->IsFormallyRooted());F.Mines()->FlushRootCleanup(F.Player);
    TestEqual(TEXT("All physical root mines cleaned"),F.Physical().Num(),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMineWallAndPauseTest,"DoughWorld.Combat.Mines.VisibilityWallAndRealPauseBlockTrigger",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMineWallAndPauseTest::RunTest(const FString&)
{
    DWMineTests::FWorld F;if(!F.World)return false;F.Box(FVector(0,0,-10),FVector(600,600,10));
    if(!F.MakePlayer()||!F.Player->TryToggleFormalRoot())return false;auto Mines=F.Physical();if(Mines.IsEmpty())return false;
    auto* E=F.Enemy(FVector(1000,0,100));if(!E)return false;F.PlaceAtMine(*E,*Mines[0],60.);
    auto* Wall=F.Box(Mines[0]->GetActorLocation()+FVector(15,0,50),FVector(3,80,100));
    TestFalse(TEXT("Blocking wall prevents trigger/damage"),Mines[0]->TryTrigger());const float Health=E->Health;Wall->Destroy();
    auto* Pauser=F.Spawn<APlayerState>();F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    const double Remaining=F.Mines()->GetCooldownRemaining(F.Player);F.World->Tick(LEVELTICK_All,1.f);F.Mines()->Tick(1.f);
    TestFalse(TEXT("Real world pause refuses trigger"),Mines[0]->TryTrigger());TestEqual(TEXT("Pause does not run cooldown"),F.Mines()->GetCooldownRemaining(F.Player),Remaining);
    TestEqual(TEXT("Paused target unharmed"),E->Health,Health);F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    TestTrue(TEXT("Removing wall and resuming allows actual hit"),Mines[0]->TryTrigger());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMineCooldownDeathTest,"DoughWorld.Combat.Mines.CooldownSurvivesUnrootFormRestoreAndDeath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMineCooldownDeathTest::RunTest(const FString&)
{
    DWMineTests::FWorld F;if(!F.World)return false;F.Box(FVector(0,0,-10),FVector(600,600,10));
    if(!F.MakePlayer()||!F.Player->TryToggleFormalRoot())return false;
    const double Initial=F.Mines()->GetCooldownRemaining(F.Player);TestTrue(TEXT("Actual release paid a cooldown"),Initial>0.);
    TestTrue(TEXT("Unroot"),F.Player->TryToggleFormalRoot());TestEqual(TEXT("Unroot keeps cooldown"),F.Mines()->GetCooldownRemaining(F.Player),Initial);
    TestTrue(TEXT("Deliberate reentry within cooldown"),F.Player->TryToggleFormalRoot());TestEqual(TEXT("Reentry grants no replacement ring"),F.Mines()->GetLiveMineCount(F.Player),0);
    TestTrue(TEXT("Real form restore to Normal"),F.Form(EDWCombatBaseForm::Normal));TestEqual(TEXT("Form end retains cooldown"),F.Mines()->GetCooldownRemaining(F.Player),Initial);
    F.World->Tick(LEVELTICK_All,.5f);TestTrue(TEXT("Normal form game time consumes remaining cooldown"),F.Mines()->GetCooldownRemaining(F.Player)<Initial);
    const double BeforeDeath=F.Mines()->GetCooldownRemaining(F.Player);FDamageEvent Event;F.Player->TakeDamage(10000.f,Event,nullptr,nullptr);
    TestTrue(TEXT("Actual native death"),F.Player->IsDead());TestEqual(TEXT("Death does not reset cooldown"),F.Mines()->GetCooldownRemaining(F.Player),BeforeDeath);
    FDWMoldMineRingSave S;TestTrue(TEXT("Controlled save snapshot after death"),F.Mines()->CaptureSave(F.Player,S,F.Error));
    TestFalse(TEXT("Death snapshot cannot preserve a live defensive ring"),S.bActive);TestEqual(TEXT("Remaining clock is exact snapshot fact"),S.CooldownRemainingSeconds,BeforeDeath);
    return true;
}
#endif
