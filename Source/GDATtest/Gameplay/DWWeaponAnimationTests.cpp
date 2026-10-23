#if WITH_DEV_AUTOMATION_TESTS
#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipmentTestFixture.h"
#include "DWPlayerCharacter.h"
#include "DWCombatFeedbackComponent.h"
#include "DWCombatRules.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "Animation/AnimSequence.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "DWCombatTestWorld.h"
#include "ProceduralMeshComponent.h"
#include "NiagaraComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "HAL/PlatformTime.h"

namespace DWWeaponAnimationTests
{
    struct FScope
    {
        TStrongObjectPtr<UDWGameInstance> CombatTestGI;
        UWorld* World=nullptr;
        ADWPlayerCharacter* Player=nullptr;
        FScope()
        {
            World=DWCombatTestWorld::Create(CombatTestGI,TEXT("DWWeaponAnimationTests"));
            if(!World)return;
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Player=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Params);
            if(Player&&!Player->HasActorBegunPlay())Player->DispatchBeginPlay();
        }
        ~FScope()
        {
            for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            CombatTestGI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
        }
        UDWWeaponAnimationComponent* Component()const{return Player->WeaponAnimationComponent;}
        UDWWeaponAnimationSet* MakeSet(EDWAnimatedWeapon Weapon,float Duration)
        {
            auto* Set=NewObject<UDWWeaponAnimationSet>(Player);Set->Weapon=Weapon;Set->bApplyBasicDamage=false;
            AddClip(Set,TEXT("Attack"),Duration);Component()->AnimationSets.Add(Set);Equip(Weapon);return Set;
        }
        bool Equip(EDWAnimatedWeapon Kind)
        {FString Error;return DWCombatEquipmentTestFixture::RestoreOwnedCurrent(*Component(),*CombatTestGI->GetCombatRules(),Kind,Error);}
        void AddClip(UDWWeaponAnimationSet* Set,FName Role,float Duration,bool bLoop=false)
        {
            FDWWeaponAnimationClip Clip;Clip.Role=Role;Clip.DurationSeconds=Duration;Clip.bLoop=bLoop;
            Clip.Animation=NewObject<UAnimSequence>(Set);Set->Clips.Add(Clip);
        }
        void Advance(float Dt){World->TimeSeconds+=Dt;Component()->AdvanceAnimation(Dt);}
        FVector Target()const{return Player->GetActorLocation()+FVector(400,0,0);}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponAnimationPrecisionTest,"DoughWorld.Combat.WeaponAnimation.ExactSourceClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponAnimationPrecisionTest::RunTest(const FString& Parameters)
{
    for(float Dt:{1.f/30.f,1.f/60.f})
    {
        DWWeaponAnimationTests::FScope S;auto* C=S.Component();
        TestTrue(TEXT("Default empty equipment has formal unarmed gameplay without source art"),C->IsEnabled()&&C->GetCurrentEquipmentId().IsNone());
        // Non-damaging synthetic source clock only; not a formal weapon attack assertion.
        auto* Set=S.MakeSet(EDWAnimatedWeapon::Spear,.24f);Set->AttackIntervalSeconds=.24f;Set->bRepeatWhileHeld=true;
        FDWWeaponDamageEvent Event;Event.TimeSeconds=.12f;Set->Clips[0].DamageEvents.Add(Event);
        TestTrue(TEXT("Start synthetic repeated clock"),C->PrimaryPressed(S.Target()));
        const int32 Initial=C->ActionSerial;float Time=0.f;
        while(Time<7.21f){S.Advance(Dt);Time+=Dt;}
        const float Reconstructed=(C->ActionSerial-Initial)*.24f+C->GetElapsedSeconds();
        TestTrue(TEXT("Thirty cycles retain fractional timing at 30/60 Hz"),FMath::Abs(Reconstructed-Time)<.0002f);
        TestEqual(TEXT("One event per observed source interval"),C->DispatchedEventCount,FMath::FloorToInt((Time-.12f)/.24f)+1);
        C->PrimaryReleased();S.Advance(.3f);TestFalse(TEXT("Release exits fire"),C->IsBusy());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponAnimationChargeTest,"DoughWorld.Combat.WeaponAnimation.ChargeAndRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponAnimationChargeTest::RunTest(const FString& Parameters)
{
    DWWeaponAnimationTests::FScope S;auto* C=S.Component();auto* Set=S.MakeSet(EDWAnimatedWeapon::Spear,3.7833333f);
    // Preserve the authored charge/hold/cancel clock test on a non-damaging generic source fixture.
    // Formal Greatsword is automatic two-stage gameplay and does not use this legacy input contract.
    Set->ChargeSeconds=1.6f;S.AddClip(Set,TEXT("ChargeHold"),1.f,true);S.AddClip(Set,TEXT("Cancel"),.7f);
    FDWWeaponDamageEvent Event;Event.TimeSeconds=1.8f;Set->Clips[0].DamageEvents.Add(Event);
    TestTrue(TEXT("Begin charge"),C->PrimaryPressed(S.Target()));S.Advance(1.61f);
    TestEqual(TEXT("Full charge enters hold"),C->GetActiveRole(),FName(TEXT("ChargeHold")));
    TestEqual(TEXT("Charge causes no hit"),C->DispatchedEventCount,0);
    C->PrimaryReleased();TestTrue(TEXT("Release resumes authored source time"),FMath::IsNearlyEqual(C->GetElapsedSeconds(),1.6f));
    S.Advance(.25f);TestEqual(TEXT("A tick across contact emits once"),C->DispatchedEventCount,1);
    S.Advance(.1f);TestEqual(TEXT("Contact is not repeated"),C->DispatchedEventCount,1);
    C->CancelAction();S.Advance(2.f);C->PrimaryPressed(S.Target());S.Advance(.2f);C->PrimaryReleased();
    TestEqual(TEXT("Undercharge uses authored cancel"),C->GetActiveRole(),FName(TEXT("Cancel")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponAnimationReloadTest,"DoughWorld.Combat.WeaponAnimation.ReloadCarry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponAnimationReloadTest::RunTest(const FString& Parameters)
{
    for(float Dt:{1.f/30.f,1.f/60.f})
    {
        DWWeaponAnimationTests::FScope S;auto* C=S.Component();auto* Set=S.MakeSet(EDWAnimatedWeapon::Shotgun,1.f);Set->MagazineCapacity=6;
        S.AddClip(Set,TEXT("ReloadEnter"),.2f);S.AddClip(Set,TEXT("ReloadOne"),.55f);S.AddClip(Set,TEXT("ReloadExit"),.2f);

        TSharedPtr<const FDWCombatRules> Rules;FString Error;
        if(!TestTrue(TEXT("Load formal resource rules"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
        if(!TestTrue(TEXT("Real registered reserve"),S.Player->Inventory->TryAddItem(TEXT("DWAmmo_BreadShell"),6)))return false;
        FDWCombatWeaponResourceSnapshot Snapshot;Snapshot.OwnedPermanentWeaponIds.Add(TEXT("DWWeapon_Shotgun"));
        Snapshot.CurrentEquipmentId=TEXT("DWWeapon_Shotgun");
        if(!TestTrue(TEXT("Explicit isolated ownership with empty magazine"),C->RestoreCombatWeaponState(Snapshot,*Rules,Error)))return false;
        TestTrue(TEXT("Select explicitly owned shotgun"),C->SelectWeapon(EDWAnimatedWeapon::Shotgun));
        TestTrue(TEXT("Begin isolated reserve-backed reload"),C->RequestReload());
        double ShellSeconds=0.;if(!TestTrue(TEXT("Read formal single-shell clock"),Rules->TryGetNumber(TEXT("weapon.shotgun.reload_one_s"),EDWCombatRuleUnit::Seconds,ShellSeconds,Error)))return false;
        const double BeforeLast=6.*ShellSeconds-.001;double Time=0.;
        while(Time<BeforeLast){const float Step=static_cast<float>(FMath::Min(static_cast<double>(Dt),BeforeLast-Time));S.Advance(Step);Time+=Step;}
        TestEqual(TEXT("Sixth shell does not commit before its exact formal deadline"),C->AmmoInMagazine,5);
        S.Advance(.002f);TestEqual(TEXT("Six exact formal reload periods carry at 30/60 Hz"),C->AmmoInMagazine,6);
        TestEqual(TEXT("R07/R08 move six real reserve shells, never free-fill"),S.Player->Inventory->CountItem(TEXT("DWAmmo_BreadShell")),0);
        TestEqual(TEXT("Original enter source duration remains intact"),Set->FindClip(TEXT("ReloadEnter"))->DurationSeconds,.2f);
        TestEqual(TEXT("Original exit source duration remains intact"),Set->FindClip(TEXT("ReloadExit"))->DurationSeconds,.2f);
        // Formal reload has no extra source-dependent enter/exit gameplay delay.
        TestFalse(TEXT("No reserve ends the formal reload immediately"),C->IsBusy());
        S.Advance(.25f);TestFalse(TEXT("Reload returns to normal locomotion"),C->IsBusy());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponAnimationTrailScaleTest,"DoughWorld.Combat.WeaponAnimation.TrailScalePivot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponAnimationTrailScaleTest::RunTest(const FString& Parameters)
{
    DWWeaponAnimationTests::FScope S;auto* C=S.Component();auto* Set=S.MakeSet(EDWAnimatedWeapon::DualDaggers,1.f);Set->bUseGeometryTrail=true;
    Set->PrimaryGeometry.Vertices={FVector(0,0,0),FVector(10,2,4),FVector(0,8,1)};
    Set->PrimaryGeometry.Triangles={0,1,2};Set->SecondaryGeometry=Set->PrimaryGeometry;
    const FTransform Primary(FRotator(10,45,0),FVector(100,-40,20),FVector(1.2,.8,1));
    const FTransform Secondary(FRotator(-15,-70,0),FVector(-65,55,35),FVector(.7,1.3,1));
    FDWWeaponVisualSample Sample;Sample.PrimaryTransform=Primary;Sample.SecondaryTransform=Secondary;Sample.bTrailActive=true;
    FDWWeaponGeometryFrame Frame;Frame.TrailTriangles=Frame.SecondaryTrailTriangles={0,1,2};
    for(const FVector& V:Set->PrimaryGeometry.Vertices)
    {
        Frame.TrailVertices.Add(Primary.TransformPosition(V));
        Frame.SecondaryTrailVertices.Add(Secondary.TransformPosition(V));
    }
    Set->Clips[0].HeldStartSeconds=0.f;Set->Clips[0].HeldEndSeconds=1.f;
    Set->Clips[0].VisualSamples={Sample};Set->Clips[0].GeometryFrames={Frame};
    auto FindVisual=[&S](const TCHAR* Name)->UProceduralMeshComponent*
    {
        TArray<UProceduralMeshComponent*> Components;S.Player->GetComponents(Components);
        for(auto* Component:Components)if(Component->GetFName()==FName(Name))return Component;
        return nullptr;
    };
    for(float Scale:{1.f,2.5f,.5f})
    {
        C->CancelAction();S.Advance(static_cast<float>(C->GetRemainingAttackLockSeconds()+.01));Set->VisualScale=Scale;
        TestTrue(TEXT("Start translated, rotated dual-weapon sample through formal input"),C->PrimaryPressed(S.Target()));
        C->PublishAcceptedWeaponStartEffects();
        for(int32 Hand=0;Hand<2;++Hand)
        {
            auto* Trail=FindVisual(Hand==0?TEXT("WeaponPrimaryTrail"):TEXT("WeaponSecondaryTrail"));
            auto* Weapon=FindVisual(Hand==0?TEXT("WeaponPrimaryDeformation"):TEXT("WeaponSecondaryDeformation"));
            if(!TestNotNull(TEXT("Trail component exists"),Trail)||!TestNotNull(TEXT("Weapon component exists"),Weapon))continue;
            auto* Section=Trail->GetProcMeshSection(0);
            if(!TestNotNull(TEXT("Actual procedural trail has a mesh section"),Section))continue;
            TestTrue(TEXT("Actor-local trail component itself stays unscaled"),Trail->GetRelativeTransform().Equals(FTransform::Identity));
            TestEqual(TEXT("Triangle has all three vertices"),Section->ProcVertexBuffer.Num(),3);
            if(Section->ProcVertexBuffer.Num()!=3)continue;
            for(int32 I=0;I<3;++I)
            {
                const FVector Expected=Weapon->GetRelativeTransform().TransformPosition(Set->PrimaryGeometry.Vertices[I]);
                TestTrue(TEXT("Trail remains attached to the corresponding weapon for either hand and scale"),Section->ProcVertexBuffer[I].Position.Equals(Expected,.001));
                if(Scale==1.f)
                {
                    const FVector Original=Hand==0?Frame.TrailVertices[I]:Frame.SecondaryTrailVertices[I];
                    TestTrue(TEXT("Default size preserves authored actor-local vertices"),Section->ProcVertexBuffer[I].Position.Equals(Original,0.));
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponAnimationEmptyTrailSlotsTest,"DoughWorld.Combat.WeaponAnimation.EmptyParticleSlots",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponAnimationEmptyTrailSlotsTest::RunTest(const FString& Parameters)
{
    DWWeaponAnimationTests::FScope S;auto* C=S.Component();auto* Set=S.MakeSet(EDWAnimatedWeapon::DualDaggers,1.f);
    Set->PrimaryGeometry.Vertices={FVector(0,0,0),FVector(20,0,0),FVector(0,20,0)};
    Set->PrimaryGeometry.Triangles={0,1,2};Set->SecondaryGeometry=Set->PrimaryGeometry;
    FDWWeaponVisualSample Sample;Sample.bTrailActive=true;Sample.PrimaryTrailTip=FVector(20,0,0);Sample.SecondaryTrailTip=FVector(0,20,0);
    FDWWeaponGeometryFrame Frame;Frame.TrailVertices=Frame.SecondaryTrailVertices=Set->PrimaryGeometry.Vertices;
    Frame.TrailTriangles=Frame.SecondaryTrailTriangles=Set->PrimaryGeometry.Triangles;
    Set->Clips[0].VisualSamples={Sample};Set->Clips[0].GeometryFrames={Frame};
    // Explicitly disable the newly requested runtime highlight override to verify the original empty-slot contract.
    S.Player->CombatFeedback->bHighlightTrails=false;
    TestTrue(TEXT("Legacy trail geometry remains loadable but disabled by default"),!Set->bUseGeometryTrail);
    TestTrue(TEXT("Attack starts with both optional Niagara slots empty"),C->PrimaryPressed(S.Target()));
    C->PublishAcceptedWeaponStartEffects();
    TestFalse(TEXT("No actual primary particle is active without a system"),C->IsPrimaryTrailActive());
    TestFalse(TEXT("No actual secondary particle is active without a system"),C->IsSecondaryTrailActive());
    TestNull(TEXT("No idle Niagara primary component is allocated for an empty slot"),C->PrimaryTrailComponent.Get());
    TestNull(TEXT("No idle Niagara secondary component is allocated for an empty slot"),C->SecondaryTrailComponent.Get());
    TArray<UProceduralMeshComponent*> Components;S.Player->GetComponents(Components);
    int32 VisibleWeapons=0;
    for(auto* Component:Components)
    {
        TestFalse(TEXT("Disabled geometry trail does not allocate a procedural trail component"),Component->GetFName()==TEXT("WeaponPrimaryTrail")||Component->GetFName()==TEXT("WeaponSecondaryTrail"));
        if((Component->GetFName()==TEXT("WeaponPrimaryDeformation")||Component->GetFName()==TEXT("WeaponSecondaryDeformation"))&&Component->IsVisible())++VisibleWeapons;
    }
    TestEqual(TEXT("Disabling the trail preserves both weapon deformation meshes"),VisibleWeapons,2);
    S.Player->CombatFeedback->bHighlightTrails=true;C->TickComponent(0.f,LEVELTICK_All,nullptr);
    Components.Reset();S.Player->GetComponents(Components);int32 HighlightedTrails=0;
    for(auto* Mesh:Components)if(Mesh->GetFName()==TEXT("WeaponPrimaryTrail")||Mesh->GetFName()==TEXT("WeaponSecondaryTrail"))
    {
        TestTrue(TEXT("Explicit highlight override displays existing authored tail"),Mesh->IsVisible());
        TestEqual(TEXT("Tail uses the independent highlight material"),Mesh->GetMaterial(0),S.Player->CombatFeedback->TrailHighlightMaterial.Get());++HighlightedTrails;
    }
    TestEqual(TEXT("Highlight override has both original tail faces"),HighlightedTrails,2);
    C->CancelAction();TestFalse(TEXT("Empty-slot cancellation remains safe"),C->IsBusy());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponGeometryEquivalenceTest,"DoughWorld.Combat.WeaponAnimation.GeometryTangentEquivalence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponGeometryEquivalenceTest::RunTest(const FString& Parameters)
{
    auto Compare=[this](const FString& Name,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,const TArray<FVector2D>& SourceUVs,bool bRequireImprovement)
    {
        TArray<FVector2D> UVs=SourceUVs;if(UVs.Num()!=Vertices.Num())UVs.Init(FVector2D::ZeroVector,Vertices.Num());
        TArray<FVector> OldNormals,NewNormals;TArray<FProcMeshTangent> OldTangents,NewTangents;
        const double OldStart=FPlatformTime::Seconds();
        UKismetProceduralMeshLibrary::CalculateTangentsForMesh(Vertices,Triangles,UVs,OldNormals,OldTangents);
        const double OldMs=(FPlatformTime::Seconds()-OldStart)*1000.;
        uint64 Comparisons=0;const double NewStart=FPlatformTime::Seconds();
        DWWeaponGeometry::CalculateTangents(Vertices,Triangles,UVs,NewNormals,NewTangents,&Comparisons);
        const double NewMs=(FPlatformTime::Seconds()-NewStart)*1000.;
        if(!TestEqual(Name+TEXT(" normal count"),NewNormals.Num(),OldNormals.Num())||!TestEqual(Name+TEXT(" tangent count"),NewTangents.Num(),OldTangents.Num()))return;
        double MaxNormalError=0.,MaxTangentError=0.;int32 FlipDifferences=0,InvalidBasis=0;
        for(int32 V=0;V<Vertices.Num();++V)
        {
            if(NewNormals[V].ContainsNaN()||NewTangents[V].TangentX.ContainsNaN())++InvalidBasis;
            MaxNormalError=FMath::Max(MaxNormalError,FVector::Dist(NewNormals[V],OldNormals[V]));
            MaxTangentError=FMath::Max(MaxTangentError,FVector::Dist(NewTangents[V].TangentX,OldTangents[V].TangentX));
            if(NewTangents[V].bFlipTangentY!=OldTangents[V].bFlipTangentY)++FlipDifferences;
        }
        TestEqual(Name+TEXT(" finite basis"),InvalidBasis,0);
        TestTrue(Name+TEXT(" full vertex normals match UE helper"),MaxNormalError<=.00001);
        TestTrue(Name+TEXT(" full vertex tangents match UE helper"),MaxTangentError<=.00001);
        TestEqual(Name+TEXT(" tangent handedness matches UE helper"),FlipDifferences,0);
        const uint64 OldComparisons=static_cast<uint64>(Vertices.Num())*Triangles.Num();
        if(bRequireImprovement)TestTrue(Name+TEXT(" overlap comparisons reduced by at least 100x without reducing mesh"),Comparisons*100<OldComparisons);
        AddInfo(FString::Printf(TEXT("%s: V=%d T=%d old=%.3fms new=%.3fms overlap=%llu -> %llu maxNormal=%.9g maxTangent=%.9g"),*Name,Vertices.Num(),Triangles.Num()/3,OldMs,NewMs,OldComparisons,Comparisons,MaxNormalError,MaxTangentError));
    };
    // Duplicate positions with different UVs must share normals without merging UV tangents.
    const TArray<FVector> SeamVertices={FVector(0,0,0),FVector(10,0,0),FVector(0,10,0),FVector(0,0,0),FVector(0,10,0),FVector(0,0,10)};
    const TArray<int32> SeamTriangles={0,1,2,3,4,5};
    const TArray<FVector2D> SeamUVs={FVector2D(0,0),FVector2D(1,0),FVector2D(0,1),FVector2D(1,1),FVector2D(0,1),FVector2D(1,0)};
    Compare(TEXT("UV seam"),SeamVertices,SeamTriangles,SeamUVs,false);
    TArray<FVector> BoundaryVertices=SeamVertices;
    BoundaryVertices[0]=FVector(-.25*UE_KINDA_SMALL_NUMBER,0,0);
    BoundaryVertices[3]=FVector(.25*UE_KINDA_SMALL_NUMBER,0,0);
    Compare(TEXT("Tolerance overlap across spatial cell boundary"),BoundaryVertices,SeamTriangles,SeamUVs,false);
    Compare(TEXT("Missing UV / debug trail basis"),SeamVertices,SeamTriangles,TArray<FVector2D>(),false);
    auto* Set=LoadObject<UDWWeaponAnimationSet>(nullptr,TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Spear.DA_DWHeroWeapon_Spear"));
    if(!TestNotNull(TEXT("Real configured spear asset loads"),Set))return false;
    TestTrue(TEXT("Test uses the full production spear, not a reduced fixture"),Set->PrimaryGeometry.Vertices.Num()>5000&&Set->PrimaryGeometry.Triangles.Num()>30000);
    Compare(TEXT("Full production Spear rest"),Set->PrimaryGeometry.Vertices,Set->PrimaryGeometry.Triangles,Set->PrimaryGeometry.UVs,true);
    const auto* Clip=Set->FindClip(TEXT("Attack"));
    if(!TestNotNull(TEXT("Real spear attack exists"),Clip))return false;
    bool bComparedDeformation=false;
    for(int32 I=0;I+1<Clip->GeometryFrames.Num();++I)
    {
        const auto& A=Clip->GeometryFrames[I].PrimaryVertices;const auto& B=Clip->GeometryFrames[I+1].PrimaryVertices;
        if(A.Num()!=Set->PrimaryGeometry.Vertices.Num()||B.Num()!=A.Num()||A==B)continue;
        TArray<FVector> Between;Between.SetNumUninitialized(A.Num());
        for(int32 V=0;V<A.Num();++V)Between[V]=FMath::Lerp(A[V],B[V],.37f);
        Compare(TEXT("Full production Spear sub-frame deformation"),Between,Set->PrimaryGeometry.Triangles,Set->PrimaryGeometry.UVs,true);
        bComparedDeformation=true;break;
    }
    TestTrue(TEXT("A non-keyframe shape was compared against the old full algorithm"),bComparedDeformation);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWeaponGeometryCacheTest,"DoughWorld.Combat.WeaponAnimation.GeometryCacheAndDeformation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWeaponGeometryCacheTest::RunTest(const FString& Parameters)
{
    auto* Source=LoadObject<UDWWeaponAnimationSet>(nullptr,TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Spear.DA_DWHeroWeapon_Spear"));
    if(!TestNotNull(TEXT("Real spear asset is available"),Source))return false;
    DWWeaponAnimationTests::FScope S;auto* C=S.Component();
    auto* Set=DuplicateObject<UDWWeaponAnimationSet>(Source,S.Player);Set->bApplyBasicDamage=false;
    Set->PrimaryTrailSystem=nullptr;Set->SecondaryTrailSystem=nullptr;
    C->AnimationSets={Set};TestTrue(TEXT("Explicit owned/current isolated copy of real spear"),S.Equip(EDWAnimatedWeapon::Spear));
    TestTrue(TEXT("Selecting the same equipped spear does not add a switch lock"),C->SelectWeapon(EDWAnimatedWeapon::Spear));
    C->TickComponent(0.f,LEVELTICK_All,nullptr); // Initial visual upload; subsequent idle ticks must use the cache.
    const auto Initial=C->GetGeometryStats();
    const double IdleStart=FPlatformTime::Seconds();
    for(int32 I=0;I<120;++I)C->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
    const double IdleMs=(FPlatformTime::Seconds()-IdleStart)*1000.;
    const auto Idle=C->GetGeometryStats();
    TestEqual(TEXT("120 idle ticks do not rebuild tangents"),Idle.TangentBuilds,Initial.TangentBuilds);
    TestEqual(TEXT("120 idle ticks do not upload geometry"),Idle.MeshUploads,Initial.MeshUploads);
    TestTrue(TEXT("Repeated geometry uses exact unchanged cache"),Idle.UnchangedSkips>=Initial.UnchangedSkips+120);
    AddInfo(FString::Printf(TEXT("Full Spear idle: 120 UpdateVisuals=%.3fms, tangent builds=%llu mesh uploads=%llu unchanged skips=%llu"),IdleMs,Idle.TangentBuilds-Initial.TangentBuilds,Idle.MeshUploads-Initial.MeshUploads,Idle.UnchangedSkips-Initial.UnchangedSkips));
    const auto* Clip=Set->FindClip(TEXT("Attack"));if(!TestNotNull(TEXT("Attack clip exists"),Clip))return false;
    int32 Pair=INDEX_NONE;
    for(int32 I=0;I+1<Clip->GeometryFrames.Num();++I)
    {
        const auto& A=Clip->GeometryFrames[I];const auto& B=Clip->GeometryFrames[I+1];
        if(A.PrimaryVertices.Num()==Set->PrimaryGeometry.Vertices.Num()&&B.PrimaryVertices.Num()==A.PrimaryVertices.Num()&&A.PrimaryVertices!=B.PrimaryVertices&&B.TimeSeconds>A.TimeSeconds){Pair=I;break;}
    }
    if(!TestTrue(TEXT("Spear has actual authored deformation"),Pair!=INDEX_NONE))return false;
    const auto& A=Clip->GeometryFrames[Pair];const auto& B=Clip->GeometryFrames[Pair+1];
    TestTrue(TEXT("Start unchanged production attack"),C->PlayRole(TEXT("Attack"),S.Target()));
    const float TargetTime=FMath::Lerp(A.TimeSeconds,B.TimeSeconds,.37f);S.Advance(TargetTime);
    C->TickComponent(0.f,LEVELTICK_All,nullptr);
    UProceduralMeshComponent* Visual=nullptr;TArray<UProceduralMeshComponent*> Components;S.Player->GetComponents(Components);
    for(auto* Component:Components)if(Component->GetFName()==TEXT("WeaponPrimaryDeformation"))Visual=Component;
    if(!TestNotNull(TEXT("Actual spear procedural visual exists"),Visual))return false;
    auto* Section=Visual->GetProcMeshSection(0);if(!TestNotNull(TEXT("Actual spear mesh section exists"),Section))return false;
    if(!TestEqual(TEXT("All source vertices retained"),Section->ProcVertexBuffer.Num(),A.PrimaryVertices.Num()))return false;
    const float Alpha=FMath::Clamp((C->GetElapsedSeconds()-A.TimeSeconds)/FMath::Max(.001f,B.TimeSeconds-A.TimeSeconds),0.f,1.f);
    double MaxError=0.;
    for(int32 V=0;V<A.PrimaryVertices.Num();++V)MaxError=FMath::Max(MaxError,FVector::Dist(Section->ProcVertexBuffer[V].Position,FMath::Lerp(A.PrimaryVertices[V],B.PrimaryVertices[V],Alpha)));
    TestTrue(TEXT("Every actual uploaded vertex keeps precise sub-frame shape"),MaxError<.000001);
    const auto AtPose=C->GetGeometryStats();
    C->TickComponent(0.f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("Unchanged attack time does not upload again"),C->GetGeometryStats().MeshUploads,AtPose.MeshUploads);
    // Still inside the same authored sample pair: a tiny time change must not be quantized away.
    S.Advance((B.TimeSeconds-A.TimeSeconds)*.01f);C->TickComponent(0.f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Small real sub-frame changes still update the full mesh"),C->GetGeometryStats().MeshUploads>AtPose.MeshUploads);
    C->CancelAction();C->TickComponent(0.f,LEVELTICK_All,nullptr);
    const auto Returned=C->GetGeometryStats();
    for(int32 I=0;I<60;++I)C->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
    TestEqual(TEXT("After attacking, idle remains cached rather than continuing uploads"),C->GetGeometryStats().MeshUploads,Returned.MeshUploads);
    TestEqual(TEXT("After attacking, idle remains cached rather than rebuilding tangents"),C->GetGeometryStats().TangentBuilds,Returned.TangentBuilds);
    AddInfo(FString::Printf(TEXT("Production spear deformation max position error=%.9g cm, total builds=%llu uploads=%llu comparisons=%llu"),MaxError,C->GetGeometryStats().TangentBuilds,C->GetGeometryStats().MeshUploads,C->GetGeometryStats().OverlapComparisons));
    return true;
}
#endif
