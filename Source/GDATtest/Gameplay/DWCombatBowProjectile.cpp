#include "DWCombatBowProjectile.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWWeaponGeometry.h"
#include "ProceduralMeshComponent.h"
#include "Components/SceneComponent.h"
#include "DWDamageVolumeLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "UObject/ConstructorHelpers.h"
ADWCombatBowProjectile::ADWCombatBowProjectile()
{
    PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostPhysics;
    FlightRoot=CreateDefaultSubobject<USceneComponent>(TEXT("ArrowFlightRoot"));SetRootComponent(FlightRoot);
    VisualRoot=CreateDefaultSubobject<USceneComponent>(TEXT("ArrowVisualRoot"));VisualRoot->SetupAttachment(FlightRoot);
    Placeholder=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowPlaceholder"));Placeholder->SetupAttachment(VisualRoot);
    ArrowTip=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArrowTip"));ArrowTip->SetupAttachment(VisualRoot);
    AuthoredArrow=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ArrowAuthoredGeometry"));AuthoredArrow->SetupAttachment(VisualRoot);
    AuthoredArrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);AuthoredArrow->SetGenerateOverlapEvents(false);
    AuthoredArrow->SetCanEverAffectNavigation(false);AuthoredArrow->SetCastShadow(false);AuthoredArrow->SetVisibility(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Shaft(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Tip(TEXT("/Engine/BasicShapes/Cone.Cone"));
    if(Shaft.Succeeded())Placeholder->SetStaticMesh(Shaft.Object);if(Tip.Succeeded())ArrowTip->SetStaticMesh(Tip.Object);
    Placeholder->SetRelativeScale3D(FVector(.8,.06,.06));
    ArrowTip->SetRelativeLocation(FVector(50,0,0));ArrowTip->SetRelativeRotation(FRotator(90,0,0));ArrowTip->SetRelativeScale3D(FVector(.14,.14,.2));
    for(auto* Mesh:{Placeholder.Get(),ArrowTip.Get()})
    {Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);Mesh->SetCanEverAffectNavigation(false);}
    SetActorEnableCollision(false);SetActorHiddenInGame(true);
}
void ADWCombatBowProjectile::ActivateUnchecked(const FDWBowShotSnapshot& S,ADWPlayerCharacter& P,DWCombatCommit::FEntityLife Life,
    DWCombatCommit::FEmitterId Id,FVector Facing,double At)
{Shot=S;Source=&P;SourceController=P.GetController();SourceLife=Life;Emitter=Id;Direction=Facing.GetSafeNormal();ReleasedAt=LastWorldSeconds=At;bReleased=true;}
bool ADWCombatBowProjectile::FindFirstCollision(FVector Start,FVector End,FHitResult& Out)const
{
    FCollisionQueryParams Q(SCENE_QUERY_STAT(DWFormalArrowFlight),false,this);Q.AddIgnoredActor(Source.Get());TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByObjectType(Hits,Start,End,FQuat::Identity,FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
        FCollisionShape::MakeSphere(static_cast<float>(Shot.RadiusCm)),Q);
    double Best=MAX_dbl;bool Found=false;for(const auto& Hit:Hits)
    {
        auto* C=Hit.GetComponent();auto* A=Hit.GetActor();if(!IsValid(C)||!IsValid(A)||A==this||A==Source.Get()||A->IsActorBeingDestroyed())continue;
        const bool Combat=A->IsA<ADWEnemyCharacter>()||A->IsA<ADWEnemyNest>();
        if(Combat?!UDWDamageVolumeLibrary::IsDamageHitComponent(C):C->GetCollisionResponseToChannel(ECC_Visibility)!=ECR_Block)continue;
        if(Hit.Time<Best){Out=Hit;Best=Hit.Time;Found=true;}
    }
    return Found;
}
void ADWCombatBowProjectile::Tick(float Dt)
{
    Super::Tick(Dt);auto* World=GetWorld();auto* A=UDWCombatAuthoritySubsystem::Get(this);
    if(!World||World->bIsTearingDown||World->IsPaused()||!A||!bReleased||bSpent||bAdvancing||!FMath::IsFinite(Dt)||Dt<0.)return;
    TGuardValue<bool> Guard(bAdvancing,true);if(!PublishFlightPresentation())return;
    if(IsActorBeingDestroyed()||GetWorld()!=World||World->bIsTearingDown||World->IsPaused())return;
    const double Now=World->GetTimeSeconds();if(Now<=LastWorldSeconds)return;
    const double Distance=FMath::Min(Shot.RangeCm-TravelledCm,(Now-LastWorldSeconds)*Shot.SpeedCmPerSecond);LastWorldSeconds=Now;
    if(Distance<=0.){FinishFlight();return;}
    const FVector Start=GetActorLocation(),End=Start+Direction*Distance;FHitResult Hit;
    if(FindFirstCollision(Start,End,Hit))
    {
        TravelledCm+=Distance*FMath::Clamp(static_cast<double>(Hit.Time),0.,1.);bSpent=true;
        A->SubmitBowImpact(*this,Hit,ReleasedAt+TravelledCm/Shot.SpeedCmPerSecond);
        if(IsValid(this)&&!IsActorBeingDestroyed())FinishFlight();return;
    }
    TravelledCm+=Distance;SetActorLocation(End,false,nullptr,ETeleportType::TeleportPhysics);
    if(IsValid(this)&&!IsActorBeingDestroyed()&&TravelledCm>=Shot.RangeCm)FinishFlight();
}
void ADWCombatBowProjectile::FinishFlight()
{bSpent=true;if(!bAreaOwned)if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->RetireBowProjectile(*this);if(GetWorld()&&!GetWorld()->bIsTearingDown&&!IsActorBeingDestroyed())Destroy();}
void ADWCombatBowProjectile::EndPlay(EEndPlayReason::Type Reason)
{bSpent=true;if(!bAreaOwned)if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->RetireBowProjectile(*this);Super::EndPlay(Reason);}
bool ADWCombatBowProjectile::HasUnserializedReleasedArrows(UWorld* W)
{if(!W)return false;for(TActorIterator<ADWCombatBowProjectile> It(W);It;++It)if(It->bReleased&&!It->bSpent&&!It->IsActorBeingDestroyed())return true;return false;}


void ADWCombatBowProjectile::CaptureFlightPresentation(const UDWWeaponAnimationComponent& W,bool bMatchCurrentWorldPose)
{
    // Copy only native facts while the carrier is inert. Publication happens after the one gameplay commit.
    bFlightPresentationCaptured=true;CapturedCustomMesh=W.BowProjectileMesh;CapturedVisualTransform=W.BowProjectileVisualTransform;
    bHasAuthoredArrow=false;CapturedGeometry={};CapturedMaterial=nullptr;
    if(!IsValid(CapturedCustomMesh))
    {
        FTransform ActorLocal;UMaterialInterface* Material=nullptr;
        bHasAuthoredArrow=W.EvaluateBowReleaseVisual(CapturedGeometry,ActorLocal,Material);CapturedMaterial=Material;
        if(bHasAuthoredArrow)
        {
            CapturedVisualTransform=ActorLocal*W.BowProjectileVisualTransform;
            if(bMatchCurrentWorldPose&&W.GetOwner())
                CapturedVisualTransform=CapturedVisualTransform*W.GetOwner()->GetActorTransform()*GetActorTransform().Inverse();
        }
    }
    if(!CapturedVisualTransform.IsValid())CapturedVisualTransform=FTransform::Identity;
}
bool ADWCombatBowProjectile::PublishFlightPresentation()
{
    const TWeakObjectPtr<ADWCombatBowProjectile> Self(this);const TWeakObjectPtr<UWorld> World(GetWorld());
    const auto Expected=Emitter;
    const auto Current=[&]()
    {
        if(!Self.IsValid()||IsActorBeingDestroyed()||!World.IsValid()||World->bIsTearingDown||GetWorld()!=World.Get()||
            !bReleased||bSpent||Emitter.Epoch!=Expected.Epoch||Emitter.Id!=Expected.Id)return false;
        const auto* A=UDWCombatAuthoritySubsystem::Get(this);return A&&A->GetWorldEpoch()==Expected.Epoch;
    };
    if(!Current())return false;
    if(!bFlightPresentationCaptured)
    {
        // Old legal saves have no visual extension; reconstruct from the same source arrow, never the current held weapon.
        if(const auto* P=Source.Get())if(const auto* W=P->WeaponAnimationComponent.Get())CaptureFlightPresentation(*W,false);
        if(!Current())return false;
    }
    if(!bFlightPresentationReady)
    {
        bFlightPresentationReady=true;
        VisualRoot->SetRelativeTransform(CapturedVisualTransform);if(!Current())return false;
        if(IsValid(CapturedCustomMesh))
        {
            Placeholder->SetStaticMesh(CapturedCustomMesh);if(!Current())return false;
            Placeholder->SetRelativeTransform(FTransform::Identity);if(!Current())return false;
            ArrowTip->SetVisibility(false);if(!Current())return false;
        }
        else if(bHasAuthoredArrow)
        {
            const auto& G=CapturedGeometry;TArray<FVector2D> UVs=G.UVs;
            if(UVs.Num()!=G.Vertices.Num())UVs.Init(FVector2D::ZeroVector,G.Vertices.Num());
            TArray<FVector> Normals;TArray<FProcMeshTangent> Tangents;DWWeaponGeometry::CalculateTangents(G.Vertices,G.Triangles,UVs,Normals,Tangents);
            AuthoredArrow->CreateMeshSection_LinearColor(0,G.Vertices,G.Triangles,Normals,UVs,TArray<FLinearColor>(),Tangents,false);if(!Current())return false;
            if(CapturedMaterial){AuthoredArrow->SetMaterial(0,CapturedMaterial);if(!Current())return false;}
            AuthoredArrow->SetVisibility(true);if(!Current())return false;
            Placeholder->SetVisibility(false);if(!Current())return false;ArrowTip->SetVisibility(false);if(!Current())return false;
        }
        // Gameplay root stays unscaled; this rotation carries only the independent flight and visual children.
        SetActorRotation(Direction.Rotation());if(!Current())return false;
    }
    SetActorHiddenInGame(false);return Current();
}
