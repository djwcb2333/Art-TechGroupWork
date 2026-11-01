#pragma once
#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"
#include "AssetEditorModeManager.h"
#include "ScopedTransaction.h"
#include "DWAttackVfxAuthoringBridge.h"
class FAssetEditorModeManager;
class FScopedTransaction;

class SDWAttackVfxAuthoringViewport : public SEditorViewport, public FGCObject
{
public:
    SLATE_BEGIN_ARGS(SDWAttackVfxAuthoringViewport){} SLATE_END_ARGS()
    void Construct(const FArguments&,TSharedRef<IDWAttackVfxAuthoringBridge> InBridge);
    virtual ~SDWAttackVfxAuthoringViewport() override;
    virtual void AddReferencedObjects(FReferenceCollector&) override;
    virtual FString GetReferencerName() const override{return TEXT("DWAttackVfxAuthoringPreview");}
    virtual void Tick(const FGeometry&,double,float) override;
    void Seek(double Seconds);
    void Replay();
    void RefreshAfterEdit();
    void SetPlaying(bool Value){bPlaying=Value;}
    bool IsPlaying() const{return bPlaying;}
    void SetLoop(bool Value){bLoop=Value;}
    bool IsLooping() const{return bLoop;}
    void SetRate(double Value){Rate=FMath::Clamp(Value,0.1,4.0);}
    double GetRate() const{return Rate;}
    double GetTime() const{return RequestedTime;}
    double Duration() const;
    FText Status() const{return StatusText;}
    void Focus();
    FTransform SelectedWorld() const;
    bool HasSelection() const;
    void BeginGizmoDrag();
    bool ApplyGizmoDelta(FVector Drag,FRotator Rotation,FVector Scale);
    void EndGizmoDrag();
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
private:
    TSharedPtr<IDWAttackVfxAuthoringBridge> Bridge;
    TUniquePtr<FAdvancedPreviewScene> Scene;
    // FEditorViewportClient adopts this manager through AsShared().
    TSharedPtr<FAssetEditorModeManager> ModeTools;
    TUniquePtr<FScopedTransaction> DragTransaction;
    bool bPlaying=false,bLoop=true,bNeedsReset=true,bPreviewReady=false;
    double RequestedTime=0,SimulatedTime=0,Rate=1;
    FText StatusText;
    // Budgeted historical replay; hard limit is visible rather than silent truncation.
    static constexpr double FixedStep=1.0/60.0;
    static constexpr int32 MaxStepsPerEditorTick=16;
    static constexpr int32 MaxReplaySteps=1200;
};
