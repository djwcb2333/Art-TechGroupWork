#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "DWWeaponAnimationComponent.h"
#include "DWCombatResourceVerificationLibrary.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatEquipmentTestFixture.h"
#include "DWCombatPlayerSave.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWSaveGame.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DWR1BindingTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI{NewObject<UDWGameInstance>(GEngine)};
    UWorld* World=nullptr;
    ADWPlayerCharacter* Player=nullptr;
    UDWWeaponAnimationComponent* Weapon=nullptr;
    UDWWeaponAnimationSet* Set=nullptr;
    bool bReady=false;
    explicit FFixture(double Contact=7./24.,double SourceDuration=1.)
    {
        const FString Previous=FCommandLine::Get();
        const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/D12R1")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
        const FString Command=FString::Printf(TEXT("-DWVerificationSavePrefix=DW_QA_D12_R1_ -DWSaveTestDirectory=\"%s\" %s"),*Directory,*Previous);
        FCommandLine::Set(*Command);
        GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWR1Binding")),GetTransientPackage());
        FCommandLine::Set(*Previous);
        World=GI->GetWorld();if(!World||!GI->GetCombatRules())return;
        FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Player=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Params);
        if(!Player||Player->HasActorBegunPlay())return; // Configuration must precede the real component BeginPlay.
        Weapon=Player->WeaponAnimationComponent.Get();if(!Weapon)return;
        Set=NewObject<UDWWeaponAnimationSet>(Player);Set->Weapon=EDWAnimatedWeapon::Spear;Set->bRepeatWhileHeld=true;
        auto* Sequence=NewObject<UAnimSequence>(Set);
        // UE5.8's sequencer model requires a real skeleton even for a duration-only source.
        // Duplicate the existing skeleton into this transient fixture; never mutate/save the asset.
        auto* SourceSkeleton=LoadObject<USkeleton>(nullptr,TEXT("/Game/DoughWorld/Characters/Hero/Meshes/SK_DWHeroSpear_Skeleton.SK_DWHeroSpear_Skeleton"));
        if(!SourceSkeleton)return;
        auto* TestSkeleton=DuplicateObject<USkeleton>(SourceSkeleton,Set);
        if(!TestSkeleton)return;
        TestSkeleton->SetFlags(RF_Transient);Sequence->SetSkeleton(TestSkeleton);
        auto& Controller=Sequence->GetController();Controller.InitializeModel();
        Controller.SetFrameRate(FFrameRate(24,1),false);Controller.SetNumberOfFrames(FFrameNumber(24),false);
        FDWWeaponAnimationClip Clip;Clip.Role=TEXT("Attack");Clip.Animation=Sequence;Clip.DurationSeconds=static_cast<float>(SourceDuration);
        FDWWeaponDamageEvent Marker;Marker.TimeSeconds=static_cast<float>(Contact);Clip.DamageEvents.Add(Marker);Set->Clips.Add(Clip);
        Weapon->AnimationSets={Set};Weapon->bEnableFormalSpearExplicitBinding=true;
        Weapon->ApprovedFormalSpearSequence=Sequence;Weapon->ApprovedFormalSpearSourceDurationSeconds=SourceDuration;
        Weapon->ApprovedFormalSpearContactBeginSeconds=Contact;Weapon->ApprovedFormalSpearContactEndSeconds=Contact;
        Player->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
        World->BeginPlay();if(!Player->HasActorBegunPlay())Player->DispatchBeginPlay();
        bReady=Player->HasCombatVitalsReady()&&!GI->HasActiveSlot()&&GI->GetSaveDirectory()==Directory;
    }
    ~FFixture()
    {
        if(!World)return;
        World->EndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    FVector Target() const {return Player->GetActorLocation()+FVector(200,0,0);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWR1ExplicitBeginPlayTest,"DoughWorld.Combat.Spear.R1.ExplicitBeginPlayAndOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWR1ExplicitBeginPlayTest::RunTest(const FString& Parameters)
{
    DWR1BindingTests::FFixture F;if(!TestTrue(TEXT("Isolated real BeginPlay and rules ready"),F.bReady))return false;
    TestTrue(TEXT("One second synthetic source initialized from explicit component binding"),F.Weapon->IsFormalSpearConfigured());
    TestTrue(TEXT("Successful binding clears diagnostic"),F.Weapon->FormalSpearBindingError.IsEmpty());
    TestEqual(TEXT("Real source duration preserved"),F.Weapon->ApprovedFormalSpearSequence->GetPlayLength(),1.f);
    TestTrue(TEXT("R1 contact is 7/24 and not the .25 burst marker"),FMath::IsNearlyEqual(F.Weapon->ApprovedFormalSpearContactBeginSeconds,7./24.,1.e-12)&&F.Weapon->ApprovedFormalSpearContactBeginSeconds!=.25);
    TestTrue(TEXT("Initialization grants no permanent weapon"),F.Weapon->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty());
    TestFalse(TEXT("Unowned explicit spear cannot be selected"),F.Weapon->SelectWeapon(EDWAnimatedWeapon::Spear));
    TestTrue(TEXT("Default empty-hand identity is not a spear attack request"),F.Weapon->GetCurrentEquipmentId().IsNone()&&F.Weapon->SelectedWeapon==EDWAnimatedWeapon::Unarmed);
    TestFalse(TEXT("Unowned binding never starts a formal spear action"),F.Weapon->GetFormalSpearPhase()==EDWSpearPhase::Windup);
    TestFalse(TEXT("PlayRole cannot bypass the explicit gate"),F.Weapon->PlayRole(TEXT("Attack"),F.Target()));
    auto* Save=NewObject<UDWSaveGame>();FString Error;
    if(!TestTrue(TEXT("Strict isolated resource candidate"),DWCombatPlayerSave::MakeNew(*F.GI->GetCombatRules(),*Save,Error)))return false;
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Spear);
    if(!TestNotNull(TEXT("Registered permanent spear"),Binding))return false;
    Save->CombatPlayerResources.OwnedPermanentWeaponIds={Binding->ItemId};Save->CombatPlayerResources.CurrentEquipmentId=Binding->ItemId;
    if(!TestTrue(TEXT("Ownership uses real player restore"),F.Player->TryApplySaveData(Save)))return false;
    if(!TestTrue(TEXT("Owned explicit spear enters formal request"),F.Weapon->PrimaryPressed(F.Target())))return false;
    TestTrue(TEXT("Starts formal windup"),F.Weapon->GetFormalSpearPhase()==EDWSpearPhase::Windup);
    F.Weapon->AdvanceAnimation(.25);
    TestEqual(TEXT("Burst visual time does not dispatch the contact marker"),F.Weapon->DispatchedEventCount,0);
    F.Weapon->AdvanceAnimation(.05);
    TestEqual(TEXT("Reviewed contact dispatches once"),F.Weapon->DispatchedEventCount,1);
    F.Weapon->AdvanceAnimation(2.);
    TestFalse(TEXT("Action completes without legacy held-repeat despite old DA repeat flag"),F.Weapon->IsBusy());
    TestEqual(TEXT("No synthetic replay of source contact"),F.Weapon->DispatchedEventCount,1);
    TestEqual(TEXT("Formal phase keeps full source duration"),F.Weapon->GetActionDuration(),1.f);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWR1FailClosedTest,"DoughWorld.Combat.Spear.R1.InvalidBindingKeepsInputClaimed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWR1FailClosedTest::RunTest(const FString& Parameters)
{
    AddExpectedError(TEXT("Explicit formal spear binding rejected:"),EAutomationExpectedErrorFlags::Contains,1);
    DWR1BindingTests::FFixture F(1.1);if(!TestTrue(TEXT("Isolated player ready despite rejected art"),F.bReady))return false;
    TestFalse(TEXT("Contact beyond the actual source cannot qualify an approved binding"),F.Weapon->IsFormalSpearConfigured());
    TestTrue(TEXT("Rejected binding is diagnosable"),F.Weapon->FormalSpearBindingError.Contains(TEXT("source")));
    FString EquipmentError;
    if(!TestTrue(TEXT("Owned/current invalid-binding fixture"),DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*F.Weapon,*F.GI->GetCombatRules(),EDWAnimatedWeapon::Spear,EquipmentError)))return false;
    TestTrue(TEXT("Controller still routes input to the rejecting weapon"),F.Weapon->IsEnabled());
    TestFalse(TEXT("No legacy basic attack fallback"),F.Weapon->PrimaryPressed(F.Target()));
    TestFalse(TEXT("No PlayRole bypass"),F.Weapon->PlayRole(TEXT("Attack"),F.Target()));
    TestTrue(TEXT("Owned selection keeps identity without approving invalid source"),F.Weapon->SelectWeapon(EDWAnimatedWeapon::Spear));
    TestFalse(TEXT("Selection cannot qualify rejected formal attack"),F.Weapon->PrimaryPressed(F.Target()));
    F.Weapon->bEnabled=false;
    TestFalse(TEXT("Global disable is reflected by the D04 component contract"),F.Weapon->IsEnabled());
    TestFalse(TEXT("Player formal routing cannot fall back to old melee when disabled"),F.Player->PerformMeleeAttack(F.Target()));
    TestFalse(TEXT("Disabled weapon still cannot attack"),F.Weapon->PrimaryPressed(F.Target()));
    F.Weapon->AdvanceAnimation(1.);
    F.Weapon->TickComponent(1.f/60.f,LEVELTICK_All,nullptr); // Real UpdateVisuals path with disabled explicit routing.
    TestFalse(TEXT("Disabled clock and visual tick stay inactive"),F.Weapon->IsBusy());
    F.Weapon->AnimationSets.Reset();
    F.Weapon->bEnabled=true;
    TestTrue(TEXT("Missing clip still cannot expose legacy controller route"),F.Weapon->IsEnabled());
    TestFalse(TEXT("Missing set is safely rejected"),F.Weapon->PrimaryPressed(F.Target()));
    F.Weapon->AdvanceAnimation(2.);
    F.Weapon->TickComponent(1.f/60.f,LEVELTICK_All,nullptr); // Input claimed != a valid Set; must never dereference null.
    TestFalse(TEXT("Missing set has no scheduled gameplay event"),FMath::IsFinite(F.Weapon->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds())));
    TestFalse(TEXT("No pending/repeated action"),F.Weapon->IsBusy());
    TestEqual(TEXT("No legacy contact dispatched"),F.Weapon->DispatchedEventCount,0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWR1SourceAndFixtureGateTest,"DoughWorld.Combat.Spear.R1.SourceLengthAndVerificationGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWR1SourceAndFixtureGateTest::RunTest(const FString& Parameters)
{
    AddExpectedError(TEXT("Explicit formal spear binding rejected:"),EAutomationExpectedErrorFlags::Contains,2);
    {
        DWR1BindingTests::FFixture F(7./24.,.8);if(!TestTrue(TEXT("Duration mismatch fixture ready"),F.bReady))return false;
        TestFalse(TEXT("One second source cannot be silently cropped to .8"),F.Weapon->IsFormalSpearConfigured());
        TestTrue(TEXT("Diagnostic identifies the source length"),F.Weapon->FormalSpearBindingError.Contains(TEXT("source duration")));
    }
    {
        DWR1BindingTests::FFixture F(7./24.,std::numeric_limits<double>::quiet_NaN());if(!TestTrue(TEXT("Nonfinite fixture ready"),F.bReady))return false;
        TestFalse(TEXT("Nonfinite source duration never initializes"),F.Weapon->IsFormalSpearConfigured());
        FString EquipmentError;
        if(!TestTrue(TEXT("Owned/current nonfinite-binding fixture"),DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*F.Weapon,*F.GI->GetCombatRules(),EDWAnimatedWeapon::Spear,EquipmentError)))return false;
        TestFalse(TEXT("Nonfinite binding cannot reach legacy dispatch"),F.Weapon->PrimaryPressed(F.Target()));
    }
    DWR1BindingTests::FFixture F;if(!TestTrue(TEXT("Valid non-PIE fixture ready"),F.bReady))return false;
    const int64 Revision=F.Weapon->GetCombatWeaponResourceRevision();
    const FString Result=UDWCombatResourceVerificationLibrary::ApplySpearVerificationFixture(F.Player);
    TSharedPtr<FJsonObject> Json;auto Reader=TJsonReaderFactory<>::Create(Result);
    if(!TestTrue(TEXT("QA rejection is valid JSON"),FJsonSerializer::Deserialize(Reader,Json)&&Json.IsValid()))return false;
    TestFalse(TEXT("Non-PIE standalone world cannot use the controlled grant"),Json->GetBoolField(TEXT("ok")));
    TestEqual(TEXT("Rejected QA call cannot commit weapon resources"),F.Weapon->GetCombatWeaponResourceRevision(),Revision);
    TestTrue(TEXT("Rejected QA call cannot grant ownership"),F.Weapon->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty());
    TestFalse(TEXT("QA helper did not create an active save slot"),F.GI->HasActiveSlot());return true;
}
#endif
