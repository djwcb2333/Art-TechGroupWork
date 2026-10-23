#include "DWCombatSporeProjectile.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWPlayerCharacter.h"
#include "DWDamageVolumeLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "UObject/ConstructorHelpers.h"

ADWCombatSporeProjectile::ADWCombatSporeProjectile()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostPhysics;
    FlightRoot=CreateDefaultSubobject<USceneComponent>(TEXT("FlightRoot"));SetRootComponent(FlightRoot);
    Placeholder=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Placeholder"));Placeholder->SetupAttachment(FlightRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(Sphere.Succeeded())Placeholder->SetStaticMesh(Sphere.Object);
    Placeholder->SetRelativeScale3D(FVector(.16));Placeholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Placeholder->SetCanEverAffectNavigation(false);Placeholder->SetGenerateOverlapEvents(false);
    // Staging is inert even if Spawn/FinishSpawning triggers third-party world delegates.
    SetActorEnableCollision(false);SetActorHiddenInGame(true);
}
ADWEnemyCharacter* ADWCombatSporeProjectile::GetLockedTarget()const{return bLostLock?nullptr:LockedTarget.Get();}
ADWEnemyCharacter* ADWCombatSporeProjectile::FindLock(ADWPlayerCharacter& P,FVector Origin,FVector Facing,
    const FDWSporeGunConfig& Config,const FDWSporeShotSnapshot& Snapshot)
{
    auto* World=P.GetWorld();if(!World||World->bIsTearingDown)return nullptr;
    ADWEnemyCharacter* Best=nullptr;double Distance=Snapshot.RangeCm*Snapshot.RangeCm;
    const double Cos=FMath::Cos(FMath::DegreesToRadians(Config.TargetArcDegrees*.5));
    for(TActorIterator<ADWEnemyCharacter> It(World);It;++It)
    {
        auto* E=*It;if(!E->IsAlive()||!E->IsHostileToCombatPlayer(&P)||E->IsActorBeingDestroyed()||!E->GetActorEnableCollision())continue;
        const FVector To=E->GetActorLocation()-Origin;const double D=To.SizeSquared();
        if(D>Distance||FVector::DotProduct(To.GetSafeNormal2D(),Facing.GetSafeNormal2D())+UE_DOUBLE_SMALL_NUMBER<Cos)continue;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DWSporeLockSight),false,&P);Params.AddIgnoredActor(E);
        FHitResult Block;if(World->LineTraceSingleByChannel(Block,Origin,E->GetActorLocation(),ECC_Visibility,Params))continue;
        if(D<Distance||!Best||(D==Distance&&E->GetUniqueID()<Best->GetUniqueID())){Distance=D;Best=E;}
    }
    return Best;
}
void ADWCombatSporeProjectile::ActivateUnchecked(const FDWSporeShotSnapshot& Snapshot,ADWPlayerCharacter& P,ADWEnemyCharacter* Target,
    DWCombatCommit::FEntityLife PlayerLife,DWCombatCommit::FEntityLife TargetLife,DWCombatCommit::FEmitterId Id,FVector Facing,double WorldSeconds)
{
    Shot=Snapshot;Source=&P;SourceController=P.GetController();SourceLife=PlayerLife;LockedTarget=Target;LockedTargetLife=TargetLife;
    Emitter=Id;Direction=Facing.GetSafeNormal();LastWorldSeconds=WorldSeconds;bReleased=true;bLostLock=Target==nullptr;
    // Facts only. Visibility and transform notifications occur after inventory/receipt adoption.
}
bool ADWCombatSporeProjectile::FindFirstCollision(FVector Start,FVector End,FHitResult& Out)const
{
    auto* World=GetWorld();if(!World)return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWSporeFlight),false,this);Params.AddIgnoredActor(Source.Get());
    TArray<FHitResult> Hits;World->SweepMultiByObjectType(Hits,Start,End,FQuat::Identity,
        FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),FCollisionShape::MakeSphere(static_cast<float>(Shot.RadiusCm)),Params);
    double Best=MAX_dbl;bool Found=false;
    for(const auto& Hit:Hits)
    {
        auto* C=Hit.GetComponent();auto* A=Hit.GetActor();if(!IsValid(C)||!IsValid(A)||A==this||A==Source.Get()||A->IsActorBeingDestroyed())continue;
        if(A->IsA<ADWCombatSporeProjectile>())continue;
        const bool CombatBody=A->IsA<ADWEnemyCharacter>()||A->IsA<ADWEnemyNest>();
        if(CombatBody?!UDWDamageVolumeLibrary::IsDamageHitComponent(C):C->GetCollisionResponseToChannel(ECC_Visibility)!=ECR_Block)continue;
        if(Hit.Time<Best){Best=Hit.Time;Out=Hit;Found=true;}
    }
    return Found;
}
void ADWCombatSporeProjectile::Tick(float Dt)
{
    Super::Tick(Dt);
    auto* A=UDWCombatAuthoritySubsystem::Get(this);auto* World=GetWorld();
    if(!A||!bReleased||bSpent||bAdvancing||!World||World->IsPaused()||!FMath::IsFinite(Dt)||Dt<0.)return;
    if(IsHidden())SetActorHiddenInGame(false);
    if(IsActorBeingDestroyed()||!GetWorld()||GetWorld()!=World||World->bIsTearingDown||World->IsPaused())return;
    const double Now=World->GetTimeSeconds();if(Now<LastWorldSeconds)return;
    PendingSeconds+=Now-LastWorldSeconds;LastWorldSeconds=Now;
    TGuardValue<bool> Guard(bAdvancing,true);const TWeakObjectPtr<UWorld> ExpectedWorld(World);
    for(int32 Budget=256;Budget>0&&PendingSeconds>0.&&!bSpent;--Budget)
    {
        if(!IsValid(this)||IsActorBeingDestroyed()||!ExpectedWorld.IsValid()||GetWorld()!=ExpectedWorld.Get()||World->bIsTearingDown)return;
        auto* P=Source.Get();auto* Target=LockedTarget.Get();
        if(!bLostLock)
        {
            const auto Life=A->CaptureActorLife(Target);
            if(!IsValid(P)||!IsValid(Target)||P->IsActorBeingDestroyed()||Target->IsActorBeingDestroyed()||!Target->IsAlive()||
                Life.Entity!=LockedTargetLife.Entity||Life.Life!=LockedTargetLife.Life||!Target->IsHostileToCombatPlayer(P))bLostLock=true;
        }
        const double Remaining=FMath::Max(0.,Shot.RangeCm-TravelledCm);
        if(Remaining<=0.){FinishFlight();return;}
        // Numerical sweep resolution; this does not change the configured speed, range or turn-rate cap.
        const double Step=FMath::Min3(PendingSeconds,1./120.,Remaining/Shot.SpeedCmPerSecond);
        if(Step<=0.){FinishFlight();return;}
        if(!bLostLock)Direction=DWSporeGun::TurnTowards(Direction,Target->GetActorLocation()-GetActorLocation(),Shot.TurnDegreesPerSecond*Step);
        const FVector Start=GetActorLocation();const double Distance=Shot.SpeedCmPerSecond*Step;
        const FVector End=Start+Direction*Distance;FHitResult Hit;const bool Collided=FindFirstCollision(Start,End,Hit);
        const bool ReachesRange=!Collided&&Step==Remaining/Shot.SpeedCmPerSecond;
        TravelledCm=ReachesRange?Shot.RangeCm:TravelledCm+Distance*(Collided?FMath::Clamp(static_cast<double>(Hit.Time),0.,1.):1.);PendingSeconds-=Step;
        if(Collided)
        {
            bSpent=true; // A nested damage callback cannot ask this projectile to collide again.
            A->SubmitSporeImpact(*this,Hit);
            if(IsValid(this)&&!IsActorBeingDestroyed())FinishFlight();return;
        }
        SetActorLocation(End,false,nullptr,ETeleportType::TeleportPhysics);
        if(!IsValid(this)||IsActorBeingDestroyed()||!ExpectedWorld.IsValid()||World->bIsTearingDown)return;
        if(TravelledCm>=Shot.RangeCm){FinishFlight();return;}
    }
}
void ADWCombatSporeProjectile::FinishFlight()
{
    bSpent=true;
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->RetireSporeProjectile(*this);
    if(GetWorld()&&!GetWorld()->bIsTearingDown&&!IsActorBeingDestroyed())Destroy();
}
void ADWCombatSporeProjectile::EndPlay(EEndPlayReason::Type Reason)
{
    bSpent=true;if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->RetireSporeProjectile(*this);
    Super::EndPlay(Reason);
}
