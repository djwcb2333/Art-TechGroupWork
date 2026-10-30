#pragma once
#include "CoreMinimal.h"
#include "DWMapMotionAdapter.h"
#include "DWMapMotionSchedule.h"
#include "RenderCommandFence.h"
#include "RHIResources.h"
#include "UObject/Object.h"
#include "DWMapTransitionCapture.generated.h"

class APlayerController;class ASceneCapture2D;class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;class UDWMinimapComponent;class UDWWorldMapSubsystem;

/** One terrain-only source with double buffering and asynchronous GPU fences. */
UCLASS(Transient)
class GDATTEST_API UDWMapTransitionCapture : public UObject
{
    GENERATED_BODY()
public:
    bool Initialize(APlayerController* PC,UDWMinimapComponent* Mini,UDWWorldMapSubsystem* Map,
        const DWMapMotionAdapter::FFrame& Start,const DWMapMotionAdapter::FFrame& End,int32 RequestedResolution,FString& Error);
    bool Request(const DWMapMotionAdapter::FFrame& Frame,double Alpha,double Now,bool bForce,FString& Error);
    bool Poll(FString& Error);
    void SetDirection(int Direction){Schedule.SetDirection(Direction);}
    void SetEndFrame(const DWMapMotionAdapter::FFrame& Frame){EndFrame=Frame;}
    bool HasPending() const{return Schedule.IsPending();}
    void PauseDeadline(double Seconds){if(Schedule.IsPending()&&FMath::IsFinite(Seconds)&&Seconds>0.)PendingAt+=Seconds;}
    double GetCommittedAlpha() const{return Schedule.GetCommittedAlpha();}
    const DWMapMotionAdapter::FFrame& GetCommittedFrame() const{return Committed;}
    UMaterialInstanceDynamic* GetMiniMaterial() const;
    UMaterialInstanceDynamic* GetFullMaterial() const;
    bool UpdateStableFullView(const DWMapMotionAdapter::FFrame& Current,FString& Error);
    void Release();
    virtual void BeginDestroy() override;
private:
    UPROPERTY(Transient) TObjectPtr<ASceneCapture2D> FullCapture;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> FullRT[2];
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FullMID[2];
    TWeakObjectPtr<UWorld> OwnerWorld;
    FRenderCommandFence Fence;
    FGPUFenceRHIRef GPUFence;
    DWMapMotionSchedule::Scheduler Schedule;
    DWMapMotionAdapter::FFrame StartFrame,EndFrame,PendingFrame,Committed;
    DWMapMotionSchedule::Request PendingRequest;
    FLinearColor GroundTint,SurfaceTint,WallTint;
    float Samples=2.f,StartDistance=0.f,EndDistance=0.f,StartGround=0.f,EndGround=0.f,StartMaxDistance=0.f,EndMaxDistance=0.f;
    int32 Front=0,Back=1;
    double PendingAt=0.;
    bool bReady=false,bHaveCommitted=false,bWaitingForReuse=false;
    bool SubmitPending(FString& Error);
    bool QueueGPUFence();
};
