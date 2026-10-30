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
#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "LandscapeProxy.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "VT/RuntimeVirtualTexture.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "UObject/ConstructorHelpers.h"
#include <cmath>

ADWGatheringPond::ADWGatheringPond()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=false;
    PondRoot=CreateDefaultSubobject<USceneComponent>(TEXT("PondRoot"));
    SetRootComponent(PondRoot);
    PondBoundary=CreateDefaultSubobject<USplineComponent>(TEXT("PondBoundary"));
    PondBoundary->SetupAttachment(PondRoot);
    PondBoundary->SetRelativeLocation(FVector(0.,0.,WaterHeightCm));
    PondBoundary->SetHiddenInGame(true);
    PondBoundary->bInputSplinePointsToConstructionScript=true;
    PondBoundary->SetCanEverAffectNavigation(false);
    PondBoundary->ClearSplinePoints(false);
    for(int32 I=0;I<12;++I)
    {
        const double Angle=2.*PI*I/12.;
        PondBoundary->AddSplinePoint(FVector(RadiusCm*FMath::Cos(Angle),RadiusCm*FMath::Sin(Angle),0.),ESplineCoordinateSpace::Local,false);
        PondBoundary->SetSplinePointType(I,ESplinePointType::Curve,false);
    }
    PondBoundary->SetClosedLoop(true,true);
    WaterMesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
    PondBank=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("PondBank"));
    EdgeFoamMesh=CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("EdgeFoamMesh"));
    EdgeFoamMesh->SetupAttachment(PondRoot);EdgeFoamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    EdgeFoamMesh->SetGenerateOverlapEvents(false);EdgeFoamMesh->SetCanEverAffectNavigation(false);EdgeFoamMesh->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> EdgeFoamAsset(TEXT("/Game/DoughWorld/Fab/SoStylized/Environment/Water/Materials/MI_WaterWaves.MI_WaterWaves"));
    if(EdgeFoamAsset.Succeeded())EdgeFoamMaterial=EdgeFoamAsset.Object;
    for(auto* Mesh:{WaterMesh.Get(),PondBank.Get()})
    {
        Mesh->SetupAttachment(PondRoot);
        Mesh->bUseComplexAsSimpleCollision=true;
        Mesh->bUseAsyncCooking=false;
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    }
    WaterMesh->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    WaterMesh->SetCanEverAffectNavigation(false);
    WaterMesh->SetCastShadow(false);
    PondBank->SetCollisionObjectType(ECC_WorldStatic);
    PondBank->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
    PondBank->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
    PondBank->SetCollisionResponseToChannel(ECC_WorldDynamic,ECR_Block);
    PondBank->SetCanEverAffectNavigation(true);
    DryBed=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DryBed"));
    WaterSurface=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterSurface"));
    DryBed->SetupAttachment(PondRoot);WaterSurface->SetupAttachment(PondRoot);
    WaterArt=CreateDefaultSubobject<UChildActorComponent>(TEXT("WaterArt"));
    WaterArt->SetupAttachment(PondRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Grid(TEXT("/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Sand(TEXT("/Game/DoughWorld/Environment/Materials/Landscape/MI_DW_MeshPaint_Desert_Map1.MI_DW_MeshPaint_Desert_Map1"));
    if(Sand.Succeeded())BankMaterial=Sand.Object;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Match(TEXT("/Game/DoughWorld/Gameplay/Resources/Art/Pond/Materials/M_DW_PondBank_RVT_Storm.M_DW_PondBank_RVT_Storm"));
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Ripple(TEXT("/Game/DoughWorld/Fab/SoStylized/Effects/Systems/NS_WaterRipple.NS_WaterRipple"));
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Splash(TEXT("/Game/DoughWorld/Fab/SoStylized/Environment/Water/Waterfalls/NS_WaterfallSplash_Calm.NS_WaterfallSplash_Calm"));
    if(Match.Succeeded())MatchedBankMaterial=Match.Object;
    if(Ripple.Succeeded())WaterRippleSystem=Ripple.Object;
    if(Splash.Succeeded())EntrySplashSystem=Splash.Object;
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
    OverlapRange->bDrawOnlyIfSelected=true;
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
    // Native defaults are seeded once above. Never replace saved spline points from OnConstruction.
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
        !GetActorLocation().ContainsNaN()&&GetActorUpVector().Z>.999&&
        (!bUseSplineBoundary||(bBoundaryValid&&AreShapeParametersValid()));
}

void ADWGatheringPond::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if(!HasActorBegunPlay()&&!bWaterRestored&&std::isfinite(InitialReserve)&&InitialReserve>=0.)RemainingWater=InitialReserve;
    RebuildPondGeometry();
}

void ADWGatheringPond::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
    // Loaded maps do not always run construction. Populate the transient outline
    // after serialized native spline components are registered, also in editor.
    if(!IsTemplate()&&GetWorld()&&!GetWorld()->bIsTearingDown&&
        PondBoundary&&PondBoundary->IsRegistered()&&WaterMesh&&WaterMesh->IsRegistered()&&PondBank&&PondBank->IsRegistered())
        RebuildPondGeometry();
}

#if WITH_EDITOR
void ADWGatheringPond::PostEditMove(bool bFinished)
{
    Super::PostEditMove(bFinished);
    // Visualizer edits preview while dragging; this is an editor event, never a Tick rebuild.
    RebuildPondGeometry();
}
void ADWGatheringPond::PostEditUndo()
{
    Super::PostEditUndo();RebuildPondGeometry();
}
#endif

void ADWGatheringPond::RandomizeSceneScale()
{
    if(HasActorBegunPlay()||!FMath::IsFinite(RandomScaleMin)||!FMath::IsFinite(RandomScaleMax)||
        RandomScaleMin<=0.f||RandomScaleMax<RandomScaleMin||RandomScaleMax>100.f)return;
    Modify();SetActorScale3D(FVector(FMath::FRandRange(RandomScaleMin,RandomScaleMax)));
    RefreshRangeBounds();RefreshPlaceholder();
}

void ADWGatheringPond::BeginPlay()
{
    Super::BeginPlay();
    if(auto* GI=GetGameInstance<UDWGameInstance>())GI->MarkUnsupportedGatheringPersistence(GetWorld());
    if(!bWaterRestored&&std::isfinite(InitialReserve)&&InitialReserve>=0.)RemainingWater=InitialReserve;
    OverlapRange->OnComponentBeginOverlap.AddDynamic(this,&ADWGatheringPond::OnPondBeginOverlap);
    OverlapRange->OnComponentEndOverlap.AddDynamic(this,&ADWGatheringPond::OnPondEndOverlap);
    RebuildPondGeometry();
    RefreshPromptAvailability();
}

bool ADWGatheringPond::IsGatheringProfileActive() const
{
    if(!GetWorld()||GetWorld()->bIsTearingDown||IsActorBeingDestroyed())return false;
    auto* GI=GetGameInstance<UDWGameInstance>();
    return GI&&UDWGameplayConfig::AsGatheringConfig(GI->GetConfig())!=nullptr;
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
    return ContainsWorldPoint(Feet,ExtraRadius);
}

bool ADWGatheringPond::ContainsWorldPoint(FVector WorldFeet,double ExtraRadius) const
{
    if(!IsConfigurationValid())return false;
    if(bUseSplineBoundary)
        return DWGatheringPondMath::ContainsPolygonFeet(BoundaryPolygon,GetActorTransform(),WorldFeet,
            WaterHeightCm,SurfaceHeightToleranceCm,ExtraRadius);
    const FVector LocalFeet=GetActorTransform().InverseTransformPositionNoScale(WorldFeet);
    const FVector Scale=GetActorScale3D();
    return DWGatheringPondMath::ContainsFeet(FVector::ZeroVector,LocalFeet,
        RadiusCm*Scale.X,RadiusCm*Scale.Y,SurfaceHeightToleranceCm,ExtraRadius);
}

bool ADWGatheringPond::IsWaterSurfaceHit(const FHitResult& Hit) const
{
    if(!Hit.bBlockingHit||Hit.GetActor()!=this||!IsWetAndOperational())return false;
    const UPrimitiveComponent* Surface=bUseSplineBoundary?static_cast<UPrimitiveComponent*>(WaterMesh.Get()):WaterSurface.Get();
    return Hit.GetComponent()==Surface&&ContainsWorldPoint(Hit.ImpactPoint);
}

bool ADWGatheringPond::IsUsableWaterStation(const AActor* User) const
{
    if(!IsWetAndOperational())return false;
    if(!User)return true;
    if(const auto* Player=Cast<ADWPlayerCharacter>(User))
        if(Player->IsDead()||!UDWGameplayConfig::AsGatheringConfig(Player->GetGameplayConfig()))return false;
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

bool ADWGatheringPond::AreShapeParametersValid() const
{
    return std::isfinite(WaterHeightCm)&&WaterHeightCm>0.&&WaterHeightCm<=1000.&&
        std::isfinite(BedHeightCm)&&BedHeightCm>=-10000.&&BedHeightCm<WaterHeightCm&&
        std::isfinite(OuterGroundHeightCm)&&OuterGroundHeightCm>=-10000.&&OuterGroundHeightCm<=WaterHeightCm+BankCrestAboveWaterCm&&
        std::isfinite(InnerSlopeWidthCm)&&InnerSlopeWidthCm>0.&&InnerSlopeWidthCm<=10000.&&
        std::isfinite(BankCrestAboveWaterCm)&&BankCrestAboveWaterCm>0.&&BankCrestAboveWaterCm<=1000.&&
        std::isfinite(BankCrestDistanceCm)&&BankCrestDistanceCm>0.&&
        std::isfinite(OuterBankWidthCm)&&OuterBankWidthCm>BankCrestDistanceCm&&OuterBankWidthCm<=10000.&&
        std::isfinite(BankGridSpacingCm)&&BankGridSpacingCm>=5.&&BankGridSpacingCm<=1000.&&
        std::isfinite(MaterialTilingCm)&&MaterialTilingCm>0.&&MaterialTilingCm<=100000.&&
        BoundarySamplesPerSegment>=1&&BoundarySamplesPerSegment<=32;
}

void ADWGatheringPond::ResetBoundaryToCircle()
{
    if(!PondBoundary||!std::isfinite(RadiusCm)||RadiusCm<=0.||RadiusCm>100000.)return;
    Modify();PondBoundary->Modify();
    PondBoundary->ClearSplinePoints(false);
    for(int32 I=0;I<12;++I)
    {
        const double Angle=2.*PI*I/12.;
        PondBoundary->AddSplinePoint(FVector(RadiusCm*FMath::Cos(Angle),RadiusCm*FMath::Sin(Angle),0.),ESplineCoordinateSpace::Local,false);
        PondBoundary->SetSplinePointType(I,ESplinePointType::Curve,false);
    }
    PondBoundary->SetClosedLoop(true,true);
    PondBoundary->bSplineHasBeenEdited=true;
    RebuildPondGeometry();
}

void ADWGatheringPond::HideLegacyGeometry()
{
    for(auto* Mesh:{DryBed.Get(),WaterSurface.Get()})
    {
        Mesh->SetVisibility(false);Mesh->SetHiddenInGame(true);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetGenerateOverlapEvents(false);
    }
    // A hidden child still exposes its own PondBoundary in the editor visualizer.
    // Uninstantiate legacy art in spline mode; keep WaterArtClass as the legacy
    // source so RefreshPlaceholder can recreate it when that mode is selected.
    if(WaterArt->GetChildActorClass()||WaterArt->GetChildActor())WaterArt->SetChildActorClass(nullptr);
    WaterArt->SetVisibility(false,true);WaterArt->SetHiddenInGame(true,true);
}

void ADWGatheringPond::RefreshSplineState()
{
    HideLegacyGeometry();
    bIsDry=!std::isfinite(RemainingWater)||RemainingWater<=0.;
    const bool Enabled=!bIsDry&&IsConfigurationValid();
    WaterMesh->SetVisibility(Enabled);WaterMesh->SetHiddenInGame(!Enabled);
    WaterMesh->SetCollisionEnabled(Enabled?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
    const bool BankEnabled=bBoundaryValid&&AreShapeParametersValid()&&
        !GetActorTransform().ContainsNaN()&&GetActorScale3D().GetMin()>0.&&GetActorUpVector().Z>.999;
    PondBank->SetVisibility(BankEnabled);PondBank->SetHiddenInGame(!BankEnabled);
    PondBank->SetCollisionEnabled(BankEnabled?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
    bAppliedDryState=bIsDry;bAppliedSurfaceEnabled=Enabled;
    RefreshEdgeFoamVisibility();
    if(!Enabled)ReleaseAllEnvironmentSlow();
}

void ADWGatheringPond::RefreshRangeBounds()
{
    LastRangeTransform=GetActorTransform();
    if(!bUseSplineBoundary)return;
    if(!bBoundaryValid||!IsConfigurationValid())
    {OverlapRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);return;}
    FBox Bounds(ForceInit);double MaxRadius=0.;
    const FVector Origin=GetActorLocation();
    for(const auto& Point:BoundaryPolygon)
    {
        const FVector World=GetActorTransform().TransformPosition(FVector(Point.X,Point.Y,WaterHeightCm));
        Bounds+=World;MaxRadius=FMath::Max(MaxRadius,FVector2D(World.X-Origin.X,World.Y-Origin.Y).Size());
    }
    const FVector Extent=Bounds.GetExtent();
    OverlapRange->SetWorldLocation(Bounds.GetCenter());
    OverlapRange->SetBoxExtent(FVector(Extent.X+2.,Extent.Y+2.,SurfaceHeightToleranceCm+300.));
    OverlapRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    if(Prompt)
    {
        Prompt->GenericDetectionRadius=static_cast<float>(MaxRadius+StationExtraRadiusCm);
        Prompt->HeightTolerance=static_cast<float>(SurfaceHeightToleranceCm+FMath::Abs(WaterHeightCm*GetActorScale3D().Z)+300.);
    }
}

void ADWGatheringPond::RebuildPondGeometry()
{
    if(EdgeFoamMesh){EdgeFoamMesh->ClearAllMeshSections();EdgeFoamMesh->SetVisibility(false);EdgeFoamMesh->SetHiddenInGame(true);}
    if(!WaterMesh||!PondBank||!PondBoundary)return;
    PondBoundary->SetHiddenInGame(true);
    if(!OverlapRange->bDrawOnlyIfSelected)
    {OverlapRange->bDrawOnlyIfSelected=true;OverlapRange->MarkRenderStateDirty();}
    if(!bUseSplineBoundary)
    {
        bBoundaryValid=false;BoundaryPolygon.Reset();BoundarySampleCount=0;
        BoundaryValidationMessage=TEXT("Legacy circle mode; spline geometry disabled.");
        WaterMesh->ClearAllMeshSections();PondBank->ClearAllMeshSections();
        OverlapRange->SetRelativeLocation(FVector::ZeroVector);
        OverlapRange->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        WaterArt->SetVisibility(true,true);WaterArt->SetHiddenInGame(false,true);
        RefreshPlaceholder();RebuildEdgeFoam();return;
    }
    bBoundaryValid=false;BoundaryPolygon.Reset();BoundarySampleCount=0;
    WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PondBank->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    WaterMesh->ClearAllMeshSections();PondBank->ClearAllMeshSections();
    auto Fail=[this](const FString& Message)
    {
        BoundaryValidationMessage=Message;
        BoundaryPolygon.Reset();BoundarySampleCount=0;bBoundaryValid=false;
        RefreshSplineState();RefreshRangeBounds();RefreshPromptAvailability();
    };
    if(!AreShapeParametersValid()){Fail(TEXT("Invalid pond height, bank width, sampling or material tiling parameters."));return;}
    if(GetActorTransform().ContainsNaN()||GetActorScale3D().GetMin()<=0.||GetActorScale3D().GetMax()>100.||GetActorUpVector().Z<=.999)
    {Fail(TEXT("Use a horizontal pond (yaw only) with positive actor scale no greater than 100."));return;}
    // Display XY handles on the water plane. This sets an absolute component-local
    // Z, never adds an offset or changes serialized control points/tangents.
    FVector BoundaryLocation=PondBoundary->GetRelativeLocation();
    if(!FMath::IsNearlyEqual(BoundaryLocation.Z,WaterHeightCm))
    {BoundaryLocation.Z=WaterHeightCm;PondBoundary->SetRelativeLocation(BoundaryLocation);}
    if(!PondBoundary->IsClosedLoop()){Fail(TEXT("Pond Boundary must be a closed loop."));return;}
    const int32 Controls=PondBoundary->GetNumberOfSplinePoints();
    if(Controls<3||Controls>128||Controls*BoundarySamplesPerSegment>512)
    {Fail(TEXT("Use at least 3 spline points and at most 512 total samples (points x samples per segment)."));return;}
    PondBoundary->UpdateSpline();
    const FTransform SplineToActor=PondBoundary->GetComponentTransform().GetRelativeTransform(GetActorTransform());
    if(SplineToActor.ContainsNaN()){Fail(TEXT("Invalid spline component transform."));return;}
    TArray<FVector2D> Polygon;Polygon.Reserve(Controls*BoundarySamplesPerSegment);
    for(int32 I=0;I<Controls;++I)
        for(int32 J=0;J<BoundarySamplesPerSegment;++J)
        {
            const float Key=static_cast<float>(I)+static_cast<float>(J)/BoundarySamplesPerSegment;
            const FVector Point=SplineToActor.TransformPosition(PondBoundary->GetLocationAtSplineInputKey(Key,ESplineCoordinateSpace::Local));
            Polygon.Emplace(Point.X,Point.Y);
        }
    TArray<int32> WaterTriangles;FString Error;
    if(!DWGatheringPondMath::TriangulatePolygon(Polygon,WaterTriangles,&Error)){Fail(Error);return;}

    // The bank is a heightfield evaluated from signed distance to the SAME polygon.
    // This supports concave outlines without self-crossing offset strips or center fans.
    FBox2D Bounds(ForceInit);for(const auto& P:Polygon)Bounds+=P;
    const FVector2D Min=Bounds.Min-FVector2D(OuterBankWidthCm+BankGridSpacingCm);
    const FVector2D Max=Bounds.Max+FVector2D(OuterBankWidthCm+BankGridSpacingCm);
    const int32 CellsX=FMath::CeilToInt((Max.X-Min.X)/BankGridSpacingCm);
    const int32 CellsY=FMath::CeilToInt((Max.Y-Min.Y)/BankGridSpacingCm);
    if(CellsX<1||CellsY<1||CellsX>192||CellsY>192)
    {Fail(TEXT("Bank grid exceeds 192 cells per side; increase Bank Grid Spacing Cm or reduce the outline."));return;}
    TArray<FVector> BankVertices,BankNormals,WaterVertices,WaterNormals;
    TArray<FVector2D> BankUV,WaterUV;
    TArray<FLinearColor> BankColors,WaterColors;
    TArray<FProcMeshTangent> BankTangents,WaterTangents;
    TArray<double> SignedDistances;
    TArray<int32> BankTriangles;
    const int32 Row=CellsX+1;
    for(int32 Y=0;Y<=CellsY;++Y)
        for(int32 X=0;X<=CellsX;++X)
        {
            const FVector2D Point(Min.X+(Max.X-Min.X)*X/CellsX,Min.Y+(Max.Y-Min.Y)*Y/CellsY);
            const double Distance=DWGatheringPondMath::DistanceToBoundary(Polygon,Point);
            const double Signed=DWGatheringPondMath::ContainsPoint(Polygon,Point)?-Distance:Distance;
            const double Height=DWGatheringPondMath::BankHeight(Signed,WaterHeightCm,BedHeightCm,
                InnerSlopeWidthCm,BankCrestAboveWaterCm,BankCrestDistanceCm,OuterBankWidthCm,OuterGroundHeightCm);
            BankVertices.Emplace(Point.X,Point.Y,Height);BankUV.Add(Point/MaterialTilingCm);
            BankColors.Add(FLinearColor::White);BankTangents.Emplace(1.f,0.f,0.f);SignedDistances.Add(Signed);
        }
    auto AddBankTriangle=[&](int32 A,int32 B,int32 C)
    {
        if(SignedDistances[A]>OuterBankWidthCm&&SignedDistances[B]>OuterBankWidthCm&&SignedDistances[C]>OuterBankWidthCm)return;
        BankTriangles.Append({A,B,C});
    };
    for(int32 Y=0;Y<CellsY;++Y)
        for(int32 X=0;X<CellsX;++X)
        {
            const int32 A=Y*Row+X,B=A+1,C=A+Row,D=C+1;
            AddBankTriangle(A,B,D);AddBankTriangle(A,D,C);
        }
    BankNormals.Init(FVector::ZeroVector,BankVertices.Num());
    for(int32 I=0;I<BankTriangles.Num();I+=3)
    {
        const int32 A=BankTriangles[I],B=BankTriangles[I+1],C=BankTriangles[I+2];
        const FVector Normal=FVector::CrossProduct(BankVertices[B]-BankVertices[A],BankVertices[C]-BankVertices[A]);
        BankNormals[A]+=Normal;BankNormals[B]+=Normal;BankNormals[C]+=Normal;
    }
    for(int32 I=0;I<BankNormals.Num();++I)
    {
        FVector& N=BankNormals[I];if(!N.Normalize())N=FVector::UpVector;
        FVector Tangent=FVector::ForwardVector-N*FVector::DotProduct(N,FVector::ForwardVector);
        if(!Tangent.Normalize())Tangent=FVector::RightVector;
        BankTangents[I]=FProcMeshTangent(Tangent,false);
    }
    for(const auto& P:Polygon)
    {
        WaterVertices.Emplace(P.X,P.Y,WaterHeightCm);WaterNormals.Add(FVector::UpVector);
        WaterUV.Add(P/MaterialTilingCm);WaterColors.Add(FLinearColor::White);WaterTangents.Emplace(1.f,0.f,0.f);
    }
    // Ear clipping and bank normal accumulation use mathematical CCW (+Z cross).
    // UE's procedural front face uses the opposite index winding, as do the engine
    // GenerateBoxMesh top face and CreateGridMeshWelded. Flip only at submission;
    // keep the calculated upward normals and geometry-test convention unchanged.
    for(int32 I=0;I<WaterTriangles.Num();I+=3)Swap(WaterTriangles[I+1],WaterTriangles[I+2]);
    for(int32 I=0;I<BankTriangles.Num();I+=3)Swap(BankTriangles[I+1],BankTriangles[I+2]);
    WaterMesh->CreateMeshSection_LinearColor(0,WaterVertices,WaterTriangles,WaterNormals,WaterUV,WaterColors,WaterTangents,true);
    PondBank->CreateMeshSection_LinearColor(0,BankVertices,BankTriangles,BankNormals,BankUV,BankColors,BankTangents,true);
    WaterMesh->SetMaterial(0,WaterMaterial);PondBank->SetMaterial(0,BankMaterial);
    RefreshBankAppearance();
    BoundaryPolygon=MoveTemp(Polygon);BoundarySampleCount=BoundaryPolygon.Num();bBoundaryValid=true;
    RebuildEdgeFoam();
    BoundaryValidationMessage=FString::Printf(TEXT("Valid: %d boundary samples, %d water triangles; XY outline, local water Z %.1f cm."),
        BoundarySampleCount,WaterTriangles.Num()/3,WaterHeightCm);
    RefreshSplineState();RefreshRangeBounds();RefreshPromptAvailability();
}

void ADWGatheringPond::RefreshPlaceholder()
{
    if(bUseSplineBoundary){RefreshSplineState();return;}
    WaterMesh->SetVisibility(false);WaterMesh->SetHiddenInGame(true);WaterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PondBank->SetVisibility(false);PondBank->SetHiddenInGame(true);PondBank->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DryBed->SetVisibility(true);DryBed->SetHiddenInGame(false);
    const double SafeRadius=std::isfinite(RadiusCm)?FMath::Clamp(RadiusCm,1.,100000.):250.;
    DryBed->SetRelativeScale3D(FVector(SafeRadius/50.*1.05,SafeRadius/50.*1.05,.04));
    DryBed->SetRelativeLocation(FVector(0.,0.,-2.));
    WaterSurface->SetRelativeScale3D(FVector(SafeRadius/50.,SafeRadius/50.,.04));
    WaterSurface->SetRelativeLocation(FVector(0.,0.,2.));
    bIsDry=!std::isfinite(RemainingWater)||RemainingWater<=0.;
    // The invisible cylinder remains the exact water cursor/query proxy.
    // Only the water child hides on depletion; the bank/dry bed remains.
    if(WaterArt->GetChildActorClass()!=WaterArtClass)WaterArt->SetChildActorClass(WaterArtClass);
    WaterArt->SetRelativeLocation(FVector(0.,0.,2.));
    WaterArt->SetRelativeScale3D(FVector(SafeRadius/250.,SafeRadius/250.,1.));
    AActor* ArtActor=WaterArt->GetChildActor();
    const bool bHasArt=IsValid(ArtActor);
    if(bHasArt)
    {
        ArtActor->SetActorHiddenInGame(bIsDry);
        TInlineComponentArray<UPrimitiveComponent*> ArtPrimitives(ArtActor);
        for(UPrimitiveComponent* Primitive:ArtPrimitives)
        {
            Primitive->SetVisibility(!bIsDry);
            Primitive->SetHiddenInGame(bIsDry);
            Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Primitive->SetGenerateOverlapEvents(false);
            Primitive->SetCanEverAffectNavigation(false);
        }
    }
    WaterSurface->SetVisibility(!bIsDry&&!bHasArt);
    WaterSurface->SetHiddenInGame(bIsDry||bHasArt);
    bAppliedDryState=bIsDry;
    RefreshEdgeFoamVisibility();
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
    // Reuse presentation unchanged; authoritative feet/outline range filters coarse proximity.
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
        if((!Player&&!Enemy)||(Player&&(Player->IsDead()||!UDWGameplayConfig::AsGatheringConfig(Player->GetGameplayConfig())))||
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
    // Transform-only changes update broad-phase/query bounds, never retessellate the meshes.
    if(bUseSplineBoundary&&!LastRangeTransform.Equals(GetActorTransform()))RefreshRangeBounds();
    if(bUseSplineBoundary&&bAppliedSurfaceEnabled!=(!bIsDry&&IsConfigurationValid()))RefreshSplineState();
    if(!IsGatheringProfileActive()){ClearPondVFX();ReleaseAllEnvironmentSlow();RefreshPromptAvailability();return;}
    if(GetWorld()->IsPaused()||UDWWorldEventSubsystem::IsPlaying(this)){ClearPondVFX();return;}
    if(!FMath::IsFinite(DeltaSeconds)||DeltaSeconds<0.f)return;
    RefreshOccupants(DeltaSeconds);
    TickPondVFX(DeltaSeconds);
    // Track the applied dry state, independent of an intentionally hidden query proxy.
    if(bAppliedDryState!=bIsDry)RefreshPlaceholder();
    RefreshPromptAvailability();
}

void ADWGatheringPond::OnPondBeginOverlap(UPrimitiveComponent* Component,AActor* OtherActor,
    UPrimitiveComponent* OtherComponent,int32 OtherBodyIndex,bool bFromSweep,const FHitResult& Sweep)
{
    if(!HasAuthority()||!IsWetAndOperational()||!ContainsActor(OtherActor))return;
    const auto* Player=Cast<ADWPlayerCharacter>(OtherActor);
    const auto* Enemy=Cast<ADWEnemyCharacter>(OtherActor);
    if((Player&&!Player->IsDead()&&UDWGameplayConfig::AsGatheringConfig(Player->GetGameplayConfig()))||
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
    if(EdgeFoamMesh){EdgeFoamMesh->SetVisibility(false);EdgeFoamMesh->ClearAllMeshSections();EdgeFoamMesh->SetMaterial(0,nullptr);}
    EdgeFoamMID=nullptr;
    ClearPondVFX();
    ReleaseAllEnvironmentSlow();
    Super::EndPlay(Reason);
}

void ADWGatheringPond::RefreshBankAppearance()
{
    if(!PondBank)return;
    PondBank->SetMaterial(0,BankMaterial);
    UWorld* World=GetWorld();if(!bMatchLandscapeBank||!World||World->bIsTearingDown)return;
    const auto* Enabled=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VirtualTextures"));
    if(!Enabled||Enabled->GetInt()==0)return;
    auto* RVT=LoadObject<URuntimeVirtualTexture>(nullptr,TEXT("/Game/DoughWorld/Fab/SoStylized/Environment/Landscape/Textures/RVT_Landscape.RVT_Landscape"));
    auto* Display=MatchedBankMaterial.Get();
    if(!RVT||!Display)return;
    const FBox Bank=PondBank->Bounds.GetBox();if(!Bank.IsValid)return;
    int32 Writers=0;
    for(TActorIterator<ALandscapeProxy> It(World);It;++It)
    {
        const auto* L=*It;if(!IsValid(L)||!L->RuntimeVirtualTextures.Contains(RVT))continue;
        const FBox B=L->GetComponentsBoundingBox(true);
        if(B.IsValid&&Bank.Min.X>=B.Min.X&&Bank.Min.Y>=B.Min.Y&&Bank.Max.X<=B.Max.X&&Bank.Max.Y<=B.Max.Y)++Writers;
    }
    if(Writers!=1)return;
    int32 Covers=0;
    for(TActorIterator<ARuntimeVirtualTextureVolume> It(World);It;++It)
    {
        auto* Component=It->VirtualTextureComponent.Get();if(!Component||Component->GetVirtualTexture()!=RVT)continue;
        const FTransform T=Component->GetComponentTransform();bool Inside=true;
        for(int32 I=0;I<8;++I)
        {
            const FVector P=T.InverseTransformPosition(FVector((I&1)?Bank.Max.X:Bank.Min.X,(I&2)?Bank.Max.Y:Bank.Min.Y,(I&4)?Bank.Max.Z:Bank.Min.Z));
            if(P.ContainsNaN()||P.GetMin()<0.||P.GetMax()>1.){Inside=false;break;}
        }
        if(Inside)++Covers;
    }
    if(Covers==1)PondBank->SetMaterial(0,Display);
}

void ADWGatheringPond::ClearPondVFX()
{
    for(auto& P:PondPulses)if(P.Key.IsValid())P.Key->DestroyComponent();
    PondPulses.Reset();VisualOccupant.Reset();MovementRippleClock=MovementSplashClock=ShoreRippleClock=VisualSampleClock=0.f;
}
void ADWGatheringPond::SpawnPondRipple(FVector Position,bool bSplash)
{
    if(PondPulses.Num()>=4||Position.ContainsNaN()||!GetWorld()||GetWorld()->GetNetMode()==NM_DedicatedServer)return;
    auto* System=bSplash?EntrySplashSystem.Get():WaterRippleSystem.Get();if(!System)return;
    const float RawScale=bSplash?EntrySplashScale:RippleScale;
    const float Scale=FMath::Clamp(FMath::IsFinite(RawScale)?RawScale:.3f,.01f,10.f);
    auto* Effect=UNiagaraFunctionLibrary::SpawnSystemAtLocation(this,System,Position,FRotator::ZeroRotator,FVector(Scale),false,true,ENCPoolMethod::None,true);
    if(Effect)PondPulses.Emplace(Effect,FMath::Clamp(FMath::IsFinite(RippleLifetime)?RippleLifetime:1.5f,.1f,5.f));
}
void ADWGatheringPond::TickPondVFX(float DeltaSeconds)
{
    for(int32 I=PondPulses.Num()-1;I>=0;--I)
    {
        auto& P=PondPulses[I];P.Value-=DeltaSeconds;
        if(!P.Key.IsValid()||P.Value<=0.f){if(P.Key.IsValid())P.Key->DestroyComponent();PondPulses.RemoveAtSwap(I);}
    }
    if(!bEnablePondVFX||!IsWetAndOperational()||!GetWorld()||GetWorld()->GetNetMode()==NM_DedicatedServer){ClearPondVFX();return;}
    auto* Player=Cast<ADWPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    if(!IsValid(Player)||Player->IsDead()){ClearPondVFX();return;}
    VisualSampleClock+=DeltaSeconds;if(VisualSampleClock<.1f)return;
    const float SampleSeconds=VisualSampleClock;VisualSampleClock=0.f;
    bool Inside=ContainsActor(Player);
    if(Inside)
    {
        TArray<AActor*> Others;Player->GetOverlappingActors(Others,ADWGatheringPond::StaticClass());
        for(auto* A:Others)if(auto* P=Cast<ADWGatheringPond>(A))
            if(P!=this&&P->IsWetAndOperational()&&P->ContainsActor(Player)&&P->GetPathName()<GetPathName()){Inside=false;break;}
    }
    const double Z=bUseSplineBoundary?GetActorTransform().TransformPosition(FVector(0,0,WaterHeightCm)).Z:WaterSurface->GetComponentLocation().Z;
    FVector At=Player->GetActorLocation();At.Z=Z;
    if(Inside&&!VisualOccupant.IsValid()){VisualOccupant=Player;SpawnPondRipple(At,true);}
    else if(!Inside&&VisualOccupant.IsValid()){SpawnPondRipple(LastWaterVFXPosition,true);VisualOccupant.Reset();MovementRippleClock=0.f;}
    if(Inside)LastWaterVFXPosition=At;
    if(Inside&&Player->GetVelocity().SizeSquared2D()>FMath::Square(20.f))
    {
        MovementRippleClock+=SampleSeconds;
        const float Interval=FMath::Clamp(FMath::IsFinite(MovementRippleInterval)?MovementRippleInterval:.4f,.1f,10.f);
        if(MovementRippleClock>=Interval){MovementRippleClock=0.f;SpawnPondRipple(At);}
        MovementSplashClock+=SampleSeconds;
        const float SplashInterval=FMath::Clamp(FMath::IsFinite(MovementSplashInterval)?MovementSplashInterval:1.2f,.3f,10.f);
        if(bMovementSplash&&MovementSplashClock>=SplashInterval){MovementSplashClock=0.f;SpawnPondRipple(At,true);}
    }
    else MovementRippleClock=MovementSplashClock=0.f;
    const float Interval=FMath::Clamp(FMath::IsFinite(ShoreRippleInterval)?ShoreRippleInterval:3.f,.1f,30.f);
    if(FVector::DistSquared2D(Player->GetActorLocation(),GetActorLocation())<FMath::Square(3000.))
    {
        ShoreRippleClock+=SampleSeconds;
        if(ShoreRippleClock>=Interval)
        {
            ShoreRippleClock=0.f;FVector Point;
            if(bUseSplineBoundary&&!BoundaryPolygon.IsEmpty())
            {const FVector2D P=BoundaryPolygon[FMath::RandRange(0,BoundaryPolygon.Num()-1)];Point=GetActorTransform().TransformPosition(FVector(P.X,P.Y,WaterHeightCm));}
            else Point=GetActorTransform().TransformPosition(FVector(RadiusCm,0,2.));
            SpawnPondRipple(Point);
        }
    }
    else ShoreRippleClock=0.f;
}
