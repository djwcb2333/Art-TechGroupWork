#include "DWWorldMapScreen.h"
#include "DWWorldMap.h"
#include "DWMapTransitionCapture.h"
#include "DWMapMaskPaint.h"
#include "DWMapMarkerPaint.h"
#include "DWMapMiniSnapshotPaint.h"
#include "DWMinimapNavigation.h"
#include "DWPlayerController.h"
#include "DWMapVisualMath.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DWMinimap.h"
#include "DWMinimapWidget.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
    constexpr float InsetX=36.f, InsetTop=58.f, InsetBottom=36.f;
    const FLinearColor Background(.16f,.15f,.13f,.96f);
    const FLinearColor MarkerColor(.96f,.95f,.89f,1.f);
    const FSlateBrush* Solid(){return FCoreStyle::Get().GetBrush("WhiteBrush");}
    bool Finite(FVector2D P){return FMath::IsFinite(P.X)&&FMath::IsFinite(P.Y);}
    FVector2D CloseOrigin(FVector2D Screen){return FVector2D(FMath::Max(0.,Screen.X-52.),8.);}
}

void UDWWorldMapScreen::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    if(WidgetTree&&!WidgetTree->RootWidget)
        WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("WorldMapRoot"));
}

bool UDWWorldMapScreen::InitializeMap(ADWGameplayHUD* InHUD,FString& OutError)
{
    OutError.Reset();HUD=InHUD;auto* PC=GetOwningPlayer();UWorld* World=GetWorld();
    auto* Subsystem=World?World->GetSubsystem<UDWWorldMapSubsystem>():nullptr;
    auto* MapPC=Cast<ADWPlayerController>(PC);
    APawn* Pawn=PC?PC->GetPawn():nullptr;
    auto* Mini=Pawn?Pawn->FindComponentByClass<UDWMinimapComponent>():nullptr;
    if(!InHUD||!PC||!MapPC||!MapPC->OwnsWorldMapPause()||InHUD->GetMenuPage()!=EDWMenuPage::None||!PC->IsLocalController()||!Subsystem||!Mini){OutError=TEXT("地图或小地图不可用。 / Map or minimap is unavailable.");return false;}
    CaptureMinimapRect(PC);
    if(!bMeasuredMinimapGeometry){OutError=TEXT("无法读取小地图位置。 / Minimap geometry is unavailable.");return false;}
    MiniOrigin=FVector2D(MinimapSnapshot.X,MinimapSnapshot.Y);
    MiniSize=FVector2D(MinimapSnapshot.Width,MinimapSnapshot.Height);
    const FVector2D Initial(MinimapSnapshot.ViewWidth,MinimapSnapshot.ViewHeight);
    if(!DWMapMotionAdapter::SnapshotMinimap(Mini,MiniOrigin,MiniSize,StartFrame,OutError))return false;
    auto* Nav=World->GetSubsystem<UDWMinimapNavigationSubsystem>();
    if(!IsValid(Nav)){OutError=TEXT("导航状态不可用。 / Navigation is unavailable.");return false;}
    const TWeakObjectPtr<UWorld> WeakWorld(World);const TWeakObjectPtr<APlayerController> WeakPC(PC);
    const TWeakObjectPtr<APawn> WeakPawn(Pawn);const TWeakObjectPtr<UDWMinimapComponent> WeakMini(Mini);
    const TWeakObjectPtr<ADWGameplayHUD> WeakHUD(InHUD);const TWeakObjectPtr<UDWWorldMapSubsystem> WeakMap(Subsystem);
    const TWeakObjectPtr<ADWPlayerController> WeakMapPC(MapPC);
    const TWeakObjectPtr<UDWMinimapNavigationSubsystem> WeakNav(Nav);const uint64 NavigationVersion=Nav->GetNavigationRevision();
    const auto Current=[&](){return WeakWorld.IsValid()&&!WeakWorld->bIsTearingDown&&WeakPC.IsValid()&&!WeakPC->IsActorBeingDestroyed()&&WeakPawn.IsValid()&&!WeakPawn->IsActorBeingDestroyed()&&WeakMini.IsValid()&&WeakHUD.IsValid()&&!WeakHUD->IsActorBeingDestroyed()&&WeakMap.IsValid()&&WeakNav.IsValid()&&
        WeakPC->GetWorld()==WeakWorld.Get()&&WeakPC->GetPawn()==WeakPawn.Get()&&WeakMini->GetWorld()==WeakWorld.Get()&&WeakMini->GetOwner()==WeakPawn.Get()&&
        WeakHUD->GetWorld()==WeakWorld.Get()&&WeakHUD->GetMenuPage()==EDWMenuPage::None&&WeakMapPC.IsValid()&&WeakMapPC->OwnsWorldMapPause()&&
        WeakMap->GetWorld()==WeakWorld.Get()&&WeakNav->GetWorld()==WeakWorld.Get()&&WeakNav->GetNavigationRevision()==NavigationVersion;};
    // Do not enumerate enemy or resource markers while opening the world map.
    MiniMarkers.Reset();
    MiniTarget=Mini->GetNavigationTarget();
    if(!Current()){OutError=TEXT("导航状态已改变，请重试。 / Navigation state changed; retry.");return false;}
    if(MiniTarget.bIsActorTarget)MiniTarget=FDWMinimapNavigationTarget();
    MiniMarkers.StableSort([](const FDWMinimapDisplayMarker& A,const FDWMinimapDisplayMarker& B){return A.Priority<B.Priority;});
    MiniPlayer=Mini->GetPlayerLocation();MiniForward=Pawn->GetActorForwardVector();MiniNorth=FRotator(0.f,Mini->NorthYaw,0.f).Vector();
    auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DoughWorld/UI/M_DWMapMotionClay.M_DWMapMotionClay"));
    VertexTintMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DoughWorld/UI/M_DWMapVertexTint.M_DWMapVertexTint"));
    if(!Current()){OutError=TEXT("地图加载期间状态改变。 / Map state changed during material load.");return false;}
    InitialMiniMaterial=Base?UMaterialInstanceDynamic::Create(Base,this):nullptr;
    if(!Current()){OutError=TEXT("地图材质创建期间状态改变。 / Map state changed during material creation.");return false;}
    if(!InitialMiniMaterial||!VertexTintMaterial||!DWMapMotionAdapter::CopyMiniSnapshotToWorldMaterial(Mini,InitialMiniMaterial,OutError))
    {if(OutError.IsEmpty())OutError=TEXT("地图过渡材质不可用。 / Map transition material unavailable.");return false;}
    if(!Current()){OutError=TEXT("地图打开前状态改变，请重试。 / Map state changed; retry.");return false;}
    if(!Subsystem->OpenMap(PC,Initial,OutError))return false;
    if(!WeakWorld.IsValid()||WeakWorld->bIsTearingDown||!WeakPC.IsValid()||WeakPC->IsActorBeingDestroyed()||!WeakPawn.IsValid()||WeakPawn->IsActorBeingDestroyed()||!WeakMini.IsValid()||!WeakHUD.IsValid()||WeakHUD->IsActorBeingDestroyed()||!WeakMap.IsValid()||!WeakMapPC.IsValid()||!WeakMapPC->OwnsWorldMapPause()||WeakHUD->GetMenuPage()!=EDWMenuPage::None||WeakPC->GetPawn()!=WeakPawn.Get()||WeakMini->GetOwner()!=WeakPawn.Get()||WeakPC->GetWorld()!=WeakWorld.Get())
    {if(WeakMap.IsValid())WeakMap->CloseMap();OutError=TEXT("地图打开期间状态改变。 / Map owner changed.");return false;}
    Map=Subsystem;MiniComponent=Mini;MapMaterial=Subsystem->GetMapMaterial();
    if(!MapMaterial||!DWMapMotionAdapter::SnapshotFullMap(Subsystem,Pawn->GetActorLocation().Z,FVector2D::ZeroVector,Initial,EndFrame,OutError))
    {Subsystem->CloseMap();Map.Reset();return false;}
    TransitionCapture=NewObject<UDWMapTransitionCapture>(this);
    if(!TransitionCapture||!TransitionCapture->Initialize(PC,Mini,Subsystem,StartFrame,EndFrame,TransitionResolution,OutError))
    {Subsystem->CloseMap();Map.Reset();TransitionCapture=nullptr;return false;}
    TransitionCapture->SetDirection(1);DisplayFrame=StartFrame;DisplayAlpha=0.f;bDisplayCommitted=false;
    MapBrush.DrawAs=ESlateBrushDrawType::Image;MapBrush.SetResourceObject(MapMaterial);
    CloseArt=LoadObject<UTexture2D>(nullptr,TEXT("/Game/DoughWorld/UI/Image/T_DW_C04_CloseBrush.T_DW_C04_CloseBrush"));
    CloseBrush.DrawAs=ESlateBrushDrawType::Image;CloseBrush.SetResourceObject(CloseArt);
    bOpen=true;bClosing=false;bDragging=false;bMoved=false;bSettingsOverlayActive=false;
    MotionProgress=0.;MotionTime=FPlatformTime::Seconds();LastViewportSize=Initial;
    SetRenderTranslation(FVector2D::ZeroVector);SetRenderScale(FVector2D(1.,1.));SetRenderOpacity(1.f);
    SetVisibility(ESlateVisibility::Visible);return true;
}

void UDWWorldMapScreen::SetSettingsOverlayActive(bool bActive)
{
    const double Now=FPlatformTime::Seconds();
    if(bActive&&!bSettingsOverlayActive)PauseAt=Now;
    if(!bActive&&bSettingsOverlayActive&&TransitionCapture)TransitionCapture->PauseDeadline(Now-PauseAt);
    bSettingsOverlayActive=bActive;bDragging=false;MotionTime=Now;
    SetVisibility(bActive?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
}

void UDWWorldMapScreen::CloseMapView()
{
    bDragging=false;bOpen=false;
    if(TransitionCapture)TransitionCapture->Release();TransitionCapture=nullptr;InitialMiniMaterial=nullptr;VertexTintMaterial=nullptr;MiniComponent.Reset();
    if(auto* Subsystem=Map.Get())Subsystem->CloseMap();
    Map.Reset();MapMaterial=nullptr;CloseArt=nullptr;MapBrush.SetResourceObject(nullptr);CloseBrush.SetResourceObject(nullptr);
    RemoveFromParent();
}

void UDWWorldMapScreen::RequestClose()
{
    if(!bOpen||bSettingsOverlayActive)return;
    // Reverse from what was actually shown, not from a newer requested frame.
    MotionProgress=1.-FMath::Pow(1.-FMath::Clamp(double(DisplayAlpha),0.,1.),1./3.);
    bClosing=!bClosing;bDragging=false;MotionTime=FPlatformTime::Seconds();
    if(TransitionCapture)TransitionCapture->SetDirection(bClosing?-1:1);
}

void UDWWorldMapScreen::NativeDestruct()
{
    if(TransitionCapture)TransitionCapture->Release();TransitionCapture=nullptr;InitialMiniMaterial=nullptr;VertexTintMaterial=nullptr;MiniComponent.Reset();
    if(bOpen){bOpen=false;bDragging=false;if(auto* Subsystem=Map.Get())Subsystem->CloseMap();Map.Reset();MapMaterial=nullptr;CloseArt=nullptr;}
    Super::NativeDestruct();
}

FVector2D UDWWorldMapScreen::ContentSize(const FGeometry& Geometry) const{return Geometry.GetLocalSize();}
bool UDWWorldMapScreen::LocalPoint(const FGeometry& Geometry,FVector2D ScreenPoint,FVector2D& Out) const
{
    Out=Geometry.AbsoluteToLocal(ScreenPoint);const FVector2D S=Geometry.GetLocalSize();
    return Finite(Out)&&Out.X>=0.&&Out.Y>=0.&&Out.X<S.X&&Out.Y<S.Y;
}

void UDWWorldMapScreen::CaptureMinimapRect(APlayerController* PC)
{
    const FGeometry View=UWidgetLayoutLibrary::GetViewportWidgetGeometry(this);
    FVector2D ViewSize=View.GetLocalSize();
    if(!Finite(ViewSize)||ViewSize.X<=1.||ViewSize.Y<=1.)
    {
        int32 X=0,Y=0;if(PC)PC->GetViewportSize(X,Y);
        const float DPI=UWidgetLayoutLibrary::GetViewportScale(this);
        const double Scale=FMath::IsFinite(DPI)&&DPI>0.f?DPI:1.;
        ViewSize=FVector2D(FMath::Max(1.,double(X)/Scale),FMath::Max(1.,double(Y)/Scale));
    }
    const APawn* Pawn=PC?PC->GetPawn():nullptr;
    const auto* Mini=Pawn?Pawn->FindComponentByClass<UDWMinimapComponent>():nullptr;
    const auto* Widget=Mini?Mini->GetMinimapWidget():nullptr;
    bMeasuredMinimapGeometry=false;
    FVector2D TopLeft=FVector2D::ZeroVector,BottomRight=FVector2D::ZeroVector;
    if(Mini&&Mini->IsMinimapVisible()&&Widget&&Widget->IsInViewport()&&View.GetLocalSize().X>1.&&View.GetLocalSize().Y>1.)
    {
        const FGeometry MapGeo=Widget->GetCachedGeometry();const FVector2D Local=MapGeo.GetLocalSize();
        const double Side=FMath::Min(Local.X,Local.Y); // Exclude the optional footer.
        if(Finite(Local)&&Side>1.)
        {
            TopLeft=View.AbsoluteToLocal(MapGeo.LocalToAbsolute(FVector2D::ZeroVector));
            BottomRight=View.AbsoluteToLocal(MapGeo.LocalToAbsolute(FVector2D(Side,Side)));
            bMeasuredMinimapGeometry=Finite(TopLeft)&&Finite(BottomRight)&&BottomRight.X>TopLeft.X+1.&&BottomRight.Y>TopLeft.Y+1.
                &&TopLeft.X<ViewSize.X&&TopLeft.Y<ViewSize.Y&&BottomRight.X>0.&&BottomRight.Y>0.;
        }
    }
    if(!bMeasuredMinimapGeometry)
    {
        const float Diameter=Mini&&FMath::IsFinite(Mini->Diameter)?FMath::Clamp(Mini->Diameter,1.f,400.f):180.f;
        const FVector2D Margin=Mini&&Finite(Mini->BottomRightMargin)?Mini->BottomRightMargin:FVector2D(30.,30.);
        BottomRight=ViewSize-Margin;TopLeft=BottomRight-FVector2D(Diameter,Diameter);
    }
    MinimapSnapshot={TopLeft.X,TopLeft.Y,BottomRight.X-TopLeft.X,BottomRight.Y-TopLeft.Y,ViewSize.X,ViewSize.Y};
}
void UDWWorldMapScreen::ApplyTransition(FVector2D,float) {}
void UDWWorldMapScreen::AbortTransition(const FString& Reason)
{
    if(auto* Owner=HUD.Get()){Owner->ShowToast(FText::FromString(Reason));Owner->FinishWorldMapClose(this);}
    else CloseMapView();
}

void UDWWorldMapScreen::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);const double Now=FPlatformTime::Seconds();
    if(!bOpen||bSettingsOverlayActive){MotionTime=Now;return;}
    auto* Subsystem=Map.Get();auto* PC=GetOwningPlayer();APawn* Pawn=PC?PC->GetPawn():nullptr;
    if(!Subsystem||!Pawn||!TransitionCapture){AbortTransition(TEXT("地图已失效。 / Map unavailable."));return;}
    const FVector2D Size=Geometry.GetLocalSize();if(!Finite(Size)||Size.X<=1.||Size.Y<=1.)return;
    FString Error;
    if(!Size.Equals(LastViewportSize,.1))
    {
        bDragging=false;
        MiniOrigin+=Size-LastViewportSize;
        StartFrame.View.Frame.X=MiniOrigin.X;StartFrame.View.Frame.Y=MiniOrigin.Y;
        if(!Subsystem->SetViewportSize(Size)){AbortTransition(TEXT("地图尺寸无效。 / Invalid map size."));return;}
        TransitionCapture->SetDirection(0);TransitionCapture->SetDirection(bClosing?-1:1);LastViewportSize=Size;
    }
    if(!DWMapMotionAdapter::SnapshotFullMap(Subsystem,Pawn->GetActorLocation().Z,FVector2D::ZeroVector,Size,EndFrame,Error))
    {AbortTransition(Error);return;}
    TransitionCapture->SetEndFrame(EndFrame);
    if(!TransitionCapture->Poll(Error)){AbortTransition(Error);return;}
    if(TransitionCapture->GetMiniMaterial()&&TransitionCapture->GetFullMaterial())
    {DisplayFrame=TransitionCapture->GetCommittedFrame();DisplayAlpha=float(TransitionCapture->GetCommittedAlpha());bDisplayCommitted=true;}
    if(DisplayAlpha>=.9999f)DisplayFrame=EndFrame;
    const double Duration=FMath::IsFinite(ExpandSeconds)?FMath::Max(.01f,ExpandSeconds):.28;
    // Rendering can take longer than a game tick. Never advance the desired
    // timeline while a frame is in flight: slow GPUs extend the animation
    // instead of skipping directly from an old frame to the endpoint.
    if(!TransitionCapture->HasPending())
        MotionProgress=FMath::Clamp(MotionProgress+(bClosing?-1.:1.)*FMath::Clamp(Now-MotionTime,0.,.1)/Duration,0.,1.);
    MotionTime=Now;
    const float Eased=float(1.-FMath::Pow(1.-MotionProgress,3.));
    DWMapVisualMath::Rect Bounds;double Radius=0.;
    if(!DWMapVisualMath::Morph({MiniOrigin.X,MiniOrigin.Y,MiniOrigin.X+MiniSize.X,MiniOrigin.Y+MiniSize.Y},{Size.X,Size.Y},Eased,Bounds,Radius))return;
    DWMapMotionAdapter::FFrame Desired;
    if(!DWMapMotionAdapter::Interpolate(StartFrame,EndFrame,FVector2D(Bounds.Left,Bounds.Top),FVector2D(Bounds.Right-Bounds.Left,Bounds.Bottom-Bounds.Top),Eased,Desired))return;
    if(!TransitionCapture->Request(Desired,Eased,Now,MotionProgress==0.||MotionProgress==1.,Error)){AbortTransition(Error);return;}
    if(bClosing&&MotionProgress<=0.&&DisplayAlpha<=.0001f&&!TransitionCapture->HasPending())
        if(auto* Owner=HUD.Get())Owner->FinishWorldMapClose(this);
}

int32 UDWWorldMapScreen::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
    FSlateWindowElementList& DrawElements,int32 LayerId,const FWidgetStyle& Style,bool bParentEnabled) const
{
    LayerId=Super::NativePaint(Args,Geometry,CullingRect,DrawElements,LayerId,Style,bParentEnabled);
    if(!bOpen||bSettingsOverlayActive)return LayerId;
    auto* Subsystem=Map.Get();if(!Subsystem)return LayerId;
    const FVector2D Size=Geometry.GetLocalSize();
    const FVector2D FramePos(DisplayFrame.View.Frame.X,DisplayFrame.View.Frame.Y),FrameSize(DisplayFrame.View.Frame.W,DisplayFrame.View.Frame.H);
    UMaterialInterface* MiniMaterial=bDisplayCommitted&&TransitionCapture?TransitionCapture->GetMiniMaterial():InitialMiniMaterial.Get();
    if(DisplayAlpha>=.9999f) { MiniMaterial=MapMaterial; }
    if(!DWMapMaskPaint::Paint(DrawElements,++LayerId,Geometry,MiniMaterial,MiniOrigin,MiniSize,FramePos,FrameSize,DisplayAlpha,false,1.f))return LayerId;
    LayerId+=2;
    LayerId=DWMapMiniSnapshotPaint::Paint(DrawElements,LayerId,Geometry,VertexTintMaterial,DisplayFrame,MiniMarkers,MiniTarget,MiniPlayer,MiniForward,MiniNorth,float(MiniSize.X),1.f-DisplayAlpha);
    DWMapVisualMath::Rect Bounds;double Radius=0.;
    DWMapVisualMath::Morph({MiniOrigin.X,MiniOrigin.Y,MiniOrigin.X+MiniSize.X,MiniOrigin.Y+MiniSize.Y},{Size.X,Size.Y},DisplayAlpha,Bounds,Radius);
    const FGuid Selected=Subsystem->GetSelectedMarkerId();
    for(const FDWWorldMapMarker& Marker:Subsystem->GetMarkers())
    {
        FVector2D P;if(!DWMapMotionAdapter::WorldToSlate(DisplayFrame,Marker.Position,P)||!DWMapVisualMath::InsideRounded({P.X,P.Y},Bounds,Radius))continue;
        const double Age=Marker.Id==PulseMarker?FPlatformTime::Seconds()-PulseAt:-1.;
        DWMapMarkerPaint::Diamond(DrawElements,LayerId,Geometry,VertexTintMaterial,P,9.f,Marker.Id==Selected,Age,.16f,3.f,.65f,DisplayAlpha);
    }
    LayerId+=2;
    if(const APlayerController* PC=GetOwningPlayer())if(const APawn* Pawn=PC->GetPawn())
    {
        FVector2D P,Q;
        if(DWMapMotionAdapter::WorldToSlate(DisplayFrame,Pawn->GetActorLocation(),P)&&
           DWMapMotionAdapter::WorldToSlate(DisplayFrame,Pawn->GetActorLocation()+Pawn->GetActorForwardVector()*100.,Q)&&
           DWMapVisualMath::InsideRounded({P.X,P.Y},Bounds,Radius))
        {
            const FVector2D Forward=(Q-P).GetSafeNormal(),Right(-Forward.Y,Forward.X);
            const TArray<FVector2D> Line={P+Forward*14.,P-Forward*9.+Right*7.,P-Forward*5.,P-Forward*9.-Right*7.,P+Forward*14.};
            FSlateDrawElement::MakeLines(DrawElements,++LayerId,Geometry.ToPaintGeometry(),Line,ESlateDrawEffect::None,FLinearColor(1,1,1,DisplayAlpha),true,3.f);
        }
    }
    if(DisplayAlpha<.9999f)return LayerId;
    const FVector2D Screen=Geometry.GetLocalSize();
    FSlateFontInfo Font=HUD.IsValid()&&HUD->GameUIFont?FSlateFontInfo(HUD->GameUIFont,16):FCoreStyle::GetDefaultFontStyle("Regular",16);
    Font.OutlineSettings.OutlineSize=0;
    const FText First=DWText(this,TEXT("M 关闭 · Esc 设置"),TEXT("M Close · Esc Settings"));
    const FText Second=DWText(this,TEXT("拖动平移 · 滚轮缩放 · 右键标记"),TEXT("Drag · Wheel · Right-click to mark"));
    const FText Full=FText::Format(FText::FromString(TEXT("{0}    {1}")),First,Second);
    const float Available=float(FMath::Max(1.,Screen.X-InsetX-60.));
    bool TwoRows=false;
    if(FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetRenderer())
    {
        auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        TwoRows=Measure->Measure(Full,Font).X>Available;
        while(Font.Size>6&&(TwoRows?FMath::Max(Measure->Measure(First,Font).X,Measure->Measure(Second,Font).X):Measure->Measure(Full,Font).X)>Available)--Font.Size;
    }
    FSlateDrawElement::MakeText(DrawElements,++LayerId,Geometry.ToPaintGeometry(FVector2D(Available,24),FSlateLayoutTransform(FVector2D(InsetX,TwoRows?8.:17.))),TwoRows?First:Full,Font,ESlateDrawEffect::None,FLinearColor::White);
    if(TwoRows)FSlateDrawElement::MakeText(DrawElements,LayerId,Geometry.ToPaintGeometry(FVector2D(Available,24),FSlateLayoutTransform(FVector2D(InsetX,30.))),Second,Font,ESlateDrawEffect::None,FLinearColor::White);
    const FVector2D ClosePos=CloseOrigin(Screen),CloseSize(36.,36.);
    if(CloseArt)FSlateDrawElement::MakeBox(DrawElements,++LayerId,Geometry.ToPaintGeometry(CloseSize,FSlateLayoutTransform(ClosePos)),&CloseBrush,ESlateDrawEffect::None,FLinearColor::White);
    else
    {
        const TArray<FVector2D> A={ClosePos+FVector2D(8,8),ClosePos+FVector2D(28,28)};
        const TArray<FVector2D> B={ClosePos+FVector2D(28,8),ClosePos+FVector2D(8,28)};
        FSlateDrawElement::MakeLines(DrawElements,++LayerId,Geometry.ToPaintGeometry(),A,ESlateDrawEffect::None,FLinearColor::White,true,3.f);
        FSlateDrawElement::MakeLines(DrawElements,LayerId,Geometry.ToPaintGeometry(),B,ESlateDrawEffect::None,FLinearColor::White,true,3.f);
    }
    if(!LastError.IsEmpty())FSlateDrawElement::MakeText(DrawElements,++LayerId,Geometry.ToPaintGeometry(FVector2D(Geometry.GetLocalSize().X-2*InsetX,24),FSlateLayoutTransform(FVector2D(InsetX,Geometry.GetLocalSize().Y-30))),LastError,Font,ESlateDrawEffect::None,FLinearColor::White);
    return LayerId;
}

FReply UDWWorldMapScreen::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    FVector2D Point;auto* Subsystem=Map.Get();
    if(!bOpen||bClosing||bSettingsOverlayActive||!Subsystem||DisplayAlpha<.9999f||MotionProgress<1.||TransitionCapture->HasPending())return FReply::Handled();
    const FVector2D Local=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()),ClosePos=CloseOrigin(Geometry.GetLocalSize());
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton&&Local.X>=ClosePos.X&&Local.Y>=ClosePos.Y&&Local.X<ClosePos.X+36.&&Local.Y<ClosePos.Y+36.)
    {RequestClose();return FReply::Handled();}
    if(!LocalPoint(Geometry,Event.GetScreenSpacePosition(),Point))return FReply::Handled();
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton){bDragging=true;bMoved=false;PressStart=Point;PreviousDrag=Point;return FReply::Handled().CaptureMouse(TakeWidget());}
    if(Event.GetEffectingButton()==EKeys::RightMouseButton)
    {FString Error;if(!Subsystem->ToggleMarkerAtPixel(Point,MarkerHitRadius,Error))LastError=FText::FromString(Error.IsEmpty()?TEXT("Map marker unavailable."):Error);else {LastError=FText::GetEmpty();const FGuid Selected=Subsystem->GetSelectedMarkerId();if(Selected.IsValid()){PulseMarker=Selected;PulseAt=FPlatformTime::Seconds();}}}
    return FReply::Handled();
}
FReply UDWWorldMapScreen::NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton){
        if(bDragging&&!bMoved&&!bClosing&&DisplayAlpha>=.9999f)if(auto* Subsystem=Map.Get())
        {FVector2D Point;FGuid Id;FString Error;if(LocalPoint(Geometry,Event.GetScreenSpacePosition(),Point)&&Subsystem->SelectMarkerAtPixel(Point,MarkerHitRadius,Id,Error)){PulseMarker=Id;PulseAt=FPlatformTime::Seconds();}}
        bDragging=false;return FReply::Handled().ReleaseMouseCapture();}
    return FReply::Handled();
}
FReply UDWWorldMapScreen::NativeOnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event)
{
    FVector2D Point;if(bDragging&&!bClosing&&MotionProgress>=1.&&DisplayAlpha>=.9999f&&TransitionCapture&&!TransitionCapture->HasPending()&&Map.IsValid())
    {Point=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());bMoved|=FVector2D::Distance(Point,PressStart)>5.;if(FMath::IsFinite(Point.X)&&FMath::IsFinite(Point.Y)){Map->PanPixels(Point-PreviousDrag);PreviousDrag=Point;}}
    return FReply::Handled();
}
FReply UDWWorldMapScreen::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
    FVector2D Point;if(!bClosing&&MotionProgress>=1.&&DisplayAlpha>=.9999f&&TransitionCapture&&!TransitionCapture->HasPending()&&Map.IsValid()&&LocalPoint(Geometry,Event.GetScreenSpacePosition(),Point)&&FMath::IsFinite(Event.GetWheelDelta()))
        Map->ZoomAtPixel(FMath::Pow(1.15f,Event.GetWheelDelta()),Point);
    return FReply::Handled();
}
FReply UDWWorldMapScreen::NativeOnKeyDown(const FGeometry&,const FKeyEvent& Event)
{
    if(Event.IsRepeat())return FReply::Handled();
    if(auto* Owner=HUD.Get())
    {if(Event.GetKey()==EKeys::M)Owner->ToggleWorldMap();else if(Event.GetKey()==EKeys::Escape)Owner->OpenSettings();}
    return FReply::Handled();
}
void UDWWorldMapScreen::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event){bDragging=false;Super::NativeOnMouseCaptureLost(Event);}
void UDWWorldMapScreen::NativeOnFocusLost(const FFocusEvent& Event){bDragging=false;Super::NativeOnFocusLost(Event);}
