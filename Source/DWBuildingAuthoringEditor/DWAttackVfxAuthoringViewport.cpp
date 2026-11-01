#include "DWAttackVfxAuthoringViewport.h"
#include "EditorViewportClient.h"
#include "AssetEditorModeManager.h"
#include "Engine/Selection.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "InputKeyEventArgs.h"
#include "SceneManagement.h"
#include "Slate/SceneViewport.h"
#include "MouseDeltaTracker.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/PlatformTime.h"

namespace DWAttackVfxGizmoDiagnostics
{
    bool IsEnabled()
    {return FParse::Param(FCommandLine::Get(),TEXT("DWAttackVfxGizmoTrace"));}
    void Log(const TCHAR* Stage,const FGuid& Id,const FString& Detail)
    {
        if(!IsEnabled())return;
        static int32 Count=0;
        if(Count>=512)return;
        UE_LOG(LogTemp,Display,TEXT("DWVfxGizmo t=%.6f #%d %s Entry=%s %s"),
            FPlatformTime::Seconds(),++Count,Stage,*Id.ToString(),*Detail);
        if(Count==512)UE_LOG(LogTemp,Display,TEXT("DWVfxGizmo trace capped at512 lines for this process"));
    }
}
namespace
{
    bool ShouldFlushWidgetRelease(EInputEvent Event,const FKey& Key,bool Tracking,EAxisList::Type Axis,bool HasViewport)
    {return Event==IE_Released&&Key==EKeys::LeftMouseButton&&Tracking&&Axis!=EAxisList::None&&HasViewport;}
}

class FDWAttackVfxViewportClient : public FEditorViewportClient
{
    TWeakPtr<SDWAttackVfxAuthoringViewport> Owner;
public:
    FDWAttackVfxViewportClient(FAssetEditorModeManager* Tools,FPreviewScene* Scene,
        const TWeakPtr<SDWAttackVfxAuthoringViewport>& Widget)
        :FEditorViewportClient(Tools,Scene,Widget),Owner(Widget)
    {
        SetRealtime(true);bUsingOrbitCamera=true;ShowWidget(true);
        EngineShowFlags.SetModeWidgets(true);EngineShowFlags.SetSelectionOutline(false);
        SetViewLocation(FVector(-350,350,220));SetViewRotation(FRotator(-20,-45,0));
        // Client SetWidgetMode invalidates Viewport, which is not bound until
        // SEditorViewport::Construct creates FSceneViewport after this constructor.
        // Initialize only the private manager's value; W/E/R remains on the client.
        Tools->SetWidgetMode(UE::Widget::WM_Translate);
    }
    virtual bool ShouldOrbitCamera() const override{return true;}
    virtual bool CanSetWidgetMode(UE::Widget::EWidgetMode Mode) const override
    {return Mode==UE::Widget::WM_Translate||Mode==UE::Widget::WM_Rotate||Mode==UE::Widget::WM_Scale;}
    virtual FVector GetWidgetLocation() const override
    {const auto W=Owner.Pin();return W?W->SelectedWorld().GetLocation():FVector::ZeroVector;}
    virtual FMatrix GetWidgetCoordSystem() const override{return FMatrix::Identity;}
    virtual ECoordSystem GetWidgetCoordSystemSpace() const override{return COORD_World;}
    virtual bool InputKey(const FInputKeyEventArgs& Args) override
    {
        // Consume pending widget motion before base release stops tracking and clears it.
        // UpdateMouseDelta reduces consumed delta, so the usual Tick does not apply it twice.
        if(ShouldFlushWidgetRelease(Args.Event,Args.Key,IsTracking(),GetCurrentWidgetAxis(),Viewport!=nullptr))
        {
            DWAttackVfxGizmoDiagnostics::Log(TEXT("ReleaseFlush"),FGuid(),FString::Printf(
                TEXT("Axis=%d Pending=%s Orbit=%d"),int32(GetCurrentWidgetAxis()),*MouseDeltaTracker->GetDelta().ToString(),ShouldOrbitCamera()));
            UpdateMouseDelta();
        }
        if(Args.Event==IE_Pressed)
        {
            if(Args.Key==EKeys::W){SetWidgetMode(UE::Widget::WM_Translate);return true;}
            if(Args.Key==EKeys::E){SetWidgetMode(UE::Widget::WM_Rotate);return true;}
            if(Args.Key==EKeys::R){SetWidgetMode(UE::Widget::WM_Scale);return true;}
            if(Args.Key==EKeys::F){if(auto W=Owner.Pin())W->Focus();return true;}
        }
        return FEditorViewportClient::InputKey(Args);
    }
    virtual void TrackingStarted(const FInputEventState&,bool bDraggingWidget,bool) override
    {
        DWAttackVfxGizmoDiagnostics::Log(TEXT("TrackingStarted"),FGuid(),FString::Printf(
            TEXT("Widget=%d Axis=%d Mode=%d Orbit=%d"),bDraggingWidget,int32(GetCurrentWidgetAxis()),int32(GetWidgetMode()),ShouldOrbitCamera()));
        if(bDraggingWidget)if(auto W=Owner.Pin())W->BeginGizmoDrag();
    }
    virtual void TrackingStopped() override{if(auto W=Owner.Pin())W->EndGizmoDrag();}
    virtual bool InputWidgetDelta(FViewport*,EAxisList::Type Axis,FVector& Drag,FRotator& Rotation,FVector& Scale) override
    {
        DWAttackVfxGizmoDiagnostics::Log(TEXT("InputWidgetDelta"),FGuid(),FString::Printf(
            TEXT("Axis=%d Drag=%s Rot=%s Scale=%s Nonzero=%d"),int32(Axis),*Drag.ToString(),*Rotation.ToString(),*Scale.ToString(),
            !Drag.IsNearlyZero()||!Rotation.IsNearlyZero()||!Scale.IsNearlyZero()));
        if(Axis==EAxisList::None)return false;
        if(auto W=Owner.Pin())return W->ApplyGizmoDelta(Drag,Rotation,Scale);
        return false;
    }
    virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) override
    {
        if(auto W=Owner.Pin())ShowWidget(W->HasSelection());
        FEditorViewportClient::Draw(View,PDI);
        if(auto W=Owner.Pin();W&&W->HasSelection())
        {
            const FVector P=W->SelectedWorld().GetLocation();
            PDI->DrawPoint(P,FLinearColor::Yellow,8,SDPG_Foreground);
        }
    }
};

void SDWAttackVfxAuthoringViewport::Construct(const FArguments&,TSharedRef<IDWAttackVfxAuthoringBridge> InBridge)
{
    Bridge=InBridge;
    Scene=MakeUnique<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues().SetEditor(true).SetCreatePhysicsScene(false).SetTransactional(false));
    Scene->SetFloorVisibility(true,true);Scene->SetLightBrightness(3);Scene->SetSkyBrightness(1);
    ModeTools=MakeShared<FAssetEditorModeManager>();ModeTools->SetPreviewScene(Scene.Get());ModeTools->SetSupportsViewportITF(false);ModeTools->ActivateDefaultMode();
    // A private marker exists only in EditorPreview selection, never GEditor selection/level.
    FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
    AActor* Marker=Scene->GetWorld()->SpawnActor<AActor>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
    auto* Root=NewObject<USceneComponent>(Marker,NAME_None,RF_Transient);Marker->SetRootComponent(Root);
    Marker->AddInstanceComponent(Root);Root->RegisterComponent();Marker->SetActorEnableCollision(false);
    ModeTools->GetSelectedActors()->Select(Marker,true);
    FString Error;
    bPreviewReady=Bridge->PopulateTransientPreview(*Scene,Error);
    if(!bPreviewReady)StatusText=FText::FromString(Error);
    SEditorViewport::Construct(SEditorViewport::FArguments());
    Focus();Replay();
}
SDWAttackVfxAuthoringViewport::~SDWAttackVfxAuthoringViewport()
{
    EndGizmoDrag();
    // UE 5.8 SceneViewport holds a strong client; detach while both sides are alive.
    // Retain SceneViewport itself for the base SEditorViewport destructor's unique check.
    if(SceneViewport)SceneViewport->SetViewportClient(TStrongPtrVariant<FViewportClient>(nullptr));
    Client.Reset();
    if(Bridge)Bridge->ClearTransientPreview();
    if(ModeTools)ModeTools->GetSelectedActors()->DeselectAll();
    if(ModeTools)ModeTools->SetPreviewScene(nullptr);
    ModeTools.Reset();Scene.Reset();
}
void SDWAttackVfxAuthoringViewport::AddReferencedObjects(FReferenceCollector& Collector)
{if(Bridge)Bridge->CollectReferences(Collector);}
TSharedRef<FEditorViewportClient> SDWAttackVfxAuthoringViewport::MakeEditorViewportClient()
{return MakeShared<FDWAttackVfxViewportClient>(ModeTools.Get(),Scene.Get(),SharedThis(this));}
double SDWAttackVfxAuthoringViewport::Duration() const
{return Bridge?FMath::Clamp(Bridge->SourceDurationSeconds(),0.0,FixedStep*MaxReplaySteps):0.0;}
void SDWAttackVfxAuthoringViewport::Focus()
{if(Client&&Bridge)Client->FocusViewportOnBox(Bridge->PreviewBounds());}
void SDWAttackVfxAuthoringViewport::Seek(double Seconds)
{
    bPlaying=false;RequestedTime=FMath::Clamp(Seconds,0.0,Duration());
    // Restart all history on seek so an attached ribbon sees the historical poses.
    bNeedsReset=true;
}
void SDWAttackVfxAuthoringViewport::Replay(){RequestedTime=0;SimulatedTime=0;bNeedsReset=true;}
void SDWAttackVfxAuthoringViewport::RefreshAfterEdit()
{RequestedTime=FMath::Clamp(RequestedTime,0.0,Duration());bNeedsReset=true;Invalidate();}
FTransform SDWAttackVfxAuthoringViewport::SelectedWorld() const
{FTransform T;return Bridge&&Bridge->ReadSelectedWorldTransform(T)?T:FTransform::Identity;}
bool SDWAttackVfxAuthoringViewport::HasSelection() const
{FTransform T;return Bridge&&Bridge->ReadSelectedWorldTransform(T);}
void SDWAttackVfxAuthoringViewport::BeginGizmoDrag()
{
    if(DragTransaction||!HasSelection())return;
    DWAttackVfxGizmoDiagnostics::Log(TEXT("Begin"),Bridge->SelectedEntry(),FString::Printf(TEXT("Profile=%s World=%s"),*Bridge->GetProfile()->GetPathName(),*SelectedWorld().ToString()));
    bPlaying=false;DragTransaction=MakeUnique<FScopedTransaction>(FText::FromString(TEXT("Adjust Attack VFX Transform")));
    Bridge->GetProfile()->Modify();
}
bool SDWAttackVfxAuthoringViewport::ApplyGizmoDelta(FVector Drag,FRotator Rotation,FVector Scale)
{
    if(!DragTransaction||!Bridge)
    {DWAttackVfxGizmoDiagnostics::Log(TEXT("ApplyRejected"),FGuid(),TEXT("No active transaction or bridge"));return false;}
    FTransform T=SelectedWorld();T.AddToTranslation(Drag);
    T.SetRotation((Rotation.Quaternion()*T.GetRotation()).GetNormalized());
    const FVector NewScale=T.GetScale3D()+Scale;
    if(NewScale.GetMin()<=KINDA_SMALL_NUMBER||NewScale.ContainsNaN())return false;
    T.SetScale3D(NewScale);
    FString Error;
    if(!Bridge->WriteSelectedWorldTransform(T,Error))
    {DWAttackVfxGizmoDiagnostics::Log(TEXT("ApplyRejected"),Bridge->SelectedEntry(),Error);StatusText=FText::FromString(Error);return false;}
    DWAttackVfxGizmoDiagnostics::Log(TEXT("ApplyAccepted"),Bridge->SelectedEntry(),FString::Printf(TEXT("RequestedWorld=%s ReadbackWorld=%s"),*T.ToString(),*SelectedWorld().ToString()));
    if(Client)Client->Invalidate();return true;
}
void SDWAttackVfxAuthoringViewport::EndGizmoDrag()
{
    DWAttackVfxGizmoDiagnostics::Log(TEXT("End"),Bridge?Bridge->SelectedEntry():FGuid(),FString::Printf(TEXT("Transaction=%d World=%s"),bool(DragTransaction),*SelectedWorld().ToString()));
    if(!DragTransaction)return;
    if(Bridge&&Bridge->GetProfile()){Bridge->GetProfile()->PostEditChange();Bridge->GetProfile()->MarkPackageDirty();}
    DragTransaction.Reset();bNeedsReset=true;
}
void SDWAttackVfxAuthoringViewport::Tick(const FGeometry& G,double Time,float Delta)
{
    SEditorViewport::Tick(G,Time,Delta);
    if(!Bridge||!Scene||!bPreviewReady)return;
    FString Error;
    if(bNeedsReset)
    {
        Bridge->ResetPreviewSimulation();SimulatedTime=0;bNeedsReset=false;
        if(!Bridge->StepPreview(0,0,Error)){bPlaying=false;StatusText=FText::FromString(Error);return;}
    }
    // Catch up history before advancing playback. A seek can never make this loop unbounded.
    if(bPlaying&&SimulatedTime+FixedStep>=RequestedTime)
    {
        RequestedTime=FMath::Min(Duration(),RequestedTime+FMath::Clamp(double(Delta),0.0,0.1)*Rate);
        if(RequestedTime>=Duration()&&SimulatedTime>=Duration())
        {if(bLoop)Replay();else bPlaying=false;}
    }
    int32 Steps=0;
    while(SimulatedTime+SMALL_NUMBER<RequestedTime&&Steps++<MaxStepsPerEditorTick)
    {
        const double Next=FMath::Min(RequestedTime,SimulatedTime+FixedStep);
        if(!Bridge->StepPreview(Next,float(Next-SimulatedTime),Error))
        {bPlaying=false;StatusText=FText::FromString(Error);return;}
        SimulatedTime=Next;
    }
    StatusText=FText::FromString(FString::Printf(TEXT("%.3f / %.3f s | %s | max preview %.1fs"),SimulatedTime,Duration(),
        SimulatedTime+SMALL_NUMBER<RequestedTime?TEXT("Replaying history"):TEXT("Ready"),FixedStep*MaxReplaySteps));
    if(Client)Client->Invalidate();
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAttackVfxViewportOwnership,
    "DoughWorld.Editor.AttackVfx.ViewportOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAttackVfxViewportOwnership::RunTest(const FString&)
{
    // Exercise the actual derived client constructor, then engine strong association/release.
    // No authoring widget, gameplay actor, preview world, asset edit or player input.
    TestTrue(TEXT("Only tracked widget left-release flushes pending delta"),ShouldFlushWidgetRelease(IE_Released,EKeys::LeftMouseButton,true,EAxisList::Z,true));
    TestFalse(TEXT("Press does not flush"),ShouldFlushWidgetRelease(IE_Pressed,EKeys::LeftMouseButton,true,EAxisList::Z,true));
    TestFalse(TEXT("Camera release does not flush"),ShouldFlushWidgetRelease(IE_Released,EKeys::RightMouseButton,true,EAxisList::Z,true));
    TestFalse(TEXT("No axis or unbound viewport does not flush"),ShouldFlushWidgetRelease(IE_Released,EKeys::LeftMouseButton,true,EAxisList::None,true)||ShouldFlushWidgetRelease(IE_Released,EKeys::LeftMouseButton,true,EAxisList::Z,false));
    for(int32 Cycle=0;Cycle<3;++Cycle)
    {
        TSharedPtr<FAssetEditorModeManager> Tools=MakeShared<FAssetEditorModeManager>();
        Tools->SetSupportsViewportITF(false);
        TWeakPtr<FAssetEditorModeManager> WeakTools=Tools;
        TSharedPtr<FEditorViewportClient> TestClient=MakeShared<FDWAttackVfxViewportClient>(
            Tools.Get(),nullptr,TWeakPtr<SDWAttackVfxAuthoringViewport>());
        TestNull(TEXT("Actual authoring client constructor supports unbound viewport"),TestClient->Viewport);
        TestEqual(TEXT("Actual authoring client initializes private Translate mode"),Tools->GetWidgetMode(),UE::Widget::WM_Translate);
        TWeakPtr<FEditorViewportClient> WeakClient=TestClient;
        Tools.Reset();
        TestTrue(TEXT("Client adopted a real shared mode manager"),WeakTools.IsValid());
        auto TestViewport=FSceneViewport::Create(TestClient,TSharedPtr<SViewport>());
        TestClient.Reset();
        TestTrue(TEXT("SceneViewport owns the client until detached"),WeakClient.IsValid());
        TestViewport->SetViewportClient(TStrongPtrVariant<FViewportClient>(nullptr));
        TestFalse(TEXT("Detach releases client with Viewport cleared"),WeakClient.IsValid());
        TestFalse(TEXT("Client destruction releases its private mode manager"),WeakTools.IsValid());
    }
    return true;
}
#endif
