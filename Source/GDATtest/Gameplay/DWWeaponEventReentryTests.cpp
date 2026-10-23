#include "DWWeaponEventReentryTestProbe.h"
void UDWWeaponEventReentryTestProbe::HandleDamage(AActor*,float,const UDamageType*,AController*,AActor*)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(DamageCallback)DamageCallback();
#endif
}
void UDWWeaponEventReentryTestProbe::HandleAnimation(EDWAnimatedWeapon Weapon,FName Role,int32 EventIndex)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(AnimationCallback)AnimationCallback(Weapon,Role,EventIndex);
#endif
}
#if WITH_DEV_AUTOMATION_TESTS
#include "DWWeaponAnimationComponent.h"
#include "DWCombatDagger.h"
#include "Components/CapsuleComponent.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWDamageVolumeLibrary.h"
#include "DWCombatRules.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "DWCombatTestWorld.h"
#include "UObject/StrongObjectPtr.h"
namespace DWWeaponReentryTests
{
    struct FScope
    {
        TStrongObjectPtr<UDWGameInstance> CombatTestGI;
        UWorld* World=nullptr;
        ADWPlayerCharacter* Player=nullptr;
        TStrongObjectPtr<UDWWeaponEventReentryTestProbe> Probe{NewObject<UDWWeaponEventReentryTestProbe>()};
        FScope()
        {
            World=DWCombatTestWorld::Create(CombatTestGI,TEXT("DWWeaponEventReentryTests"));
            if(!World)return;
            Player=Spawn<ADWPlayerCharacter>(FVector(0,0,500));Player->bCombatRequireLineOfSight=false;
            Player->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        }
        ~FScope()
        {
            Probe->DamageCallback=nullptr;Probe->AnimationCallback=nullptr;
            for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            CombatTestGI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
        }
        template<class T> T* Spawn(FVector Position)
        {
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            T* Actor=World->SpawnActor<T>(T::StaticClass(),Position,FRotator::ZeroRotator,Params);
            if(Actor&&!Actor->HasActorBegunPlay())Actor->DispatchBeginPlay();
            return Actor;
        }
        UDWWeaponAnimationComponent* C()const{return Player->WeaponAnimationComponent;}
        bool PrepareResources(FName Current)
        {
            TSharedPtr<const FDWCombatRules> Rules;FString Error;
            if(!FDWCombatRules::LoadProjectPayload(Rules,Error))return false;
            // Explicit isolated ownership and loaded-shell snapshot; never rely on BeginPlay freefill.
            FDWCombatWeaponResourceSnapshot Snapshot;
            Snapshot.OwnedPermanentWeaponIds={TEXT("DWWeapon_Shotgun"),TEXT("DWWeapon_Spear"),TEXT("DWWeapon_DualDaggers")};
            Snapshot.LoadedShotgunShells=3;Snapshot.CurrentEquipmentId=Current;
            return C()->RestoreCombatWeaponState(Snapshot,*Rules,Error);
        }
        static void AddClip(UDWWeaponAnimationSet* Set,FName Role,bool bEvent)
        {
            FDWWeaponAnimationClip Clip;Clip.Role=Role;Clip.DurationSeconds=1.f;Clip.Animation=NewObject<UAnimSequence>(Set);
            if(bEvent){FDWWeaponDamageEvent Event;Event.TimeSeconds=.1f;Clip.DamageEvents.Add(Event);}
            Set->Clips.Add(Clip);
        }
        UDWWeaponAnimationSet* MakeSet(EDWAnimatedWeapon Weapon,bool bDamage=true)
        {
            auto* Set=NewObject<UDWWeaponAnimationSet>(Player);Set->Weapon=Weapon;
            Set->Damage=10.f;Set->DamageRange=500.f;Set->ArcDegrees=90.f;Set->KnockbackDistance=0.f;
            Set->bApplyBasicDamage=bDamage;Set->MagazineCapacity=Weapon==EDWAnimatedWeapon::Shotgun?3:0;
            AddClip(Set,TEXT("Attack"),true);C()->AnimationSets.Add(Set);return Set;
        }
        ADWEnemyCharacter* Enemy(float X)
        {
            auto* Enemy=Spawn<ADWEnemyCharacter>(FVector(X,0,500));Enemy->Health=100.f;
            Enemy->SetActorRotation(FRotator(0,180,0));Enemy->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
            auto* Box=NewObject<UDWDamageHitboxComponent>(Enemy);Enemy->AddInstanceComponent(Box);
            Box->SetupAttachment(Enemy->GetRootComponent());Box->SetBoxExtent(FVector(10.f));Box->RegisterComponent();
            Box->SetWorldLocation(Enemy->GetActorLocation());Box->SetWorldScale3D(FVector::OneVector);return Enemy;
        }
        FVector Aim()const{return Player->GetActorLocation()+FVector(500,0,0);}
        void Advance(float Dt){World->TimeSeconds+=Dt;C()->AdvanceAnimation(Dt);}
    };
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponDamageReentryTest,"DoughWorld.Combat.WeaponAnimation.DamageReentry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponDamageReentryTest::RunTest(const FString& Parameters)
{
    for(int32 Mode=0;Mode<3;++Mode)
    {
        DWWeaponReentryTests::FScope S;auto* C=S.C();S.MakeSet(EDWAnimatedWeapon::DualDaggers);
        auto* Replacement=S.MakeSet(EDWAnimatedWeapon::Spear,false);
        DWWeaponReentryTests::FScope::AddClip(Replacement,TEXT("Replacement"),false);
        if(!TestTrue(TEXT("Explicit current dagger plus owned replacement and stored shells"),S.PrepareResources(TEXT("DWWeapon_DualDaggers"))))return false;
        FDWDaggerTuning T;FString Error;if(!FDWDaggerTuning::Build(*S.CombatTestGI->GetCombatRules(),T,Error))return false;
        {FDWSpearTuning First;if(!T.MakeSegmentTuning(0,First))return false;T.Attack=First;}
        auto* First=S.Enemy(static_cast<float>(T.Attack.RangeCm*.4));auto* Second=S.Enemy(static_cast<float>(T.Attack.RangeCm*.8));
        int32 DamageCallbacks=0,Notifications=0;bool bSelected=false,bImmediateReplacement=false;
        S.Probe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32){++Notifications;};
        S.Probe->DamageCallback=[&]()
        {
            ++DamageCallbacks;if(Mode==2){S.Player->Destroy();return;}
            C->CancelAction();if(Mode==1){bSelected=C->SelectWeapon(EDWAnimatedWeapon::Spear);bImmediateReplacement=C->PlayRole(TEXT("Replacement"),S.Aim());}
        };
        First->OnTakeAnyDamage.AddDynamic(S.Probe.Get(),&UDWWeaponEventReentryTestProbe::HandleDamage);
        C->OnAnimationGameplayEvent.AddDynamic(S.Probe.Get(),&UDWWeaponEventReentryTestProbe::HandleAnimation);
        TestTrue(TEXT("Start formal dagger attack"),C->PrimaryPressed(S.Aim()));C->PublishAcceptedWeaponStartEffects();
        S.Advance(static_cast<float>(T.Attack.WindupSeconds+.001));
        TestEqual(FString::Printf(TEXT("Mode %d real callback ran once"),Mode),DamageCallbacks,1);
        TestEqual(FString::Printf(TEXT("Mode %d accepted formal hit remains"),Mode),First->Health,100.f-static_cast<float>(T.Attack.Damage));
        TestEqual(FString::Printf(TEXT("Mode %d no stale second target"),Mode),Second->Health,100.f);
        TestEqual(FString::Printf(TEXT("Mode %d source marker cannot duplicate gameplay"),Mode),Notifications,0);
        if(Mode==0){TestFalse(TEXT("Cancelled action stays stopped"),C->IsBusy());TestEqual(TEXT("Melee does not consume unrelated stored shells"),C->GetCombatWeaponStateSnapshot().LoadedShotgunShells,3);}
        if(Mode==1)
        {
            TestTrue(TEXT("Owned replacement selected in callback"),bSelected);TestFalse(TEXT("A04 switch lock forbids immediate replacement start"),bImmediateReplacement);
            TestFalse(TEXT("Old carried delta cannot restart an action"),C->IsBusy());
            S.Advance(static_cast<float>(C->GetRemainingAttackLockSeconds()+.01));
            TestTrue(TEXT("Replacement can start after retained attack and switch locks"),C->PlayRole(TEXT("Replacement"),S.Aim()));
            TestEqual(TEXT("Replacement role survives"),C->GetActiveRole(),FName(TEXT("Replacement")));TestEqual(TEXT("Replacement starts at its own zero"),C->GetElapsedSeconds(),0.f);
        }
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponBroadcastReentryTest,"DoughWorld.Combat.WeaponAnimation.BroadcastReentry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponBroadcastReentryTest::RunTest(const FString& Parameters)
{
    DWWeaponReentryTests::FScope S;auto* C=S.C();auto* Set=S.MakeSet(EDWAnimatedWeapon::Spear,false);
    FDWWeaponDamageEvent Later;Later.TimeSeconds=.2f;Set->Clips[0].DamageEvents.Add(Later);
    DWWeaponReentryTests::FScope::AddClip(Set,TEXT("Replacement"),false);
    if(!TestTrue(TEXT("Explicit owned/current non-damaging source fixture"),S.PrepareResources(TEXT("DWWeapon_Spear"))))return false;
    int32 Notifications=0;bool bOriginalIdentity=false,bReplacementStarted=false;
    S.Probe->AnimationCallback=[&](EDWAnimatedWeapon Weapon,FName Role,int32 Index)
    {
        ++Notifications;bOriginalIdentity=Weapon==EDWAnimatedWeapon::Spear&&Role==TEXT("Attack")&&Index==0;
        C->CancelAction();bReplacementStarted=C->PlayRole(TEXT("Replacement"),S.Aim());
    };
    C->OnAnimationGameplayEvent.AddDynamic(S.Probe.Get(),&UDWWeaponEventReentryTestProbe::HandleAnimation);
    // Synthetic source notification test. It does not enter a resource-backed weapon or authorize damage.
    TestTrue(TEXT("Start source clock"),C->PlayRole(TEXT("Attack"),S.Aim()));S.Advance(.25f);
    TestTrue(TEXT("Broadcast carries original identity"),bOriginalIdentity);TestTrue(TEXT("Same-source callback may start a new role"),bReplacementStarted);
    TestEqual(TEXT("Later old event not emitted"),Notifications,1);TestEqual(TEXT("Only first old event processed"),C->DispatchedEventCount,1);
    TestEqual(TEXT("Old carry cannot advance replacement"),C->GetElapsedSeconds(),0.f);
    TestEqual(TEXT("Presentation event never consumes stored shell resources"),C->GetCombatWeaponStateSnapshot().LoadedShotgunShells,3);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponEventValuesTest,"DoughWorld.Combat.WeaponAnimation.EventValueSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponEventValuesTest::RunTest(const FString& Parameters)
{
    DWWeaponReentryTests::FScope S;auto* C=S.C();auto* Set=S.MakeSet(EDWAnimatedWeapon::DualDaggers);
    if(!TestTrue(TEXT("Explicit owned/current formal dagger"),S.PrepareResources(TEXT("DWWeapon_DualDaggers"))))return false;
    FDWDaggerTuning T;FString Error;if(!FDWDaggerTuning::Build(*S.CombatTestGI->GetCombatRules(),T,Error))return false;
        {FDWSpearTuning First;if(!T.MakeSegmentTuning(0,First))return false;T.Attack=First;}
    auto* First=S.Enemy(static_cast<float>(T.Attack.RangeCm*.4));auto* Second=S.Enemy(static_cast<float>(T.Attack.RangeCm*.8));int32 Notifications=0,DamageCallbacks=0;
    S.Probe->DamageCallback=[Set,&DamageCallbacks](){++DamageCallbacks;Set->Damage=99.f;Set->DamageRange=1.f;Set->Clips[0].DamageEvents.Reset();};
    S.Probe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32){++Notifications;};
    First->OnTakeAnyDamage.AddDynamic(S.Probe.Get(),&UDWWeaponEventReentryTestProbe::HandleDamage);
    C->OnAnimationGameplayEvent.AddDynamic(S.Probe.Get(),&UDWWeaponEventReentryTestProbe::HandleAnimation);
    TestTrue(TEXT("Start formal attack"),C->PrimaryPressed(S.Aim()));C->PublishAcceptedWeaponStartEffects();S.Advance(static_cast<float>(T.Attack.WindupSeconds+.001));
    TestEqual(TEXT("Real damage listener changed the source asset"),DamageCallbacks,1);
    const float After=100.f-static_cast<float>(T.Attack.Damage);
    TestEqual(TEXT("First target uses formal entry damage"),First->Health,After);TestEqual(TEXT("Callback cannot retune same formal window"),Second->Health,After);
    TestEqual(TEXT("Source events do not provide duplicate dagger authority"),Notifications,0);
    S.Advance(static_cast<float>(T.Attack.ActiveSeconds*.5));TestEqual(TEXT("First target receipt prevents repeat"),First->Health,After);TestEqual(TEXT("Second target receipt prevents repeat"),Second->Health,After);
    return true;
}
#endif
