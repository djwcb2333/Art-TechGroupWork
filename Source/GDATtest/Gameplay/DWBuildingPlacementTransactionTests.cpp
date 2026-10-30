#if WITH_DEV_AUTOMATION_TESTS
#include "DWBuildingPlacementComponent.h"
#include "DWBuildingDefinition.h"
#include "DWBuildingEffectsComponent.h"
#include "DWPlaceableBuilding.h"
#include "DWGatheringPond.h"
#include "DWGameplayConfig.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatTestWorld.h"
#include "DWSaveGame.h"
#include "DWWeaponAnimationComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

// Test-only native access to the fact transaction. The public confirmation method still
// requires real viewport focus and a freshly armed mouse press in every build.
struct FDWBuildingPlacementAutomationAccess
{
    static bool Arm(UDWBuildingPlacementComponent& C,ADWPlayerCharacter& Player,
        UDWBuildingDefinition* Definition,const FTransform& Transform)
    {
        C.EndPlacement(false);
        auto* PC=Cast<ADWPlayerController>(Player.GetController());
        auto* Bag=Player.GetInventory();auto* Authority=UDWCombatAuthoritySubsystem::Get(&Player);
        if(!PC||!Bag||!Authority)return false;
        FDWInventorySlotSnapshot Slot;
        for(int32 I=0;I<Bag->GetSlots().Num();++I)
            if(Bag->GetSlots()[I].ItemId==Definition->BuildingItemId){if(!Bag->CaptureSlotSnapshot(I,Slot))return false;break;}
        if(!Bag->IsSlotSnapshotCurrent(Slot))return false;
        const auto Life=Authority->CaptureActorLife(&Player);if(!Life.Entity||!Life.Life)return false;
        C.ActiveDefinition=Definition;C.SourceSlot=Slot;C.PreviewTransform=Transform;
        C.SessionPlayer=&Player;C.SessionController=PC;C.SessionInventory=Bag;C.SessionConfig=Player.GetGameplayConfig();
        C.SessionEpoch=Authority->GetWorldEpoch();C.SessionEntity=Life.Entity;C.SessionLife=Life.Life;
        C.bPlacing=true;C.bPlacementValid=true;C.bMouseArmed=false;
        return C.IsSessionCurrent();
    }
    static bool Commit(UDWBuildingPlacementComponent& C){return C.CommitPlacementTransaction();}
    static bool SaveRejected(UDWGameInstance& GI){return GI.RejectGatheringPreviewPersistence();}
    static double Rotate(UDWBuildingPlacementComponent& C,bool QHeld,bool QPressed,bool EHeld,bool EPressed,double Now)
    {C.UpdateRotationInput(QHeld,QPressed,EHeld,EPressed,Now);return C.PlacementYaw;}
    static bool ConfirmInput(UDWBuildingPlacementComponent& C,bool Held,bool Pressed)
    {return C.UpdateConfirmationInput(Held,Pressed);}
    static void LoseInputFocus(UDWBuildingPlacementComponent& C){C.ResetPlacementInput();}
};

namespace DWBuildingTransactions
{
const FName ItemId(TEXT("QA_PlacementBuilding"));
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWGameplayConfig> Config;
    TStrongObjectPtr<UDWBuildingDefinition> Definition;
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;ADWPlayerController* PC=nullptr;
    bool Ready=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("DWBuildingTransactionWorld"));if(!World)return;
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        // Actual collision geometry, not an override of ground validity.
        auto* Floor=World->SpawnActor<AActor>(AActor::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(!Floor)return;
        auto* Surface=NewObject<UBoxComponent>(Floor);Floor->AddInstanceComponent(Surface);Floor->SetRootComponent(Surface);
        Surface->SetBoxExtent(FVector(500.,500.,10.));Surface->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Surface->SetCollisionObjectType(ECC_WorldStatic);Surface->SetCollisionResponseToAllChannels(ECR_Block);
        Surface->SetCanEverAffectNavigation(false);Surface->RegisterComponent();Floor->SetActorLocation(FVector(0,0,-10));
        Player=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,100),FRotator::ZeroRotator,Params);
        if(!Player)return;if(!Player->HasActorBegunPlay())Player->DispatchBeginPlay();
        PC=World->SpawnActor<ADWPlayerController>(ADWPlayerController::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(!PC)return;if(!PC->HasActorBegunPlay())PC->DispatchBeginPlay();PC->Possess(Player);
        PC->SetActorTickEnabled(false);Player->SetActorTickEnabled(false);
        if(Player->WeaponAnimationComponent)Player->WeaponAnimationComponent->bEnabled=false;
        if(!Player->HasCombatVitalsReady()||!Player->GetGameplayConfig()||!Component())return;
        Config.Reset(DuplicateObject<UDWGameplayConfig>(Player->GetGameplayConfig(),Player));
        Config->ItemTable=nullptr;Config->RecipeTable=nullptr;Config->MaxInventorySlots=24;
        Definition.Reset(NewObject<UDWBuildingDefinition>(Player));
        Definition->BuildingItemId=ItemId;Definition->StationId=TEXT("QA_PlacementStation");
        Definition->ActorClass=ADWPlaceableBuilding::StaticClass();Definition->FootprintHalfExtent=FVector(50,50,50);
        FDWBuildingMeshPart Part;Part.PartName=TEXT("FixtureCube");
        Part.Mesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
        Part.RelativeTransform=FTransform(FVector(0,0,50));Definition->Parts.Add(Part);
        Config->BuildingDefinitions.Add(Definition.Get());
        FDWItemDefinition Item;Item.ItemId=ItemId;Item.bUsable=true;Config->Items.Add(Item);
        if(!Bag()->SetSlots({}))return;
        Player->GameplayConfig=Config.Get();Bag()->InitializeInventory(Config.Get());
        Ready=Bag()->TryAddItem(ItemId,2)&&Bag()->CountItem(ItemId)==2&&!PC->IsGameplayBlocked();
    }
    ~FFixture()
    {
        if(!World)return;
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        if(GI.IsValid())GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    UDWInventoryComponent* Bag()const{return Player?Player->GetInventory():nullptr;}
    UDWBuildingPlacementComponent* Component()const{return Player?Player->FindComponentByClass<UDWBuildingPlacementComponent>():nullptr;}
    bool Arm(FVector Position=FVector(300,0,0),double Yaw=0.)
    {return FDWBuildingPlacementAutomationAccess::Arm(*Component(),*Player,Definition.Get(),FTransform(FRotator(0,Yaw,0),Position));}
    int32 Buildings()const
    {
        int32 N=0;for(TActorIterator<ADWPlaceableBuilding> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&!It->IsStaged())++N;return N;
    }
    ADWPlaceableBuilding* FirstBuilding()const
    {for(TActorIterator<ADWPlaceableBuilding> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&!It->IsStaged())return *It;return nullptr;}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingPlacementAtomicCommit,
    "DoughWorld.Buildings.Transaction.SuccessOneItemOneActor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingPlacementAtomicCommit::RunTest(const FString&)
{
    using namespace DWBuildingTransactions;FFixture F;if(!TestTrue(TEXT("Real isolated player/inventory/ground fixture"),F.Ready))return false;
    TestFalse(TEXT("Unified configuration alone does not block formal save admission"),FDWBuildingPlacementAutomationAccess::SaveRejected(*F.GI));
    TStrongObjectPtr<UDWSaveGame> BeforePlacement(NewObject<UDWSaveGame>());
    F.Player->CaptureSaveData(BeforePlacement.Get());
    TestTrue(TEXT("Old world capture remains available"),BeforePlacement->CombatPlayerResources.SubblockSchema>0);
    if(!TestTrue(TEXT("Arm current source slot"),F.Arm()))return false;
    TestFalse(TEXT("Public confirmation retains mouse/focus guard in headless test"),F.Component()->ConfirmPlacement());
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    int32 Notices=0;bool Coherent=true,Reentrant=false;
    Probe->TestHook=[&](){++Notices;Coherent&=F.Bag()->CountItem(ItemId)==1&&F.Buildings()==1;
        Reentrant|=FDWBuildingPlacementAutomationAccess::Commit(*F.Component());};
    F.Bag()->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Real placement transaction commits"),FDWBuildingPlacementAutomationAccess::Commit(*F.Component()));
    TestEqual(TEXT("Exactly one of two buildings consumed"),F.Bag()->CountItem(ItemId),1);
    TestEqual(TEXT("Exactly one independent building actor published"),F.Buildings(),1);
    TestEqual(TEXT("Exactly one inventory notice"),Notices,1);
    TestTrue(TEXT("Notice observes inventory and world facts together"),Coherent);
    TestFalse(TEXT("Reentry cannot consume another item"),Reentrant);
    TestFalse(TEXT("Repeated confirmation cannot duplicate actor"),FDWBuildingPlacementAutomationAccess::Commit(*F.Component()));
    if(auto* Building=F.FirstBuilding())
    {
        TestTrue(TEXT("New station is interactive"),Building->IsInteractionAvailable());
        TestEqual(TEXT("Existing construction effect plays exactly once"),Building->BuildingEffects->ConstructionPlayCount,1);
        TestTrue(TEXT("Unsupported placed-world state blocks saving before any slot write"),FDWBuildingPlacementAutomationAccess::SaveRejected(*F.GI));
        TestFalse(TEXT("Public native apply cannot refund consumed building while its actor survives"),F.Player->TryApplySaveData(BeforePlacement.Get()));
        F.Player->ApplySaveData(BeforePlacement.Get());
        TestEqual(TEXT("Blueprint apply wrapper also preserves consumed stock"),F.Bag()->CountItem(ItemId),1);
        TestEqual(TEXT("Rejected apply leaves the single world building intact"),F.Buildings(),1);
        TStrongObjectPtr<UDWSaveGame> CaptureTarget(NewObject<UDWSaveGame>());
        CaptureTarget->Health=123.f;CaptureTarget->Inventory={FDWItemStack(ItemId,7)};
        F.Player->CaptureSaveData(CaptureTarget.Get());
        TestEqual(TEXT("Rejected capture leaves existing target health unchanged"),CaptureTarget->Health,123.f);
        TestEqual(TEXT("Rejected capture leaves existing target inventory unchanged"),CaptureTarget->Inventory[0].Quantity,7);
        TestEqual(TEXT("Rejected capture never creates a partial combat subblock"),CaptureTarget->CombatPlayerResources.SubblockSchema,0);
        Building->Destroy();
        TestTrue(TEXT("Guard remains after actor destruction, preventing forgotten state"),FDWBuildingPlacementAutomationAccess::SaveRejected(*F.GI));
    }
    F.Bag()->OnChanged.RemoveDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingPlacementFailureAtomicity,
    "DoughWorld.Buildings.Transaction.CancelInvalidGroundAndStaleSlot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingPlacementFailureAtomicity::RunTest(const FString&)
{
    using namespace DWBuildingTransactions;FFixture F;if(!TestTrue(TEXT("Isolated fixture"),F.Ready))return false;
    TestTrue(TEXT("Arm cancellation"),F.Arm());F.Component()->CancelPlacement();
    TestFalse(TEXT("Cancelled ticket cannot commit"),FDWBuildingPlacementAutomationAccess::Commit(*F.Component()));
    TestEqual(TEXT("Cancel preserves both items"),F.Bag()->CountItem(ItemId),2);
    TestTrue(TEXT("Arm unsupported canyon edge"),F.Arm(FVector(480,0,0)));
    TestFalse(TEXT("Nine real support traces reject an overhanging footprint"),FDWBuildingPlacementAutomationAccess::Commit(*F.Component()));
    TestEqual(TEXT("Invalid ground has no cost"),F.Bag()->CountItem(ItemId),2);
    TestTrue(TEXT("Arm footprint around player"),F.Arm(FVector::ZeroVector));
    TestFalse(TEXT("Occupancy still checks player collision"),FDWBuildingPlacementAutomationAccess::Commit(*F.Component()));
    TestTrue(TEXT("Arm source before external inventory edit"),F.Arm());
    TestTrue(TEXT("Independent inventory edit advances revision"),F.Bag()->TryAddItem(TEXT("Flour"),1));
    TestFalse(TEXT("Stale slot cannot pay for a building"),FDWBuildingPlacementAutomationAccess::Commit(*F.Component()));
    TestEqual(TEXT("Stale slot preserves building stock"),F.Bag()->CountItem(ItemId),2);
    TestTrue(TEXT("Arm real staged-spawn failure"),F.Arm());
    int32 StagedSpawns=0;
    const FDelegateHandle SpawnHook=F.World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
    {
        if(auto* Candidate=Cast<ADWPlaceableBuilding>(Actor))
        {++StagedSpawns;Candidate->SetActorLocation(Candidate->GetActorLocation()+FVector(0,0,30));}
    }));
    TestFalse(TEXT("Construction callback changing candidate pose rejects post-spawn commit"),FDWBuildingPlacementAutomationAccess::Commit(*F.Component()));
    F.World->RemoveOnActorSpawnedHandler(SpawnHook);
    TestEqual(TEXT("Failure exercised an actual deferred building spawn"),StagedSpawns,1);
    TestEqual(TEXT("Failed staged building preserves both items"),F.Bag()->CountItem(ItemId),2);
    TestEqual(TEXT("All failures leave no committed actors"),F.Buildings(),0);
    TestFalse(TEXT("Cancelled/failed previews do not contaminate world save admission"),FDWBuildingPlacementAutomationAccess::SaveRejected(*F.GI));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingPlacementGeometry,
    "DoughWorld.Buildings.Placement.RotatedFootprintAndPond",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingPlacementGeometry::RunTest(const FString&)
{
    using namespace DWBuildingTransactions;FFixture F;if(!TestTrue(TEXT("Isolated geometry fixture"),F.Ready))return false;
    F.Definition->FootprintHalfExtent=FVector(100,20,50);
    const auto Valid=[&](FVector Position,double Yaw){return UDWBuildingPlacementComponent::ValidateGroundTransform(
        F.World,F.Player,*F.Definition,FTransform(FRotator(0,Yaw,0),Position));};
    TestFalse(TEXT("Long X footprint crosses floor edge"),Valid(FVector(440,0,0),0.));
    TestTrue(TEXT("90 degree rotation fits the same ground point"),Valid(FVector(440,0,0),90.));
    auto* Pond=F.World->SpawnActor<ADWGatheringPond>();
    if(TestNotNull(TEXT("Actual pond surface"),Pond))
    {
        Pond->SetActorLocation(FVector(300,0,5));
        FHitResult Surface;F.World->LineTraceSingleByChannel(Surface,FVector(300,0,100),FVector(300,0,-100),ECC_Visibility);
        TestTrue(TEXT("Fixture cursor ray actually hits the pond"),Surface.GetActor()==Pond);
        TestFalse(TEXT("Visibility-blocking water is not valid building ground"),Valid(Surface.ImpactPoint,0.));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingPlacementRotationTiming,
    "DoughWorld.Buildings.Placement.RotationTapHoldAndFocusReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingPlacementRotationTiming::RunTest(const FString&)
{
    TStrongObjectPtr<UDWBuildingPlacementComponent> C(NewObject<UDWBuildingPlacementComponent>());
    using Access=FDWBuildingPlacementAutomationAccess;
    Access::Rotate(*C,false,false,false,false,0.);
    TestEqual(TEXT("Fresh E press rotates 15 degrees clockwise"),Access::Rotate(*C,false,false,true,true,0.),15.);
    TestEqual(TEXT("Hold does not repeat before initial delay"),Access::Rotate(*C,false,false,true,false,.349),15.);
    TestEqual(TEXT("Hold repeats after 0.35 seconds"),Access::Rotate(*C,false,false,true,false,.351),30.);
    TestEqual(TEXT("Second repeat waits 0.10 seconds"),Access::Rotate(*C,false,false,true,false,.450),30.);
    TestEqual(TEXT("Continuous hold takes another 15 degree step"),Access::Rotate(*C,false,false,true,false,.452),45.);
    TestEqual(TEXT("Release stops rotation"),Access::Rotate(*C,false,false,false,false,1.),45.);
    TestEqual(TEXT("Q tap immediately reverses one 15 degree step"),Access::Rotate(*C,true,true,false,false,1.01),30.);
    TestEqual(TEXT("Q uses its own fresh initial hold delay"),Access::Rotate(*C,true,false,false,false,1.359),30.);
    TestEqual(TEXT("Q hold repeats counterclockwise"),Access::Rotate(*C,true,false,false,false,1.361),15.);
    TestEqual(TEXT("Both keys together produce no rotation"),Access::Rotate(*C,true,false,true,true,1.5),15.);
    TestEqual(TEXT("Both remain neutral even after repeat timer"),Access::Rotate(*C,true,false,true,false,2.),15.);
    TestEqual(TEXT("Releasing Q resumes E by one step"),Access::Rotate(*C,false,false,true,false,2.1),30.);
    TestEqual(TEXT("Resumed E gets full first delay"),Access::Rotate(*C,false,false,true,false,2.449),30.);
    TestEqual(TEXT("Direct direction change is immediate"),Access::Rotate(*C,true,true,false,false,2.46),15.);
    Access::LoseInputFocus(*C);
    TestEqual(TEXT("Held Q cannot restart after focus/pause reset"),Access::Rotate(*C,true,false,false,false,3.),15.);
    TestEqual(TEXT("Swapping to E without releasing both is still gated"),Access::Rotate(*C,false,false,true,true,3.1),15.);
    Access::Rotate(*C,false,false,false,false,3.2);
    TestEqual(TEXT("Release then fresh press re-arms Q"),Access::Rotate(*C,true,true,false,false,3.3),0.);
    TestEqual(TEXT("Counterclockwise rotation wraps into 0 to360"),Access::Rotate(*C,true,false,false,false,3.651),345.);
    Access::Rotate(*C,false,false,false,false,4.);
    TestEqual(TEXT("A complete E tap in one input batch still rotates once"),Access::Rotate(*C,false,false,false,true,4.1),0.);
    TestFalse(TEXT("Opening Use press never confirms"),Access::ConfirmInput(*C,true,true));
    TestFalse(TEXT("Holding opening Use press never confirms"),Access::ConfirmInput(*C,true,false));
    TestFalse(TEXT("Opening release arms but never confirms"),Access::ConfirmInput(*C,false,false));
    TestTrue(TEXT("First fresh left press can confirm"),Access::ConfirmInput(*C,true,true));
    Access::LoseInputFocus(*C);
    TestFalse(TEXT("Returning to viewport with held mouse cannot confirm"),Access::ConfirmInput(*C,true,false));
    TestFalse(TEXT("First click to return focus cannot confirm"),Access::ConfirmInput(*C,true,true));
    TestFalse(TEXT("Fresh release rearms safely"),Access::ConfirmInput(*C,false,false));
    TestTrue(TEXT("Subsequent deliberate press confirms"),Access::ConfirmInput(*C,true,true));
    return true;
}
#endif
