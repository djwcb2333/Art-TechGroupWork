#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatBow.h"
#include "DWCombatBowProjectile.h"
#include "DWCombatGroundAreaSubsystem.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatTestWorld.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "ProceduralMeshComponent.h"
#include "DWDamageVolumeLibrary.h"
#include "DWSaveGame.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
namespace DWBowTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> InventoryProbe{NewObject<UDWInventoryTransactionTestProbe>()};
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> DamageProbe{NewObject<UDWCombatAuthorityTestProbe>()};
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;UDWWeaponAnimationComponent* Weapon=nullptr;
    UDWCombatGroundAreaSubsystem* Areas=nullptr;const FDWCombatWeaponBinding* Binding=nullptr;FDWBowConfig Config;FString Error;bool Ready=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D17BowWorld"));if(!World)return;
        Player=Spawn<ADWPlayerCharacter>(FVector(0,0,96));if(!Player)return;Weapon=Player->WeaponAnimationComponent;
        Areas=UDWCombatGroundAreaSubsystem::Get(Player);Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::MoldBow);
        Ready=Areas&&Binding&&FDWBowConfig::Build(*GI->GetCombatRules(),Config,Error)&&Restore(8);
        if(Ready)Player->Inventory->OnChanged.AddDynamic(InventoryProbe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
        Box(FVector(0,0,-25),FVector(4000,4000,25));
    }
    ~FFixture()
    {
        InventoryProbe->TestHook=nullptr;DamageProbe->AnyCallback=nullptr;DamageProbe->PointCallback=nullptr;
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn(FVector Location)
    {FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* A=World->SpawnActor<T>(T::StaticClass(),Location,FRotator::ZeroRotator,P);if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;}
    AActor* Box(FVector Location,FVector Extent)
    {auto* A=Spawn<AActor>(Location);auto* B=NewObject<UBoxComponent>(A);A->SetRootComponent(B);A->AddInstanceComponent(B);B->SetBoxExtent(Extent);B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);B->SetCollisionObjectType(ECC_WorldStatic);B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();A->SetActorLocation(Location);return A;}
    ADWEnemyCharacter* Enemy(FVector Location)
    {auto* E=Spawn<ADWEnemyCharacter>(Location);if(!E)return nullptr;E->SetActorTickEnabled(false);E->Health=E->MaxHealth=1000.f;auto* H=NewObject<UDWDamageHitboxComponent>(E);H->SetupAttachment(E->GetRootComponent());H->SetBoxExtent(FVector(35,35,80));E->AddInstanceComponent(H);H->RegisterComponent();return E;}
    bool Restore(int32 Ammo)
    {
        auto* S=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error))return false;
        S->PlayerTransform=FTransform(FRotator::ZeroRotator,FVector(0,0,96));S->bHasPlayerTransform=true;
        S->CombatPlayerResources.OwnedPermanentWeaponIds={Binding->ItemId};S->CombatPlayerResources.CurrentEquipmentId=Binding->ItemId;
        FDWCombatPlayerVitalsConfig V;if(!FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),V,Error))return false;
        S->CombatPlayerResources.Vitals.Water=V.Vitals.WaterMax;S->CombatPlayerResources.Vitals.Satiety=V.Vitals.SatietyMax;
        if(Ammo){FDWItemStack Stack;Stack.ItemId=Binding->AmmoItemId;Stack.Quantity=Ammo;S->Inventory.Add(Stack);}
        return Player->TryApplySaveData(S)&&!GI->HasActiveSlot();
    }
    TArray<ADWCombatBowProjectile*> Arrows()const
    {TArray<ADWCombatBowProjectile*> R;for(TActorIterator<ADWCombatBowProjectile> It(World);It;++It)if(It->IsReleased()&&!It->IsSpent()&&!It->IsActorBeingDestroyed())R.Add(*It);return R;}
    bool Press(){return Weapon->PrimaryPressed(Player->GetActorLocation()+FVector(1000,0,0));}
    void Advance(float Dt,bool MoveArrows=true)
    {World->TimeSeconds+=Dt;Player->TickGameplayClocks(Dt,false);if(MoveArrows)for(auto* A:Arrows())A->Tick(Dt);Areas->Tick(Dt);}
    DWCombatCommit::FEmitterId ReleasedIdentity()
    {
        Advance(static_cast<float>(Config.IntervalSeconds+.1),false);if(!Press())return {};Weapon->PrimaryReleased();Advance(static_cast<float>(Config.WindupSeconds+.001),false);
        DWCombatCommit::FEmitterId Last;for(auto* Arrow:Arrows())if(Arrow->GetEmitter().Id>Last.Id)Last=Arrow->GetEmitter();return Last;
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBowAtomicTest,"DoughWorld.Combat.BowRuntime.SingleTapCostAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBowAtomicTest::RunTest(const FString& Parameters)
{
    using namespace DWBowTests;FFixture F;if(!TestTrue(TEXT("Isolated owned bow, no clip required"),F.Ready))return false;
    int32 Notices=0;F.InventoryProbe->TestHook=[&]()
    {++Notices;TestEqual(TEXT("Arrow already released before real inventory notification"),F.Arrows().Num(),1);const auto R=UDWCombatAuthoritySubsystem::Get(F.Player)->FindReceipt(F.Weapon->GetLastWeaponResourceReceipt());TestTrue(TEXT("Receipt committed"),R&&R->State==DWCombatCommit::EReceiptState::Committed);TestFalse(TEXT("Recursive clock rejected"),F.Weapon->AdvanceAnimation(10.));};
    TestTrue(TEXT("Tap begins"),F.Press());F.Weapon->PrimaryReleased();F.Advance(static_cast<float>(F.Config.WindupSeconds*.5));TestEqual(TEXT("No upfront debit"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),8);
    F.Advance(static_cast<float>(F.Config.WindupSeconds));F.InventoryProbe->TestHook=nullptr;F.Advance(3.f);
    TestEqual(TEXT("Tap fires once and never repeats"),Notices,1);TestEqual(TEXT("One arrow cost"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),8-F.Config.AmmoPerShot);
    TestTrue(TEXT("New press after cooldown"),F.Press());F.Player->CancelCombatActions();F.Advance(2.f);TestEqual(TEXT("Cancelled pre-release free"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),8-F.Config.AmmoPerShot);
    TestTrue(TEXT("Strict empty restore"),F.Restore(0));F.Advance(2.f);TestFalse(TEXT("Empty stock no attack"),F.Press());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBowImpactTest,"DoughWorld.Combat.BowRuntime.DirectDamageNearbyPurePushAndNoDOT",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBowImpactTest::RunTest(const FString& Parameters)
{
    using namespace DWBowTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
    auto* Direct=F.Enemy(FVector(400,0,96));auto* Nearby=F.Enemy(FVector(400,130,96));if(!Direct||!Nearby)return false;int32 DirectNotices=0,NearbyNotices=0;
    Direct->OnTakeAnyDamage.AddDynamic(F.DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);Nearby->OnTakeAnyDamage.AddDynamic(F.DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
    F.DamageProbe->AnyCallback=[&](AActor* Victim,float,AController*,AActor*)
    {
        if(Victim==Direct)++DirectNotices;else ++NearbyNotices;
        TestTrue(TEXT("Nearby push native fact exists before direct damage callback"),Nearby->bIsCombatKnockedBack);
        TestEqual(TEXT("Nearby HP was never changed"),Nearby->Health,1000.f);TestEqual(TEXT("Area adopted before damage listener"),F.Areas->GetLiveAreaCount(),1);
    };
    TestTrue(TEXT("Shoot"),F.Press());F.Weapon->PrimaryReleased();F.Advance(static_cast<float>(F.Config.WindupSeconds+.4));
    TestEqual(TEXT("One direct damage packet"),DirectNotices,1);TestEqual(TEXT("No fake micro damage or notification"),NearbyNotices,0);
    TestEqual(TEXT("Exactly direct snapshot"),Direct->Health,1000.f-static_cast<float>(F.Config.Base.Damage));
    TestEqual(TEXT("Bow slow applies"),Nearby->GetCombatGroundAreaSlowFraction(),F.Config.Base.Area.SlowFraction);
    const float HP=Direct->Health;F.Advance(3.f);TestEqual(TEXT("Bow area produces zero DOT"),Direct->Health,HP);
    Nearby->SetActorLocation(FVector(1200,1200,96));F.Areas->Tick(0.f);TestEqual(TEXT("Departure clears slow with no tail"),Nearby->GetCombatGroundAreaSlowFraction(),0.);
    F.Advance(2.f);TestEqual(TEXT("Area expires independently"),F.Areas->GetLiveAreaCount(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBowWallTest,"DoughWorld.Combat.BowRuntime.FirstWallStopsArrowAndRadialEffect",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBowWallTest::RunTest(const FString& Parameters)
{
    using namespace DWBowTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
    auto* Behind=F.Enemy(FVector(400,0,96));F.Box(FVector(250,0,150),FVector(20,200,150));if(!Behind)return false;
    TestTrue(TEXT("Wall shot"),F.Press());F.Weapon->PrimaryReleased();F.Advance(static_cast<float>(F.Config.WindupSeconds+.4));
    TestEqual(TEXT("Wall prevented direct damage"),Behind->Health,1000.f);TestFalse(TEXT("Wall prevented radial push"),Behind->bIsCombatKnockedBack);
    TestEqual(TEXT("Wall prevented slow"),Behind->GetCombatGroundAreaSlowFraction(),0.);TestEqual(TEXT("Arrow terminated first collision"),F.Arrows().Num(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGroundAreaRuntimeTest,"DoughWorld.Combat.GroundAreasRuntime.StrongestSharedCadencePauseAndLastTick",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGroundAreaRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWBowTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
    // Real paid releases supply IDs. This isolated area fixture does not advance the carrier actors.
    const auto First=F.ReleasedIdentity(),Second=F.ReleasedIdentity(),Bow=F.ReleasedIdentity();if(!TestTrue(TEXT("Three genuine released identities"),First.Id&&Second.Id&&Bow.Id&&First.Id!=Second.Id&&Second.Id!=Bow.Id))return false;
    auto* E=F.Enemy(FVector(500,0,96));auto* Other=F.Enemy(FVector(500,100,96));auto* Pause=F.Spawn<APlayerState>(FVector::ZeroVector);if(!E||!Other||!Pause)return false;
    F.World->TimeSeconds=100.f;auto* A=UDWCombatAuthoritySubsystem::Get(F.Player);const auto Life=A->CaptureActorLife(F.Player);const FVector Center(500,0,2);
    const FDWAlcoholAreaSnapshot Weak{4.,220.,6.,.5,.25},Strong{6.,220.,1.,.5,.30};
    TestTrue(TEXT("First pool adopts"),F.Areas->AdoptReleasedAlcohol(Weak,F.Player,nullptr,Life,First,Center,100.)==EDWAreaAdoption::Created);
    TestTrue(TEXT("Repeated same emitter cannot renew"),F.Areas->AdoptReleasedAlcohol(Weak,F.Player,nullptr,Life,First,Center,100.)==EDWAreaAdoption::Existing);
    F.World->TimeSeconds=100.25f;TestTrue(TEXT("Stronger overlap"),F.Areas->AdoptReleasedAlcohol(Strong,F.Player,nullptr,Life,Second,Center,100.25)==EDWAreaAdoption::Created);
    F.Areas->AdoptReleasedBowSlow({220.,4.,.35},F.Player,nullptr,Life,Bow,Center,100.25);
    TestEqual(TEXT("Kinds take strongest slow, never multiply"),E->GetCombatGroundAreaSlowFraction(),.35);TestEqual(TEXT("No first-contact damage"),E->Health,1000.f);
    int32 Notices=0;E->OnTakeAnyDamage.AddDynamic(F.DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {++Notices;TestEqual(TEXT("Same-instant second victim committed first"),Other->Health,E->Health);F.Areas->Tick(0.f);if(Notices==1)F.World->GetWorldSettings()->SetPauserPlayerState(Pause);};
    F.World->TimeSeconds=102.f;F.Areas->Tick(2.f);TestEqual(TEXT("Callback pause stops further catch-up"),E->Health,994.f);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.Areas->Tick(0.f);TestEqual(TEXT("Shared cadence: two strong, two weak ticks"),E->Health,980.f);
    E->SetActorLocation(FVector(1500,0,96));F.World->TimeSeconds=102.5f;F.Areas->Tick(.5f);TestEqual(TEXT("Outside does not tick"),E->Health,980.f);
    E->SetActorLocation(FVector(500,0,96));F.Areas->Tick(0.f);TestEqual(TEXT("Reentry has no bonus first hit"),E->Health,980.f);
    F.DamageProbe->AnyCallback=nullptr;F.World->TimeSeconds=106.f;F.Areas->Tick(3.5f);
    TestEqual(TEXT("One skipped outside period; final endpoint retained"),E->Health,952.f);TestEqual(TEXT("All independent lifetimes ended"),F.Areas->GetLiveAreaCount(),0);
    TestEqual(TEXT("Slow immediately zero at expiry"),E->GetCombatGroundAreaSlowFraction(),0.);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBowVisibleCarrierTest,"DoughWorld.Combat.BowRuntime.VisibleIndependentCarrierAndRangeEnd",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBowVisibleCarrierTest::RunTest(const FString& Parameters)
{
    using namespace DWBowTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
    TestTrue(TEXT("Owned bow starts without an animation clip"),F.Press());F.Weapon->PrimaryReleased();
    F.Advance(static_cast<float>(F.Config.WindupSeconds+.001),false);const auto Arrows=F.Arrows();
    if(!TestEqual(TEXT("One separate gameplay arrow actor"),Arrows.Num(),1))return false;
    auto* Arrow=Arrows[0];Arrow->Tick(0.f);
    TestFalse(TEXT("Released carrier is visible"),Arrow->IsHidden());
    TestTrue(TEXT("Unscaled scene root preserves gameplay sweep"),Arrow->GetRootComponent()->GetRelativeScale3D().Equals(FVector::OneVector));
    TArray<UStaticMeshComponent*> Meshes;Arrow->GetComponents(Meshes);
    TestEqual(TEXT("Fallback has independent shaft and arrowhead"),Meshes.Num(),2);
    for(const auto* Mesh:Meshes)
    {TestNotNull(TEXT("Fallback uses an existing engine mesh"),Mesh->GetStaticMesh().Get());TestTrue(TEXT("Visual mesh cannot make gameplay hits"),Mesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision);}
    const int32 Ammo=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);
    F.Advance(static_cast<float>(F.Config.Base.RangeCm/F.Config.Base.SpeedCmPerSecond+.1));
    TestTrue(TEXT("Range end destroys the independent carrier"),Arrow->IsActorBeingDestroyed());
    TestEqual(TEXT("Range end does not debit another arrow"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),Ammo);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBowCustomMeshTest,"DoughWorld.Combat.BowRuntime.CustomVisualDoesNotChangeGameplayGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBowCustomMeshTest::RunTest(const FString& Parameters)
{
    using namespace DWBowTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
    auto* Custom=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));if(!TestNotNull(TEXT("Existing custom test mesh"),Custom))return false;
    F.Weapon->BowProjectileMesh=Custom;
    F.Weapon->BowProjectileVisualTransform=FTransform(FRotator(0,90,0),FVector(0,0,20),FVector(2,3,4));
    auto* E=F.Enemy(FVector(400,0,96));if(!E)return false;
    TestTrue(TEXT("Actual paid release"),F.Press());F.Weapon->PrimaryReleased();
    F.Advance(static_cast<float>(F.Config.WindupSeconds+.001),false);const auto Arrows=F.Arrows();if(Arrows.Num()!=1)return false;
    auto* Arrow=Arrows[0];Arrow->Tick(0.f);
    TestEqual(TEXT("Rules retain the actual independent collision radius"),Arrow->GetShotSnapshot().RadiusCm,F.Config.Base.RadiusCm);
    TArray<UStaticMeshComponent*> Meshes;Arrow->GetComponents(Meshes);bool CustomFound=false,HiddenTip=false;
    for(const auto* Mesh:Meshes){CustomFound|=Mesh->GetStaticMesh()==Custom;HiddenTip|=Mesh->GetName()==TEXT("ArrowTip")&&!Mesh->IsVisible();}
    TestTrue(TEXT("Custom slot is applied and composite head hidden"),CustomFound&&HiddenTip);
    TestTrue(TEXT("Visual scale does not scale actor root"),Arrow->GetActorScale3D().Equals(FVector::OneVector));
    F.Advance(.4f);
    TestTrue(TEXT("First hurtbox contact destroys carrier"),Arrow->IsActorBeingDestroyed());
    TestEqual(TEXT("Visual replacement keeps one formal direct hit"),E->Health,1000.f-static_cast<float>(F.Config.Base.Damage));
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBowSeamlessVisualTest,"DoughWorld.Combat.BowRuntime.SourceArrowHandoffAndParameterMapping",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBowSeamlessVisualTest::RunTest(const FString& Parameters)
{
    using namespace DWBowTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
    F.Weapon->BowSourceReleaseSeconds=.55; // Synthetic source anchor differs from formal defaults and proves the mapping.
    auto* Set=NewObject<UDWWeaponAnimationSet>(F.Weapon);Set->Weapon=EDWAnimatedWeapon::MoldBow;Set->VisualScale=1.2f;
    Set->SecondaryGeometry.Vertices={FVector(0,0,0),FVector(0,121,0),FVector(6,100,0)};Set->SecondaryGeometry.Triangles={0,1,2};
    FDWWeaponAnimationClip Clip;Clip.DurationSeconds=1.1f;Clip.HeldStartSeconds=.08f;Clip.HeldEndSeconds=.94f;
    FDWWeaponVisualSample A;A.TimeSeconds=0.f;A.bSecondaryVisible=false;
    FDWWeaponVisualSample B;B.TimeSeconds=static_cast<float>(F.Weapon->BowSourceReleaseSeconds);B.bSecondaryVisible=true;
    B.SecondaryTransform=FTransform(FRotator(0,-90,0),FVector(113,-63,18));
    FDWWeaponVisualSample C=B;C.TimeSeconds=1.1f;C.bSecondaryVisible=false;Clip.VisualSamples={A,B,C};Set->Clips.Add(Clip);F.Weapon->AnimationSets={Set};
    F.Weapon->TickComponent(0.f,LEVELTICK_All,nullptr);TArray<UProceduralMeshComponent*> Held;F.Player->GetComponents(Held);
    for(const auto* M:Held)if(M->GetName()==TEXT("WeaponSecondaryDeformation"))TestFalse(TEXT("No default idle arrow"),M->IsVisible());
    TestTrue(TEXT("Actual public owned attack"),F.Press());F.Weapon->PrimaryReleased();
    const double Half=F.Config.WindupSeconds*.5;F.Advance(static_cast<float>(Half),false);
    TestTrue(TEXT("Source windup follows normalized formal progress"),FMath::IsNearlyEqual(static_cast<double>(F.Weapon->GetElapsedSeconds()),F.Weapon->BowSourceReleaseSeconds*.5,1.e-5));
    FDWWeaponGeometry Expected;FTransform Local;UMaterialInterface* Material=nullptr;
    TestTrue(TEXT("Source arrow can be evaluated without gameplay"),F.Weapon->EvaluateBowReleaseVisual(Expected,Local,Material));
    const FTransform WorldPose=Local*F.Player->GetActorTransform();
    F.Advance(static_cast<float>(F.Config.WindupSeconds-Half+.000001),false);const auto Arrows=F.Arrows();
    if(!TestEqual(TEXT("Exactly one paid independent arrow"),Arrows.Num(),1))return false;auto* Arrow=Arrows[0];
    TestFalse(TEXT("Arrow is published in the release frame"),Arrow->IsHidden());
    TArray<UProceduralMeshComponent*> Visuals;Arrow->GetComponents(Visuals);bool Found=false;
    for(auto* M:Visuals)if(M->GetName()==TEXT("ArrowAuthoredGeometry"))
    {Found=true;TestTrue(TEXT("Authored arrow visible"),M->IsVisible());TestTrue(TEXT("Exact release world pose"),M->GetComponentTransform().Equals(WorldPose,1.e-4));TestTrue(TEXT("Authored vertices copied"),M->GetProcMeshSection(0)&&M->GetProcMeshSection(0)->ProcVertexBuffer.Num()==Expected.Vertices.Num());}
    TestTrue(TEXT("Same source arrow gets its independent component"),Found);F.Weapon->TickComponent(0.f,LEVELTICK_All,nullptr);
    for(const auto* M:Held)if(M->GetName()==TEXT("WeaponSecondaryDeformation"))TestFalse(TEXT("Source arrow hidden after unique commit"),M->IsVisible());
    const int32 Stock=F.Player->Inventory->CountItem(F.Binding->AmmoItemId);F.Weapon->CancelAction();
    TestTrue(TEXT("Cancel keeps paid arrow alive"),!Arrow->IsActorBeingDestroyed()&&Arrow->IsReleased());
    TestEqual(TEXT("Handoff/cancel never spends twice"),F.Player->Inventory->CountItem(F.Binding->AmmoItemId),Stock);return true;
}

#endif
