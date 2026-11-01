#include "DWMapImage.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "TextureResource.h"

FDWMapImageSettings::FDWMapImageSettings()
{Image=FSoftObjectPath(TEXT("/Game/DoughWorld/UI/Image/T_DWMapPlaceholderWhite.T_DWMapPlaceholderWhite"));}
bool FDWMapImageSettings::IsValidCalibration() const
{return !WorldCenterCm.ContainsNaN()&&!WorldSizeCm.ContainsNaN()&&FMath::IsFinite(ImageUpYaw)&&
    WorldCenterCm.GetAbsMax()<=100000000.&&WorldSizeCm.X>=1.&&WorldSizeCm.Y>=1.&&WorldSizeCm.GetMax()<=20000000.;}
bool FDWMapImageSettings::SameAs(const FDWMapImageSettings& O) const
{return Image.ToSoftObjectPath()==O.Image.ToSoftObjectPath()&&WorldCenterCm==O.WorldCenterCm&&WorldSizeCm==O.WorldSizeCm&&ImageUpYaw==O.ImageUpYaw;}
bool FDWMapImageSettings::WorldToUV(FVector2D P,FVector2D& UV) const
{
    if(!IsValidCalibration()||P.ContainsNaN())return false;
    const FVector2D N(FRotator(0.,ImageUpYaw,0.).Vector());const FVector2D E(-N.Y,N.X),D=P-WorldCenterCm;
    UV=FVector2D(.5+FVector2D::DotProduct(D,E)/WorldSizeCm.X,.5-FVector2D::DotProduct(D,N)/WorldSizeCm.Y);return !UV.ContainsNaN();
}
bool FDWMapImageSettings::UVToWorld(FVector2D UV,FVector2D& P) const
{
    if(!IsValidCalibration()||UV.ContainsNaN())return false;
    const FVector2D N(FRotator(0.,ImageUpYaw,0.).Vector()),E(-N.Y,N.X);
    P=WorldCenterCm+E*((UV.X-.5)*WorldSizeCm.X)+N*((.5-UV.Y)*WorldSizeCm.Y);return !P.ContainsNaN();
}
double FDWMapImageSettings::WidthAtViewYaw(double Yaw) const
{
    if(!IsValidCalibration()||!FMath::IsFinite(Yaw))return 0.;
    const double R=FMath::DegreesToRadians(double(ImageUpYaw)-Yaw),C=FMath::Abs(FMath::Cos(R)),S=FMath::Abs(FMath::Sin(R));
    return FMath::Max(WorldSizeCm.X*C+WorldSizeCm.Y*S,WorldSizeCm.X*S+WorldSizeCm.Y*C);
}
void UDWMapImageResource::Request(const FDWMapImageSettings& Settings)
{
    if(bRequested&&Current.SameAs(Settings))return;
    Cancel();Current=Settings;bRequested=true;
    if(!Current.IsValidCalibration()||Current.Image.IsNull())return;
    Texture=Current.Image.Get();
    if(!Texture)Handle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Current.Image.ToSoftObjectPath(),FStreamableDelegate());
}
void UDWMapImageResource::Cancel()
{
    ++Generation;bRequested=false;Texture=nullptr;
    if(Handle)Handle->CancelHandle();Handle.Reset();
}
UTexture2D* UDWMapImageResource::GetReadyTexture() const
{
    if(!bRequested||!IsValid(Texture))return nullptr;
#if WITH_EDITOR
    if(Texture->IsCompiling())return nullptr;
#endif
    const FTextureResource* R=Texture->GetResource();
    return R&&R->IsInitialized()&&R->TextureRHI.IsValid()?Texture.Get():nullptr;
}
bool UDWMapImageResource::Poll(bool& Ready,FString& Error)
{
    Ready=false;
    if(!bRequested||!Current.IsValidCalibration()||Current.Image.IsNull()){Error=TEXT("Invalid map image or world calibration.");return false;}
    if(!Texture)
    {
        if(!Handle){Error=TEXT("Map image load could not be requested.");return false;}
        if(!Handle->HasLoadCompleted()){Error.Reset();return true;}
        Texture=Current.Image.Get();
        if(!Texture){Error=TEXT("Map image is missing or is not a Texture2D.");return false;}
    }
    Ready=GetReadyTexture()!=nullptr;Error.Reset();return true;
}
void UDWMapImageResource::BeginDestroy(){Cancel();Super::BeginDestroy();}
bool DWMapImageMath::ScreenToWorld(const DWMapMotionMath::View& V,FVector2D Pixel,FVector2D& World)
{
    if(!DWMapMotionMath::Valid(V)||Pixel.ContainsNaN()||FMath::Abs(V.Pitch+90.)>.001)return false;
    const double X=(Pixel.X-V.Frame.X-V.Frame.W*.5)*V.UnitsPerPixel,Y=(V.Frame.Y+V.Frame.H*.5-Pixel.Y)*V.UnitsPerPixel;
    World=FVector2D(V.Center.X+V.Right.X*X+V.Up.X*Y,V.Center.Y+V.Right.Y*X+V.Up.Y*Y);return !World.ContainsNaN();
}
