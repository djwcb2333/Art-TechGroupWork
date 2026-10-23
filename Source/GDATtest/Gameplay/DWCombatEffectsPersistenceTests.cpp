#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatEffectsPersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatBowProjectile.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatTestWorld.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWSaveGame.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"
#include "Misc/AutomationTest.h"
namespace DWEffectsTests
{
FDWCombatEffectsSave Sample()
{
    using namespace DWCombatCommit;FLedger L;L.SetWorldEpoch(3);L.SetLife({1,1});L.SetLife({2,1});L.SetLife({3,1});
    const auto A=L.BeginAction({2,1});TArray<FEmitterId> E;
    for(uint64 I=1;I<=4;++I){auto R=L.AdmitResource({A,1,EResourceKind::WeaponUse,I});E.Add(L.CommitWeaponUseWithEmitter(R.Receipt.Handle,I).Handle);}
    auto R=L.AdmitDamage({FEmitterDamage{E[3],EEmitterDamageKind::AreaTick,7},{3,1}});L.Resolve(R.Receipt.Handle,EOutcome::Committed);L.SetLife({2,2});
    FDWCombatEffectsSave S;S.Authority.LastEntity=3;S.Authority.Ledger=L.CaptureSnapshot();
    S.Authority.Actors.Add({TEXT("player:0"),{2,2},1,EDWCombatSavedActorKind::Player,true,false});
    S.Authority.Actors.Add({TEXT("enemy:test"),{3,1},1,EDWCombatSavedActorKind::Enemy,false,false});
    FDWSporeFlightSave P;P.Owner={{2,1},{},E[0].Id};P.Shot={8.,2000.,1200.,90.,8.,0.,.2};P.Position={50,0,100};P.Direction=FVector::ForwardVector;P.TravelledCm=50.;P.LastWorldOffset=-.05;P.PendingSeconds=.025;P.bLostLock=false;P.LockedTargetLife={3,1};S.Spores.Add(P);
    FDWBowFlightSave B;B.Owner={{2,1},{},E[1].Id};B.Shot={24.,1400.,1800.,8.,150.,.2,{220.,4.,.35}};B.Position={180,0,100};B.TravelledCm=180.;B.ReleasedOffset=-.2;B.LastWorldOffset=-.1;S.Arrows.Add(B);
    FDWBottleFlightSave F;F.Owner={{2,1},{},E[2].Id};F.Shot={1000.,.7,200.,12.,220.,6.,.5,4.,.25};F.FlightStart={0,0,100};F.GroundTarget={500,0,2};F.FlightAge=.2;F.Position=DWBottle::SampleArc(F.FlightStart,F.GroundTarget+FVector(0,0,12),.7,200.,.2);F.ReleasedOffset=-.25;F.LastWorldOffset=-.025;F.PendingSeconds=.025;F.WalkableFloorZ=.7;S.Bottles.Add(F);
    S.Areas.Add({{{2,1},{},E[3].Id},FVector(500,0,2),-4.,2.,220.,.25,4.,.5,true});S.TargetPhases.Add({{3,1},-4.,.5,8});return S;
}
struct FWorld
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;UDWCombatAuthoritySubsystem* Authority=nullptr;FString Error;bool Ready=false;
    FWorld()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D22EffectsWorld"));if(!World)return;
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Player=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,100),FRotator::ZeroRotator,P);if(!Player)return;if(!Player->HasActorBegunPlay())Player->DispatchBeginPlay();
        auto* Save=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*Save,Error))return;
        const auto* Bow=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::MoldBow);if(!Bow)return;
        Save->CombatPlayerResources.OwnedPermanentWeaponIds={Bow->ItemId};Save->CombatPlayerResources.CurrentEquipmentId=Bow->ItemId;
        FDWItemStack Stack;Stack.ItemId=Bow->AmmoItemId;Stack.Quantity=8;Save->Inventory.Add(Stack);
        Save->PlayerTransform=FTransform(FRotator::ZeroRotator,FVector(0,0,100));Save->bHasPlayerTransform=true;
        if(!Player->TryApplySaveData(Save))return;Authority=UDWCombatAuthoritySubsystem::Get(Player);Ready=Authority!=nullptr;
    }
    ~FWorld(){if(!World)return;for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
    TArray<FDWCombatObjectBinding> Bindings()const{return {{TEXT("player:0"),Player}};}
    ADWCombatBowProjectile* Arrow()const{for(TActorIterator<ADWCombatBowProjectile> It(World);It;++It)if(It->IsReleased()&&!It->IsSpent()&&!It->IsActorBeingDestroyed())return *It;return nullptr;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEffectsCodecTest,"DoughWorld.Combat.Persistence.EffectsCodecStrictReferences",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEffectsCodecTest::RunTest(const FString& Parameters)
{
    auto S=DWEffectsTests::Sample();FString Error;TArray<uint8> Bytes,Again;FDWCombatEffectsSave D;
    if(!TestTrue(TEXT("Three flights, old source life, area and target cadence encode"),DWCombatEffectsSave::Encode(S,Bytes,Error))){AddError(Error);return false;}
    if(!TestTrue(TEXT("Strict decode"),DWCombatEffectsSave::Decode(Bytes,D,Error)))return false;TestTrue(TEXT("Re-encode"),DWCombatEffectsSave::Encode(D,Again,Error));TestTrue(TEXT("Byte exact round trip"),Bytes==Again);
    TestEqual(TEXT("No fresh target first tick"),D.TargetPhases[0].NextOrdinal,uint64(8));TestEqual(TEXT("Dead source does not change shot life"),D.Arrows[0].Owner.SourceLife.Life,uint64(1));
    auto Bad=Bytes;Bad.Add(0);TestFalse(TEXT("Trailing bytes reject"),DWCombatEffectsSave::Decode(Bad,D,Error));Bad=Bytes;Bad.SetNum(Bad.Num()-1);TestFalse(TEXT("Truncation rejects"),DWCombatEffectsSave::Decode(Bad,D,Error));
    Bad=Bytes;Bad[4]=99;TestFalse(TEXT("Unknown wire schema rejects"),DWCombatEffectsSave::Decode(Bad,D,Error));
    auto Invalid=S;Invalid.Arrows[0].Owner.Emitter=Invalid.Spores[0].Owner.Emitter;TestFalse(TEXT("Two physical carriers for one release reject"),DWCombatEffectsSave::Validate(Invalid,Error));
    Invalid=S;Invalid.Authority.Actors.RemoveAt(1);TestFalse(TEXT("Missing target binding rejects"),DWCombatEffectsSave::Validate(Invalid,Error));
    Invalid=S;const auto DuplicatePhase=Invalid.TargetPhases[0];Invalid.TargetPhases.Add(DuplicatePhase);TestFalse(TEXT("Duplicate target phase rejects"),DWCombatEffectsSave::Validate(Invalid,Error));
    Invalid=S;Invalid.Bottles[0].FlightAge=Invalid.Bottles[0].Shot.FlightSeconds;TestFalse(TEXT("Spent bottle cannot be recreated as flight"),DWCombatEffectsSave::Validate(Invalid,Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBowVisualExtensionTest,"DoughWorld.Combat.Persistence.BowVisualOptionalExtension",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBowVisualExtensionTest::RunTest(const FString& Parameters)
{
    auto S=DWEffectsTests::Sample();FString Error;FDWCombatEffectsSave Old,Decoded;TArray<uint8> Legacy,Bytes,Again;
    if(!TestTrue(TEXT("Old Schema1 layout encodes without an extension"),DWCombatEffectsSave::Encode(S,Legacy,Error)))return false;
    if(!TestTrue(TEXT("Old Schema1 remains legal"),DWCombatEffectsSave::Decode(Legacy,Old,Error)))return false;
    TestFalse(TEXT("Old arrows request DA reconstruction"),Old.Arrows[0].bHasVisualTransform);
    S.Arrows[0].bHasVisualTransform=true;S.Arrows[0].VisualTransform=FTransform(FRotator(5,-90,7),FVector(113,-63,18),FVector(1.2,.8,1.1));
    if(!TestTrue(TEXT("Visual extension encodes"),DWCombatEffectsSave::Encode(S,Bytes,Error)))return false;
    TestTrue(TEXT("Legacy bytes stay an exact prefix"),Bytes.Num()>Legacy.Num()&&FMemory::Memcmp(Bytes.GetData(),Legacy.GetData(),Legacy.Num())==0);
    if(!TestTrue(TEXT("New optional fields decode"),DWCombatEffectsSave::Decode(Bytes,Decoded,Error)))return false;
    TestTrue(TEXT("Exact release child pose survives"),Decoded.Arrows[0].bHasVisualTransform&&Decoded.Arrows[0].VisualTransform.Equals(S.Arrows[0].VisualTransform,0.));
    TestTrue(TEXT("New extension re-encodes"),DWCombatEffectsSave::Encode(Decoded,Again,Error)&&Again==Bytes);
    auto Bad=Bytes;Bad.Add(0);TestFalse(TEXT("Unknown extra tail still rejects"),DWCombatEffectsSave::Decode(Bad,Decoded,Error));
    Bad=Bytes;Bad.SetNum(Bad.Num()-1);TestFalse(TEXT("Partial optional transform rejects"),DWCombatEffectsSave::Decode(Bad,Decoded,Error));
    Bad=Bytes;Bad[Legacy.Num()]=0;TestFalse(TEXT("Unknown extension magic rejects"),DWCombatEffectsSave::Decode(Bad,Decoded,Error));
    Bad=Bytes;const uint64 NaNBits=0x7ff8000000000000ULL;
    FMemory::Memcpy(Bad.GetData()+Legacy.Num()+12+1+3*sizeof(double),&NaNBits,sizeof(NaNBits));
    TestFalse(TEXT("Nonfinite raw quaternion rejects before transform construction"),DWCombatEffectsSave::Decode(Bad,Decoded,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEffectsRuntimeTest,"DoughWorld.Combat.Persistence.ReleasedArrowBarrierAndOldEpoch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEffectsRuntimeTest::RunTest(const FString& Parameters)
{
    using namespace DWEffectsTests;FWorld F;if(!TestTrue(TEXT("Isolated ready player"),F.Ready))return false;
    auto* W=F.Player->WeaponAnimationComponent.Get();if(!TestTrue(TEXT("Paid bow attack"),W->PrimaryPressed(FVector(1000,0,100))))return false;W->PrimaryReleased();
    const float ToRelease=static_cast<float>(W->GetNextCombatBoundarySeconds(F.World->GetTimeSeconds())+.01);
    if(!TestTrue(TEXT("Finite actual release boundary"),FMath::IsFinite(ToRelease)&&ToRelease>0.f))return false;F.World->TimeSeconds+=ToRelease;F.Player->TickGameplayClocks(ToRelease,false);
    auto* Old=F.Arrow();if(!TestNotNull(TEXT("Released arrow"),Old))return false;Old->Tick(ToRelease);const FVector Position=Old->GetActorLocation();const auto OldEmitter=Old->GetEmitter();
    FDWCombatEffectsSave Saved;FString Error;if(!TestTrue(TEXT("Capture running flight"),FDWCombatEffectsPersistence::Capture(*F.Authority,F.Bindings(),Saved,Error))){AddError(Error);return false;}
    TestTrue(TEXT("New live arrow captures optional visual transform before any save"),Saved.Arrows.Num()==1&&Saved.Arrows[0].bHasVisualTransform);
    const FTransform VisualBefore=Saved.Arrows[0].VisualTransform;
    const auto* Bow=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::MoldBow);const int32 Stock=F.Player->Inventory->CountItem(Bow->AmmoItemId);
    FDWCombatEffectsRestorePlan Plan;if(!TestTrue(TEXT("Stage inert restore"),FDWCombatEffectsPersistence::PrepareRestore(*F.Authority,Saved,F.Bindings(),F.Bindings(),{},Plan,Error))){AddError(Error);return false;}
    TestEqual(TEXT("Staging did not charge"),F.Player->Inventory->CountItem(Bow->AmmoItemId),Stock);TestTrue(TEXT("Precommit candidate current"),Plan.CanCommit(Error));
    if(!TestTrue(TEXT("Enter world barrier"),F.Authority->BeginWorldRestoreBarrier(Plan.Authority(),Error)))return false;TestNull(TEXT("No outside gameplay authority during native adoption"),UDWCombatAuthoritySubsystem::Get(F.Player));
    FDWCombatEffectsPersistence::CommitFactsUnchecked(Plan);F.Authority->FinishWorldRestoreBarrier();FDWCombatEffectsPersistence::PublishRestoredEffects(Plan);
    auto* New=F.Arrow();if(!TestNotNull(TEXT("Restored flight active"),New))return false;
    TestTrue(TEXT("New physical carrier, same logical emitter ID"),New!=Old&&New->GetEmitter().Id==OldEmitter.Id&&New->GetEmitter().Epoch!=OldEmitter.Epoch);
    TestEqual(TEXT("Saved position preserved"),New->GetActorLocation(),Position);TestEqual(TEXT("No refund or second debit"),F.Player->Inventory->CountItem(Bow->AmmoItemId),Stock);
    TArray<USceneComponent*> NewComponents;New->GetComponents(NewComponents);bool FoundVisual=false;
    for(const auto* C:NewComponents)if(C->GetName()==TEXT("ArrowVisualRoot")){FoundVisual=true;TestTrue(TEXT("Restored visual child keeps release pose"),C->GetRelativeTransform().Equals(VisualBefore));}
    TestTrue(TEXT("Restored independent arrow visual root exists"),FoundVisual);
    TestFalse(TEXT("Consumed candidate cannot replay"),Plan.CanCommit(Error));
    TestTrue(TEXT("Old physical carrier retired"),!IsValid(Old)||Old->IsActorBeingDestroyed()||Old->IsSpent());
    F.World->TimeSeconds+=.05f;New->Tick(.05f);TestTrue(TEXT("Restored arrow resumes instead of restarting"),New->GetActorLocation().X>Position.X);
    FDWCombatEffectsSave LegacySaved,LegacyDecoded;TArray<uint8> LegacyBytes;
    if(!TestTrue(TEXT("Capture second isolated running flight"),FDWCombatEffectsPersistence::Capture(*F.Authority,F.Bindings(),LegacySaved,Error)))return false;
    for(auto& V:LegacySaved.Arrows){V.bHasVisualTransform=false;V.VisualTransform=FTransform::Identity;}
    if(!TestTrue(TEXT("Encode actual legacy layout without optional fields"),DWCombatEffectsSave::Encode(LegacySaved,LegacyBytes,Error)&&DWCombatEffectsSave::Decode(LegacyBytes,LegacyDecoded,Error)))return false;
    FDWCombatEffectsRestorePlan LegacyPlan;
    if(!TestTrue(TEXT("Stage old Schema1 arrow with DA fallback"),FDWCombatEffectsPersistence::PrepareRestore(*F.Authority,LegacyDecoded,F.Bindings(),F.Bindings(),{},LegacyPlan,Error)))return false;
    if(!TestTrue(TEXT("Old-layout restore barrier"),F.Authority->BeginWorldRestoreBarrier(LegacyPlan.Authority(),Error)))return false;
    FDWCombatEffectsPersistence::CommitFactsUnchecked(LegacyPlan);F.Authority->FinishWorldRestoreBarrier();FDWCombatEffectsPersistence::PublishRestoredEffects(LegacyPlan);
    auto* LegacyArrow=F.Arrow();TestTrue(TEXT("Old-layout independent carrier publishes without respending"),LegacyArrow&&LegacyArrow!=New&&!LegacyArrow->IsHidden()&&F.Player->Inventory->CountItem(Bow->AmmoItemId)==Stock);
    return true;
}
#endif
