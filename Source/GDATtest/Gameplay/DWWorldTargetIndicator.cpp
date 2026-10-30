#include "DWWorldTargetIndicator.h"
#include "DWGameplayHUD.h"
#include "DWGameplayWidget.h"
#include "DWPlayerController.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWMinimapNavigation.h"
#include "DWWorldEvent.h"
#include "DWMapMotionAdapter.h"
#include "DWMapVisualMath.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "LandscapeProxy.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "SceneView.h"
#include "UnrealClient.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
bool GroundRingBasis(const FVector& Normal,FVector& A,FVector& B)
{
    if(Normal.ContainsNaN()||Normal.IsNearlyZero())return false;
    const FVector N=Normal.GetSafeNormal();
    A=FVector::CrossProduct(N,FVector::RightVector).GetSafeNormal();
    if(A.IsNearlyZero())A=FVector::CrossProduct(N,FVector::ForwardVector).GetSafeNormal();
    B=FVector::CrossProduct(N,A).GetSafeNormal();
    return !A.IsNearlyZero()&&!B.IsNearlyZero();
}
bool IsTerrainHit(const FHitResult& Hit)
{
    const AActor* Actor=Hit.GetActor();const UPrimitiveComponent* Component=Hit.GetComponent();
    if(!IsValid(Actor)||!IsValid(Component))return false;
    if(Actor->IsA<ALandscapeProxy>()||Actor->ActorHasTag(TEXT("DWWorldMapInclude"))||Component->ComponentHasTag(TEXT("DWWorldMapInclude")))return true;
    const auto* Mesh=Cast<UStaticMeshComponent>(Component);
    if(!Actor->IsA<AStaticMeshActor>()||!Mesh||!Mesh->GetStaticMesh())return false;
    const FString Path=Mesh->GetStaticMesh()->GetPathName();
    return Path.StartsWith(TEXT("/Game/DoughWorld/Fab/SoStylized/Environment/Rocks/"),ESearchCase::IgnoreCase)||
        Path.StartsWith(TEXT("/Engine/BasicShapes/Cube."),ESearchCase::IgnoreCase);
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMapGroundRingBasisTest,"DoughWorld.UI.Map.GroundRingBasis",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWMapGroundRingBasisTest::RunTest(const FString& Parameters)
{
    FVector A,B;const FVector N=FVector(0.,.6,.8).GetSafeNormal();
    TestTrue(TEXT("Sloped surface has a basis"),GroundRingBasis(N,A,B));
    TestTrue(TEXT("First axis lies on surface"),FMath::Abs(FVector::DotProduct(A,N))<1.e-6);
    TestTrue(TEXT("Second axis lies on surface"),FMath::Abs(FVector::DotProduct(B,N))<1.e-6);
    TestTrue(TEXT("Both axes are unit and perpendicular"),FMath::IsNearlyEqual(A.Size(),1.,1.e-6)&&FMath::IsNearlyEqual(B.Size(),1.,1.e-6)&&FMath::Abs(FVector::DotProduct(A,B))<1.e-6);
    TestFalse(TEXT("Zero normal is rejected"),GroundRingBasis(FVector::ZeroVector,A,B));
    return true;
}
#endif

void UDWWorldTargetIndicator::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if(WidgetTree&&!WidgetTree->RootWidget)WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("WorldTargetRoot"));
    SetVisibility(ESlateVisibility::HitTestInvisible);ForceVolatile(true);
}
void UDWWorldTargetIndicator::InitializeIndicator(ADWGameplayHUD* InHUD){HUD=InHUD;}

void UDWWorldTargetIndicator::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);bDisplayValid=false;GroundRingPoints.Reset();
    auto* H=HUD.Get();auto* PC=Cast<ADWPlayerController>(GetOwningPlayer());UWorld* W=GetWorld();
    auto* Pawn=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
    const auto* GI=W?W->GetGameInstance<UDWGameInstance>():nullptr;
    const auto* Loading=GI?GI->GetSubsystem<UDWLoadingTransitionSubsystem>():nullptr;
    const auto* UI=H?H->GetGameplayWidget():nullptr;
    if(!H||!PC||!W||W->bIsTearingDown||!Pawn||Pawn->IsDead()||!PC->IsLocalController()||PC->GetViewTarget()!=Pawn||
       !H->IsSessionStarted()||H->IsBlockingGameplay()||PC->IsGameplayBlocked()||Pawn->IsCombatRestoreInProgress()||
       UDWWorldEventSubsystem::IsPlaying(this)||(GI&&GI->IsPlayerRestorePending())||(Loading&&Loading->IsTransitionActive())||
       (UI&&UI->bUseC01HUD&&!UI->GetC01CinematicOffset().IsNearlyZero(.5)))return;
    auto* MapNavigation=W->GetSubsystem<UDWMinimapNavigationSubsystem>();if(!MapNavigation){bTargetValid=false;return;}
    const TWeakObjectPtr<UDWMinimapNavigationSubsystem> WeakNavigation(MapNavigation);
    const uint64 NavigationVersion=MapNavigation->GetNavigationRevision();
    const auto Target=MapNavigation->GetNavigationTarget();
    if(!WeakNavigation.IsValid()||WeakNavigation->GetNavigationRevision()!=NavigationVersion||!IsValid(H)||!IsValid(PC)||!Target.bValid||Target.NavigationPosition.ContainsNaN())
    {bTargetValid=false;TargetActor.Reset();return;}
    auto* CurrentActor=Target.bIsActorTarget?MapNavigation->GetTargetMarker():nullptr;
    if(!IsValid(W)||W->bIsTearingDown||!WeakNavigation.IsValid()||WeakNavigation->GetNavigationRevision()!=NavigationVersion||!IsValid(PC)||!IsValid(H)||PC->GetWorld()!=W||H->GetWorld()!=W)return;
    const auto Current=MapNavigation->GetNavigationTarget();
    if(!Current.bValid||Current.bIsActorTarget!=Target.bIsActorTarget||!Current.NavigationPosition.Equals(Target.NavigationPosition,.01)||
       !IsValid(W)||W->bIsTearingDown||!WeakNavigation.IsValid()||WeakNavigation->GetNavigationRevision()!=NavigationVersion||!IsValid(PC)||!IsValid(H))return;
    const bool bChanged=!bTargetValid||bActorTarget!=Target.bIsActorTarget||
        (Target.bIsActorTarget?TargetActor.Get()!=CurrentActor:!TargetWorld.Equals(Target.NavigationPosition,.1));
    if(bChanged){SelectedAt=FPlatformTime::Seconds();PreviousDirection=FVector2D(0.,-1.);bHasGround=false;LastGroundQuery=0.;}
    bTargetValid=true;bActorTarget=Target.bIsActorTarget;TargetActor=CurrentActor;TargetWorld=Target.NavigationPosition;
    const double Now=FPlatformTime::Seconds();
    if(LastGroundQuery<=0.||!GroundQueryTarget.Equals(TargetWorld,50.)||Now-LastGroundQuery>=1.)
    {
        GroundQueryTarget=TargetWorld;LastGroundQuery=Now;bHasGround=false;
        FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DWWorldTargetGround),false);Params.AddIgnoredActor(Pawn);
        if(IsValid(CurrentActor)&&IsValid(CurrentActor->GetOwner()))Params.AddIgnoredActor(CurrentActor->GetOwner());
        const FVector Up(0.,0.,50000.);
        // Skip roofs and other non-terrain actors rather than anchoring the
        // marker to a high object above the map's chosen terrain point.
        for(int32 Attempt=0;Attempt<16;++Attempt)
        {
            FHitResult Hit;if(!W->LineTraceSingleByObjectType(Hit,TargetWorld+Up,TargetWorld-Up,Objects,Params))break;
            if(IsTerrainHit(Hit)&&!Hit.ImpactPoint.ContainsNaN()&&!Hit.ImpactNormal.ContainsNaN()&&Hit.ImpactNormal.Z>.1)
            {GroundWorld=Hit.ImpactPoint;GroundNormal=Hit.ImpactNormal.GetSafeNormal();bHasGround=!GroundNormal.IsNearlyZero();break;}
            AActor* Blocker=Hit.GetActor();if(!IsValid(Blocker))break;Params.AddIgnoredActor(Blocker);
        }
    }
    const ULocalPlayer* Local=PC->GetLocalPlayer();
    if(!Local||!Local->ViewportClient||!Local->ViewportClient->Viewport)return;
    FViewport* Viewport=Local->ViewportClient->Viewport;FSceneViewProjectionData Data;
    if(!Local->GetProjectionData(Viewport,Data))return;
    const FIntPoint Pixels=Viewport->GetSizeXY();const FVector2D LocalSize=Geometry.GetLocalSize();
    const FVector2D PlayerOrigin(double(Pixels.X)*Local->Origin.X,double(Pixels.Y)*Local->Origin.Y);
    const FVector2D PlayerSize(double(Pixels.X)*Local->Size.X,double(Pixels.Y)*Local->Size.Y);
    if(Pixels.X<=0||Pixels.Y<=0||PlayerSize.X<=0.||PlayerSize.Y<=0.||LocalSize.X<=0.||LocalSize.Y<=0.||LocalSize.ContainsNaN())return;
    const FIntRect Rect=Data.GetConstrainedViewRect();DWMapVisualMath::Rect LocalView;
    if(!DWMapVisualMath::PlayerPixelsToLocal({PlayerOrigin.X,PlayerOrigin.Y,PlayerOrigin.X+PlayerSize.X,PlayerOrigin.Y+PlayerSize.Y},
       {double(Rect.Min.X),double(Rect.Min.Y),double(Rect.Max.X),double(Rect.Max.Y)},
       {LocalSize.X,LocalSize.Y},LocalView))return;
    const FVector2D Origin(LocalView.Left,LocalView.Top),Size(LocalView.Right-LocalView.Left,LocalView.Bottom-LocalView.Top);
    const double Inset=FMath::IsFinite(ScreenInset)?FMath::Clamp(double(ScreenInset),0.,FMath::Max(0.,FMath::Min(Size.X,Size.Y)*.5-1.)):38.;
    const FVector2D SafeOrigin=Origin+FVector2D(Inset,Inset),SafeSize=Size-FVector2D(2.*Inset,2.*Inset);
    const FVector DisplayWorld=bHasGround?GroundWorld:TargetWorld;
    const auto VP=Data.ComputeViewProjectionMatrix();
    const FVector4 Clip=VP.TransformFVector4(FVector4(DisplayWorld,1.));
    const double Depth=Data.ViewRotationMatrix.TransformVector(DisplayWorld-Data.ViewOrigin).Z;
    const auto Projection=DWMapMotionAdapter::ProjectClipWithCameraDepth(Clip.X,Clip.Y,Clip.W,Depth,Origin,Size,SafeOrigin,SafeSize,PreviousDirection);
    if(Projection.Kind==DWMapMotionMath::TargetKind::Invalid)return;
    DisplayPosition=FVector2D(Projection.Position.X,Projection.Position.Y);
    bDisplayInside=Projection.Kind==DWMapMotionMath::TargetKind::Inside;
    if(!bDisplayInside){DisplayDirection=FVector2D(Projection.Direction.X,Projection.Direction.Y);PreviousDirection=DisplayDirection;}
    if(bDisplayInside)
    {
        // Draw a world-plane ring only when a real static surface was found;
        // navigation and stored marker heights remain unchanged.
        if(!bHasGround)return;
        const double Period=FMath::IsFinite(PulseInterval)?FMath::Clamp(double(PulseInterval),.25,10.):1.8;
        const double Scale=DWMapVisualMath::Bounce(std::fmod(FMath::Max(0.,Now-SelectedAt),Period),PulseAmplitude,PulseFrequency,PulseDamping);
        const double Radius=FMath::IsFinite(GroundRingRadius)?FMath::Clamp(double(GroundRingRadius),25.,2000.):250.;
        const double Offset=FMath::IsFinite(GroundRingOffset)?FMath::Clamp(double(GroundRingOffset),0.,50.):4.;
        FVector AxisA,AxisB;if(!GroundRingBasis(GroundNormal,AxisA,AxisB))return;
        constexpr int32 Count=64;GroundRingPoints.Reserve(Count+1);
        for(int32 I=0;I<=Count;++I)
        {
            const double A=2.*PI*double(I)/Count;
            const FVector Point=GroundWorld+GroundNormal*Offset+(AxisA*FMath::Cos(A)+AxisB*FMath::Sin(A))*Radius*Scale;
            const FVector4 C=VP.TransformFVector4(FVector4(Point,1.));
            const double D=Data.ViewRotationMatrix.TransformVector(Point-Data.ViewOrigin).Z;
            if(!FMath::IsFinite(C.X)||!FMath::IsFinite(C.Y)||!FMath::IsFinite(C.W)||!FMath::IsFinite(D)||C.W<=1.e-6||D<=1.e-6)
            {GroundRingPoints.Reset();return;}
            const FVector2D P=Origin+FVector2D((C.X/C.W*.5+.5)*Size.X,(.5-C.Y/C.W*.5)*Size.Y);
            if(P.ContainsNaN()||FMath::Abs(P.X)>100000.||FMath::Abs(P.Y)>100000.){GroundRingPoints.Reset();return;}
            GroundRingPoints.Add(FVector2f(P));
        }
    }
    bDisplayValid=true;
}

int32 UDWWorldTargetIndicator::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
    Layer=Super::NativePaint(Args,Geometry,Cull,Elements,Layer,Style,ParentEnabled);if(!bDisplayValid)return Layer;
    const float Thickness=FMath::IsFinite(RingThickness)?FMath::Clamp(RingThickness,1.f,12.f):4.f;
    const float Radius=FMath::IsFinite(RingRadius)?FMath::Clamp(RingRadius,4.f,120.f):22.f;
    if(bDisplayInside)
    {
        if(GroundRingPoints.Num()>1)FSlateDrawElement::MakeLines(Elements,++Layer,Geometry.ToPaintGeometry(),GroundRingPoints,ESlateDrawEffect::None,FLinearColor::White,true,Thickness);
    }
    else
    {
        const FVector2D D=DisplayDirection.GetSafeNormal(),Side(-D.Y,D.X),Tip=DisplayPosition;
        const double L=FMath::Clamp(double(Radius)*.8,10.,40.);
        const TArray<FVector2f> Arrow={FVector2f(Tip-D*L+Side*L*.55),FVector2f(Tip),FVector2f(Tip-D*L-Side*L*.55)};
        FSlateDrawElement::MakeLines(Elements,++Layer,Geometry.ToPaintGeometry(),Arrow,ESlateDrawEffect::None,FLinearColor::White,true,Thickness);
    }
    return Layer;
}
