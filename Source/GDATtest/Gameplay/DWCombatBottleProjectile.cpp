#include "DWCombatBottleProjectile.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatGroundAreaSubsystem.h"
#include "DWAlcoholProjectile.h"
#include "DWDamageVolumeLibrary.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWPlayerCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "UObject/ConstructorHelpers.h"

ADWCombatBottleProjectile::ADWCombatBottleProjectile()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostPhysics;
    FlightRoot=CreateDefaultSubobject<USceneComponent>(TEXT("FlightRoot"));SetRootComponent(FlightRoot);
    BottleVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BottleVisual"));BottleVisual->SetupAttachment(FlightRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    if(Shape.Succeeded())BottleVisual->SetStaticMesh(Shape.Object);
    BottleVisual->SetRelativeScale3D(FVector(.16,.16,.32));BottleVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BottleVisual->SetCanEverAffectNavigation(false);BottleVisual->SetGenerateOverlapEvents(false);
    SetActorEnableCollision(false);SetActorHiddenInGame(true); // Staged actors have no flight or gameplay side effects.
}
bool ADWCombatBottleProjectile::HasReleasedBottle(const UWorld* World)
{
    if(!World)return false;
    for(TActorIterator<ADWCombatBottleProjectile> It(const_cast<UWorld*>(World));It;++It)
        if(It->bReleased&&!It->bSpent&&!It->IsActorBeingDestroyed())return true;
    return false;
}
void ADWCombatBottleProjectile::CopyAuthoredVisual(const ADWPlayerCharacter& P)
{
    const auto* Template=P.AlcoholProjectileClass?P.AlcoholProjectileClass->GetDefaultObject<ADWAlcoholProjectile>():nullptr;
    if(!Template||!Template->BottleMesh)return;
    const auto* Original=Template->BottleMesh.Get();if(Original->GetStaticMesh())BottleVisual->SetStaticMesh(Original->GetStaticMesh());
    BottleVisual->SetRelativeTransform(Original->GetRelativeTransform());
    for(int32 Index=0;Index<Original->GetNumMaterials();++Index)BottleVisual->SetMaterial(Index,Original->GetMaterial(Index));
    // Read-only reuse of existing artwork; never spawn the legacy damaging projectile/area path.
}
bool ADWCombatBottleProjectile::FindLegalGround(UWorld* World,const AActor* Ignore,FVector Nearby,double Depth,double Radius,double FloorZ,FVector& Out)
{
    if(!World||World->bIsTearingDown||Nearby.ContainsNaN()||!FMath::IsFinite(Depth)||Depth<=0.||
        !FMath::IsFinite(Radius)||Radius<=0.||!FMath::IsFinite(FloorZ)||FloorZ<0.||FloorZ>1.)return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWFormalBottleGround),false,Ignore);
    TArray<FHitResult> Hits;
    World->LineTraceMultiByObjectType(Hits,Nearby+FVector(0.,0.,Radius*2.),Nearby-FVector(0.,0.,Depth),
        FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),Params);
    double Best=MAX_dbl;bool Found=false;
    for(const auto& Hit:Hits)
    {
        const auto* Actor=Hit.GetActor();const auto* Component=Hit.GetComponent();
        if(!IsValid(Actor)||Actor->IsActorBeingDestroyed()||!IsValid(Component)||Actor->IsA<APawn>()||
            Actor->IsA<ADWEnemyNest>()||Actor->IsA<ADWCombatBottleProjectile>()||
            Component->GetCollisionResponseToChannel(ECC_Visibility)!=ECR_Block||Hit.ImpactNormal.Z<FloorZ)continue;
        if(Hit.Time<Best){Best=Hit.Time;Out=Hit.ImpactPoint+FVector(0.,0.,2.);Found=true;}
    }
    return Found;
}
void ADWCombatBottleProjectile::ActivateUnchecked(const FDWBottleSnapshot& Snapshot,ADWPlayerCharacter& P,
    DWCombatCommit::FEntityLife Life,DWCombatCommit::FEmitterId Id,FVector Start,FVector Ground,double At,double FloorZ)
{
    Shot=Snapshot;Source=&P;SourceController=P.GetController();SourceLife=Life;Emitter=Id;
    FlightStart=Start;GroundTarget=Ground;ReleasedWorldSeconds=LastWorldSeconds=At;WalkableFloorZ=FloorZ;bReleased=true;
}
bool ADWCombatBottleProjectile::FindFirstCollision(FVector Start,FVector End,FHitResult& Out)const
{
    auto* World=GetWorld();if(!World)return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWFormalBottleFlight),false,this);Params.AddIgnoredActor(Source.Get());
    TArray<FHitResult> Hits;World->SweepMultiByObjectType(Hits,Start,End,FQuat::Identity,
        FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),FCollisionShape::MakeSphere(static_cast<float>(Shot.CollisionRadiusCm)),Params);
    double Best=MAX_dbl;bool Found=false;
    for(const auto& Hit:Hits)
    {
        auto* C=Hit.GetComponent();auto* Actor=Hit.GetActor();
        if(!IsValid(C)||!IsValid(Actor)||Actor==this||Actor==Source.Get()||Actor->IsActorBeingDestroyed()||Actor->IsA<ADWCombatBottleProjectile>())continue;
        const bool CombatBody=Actor->IsA<ADWEnemyCharacter>()||Actor->IsA<ADWEnemyNest>();
        if(CombatBody?!UDWDamageVolumeLibrary::IsDamageHitComponent(C):C->GetCollisionResponseToChannel(ECC_Visibility)!=ECR_Block)continue;
        if(Hit.Time<Best){Best=Hit.Time;Out=Hit;Found=true;}
    }
    return Found;
}
void ADWCombatBottleProjectile::Tick(float Dt)
{
    Super::Tick(Dt);auto* World=GetWorld();auto* A=UDWCombatAuthoritySubsystem::Get(this);
    if(!A||!bReleased||bSpent||bAdvancing||!World||World->IsPaused()||!FMath::IsFinite(Dt)||Dt<0.)return;
    const TWeakObjectPtr<UWorld> Expected(World);const uint64 Epoch=A->GetWorldEpoch();
    if(IsHidden())SetActorHiddenInGame(false);
    if(!IsValid(this)||IsActorBeingDestroyed()||!Expected.IsValid()||GetWorld()!=World||World->bIsTearingDown||World->IsPaused())return;
    const double Now=World->GetTimeSeconds();if(Now<LastWorldSeconds)return;
    PendingSeconds+=Now-LastWorldSeconds;LastWorldSeconds=Now;
    TGuardValue<bool> Guard(bAdvancing,true);
    for(int32 Budget=256;Budget>0&&PendingSeconds>0.&&!bSpent;--Budget)
    {
        if(!IsValid(this)||IsActorBeingDestroyed()||!Expected.IsValid()||GetWorld()!=World||World->bIsTearingDown||World->IsPaused())return;
        A=UDWCombatAuthoritySubsystem::Get(this);if(!A||A->GetWorldEpoch()!=Epoch)return;
        const double Left=Shot.FlightSeconds-FlightAge;if(Left<=0.){Land(GroundTarget,ReleasedWorldSeconds+Shot.FlightSeconds);return;}
        // Only numerical curve sweep subdivision; game flight time/height remain the frozen rules.
        const double Step=FMath::Min3(PendingSeconds,Left,1./120.);
        const double NextAge=Step>=Left?Shot.FlightSeconds:FlightAge+Step;
        const FVector End=DWBottle::SampleArc(FlightStart,GroundTarget+FVector(0.,0.,Shot.CollisionRadiusCm),Shot.FlightSeconds,Shot.ArcHeightCm,NextAge);
        FHitResult Hit;const bool Collision=FindFirstCollision(GetActorLocation(),End,Hit);
        if(Collision)
        {FlightAge+=Step*FMath::Clamp(static_cast<double>(Hit.Time),0.,1.);Land(Hit.ImpactPoint,ReleasedWorldSeconds+FlightAge);return;}
        FlightAge=NextAge;PendingSeconds=FMath::Max(0.,PendingSeconds-Step);
        SetActorLocation(End,false,nullptr,ETeleportType::TeleportPhysics);
        if(!IsValid(this)||IsActorBeingDestroyed()||!Expected.IsValid()||GetWorld()!=World||World->bIsTearingDown)return;
        if(FlightAge>=Shot.FlightSeconds){Land(GroundTarget,ReleasedWorldSeconds+Shot.FlightSeconds);return;}
    }
}
void ADWCombatBottleProjectile::Land(FVector Nearby,double At)
{
    if(bSpent)return;bSpent=true;FVector Ground;
    if(FindLegalGround(GetWorld(),this,Nearby,Shot.RangeCm+Shot.ArcHeightCm,Shot.CollisionRadiusCm,WalkableFloorZ,Ground))
    {
        if(auto* Areas=UDWCombatGroundAreaSubsystem::Get(this))
        {
            const FDWAlcoholAreaSnapshot Area{Shot.TickDamage,Shot.AreaRadiusCm,Shot.AreaSeconds,Shot.TickSeconds,Shot.SlowFraction};
            const auto Result=Areas->AdoptReleasedAlcohol(Area,Source,SourceController,SourceLife,Emitter,Ground,At);
            bAreaOwned=Result==EDWAreaAdoption::Created||Result==EDWAreaAdoption::Existing;
        }
    }
    FinishFlight(); // No direct damage, explosion or per-bottle tick timer.
}
void ADWCombatBottleProjectile::FinishFlight()
{
    bSpent=true;if(!bAreaOwned)if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->RetireBottleProjectile(*this);
    if(GetWorld()&&!GetWorld()->bIsTearingDown&&!IsActorBeingDestroyed())Destroy();
}
void ADWCombatBottleProjectile::EndPlay(EEndPlayReason::Type Reason)
{
    bSpent=true;if(!bAreaOwned)if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->RetireBottleProjectile(*this);
    Super::EndPlay(Reason);
}
