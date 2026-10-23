#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatSpear.h"
#include "DWCombatRules.h"
#include "DWCombatPresentationTiming.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "DWSaveGame.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWDamageVolumeLibrary.h"
#include "Animation/AnimSequence.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DWSpearTests
{
FDWSpearTuning Tuning()
{
    // Isolated explicit scenario values; these are not a production fallback configuration.
    FDWSpearTuning T;T.Damage=28.;T.RangeCm=320.;T.FullArcDegrees=80.;T.IntervalSeconds=1.;
    T.EdgeBandFraction=.25;T.MinimumDamageFraction=.5;T.KnockbackCm=60.;T.KnockbackSeconds=.2;
    T.WindupSeconds=.2;T.ActiveSeconds=.18;T.RecoverySeconds=.62;
    T.YeastDamageMultiplier=1.2;T.YeastRateMultiplier=1.15;T.RootedDamageMultiplier=1.35;T.SwitchLockSeconds=.15;return T;
}
struct FWorldFixture
{
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;UDWCombatAuthoritySubsystem* Authority=nullptr;
    TStrongObjectPtr<UDWGameInstance> GI{NewObject<UDWGameInstance>(GEngine)};
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe{NewObject<UDWCombatAuthorityTestProbe>()};
    bool bReady=false;
    FWorldFixture()
    {
        const FString Previous=FCommandLine::Get();
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/D12Spear"));
        const FString Isolated=FString::Printf(TEXT("-DWVerificationSavePrefix=DW_QA_D12_ -DWSaveTestDirectory=\"%s\" %s"),*Directory,*Previous);
        FCommandLine::Set(*Isolated);
        GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWSpearTest")),GetTransientPackage());
        FCommandLine::Set(*Previous);
        World=GI->GetWorld();if(!World||!GI->GetCombatRules())return;
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);
        Player=Spawn<ADWPlayerCharacter>(FVector(0,0,500));if(!Player)return;
        Authority=UDWCombatAuthoritySubsystem::Get(Player);
        Player->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        bReady=Authority&&Player->HasCombatVitalsReady()&&!GI->HasActiveSlot()&&GI->GetSaveDirectory()==Directory;
    }
    ~FWorldFixture()
    {
        Probe->AnyCallback=nullptr;
        if(!World)return;
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn(FVector Location)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        T* A=World->SpawnActor<T>(T::StaticClass(),Location,FRotator::ZeroRotator,Params);
        if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    ADWEnemyCharacter* Enemy(FVector Offset,FVector Extent)
    {
        auto* E=Spawn<ADWEnemyCharacter>(Player->GetActorLocation()+Offset);E->Health=100.f;
        E->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        auto* Box=NewObject<UDWDamageHitboxComponent>(E);E->AddInstanceComponent(Box);Box->SetupAttachment(E->GetRootComponent());
        Box->SetBoxExtent(Extent);Box->RegisterComponent();Box->SetWorldLocation(E->GetActorLocation());
        E->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);return E;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSpearCurveTest,"DoughWorld.Combat.Spear.RulesAndDistanceCurve",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSpearCurveTest::RunTest(const FString& Parameters)
{
    auto T=DWSpearTests::Tuning();double D=-7.;
    for(const auto& P:TArray<TPair<double,double>>{{0.,14.},{120.,21.},{240.,28.},{320.,28.}})
    {TestTrue(TEXT("Valid distance"),T.DamageAtDistance(P.Key,T.Damage,D));TestTrue(TEXT("Expected linear/band damage"),FMath::IsNearlyEqual(D,P.Value,1.e-9));}
    TestFalse(TEXT("Strict outside range"),T.DamageAtDistance(320.00001,T.Damage,D));
    TestFalse(TEXT("Negative distance invalid"),T.DamageAtDistance(-1.,T.Damage,D));
    D=-7.;TestFalse(TEXT("NaN rejected"),T.DamageAtDistance(std::numeric_limits<double>::quiet_NaN(),T.Damage,D));TestEqual(TEXT("Failure leaves output"),D,-7.);
    TestTrue(TEXT("Yeast snapshot curve"),T.DamageAtDistance(0.,T.Damage*T.DamageMultiplier(EDWSpearForm::Yeast),D));TestTrue(TEXT("Yeast near 16.8"),FMath::IsNearlyEqual(D,16.8,1.e-9));
    TestEqual(TEXT("Rooted speed does not inherit ranged bonus"),T.RateMultiplier(EDWSpearForm::MoldRooted),1.);
    T.WindupSeconds=0.;T.RecoverySeconds=.82;TestTrue(TEXT("Zero windup is configurable without invented lower limit"),T.IsValid());
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWSpearTuning Live;
    if(!TestTrue(TEXT("Load approved project payload"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
    if(!TestTrue(TEXT("Read all approved spear rules"),FDWSpearTuning::Build(*Rules,Live,Error))){AddError(Error);return false;}
    double RangeMeters=0.;Rules->TryGetNumber(TEXT("weapon.spear.range_m"),EDWCombatRuleUnit::Meters,RangeMeters,Error);
    TestTrue(TEXT("Single m-to-cm conversion"),FMath::IsNearlyEqual(Live.RangeCm,RangeMeters*100.,1.e-9));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSpearPhaseTest,"DoughWorld.Combat.Spear.PhaseBoundariesAndFormSnapshots",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSpearPhaseTest::RunTest(const FString& Parameters)
{
    const auto T=DWSpearTests::Tuning();FDWSpearAttackPlan P;double Used=0.,Left=0.;
    TestTrue(TEXT("Start yeast windup"),P.Start(T,EDWSpearForm::Yeast));
    TestTrue(TEXT("Advance half phase"),P.AdvanceCurrentPhase(.1,Used,Left));TestTrue(TEXT("Source seconds honor stage rate"),FMath::IsNearlyEqual(P.GetSourceSeconds(),.115,1.e-9));
    TestFalse(TEXT("Cannot prematurely enter active"),P.EnterNextPhase(EDWSpearForm::Normal));
    P.AdvanceCurrentPhase(1.,Used,Left);TestTrue(TEXT("Large delta stops at first boundary"),P.IsAtBoundary()&&Left>0.);
    TestTrue(TEXT("Expire before active boundary"),P.EnterNextPhase(EDWSpearForm::Normal));
    TestEqual(TEXT("Active reads current form"),P.GetPhaseRate(),1.);TestEqual(TEXT("Damage fixed at release"),P.GetDamageSnapshot(),28.);
    P.AdvanceCurrentPhase(.18,Used,Left);P.EnterNextPhase(EDWSpearForm::Yeast);
    TestEqual(TEXT("Next stage picks new speed"),P.GetPhaseRate(),1.15);TestEqual(TEXT("New stage cannot rewrite released damage"),P.GetDamageSnapshot(),28.);
    const double Before=P.GetSourceSeconds();Used=17.;Left=19.;
    TestFalse(TEXT("Invalid delta refused atomically"),P.AdvanceCurrentPhase(-1.,Used,Left));TestEqual(TEXT("No progress rollback"),P.GetSourceSeconds(),Before);TestEqual(TEXT("No changed output"),Used,17.);
    P.AdvanceCurrentPhase(1.,Used,Left);P.EnterNextPhase(EDWSpearForm::Normal);
    TestTrue(TEXT("Complete at terminal pose"),P.GetPhase()==EDWSpearPhase::Complete&&P.GetSourceSeconds()==1.);
    TestFalse(TEXT("Completed phase cannot be replayed"),P.EnterNextPhase(EDWSpearForm::Normal));
    FDWSpearAttackPlan Q;Q.Start(T,EDWSpearForm::Normal);Q.Cancel();TestFalse(TEXT("Cancelled never advances"),Q.AdvanceCurrentPhase(1.,Used,Left));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSpearSourceGateTest,"DoughWorld.Combat.Spear.HC04BindingGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSpearSourceGateTest::RunTest(const FString& Parameters)
{
    auto T=DWSpearTests::Tuning();FString Error;
    TestTrue(TEXT("A truthful source marker may use different absolute seconds than the gameplay active window"),FDWSpearSourceTiming{1.,.5,.5}.Validate(T,Error));
    TestTrue(TEXT("Source validation clears a prior diagnostic"),Error.IsEmpty());
    TestTrue(TEXT("Existing aligned source stays valid"),FDWSpearSourceTiming{1.,.2,.38}.Validate(T,Error));
    TestTrue(TEXT("A complete source with a different duration may be retimed"),FDWSpearSourceTiming{1.1,.2,.38}.Validate(T,Error));
    auto Faster=T;Faster.IntervalSeconds*=.5;Faster.WindupSeconds*=.5;Faster.ActiveSeconds*=.5;Faster.RecoverySeconds*=.5;
    TestTrue(TEXT("Legal target duration changes cannot reject the complete approved source"),FDWSpearSourceTiming{1.,7./24.,7./24.}.Validate(Faster,Error));
    TestEqual(TEXT("Retiming preserves the complete source endpoint"),DWCombatPresentationTiming::MapSegment(Faster.IntervalSeconds,0.,Faster.IntervalSeconds,0.,1.),1.);
    TestFalse(TEXT("Contact outside its real source remains invalid"),FDWSpearSourceTiming{1.,1.1,1.1}.Validate(T,Error));
    TestFalse(TEXT("Reversed source contact remains invalid"),FDWSpearSourceTiming{1.,.4,.2}.Validate(T,Error));
    TestFalse(TEXT("Nonfinite source remains invalid"),FDWSpearSourceTiming{std::numeric_limits<double>::quiet_NaN(),.2,.3}.Validate(T,Error));
    DWSpearTests::FWorldFixture F;if(!TestTrue(TEXT("Isolated native player ready"),F.bReady))return false;
    auto* W=F.Player->WeaponAnimationComponent.Get();
    auto* Set=NewObject<UDWWeaponAnimationSet>(F.Player);Set->Weapon=EDWAnimatedWeapon::Spear;
    FDWWeaponAnimationClip Clip;Clip.Animation=NewObject<UAnimSequence>(Set);Clip.DurationSeconds=1.f;
    FDWWeaponDamageEvent Contact;Contact.TimeSeconds=.5f;Clip.DamageEvents.Add(Contact);Set->Clips.Add(Clip);W->AnimationSets={Set};
    TSharedPtr<const FDWCombatRules> Rules;if(!TestTrue(TEXT("Rules"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
    TestFalse(TEXT("No implicit activation"),W->IsFormalSpearConfigured());
    TestTrue(TEXT("A truthful matching clip can bind without a no-retime rule"),W->ConfigureFormalSpear(*Rules,Clip.Animation,{1.,.5,.5},Error));
    TestFalse(TEXT("Supplying a false .2 timing cannot bypass the clip's .5 marker"),W->ConfigureFormalSpear(*Rules,Clip.Animation,{1.,.2,.38},Error));
    TestTrue(TEXT("A rejected replacement does not discard the last validated source identity"),W->IsFormalSpearConfigured());
    Set->Clips[0].DamageEvents.Reset();
    TestFalse(TEXT("Missing source contact evidence refused"),W->ConfigureFormalSpear(*Rules,Clip.Animation,{1.,.2,.38},Error));
    // Only this transient synthetic clip changes; no authored sequence or DA is modified.
    Contact.TimeSeconds=.25f;Set->Clips[0].DamageEvents.Add(Contact);
    TestTrue(TEXT("Explicit synthetic fixture binding accepted"),W->ConfigureFormalSpear(*Rules,Clip.Animation,{1.,.2,.38},Error));
    TestFalse(TEXT("PlayRole cannot bypass formal request"),W->PlayRole(TEXT("Attack"),F.Player->GetActorLocation()+FVector(100,0,0)));
    TestFalse(TEXT("Missing ownership cannot select formal spear gameplay"),W->SelectWeapon(EDWAnimatedWeapon::Spear));
    TestTrue(TEXT("Unowned spear request preserves actual empty-hand identity"),W->GetCurrentEquipmentId().IsNone()&&W->SelectedWeapon==EDWAnimatedWeapon::Unarmed);
    TestFalse(TEXT("No formal spear action was started"),W->GetFormalSpearPhase()==EDWSpearPhase::Windup);
    TestEqual(TEXT("Existing source duration preserved"),Set->Clips[0].DurationSeconds,1.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSpearGeometryTest,"DoughWorld.Combat.Spear.CollisionEdgesAndWall",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSpearGeometryTest::RunTest(const FString& Parameters)
{
    using namespace DWSpearTests;FWorldFixture F;if(!TestTrue(TEXT("Isolated native player ready"),F.bReady))return false;const auto T=Tuning();
    auto* Large=F.Enemy(FVector(400,0,0),FVector(160,35,70));
    auto* Near=F.Enemy(FVector(70,0,0),FVector(70,25,70));
    auto* Side=F.Enemy(FVector(0,300,0),FVector(25));FDWSpearHitCandidate Hit;
    const FVector Origin=F.Player->GetActorLocation();
    TestTrue(TEXT("Large centre outside but collision edge inside"),FDWSpearHitQuery::Evaluate(F.World,F.Player,Large,Origin,FVector::ForwardVector,T,Hit));
    TestTrue(TEXT("Uses edge 240 not centre 400"),FMath::IsNearlyEqual(Hit.EdgeDistanceCm,240.,.01));
    TestTrue(TEXT("Close collision is valid"),FDWSpearHitQuery::Evaluate(F.World,F.Player,Near,Origin,FVector::ForwardVector,T,Hit));
    TestTrue(TEXT("Close distance zero"),FMath::IsNearlyZero(Hit.EdgeDistanceCm,.01));
    TestFalse(TEXT("Outside full-angle half-cone"),FDWSpearHitQuery::Evaluate(F.World,F.Player,Side,Origin,FVector::ForwardVector,T,Hit));
    auto* EdgeBox=F.Enemy(FVector(200,200,0),FVector(120,15,50));
    TestTrue(TEXT("Box overlaps sector although its nearest point is outside the half-angle"),FDWSpearHitQuery::Evaluate(F.World,F.Player,EdgeBox,Origin,FVector::ForwardVector,T,Hit));
    TestTrue(TEXT("Falloff still uses global closest collision edge"),FMath::IsNearlyEqual(Hit.EdgeDistanceCm,FVector(80,185,0).Size(),.01));
    TestTrue(TEXT("Sight witness is inside actual sector"),FVector::DotProduct((Hit.ClosestPoint-Origin).GetSafeNormal2D(),FVector::ForwardVector)>=FMath::Cos(FMath::DegreesToRadians(40.))-.0001);
    auto* High=F.Enemy(FVector(100,0,420),FVector(20,20,100));
    TestFalse(TEXT("XY projection alone cannot hit a target outside the 3D range"),FDWSpearHitQuery::Evaluate(F.World,F.Player,High,Origin,FVector::ForwardVector,T,Hit));
    auto Wide=T;Wide.FullArcDegrees=270.;
    TestTrue(TEXT("Large legal arcs include their side sector"),FDWSpearHitQuery::Evaluate(F.World,F.Player,Side,Origin,FVector::ForwardVector,Wide,Hit));
    auto* Wall=F.Spawn<AActor>(Origin+FVector(160,0,0));auto* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);
    Box->SetBoxExtent(FVector(10,60,100));Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetCollisionResponseToAllChannels(ECR_Ignore);Box->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);Box->RegisterComponent();Box->SetWorldLocation(Origin+FVector(160,0,0));
    TestFalse(TEXT("Visibility wall blocks formal spear even if legacy LOS is disabled"),FDWSpearHitQuery::Evaluate(F.World,F.Player,Large,Origin,FVector::ForwardVector,T,Hit));
    Wall->Destroy();
    Large->SetActorLocation(Origin+FVector(481,0,0));
    TestFalse(TEXT("Nearest edge beyond range never clamped into hit"),FDWSpearHitQuery::Evaluate(F.World,F.Player,Large,Origin,FVector::ForwardVector,T,Hit));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSpearUniquePacketTest,"DoughWorld.Combat.Spear.TypedMultiTargetAndReentry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSpearUniquePacketTest::RunTest(const FString& Parameters)
{
    using namespace DWSpearTests;using namespace DWCombatCommit;FWorldFixture F;if(!TestTrue(TEXT("Isolated native player ready"),F.bReady))return false;const auto T=Tuning();
    auto* Near=F.Enemy(FVector(70,0,0),FVector(70,20,60));auto* Far=F.Enemy(FVector(300,0,0),FVector(60,20,60));
    const auto Action=F.Authority->BeginAction(F.Player);TArray<FDWSpearHitCandidate> Hits;
    FDWSpearHitQuery::Collect(F.World,F.Player,F.Player->GetActorLocation(),FVector::ForwardVector,T,Hits);
    TestEqual(TEXT("One candidate per actor"),Hits.Num(),2);int32 Callbacks=0;
    TArray<FDWCombatDamageCommand> Commands;
    for(const auto& Hit:Hits)
    {
        double Damage=0.;if(!T.DamageAtDistance(Hit.EdgeDistanceCm,T.Damage,Damage))continue;
        FDWCombatDamageCommand C;C.Identity={Action,1,EActionDamageKind::Melee,1};C.TargetLife=F.Authority->CaptureActorLife(Hit.Target.Get());
        C.Source=F.Player;C.Target=Hit.Target;C.Amount=static_cast<float>(Damage);C.KnockbackSeconds=static_cast<float>(T.KnockbackSeconds);Commands.Add(C);
    }
    F.Probe->AnyCallback=[&](AActor* Target,float,AController*,AActor*)
    {
        ++Callbacks;
        for(const auto& C:Commands)if(C.Target.Get()==Target)
            TestEqual(TEXT("Real callback repeating same packet has no fresh effect"),F.Authority->SubmitActionDamage(C).AppliedThisCall,0.f);
    };
    for(int32 VisualMarker=0;VisualMarker<3;++VisualMarker)for(const auto& C:Commands)F.Authority->SubmitActionDamage(C);
    TestEqual(TEXT("Near only one 14-point hit"),Near->Health,86.f);TestEqual(TEXT("Far only one 28-point hit"),Far->Health,72.f);
    TestEqual(TEXT("Three markers plus callback retries still two actual notifications"),Callbacks,2);
    F.Authority->EndAction(Action);
    for(const auto& C:Commands)TestEqual(TEXT("Ended action cannot create new effects"),F.Authority->SubmitActionDamage(C).AppliedThisCall,0.f);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSpearComponentReentryTest,"DoughWorld.Combat.Spear.ComponentStageAndCallbackCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSpearComponentReentryTest::RunTest(const FString& Parameters)
{
    using namespace DWSpearTests;FWorldFixture F;if(!TestTrue(TEXT("Isolated native player ready"),F.bReady))return false;
    FString Error;auto* Save=NewObject<UDWSaveGame>();
    if(!TestTrue(TEXT("Strict isolated resource candidate"),DWCombatPlayerSave::MakeNew(*F.GI->GetCombatRules(),*Save,Error)))return false;
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Spear);
    if(!TestNotNull(TEXT("Registered permanent spear"),Binding))return false;
    Save->CombatPlayerResources.OwnedPermanentWeaponIds.Add(Binding->ItemId);Save->CombatPlayerResources.CurrentEquipmentId=Binding->ItemId;
    if(!TestTrue(TEXT("Explicit fixture ownership through strict restore"),F.Player->TryApplySaveData(Save)))return false;
    auto* W=F.Player->WeaponAnimationComponent.Get();auto* Set=NewObject<UDWWeaponAnimationSet>(F.Player);
    Set->Weapon=EDWAnimatedWeapon::Spear;FDWWeaponAnimationClip Clip;Clip.Animation=NewObject<UAnimSequence>(Set);Clip.DurationSeconds=1.f;
    FDWWeaponDamageEvent Event;Event.TimeSeconds=.25f;Clip.DamageEvents.Add(Event);Set->Clips.Add(Clip);W->AnimationSets={Set};
    if(!TestTrue(TEXT("Synthetic source independently aligned"),W->ConfigureFormalSpear(*F.GI->GetCombatRules(),Clip.Animation,{1.,.2,.38},Error)))return false;
    auto* Near=F.Enemy(FVector(70,0,0),FVector(70,20,60));auto* Far=F.Enemy(FVector(300,0,0),FVector(60,20,60));int32 Calls=0;
    F.Probe->AnyCallback=[&](AActor* Victim,float,AController*,AActor*){++Calls;if(Victim==Near)W->CancelAction();};
    if(!TestTrue(TEXT("Real component accepts one owned spear action"),W->PrimaryPressed(F.Player->GetActorLocation()+FVector(100,0,0))))return false;
    TestTrue(TEXT("Whole action locks movement"),W->LocksMovement());
    W->AdvanceAnimation(.1f);TestEqual(TEXT("Windup cannot deal early damage"),Near->Health,100.f);
    W->AdvanceAnimation(.2f);
    TestEqual(TEXT("First active contact commits once"),Near->Health,86.f);
    TestEqual(TEXT("Callback cancellation prevents second target from old loop"),Far->Health,100.f);
    TestEqual(TEXT("One actual engine notification"),Calls,1);
    TestFalse(TEXT("Callback ended formal component action"),W->IsBusy());
    TestFalse(TEXT("Cancellation preserves original attack lock"),W->PrimaryPressed(F.Player->GetActorLocation()+FVector(100,0,0)));
    W->AdvanceAnimation(2.f);TestEqual(TEXT("Old clock cannot restart damage"),Near->Health,86.f);return true;
}
#endif
