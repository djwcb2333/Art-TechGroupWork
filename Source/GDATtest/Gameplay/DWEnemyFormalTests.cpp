#if WITH_DEV_AUTOMATION_TESTS
#include "DWEnemyCombatProfile.h"
#include "DWCombatPlayerVitals.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatOverlapTestProbe.h"
#include "DWDamageVolumeLibrary.h"
#include "DWGameInstance.h"
#include "DWWeaponAnimationComponent.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DWEnemyFormalTests
{
    struct FWorld
    {
        UWorld* World=nullptr;TStrongObjectPtr<UDWGameInstance> GI;
        TSharedPtr<const FDWCombatRules> Rules;FString Error;
        FWorld()
        {
            if(!GEngine||!FDWCombatRules::LoadProjectPayload(Rules,Error))return;
            const FString SavedCommandLine=FCommandLine::Get();
            const FString TestDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/D06_FormalEnemy")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
            // Init normally initializes save storage. Replace, rather than append, duplicate CLI keys;
            // the QA prefix disables migration and the absolute directory isolates every fixture.
            FCommandLine::Set(*FString::Printf(TEXT("-DWVerificationSavePrefix=DW_QA_D06_ -DWSaveTestDirectory=\"%s\""),*TestDirectory));
            GI.Reset(NewObject<UDWGameInstance>(GEngine));
            GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWFormalEnemyTest")),GetTransientPackage());
            FCommandLine::Set(*SavedCommandLine);World=GI->GetWorld();if(!World)return;
            FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
        }
        ~FWorld(){if(World){World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
        template<class T>T* Spawn(FVector P=FVector(0,0,500))
        {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* A=World?World->SpawnActor<T>(T::StaticClass(),P,FRotator::ZeroRotator,S):nullptr;if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;}
        ADWEnemyCharacter* Enemy(EDWEnemyCombatProfile Profile,FVector P=FVector(0,0,500))
        {auto* E=Spawn<ADWEnemyCharacter>(P);if(!E)return nullptr;E->CombatProfile=Profile;E->bShowCombatDebug=false;return E->InitializeFormalCombat(*Rules,Error)?E:nullptr;}
        ADWPlayerCharacter* Player(FVector P)
        {
            auto* A=Spawn<ADWPlayerCharacter>(P);if(!A)return nullptr;
            A->WeaponAnimationComponent->bEnabled=false;
            auto* Hurtbox=NewObject<UDWDamageHitboxComponent>(A,TEXT("D06FixtureHurtbox"));
            A->AddInstanceComponent(Hurtbox);Hurtbox->SetupAttachment(A->GetRootComponent());Hurtbox->SetBoxExtent(FVector(30,30,60));Hurtbox->RegisterComponent();
            return A;
        }
        void Advance(ADWEnemyCharacter* E,float Dt){World->TimeSeconds+=Dt;E->Tick(Dt);}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalProfilesTest,"DoughWorld.Combat.FormalEnemy.RuleProjectionAndLegacyIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalProfilesTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;
    if(!TestTrue(TEXT("Load sole formal numeric payload"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
    const EDWEnemyCombatProfile Profiles[]={EDWEnemyCombatProfile::MoldGrunt,EDWEnemyCombatProfile::YeastGrunt,EDWEnemyCombatProfile::DoughZombie,EDWEnemyCombatProfile::DoughElite};
    const double Healths[]={100.,80.,90.,300.},Speeds[]={380.,440.,320.,280.};
    for(int32 I=0;I<4;++I)
    {
        FDWEnemyCombatConfig C;if(!TestTrue(TEXT("Independent profile resolves all rule bindings"),DWEnemyFormal::BuildConfig(*Rules,Profiles[I],C,Error)))return false;
        TestEqual(TEXT("Four independent health values"),C.Health,Healths[I]);TestEqual(TEXT("Meters per second converted once"),C.MoveSpeedCmPerSecond,Speeds[I]);
        TestEqual(TEXT("Half-windup lock configured"),C.WindupLockFraction,.5);TestEqual(TEXT("One target at most once per attack"),C.HitsPerTarget,1);
        TestEqual(TEXT("Full angle stays full until geometry boundary"),C.AttackFullAngleDegrees,I==3?120.:100.);
        TestEqual(TEXT("Disengage distance separate from alert distance"),C.DisengageDistanceCm,I==3?1500.:1200.);
        TestEqual(TEXT("No legacy minimum interval inserted"),C.ExtraCooldownSeconds,0.);
    }
    FDWEnemyCombatConfig Sentinel;Sentinel.Health=123.;
    TestFalse(TEXT("Legacy never silently becomes a new faction"),DWEnemyFormal::BuildConfig(*Rules,EDWEnemyCombatProfile::Legacy,Sentinel,Error));
    TestEqual(TEXT("Failed profile build is atomic"),Sentinel.Health,123.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalMemoryTest,"DoughWorld.Combat.FormalEnemy.FactionAndContinuousDisengage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalMemoryTest::RunTest(const FString&)
{
    using P=EDWEnemyCombatProfile;using F=EDWCombatBaseForm;
    TestTrue(TEXT("Normal belongs to neither faction"),DWEnemyFormal::IsHostile(P::MoldGrunt,F::Normal,false)&&DWEnemyFormal::IsHostile(P::YeastGrunt,F::Normal,false));
    TestFalse(TEXT("Unengaged same faction neutral"),DWEnemyFormal::IsHostile(P::MoldGrunt,F::Mold,false));
    TestTrue(TEXT("An engaged enemy remembers through a form switch"),DWEnemyFormal::IsHostile(P::MoldGrunt,F::Mold,true));
    TestTrue(TEXT("Dough faction always hostile"),DWEnemyFormal::IsHostile(P::DoughElite,F::Mold,false)&&DWEnemyFormal::IsHostile(P::DoughZombie,F::Yeast,false));
    FDWEnemyCombatMemory A,B;A.Engage();TestFalse(TEXT("No shared neighbor aggro"),B.bEngaged);
    FDWEnemyCombatConfig C;C.DisengageDistanceCm=1200.;C.DisengageSeconds=6.;
    TestFalse(TEXT("Exactly distance threshold does not qualify"),A.ObserveDisengage(0.,1200.,false,false,C));
    A.ObserveDisengage(1.,1201.,false,false,C);A.ObserveDisengage(6.9,1201.,false,false,C);
    TestTrue(TEXT("Not six continuous seconds"),A.bEngaged);
    TestFalse(TEXT("Pause same time contributes zero"),A.ObserveDisengage(6.9,1201.,false,false,C));
    A.ObserveDisengage(7.,1201.,true,false,C);TestEqual(TEXT("Sight resets partial progress"),A.DisengageElapsedSeconds,0.);
    A.ObserveDisengage(8.,1201.,false,false,C);A.ObserveDisengage(13.,1201.,false,false,C);
    A.ObserveDisengage(13.5,1201.,false,true,C);TestEqual(TEXT("Either participant hurt resets progress"),A.DisengageElapsedSeconds,0.);
    A.ObserveDisengage(14.,1201.,false,false,C);
    TestTrue(TEXT("Exact six-second uninterrupted boundary disengages"),A.ObserveDisengage(20.,1201.,false,false,C));
    TestFalse(TEXT("Legal disengage clears only this enemy memory"),A.bEngaged);TestTrue(TEXT("Return state begins without healing operation"),A.bReturningHome);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreDecisionTest,"DoughWorld.Combat.FormalEnemy.CoreDecisionStrictProbabilityAndNoReroll",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreDecisionTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;
    if(!TestTrue(TEXT("Rules"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
    FDWEnemyCoreDropConfig C;if(!TestTrue(TEXT("Five core parameters"),DWEnemyFormal::BuildCoreDropConfig(*Rules,C,Error)))return false;
    TestEqual(TEXT("Pickup radius boundary conversion"),C.PickupRadiusCm,150.);TestEqual(TEXT("Core stack cap independent of global inventory cap"),C.StackLimit,40);
    FDWEnemyCoreDropRecord Failed;const FGuid Id=FGuid::NewGuid();
    TestTrue(TEXT("Equal probability sample resolves as failure"),DWEnemyFormal::DecideCoreDrop(Failed,EDWEnemyCombatProfile::MoldGrunt,C,{1,10},{2,1},{3,1},.5,Id));
    TestEqual(TEXT("Strict less-than, not <="),Failed.Decision,EDWEnemyCoreDropDecision::Failed);
    TestFalse(TEXT("Duplicate death cannot reroll failure with a better sample"),DWEnemyFormal::DecideCoreDrop(Failed,EDWEnemyCombatProfile::MoldGrunt,C,{1,10},{2,1},{3,1},0.,Id));
    FDWEnemyCoreDropRecord Won;
    TestTrue(TEXT("Below boundary selects exactly one matching core"),DWEnemyFormal::DecideCoreDrop(Won,EDWEnemyCombatProfile::YeastGrunt,C,{1,11},{4,1},{3,1},.499999,Id));
    TestEqual(TEXT("Correct separate core ID"),Won.CoreItemId,FDWCombatItemRegistry::YeastCoreId());TestEqual(TEXT("Configured count"),Won.Count,C.Count);
    FDWEnemyCoreDropRecord Environment;
    TestTrue(TEXT("Unattributed death records ineligibility"),DWEnemyFormal::DecideCoreDrop(Environment,EDWEnemyCombatProfile::MoldGrunt,C,{1,12},{5,1},{},0.,Id));
    TestEqual(TEXT("No forever-was-hit player credit"),Environment.Decision,EDWEnemyCoreDropDecision::Ineligible);
    FDWEnemyCoreDropRecord Invalid;
    TestFalse(TEXT("Nonfinite RNG rejected before state write"),DWEnemyFormal::DecideCoreDrop(Invalid,EDWEnemyCombatProfile::MoldGrunt,C,{1,13},{6,1},{3,1},std::numeric_limits<double>::quiet_NaN(),Id));
    TestEqual(TEXT("Invalid preparation not consumed"),Invalid.Decision,EDWEnemyCoreDropDecision::Unresolved);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalWindupAndKnockbackTest,"DoughWorld.Combat.FormalEnemy.HalfWindupLockAndStageKnockback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalWindupAndKnockbackTest::RunTest(const FString&)
{
    DWEnemyFormalTests::FWorld F;if(!TestNotNull(TEXT("Transient world"),F.World))return false;
    auto* Enemy=F.Enemy(EDWEnemyCombatProfile::MoldGrunt);auto* Player=F.Player(FVector(140,0,500));
    if(!TestNotNull(TEXT("Formal enemy initialized"),Enemy)||!TestNotNull(TEXT("Player"),Player))return false;
    if(!TestTrue(TEXT("Player uses real initialized formal resources"),Player->HasCombatVitalsReady()))return false;
    TestTrue(TEXT("Start formal enemy attack at live player"),Enemy->TryStartMeleeAttack(Player));
    Player->SetActorLocation(FVector(100,100,500));F.Advance(Enemy,.2f);
    TestTrue(TEXT("First half follows target"),Enemy->GetActorForwardVector().Y>.6);
    const FVector Locked=Enemy->GetActorForwardVector();Player->SetActorLocation(FVector(0,-140,500));F.Advance(Enemy,.16f);
    TestTrue(TEXT("Crossing fifty percent preserves last legal facing"),Enemy->GetActorForwardVector().Equals(Locked,.001));
    Enemy->ApplyCombatKnockback(FVector(-1,0,0),100.f,.2f);
    TestEqual(TEXT("Ordinary effective displacement enters recovery"),Enemy->AttackPhase,EDWCombatPhase::Recovery);
    TestTrue(TEXT("Recovery retains Attack animation state without a new strike"),Enemy->GetCombatAnimationState()==EDWCombatAnimationState::Attack);
    auto* Elite=F.Enemy(EDWEnemyCombatProfile::DoughElite,FVector(0,400,500));Player->SetActorLocation(FVector(150,400,500));
    if(!TestNotNull(TEXT("Elite"),Elite))return false;
    TestTrue(TEXT("Elite starts windup"),Elite->TryStartMeleeAttack(Player));
    Elite->ApplyCombatKnockback(FVector(-1,0,0),100.f,.2f);
    TestEqual(TEXT("Elite windup is not cancelled by knockback"),Elite->AttackPhase,EDWCombatPhase::Windup);
    const FVector Before=Elite->GetActorLocation();F.Advance(Elite,.2f);
    TestTrue(TEXT("Elite attacking resistance gives 25 cm, swept by actor"),FMath::IsNearlyEqual(FVector::Dist2D(Before,Elite->GetActorLocation()),25.,.01));
    TestEqual(TEXT("No instantaneous ordinary health-based stagger"),Elite->AttackPhase,EDWCombatPhase::Windup);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalSectorTest,"DoughWorld.Combat.FormalEnemy.HurtboxSectorHeightAndOcclusion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalSectorTest::RunTest(const FString&)
{
    auto Hit=[](FVector P,FVector Extent,FRotator R=FRotator::ZeroRotator,double Angle=100.)
    {return DWEnemyFormal::BoxIntersectsSector(FTransform(R,P),Extent,FVector::ZeroVector,FVector::ForwardVector,180.,180.,Angle);};
    TestTrue(TEXT("A large hurtbox corner can enter the sector while its nearest point is outside"),Hit(FVector(90,150,0),FVector(60,20,40)));
    TestFalse(TEXT("Rear hurtbox outside forward sector"),Hit(FVector(-100,0,0),FVector(20)));
    TestTrue(TEXT("Full circle query is used only for approach, not locked strike"),Hit(FVector(-100,0,0),FVector(20),FRotator::ZeroRotator,360.));
    TestFalse(TEXT("Box fully above attack height misses"),Hit(FVector(100,0,151),FVector(20,20,60)));
    TestTrue(TEXT("Exact vertical boundary touches"),Hit(FVector(100,0,150),FVector(20,20,60)));
    TestFalse(TEXT("Diagonal rotated box beyond radius"),Hit(FVector(260,0,0),FVector(30,10,20),FRotator(25,45,10)));
    DWEnemyFormalTests::FWorld F;if(!TestNotNull(TEXT("World"),F.World))return false;
    auto* Enemy=F.Enemy(EDWEnemyCombatProfile::MoldGrunt);auto* Player=F.Player(FVector(140,0,500));
    if(!TestNotNull(TEXT("Enemy"),Enemy)||!TestNotNull(TEXT("Player"),Player))return false;
    TestTrue(TEXT("Unobstructed live explicit box is eligible"),Enemy->CanResolveMeleeHit(Player));
    auto* Wall=F.Spawn<AActor>(FVector(70,0,500));auto* Box=NewObject<UBoxComponent>(Wall);
    Wall->AddInstanceComponent(Box);Wall->SetRootComponent(Box);Box->SetBoxExtent(FVector(5,150,150));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);
    Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Box->RegisterComponent();Wall->SetActorLocation(FVector(70,0,500));
    TestFalse(TEXT("Visibility wall blocks even when hurtbox is in sector"),Enemy->CanResolveMeleeHit(Player));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalAuthorityTest,"DoughWorld.Combat.FormalEnemy.TypedDamageOneHitAndKnockbackFactsBeforeNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalAuthorityTest::RunTest(const FString&)
{
    DWEnemyFormalTests::FWorld F;if(!TestNotNull(TEXT("World"),F.World))return false;
    auto* Enemy=F.Enemy(EDWEnemyCombatProfile::MoldGrunt);auto* Player=F.Player(FVector(140,0,500));
    if(!TestNotNull(TEXT("Enemy"),Enemy)||!TestNotNull(TEXT("Player"),Player)||!TestTrue(TEXT("Player ready"),Player->HasCombatVitalsReady()))return false;
    TestTrue(TEXT("Native formal melee accepts live player"),Enemy->TryStartMeleeAttack(Player));
    const float Before=Player->Health;F.Advance(Enemy,.71f);
    TestEqual(TEXT("Explicit EnemyMelee uses shared authority and normal-form damage"),Player->Health,Before-14.f);
    F.Advance(Enemy,.05f);TestEqual(TEXT("Repeated active tick cannot hit same target twice"),Player->Health,Before-14.f);
    F.Advance(Enemy,1.1f);Player->SetActorLocation(FVector(140,0,500));
    TestTrue(TEXT("Next accepted enemy cycle"),Enemy->TryStartMeleeAttack(Player));
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe(NewObject<UDWCombatAuthorityTestProbe>());
    int32 Notices=0;
    Probe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Notices;TestEqual(TEXT("Health fact precedes delegate"),Enemy->Health,95.f);
        TestEqual(TEXT("Ordinary interrupt already committed before delegate"),Enemy->AttackPhase,EDWCombatPhase::Recovery);
        TestTrue(TEXT("Knockback fact already committed before delegate"),Enemy->bIsCombatKnockedBack);
        TestTrue(TEXT("Victim-only aggro already remembered"),Enemy->HasRememberedPlayerCombat());
        Enemy->CancelMeleeAttack(); // Publication must not recompute resistance/restart recovery after this callback.
    };
    Enemy->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    auto* Authority=UDWCombatAuthoritySubsystem::Get(Player);if(!TestNotNull(TEXT("Authority"),Authority))return false;
    FDWCombatDamageCommand C;C.Source=Player;C.Target=Enemy;C.TargetLife=Authority->CaptureActorLife(Enemy);
    C.Identity={Authority->BeginAction(Player),1,DWCombatCommit::EActionDamageKind::Melee,1};C.Amount=5.f;
    C.KnockbackDirection=FVector(-1,0,0);C.KnockbackDistance=80.f;C.KnockbackSeconds=.2f;
    TestEqual(TEXT("Single authorized player hit"),Authority->SubmitActionDamage(C).AppliedThisCall,5.f);
    TestEqual(TEXT("One notice"),Notices,1);TestEqual(TEXT("Callback cancellation remains intact"),Enemy->AttackPhase,EDWCombatPhase::None);
    TestEqual(TEXT("Duplicate cannot deduct twice"),Authority->SubmitActionDamage(C).AppliedThisCall,0.f);
    TestEqual(TEXT("Duplicate cannot notify twice"),Notices,1);
    Enemy->OnTakeAnyDamage.RemoveDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);Probe->AnyCallback=nullptr;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalMemorySaveTest,"DoughWorld.Combat.FormalEnemy.BirthMemoryRestoreAndLegacyProtection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalMemorySaveTest::RunTest(const FString&)
{
    DWEnemyFormalTests::FWorld F;if(!TestNotNull(TEXT("World"),F.World))return false;
    auto* Enemy=F.Enemy(EDWEnemyCombatProfile::YeastGrunt);if(!TestNotNull(TEXT("Formal enemy"),Enemy))return false;
    const FVector Birth=Enemy->GetActorLocation();auto S=Enemy->CaptureFormalCombatSave();
    S.bEngaged=true;S.bDisengageCandidate=true;S.DisengageElapsedSeconds=2.5;S.AttackCooldownRemainingSeconds=.2;
    Enemy->SetActorLocation(Birth+FVector(900,0,0));FString Error;
    TestTrue(TEXT("Explicit memory/home restore accepted"),Enemy->RestoreFormalCombatSave(S,35.f,Error));
    const auto Restored=Enemy->CaptureFormalCombatSave();
    TestTrue(TEXT("Original birth location is independent of saved/current position"),Restored.BirthLocation.Equals(Birth));
    TestTrue(TEXT("Remembered combat survives load"),Enemy->HasRememberedPlayerCombat());
    TestEqual(TEXT("Partial disengage restored"),Restored.DisengageElapsedSeconds,2.5);TestEqual(TEXT("No home healing"),Enemy->Health,35.f);
    S.BirthLocation.X=std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("Invalid memory save rejected atomically"),Enemy->RestoreFormalCombatSave(S,80.f,Error));TestEqual(TEXT("Invalid restore leaves health"),Enemy->Health,35.f);
    auto* Legacy=F.Spawn<ADWEnemyCharacter>(FVector(1000,1000,500));
    Legacy->AttackDamage=37.f;Legacy->AttackVariants.AddDefaulted();
    TestEqual(TEXT("Existing default stays Legacy"),Legacy->CombatProfile,EDWEnemyCombatProfile::Legacy);
    TestFalse(TEXT("Legacy does not accept new rules implicitly"),Legacy->InitializeFormalCombat(*F.Rules,Error));
    TestEqual(TEXT("Legacy user tuning preserved"),Legacy->AttackDamage,37.f);TestEqual(TEXT("Legacy authored variant preserved"),Legacy->AttackVariants.Num(),1);
    auto* Invalid=F.Spawn<ADWEnemyCharacter>(FVector(1500,1500,500));Invalid->MaxHealth=1000.f;Invalid->SetHealthForLoad(101.f);
    Invalid->CombatProfile=EDWEnemyCombatProfile::YeastGrunt;
    TestFalse(TEXT("Preloaded health above new profile cap rejects before any initialization write"),Invalid->InitializeFormalCombat(*F.Rules,Error));
    TestEqual(TEXT("Rejected initialization preserves health"),Invalid->Health,101.f);TestEqual(TEXT("Rejected initialization preserves old max"),Invalid->MaxHealth,1000.f);
    TestFalse(TEXT("Rejected initialization not marked ready"),Invalid->IsFormalCombatReady());
    TestFalse(TEXT("New formal actor has persistence identity before death"),Enemy->PersistentId.IsNone());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalReturnAndOverlapTest,"DoughWorld.Combat.FormalEnemy.NavigationArrivalAndSweepRestoreReentry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalReturnAndOverlapTest::RunTest(const FString&)
{
    DWEnemyFormalTests::FWorld F;if(!TestNotNull(TEXT("World"),F.World))return false;
    auto* Enemy=F.Enemy(EDWEnemyCombatProfile::MoldGrunt);if(!TestNotNull(TEXT("Enemy"),Enemy))return false;
    const FVector Birth=Enemy->GetActorLocation();auto Save=Enemy->CaptureFormalCombatSave();Save.bReturningHome=true;
    Enemy->NavigationAcceptanceRadius=35.f;const double Arrival=35.+Enemy->GetNavAgentPropertiesRef().AgentRadius;
    Enemy->SetActorLocation(Birth+FVector(Arrival-.1,0,0));
    TestTrue(TEXT("Prepare returning state"),Enemy->RestoreFormalCombatSave(Save,50.f,F.Error));F.Advance(Enemy,.01f);
    TestFalse(TEXT("MoveTo StopOnOverlap arrival includes agent radius"),Enemy->CaptureFormalCombatSave().bReturningHome);
    TestEqual(TEXT("Arrival did not heal"),Enemy->Health,50.f);
    Enemy->SetActorLocation(Birth);Enemy->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    auto* Trigger=F.Spawn<AActor>(Birth+FVector(90,0,0));auto* Box=NewObject<UBoxComponent>(Trigger);
    Trigger->AddInstanceComponent(Box);Trigger->SetRootComponent(Box);Box->SetBoxExtent(FVector(5,150,150));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionObjectType(ECC_WorldDynamic);
    Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    Box->SetGenerateOverlapEvents(true);Box->RegisterComponent();Trigger->SetActorLocation(Birth+FVector(90,0,0));
    TStrongObjectPtr<UDWCombatOverlapTestProbe> Probe(NewObject<UDWCombatOverlapTestProbe>());int32 Restores=0;
    Probe->Callback=[&](AActor*,AActor* Other){if(Other==Enemy){++Restores;Enemy->SetHealthForLoad(37.f);}};
    Trigger->OnActorBeginOverlap.AddDynamic(Probe.Get(),&UDWCombatOverlapTestProbe::Overlap);
    Enemy->ApplyCombatKnockback(FVector::ForwardVector,120.f,.2f);F.Advance(Enemy,.2f);
    TestEqual(TEXT("Real swept overlap executes one restore"),Restores,1);
    TestEqual(TEXT("Restore health remains authoritative after old movement returns"),Enemy->Health,37.f);
    TestFalse(TEXT("Old knockback cannot restart after life restore"),Enemy->bIsCombatKnockedBack);
    TestEqual(TEXT("Old attack update cannot resume after restore"),Enemy->AttackPhase,EDWCombatPhase::None);
    Trigger->OnActorBeginOverlap.RemoveDynamic(Probe.Get(),&UDWCombatOverlapTestProbe::Overlap);Probe->Callback=nullptr;
    return true;
}
#endif
