#if WITH_DEV_AUTOMATION_TESTS
#include "DWWeaponAnimationComponent.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatPlayerSave.h"
#include "DWInventoryComponent.h"
#include "DWCombatTestWorld.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWSaveGame.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Dom/JsonObject.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DWPresentationTimingTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;TSharedPtr<const FDWCombatRules> OriginalRules;
    UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;ADWPlayerController* PC=nullptr;FString Error;float InitialHealth=0.f;
    explicit FFixture(double Scale)
    {
        World=DWCombatTestWorld::Create(GI,TEXT("Feedback25PresentationTiming"));if(!World)return;
        OriginalRules=GI->GetCombatRules();FString Text;TSharedPtr<FJsonObject> Root;
        if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWCombatRules.json")))||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root)return;
        const TSet<FString> Scaled={
            TEXT("weapon.spear.interval_s"),TEXT("weapon.spear.windup_s"),TEXT("weapon.spear.active_s"),TEXT("weapon.spear.recovery_s"),
            TEXT("weapon.axe.interval_s"),TEXT("weapon.axe.windup_s"),TEXT("weapon.axe.active_s"),TEXT("weapon.axe.recovery_s"),
            TEXT("weapon.dagger.stage1_duration_s"),TEXT("weapon.dagger.stage2_duration_s"),TEXT("weapon.dagger.stage3_duration_s"),TEXT("weapon.dagger.stage4_duration_s"),
            TEXT("weapon.gun.interval_s"),TEXT("weapon.gun.windup_s"),TEXT("weapon.gun.recovery_s"),
            TEXT("weapon.bow.interval_s"),TEXT("weapon.bow.windup_s"),TEXT("weapon.bow.recovery_s"),
            TEXT("weapon.shotgun.interval_s"),TEXT("weapon.shotgun.windup_s"),TEXT("weapon.shotgun.recovery_s"),TEXT("weapon.shotgun.reload_one_s"),
            TEXT("weapon.alcohol.interval_s"),TEXT("weapon.alcohol.windup_s"),TEXT("weapon.alcohol.recovery_s"),
            TEXT("weapon.sword.charge_s"),TEXT("weapon.sword.slam_active_s"),TEXT("weapon.sword.follow_gap_s"),TEXT("weapon.sword.sweep_active_s"),TEXT("weapon.sword.recovery_s")};
        int32 Changed=0;
        for(const auto& Row:Root->GetArrayField(TEXT("parameters")))
        {
            const auto Object=Row->AsObject();if(!Object)continue;
            if(Scaled.Contains(Object->GetStringField(TEXT("id")))){Object->SetNumberField(TEXT("value"),Object->GetNumberField(TEXT("value"))*Scale);++Changed;}
        }
        if(Changed!=Scaled.Num()){Error=TEXT("Install all formal timing parameters before this verification.");return;}
        FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));TSharedPtr<const FDWCombatRules> Rules;
        if(!FDWCombatRules::LoadFromJson(Text,Rules,Error))return;
        // The fixture owns a non-const GI created by NewObject, not a const object. Only its
        // shared_ptr member is replaced, after strict parsing and BEFORE any player is spawned.
        // No rule object is const-mutated, no production setter/config/save is changed.
        const_cast<TSharedPtr<const FDWCombatRules>&>(GI->GetCombatRules())=Rules;
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        // Bottle's real release requires a legal standable landing surface. This transient
        // query-only floor is test world geometry, not an actor/ground bypass in production.
        auto* Floor=World->SpawnActor<AActor>(AActor::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(!Floor){Error=TEXT("Transient landing floor could not be created.");return;}
        auto* Surface=NewObject<UBoxComponent>(Floor);Floor->AddInstanceComponent(Surface);Floor->SetRootComponent(Surface);
        Surface->SetBoxExtent(FVector(3000.,3000.,50.));Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Surface->SetCollisionObjectType(ECC_WorldStatic);Surface->SetCollisionResponseToAllChannels(ECR_Block);
        Surface->SetCanEverAffectNavigation(false);Surface->RegisterComponent();Floor->SetActorLocation(FVector(1000.,0.,-50.));
        P=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Params);if(!P)return;
        auto* W=P->WeaponAnimationComponent.Get();if(!W)return;
        for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
        {
            const FString Path=Binding.Presentation==EDWAnimatedWeapon::Spear
                ?TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Spear_HC04_CandidateA_R1.DA_DWHeroWeapon_Spear_HC04_CandidateA_R1")
                :Binding.PresentationAsset.ToString();
            auto* Set=LoadObject<UDWWeaponAnimationSet>(nullptr,*Path);if(!Set){Error=TEXT("Source set missing: ")+Path;return;}W->AnimationSets.Add(Set);
            if(Binding.Presentation==EDWAnimatedWeapon::Spear)
            {
                const auto* Clip=Set->FindClip(TEXT("Attack"));if(!Clip||!Clip->Animation)return;
                W->bEnableFormalSpearExplicitBinding=true;W->ApprovedFormalSpearSequence=Cast<UAnimSequence>(Clip->Animation.Get());
                W->ApprovedFormalSpearSourceDurationSeconds=Clip->Animation->GetPlayLength();
                W->ApprovedFormalSpearContactBeginSeconds=W->ApprovedFormalSpearContactEndSeconds=7./24.;
            }
        }
        if(!P->HasActorBegunPlay())P->DispatchBeginPlay();
        PC=World->SpawnActor<ADWPlayerController>(ADWPlayerController::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(!PC)return;PC->SetAsLocalPlayerController();if(!PC->HasActorBegunPlay())PC->DispatchBeginPlay();PC->Possess(P);
        PC->SetActorTickEnabled(false);PC->PrepareCombatInputForSimulation();P->SetActorTickEnabled(false);
        P->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        auto* Save=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*Rules,*Save,Error))return;
        for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
        {
            if(Binding.ItemRole==EDWCombatItemRole::PermanentWeapon)Save->CombatPlayerResources.OwnedPermanentWeaponIds.Add(Binding.ItemId);
            if(!Binding.AmmoItemId.IsNone()){FDWItemStack Stock;Stock.ItemId=Binding.AmmoItemId;Stock.Quantity=16;Save->Inventory.Add(Stock);}
        }
        int32 Capacity=0;if(!Rules->TryGetCount(TEXT("weapon.shotgun.magazine"),Capacity,Error))return;
        Save->CombatPlayerResources.LoadedShotgunShells=Capacity;
        if(!P->TryApplySaveData(Save)){Error=TEXT("Explicit isolated initial state rejected.");return;}
        PC->PrepareCombatInputForSimulation();InitialHealth=P->GetHealth();
    }
    ~FFixture()
    {
        if(!World)return;
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        World->EndPlay(EEndPlayReason::EndPlayInEditor);
        if(GI.Get()){const_cast<TSharedPtr<const FDWCombatRules>&>(GI->GetCombatRules())=OriginalRules;GI->Shutdown();}
        World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    UDWWeaponAnimationComponent* W()const{return P?P->WeaponAnimationComponent.Get():nullptr;}
    bool Ready()const{return World&&P&&PC&&W()&&P->HasCombatVitalsReady()&&PC->GetPawn()==P&&PC->IsLocalController()&&Error.IsEmpty()&&!GI->HasActiveSlot();}
    // Native unit fixture advancement of the ONE production weapon clock. No render-rate or fake source clock.
    bool Advance(double Dt){World->TimeSeconds+=static_cast<float>(Dt);return W()->AdvanceAnimation(Dt);}
    bool Select(EDWAnimatedWeapon Weapon)
    {W()->CancelAction();if(!W()->SelectWeapon(Weapon))return false;Advance(W()->GetRemainingAttackLockSeconds()+.001);return W()->IsEnabled();}
    bool Press(){const bool Accepted=W()->PrimaryPressed(P->GetActorLocation()+FVector(1000,0,0));if(Accepted)W()->PublishAcceptedWeaponStartEffects();return Accepted;}
    double Length()const{return W()->GetActionDuration();}
};

bool Run(FAutomationTestBase& Test,double Scale)
{
    FFixture F(Scale);if(!Test.TestTrue(TEXT("Strict isolated timing fixture and real controller ready"),F.Ready())){Test.AddError(F.Error);return false;}
    auto CheckSource=[&](const FString& Label,double Expected)
    {
        const double Actual=F.W()->GetPresentationSourceSeconds();
        Test.TestTrue(FString::Printf(TEXT("%s source time (actual=%.9f expected=%.9f role=%s busy=%d)"),*Label,Actual,Expected,*F.W()->GetActiveRole().ToString(),F.W()->IsBusy()),FMath::IsNearlyEqual(Actual,Expected,2.e-5));
        Test.TestTrue(Label+TEXT(" AnimBP getter shares source time"),FMath::IsNearlyEqual(static_cast<double>(F.P->GetMeleeAnimationElapsedSeconds()),Actual,2.e-5));
        if(const auto* Sequence=F.W()->GetActiveSequence())
            Test.TestTrue(Label+TEXT(" actual sequence position maps the same full source"),FMath::IsNearlyEqual(static_cast<double>(F.W()->GetPositionSeconds()),Sequence->GetPlayLength()*FMath::Clamp(Actual/F.Length(),0.,1.),2.e-5));
    };
    const auto& Rules=*F.GI->GetCombatRules();FString Error;
    for(const auto Kind:{EDWAnimatedWeapon::Spear,EDWAnimatedWeapon::ShieldAxe})
    {
        if(!Test.TestTrue(TEXT("Select melee with real source"),F.Select(Kind)&&F.Press()))return false;
        FDWSpearTuning T;
        if(Kind==EDWAnimatedWeapon::Spear){if(!FDWSpearTuning::Build(Rules,T,Error))return false;}
        else{FDWShieldAxeTuning Axe;if(!FDWShieldAxeTuning::Build(Rules,Axe,Error))return false;T=Axe.Attack;}
        const auto Ends=F.W()->AttackSourcePhaseEnds.FindChecked(Kind);const double L=F.Length();
        const int32 EventsBefore=F.W()->DispatchedEventCount;
        F.Advance(T.WindupSeconds*.5);CheckSource(TEXT("Changed melee half windup"),Ends.X*L*.5);
        F.Advance(T.WindupSeconds*.5);CheckSource(TEXT("Changed melee windup endpoint"),Ends.X*L);
        if(Kind==EDWAnimatedWeapon::Spear)
        {
            Test.TestEqual(TEXT("R1 contact cannot fire early because gameplay duration became longer"),F.W()->DispatchedEventCount,EventsBefore);
            const double ToContact=(7./24./L-Ends.X)/(Ends.Y-Ends.X)*T.ActiveSeconds;
            Test.TestTrue(TEXT("Shared boundary maps source contact back to the current gameplay phase"),FMath::IsNearlyEqual(F.W()->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds()),ToContact,2.e-5));
        }
        F.Advance(T.ActiveSeconds);CheckSource(TEXT("Changed melee active endpoint"),Ends.Y*L);
        if(Kind==EDWAnimatedWeapon::Spear)Test.TestEqual(TEXT("R1 source contact is dispatched once inside the retimed active phase"),F.W()->DispatchedEventCount,EventsBefore+1);
        F.Advance(T.RecoverySeconds+.000001);CheckSource(TEXT("Changed melee full source endpoint"),L);
    }
    if(!Test.TestTrue(TEXT("Select first complete dagger group"),F.Select(EDWAnimatedWeapon::DualDaggers)&&F.Press()))return false;
    FDWDaggerTuning Dagger;if(!FDWDaggerTuning::Build(Rules,Dagger,Error))return false;
    F.Advance(Dagger.SegmentDurationSeconds[0]*.5);CheckSource(TEXT("Changed dagger complete-group midpoint"),F.W()->DaggerSourceSegments[0].Y*.5);
    F.Advance(Dagger.SegmentDurationSeconds[0]*.5+.000001);CheckSource(TEXT("Changed dagger stops after G1"),F.W()->DaggerSourceSegments[0].Y);
    FDWSwordTuning Sword;if(!FDWSwordTuning::Build(Rules,Sword,Error)||!F.Select(EDWAnimatedWeapon::Greatsword)||!F.Press())return false;
    const double SwordL=F.Length();const auto SwordEnds=F.W()->SwordSourcePhaseEnds;
    F.Advance(Sword.ChargeSeconds*.5);CheckSource(TEXT("Changed sword charge midpoint"),SwordEnds[0]*SwordL*.5);
    F.Advance(Sword.ChargeSeconds*.5);CheckSource(TEXT("Changed sword ready begins authored hold"),0.);
    Test.TestEqual(TEXT("Ready uses the existing authored Hold clip"),F.W()->GetPresentationClip().Role,FName(TEXT("ChargeHold")));
    F.Advance(F.W()->SwordReadyLoopSeconds*.25);CheckSource(TEXT("Ready loops a complete Hold independent of gameplay charge speed"),F.W()->GetActionDuration()*.25);
    F.W()->PrimaryReleased();F.Advance(0.);
    F.Advance(Sword.SlamSeconds);CheckSource(TEXT("Changed sword whole slam"),SwordEnds[1]*SwordL);
    F.Advance(Sword.GapSeconds);CheckSource(TEXT("Changed sword whole gap"),SwordEnds[2]*SwordL);
    F.Advance(Sword.SweepSeconds);CheckSource(TEXT("Changed sword whole sweep"),SwordEnds[3]*SwordL);
    F.Advance(Sword.RecoverySeconds+.000001);CheckSource(TEXT("Changed sword whole recovery"),SwordL);
    FDWSporeGunConfig Gun;if(!FDWSporeGunConfig::Build(Rules,Gun,Error)||!F.Select(EDWAnimatedWeapon::SporeGun)||!F.Press())return false;
    const double GunL=F.Length(),GunRelease=F.W()->AttackSourcePhaseEnds.FindChecked(EDWAnimatedWeapon::SporeGun).X*GunL;
    const int32 GunEventsBefore=F.W()->DispatchedEventCount;
    F.Advance(Gun.WindupSeconds*.5);CheckSource(TEXT("Changed spore windup mapping"),GunRelease*.5);
    Test.TestEqual(TEXT("Zero-source release cue cannot fire halfway through the gameplay windup"),F.W()->DispatchedEventCount,GunEventsBefore);
    F.Advance(Gun.WindupSeconds*.5);CheckSource(TEXT("Changed spore release endpoint"),GunRelease);
    F.Advance(Gun.RecoverySeconds);CheckSource(TEXT("Changed spore complete fire source"),GunL);F.W()->PrimaryReleased();
    FDWShotgunConfig Shotgun;if(!FDWShotgunConfig::Build(Rules,Shotgun,Error)||!F.Select(EDWAnimatedWeapon::Shotgun)||!F.Press())return false;
    const double ShotgunL=F.Length(),ShotgunRelease=F.W()->AttackSourcePhaseEnds.FindChecked(EDWAnimatedWeapon::Shotgun).X*ShotgunL;
    F.Advance(Shotgun.Windup*.5);CheckSource(TEXT("Changed shotgun half windup"),ShotgunRelease*.5);
    F.Advance(Shotgun.Windup*.5);CheckSource(TEXT("Changed shotgun source release"),ShotgunRelease);
    F.Advance(Shotgun.Recovery+.000001);CheckSource(TEXT("Changed shotgun completes its full authored fire"),ShotgunL);
    if(!Test.TestTrue(TEXT("Parameterized reload starts despite source having its original duration"),F.W()->RequestReload()))return false;
    double Reload=0.;if(!Rules.TryGetNumber(TEXT("weapon.shotgun.reload_one_s"),EDWCombatRuleUnit::Seconds,Reload,Error))return false;
    const double ReloadL=F.Length();F.Advance(Reload*.5);CheckSource(TEXT("Changed reload complete-source midpoint"),ReloadL*.5);
    F.Advance(Reload*.5+.000001);CheckSource(TEXT("Changed reload complete-source endpoint"),ReloadL);
    FDWBowConfig Bow;if(!FDWBowConfig::Build(Rules,Bow,Error)||!F.Select(EDWAnimatedWeapon::MoldBow)||!F.Press())return false;
    const double BowL=F.Length(),BowRelease=F.W()->BowSourceReleaseSeconds;
    F.Advance(Bow.WindupSeconds*.5);CheckSource(TEXT("Changed bow full draw midpoint"),BowRelease*.5);
    F.Advance(Bow.WindupSeconds*.5);CheckSource(TEXT("Changed bow source arrow release"),BowRelease);
    F.Advance(Bow.RecoverySeconds);CheckSource(TEXT("Changed bow complete release and recovery"),BowL);F.W()->PrimaryReleased();
    FDWBottleConfig Bottle;if(!FDWBottleConfig::Build(Rules,Bottle,Error)||!F.Select(EDWAnimatedWeapon::Alcohol)||!F.Press())return false;
    const double BottleL=F.Length(),BottleRelease=F.W()->AttackSourcePhaseEnds.FindChecked(EDWAnimatedWeapon::Alcohol).X*BottleL;
    const int32 BottleStock=F.P->GetInventory()->CountItem(TEXT("Alcohol"));
    F.Advance(Bottle.WindupSeconds*.5);CheckSource(TEXT("Changed bottle throw midpoint"),BottleRelease*.5);
    F.Advance(Bottle.WindupSeconds*.5);
    Test.TestEqual(TEXT("Legal floor admits one real paid bottle release"),F.P->GetInventory()->CountItem(TEXT("Alcohol")),BottleStock-Bottle.StockPerThrow);
    CheckSource(TEXT("Changed bottle source release"),BottleRelease);
    F.Advance(Bottle.RecoverySeconds);CheckSource(TEXT("Changed bottle complete source recovery"),BottleL);F.W()->PrimaryReleased();
    Test.TestEqual(TEXT("Presentation tests do not change source player's health"),F.P->GetHealth(),F.InitialHealth);
    Test.TestFalse(TEXT("Timing fixture never owns a production adventure slot"),F.GI->HasActiveSlot());return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPresentationHalfDurationTest,"DoughWorld.Combat.PresentationTiming.AllWeaponsHalfDurationCompletesFullSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPresentationHalfDurationTest::RunTest(const FString&){return DWPresentationTimingTests::Run(*this,.5);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPresentationLongDurationTest,"DoughWorld.Combat.PresentationTiming.AllWeaponsLongDurationCompletesFullSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPresentationLongDurationTest::RunTest(const FString&){return DWPresentationTimingTests::Run(*this,1.5);}
#endif
