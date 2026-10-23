#include "DWGatheringPond.h"
#include "DWGatheringPondMath.h"
#include "DWGatheringProfile.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWGameInstance.h"
#include "DWWorldEvent.h"
#include "DWInteractionPromptComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include <cmath>

ADWGatheringPond::ADWGatheringPond()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=false;
    PondRoot=CreateDefaultSubobject<USceneComponent>(TEXT("PondRoot"));
    SetRootComponent(PondRoot);
    DryBed=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DryBed"));
    WaterSurface=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterSurface"));
    DryBed->SetupAttachment(PondRoot);WaterSurface->SetupAttachment(PondRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Grid(TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
    for(auto* Mesh:{DryBed.Get(),WaterSurface.Get()})
    {
        if(Cylinder.Succeeded())Mesh->SetStaticMesh(Cylinder.Object);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
    }
    if(Grid.Succeeded())DryBed->SetMaterial(0,Grid.Object);
    // Cursor hit testing needs visibility queries; every movement/physics channel remains ignored.
    WaterSurface->SetCollisionResponseToAllChannels(ECR_Ignore);
    WaterSurface->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    OverlapRange=CreateDefaultSubobject<UBoxComponent>(TEXT("OverlapRange"));
    OverlapRange->SetupAttachment(PondRoot);
    OverlapRange->SetUsingAbsoluteScale(true);
    OverlapRange->SetUsingAbsoluteRotation(true);
    OverlapRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    OverlapRange->SetCollisionObjectType(ECC_WorldDynamic);
    OverlapRange->SetCollisionResponseToAllChannels(ECR_Ignore);
    OverlapRange->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    OverlapRange->SetGenerateOverlapEvents(true);
    OverlapRange->SetCanEverAffectNavigation(false);
    OverlapRange->SetHiddenInGame(true);
    OverlapRange->ShapeColor=FColor(50,170,230);
    Prompt=CreateDefaultSubobject<UDWInteractionPromptComponent>(TEXT("InteractionPrompt"));
    Prompt->SetupAttachment(PondRoot);
    Prompt->GenericPromptTitle=FText::FromString(TEXT("水塘"));
    Prompt->GenericEnglishPromptTitle=FText::FromString(TEXT("Pond"));
    Prompt->GenericPromptAction=FText::FromString(TEXT("使用制作"));
    Prompt->GenericEnglishAction=FText::FromString(TEXT("Use crafting"));
    Prompt->GenericPromptKey=FText::FromString(TEXT("F"));
    Prompt->WorldOffset=FVector(0.,0.,100.);
    Prompt->bPromptEnabled=false;
    Tags.AddUnique(TEXT("DoughWorldGatheringPond"));
    RefreshPlaceholder();
}

bool ADWGatheringPond::IsConfigurationValid() const
{
    const FVector Scale=GetActorScale3D();
    return std::isfinite(InitialReserve)&&InitialReserve>=0.&&
        std::isfinite(RefillPerSecond)&&RefillPerSecond>=0.&&
        std::isfinite(SlowFraction)&&SlowFraction>=0.&&SlowFraction<=1.&&
        std::isfinite(RadiusCm)&&RadiusCm>0.&&RadiusCm<=100000.&&
        std::isfinite(SurfaceHeightToleranceCm)&&SurfaceHeightToleranceCm>=0.&&SurfaceHeightToleranceCm<=10000.&&
        std::isfinite(StationExtraRadiusCm)&&StationExtraRadiusCm>=0.&&StationExtraRadiusCm<=100000.&&
        !Scale.ContainsNaN()&&Scale.X>0.&&Scale.Y>0.&&Scale.Z>0.&&Scale.GetMax()<=100.&&
        !GetActorLocation().ContainsNaN()&&GetActorUpVector().Z>.999;
}

void ADWGatheringPond::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if(!HasActorBegunPlay()&&!bWaterRestored&&std::isfinite(InitialReserve)&&InitialReserve>=0.)RemainingWater=InitialReserve;
    RefreshPlaceholder();
}

void ADWGatheringPond::RandomizeSceneScale()
{
    if(HasActorBegunPlay()||!FMath::IsFinite(RandomScaleMin)||!FMath::IsFinite(RandomScaleMax)||
        RandomScaleMin<=0.f||RandomScaleMax<RandomScaleMin||RandomScaleMax>100.f)return;
    Modify();SetActorScale3D(FVector(FMath::FRandRange(RandomScaleMin,RandomScaleMax)));
    RefreshPlaceholder();
}

void ADWGatheringPond::BeginPlay()
{
    Super::BeginPlay();
    if(!bWaterRestored&&std::isfinite(InitialReserve)&&InitialReserve>=0.)RemainingWater=InitialReserve;
    OverlapRange->OnComponentBeginOverlap.AddDynamic(this,&ADWGatheringPond::OnPondBeginOverlap);
    OverlapRange->OnComponentEndOverlap.AddDynamic(this,&ADWGatheringPond::OnPondEndOverlap);
    RefreshPlaceholder();
    RefreshPromptAvailability();
}

bool ADWGatheringPond::IsGatheringProfileActive() const
{
    if(!GetWorld()||GetWorld()->bIsTearingDown||IsActorBeingDestroyed())return false;
    auto* GI=GetGameInstance<UDWGameInstance>();
    return GI&&Cast<UDWGatheringProfile>(GI->GetConfig())!=nullptr;
}

bool ADWGatheringPond::IsWetAndOperational() const
{
    return IsConfigurationValid()&&std::isfinite(RemainingWater)&&RemainingWater>0.&&
        RemainingWater<=InitialReserve&&IsGatheringProfileActive();
}

bool ADWGatheringPond::ContainsActor(const AActor* Actor,double ExtraRadius) const
{
    if(!IsValid(Actor)||Actor->IsActorBeingDestroyed()||Actor->GetWorld()!=GetWorld()||!IsConfigurationValid())return false;
    FVector Feet=Actor->GetActorLocation();
    if(const auto* Character=Cast<ACharacter>(Actor))
        if(const auto* Capsule=Character->GetCapsuleComponent())Feet.Z-=Capsule->GetScaledCapsuleHalfHeight();
    // Yaw is allowed, pitch/roll are rejected for this horizontal water-plane test actor.
    const FVector LocalFeet=GetActorTransform().InverseTransformPositionNoScale(Feet);
    const FVector Scale=GetActorScale3D();
    return DWGatheringPondMath::ContainsFeet(FVector::ZeroVector,LocalFeet,
        RadiusCm*Scale.X,RadiusCm*Scale.Y,SurfaceHeightToleranceCm,ExtraRadius);
}

bool ADWGatheringPond::IsUsableWaterStation(const AActor* User) const
{
    if(!IsWetAndOperational())return false;
    if(!User)return true;
    if(const auto* Player=Cast<ADWPlayerCharacter>(User))
        if(Player->IsDead()||!Cast<UDWGatheringProfile>(Player->GetGameplayConfig()))return false;
    return ContainsActor(User,StationExtraRadiusCm);
}

bool ADWGatheringPond::RestoreWater(double Value)
{
    if(!std::isfinite(Value)||!std::isfinite(InitialReserve)||InitialReserve<0.||Value<0.||Value>InitialReserve)return false;
    RemainingWater=Value;bWaterRestored=true;
    RefreshPlaceholder();
    if(RemainingWater==0.)ReleaseAllEnvironmentSlow();
    RefreshPromptAvailability();
    return true;
}

bool ADWGatheringPond::DebitRefillWater(double Amount)
{
    if(!HasAuthority()||!IsWetAndOperational()||!std::isfinite(Amount)||Amount<=0.||Amount>RemainingWater)return false;
    RemainingWater=Amount==RemainingWater?0.:RemainingWater-Amount;
    // No visibility, Blueprint, collision or movement call until player vitals have also committed.
    bIsDry=RemainingWater<=0.;
    return true;
}

void ADWGatheringPond::RefreshPlaceholder()
{
    const double SafeRadius=std::isfinite(RadiusCm)?FMath::Clamp(RadiusCm,1.,100000.):250.;
    DryBed->SetRelativeScale3D(FVector(SafeRadius/50.*1.05,SafeRadius/50.*1.05,.04));
    DryBed->SetRelativeLocation(FVector(0.,0.,-2.));
    WaterSurface->SetRelativeScale3D(FVector(SafeRadius/50.,SafeRadius/50.,.04));
    WaterSurface->SetRelativeLocation(FVector(0.,0.,2.));
    bIsDry=!std::isfinite(RemainingWater)||RemainingWater<=0.;
    WaterSurface->SetVisibility(!bIsDry);
    WaterSurface->SetHiddenInGame(bIsDry);
    WaterSurface->SetCollisionEnabled(bIsDry?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryOnly);
    const FVector Scale=GetActorScale3D();
    const double SafeScale=Scale.ContainsNaN()?1.:FMath::Clamp(FMath::Max(FMath::Abs(Scale.X),FMath::Abs(Scale.Y)),.01,100.);
    const double SafeHeight=std::isfinite(SurfaceHeightToleranceCm)?FMath::Clamp(SurfaceHeightToleranceCm,0.,10000.):120.;
    OverlapRange->SetBoxExtent(FVector(SafeRadius*SafeScale,SafeRadius*SafeScale,SafeHeight+300.));
    if(Prompt)
    {
        const double Extra=std::isfinite(StationExtraRadiusCm)?FMath::Clamp(StationExtraRadiusCm,0.,100000.):150.;
        Prompt->GenericDetectionRadius=static_cast<float>(SafeRadius*SafeScale+Extra);
        Prompt->HeightTolerance=static_cast<float>(SafeHeight+300.);
    }
}

void ADWGatheringPond::RefreshPromptAvailability()
{
    if(!Prompt||!HasActorBegunPlay())return;
    const auto* Player=Cast<ADWPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    // Reuse the existing presentation unchanged; authoritative feet/ellipse range filters its coarse proximity.
    const bool Enabled=Player&&IsUsableWaterStation(Player);
    if(Prompt->bPromptEnabled!=Enabled)Prompt->SetPromptEnabled(Enabled);
}

void ADWGatheringPond::SetActorEnvironmentSlow(AActor* Actor)
{
    if(auto* Player=Cast<ADWPlayerCharacter>(Actor))Player->SetGatheringEnvironmentSlow(this,SlowFraction);
    else if(auto* Enemy=Cast<ADWEnemyCharacter>(Actor))Enemy->SetGatheringEnvironmentSlow(this,SlowFraction);
}

void ADWGatheringPond::RemoveActorEnvironmentSlow(AActor* Actor)
{
    if(auto* Player=Cast<ADWPlayerCharacter>(Actor))Player->RemoveGatheringEnvironmentSlow(this);
    else if(auto* Enemy=Cast<ADWEnemyCharacter>(Actor))Enemy->RemoveGatheringEnvironmentSlow(this);
}

void ADWGatheringPond::ReleaseAllEnvironmentSlow()
{
    const auto Old=MoveTemp(SlowedActors);SlowedActors.Reset();
    for(const auto& Actor:Old)if(Actor.IsValid())RemoveActorEnvironmentSlow(Actor.Get());
}

void ADWGatheringPond::RefreshOccupants(double DeltaSeconds)
{
    if(!IsWetAndOperational()){ReleaseAllEnvironmentSlow();return;}
    TArray<AActor*> Overlapping;
    OverlapRange->GetOverlappingActors(Overlapping,ACharacter::StaticClass());
    TSet<TWeakObjectPtr<AActor>> Current;
    for(auto* Actor:Overlapping)
    {
        if(!ContainsActor(Actor))continue;
        auto* Player=Cast<ADWPlayerCharacter>(Actor);auto* Enemy=Cast<ADWEnemyCharacter>(Actor);
        if((!Player&&!Enemy)||(Player&&(Player->IsDead()||!Cast<UDWGatheringProfile>(Player->GetGameplayConfig())))||
            (Enemy&&!Enemy->IsAlive()))continue;
        Current.Add(Actor);SetActorEnvironmentSlow(Actor);
        if(Player&&DeltaSeconds>0.)
        {
            double Added=0.;
            const double Request=DWGatheringPondMath::RefillRequest(RefillPerSecond,DeltaSeconds,RemainingWater);
            if(Request>0.)Player->TryReceiveGatheringWater(*this,Request,Added);
            if(RemainingWater<=0.)break;
        }
    }
    for(const auto& Previous:SlowedActors)
        if(!Current.Contains(Previous)&&Previous.IsValid())RemoveActorEnvironmentSlow(Previous.Get());
    SlowedActors=MoveTemp(Current);
    if(RemainingWater<=0.)ReleaseAllEnvironmentSlow();
}

void ADWGatheringPond::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!HasAuthority()||!GetWorld()||GetWorld()->bIsTearingDown||IsActorBeingDestroyed())return;
    if(!IsGatheringProfileActive()){ReleaseAllEnvironmentSlow();RefreshPromptAvailability();return;}
    if(GetWorld()->IsPaused()||UDWWorldEventSubsystem::IsPlaying(this))return;
    if(!FMath::IsFinite(DeltaSeconds)||DeltaSeconds<0.f)return;
    RefreshOccupants(DeltaSeconds);
    // Keep depletion visible with only existing Engine geometry/materials: water hides, grid bed remains.
    if(WaterSurface->IsVisible()==bIsDry)RefreshPlaceholder();
    RefreshPromptAvailability();
}

void ADWGatheringPond::OnPondBeginOverlap(UPrimitiveComponent* Component,AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,int32 OtherBodyIndex,bool bFromSweep,const FHitResult& Sweep)
{
    if(!HasAuthority()||!IsWetAndOperational()||!ContainsActor(OtherActor))return;
    const auto* Player=Cast<ADWPlayerCharacter>(OtherActor);
    const auto* Enemy=Cast<ADWEnemyCharacter>(OtherActor);
    if((Player&&!Player->IsDead()&&Cast<UDWGatheringProfile>(Player->GetGameplayConfig()))||
        (Enemy&&Enemy->IsAlive()))
    {SlowedActors.Add(OtherActor);SetActorEnvironmentSlow(OtherActor);}
}

void ADWGatheringPond::OnPondEndOverlap(UPrimitiveComponent* Component,AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,int32 OtherBodyIndex)
{
    if(IsValid(OtherActor)&&!ContainsActor(OtherActor))
    {SlowedActors.Remove(OtherActor);RemoveActorEnvironmentSlow(OtherActor);}
}

void ADWGatheringPond::EndPlay(const EEndPlayReason::Type Reason)
{
    ReleaseAllEnvironmentSlow();
    Super::EndPlay(Reason);
}
