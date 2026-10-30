#include "DWMapTransitionCapture.h"
#include "DWMinimap.h"
#include "DWWorldMap.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "RHICommandList.h"
#include "DynamicRHI.h"
#include "RenderingThread.h"

namespace
{
    bool Finite(float V){return FMath::IsFinite(V);}
    bool Finite(const FLinearColor& C){return Finite(C.R)&&Finite(C.G)&&Finite(C.B)&&Finite(C.A);}
    FVector Vec(DWMapMotionMath::P3 P){return FVector(P.X,P.Y,P.Z);}
    void Configure(USceneCaptureComponent2D* To,const USceneCaptureComponent2D* From)
    {
        To->bCaptureEveryFrame=false;To->bCaptureOnMovement=false;To->bAlwaysPersistRenderingState=false;
        To->ProjectionType=ECameraProjectionMode::Orthographic;To->CaptureSource=ESceneCaptureSource::SCS_SceneDepth;
        To->DetailMode=EDetailMode::DM_Low;
        To->bAutoCalculateOrthoPlanes=false;To->bUpdateOrthoPlanes=false;To->PostProcessBlendWeight=0.f;
        To->ShowFlags=From->ShowFlags;To->PrimitiveRenderMode=From->PrimitiveRenderMode;
        To->HiddenActors=From->HiddenActors;To->ShowOnlyActors=From->ShowOnlyActors;To->ShowOnlyComponents=From->ShowOnlyComponents;
    }
    void ConfigureMID(UMaterialInstanceDynamic* MID,UTextureRenderTarget2D* RT,const DWMapMotionAdapter::FFrame& Frame,
        const FVector& Forward,const FVector& Origin,float Ground,FLinearColor GroundTint,FLinearColor SurfaceTint,FLinearColor WallTint,float Samples)
    {
        MID->SetTextureParameterValue(TEXT("WorldMapDepth"),RT);
        MID->SetVectorParameterValue(TEXT("WorldMapView"),FLinearColor(Frame.MaterialUVScale.X,Frame.MaterialUVScale.Y,.5f,.5f));
        MID->SetVectorParameterValue(TEXT("CaptureRight"),FLinearColor(Frame.View.Right.X,Frame.View.Right.Y,Frame.View.Right.Z,0));
        MID->SetVectorParameterValue(TEXT("CaptureUp"),FLinearColor(Frame.View.Up.X,Frame.View.Up.Y,Frame.View.Up.Z,0));
        MID->SetVectorParameterValue(TEXT("CaptureForward"),FLinearColor(Forward.X,Forward.Y,Forward.Z,0));
        MID->SetVectorParameterValue(TEXT("CaptureInfo"),FLinearColor(Frame.OrthoWidth,Frame.View.NorthScale,Origin.Z,Ground));
        MID->SetVectorParameterValue(TEXT("GroundTint"),GroundTint);MID->SetVectorParameterValue(TEXT("SurfaceTint"),SurfaceTint);
        MID->SetVectorParameterValue(TEXT("WallTint"),WallTint);MID->SetScalarParameterValue(TEXT("EdgeSamples"),Samples);
    }
}

bool UDWMapTransitionCapture::Initialize(APlayerController* PC,UDWMinimapComponent* Mini,UDWWorldMapSubsystem* Map,
    const DWMapMotionAdapter::FFrame& Start,const DWMapMotionAdapter::FFrame& End,int32 RequestedResolution,FString& Error)
{
    Release();UWorld* W=PC?PC->GetWorld():nullptr;
    const USceneCaptureComponent2D* MiniSource=Mini?Mini->GetMotionCaptureComponent():nullptr;
    const USceneCaptureComponent2D* FullSource=Map?Map->GetMotionCaptureComponent():nullptr;
    UMaterialInstanceDynamic* MiniOriginal=Mini?Mini->GetDisplayMaterial():nullptr;
    auto* FullOriginal=Map?Cast<UMaterialInstanceDynamic>(Map->GetMapMaterial()):nullptr;
    UMaterialInterface* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DoughWorld/UI/M_DWMapMotionClay.M_DWMapMotionClay"));
    if(!W||W->bIsTearingDown||!PC->IsLocalController()||!IsValid(MiniSource)||!IsValid(FullSource)||!IsValid(MiniOriginal)||!IsValid(FullOriginal)||!Base||
       !DWMapMotionMath::Valid(Start.View)||!DWMapMotionMath::Valid(End.View))
    {Error=TEXT("地图过渡缺少当前捕获或材质。 / Map transition capture is unavailable.");return false;}
    OwnerWorld=W;StartFrame=Start;EndFrame=End;Committed=Start;
    const FLinearColor MiniInfo=MiniOriginal->K2_GetVectorParameterValue(TEXT("CaptureInfo")),FullInfo=FullOriginal->K2_GetVectorParameterValue(TEXT("CaptureInfo"));
    GroundTint=MiniOriginal->K2_GetVectorParameterValue(TEXT("GroundTint"));SurfaceTint=MiniOriginal->K2_GetVectorParameterValue(TEXT("SurfaceTint"));
    WallTint=MiniOriginal->K2_GetVectorParameterValue(TEXT("WallTint"));Samples=1.f;
    const FVector StartCenter=Vec(Start.View.Center),EndCenter=Vec(End.View.Center);
    StartDistance=FVector::DotProduct(StartCenter-MiniSource->GetComponentLocation(),MiniSource->GetForwardVector());
    EndDistance=FVector::DotProduct(EndCenter-FullSource->GetComponentLocation(),FullSource->GetForwardVector());
    StartGround=MiniInfo.A;EndGround=FullInfo.A;StartMaxDistance=MiniSource->MaxViewDistanceOverride;EndMaxDistance=FullSource->MaxViewDistanceOverride;
    if(!Finite(MiniInfo)||!Finite(FullInfo)||!Finite(GroundTint)||!Finite(SurfaceTint)||!Finite(WallTint)||!Finite(Samples)||
       !Finite(StartDistance)||!Finite(EndDistance)||StartDistance<=0.f||EndDistance<=0.f||!Finite(StartMaxDistance)||!Finite(EndMaxDistance))
    {Error=TEXT("地图捕获参数无效。 / Invalid map capture parameters.");Release();return false;}
    FActorSpawnParameters Params;Params.Owner=PC;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    FullCapture=W->SpawnActor<ASceneCapture2D>(Params);
    if(!FullCapture){Error=TEXT("无法创建过渡捕获。 / Could not create map transition capture.");Release();return false;}
    FullCapture->SetActorEnableCollision(false);
    Configure(FullCapture->GetCaptureComponent2D(),FullSource);
    const int32 Resolution=FMath::Clamp(RequestedResolution,256,1024);
    for(int32 I=0;I<2;++I)
    {
        FullRT[I]=NewObject<UTextureRenderTarget2D>(this,NAME_None,RF_Transient);
        if(!FullRT[I]){Error=TEXT("无法创建过渡纹理。 / Could not create transition targets.");Release();return false;}
        auto* RT=FullRT[I].Get();RT->ClearColor=FLinearColor::Black;RT->RenderTargetFormat=RTF_R32f;RT->bAutoGenerateMips=false;RT->Filter=TF_Bilinear;RT->InitAutoFormat(Resolution,Resolution);RT->UpdateResourceImmediate(true);
        FullMID[I]=UMaterialInstanceDynamic::Create(Base,this);
        if(!FullMID[I]){Error=TEXT("无法创建过渡材质。 / Could not create transition material.");Release();return false;}
    }
    bReady=Schedule.Begin(0.,FPlatformTime::Seconds());bHaveCommitted=false;Front=0;Back=1;
    if(!bReady){Error=TEXT("无法启动地图过渡。 / Could not start transition.");Release();return false;}
    Error.Reset();return true;
}

bool UDWMapTransitionCapture::Request(const DWMapMotionAdapter::FFrame& Frame,double Alpha,double Now,bool bForce,FString& Error)
{
    UWorld* W=OwnerWorld.Get();
    if(!bReady||!W||W->bIsTearingDown||!IsValid(FullCapture)||!DWMapMotionMath::Valid(Frame.View)||!FMath::IsFinite(Frame.OrthoWidth)||Frame.OrthoWidth<=0.)
    {Error=TEXT("地图过渡状态失效。 / Map transition state is invalid.");return false;}
    DWMapMotionSchedule::Request Token;if(!Schedule.TryRequest(Alpha,Now,bForce,Token))return true;
    PendingFrame=Frame;PendingRequest=Token;PendingAt=Now;bWaitingForReuse=true;
    if(!QueueGPUFence()){double Ignored;Schedule.Complete(Token.Token,false,Ignored);Error=TEXT("GPU 同步不可用。 / GPU synchronization unavailable.");return false;}
    Error.Reset();return true;
}

bool UDWMapTransitionCapture::QueueGPUFence()
{
    if(!GDynamicRHI)return false;
    GPUFence=RHICreateGPUFence(TEXT("DWMapTransition"));
    if(!GPUFence.IsValid())return false;
    FGPUFenceRHIRef QueuedFence=GPUFence;
    ENQUEUE_RENDER_COMMAND(DWMapTransitionGPUFence)([QueuedFence](FRHICommandListImmediate& Cmd){Cmd.WriteGPUFence(QueuedFence.GetReference());});
    Fence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
    return true;
}

bool UDWMapTransitionCapture::SubmitPending(FString& Error)
{
    const auto& Frame=PendingFrame;const double Alpha=PendingRequest.Alpha;
    const FRotator Rotation(Frame.View.Pitch,Frame.View.Yaw,0.f);const FVector Forward=Rotation.Vector(),Center=Vec(Frame.View.Center);
    const float Distance=FMath::Lerp(StartDistance,EndDistance,float(Alpha));const FVector Origin=Center-Forward*Distance;
    const float Ground=FMath::Lerp(StartGround,EndGround,float(Alpha));
    const float MaxDistance=FMath::Lerp(StartMaxDistance,EndMaxDistance,float(Alpha));
    if(Origin.ContainsNaN()||!Finite(Distance)||!Finite(Ground)||!Finite(MaxDistance)||MaxDistance<=0.f)
    {double Ignored;Schedule.Complete(PendingRequest.Token,false,Ignored);Error=TEXT("地图过渡视野无效。 / Invalid transition view.");return false;}
    auto* B=FullCapture->GetCaptureComponent2D();
    UWorld* W=OwnerWorld.Get();
    if(!IsValid(B)||!IsValid(W)||W->bIsTearingDown||!W->Scene||B->GetWorld()!=W||
       !B->IsRegistered()||!B->IsVisible()||!IsValid(FullRT[Back].Get()))
    {double Ignored;Schedule.Complete(PendingRequest.Token,false,Ignored);Error=TEXT("地图捕获不可用。 / Capture unavailable.");return false;}
    FullCapture->SetActorLocationAndRotation(Origin,Rotation);
    B->TextureTarget=FullRT[Back];B->OrthoWidth=float(Frame.OrthoWidth);
    B->MaxViewDistanceOverride=MaxDistance;
    ConfigureMID(FullMID[Back],FullRT[Back],Frame,Forward,Origin,Ground,GroundTint,SurfaceTint,WallTint,Samples);
    B->CaptureScene();bWaitingForReuse=false;
    if(!QueueGPUFence())
    {
        // Capture commands already exist. Keep the token pending so Release waits for
        // their render-thread submission before dropping actors and render targets.
        Fence.BeginFence(FRenderCommandFence::ESyncDepth::RHIThread);
        Error=TEXT("GPU 同步不可用。 / GPU synchronization unavailable.");return false;
    }
    Error.Reset();return true;
}

bool UDWMapTransitionCapture::Poll(FString& Error)
{
    UWorld* W=OwnerWorld.Get();
    if(!bReady||!W||W->bIsTearingDown){Error=TEXT("地图世界已失效。 / Map world is no longer valid.");return false;}
    if(Schedule.IsPending()&&FPlatformTime::Seconds()-PendingAt>10.){Error=TEXT("地图捕获超时。 / Map capture timed out.");return false;}
    if(!Schedule.IsPending()||!Fence.IsFenceComplete()||!GPUFence.IsValid()||!GPUFence->Poll())return true;
    if(bWaitingForReuse)
    {
        if(!Schedule.IsPendingCurrent())
        {double Ignored=0.;Schedule.Complete(PendingRequest.Token,false,Ignored);bWaitingForReuse=false;Error.Reset();return true;}
        return SubmitPending(Error);
    }
    double Alpha=0.;if(Schedule.Complete(PendingRequest.Token,true,Alpha))
    {Swap(Front,Back);Committed=PendingFrame;bHaveCommitted=true;}
    Error.Reset();return true;
}
UMaterialInstanceDynamic* UDWMapTransitionCapture::GetMiniMaterial() const{return bHaveCommitted?FullMID[Front].Get():nullptr;}
UMaterialInstanceDynamic* UDWMapTransitionCapture::GetFullMaterial() const{return bHaveCommitted?FullMID[Front].Get():nullptr;}

bool UDWMapTransitionCapture::UpdateStableFullView(const DWMapMotionAdapter::FFrame& Current,FString& Error)
{
    if(!bHaveCommitted||Schedule.IsPending()||!DWMapMotionMath::Valid(Current.View)||Committed.OrthoWidth<=0.)return false;
    const auto& Capture=Committed.View;const FVector Delta=Vec(Current.View.Center)-Vec(Capture.Center);
    const double Width=Committed.OrthoWidth;
    const double SX=Current.View.Frame.W*Current.View.UnitsPerPixel/Width;
    const double SY=Current.View.Frame.H*Current.View.UnitsPerPixel*Current.View.NorthScale/Width;
    const double OX=.5+FVector::DotProduct(Delta,Vec(Capture.Right))/Width;
    const double OY=.5-FVector::DotProduct(Delta,Vec(Capture.Up))/Width;
    if(!FMath::IsFinite(SX)||!FMath::IsFinite(SY)||!FMath::IsFinite(OX)||!FMath::IsFinite(OY))return false;
    FullMID[Front]->SetVectorParameterValue(TEXT("WorldMapView"),FLinearColor(SX,SY,OX,OY));Error.Reset();return true;
}

void UDWMapTransitionCapture::Release()
{
    Schedule.Cancel();if(Schedule.IsPending()){Fence.Wait();double Ignored=0.;Schedule.Complete(PendingRequest.Token,false,Ignored);}
    if(IsValid(FullCapture))FullCapture->Destroy();FullCapture=nullptr;
    for(int32 I=0;I<2;++I){FullMID[I]=nullptr;FullRT[I]=nullptr;}
    GPUFence.SafeRelease();bWaitingForReuse=false;bReady=false;bHaveCommitted=false;OwnerWorld.Reset();
}
void UDWMapTransitionCapture::BeginDestroy(){Release();Super::BeginDestroy();}
