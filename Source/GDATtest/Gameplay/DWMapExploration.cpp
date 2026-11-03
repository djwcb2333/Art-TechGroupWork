#include "DWMapExploration.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TextureResource.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

void UDWMapExplorationResource::Prepare()
{
    if(bResolvedAssets)return;
    if(!bRequestedAssets)
    {
        bRequestedAssets=true;
        ParentAsset=FSoftObjectPath(TEXT("/Game/DoughWorld/UI/M_DWMapExplorationImage.M_DWMapExplorationImage"));
        TintAsset=FSoftObjectPath(TEXT("/Game/DoughWorld/UI/M_DWMapVertexTint.M_DWMapVertexTint"));
        CloseAsset=FSoftObjectPath(TEXT("/Game/DoughWorld/UI/Image/T_DW_C04_CloseBrush.T_DW_C04_CloseBrush"));
        TArray<FSoftObjectPath> Paths{ParentAsset.ToSoftObjectPath(),TintAsset.ToSoftObjectPath(),CloseAsset.ToSoftObjectPath()};
        AssetHandle=UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths,FStreamableDelegate());
    }
    // Poll only; no blocking load, GPU wait, callback or forced garbage collection.
    if(AssetHandle&&!AssetHandle->HasLoadCompleted())return;
    TintMaterial=TintAsset.Get();CloseIcon=CloseAsset.Get();
    if(!Material&&ParentAsset.IsValid())
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(DWMapPrepareMIDOnce);
        Material=UMaterialInstanceDynamic::Create(ParentAsset.Get(),this);
        if(Material){Brush.DrawAs=ESlateBrushDrawType::Image;Brush.SetResourceObject(Material);}
    }
    bResolvedAssets=true;
}

namespace DWMapExploration
{
static bool GridValid(int32 N){return N>=32&&N<=MaxGrid&&FMath::IsPowerOfTwo(N);}
static FDWMapImageSettings ImageFrame(const FDWMapExplorationLayer& L)
{FDWMapImageSettings I;I.WorldCenterCm=L.WorldCenterCm;I.WorldSizeCm=L.WorldSizeCm;I.ImageUpYaw=L.ImageUpYaw;return I;}
static bool LayerValid(const FDWMapExplorationLayer& L)
{return L.Schema==1&&GridValid(L.GridSize)&&ImageFrame(L).IsValidCalibration()&&L.Bits.Num()==L.GridSize*L.GridSize/8;}
bool SameFrame(const FDWMapExplorationLayer& L,const FDWMapImageSettings& I,int32 Grid)
{return L.Schema==1&&L.GridSize==Grid&&L.WorldCenterCm==I.WorldCenterCm&&L.WorldSizeCm==I.WorldSizeCm&&FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(double(L.ImageUpYaw),double(I.ImageUpYaw)),.00001);}
bool Validate(const TArray<FDWMapExplorationLayer>& Layers,FString& Error)
{
    if(Layers.Num()>MaxLayers){Error=TEXT("Too many exploration calibration layers.");return false;}
    for(int32 I=0;I<Layers.Num();++I)
    {
        if(!LayerValid(Layers[I])){Error=TEXT("Invalid exploration version, grid, calibration or bit count.");return false;}
        for(int32 J=0;J<I;++J)if(SameFrame(Layers[J],ImageFrame(Layers[I]),Layers[I].GridSize))
        {Error=TEXT("Duplicate exploration calibration layer.");return false;}
    }
    Error.Reset();return true;
}
int32 FindFrame(const TArray<FDWMapExplorationLayer>& Layers,const FDWMapImageSettings& I,int32 N)
{for(int32 K=0;K<Layers.Num();++K)if(SameFrame(Layers[K],I,N))return K;return INDEX_NONE;}
FDWMapExplorationLayer MakeFrame(const FDWMapImageSettings& I,int32 N)
{
    FDWMapExplorationLayer L;if(!I.IsValidCalibration()||!GridValid(N))return L;
    L.Schema=1;L.GridSize=N;L.WorldCenterCm=I.WorldCenterCm;L.WorldSizeCm=I.WorldSizeCm;
    L.ImageUpYaw=FMath::UnwindDegrees(I.ImageUpYaw);L.Bits.SetNumZeroed(N*N/8);return L;
}
uint8 Cell(const FDWMapExplorationLayer& L,int32 X,int32 Y)
{
    if(L.Schema!=1||!GridValid(L.GridSize)||X<0||Y<0||X>=L.GridSize||Y>=L.GridSize||L.Bits.Num()!=L.GridSize*L.GridSize/8)return 0;
    const int32 K=Y*L.GridSize+X;return (L.Bits[K/8]&(1u<<(K%8)))?255:0;
}
int32 FindOrAddFrame(TArray<FDWMapExplorationLayer>& Layers,const FDWMapImageSettings& Image,int32 Grid)
{
    const int32 Existing=FindFrame(Layers,Image,Grid);if(Existing!=INDEX_NONE)return Existing;
    if(Layers.Num()>=MaxLayers)return INDEX_NONE;
    auto Layer=MakeFrame(Image,Grid);if(Layer.Schema!=1)return INDEX_NONE;
    return Layers.Add(MoveTemp(Layer));
}
bool IsRevealed(const FDWMapExplorationLayer& L,FVector2D World)
{
    FVector2D UV=FVector2D::ZeroVector;if(!LayerValid(L)||!ImageFrame(L).WorldToUV(World,UV)||UV.X<0.||UV.Y<0.||UV.X>1.||UV.Y>1.)return false;
    return Cell(L,FMath::Min(L.GridSize-1,FMath::FloorToInt(UV.X*L.GridSize)),FMath::Min(L.GridSize-1,FMath::FloorToInt(UV.Y*L.GridSize)))!=0;
}
bool Reveal(FDWMapExplorationLayer& L,FVector2D World,double R,FIntRect& Dirty)
{
    Dirty=FIntRect(0,0,0,0);if(!LayerValid(L)||!FMath::IsFinite(R)||R<=0.||R>100000.)return false;
    const FDWMapImageSettings I=ImageFrame(L);FVector2D UV=FVector2D::ZeroVector;
    if(!I.WorldToUV(World,UV)||UV.X<0.||UV.Y<0.||UV.X>1.||UV.Y>1.)return false;
    const int32 N=L.GridSize;
    const int32 X0=FMath::Clamp(FMath::FloorToInt((UV.X-R/I.WorldSizeCm.X)*N),0,N-1),Y0=FMath::Clamp(FMath::FloorToInt((UV.Y-R/I.WorldSizeCm.Y)*N),0,N-1);
    const int32 X1=FMath::Clamp(FMath::CeilToInt((UV.X+R/I.WorldSizeCm.X)*N),0,N-1),Y1=FMath::Clamp(FMath::CeilToInt((UV.Y+R/I.WorldSizeCm.Y)*N),0,N-1);
    bool Changed=false;
    for(int32 Y=Y0;Y<=Y1;++Y)for(int32 X=X0;X<=X1;++X)
    {
        const double DX=FMath::Max(0.,FMath::Abs((X+.5)/N-UV.X)*I.WorldSizeCm.X-I.WorldSizeCm.X/(2.*N));
        const double DY=FMath::Max(0.,FMath::Abs((Y+.5)/N-UV.Y)*I.WorldSizeCm.Y-I.WorldSizeCm.Y/(2.*N));
        if(DX*DX+DY*DY>R*R||Cell(L,X,Y))continue;
        const int32 K=Y*N+X;L.Bits[K/8]|=uint8(1u<<(K%8));
        if(!Changed)Dirty=FIntRect(X,Y,X+1,Y+1);
        else {Dirty.Min.X=FMath::Min(Dirty.Min.X,X);Dirty.Min.Y=FMath::Min(Dirty.Min.Y,Y);Dirty.Max.X=FMath::Max(Dirty.Max.X,X+1);Dirty.Max.Y=FMath::Max(Dirty.Max.Y,Y+1);}
        Changed=true;
    }
    return Changed;
}
}

bool UDWMapExplorationResource::Sync(const FDWMapExplorationLayer& L,UTexture2D* Base,const FIntRect& Dirty)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(DWMapExplorationDirtyUpdate);
    if(!IsInGameThread()||L.Schema!=1||L.GridSize<32||L.GridSize>DWMapExploration::MaxGrid||L.Bits.Num()!=L.GridSize*L.GridSize/8)return false;
    const int32 N=L.GridSize;
    const bool ChangedFrame=!Mask||Frame.GridSize!=N||Frame.WorldCenterCm!=L.WorldCenterCm||Frame.WorldSizeCm!=L.WorldSizeCm||Frame.ImageUpYaw!=L.ImageUpYaw;
    if(ChangedFrame)
    {
        BoundImage=nullptr; // Never show another frame's picture while its replacement loads.
        Mask=UTexture2D::CreateTransient(N,N,PF_B8G8R8A8);if(!Mask)return false;
        Mask->SRGB=false;Mask->NeverStream=true;Mask->Filter=TF_Bilinear;Mask->AddressX=TA_Clamp;Mask->AddressY=TA_Clamp;
        auto& Mip=Mask->GetPlatformData()->Mips[0];void* Data=Mip.BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memzero(Data,N*N*4);Mip.BulkData.Unlock();Mask->UpdateResource();Frame=L;
    }
    FIntRect R=ChangedFrame?FIntRect(0,0,N,N):Dirty;
    R.Min.X=FMath::Clamp(R.Min.X,0,N);R.Min.Y=FMath::Clamp(R.Min.Y,0,N);R.Max.X=FMath::Clamp(R.Max.X,R.Min.X,N);R.Max.Y=FMath::Clamp(R.Max.Y,R.Min.Y,N);
    if(R.Width()>0&&R.Height()>0)
    {
        if(!Mask->GetResource())return false;
        const int32 W=R.Width(),H=R.Height();uint8* Bytes=new uint8[W*H*4];
        for(int32 Y=0;Y<H;++Y)for(int32 X=0;X<W;++X)
        {const uint8 C=DWMapExploration::Cell(L,X+R.Min.X,Y+R.Min.Y);const int32 K=(Y*W+X)*4;Bytes[K]=Bytes[K+1]=Bytes[K+2]=C;Bytes[K+3]=255;}
        auto* Region=new FUpdateTextureRegion2D(R.Min.X,R.Min.Y,0,0,W,H);
        // Buffers outlive queued rendering even when this World closes.
        Mask->UpdateTextureRegions(0,1,Region,W*4,4,Bytes,[](uint8* B,const FUpdateTextureRegion2D* P){delete[] B;delete P;});
    }
    if(Material)
    {
        if(BoundMask.Get()!=Mask.Get()){BoundMask=Mask;Material->SetTextureParameterValue(TEXT("ExploreMask"),Mask);}
        if(IsValid(Base)&&BoundImage!=Base){BoundImage=Base;Material->SetTextureParameterValue(TEXT("BaseImage"),Base);}
    }
    return true;
}
UMaterialInterface* UDWMapExplorationResource::GetMaterial() const
{return Material&&Mask&&BoundImage?Material.Get():nullptr;}
const FSlateBrush* UDWMapExplorationResource::GetBrush() const
{return GetMaterial()?&Brush:nullptr;}
void UDWMapExplorationResource::WarmMask(const FDWMapImageSettings& Image,int32 Grid)
{
    if(Mask&&DWMapExploration::SameFrame(Frame,Image,Grid))return;
    // Rendering-only blank mask prepared before M. No exploration/save facts are added here.
    auto Empty=DWMapExploration::MakeFrame(Image,Grid);
    if(Empty.Schema==1)Sync(Empty,nullptr,FIntRect(0,0,Grid,Grid));
}
