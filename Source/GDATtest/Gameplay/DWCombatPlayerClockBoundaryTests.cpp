#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatPlayerSave.h"
#include "DWCombatRules.h"
#include "DWCombatTestWorld.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWWeaponEventReentryTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWSaveGame.h"
#include "Animation/AnimSequence.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include <cmath>
#include <limits>

namespace DWPlayerClockTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWWeaponEventReentryTestProbe> EventProbe{NewObject<UDWWeaponEventReentryTestProbe>()};
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> DamageProbe{NewObject<UDWCombatAuthorityTestProbe>()};
    TStrongObjectPtr<UDWSaveGame> Saved;
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;
    FDWCombatPlayerVitalsConfig Config;FString Error;float Step=0.f;bool bReady=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D03ClockBoundaryWorld"));if(!World)return;
        Player=Spawn<ADWPlayerCharacter>();if(!Player||!Player->HasCombatVitalsReady())return;
        if(!FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),Config,Error))return;
        Saved.Reset(NewObject<UDWSaveGame>());
        if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*Saved,Error))return;
        // Strict initial equipment identity: no synthetic switch wait may alter the HC05 tie.
        Saved->CombatPlayerResources.OwnedPermanentWeaponIds.AddUnique(TEXT("DWWeapon_Spear"));
        Saved->CombatPlayerResources.CurrentEquipmentId=TEXT("DWWeapon_Spear");
        auto& V=Saved->CombatPlayerResources.Vitals;
        Saved->Health=DWCombatPlayerSave::HealthMaximum(Config);V.Health=Saved->Health;
        V.Water=Config.Vitals.WaterMax;V.Satiety=Config.Vitals.SatietyMax;
        V.BaseForm=EDWCombatBaseForm::Yeast;V.FormRemainingSeconds=Config.FormDurationSeconds;V.Special=Config.InitialFormSpecial;
        Saved->bYeastForm=true;Saved->Transformation=static_cast<float>(V.Special);
        const double Limit=FMath::Min(.5,FMath::Min(Config.FormDurationSeconds,FMath::Min(Config.Vitals.WaterDamagePeriodSeconds,Config.Vitals.SatietyDamagePeriodSeconds)));
        int Exponent=0;std::frexp(Limit,&Exponent);Step=static_cast<float>(std::ldexp(1.,Exponent-3));
        bReady=FMath::IsFinite(Step)&&Step>0.f&&Player->TryApplySaveData(Saved.Get())&&!GI->HasActiveSlot();
        if(bReady)
        {
            Player->WeaponAnimationComponent->OnAnimationGameplayEvent.AddDynamic(EventProbe.Get(),&UDWWeaponEventReentryTestProbe::HandleAnimation);
            Player->OnTakeAnyDamage.AddDynamic(DamageProbe.Get(),&UDWCombatAuthorityTestProbe::Any);
        }
    }
    ~FFixture()
    {
        EventProbe->AnimationCallback=nullptr;DamageProbe->AnyCallback=nullptr;
        if(!World)return;
        World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T> T* Spawn()
    {
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<T>(T::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,P);
        if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    bool Start(float EventTime)
    {
        auto* W=Player->WeaponAnimationComponent.Get();W->bEnabled=true;
        auto* Set=NewObject<UDWWeaponAnimationSet>(Player);Set->Weapon=EDWAnimatedWeapon::Spear;
        Set->bApplyBasicDamage=false;Set->bRepeatWhileHeld=false;Set->ChargeSeconds=0.f;
        // Presentation-notification clock only. Permit legal replacement exactly at its event;
        // cancellation still preserves the attack lock, so a default 1s lock is inappropriate here.
        Set->AttackIntervalSeconds=Step;
        FDWWeaponAnimationClip Clip;Clip.Role=TEXT("Attack");Clip.DurationSeconds=Step*8.f;
        Clip.Animation=NewObject<UAnimSequence>(Set);FDWWeaponDamageEvent Event;Event.TimeSeconds=EventTime;Clip.DamageEvents.Add(Event);
        Set->Clips.Add(Clip);W->AnimationSets={Set};
        return W->SelectWeapon(EDWAnimatedWeapon::Spear)&&W->PrimaryPressed(Player->GetActorLocation()+FVector(500,0,0));
    }
    bool DueAtStep(bool bFatal)
    {
        // Strict initial equipment identity: no synthetic switch wait may alter the HC05 tie.
        Saved->CombatPlayerResources.OwnedPermanentWeaponIds.AddUnique(TEXT("DWWeapon_Spear"));
        Saved->CombatPlayerResources.CurrentEquipmentId=TEXT("DWWeapon_Spear");
        auto& V=Saved->CombatPlayerResources.Vitals;V.Water=Config.Vitals.WaterFloor;V.Satiety=Config.Vitals.SatietyFloor;
        V.WaterEmptyElapsedSeconds=Config.Vitals.WaterDamagePeriodSeconds-Step;
        V.SatietyEmptyElapsedSeconds=Config.Vitals.SatietyDamagePeriodSeconds-Step;
        // Preserve an exact binary weapon/event tie even when a tunable period is not binary-exact.
        V.WaterEmptyTimeCarry=Step-(Config.Vitals.WaterDamagePeriodSeconds-V.WaterEmptyElapsedSeconds);
        V.SatietyEmptyTimeCarry=Step-(Config.Vitals.SatietyDamagePeriodSeconds-V.SatietyEmptyElapsedSeconds);
        if(bFatal)Saved->Health=std::nextafter(DWCombatPlayerSave::HealthFloor(Config),std::numeric_limits<float>::infinity());
        V.Health=Saved->Health;return Player->TryApplySaveData(Saved.Get());
    }
    void Advance(float Seconds){World->TimeSeconds+=Seconds;Player->TickGameplayClocks(Seconds,false);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerClockCallbackPauseTest,"DoughWorld.Combat.PlayerResources.WeaponCallbackSeesBoundaryAndPauseKeepsElapsed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerClockCallbackPauseTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerClockTests;FFixture F;if(!TestTrue(TEXT("Isolated resource fixture"),F.bReady))return false;
    auto* PauseOwner=F.Spawn<APlayerState>();if(!TestNotNull(TEXT("Pause owner"),PauseOwner))return false;
    int32 Calls=0;
    F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32)
    {
        ++Calls;auto* S=NewObject<UDWSaveGame>();F.Player->CaptureSaveData(S);
        TestTrue(TEXT("Event save sees current form time, not frame start or end"),FMath::IsNearlyEqual(S->CombatPlayerResources.Vitals.FormRemainingSeconds,F.Config.FormDurationSeconds-F.Step,1.e-9));
        const auto& V=S->CombatPlayerResources.Vitals;
        TestTrue(TEXT("Water already includes only time to event"),FMath::IsNearlyEqual(V.Water-V.WaterDrainCarry,FMath::Max(F.Config.Vitals.WaterFloor,F.Config.Vitals.WaterMax-F.Config.Vitals.WaterDrainPerSecond*F.Step),1.e-9));
        TestTrue(TEXT("Satiety already includes only time to event"),FMath::IsNearlyEqual(V.Satiety-V.SatietyDrainCarry,FMath::Max(F.Config.Vitals.SatietyFloor,F.Config.Vitals.SatietyMax-F.Config.Vitals.SatietyDrainPerSecond*F.Step),1.e-9));
        TestTrue(TEXT("Special already includes only time to event"),FMath::IsNearlyEqual(V.Special-V.SpecialGrowthCarry,FMath::Min(F.Config.Vitals.SpecialMax,F.Config.InitialFormSpecial+F.Config.SpecialGainPerSecond*F.Step),1.e-9));
        TestTrue(TEXT("Event save is strict-valid"),DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*S,F.Error));
        const double Before=F.Player->GetCombatFormRemaining();
        F.Player->TickGameplayClocks(F.Step,false);
        TestEqual(TEXT("Synchronous recursive clock cannot consume time twice"),F.Player->GetCombatFormRemaining(),Before);
        F.World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);
    };
    if(!TestTrue(TEXT("Start isolated authored event"),F.Start(F.Step)))return false;
    F.Advance(F.Step*4.f);
    TestEqual(TEXT("Only event before pause dispatched"),Calls,1);
    TestTrue(TEXT("Pause callback retains exactly already-elapsed resource time"),FMath::IsNearlyEqual(F.Player->GetCombatFormRemaining(),F.Config.FormDurationSeconds-F.Step,1.e-9));
    F.Player->TickGameplayClocks(F.Step*8.f,false);
    TestTrue(TEXT("Paused direct tick consumes nothing"),FMath::IsNearlyEqual(F.Player->GetCombatFormRemaining(),F.Config.FormDurationSeconds-F.Step,1.e-9));
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);F.Advance(F.Step);
    TestEqual(TEXT("No replay of boundary event"),Calls,1);
    TestTrue(TEXT("Resume advances new delta only"),FMath::IsNearlyEqual(F.Player->GetCombatFormRemaining(),F.Config.FormDurationSeconds-2.*F.Step,1.e-9));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerClockZeroBoundaryTest,"DoughWorld.Combat.PlayerResources.ZeroWeaponBoundaryConsumesNoResourceTime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerClockZeroBoundaryTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerClockTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    int32 Calls=0;F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32)
    {++Calls;TestEqual(TEXT("Time-zero callback sees unchanged resource time"),F.Player->GetCombatFormRemaining(),F.Config.FormDurationSeconds);};
    if(!TestTrue(TEXT("Start event at exactly zero"),F.Start(0.f)))return false;
    F.Player->TickGameplayClocks(0.f,false);
    TestEqual(TEXT("Zero event processed once"),Calls,1);TestEqual(TEXT("No invented epsilon"),F.Player->GetCombatFormRemaining(),F.Config.FormDurationSeconds);
    F.Player->TickGameplayClocks(0.f,false);TestEqual(TEXT("Zero event never repeats"),Calls,1);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerClockFatalTieTest,"DoughWorld.Combat.PlayerResources.HC05FatalVitalsPrecedeUncommittedWeapon",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerClockFatalTieTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerClockTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const double Damage=F.Config.Vitals.WaterDamagePerEvent+F.Config.Vitals.SatietyDamagePerEvent;
    if(Damage<=0.){AddInfo(TEXT("Current rules intentionally disable periodic damage; fatal-tie scenario is not applicable."));return true;}
    if(!TestTrue(TEXT("Strict near-death snapshot with exact due time"),F.DueAtStep(true)))return false;
    int32 Events=0,DamageNotices=0;
    F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32){++Events;};
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {++DamageNotices;TestTrue(TEXT("Fatal resource fact precedes notification"),F.Player->IsDead());TestEqual(TEXT("Weapon event has not fired"),Events,0);};
    if(!TestTrue(TEXT("Start event at same timestamp as both periodic channels"),F.Start(F.Step)))return false;
    F.Advance(F.Step*2.f);
    TestTrue(TEXT("Hunger/thirst kill player"),F.Player->IsDead());TestTrue(TEXT("Actual authority damage notification occurred"),DamageNotices>0);
    TestEqual(TEXT("HC05 cancels uncommitted tied weapon event"),Events,0);
    TestEqual(TEXT("No hidden dispatch"),F.Player->WeaponAnimationComponent->DispatchedEventCount,0);
    TestFalse(TEXT("Death ends attack"),F.Player->WeaponAnimationComponent->IsBusy());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerClockNonFatalTieTest,"DoughWorld.Combat.PlayerResources.HC05NonfatalTiedWeaponSeesBothResourceFacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerClockNonFatalTieTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerClockTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const float Floor=DWCombatPlayerSave::HealthFloor(F.Config);
    const float Expected=FMath::Max(Floor,FMath::Max(Floor,F.Saved->Health-static_cast<float>(F.Config.Vitals.WaterDamagePerEvent))-static_cast<float>(F.Config.Vitals.SatietyDamagePerEvent));
    if(Expected<=Floor){AddInfo(TEXT("Current rules make the two periodic channels lethal even at full health; nonfatal tie is not applicable."));return true;}
    if(!TestTrue(TEXT("Strict due snapshot"),F.DueAtStep(false)))return false;
    int32 Events=0;
    F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32)
    {
        ++Events;auto* S=NewObject<UDWSaveGame>();F.Player->CaptureSaveData(S);
        TestEqual(TEXT("Weapon observer sees both prior HP facts"),S->Health,Expected);
        TestEqual(TEXT("Dehydration ordinal and health agree"),S->CombatPlayerResources.Vitals.DehydrationOrdinal,uint64(1));
        TestEqual(TEXT("Starvation ordinal and health agree"),S->CombatPlayerResources.Vitals.StarvationOrdinal,uint64(1));
        TestTrue(TEXT("Save at weapon event is strict-valid"),DWCombatPlayerSave::Validate(*F.GI->GetCombatRules(),*S,F.Error));
    };
    if(!TestTrue(TEXT("Start tied event"),F.Start(F.Step)))return false;
    F.Advance(F.Step);TestEqual(TEXT("Nonfatal tied event proceeds once"),Events,1);TestEqual(TEXT("Both periodic amounts applied"),F.Player->Health,Expected);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerClockRestoreCallbackTest,"DoughWorld.Combat.PlayerResources.RestoreInWeaponCallbackRejectsOldFrameCarry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerClockRestoreCallbackTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerClockTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    int32 Calls=0;F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32)
    {++Calls;TestTrue(TEXT("In-memory strict restore in actual delegate"),F.Player->TryApplySaveData(F.Saved.Get()));};
    if(!TestTrue(TEXT("Start event"),F.Start(F.Step)))return false;
    F.Advance(F.Step*4.f);
    TestEqual(TEXT("One callback"),Calls,1);TestEqual(TEXT("Restored timeline receives no old carry"),F.Player->GetCombatFormRemaining(),F.Config.FormDurationSeconds);
    TestFalse(TEXT("Restore cancels old action"),F.Player->WeaponAnimationComponent->IsBusy());TestFalse(TEXT("No formal slot opened"),F.GI->HasActiveSlot());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerClockReplacementCallbackTest,"DoughWorld.Combat.PlayerResources.ReplacementInWeaponCallbackReceivesNoOldFrameCarry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerClockReplacementCallbackTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerClockTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    if(!TestTrue(TEXT("Start event"),F.Start(F.Step)))return false;
    auto* W=F.Player->WeaponAnimationComponent.Get();auto* Set=W->GetSelectedSet();
    FDWWeaponAnimationClip Replacement;Replacement.Role=TEXT("ClockReplacement");Replacement.DurationSeconds=F.Step*8.f;
    Replacement.Animation=NewObject<UAnimSequence>(Set);Set->Clips.Add(Replacement);
    int32 Calls=0;bool bReplacementStarted=false;
    F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32)
    {++Calls;W->CancelAction();bReplacementStarted=W->PlayRole(Replacement.Role,F.Player->GetActorLocation()+FVector(500,0,0));};
    F.Advance(F.Step*4.f);
    TestEqual(TEXT("Original event ran once"),Calls,1);TestTrue(TEXT("Replacement accepted in callback"),bReplacementStarted);
    TestEqual(TEXT("Replacement clock stays zero across Player outer loop"),W->GetElapsedSeconds(),0.f);
    TestEqual(TEXT("Replacement role survives"),W->GetActiveRole(),Replacement.Role);
    TestTrue(TEXT("Cancelling a weapon does not extend the independent resource timeline"),FMath::IsNearlyEqual(F.Player->GetCombatFormRemaining(),F.Config.FormDurationSeconds-4.*F.Step,1.e-9));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerClockResourceCallbackPauseTest,"DoughWorld.Combat.PlayerResources.ResourceCallbackPauseResumesPaidWeaponWithoutDoubleTime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerClockResourceCallbackPauseTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerClockTests;FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
    const double Damage=F.Config.Vitals.WaterDamagePerEvent+F.Config.Vitals.SatietyDamagePerEvent;
    const float Floor=DWCombatPlayerSave::HealthFloor(F.Config);
    const float Expected=FMath::Max(Floor,FMath::Max(Floor,F.Saved->Health-static_cast<float>(F.Config.Vitals.WaterDamagePerEvent))-static_cast<float>(F.Config.Vitals.SatietyDamagePerEvent));
    if(Damage<=0.||Expected<=Floor){AddInfo(TEXT("Configured periodic damage cannot produce a surviving damage callback; paid-slice scenario not applicable."));return true;}
    if(!TestTrue(TEXT("Due snapshot"),F.DueAtStep(false)))return false;
    auto* PauseOwner=F.Spawn<APlayerState>();if(!PauseOwner)return false;
    int32 Events=0;F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32){++Events;};
    F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        TestEqual(TEXT("Both same-time HP facts committed before pausing"),F.Player->Health,Expected);
        F.World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);
    };
    if(!TestTrue(TEXT("Start tied event"),F.Start(F.Step)))return false;
    F.Advance(F.Step*4.f);
    TestTrue(TEXT("Resource observer actually paused"),F.World->IsPaused());TestEqual(TEXT("Paused before weapon event"),Events,0);
    TestEqual(TEXT("Weapon still at slice start while paused"),F.Player->WeaponAnimationComponent->GetElapsedSeconds(),0.f);
    const double PaidResourceTime=F.Player->GetCombatFormRemaining();
    TestTrue(TEXT("Resources paid exactly the event delta"),FMath::IsNearlyEqual(PaidResourceTime,F.Config.FormDurationSeconds-F.Step,1.e-9));
    F.Player->TickGameplayClocks(F.Step*8.f,false);TestEqual(TEXT("Pause retains pending without running it"),Events,0);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    F.Player->TickGameplayClocks(0.f,false);
    TestEqual(TEXT("Resume first reaches already-paid weapon event"),Events,1);
    TestEqual(TEXT("No second resource deduction while catching weapon up"),F.Player->GetCombatFormRemaining(),PaidResourceTime);
    TestEqual(TEXT("Weapon catches up exactly, without new elapsed resource time"),F.Player->WeaponAnimationComponent->GetElapsedSeconds(),F.Step);
    TestEqual(TEXT("Periodic health cannot repeat on resume"),F.Player->Health,Expected);
    F.Advance(F.Step);
    TestEqual(TEXT("Paid weapon event never repeats"),Events,1);
    TestTrue(TEXT("Normal resumed resource delta follows the paid slice"),FMath::IsNearlyEqual(F.Player->GetCombatFormRemaining(),F.Config.FormDurationSeconds-2.*F.Step,1.e-9));
    TestEqual(TEXT("Weapon and resource clocks remain aligned after another delta"),F.Player->WeaponAnimationComponent->GetElapsedSeconds(),F.Step*2.f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerClockPaidSliceInvalidationTest,"DoughWorld.Combat.PlayerResources.PaidWeaponSliceCannotSurviveCancelRestoreOrChangedClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerClockPaidSliceInvalidationTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerClockTests;
    for(int32 Mode=0;Mode<3;++Mode)
    {
        FFixture F;if(!TestTrue(TEXT("Fixture"),F.bReady))return false;
        const double Damage=F.Config.Vitals.WaterDamagePerEvent+F.Config.Vitals.SatietyDamagePerEvent;
        if(Damage<=0.||Damage>=F.Saved->Health-DWCombatPlayerSave::HealthFloor(F.Config))
        {AddInfo(TEXT("No nonfatal periodic-callback scenario under current rules."));return true;}
        if(!TestTrue(TEXT("Due snapshot"),F.DueAtStep(false)))return false;
        auto* PauseOwner=F.Spawn<APlayerState>();if(!PauseOwner)return false;
        int32 Events=0;F.EventProbe->AnimationCallback=[&](EDWAnimatedWeapon,FName,int32){++Events;};
        bool bManuallyAdvanced=false;
        F.DamageProbe->AnyCallback=[&](AActor*,float,AController*,AActor*)
        {
            if(Mode==2&&!bManuallyAdvanced)
            {bManuallyAdvanced=true;TestTrue(TEXT("Native listener consumes the same original slice once"),F.Player->WeaponAnimationComponent->AdvanceAnimation(F.Step));}
            F.World->GetWorldSettings()->SetPauserPlayerState(PauseOwner);
        };
        if(!TestTrue(TEXT("Start tied event"),F.Start(F.Step)))return false;
        F.Advance(F.Step*2.f);if(!TestTrue(TEXT("Paused with pending paid weapon time"),F.World->IsPaused()))return false;
        if(Mode==0)F.Player->WeaponAnimationComponent->CancelAction();
        F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        if(Mode==1&&!TestTrue(TEXT("Restore rotates life and cancels pending"),F.Player->TryApplySaveData(F.Saved.Get())))return false;
        const double Before=F.Player->GetCombatFormRemaining();
        F.Player->TickGameplayClocks(0.f,false);
        TestEqual(FString::Printf(TEXT("Mode %d cannot revive or repeat old event"),Mode),Events,Mode==2?1:0);
        TestEqual(TEXT("Discarding invalid paid slice consumes no new resource time"),F.Player->GetCombatFormRemaining(),Before);
        if(Mode==2)TestEqual(TEXT("Same-role manual progression is not added again on resume"),F.Player->WeaponAnimationComponent->GetElapsedSeconds(),F.Step);
        else TestFalse(TEXT("Cancelled old action stays stopped"),F.Player->WeaponAnimationComponent->IsBusy());
    }
    return true;
}
#endif
