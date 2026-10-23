#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatBottle.h"
#include "DWCombatBottleProjectile.h"
#include "DWCombatGroundAreaSubsystem.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatTestWorld.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWDamageVolumeLibrary.h"
#include "DWSaveGame.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

namespace DWBottleTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe{NewObject<UDWInventoryTransactionTestProbe>()};
    UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;AActor* Floor=nullptr;FDWBottleConfig C;FString Error;bool Ready=false;
    FFixture(int32 Stock=4,bool WithFloor=true)
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D19Bottle"));if(!World)return;
        P=Spawn<ADWPlayerCharacter>(FVector(0,0,110));if(!P)return;
        if(WithFloor)Floor=Box(FVector(0,0,-50),FVector(5000,5000,50));
        auto* S=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error))return;
        S->bHasPlayerTransform=true;S->PlayerTransform=FTransform(FRotator::ZeroRotator,FVector(0,0,110));
        S->CombatPlayerResources.CurrentEquipmentId=TEXT("Alcohol");
        if(Stock>0){FDWItemStack Stack;Stack.ItemId=TEXT("Alcohol");Stack.Quantity=Stock;S->Inventory.Add(Stack);}
        Ready=P->TryApplySaveData(S)&&FDWBottleConfig::Build(*GI->GetCombatRules(),C,Error)&&
            W()->IsEnabled()&&UDWCombatGroundAreaSubsystem::Get(P)&&!GI->HasActiveSlot();
        if(Ready)P->Inventory->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    }
    ~FFixture()
    {
        Probe->TestHook=nullptr;if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn(FVector At=FVector::ZeroVector)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<T>(T::StaticClass(),At,FRotator::ZeroRotator,Params);
        if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    AActor* Box(FVector At,FVector Extent)
    {
        auto* A=Spawn<AActor>(At);if(!A)return nullptr;auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);A->AddInstanceComponent(B);
        B->SetBoxExtent(Extent);B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);B->SetCollisionObjectType(ECC_WorldStatic);
        B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();A->SetActorLocation(At);return A;
    }
    ADWEnemyCharacter* Enemy(FVector At)
    {
        auto* E=Spawn<ADWEnemyCharacter>(At);if(!E)return nullptr;E->SetActorTickEnabled(false);E->Health=E->MaxHealth=1000.f;
        auto* B=NewObject<UDWDamageHitboxComponent>(E);B->SetupAttachment(E->GetRootComponent());B->SetBoxExtent(FVector(35,35,80));
        E->AddInstanceComponent(B);B->RegisterComponent();return E;
    }
    UDWWeaponAnimationComponent* W()const{return P->WeaponAnimationComponent.Get();}
    UDWCombatGroundAreaSubsystem* Areas()const{return UDWCombatGroundAreaSubsystem::Get(P);}
    int32 Stock()const{return P->Inventory->CountItem(TEXT("Alcohol"));}
    TArray<ADWCombatBottleProjectile*> Bottles()const
    {TArray<ADWCombatBottleProjectile*> Result;for(TActorIterator<ADWCombatBottleProjectile> It(World);It;++It)if(It->IsReleased()&&!It->IsSpent()&&!It->IsActorBeingDestroyed())Result.Add(*It);return Result;}
    bool Press(FVector At=FVector(900,0,0))
    {if(!W()->PrimaryPressed(At))return false;W()->PublishAcceptedWeaponStartEffects();return true;}
    void Advance(double Dt)
    {
        if(!World->IsPaused())World->TimeSeconds+=static_cast<float>(Dt);
        if(IsValid(P)&&!P->IsActorBeingDestroyed())P->TickGameplayClocks(static_cast<float>(Dt),false);
        for(auto* B:Bottles())B->Tick(static_cast<float>(Dt));
        if(auto* A=World->GetSubsystem<UDWCombatGroundAreaSubsystem>())A->Tick(static_cast<float>(Dt));
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBottleMathTest,"DoughWorld.Combat.Bottle.RulesArcAndPhaseSampling",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBottleMathTest::RunTest(const FString&)
{
    DWBottleTests::FFixture F;if(!TestTrue(TEXT("Rules and owned-stock identity ready without animation"),F.Ready))return false;
    const auto Normal=F.C.Shot(EDWSpearForm::Normal),Root=F.C.Shot(EDWSpearForm::MoldRooted),Yeast=F.C.Shot(EDWSpearForm::Yeast);
    TestEqual(TEXT("Root freezes only tick damage multiplier"),Root.TickDamage,F.C.Base.TickDamage*F.C.RootDamageMultiplier);
    TestEqual(TEXT("Attack haste never accelerates DOT"),Root.TickSeconds,Normal.TickSeconds);
    TestEqual(TEXT("Attack haste never accelerates flight"),Root.FlightSeconds,Normal.FlightSeconds);
    TestEqual(TEXT("Yeast outgoing range sampled at release"),Yeast.RangeCm,F.C.YeastRangeCm);
    const FVector Start(10,20,50),End(1010,20,150);
    TestTrue(TEXT("Fixed flight endpoint"),DWBottle::SampleArc(Start,End,.7,200.,.7).Equals(End,1.e-9));
    TestTrue(TEXT("Midflight arch is relative to straight path"),DWBottle::SampleArc(Start,End,.7,200.,.35).Equals(FVector(510,20,300),1.e-9));
    TestTrue(TEXT("Horizontal clamp preserves target ground height"),DWBottle::ClampHorizontalTarget(Start,FVector(2010,20,-40),1000).Equals(FVector(1010,20,-40),1.e-9));
    FDWBottlePlan Plan;if(!Plan.Start(F.C,EDWSpearForm::Yeast))return false;double Used=0.;
    TestTrue(TEXT("Windup advances to precise boundary"),Plan.Advance(F.C.WindupSeconds/F.C.YeastRateMultiplier,Used));
    TestTrue(TEXT("Release is explicit zero boundary"),Plan.EnterNext(EDWSpearForm::Normal));TestEqual(TEXT("No time in release"),Plan.Remaining(),0.);
    TestTrue(TEXT("Recovery starts using current form"),Plan.EnterNext(EDWSpearForm::Normal));TestEqual(TEXT("Recovery not stuck with old haste"),Plan.Remaining(),F.C.RecoverySeconds/F.C.NormalRateMultiplier);
    auto Bad=F.C;Bad.Base.FlightSeconds=0.;TestFalse(TEXT("Invalid fixed flight time rejected"),Bad.IsValid());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBottleAtomicTest,"DoughWorld.Combat.Bottle.CancellationAndPausedReleaseDebitExactlyOnce",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBottleAtomicTest::RunTest(const FString&)
{
    DWBottleTests::FFixture F;if(!F.Ready)return false;const int32 Before=F.Stock();
    TestTrue(TEXT("First input accepted"),F.Press());F.Advance(F.C.WindupSeconds*.5);F.P->CancelCombatActions();F.Advance(F.C.IntervalSeconds);
    TestEqual(TEXT("Cancelled before release pays nothing"),F.Stock(),Before);TestEqual(TEXT("No inert spawn escaped"),F.Bottles().Num(),0);
    auto* Pauser=F.Spawn<APlayerState>();if(!Pauser)return false;int32 Notices=0;
    F.Probe->TestHook=[&]()
    {
        ++Notices;TestEqual(TEXT("Stock adopted before real notification"),F.Stock(),Before-F.C.StockPerThrow);
        TestEqual(TEXT("Native emitter already released before notification"),F.Bottles().Num(),1);
        const auto Receipt=UDWCombatAuthoritySubsystem::Get(F.P)->FindReceipt(F.W()->GetLastWeaponResourceReceipt());
        TestTrue(TEXT("Receipt committed atomically"),Receipt&&Receipt->State==DWCombatCommit::EReceiptState::Committed);
        TestFalse(TEXT("Reentrant animation advance cannot duplicate release"),F.W()->AdvanceAnimation(2.));
        F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);F.W()->PrimaryReleased();
    };
    TestTrue(TEXT("Second input accepted"),F.Press());F.Advance(F.C.WindupSeconds+.001);F.Probe->TestHook=nullptr;
    const auto Live=F.Bottles();if(!TestEqual(TEXT("Released bottle retained during pause"),Live.Num(),1))return false;
    const double Age=Live[0]->GetFlightAgeSeconds();F.Advance(2.);
    TestEqual(TEXT("Pause freezes flight"),Live[0]->GetFlightAgeSeconds(),Age);TestEqual(TEXT("One actual debit notice"),Notices,1);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.Advance(F.C.IntervalSeconds*2.);
    TestEqual(TEXT("Resume/held interval cannot fire another bottle"),F.Stock(),Before-F.C.StockPerThrow);
    TestFalse(TEXT("Blueprint legacy throw is not a second formal entry"),F.P->ThrowAlcoholAt(FVector(500,0,0)));
    TestFalse(TEXT("PlayRole cannot bypass release transaction"),F.W()->PlayRole(TEXT("Attack"),FVector(500,0,0)));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBottleStockGroundTest,"DoughWorld.Combat.Bottle.NoStockOrLegalGroundNeverReleases",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBottleStockGroundTest::RunTest(const FString&)
{
    {DWBottleTests::FFixture F(0);if(!F.Ready)return false;TestFalse(TEXT("No stock means no attack"),F.Press());TestEqual(TEXT("No free bottle"),F.Bottles().Num(),0);}
    {DWBottleTests::FFixture F(3,false);if(!F.Ready)return false;TestTrue(TEXT("Windup may target missing terrain"),F.Press());F.Advance(F.C.WindupSeconds+.01);
     TestEqual(TEXT("Illegal landing candidate fails before debit"),F.Stock(),3);TestEqual(TEXT("Staged bottle removed"),F.Bottles().Num(),0);TestEqual(TEXT("No floating legacy pool"),F.Areas()->GetLiveAreaCount(),0);}
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBottleWallTest,"DoughWorld.Combat.Bottle.FirstWallEndsFlightWithoutDirectDamageOrRefund",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBottleWallTest::RunTest(const FString&)
{
    DWBottleTests::FFixture F;if(!F.Ready)return false;auto* Target=F.Enemy(FVector(900,0,100));if(!Target)return false;
    F.Box(FVector(250,0,300),FVector(20,200,300));const int32 Before=F.Stock();
    TestTrue(TEXT("Throw accepted"),F.Press());F.Advance(F.C.WindupSeconds+.001);F.Advance(F.C.Base.FlightSeconds+.01);
    TestEqual(TEXT("First solid wall ends bottle"),F.Bottles().Num(),0);TestEqual(TEXT("Wall break keeps paid cost"),F.Stock(),Before-F.C.StockPerThrow);
    TestEqual(TEXT("Only one area at wall-side legal ground"),F.Areas()->GetLiveAreaCount(),1);
    TestEqual(TEXT("No direct/explosion damage behind wall"),Target->Health,1000.f);F.Advance(F.C.Base.AreaSeconds+.01);
    TestEqual(TEXT("Distant requested point never receives a relocated pool"),Target->Health,1000.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBottleAreaTest,"DoughWorld.Combat.Bottle.SharedAreaTwelveTicksAndNoImpactDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBottleAreaTest::RunTest(const FString&)
{
    DWBottleTests::FFixture F;if(!F.Ready)return false;auto* E=F.Enemy(FVector(900,0,80));if(!E)return false;
    if(!F.Press())return false;F.Advance(F.C.WindupSeconds+.001);F.Advance(F.C.Base.FlightSeconds+.001);
    TestEqual(TEXT("Physical body receives zero immediate impact damage"),E->Health,1000.f);
    TestEqual(TEXT("Impact adopted one independent region"),F.Areas()->GetLiveAreaCount(),1);
    TestTrue(TEXT("Switch after release remains legal"),F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed));
    const int32 Ticks=FMath::FloorToInt(F.C.Base.AreaSeconds/F.C.Base.TickSeconds);
    for(int32 I=0;I<Ticks;++I)F.Advance(F.C.Base.TickSeconds);
    TestTrue(TEXT("Full lifetime includes terminal tick but no extra first jump"),FMath::IsNearlyEqual(E->Health,1000.f-static_cast<float>(Ticks*F.C.Shot(EDWSpearForm::Normal).TickDamage),.001f));
    F.Advance(F.C.Base.TickSeconds);TestEqual(TEXT("Expired area does not refresh or follow target"),F.Areas()->GetLiveAreaCount(),0);return true;
}
#endif
