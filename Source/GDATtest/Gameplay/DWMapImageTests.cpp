#include "DWMapImage.h"
#include "DWWorldMapMath.h"
#include "DWMapOpenPhases.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMapImageCalibrationTest,"DoughWorld.UI.Map.ImageCalibration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMapImageCalibrationTest::RunTest(const FString&)
{
    FDWMapImageSettings S;S.WorldCenterCm=FVector2D(1500,-2300);S.WorldSizeCm=FVector2D(24000,8000);
    for(const float Yaw:{0.f,37.f,90.f,-140.f})
    {
        S.ImageUpYaw=Yaw;
        for(const FVector2D UV:{FVector2D(0,0),FVector2D(1,1),FVector2D(.21,.83),FVector2D(.5,.5)})
        {FVector2D P,Round;TestTrue(TEXT("UV to world"),S.UVToWorld(UV,P));TestTrue(TEXT("World to UV"),S.WorldToUV(P,Round));TestTrue(TEXT("Non-square rotated roundtrip"),Round.Equals(UV,1e-6));}
        FVector2D Top;S.UVToWorld(FVector2D(.5,0),Top);const FVector2D N(FRotator(0,Yaw,0).Vector());
        TestTrue(TEXT("Image top points to configured north"),(Top-S.WorldCenterCm).Equals(N*4000.,1e-4));
    }
    S.WorldSizeCm.X=0.;TestFalse(TEXT("Zero coverage rejected"),S.IsValidCalibration());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMapImageViewTest,"DoughWorld.UI.Map.ImageViewAlignment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMapImageViewTest::RunTest(const FString&)
{
    FDWMapImageSettings S;S.WorldSizeCm=FVector2D(30000,10000);S.ImageUpYaw=31.;
    for(const double Yaw:{0.,31.,76.,120.})for(const double Width:{248.,1920.})
    {
        DWMapMotionMath::View V;TestTrue(TEXT("Flat image view"),DWMapMotionMath::Make({100.,200.,10.},{12.,25.,Width,Width*.75},45.,Yaw,-90.,V));
        const FVector World(876.,-1234.,10.);DWMapMotionMath::P2 Pixel;DWMapMotionMath::WorldToScreen(V,{World.X,World.Y,World.Z},Pixel);
        FVector2D Back,UV1,UV2;TestTrue(TEXT("Screen inverse"),DWMapImageMath::ScreenToWorld(V,FVector2D(Pixel.X,Pixel.Y),Back));
        S.WorldToUV(FVector2D(World),UV1);S.WorldToUV(Back,UV2);TestTrue(TEXT("Image UV and marker world agree across sizes/yaw"),UV1.Equals(UV2,1e-6));
        DWWorldMapMath::View MapView;MapView.CaptureCenter={S.WorldCenterCm.X,S.WorldCenterCm.Y};MapView.Center=MapView.CaptureCenter;
        MapView.CaptureWidth=S.WidthAtViewYaw(Yaw);MapView.NorthYaw=Yaw;MapView.Viewport={Width,Width*.75};
        TestTrue(TEXT("Calibrated image fit is finite and positive"),DWWorldMapMath::Valid(MapView));
        for(const FVector2D Corner:{FVector2D(0,0),FVector2D(1,0),FVector2D(1,1),FVector2D(0,1)})
        {FVector2D P;S.UVToWorld(Corner,P);DWWorldMapMath::Point PixelCorner;
         TestTrue(TEXT("Image corner projects"),DWWorldMapMath::WorldToPixel(MapView,{P.X,P.Y},PixelCorner));
         TestTrue(TEXT("Every rotated non-square image corner fits full viewport"),PixelCorner.X>=-1e-5&&PixelCorner.Y>=-1e-5&&PixelCorner.X<=MapView.Viewport.X+1e-5&&PixelCorner.Y<=MapView.Viewport.Y+1e-5);}
    }
    DWMapOpenPhases::Timeline T;T.Reset(.14,.32,.18);for(int32 I=0;I<10;++I)T.Advance(.1,false,false);TestFalse(TEXT("Image not ready cannot complete"),T.Read(false).Complete);
    for(int32 I=0;I<10;++I)T.Advance(.1,false,true);TestTrue(TEXT("Ready image allows content completion"),T.Read(true).Complete);
    for(int32 I=0;I<10;++I)T.Advance(.1,true,true);TestTrue(TEXT("Rapid reverse closes"),T.Read(true).Closed);
    return true;
}
#endif
