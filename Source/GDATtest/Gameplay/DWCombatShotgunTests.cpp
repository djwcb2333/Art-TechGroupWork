#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatShotgun.h"
#include "DWCombatTestWorld.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatRules.h"
#include "DWSaveGame.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWWeaponResourceTestProbe.h"
#include "DWDamageVolumeLibrary.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"

namespace DWShotgunTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;FDWShotgunConfig Config;FString Error;
    FFixture(){World=DWCombatTestWorld::Create(GI,TEXT("ShotgunIndependent"));if(World){P=Spawn<ADWPlayerCharacter>(FVector(0,0,500));FDWShotgunConfig::Build(*GI->GetCombatRules(),Config,Error);}}
    ~FFixture(){if(!World)return;for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
    template<class T>T* Spawn(FVector At){FActorSpawnParameters O;O.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;auto* A=World->SpawnActor<T>(T::StaticClass(),At,FRotator::ZeroRotator,O);if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;}
    UDWWeaponAnimationComponent* W(){return P->WeaponAnimationComponent.Get();}
    bool Equip(int32 Shells){if(!P||!Config.IsValid())return false;FDWCombatWeaponResourceSnapshot S;S.CurrentEquipmentId=TEXT("DWWeapon_Shotgun");S.OwnedPermanentWeaponIds.Add(S.CurrentEquipmentId);S.LoadedShotgunShells=Shells;return W()->RestoreCombatWeaponState(S,*GI->GetCombatRules(),Error);}
    ADWEnemyCharacter* Target(double X,double HalfWidth)
    {
        auto* E=Spawn<ADWEnemyCharacter>(FVector(X,0,500));E->SetActorTickEnabled(false);
        auto* Box=NewObject<UDWDamageHitboxComponent>(E);E->AddInstanceComponent(Box);Box->SetupAttachment(E->GetRootComponent());Box->SetBoxExtent(FVector(1,HalfWidth,100));Box->RegisterComponent();return E;
    }
    void Advance(double T){World->TimeSeconds+=static_cast<float>(T);W()->AdvanceAnimation(T);}
    bool Fire(){const bool R=W()->PrimaryPressed(P->GetActorLocation()+FVector(1000,0,0));if(R)W()->PublishAcceptedWeaponStartEffects();return R;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShotgunRays,"DoughWorld.Combat.Shotgun.IndependentRaysDistanceAndWorldBlocking",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShotgunRays::RunTest(const FString&)
{
    DWShotgunTests::FFixture F;if(!TestTrue(TEXT("Rules and explicit equipment ready"),F.Equip(2)))return false;
    const auto& C=F.Config;TestEqual(TEXT("Formal pellet count"),C.Pellets,8);
    TestTrue(TEXT("35 full degrees has symmetric endpoints"),FMath::IsNearlyEqual(C.Angle(0),-17.5)&&FMath::IsNearlyEqual(C.Angle(7),17.5));
    TestTrue(TEXT("Full damage within three meters"),FMath::IsNearlyEqual(C.DamageAt(300,900)*8.,40.));
    TestTrue(TEXT("Final distance damage and no beyond-range hit"),FMath::IsNearlyEqual(C.DamageAt(900,900)*8.,10.)&&C.DamageAt(901,900)==0.);
    auto* E=F.Target(200,200);TArray<FDWShotgunTarget> Hits;FDWShotgunQuery::Collect(F.World,F.P,F.P->GetActorLocation(),FVector::ForwardVector,C,EDWSpearForm::Normal,Hits);
    if(!TestEqual(TEXT("Eight rays aggregate one target"),Hits.Num(),1))return false;
    TestEqual(TEXT("Eight actual ray intersections"),Hits[0].Pellets,8);TestTrue(TEXT("One strongest knockback, not eight"),FMath::IsNearlyEqual(Hits[0].Knockback,150.));
    auto* Wall=F.Spawn<AActor>(FVector(100,0,500));auto* B=NewObject<UBoxComponent>(Wall);Wall->AddInstanceComponent(B);Wall->SetRootComponent(B);B->SetBoxExtent(FVector(5,500,500));B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);B->SetCollisionResponseToAllChannels(ECR_Block);B->RegisterComponent();Wall->SetActorLocation(FVector(100,0,500));
    FDWShotgunQuery::Collect(F.World,F.P,F.P->GetActorLocation(),FVector::ForwardVector,C,EDWSpearForm::Normal,Hits);TestEqual(TEXT("Wall prevents every ray reaching target"),Hits.Num(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShotgunCommit,"DoughWorld.Combat.Shotgun.WindupAmmoAndAllDamagePrecedeReentrantNotification",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShotgunCommit::RunTest(const FString&)
{
    DWShotgunTests::FFixture F;if(!F.Equip(2))return false;auto* E=F.Target(200,200);int32 Notices=0;
    TStrongObjectPtr<UDWWeaponResourceTestProbe> Probe(NewObject<UDWWeaponResourceTestProbe>());
    Probe->TestHook=[&](int32 Loaded,int64,bool Reload){if(Reload)return;++Notices;TestEqual(TEXT("Ammo already spent at callback"),Loaded,1);TestTrue(TEXT("All eight pellets already committed at callback"),FMath::IsNearlyEqual(E->Health,60.f));F.W()->CancelAction();};
    F.W()->OnWeaponResourceChanged.AddDynamic(Probe.Get(),&UDWWeaponResourceTestProbe::OnResources);
    TestTrue(TEXT("Shot starts with no animation assets"),F.Fire());F.Advance(.099);
    TestEqual(TEXT("Windup costs no ammunition"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,2);TestEqual(TEXT("No premature damage"),E->Health,100.f);
    F.Advance(.0011);TestEqual(TEXT("One notification"),Notices,1);F.Advance(2.);TestEqual(TEXT("Cancelled callback cannot double-fire"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
    TestEqual(TEXT("No reserve inventory consumption"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),0);
    F.W()->OnWeaponResourceChanged.RemoveDynamic(Probe.Get(),&UDWWeaponResourceTestProbe::OnResources);Probe->TestHook=nullptr;return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShotgunReload,"DoughWorld.Combat.Shotgun.ReloadCompletionInterruptionAndEmptyAutoReload",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShotgunReload::RunTest(const FString&)
{
    DWShotgunTests::FFixture F;if(!F.Equip(1))return false;
    TestTrue(TEXT("Explicit fixture reserve"),F.P->GetInventory()->TryAddItem(TEXT("DWAmmo_BreadShell"),3));
    TestTrue(TEXT("Reload without an animation clip"),F.W()->RequestReload());F.Advance(.3);
    TestEqual(TEXT("Incomplete shell does not consume reserve"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),3);
    TestTrue(TEXT("Loaded shot interrupts unfinished shell"),F.Fire());F.Advance(.1);F.Advance(.45);
    TestEqual(TEXT("Shot only spent its loaded shell"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,0);
    TestTrue(TEXT("Empty automatically begins reload"),F.W()->IsBusy());F.Advance(.549);
    TestEqual(TEXT("Reload uses fixed duration"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,0);F.Advance(.0011);
    TestEqual(TEXT("Completion transfers one shell"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);TestEqual(TEXT("One reserve transferred"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),2);
    F.W()->CancelAction();TestEqual(TEXT("Completed shell remains after cancellation"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShotgunSharedClockPause,"DoughWorld.Combat.Shotgun.SharedPlayerClockPauseCannotRepayShot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShotgunSharedClockPause::RunTest(const FString&)
{
    DWShotgunTests::FFixture F;if(!F.Equip(2))return false;
    auto* Pause=F.Spawn<APlayerState>(FVector::ZeroVector);if(!Pause)return false;
    TStrongObjectPtr<UDWWeaponResourceTestProbe> Probe(NewObject<UDWWeaponResourceTestProbe>());int32 Notices=0;
    Probe->TestHook=[&](int32,int64,bool Reload){if(!Reload){++Notices;F.World->GetWorldSettings()->SetPauserPlayerState(Pause);}};
    F.W()->OnWeaponResourceChanged.AddDynamic(Probe.Get(),&UDWWeaponResourceTestProbe::OnResources);
    TestTrue(TEXT("Accept no-art formal shot"),F.Fire());F.World->TimeSeconds+=.8f;F.P->TickGameplayClocks(.8f,false);
    TestEqual(TEXT("One release before notification pause"),Notices,1);
    TestEqual(TEXT("Exactly one loaded shell committed"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
    TestTrue(TEXT("Pause interrupts the original long frame"),F.W()->IsBusy());
    F.P->TickGameplayClocks(10.f,false);TestEqual(TEXT("Paused direct tick cannot repay"),Notices,1);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.P->TickGameplayClocks(0.f,false);
    TestEqual(TEXT("Zero delta resume cannot replay receipt"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,1);
    F.World->TimeSeconds+=.451f;F.P->TickGameplayClocks(.451f,false);
    TestFalse(TEXT("New recovery delta completes action"),F.W()->IsBusy());TestEqual(TEXT("One total release notification"),Notices,1);
    F.W()->OnWeaponResourceChanged.RemoveDynamic(Probe.Get(),&UDWWeaponResourceTestProbe::OnResources);Probe->TestHook=nullptr;return true;
}
namespace DWShotgunFeedbackTests
{
bool SetForm(DWShotgunTests::FFixture& F,EDWCombatBaseForm Form)
{
    auto* Save=NewObject<UDWSaveGame>();FDWCombatPlayerVitalsConfig V;
    if(!FDWCombatPlayerVitalsConfig::Build(*F.GI->GetCombatRules(),V,F.Error)||
        !DWCombatPlayerSave::MakeNew(*F.GI->GetCombatRules(),*Save,F.Error))return false;
    auto& S=Save->CombatPlayerResources.Vitals;S.BaseForm=Form;
    S.FormRemainingSeconds=Form==EDWCombatBaseForm::Normal?0.:V.FormDurationSeconds;
    S.Special=Form==EDWCombatBaseForm::Normal?V.Vitals.NormalSpecialValue:V.Vitals.SpecialMax;
    S.Water=V.Vitals.WaterMax;S.Satiety=V.Vitals.SatietyMax;
    Save->bYeastForm=Form==EDWCombatBaseForm::Yeast;Save->Transformation=static_cast<float>(S.Special);
    if(!DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*Save,F.Error))return false;
    if(!F.P->TryApplySaveData(Save))
    {F.Error=TEXT("Validated form fixture was rejected by TryApplySaveData runtime guards.");return false;}
    return true;
}
double ReloadSeconds(DWShotgunTests::FFixture& F)
{double Value=0.;F.GI->GetCombatRules()->TryGetNumber(TEXT("weapon.shotgun.reload_one_s"),EDWCombatRuleUnit::Seconds,Value,F.Error);return Value;}
void AddRootFloor(DWShotgunTests::FFixture& F)
{
    auto* Floor=F.Spawn<AActor>(FVector(0,0,-50));auto* Box=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(10000,10000,50));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionObjectType(ECC_WorldStatic);
    Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Box->SetWorldLocation(FVector(0,0,-50));
    F.P->SetActorLocation(FVector(0,0,F.P->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.));
    F.P->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShotgunMobileReloadTest,"DoughWorld.Combat.Shotgun.ReloadKeepsMovementAndUsesFinalFormSpeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShotgunMobileReloadTest::RunTest(const FString&)
{
    using namespace DWShotgunFeedbackTests;
    for(const auto Form:{EDWCombatBaseForm::Normal,EDWCombatBaseForm::Yeast})
    {
        DWShotgunTests::FFixture F;
        if(!TestNotNull(TEXT("Mobile reload player fixture"),F.P))return false;
        if(!SetForm(F,Form))
        {AddError(FString::Printf(TEXT("Mobile reload form fixture %d: %s"),static_cast<int32>(Form),*F.Error));return false;}
        if(!F.Equip(1))
        {AddError(FString::Printf(TEXT("Mobile reload equipment fixture: %s"),*F.Error));return false;}
        if(!TestTrue(TEXT("Mobile reload reserve fixture"),F.P->GetInventory()->TryAddItem(TEXT("DWAmmo_BreadShell"),3)))return false;
        // A visual asset requesting movement locking must not override the formal mobile reload rule.
        auto* Set=NewObject<UDWWeaponAnimationSet>(F.P);Set->Weapon=EDWAnimatedWeapon::Shotgun;
        Set->bLockMovementDuringAction=true;F.W()->AnimationSets={Set};
        auto* Movement=F.P->GetCharacterMovement();const float NormalSpeed=Movement->MaxWalkSpeed;
        Movement->bOrientRotationToMovement=true;Movement->Velocity=FVector(123,17,0);
        const FVector BeforeVelocity=Movement->Velocity;F.P->SetActorRotation(FRotator(0,37,0));
        const FRotator BeforeRotation=F.P->GetActorRotation();
        F.P->MoveCameraRelative(1,0);const FVector Pending=F.P->GetPendingMovementInputVector();
        if(!TestTrue(TEXT("Reload accepts while moving"),F.W()->RequestReload()))return false;
        TestTrue(TEXT("Reload remains active"),F.W()->IsFormalShotgunReloading());
        TestFalse(TEXT("Authored visual movement flag cannot freeze reload"),F.W()->LocksMovement());
        TestEqual(TEXT("Reload does not stop current velocity"),Movement->Velocity,BeforeVelocity);
        TestEqual(TEXT("Reload does not consume pending movement"),F.P->GetPendingMovementInputVector(),Pending);
        TestTrue(TEXT("Reload does not snap facing"),F.P->GetActorRotation().Equals(BeforeRotation,.001));
        TestTrue(TEXT("Movement keeps its facing policy"),Movement->bOrientRotationToMovement);
        TestTrue(TEXT("Multiplier applies after current form movement"),FMath::IsNearlyEqual(
            static_cast<double>(Movement->MaxWalkSpeed),NormalSpeed*F.Config.ReloadMoveMultiplier,.001));
        F.P->ConsumeMovementInputVector();F.P->MoveCameraRelative(0,1);
        TestFalse(TEXT("WASD remains accepted without cancelling reload"),F.P->GetPendingMovementInputVector().IsNearlyZero());
        TestTrue(TEXT("Moving does not cancel shell clock"),F.W()->IsFormalShotgunReloading());
        F.W()->CancelAction();TestEqual(TEXT("Cancellation restores final normal speed immediately"),Movement->MaxWalkSpeed,NormalSpeed);
        TestEqual(TEXT("Unfinished reload did not spend reserve"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),3);
        auto Invalid=F.Config;Invalid.ReloadMoveMultiplier=0.;TestFalse(TEXT("Zero multiplier cannot restore immobile reload"),Invalid.IsValid());
        Invalid=F.Config;Invalid.ReloadMoveMultiplier=1.01;TestFalse(TEXT("Out-of-range speed multiplier rejected"),Invalid.IsValid());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShotgunEmptyFireCancelTest,"DoughWorld.Combat.Shotgun.EmptyFireStopsReloadWithoutAutoRestart",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShotgunEmptyFireCancelTest::RunTest(const FString&)
{
    DWShotgunTests::FFixture F;if(!F.Equip(0)||!F.P->GetInventory()->TryAddItem(TEXT("DWAmmo_BreadShell"),3))return false;
    const double Duration=DWShotgunFeedbackTests::ReloadSeconds(F);if(Duration<=0.)return false;
    const float NormalSpeed=F.P->GetCharacterMovement()->MaxWalkSpeed;
    if(!TestTrue(TEXT("Begin empty reload"),F.W()->RequestReload()))return false;
    F.Advance(Duration*.5);
    TestFalse(TEXT("Empty press cannot fire"),F.Fire());
    TestFalse(TEXT("That press stops the unfinished reload"),F.W()->IsBusy());
    TestEqual(TEXT("Cancelled shell leaves reserve untouched"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),3);
    TestEqual(TEXT("No shell materialized before completion"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,0);
    TestEqual(TEXT("Cancelled empty reload restores movement"),F.P->GetCharacterMovement()->MaxWalkSpeed,NormalSpeed);
    F.Advance(Duration*3.);
    TestFalse(TEXT("Old held input does not restart reload"),F.W()->IsBusy());
    TestEqual(TEXT("Old time cannot complete the cancelled shell"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),3);
    TestTrue(TEXT("New explicit reload remains available"),F.W()->RequestReload());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWShotgunAcceptedActionsCancelTest,"DoughWorld.Combat.Shotgun.AcceptedSwitchDashInjectionAndRootKeepCompletedShell",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWShotgunAcceptedActionsCancelTest::RunTest(const FString&)
{
    using namespace DWShotgunFeedbackTests;
    for(int32 Case=0;Case<4;++Case)
    {
        DWShotgunTests::FFixture F;
        const auto Form=Case==1?EDWCombatBaseForm::Yeast:Case==3?EDWCombatBaseForm::Mold:EDWCombatBaseForm::Normal;
        if(!TestNotNull(TEXT("Reload cancellation player fixture"),F.P))return false;
        if(!SetForm(F,Form))
        {AddError(FString::Printf(TEXT("Reload cancellation case %d form fixture %d: %s"),Case,static_cast<int32>(Form),*F.Error));return false;}
        if(!F.Equip(1))
        {AddError(FString::Printf(TEXT("Reload cancellation case %d equipment fixture: %s"),Case,*F.Error));return false;}
        if(!TestTrue(TEXT("Reload cancellation reserve fixture"),F.P->GetInventory()->TryAddItem(TEXT("DWAmmo_BreadShell"),3)))return false;
        if(Case==3)AddRootFloor(F);
        int32 CoreSlot=INDEX_NONE;
        if(Case==2)
        {
            int32 Cost=0;if(!F.GI->GetCombatRules()->TryGetCount(TEXT("form.core_cost"),Cost,F.Error)||
                !F.P->GetInventory()->TryAddItem(FDWCombatItemRegistry::YeastCoreId(),Cost))return false;
            const auto& Slots=F.P->GetInventory()->GetSlots();
            for(int32 I=0;I<Slots.Num();++I)if(Slots[I].ItemId==FDWCombatItemRegistry::YeastCoreId()){CoreSlot=I;break;}
        }
        const double Duration=ReloadSeconds(F);if(Duration<=0.||!F.W()->RequestReload())return false;
        F.Advance(Duration);F.Advance(Duration*.5);
        if(!TestEqual(TEXT("Exactly one completed shell before interrupt"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,2))return false;
        TestEqual(TEXT("Only completed shell removed from reserve"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),2);
        bool Accepted=false;
        if(Case==0)Accepted=F.W()->SelectWeapon(EDWAnimatedWeapon::Unarmed);
        else if(Case==1)
        {
            FDWCombatInputPress Press;Press.Edge.Context={1,1,1};Press.Edge.Sequence=1;
            Press.Edge.Key=TEXT("SpaceBar");Press.Edge.bDown=true;Press.Action=TEXT("Dash");
            Accepted=F.P->TryStartFormalDash(Press,FVector::ForwardVector,FVector::ForwardVector);
        }
        else if(Case==2)Accepted=CoreSlot!=INDEX_NONE&&F.P->UseInventoryItem(CoreSlot);
        else Accepted=F.P->TryToggleFormalRoot();
        if(!TestTrue(*FString::Printf(TEXT("Qualified replacement action %d accepted"),Case),Accepted))return false;
        TestFalse(TEXT("Replacement ended reload"),F.W()->IsFormalShotgunReloading());
        TestEqual(TEXT("Completed shell retained"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,2);
        TestEqual(TEXT("Unfinished next shell costs nothing"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),2);
        F.Advance(Duration*2.);
        TestEqual(TEXT("Cancelled reload cannot pay a late shell"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,2);
        TestEqual(TEXT("No delayed reserve debit"),F.P->GetInventory()->CountItem(TEXT("DWAmmo_BreadShell")),2);
    }
    return true;
}

#endif
