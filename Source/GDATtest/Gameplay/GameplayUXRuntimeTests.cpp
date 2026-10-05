// Explicit, isolated Development runtime verification. No ticker exists without -DWGameplayUXQA.
// Requires a fresh external report, DW_QA_ save prefix/directory and an external DW_QA_*.ini.
#include "CoreMinimal.h"
#include "CoreGlobals.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "DWGameInstance.h"
#include "DWMinimap.h"
#include "DWGameplayConfig.h"
#include "DWGameplayCinematic.h"
#include "DWGameplayHUD.h"
#include "DWGameplayWidget.h"
#include "DWInventoryComponent.h"
#include "DWUIEntryWidgets.h"
#include "DWLoadingTransition.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWSettingsPanel.h"
#include "DWSaveSettings.h"
#include "DWSaveStorage.h"
#include "DWUserSettings.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/WidgetSwitcher.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProperties.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"
#include "Widgets/SViewport.h"
#if __has_include("DWInventoryUXRuntimeChecks.h")
#include "DWInventoryUXRuntimeChecks.h"
#define DW_GAMEPLAY_UX_INVENTORY_CHECKS 1
#else
#define DW_GAMEPLAY_UX_INVENTORY_CHECKS 0
#endif
#if __has_include("GameplayUXAudioRuntimeChecks.h")
#include "GameplayUXAudioRuntimeChecks.h"
#define DW_GAMEPLAY_UX_AUDIO_CHECKS 1
#else
#define DW_GAMEPLAY_UX_AUDIO_CHECKS 0
#endif

namespace DWGameplayUXQA
{
static FString ActiveSettingsPath()
{
    if(GConfig)if(const FConfigFile* File=GConfig->FindConfigFile(GGameUserSettingsIni))
        if(File->Branch&&!File->Branch->IniPath.IsEmpty())return File->Branch->IniPath;
    return GGameUserSettingsIni;
}
static FString FullPath(FString Path)
{
    Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeFilename(Path);FPaths::CollapseRelativeDirectories(Path);
    while(Path.EndsWith(TEXT("/")))Path.LeftChopInline(1);return Path;
}
static bool SamePath(const FString& A,const FString& B){return FullPath(A).Equals(FullPath(B),ESearchCase::IgnoreCase);}
static bool OutsideProject(const FString& Path)
{
    return !FPaths::IsUnderDirectory(FullPath(Path),FullPath(FPaths::ProjectDir()))
        &&!FPaths::IsUnderDirectory(FullPath(Path),FullPath(FPaths::ProjectSavedDir()));
}
static bool OutsideFormalDocuments(const FString& Path)
{
    // IsUnderDirectory also rejects the directory itself; protect its complete subtree for every output.
    const FString Formal=DWSaveStorage::DocumentsDirectory(GetDefault<UDWSaveSettings>()->DocumentsFolderName);
    return !FPaths::IsUnderDirectory(FullPath(Path),FullPath(Formal));
}
enum class EStage : uint8
{
    Menu,FrontendSettingsReady,FrontendShotWait,MenuNewGame,WaitGame,Defaults,Bindings,MiddleDrag,RightDrag,NoDrag,LowSensitivity,HighSensitivity,
    WheelOne,WheelNear,NearShotWait,WheelFar,FarShotWait,SmoothObserve,SmoothCompare,
    SettingsOpen,SettingsCheck,SettingsShotWait,Paused,PauseCheck,Cinematic,CinematicCheck,CinematicReturn,
    AudioBegin,AudioWait,BookNavigationReady,BookNavigationHover,BookNavigationDown,BookNavigationUp,BookPageReady,BookShotWait,
    InventoryOpen,InventoryCheck,InventoryPageReady,InventoryShotWait,
    GlobalInventoryReady,GlobalSlotHover,GlobalSlotDown,GlobalDragMove,GlobalDrop,
    GlobalPopupReady,GlobalPopupShotWait,GlobalButtonHover,GlobalButtonDown,GlobalButtonUp,GlobalCycleWait,FinalWait
};
struct FRun
{
    TWeakObjectPtr<UDWGameInstance> GI;
    TWeakObjectPtr<ADWPlayerCharacter> ProtectedPlayer;
    TWeakObjectPtr<UDWCinematicComponent> Cinematic;
    FString Report,SaveDirectory,Prefix,UserIni,ShotDirectory;
    TArray<TSharedPtr<FJsonValue>> Checks,Samples,LayoutSamples,RoutedInputs,Skipped;
    TArray<FString> Shots;
    EStage Stage=EStage::Menu;
    double Started=FPlatformTime::Seconds(),StageStarted=Started;
    bool bPassed=true,bOldDamage=true,bSawSmooth=false,bFinished=false;
    float BeforeYaw=0.f,BeforePitch=0.f,BeforeTarget=0.f,BeforeLength=0.f;
    float SmoothReference=0.f,SmoothTime=0.f,OriginalSensitivity=1.f;
    float OriginalMin=600.f,OriginalMax=3000.f,OriginalStep=150.f,OriginalSpeed=10.f,OriginalDistance=1800.f;
    int32 BookTab=0,StableBookFrames=0,GlobalCycle=0;
    EDWMenuPage FrontendReturnPage=EDWMenuPage::Title;
    TArray<FKey> BookBindingsBefore;
    FVector2D BookAudioPosition=FVector2D::ZeroVector;
    FVector2D LastBookOrigin=FVector2D::ZeroVector,LastBookSize=FVector2D::ZeroVector;
    FVector2D SlatePosition=FVector2D::ZeroVector,SlotPosition=FVector2D::ZeroVector,DropPosition=FVector2D::ZeroVector,PopupButtonPosition=FVector2D::ZeroVector;
    int32 GlobalWaterBefore=0,GlobalStackQuantity=0;
    int64 GlobalRevisionBefore=0;
    bool bSlateButtonDown=false,bSlateSequenceStarted=false;
    bool bOldBackgroundInput=false,bManagedBackgroundInput=false;
    FKey SlateHeldButton=EKeys::LeftMouseButton;
    uint64 LastSlateDownFrame=0,LastSlateMoveFrame=0;
#if DW_GAMEPLAY_UX_AUDIO_CHECKS
    TSharedPtr<FDWGameplayUXAudioCheck> AudioCheck;
#endif
    void Check(const FString& Name,bool bResult,const FString& Detail=FString())
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("name"),Name);Item->SetBoolField(TEXT("passed"),bResult);
        Item->SetStringField(TEXT("detail"),Detail);Checks.Add(MakeShared<FJsonValueObject>(Item));bPassed&=bResult;
        UE_LOG(LogTemp,Log,TEXT("DW_GAMEPLAY_UX_QA %s: %s %s"),bResult?TEXT("PASS"):TEXT("FAIL"),*Name,*Detail);
    }
    void Skip(const FString& Reason){Skipped.Add(MakeShared<FJsonValueString>(Reason));UE_LOG(LogTemp,Warning,TEXT("DW_GAMEPLAY_UX_QA SKIPPED: %s"),*Reason);}
    void Advance(EStage Next){Stage=Next;StageStarted=FPlatformTime::Seconds();}
    bool IsolationValid()const
    {
        const auto* Instance=GI.Get();
        return Instance&&SamePath(Instance->GetSaveDirectory(),SaveDirectory)&&SamePath(ActiveSettingsPath(),UserIni)
            &&OutsideProject(Report)&&OutsideProject(UserIni)&&OutsideProject(SaveDirectory)
            &&OutsideFormalDocuments(Report)&&OutsideFormalDocuments(UserIni)&&OutsideFormalDocuments(SaveDirectory);
    }
    UWorld* World()const{return GI.IsValid()?GI->GetWorld():nullptr;}
    static void Key(ADWPlayerController* PC,FKey Input,EInputEvent Event,float Amount=1.f)
    {
        if(PC)PC->InputKey(FInputKeyEventArgs::CreateSimulated(Input,Event,Amount,Input.IsAxis1D()?1:-1));
    }
    static void Mouse(ADWPlayerController* PC,float X,float Y=0.f)
    {Key(PC,EKeys::MouseX,IE_Axis,X);Key(PC,EKeys::MouseY,IE_Axis,Y);}
    static void Wheel(ADWPlayerController* PC,float Amount){Key(PC,EKeys::MouseWheelAxis,IE_Axis,Amount);}
    static void ReleaseMouse(ADWPlayerController* PC)
    {Key(PC,EKeys::MiddleMouseButton,IE_Released,0.f);Key(PC,EKeys::RightMouseButton,IE_Released,0.f);}
    static USpringArmComponent* Arm(ADWPlayerCharacter* Player){return Player?Player->FindComponentByClass<USpringArmComponent>():nullptr;}
    void VerifyMinimapCamera(ADWPlayerCharacter* Player,const TCHAR* Phase)
    {
        auto* Map=Player->FindComponentByClass<UDWMinimapComponent>();
        Check(FString(Phase)+TEXT(" minimap follows camera by default"),Map&&Map->bFollowCameraYaw);
        if(!Map)return;
        const float Yaw=Arm(Player)->GetComponentRotation().Yaw;
        Check(FString(Phase)+TEXT(" minimap capture yaw matches live camera"),FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw,Map->GetCaptureYaw()))<.1f,
            FString::Printf(TEXT("Camera=%.3f Map=%.3f"),Yaw,Map->GetCaptureYaw()));
        const float R=FMath::DegreesToRadians(Player->GetActorRotation().Yaw-Yaw);
        Check(FString(Phase)+TEXT(" player arrow follows shared rotating projection"),Map->GetPlayerForwardOnMap().Equals(FVector2D(FMath::Sin(R),-FMath::Cos(R)),.002));
        const FVector WorldForward=FRotator(0,Yaw,0).Vector();
        Check(FString(Phase)+TEXT(" shared marker compass and edge direction face camera up"),Map->GetWorldDirectionOnMap(WorldForward).Equals(FVector2D(0,-1),.002));
    }
    void Remember(ADWPlayerCharacter* Player)
    {
        auto* Spring=Arm(Player);BeforeYaw=Spring?Spring->GetComponentRotation().Yaw:0.f;
        BeforePitch=Spring?Spring->GetComponentRotation().Pitch:0.f;
        BeforeTarget=Player->CameraDistance;BeforeLength=Player->GetCurrentCameraDistance();
    }
    bool SameCamera(ADWPlayerCharacter* Player)const
    {
        const auto* Spring=Arm(Player);
        return Spring&&FMath::Abs(FMath::FindDeltaAngleDegrees(BeforeYaw,Spring->GetComponentRotation().Yaw))<.1f
            &&FMath::Abs(FMath::FindDeltaAngleDegrees(BeforePitch,Spring->GetComponentRotation().Pitch))<.1f
            &&FMath::IsNearlyEqual(BeforeTarget,Player->CameraDistance,.1f)
            &&FMath::IsNearlyEqual(BeforeLength,Player->GetCurrentCameraDistance(),.1f);
    }
    void Sample(const TCHAR* Name,ADWPlayerCharacter* Player)
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("stage"),Name);
        Item->SetNumberField(TEXT("targetDistanceCm"),Player->CameraDistance);
        Item->SetNumberField(TEXT("actualArmLengthCm"),Player->GetCurrentCameraDistance());
        if(auto* Spring=Arm(Player))
        {Item->SetNumberField(TEXT("yaw"),Spring->GetComponentRotation().Yaw);Item->SetNumberField(TEXT("pitch"),Spring->GetComponentRotation().Pitch);}
        if(auto* Settings=UDWUserSettings::Resolve(Player))Item->SetNumberField(TEXT("sensitivityMultiplier"),Settings->GetCameraSensitivityMultiplier());
        Samples.Add(MakeShared<FJsonValueObject>(Item));
    }
    void Shot(const TCHAR* Name)
    {
        const FString File=FPaths::Combine(ShotDirectory,FPaths::GetBaseFilename(Report)+TEXT("_")+Name+TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(File,true,true);Shots.Add(FScreenshotRequest::GetFilename());
    }
    static bool CurrentPageSettled(UDWGameplayWidget* Widget)
    {
        const auto* Switcher=Widget?Cast<UWidgetSwitcher>(Widget->GetWidgetFromName(TEXT("PageSwitcher"))):nullptr;
        const auto* Page=Switcher?Switcher->GetActiveWidget():nullptr;
        return Page&&Page->IsVisible()&&Page->GetRenderOpacity()>=.95f;
    }
    // Resolve the current arranged path rather than tick/paint caches, which can be zero for no-tick UMG trees.
    static bool Arranged(const TSharedPtr<SWidget>& SlateWidget,FGeometry& Geometry,FWidgetPath* OutPath=nullptr)
    {
        if(!FSlateApplication::IsInitialized()||!SlateWidget.IsValid())return false;
        FWidgetPath Path;if(!FSlateApplication::Get().GeneratePathToWidgetUnchecked(SlateWidget.ToSharedRef(),Path))return false;
        const auto Found=Path.FindArrangedWidget(SlateWidget.ToSharedRef());if(!Found.IsSet())return false;
        Geometry=Found.GetValue().Geometry;if(OutPath)*OutPath=Path;
        return Geometry.GetLocalSize().X>1.f&&Geometry.GetLocalSize().Y>1.f;
    }
    static bool Arranged(UWidget* Widget,FGeometry& Geometry,FWidgetPath* OutPath=nullptr)
    {return Widget&&Widget->IsVisible()&&Arranged(Widget->GetCachedWidget(),Geometry,OutPath);}
    static bool ViewportGeometry(FGeometry& Geometry)
    {
        const auto Viewport=GEngine&&GEngine->GameViewport?GEngine->GameViewport->GetGameViewportWidget():nullptr;
        return Arranged(Viewport,Geometry);
    }
    static bool Inside(const FGeometry& Outer,const FGeometry& Inner)
    {
        const FVector2D Size=Inner.GetLocalSize(),Limit=Outer.GetLocalSize();
        for(const FVector2D Corner:{FVector2D::ZeroVector,FVector2D(Size.X,0),Size,FVector2D(0,Size.Y)})
        {
            const FVector2D Point=Outer.AbsoluteToLocal(Inner.LocalToAbsolute(Corner));
            if(Point.X<-.5f||Point.Y<-.5f||Point.X>Limit.X+.5f||Point.Y>Limit.Y+.5f)return false;
        }
        return true;
    }
    static UDWInventorySlotWidget* InventorySlot(UDWGameplayWidget* Widget)
    {
        auto* Grid=Widget?Cast<UUniformGridPanel>(Widget->GetWidgetFromName(TEXT("InventoryGrid"))):nullptr;
        return Grid?Cast<UDWInventorySlotWidget>(Grid->GetChildAt(0)):nullptr;
    }
    static FWidgetPath HitPath(const FVector2D& Position)
    {
        auto& Slate=FSlateApplication::Get();
        return Slate.LocateWindowUnderMouse(Position,Slate.GetInteractiveTopLevelWindows(),false,Slate.GetUserIndexForMouse());
    }
    static bool HitContains(const FVector2D& Position,UWidget* Widget)
    {
        const auto SlateWidget=Widget?Widget->GetCachedWidget():nullptr;
        return SlateWidget.IsValid()&&HitPath(Position).ContainsWidget(SlateWidget.Get());
    }
    FPointerEvent Pointer(const FVector2D& Position,const FKey& EffectingButton,bool bPressed)const
    {
        TSet<FKey> Pressed;if(bPressed)Pressed.Add(SlateHeldButton);
        return FPointerEvent(FSlateApplication::Get().GetUserIndexForMouse(),FSlateApplication::CursorPointerIndex,
            Position,SlatePosition,Pressed,EffectingButton,0.f,FModifierKeysState());
    }
    void RecordSlate(const TCHAR* Event,const FVector2D& Position)
    {
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("event"),Event);Item->SetStringField(TEXT("screenPosition"),Position.ToString());
        Item->SetNumberField(TEXT("frame"),double(GFrameCounter));Item->SetNumberField(TEXT("cycle"),GlobalCycle);
        Item->SetNumberField(TEXT("qaStage"),int32(Stage));
        Item->SetStringField(TEXT("button"),SlateHeldButton.ToString());RoutedInputs.Add(MakeShared<FJsonValueObject>(Item));
    }
    void SlateMove(const FVector2D& Position)
    {
        // false is required: Slate's own synthetic-hover flag suppresses drag detection.
        FSlateApplication::Get().ProcessMouseMoveEvent(Pointer(Position,EKeys::Invalid,bSlateButtonDown),false);
        SlatePosition=Position;bSlateSequenceStarted=true;LastSlateMoveFrame=GFrameCounter;RecordSlate(TEXT("Move"),Position);
    }
    void SlateDown(const FVector2D& Position,FKey Button=EKeys::LeftMouseButton)
    {
        SlateHeldButton=Button;
        // Null native window still routes input, without activating a platform window or moving the OS cursor.
        FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr,Pointer(Position,SlateHeldButton,true));
        SlatePosition=Position;bSlateButtonDown=true;bSlateSequenceStarted=true;LastSlateDownFrame=GFrameCounter;RecordSlate(TEXT("Down"),Position);
    }
    void SlateUp(const FVector2D& Position)
    {
        FSlateApplication::Get().ProcessMouseButtonUpEvent(Pointer(Position,SlateHeldButton,false));
        SlatePosition=Position;bSlateButtonDown=false;RecordSlate(TEXT("Up"),Position);
    }
    void ReenterHeldButton(UButton* Button,const FVector2D& Position)
    {
        // QA only: the physical cursor is deliberately untouched. A platform hover poll
        // can clear Hovered while retaining the last path. Route an actual leave/re-enter
        // through Slate so both caches agree, without directly calling button callbacks.
        FGeometry Geometry;
        if(Arranged(Button,Geometry))
        {
            SlateMove(Geometry.LocalToAbsolute(FVector2D(-12.f,Geometry.GetLocalSize().Y*.5f)));
            SlateMove(Position);
        }
    }
    static FString ButtonState(UButton* Button)
    {
        const auto SlateWidget=Button?Button->GetCachedWidget():nullptr;
        return FString::Printf(TEXT("Pressed=%d Hovered=%d Capture=%d"),Button&&Button->IsPressed(),Button&&Button->IsHovered(),SlateWidget.IsValid()&&SlateWidget->HasMouseCapture());
    }
    bool BookFrameStable(UDWGameplayWidget* Widget)
    {
        FGeometry Cover;if(!Arranged(Widget->GetWidgetFromName(TEXT("SettingsBookCover")),Cover))return false;
        const FVector2D Origin=Cover.LocalToAbsolute(FVector2D::ZeroVector),Size=Cover.GetLocalSize();
        if(Origin.Equals(LastBookOrigin,.5f)&&Size.Equals(LastBookSize,.5f))++StableBookFrames;else StableBookFrames=0;
        LastBookOrigin=Origin;LastBookSize=Size;return StableBookFrames>=2;
    }
    void VerifyBookPage(UDWGameplayWidget* Widget,bool bFrontend=false)
    {
        static const TCHAR* Tabs[]={TEXT("SettingsBookGraphicsTabButton"),TEXT("SettingsBookAudioTabButton"),TEXT("SettingsBookKeysTabButton"),TEXT("SettingsBookControllerTabButton"),TEXT("SettingsBookGeneralTabButton")};
        const FString PrefixName=FString::Printf(TEXT("%ssettings.book.page%d."),bFrontend?TEXT("frontend."):TEXT(""),BookTab);
        auto* Pages=Cast<UWidgetSwitcher>(Widget->GetWidgetFromName(TEXT("SettingsBookPages")));
        const int32 ExpectedPage=BookTab==0?0:(BookTab==4?2:1);
        FGeometry View,Cover,Top,Bottom,Body;
        const bool bView=ViewportGeometry(View);
        const bool bCover=Arranged(Widget->GetWidgetFromName(TEXT("SettingsBookCover")),Cover);
        const bool bBody=Pages&&Pages->GetActiveWidgetIndex()==ExpectedPage&&Arranged(Pages->GetActiveWidget(),Body);
        Check(PrefixName+TEXT("correct_visible_content"),Widget->GetSettingsBookTab()==BookTab&&bBody&&CurrentPageSettled(Widget),FString::Printf(TEXT("Book=%d Content=%d"),Widget->GetSettingsBookTab(),Pages?Pages->GetActiveWidgetIndex():-1));
        Check(PrefixName+TEXT("whole_cover_within_viewport"),bView&&bCover&&Inside(View,Cover));
        const bool bTop=Arranged(Widget->GetWidgetFromName(TEXT("SettingsBookTabs")),Top);
        const bool bBottom=Arranged(Widget->GetWidgetFromName(TEXT("SettingsBookActions")),Bottom);
        bool bPersistent=bView&&bTop&&bBottom&&Inside(View,Top)&&Inside(View,Bottom);
        for(const TCHAR* Name:Tabs){FGeometry G;bPersistent&=Arranged(Widget->GetWidgetFromName(Name),G)&&bView&&Inside(View,G);}
        for(const TCHAR* Name:{TEXT("SettingsBackButton"),TEXT("ApplySettingsButton")}){FGeometry G;bPersistent&=Arranged(Widget->GetWidgetFromName(Name),G)&&bView&&Inside(View,G);}
        Check(PrefixName+TEXT("all_tabs_and_bottom_actions_visible"),bPersistent);
        FGeometry Master;const bool bMaster=Arranged(Widget->GetWidgetFromName(TEXT("VolumeSlider")),Master);
        Check(PrefixName+TEXT("master_volume_only_on_audio"),bMaster==(BookTab==1),bMaster?Master.GetLocalSize().ToString():TEXT("No visible arranged path"));
        if(BookTab>=1&&BookTab<=3)
        {
            auto* Panel=Cast<UDWSettingsPanel>(Widget->GetWidgetFromName(TEXT("ExtendedSettings")));
            auto* Options=Panel?Cast<UWidgetSwitcher>(Panel->GetWidgetFromName(TEXT("OptionsSwitcher"))):nullptr;
            FGeometry OptionPage;Check(PrefixName+TEXT("correct_shared_options_page"),Options&&Options->GetActiveWidgetIndex()==BookTab-1&&Arranged(Options->GetActiveWidget(),OptionPage));
            auto* InnerTabs=Panel?Panel->GetWidgetFromName(TEXT("SettingsTabs")):nullptr;FGeometry OldTabs;
            Check(PrefixName+TEXT("old_internal_tabs_hidden_and_disabled"),InnerTabs&&!InnerTabs->GetIsEnabled()&&!Arranged(InnerTabs,OldTabs));
        }
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("stage"),PrefixName);
        Item->SetStringField(TEXT("coverLocalSize"),bCover?Cover.GetLocalSize().ToString():TEXT("missing"));
        Item->SetStringField(TEXT("viewportLocalSize"),bView?View.GetLocalSize().ToString():TEXT("missing"));
        Item->SetNumberField(TEXT("stableLayoutFrames"),StableBookFrames);LayoutSamples.Add(MakeShared<FJsonValueObject>(Item));
    }
    bool PrepareGlobalDrag(UDWGameplayWidget* Widget)
    {
        FGeometry Slot,Card,Root,View;
        auto* Entry=InventorySlot(Widget);auto* Modal=Widget->GetWidgetFromName(TEXT("InventoryDiscardModal"));
        if(!FSlateApplication::IsInitialized()||!Arranged(Entry,Slot)||!Arranged(Widget->GetWidgetFromName(TEXT("InventoryCard")),Card)
            ||!Modal||!Arranged(Modal->GetParent(),Root)||!ViewportGeometry(View))return false;
        SlotPosition=Slot.LocalToAbsolute(Slot.GetLocalSize()*.5f);
        const FVector2D Size=Root.GetLocalSize();bool bOutside=false;FGeometry Footer;
        const bool bFooter=Arranged(Widget->GetWidgetFromName(TEXT("InventoryControlsFooter")),Footer);
        // First exercise unconstrained near-cursor placement; then force clamping at a viewport edge.
        const TArray<FVector2D> Candidates=GlobalCycle==0
            ?TArray<FVector2D>{FVector2D(Size.X*.12,Size.Y*.45),FVector2D(Size.X*.75,Size.Y*.45)}
            :TArray<FVector2D>{Size-FVector2D(4,4),FVector2D(4,Size.Y-4),FVector2D(Size.X-4,4),FVector2D(4,4)};
        for(const FVector2D Candidate:Candidates)
        {
            const FVector2D Screen=Root.LocalToAbsolute(Candidate);
            if(!Card.IsUnderLocation(Screen)&&(!bFooter||!Footer.IsUnderLocation(Screen))&&View.IsUnderLocation(Screen)){DropPosition=Screen;bOutside=true;break;}
        }
        return bOutside&&HitContains(SlotPosition,Entry)&&HitContains(DropPosition,Widget);
    }
    bool VerifyPopupGeometry(UDWGameplayWidget* Widget)
    {
        auto* Modal=Widget->GetWidgetFromName(TEXT("InventoryDiscardModal"));
        auto* CanvasSlot=Modal?Cast<UCanvasPanelSlot>(Modal->Slot):nullptr;
        FGeometry Popup,Root,View,Confirm,Cancel;
        const bool bGeometry=Modal&&CanvasSlot&&Arranged(Modal,Popup)&&Arranged(Modal->GetParent(),Root)&&ViewportGeometry(View);
        const bool bButtons=Arranged(Widget->GetWidgetFromName(TEXT("InventoryDiscardConfirmButton")),Confirm)
            &&Arranged(Widget->GetWidgetFromName(TEXT("InventoryDiscardCancelButton")),Cancel);
        const FString PrefixName=FString::Printf(TEXT("inventory.global.cycle%d."),GlobalCycle);
        Check(PrefixName+TEXT("popup_and_buttons_fully_inside_viewport"),bGeometry&&bButtons&&Inside(View,Popup)&&Inside(View,Confirm)&&Inside(View,Cancel));
        if(!bGeometry)return false;
        const FVector2D RootSize=Root.GetLocalSize(),PopupSize=CanvasSlot->GetSize(),Release=Root.AbsoluteToLocal(DropPosition);
        const FVector2D Expected(FMath::Clamp(Release.X+8.0,8.0,FMath::Max(8.0,RootSize.X-PopupSize.X-8.0)),
            FMath::Clamp(Release.Y+8.0,8.0,FMath::Max(8.0,RootSize.Y-PopupSize.Y-8.0)));
        const FVector2D Actual=CanvasSlot->GetPosition();const float AnchorError=FVector2D::Distance(Expected,Actual);
        const FVector2D A=View.AbsoluteToLocal(Popup.LocalToAbsolute(FVector2D::ZeroVector));
        const FVector2D B=View.AbsoluteToLocal(Popup.LocalToAbsolute(Popup.GetLocalSize()));
        const FVector2D ActualSize=B-A,ViewSize=View.GetLocalSize();
        const double Fraction=ActualSize.X*ActualSize.Y/FMath::Max(1.0,ViewSize.X*ViewSize.Y);
        Check(PrefixName+TEXT("popup_is_small_release_point_window"),PopupSize.Equals(FVector2D(260,128),.5f)&&Fraction<.25&&ActualSize.X<ViewSize.X*.5&&ActualSize.Y<ViewSize.Y*.5,FString::Printf(TEXT("Logical=%s Screen=%s Area=%.4f"),*PopupSize.ToString(),*ActualSize.ToString(),Fraction));
        Check(PrefixName+TEXT("release_anchor_and_edge_clamp"),AnchorError<=1.f,FString::Printf(TEXT("Release=%s Expected=%s Actual=%s Error=%.3f logicalpx"),*Release.ToString(),*Expected.ToString(),*Actual.ToString(),AnchorError));
        Check(PrefixName+TEXT("requested_free_or_edge_case_is_exercised"),Expected.Equals(Release+FVector2D(8,8),1.f)==(GlobalCycle==0));
        auto* Blocker=Widget->GetWidgetFromName(TEXT("InventoryDiscardHitBlocker"));
        Check(PrefixName+TEXT("modal_hit_route_blocks_underlying_slot"),Blocker&&HitContains(SlotPosition,Blocker)&&!HitContains(SlotPosition,InventorySlot(Widget)));
        const FVector2D Origin=Root.LocalToAbsolute(FVector2D::ZeroVector);
        auto Item=MakeShared<FJsonObject>();Item->SetStringField(TEXT("stage"),PrefixName);
        Item->SetStringField(TEXT("placementCase"),GlobalCycle==0?TEXT("Free backdrop near cursor"):TEXT("Viewport edge with clamping"));
        Item->SetStringField(TEXT("releaseRootLocal"),Release.ToString());Item->SetStringField(TEXT("expectedPopupPosition"),Expected.ToString());
        Item->SetStringField(TEXT("actualPopupPosition"),Actual.ToString());Item->SetStringField(TEXT("popupLogicalSize"),PopupSize.ToString());
        Item->SetNumberField(TEXT("anchorErrorLogicalPixels"),AnchorError);Item->SetNumberField(TEXT("popupViewportAreaFraction"),Fraction);
        Item->SetNumberField(TEXT("rootDpiScaleX"),FVector2D::Distance(Origin,Root.LocalToAbsolute(FVector2D(1,0))));
        Item->SetNumberField(TEXT("rootDpiScaleY"),FVector2D::Distance(Origin,Root.LocalToAbsolute(FVector2D(0,1))));
        LayoutSamples.Add(MakeShared<FJsonValueObject>(Item));
        if(bButtons){const FGeometry& Button=GlobalCycle==0?Cancel:Confirm;PopupButtonPosition=Button.LocalToAbsolute(Button.GetLocalSize()*.5f);}
        return bButtons;
    }
    void Restore(ADWPlayerCharacter* Player,ADWPlayerController* PC)
    {
        ReleaseMouse(PC);if(Cinematic.IsValid()&&Cinematic->IsPlaying())Cinematic->CancelCinematic();
        if(bSlateSequenceStarted&&FSlateApplication::IsInitialized())
        {
            if(bSlateButtonDown)SlateUp(SlatePosition);
            if(FSlateApplication::Get().IsDragDropping())FSlateApplication::Get().CancelDragDrop();
            FSlateApplication::Get().ReleaseAllPointerCapture(FSlateApplication::Get().GetUserIndexForMouse());
        }
        if(bManagedBackgroundInput&&FSlateApplication::IsInitialized())
            FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(bOldBackgroundInput);
        if(PC&&PC->IsPaused())PC->SetPause(false);
        if(Player)
        {
            Player->MinCameraDistance=OriginalMin;Player->MaxCameraDistance=OriginalMax;Player->CameraZoomStep=OriginalStep;
            Player->CameraZoomInterpSpeed=OriginalSpeed;Player->CameraDistance=OriginalDistance;Player->PrepareCameraForReveal();
            if(ProtectedPlayer.Get()==Player)Player->SetCanBeDamaged(bOldDamage);
        }
    }
    bool Finish(const FString& Error=FString())
    {
        if(bFinished)return false;bFinished=true;if(!Error.IsEmpty())Check(TEXT("Runtime completes without interruption"),false,Error);
        UWorld* Current=World();auto* PC=Current?Cast<ADWPlayerController>(Current->GetFirstPlayerController()):nullptr;
        auto* Player=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;Restore(Player,PC);
        auto Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("passed"),bPassed);
        Result->SetStringField(TEXT("mode"),FPlatformProperties::RequiresCookedData()?TEXT("Windows Development cooked"):TEXT("Standalone game"));
        Result->SetNumberField(TEXT("elapsedSeconds"),FPlatformTime::Seconds()-Started);
        Result->SetStringField(TEXT("saveDirectory"),SaveDirectory);Result->SetStringField(TEXT("savePrefix"),Prefix);
        Result->SetStringField(TEXT("gameUserSettingsIni"),UserIni);Result->SetNumberField(TEXT("slot"),0);
        Result->SetArrayField(TEXT("checks"),Checks);Result->SetArrayField(TEXT("cameraSamples"),Samples);Result->SetArrayField(TEXT("layoutSamples"),LayoutSamples);Result->SetArrayField(TEXT("routedInputSamples"),RoutedInputs);Result->SetArrayField(TEXT("optionalSkipped"),Skipped);
        TArray<TSharedPtr<FJsonValue>> Images;for(const auto& File:Shots)Images.Add(MakeShared<FJsonValueString>(File));Result->SetArrayField(TEXT("screenshots"),Images);
        Result->SetStringField(TEXT("inputBoundary"),TEXT("Camera uses simulated PlayerController::InputKey. The inventory global sequence sends frame-separated FPointerEvents through FSlateApplication Process methods, including hit testing, preview/bubble, drag detection, capture and drop routing. Before each captured button release, global held-pointer moves leave and re-enter its actual geometry in the release frame to synchronize hover after platform cursor polling. No button callbacks are invoked directly by that global sequence. This is engine-level input injection, not OS hardware input. The separate inventory helper also exercises direct widget events and atomic APIs."));
        Result->SetStringField(TEXT("verificationBoundary"),TEXT("Current configured gameplay map only. No editor asset saves, no shipping or other-machine test. GPU performance and complete gameplay are outside this QA."));
        Result->SetStringField(TEXT("backgroundInputBoundary"),TEXT("This explicitly isolated QA process temporarily permits Slate input while the application is inactive, so synthetic Down replies can capture the pointer. The original flag is restored before report/exit. Normal launches never set this flag."));
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Result,Writer);
        if(!FFileHelper::SaveStringToFile(Text,*Report,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))UE_LOG(LogTemp,Error,TEXT("DW_GAMEPLAY_UX_QA could not write report: %s"),*Report);
        if(GEngine&&Current)GEngine->Exec(Current,TEXT("QUIT"));return false;
    }
    void VerifyBindings(ADWPlayerController* PC)
    {
        const auto Before=PC->GetConfiguredKeys();const int32 Camera=int32(EDWInputAction::CameraDrag),Harvest=int32(EDWInputAction::Harvest);
        if(Before.Num()!=int32(EDWInputAction::Count)){Check(TEXT("Input binding schema is intact"),false);return;}
        auto Persist=[&](const TArray<FKey>& Keys,int32 Version)
        {
            for(int32 I=0;I<Keys.Num();++I)GConfig->SetString(TEXT("DoughWorld.Input"),*StaticEnum<EDWInputAction>()->GetNameStringByValue(I),*Keys[I].GetFName().ToString(),GGameUserSettingsIni);
            GConfig->SetInt(TEXT("DoughWorld.Input"),TEXT("CameraDragDefaultVersion"),Version,GGameUserSettingsIni);GConfig->Flush(false,GGameUserSettingsIni);
        };
        auto Legacy=Before;Legacy[Camera]=EKeys::RightMouseButton;
        FKey Custom=Before[0];
        for(FKey Candidate:{EKeys::Up,EKeys::O,EKeys::K,EKeys::V})
        {bool Used=false;for(int32 I=1;I<Legacy.Num();++I)Used|=Legacy[I]==Candidate;if(!Used){Custom=Candidate;break;}}
        Legacy[0]=Custom;Persist(Legacy,1);PC->LoadSavedBindings();
        const auto Migrated=PC->GetConfiguredKeys();bool Others=true;
        for(int32 I=0;I<Legacy.Num();++I)if(I!=Camera)Others&=Migrated[I]==Legacy[I];
        Check(TEXT("Legacy RMB camera binding migrates to MMB while all other custom keys remain"),Migrated[Camera]==EKeys::MiddleMouseButton&&Others);
        auto Conflict=Before;Conflict[Camera]=EKeys::RightMouseButton;Conflict[Harvest]=EKeys::MiddleMouseButton;
        Persist(Conflict,1);PC->LoadSavedBindings();const auto Kept=PC->GetConfiguredKeys();
        Check(TEXT("Migration preserves a conflicting custom MMB action without resetting bindings"),Kept[Camera]==EKeys::RightMouseButton&&Kept[Harvest]==EKeys::MiddleMouseButton);
        auto Intentional=Before;Intentional[Camera]=EKeys::RightMouseButton;Persist(Intentional,2);PC->LoadSavedBindings();
        Check(TEXT("A deliberate RMB rebind after upgrade remains RMB on reload"),PC->GetConfiguredKeys()[Camera]==EKeys::RightMouseButton);
        auto Restored=Before;Restored[Camera]=EKeys::MiddleMouseButton;PC->AssignKeys(Restored);FText Error;
        Check(TEXT("Validated MMB bindings queue safe application"),PC->ApplyInputBindings(Error),Error.ToString());PC->SaveBindings();
    }
    bool Tick(float)
    {
        if(FPlatformTime::Seconds()-Started>120.0)return Finish(TEXT("120-second timeout"));
        if(!IsolationValid())return Finish(TEXT("Save/settings isolation changed; refusing further mutations"));
        auto* Current=World();if(!Current||Current->bIsTearingDown)return true;
        auto* Instance=GI.Get();const double Elapsed=FPlatformTime::Seconds()-StageStarted;
        if(Stage==EStage::Menu)
        {
            if(Elapsed<1.0)return true;auto* Config=Instance->GetConfig();
            Check(TEXT("Launch starts in the configured existing menu"),Config&&Current->GetPackage()->GetName()==Config->MainMenuMap);
            if(!Config||Current->GetPackage()->GetName()!=Config->MainMenuMap)return Finish(TEXT("Will not rewrite the map data asset"));
            auto* FrontPC=Current->GetFirstPlayerController();auto* FrontHUD=FrontPC?Cast<ADWGameplayHUD>(FrontPC->GetHUD()):nullptr;
            auto* FrontWidget=FrontHUD?FrontHUD->GetGameplayWidget():nullptr;
            if(!FrontHUD||!FrontWidget){if(Elapsed<3.0)return true;return Finish(TEXT("The existing main-menu HUD/frontend WBP is unavailable"));}
            Check(TEXT("Frontend verification uses the authored frontend WBP"),FrontWidget->GetClass()->GetPathName().Contains(TEXT("/WBP_DWFrontend.")),FrontWidget->GetClass()->GetPathName());
            FrontendReturnPage=FrontHUD->GetMenuPage();FrontHUD->OpenSettings();BookTab=0;StableBookFrames=0;FrontWidget->SelectSettingsBookTab(0);
            FrontHUD->Notify(FText::GetEmpty());Advance(EStage::FrontendSettingsReady);return true;
        }
        if(Stage==EStage::FrontendSettingsReady||Stage==EStage::FrontendShotWait)
        {
            auto* FrontPC=Current->GetFirstPlayerController();auto* FrontHUD=FrontPC?Cast<ADWGameplayHUD>(FrontPC->GetHUD()):nullptr;
            auto* FrontWidget=FrontHUD?FrontHUD->GetGameplayWidget():nullptr;
            if(!FrontHUD||!FrontWidget)return Finish(TEXT("Frontend disappeared before settings verification completed"));
            if(Stage==EStage::FrontendSettingsReady)
            {
                const bool bStable=BookFrameStable(FrontWidget);
                if(Elapsed<FMath::Max(.7f,FrontWidget->PageEnterSeconds+.1f))return true;
                if((!CurrentPageSettled(FrontWidget)||!bStable)&&Elapsed<3.0)return true;
                Check(TEXT("frontend.settings.waits_for_visible_stable_entrance"),FrontHUD->GetMenuPage()==EDWMenuPage::Settings&&CurrentPageSettled(FrontWidget)&&bStable);
                VerifyBookPage(FrontWidget,true);Shot(TEXT("FrontendSettings"));Advance(EStage::FrontendShotWait);return true;
            }
            if(Elapsed<.35)return true;FrontHUD->ReturnFromSettings();
            Check(TEXT("Frontend ReturnFromSettings restores its prior menu without starting gameplay"),FrontHUD->GetMenuPage()==FrontendReturnPage);
            Advance(EStage::MenuNewGame);return true;
        }
        if(Stage==EStage::MenuNewGame)
        {
            if(Elapsed<.35)return true;
            const FString SlotFile=FPaths::Combine(SaveDirectory,Prefix+TEXT("1.sav"));
            Check(TEXT("Isolated slot is fresh and will not overwrite an existing save"),!IFileManager::Get().FileExists(*SlotFile));
            if(IFileManager::Get().FileExists(*SlotFile))return Finish(TEXT("Choose a fresh isolated save prefix/directory"));
            Check(TEXT("Menu NewGame creates isolated slot0"),Instance->NewGame(0,TEXT("Gameplay UX isolated QA")),Instance->LastSaveError.ToString());
            Advance(EStage::WaitGame);return true;
        }
        auto* Loading=Instance->GetSubsystem<UDWLoadingTransitionSubsystem>();
        if(Instance->IsPlayerRestorePending()||(Loading&&Loading->IsTransitionActive()))return true;
        auto* PC=Cast<ADWPlayerController>(Current->GetFirstPlayerController());auto* Player=PC?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
        auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;auto* Widget=HUD?HUD->GetGameplayWidget():nullptr;
        auto* Settings=UDWUserSettings::Resolve(Current);
        if(!Player||!PC||!HUD||!Widget||!Settings||!Arm(Player))return true;
        if(ProtectedPlayer.Get()!=Player)
        {
            ProtectedPlayer=Player;bOldDamage=Player->CanBeDamaged();Player->SetCanBeDamaged(false);
            OriginalMin=Player->MinCameraDistance;OriginalMax=Player->MaxCameraDistance;OriginalStep=Player->CameraZoomStep;
            OriginalSpeed=Player->CameraZoomInterpSpeed;OriginalDistance=Player->CameraDistance;OriginalSensitivity=Settings->GetCameraSensitivityMultiplier();
        }
        auto Checker=[this](const FString& Name,bool Passed,const FString& Detail){Check(Name,Passed,Detail);};
        switch(Stage)
        {
            case EStage::Menu:case EStage::FrontendSettingsReady:case EStage::FrontendShotWait:case EStage::MenuNewGame:break;
            case EStage::WaitGame:
                if(Current->GetPackage()->GetName()!=Instance->GetConfig()->GameplayMap||!PC->IsCameraInputAllowed()||Elapsed<.8)return true;
                Check(TEXT("Gameplay loads the configured existing map with an active isolated slot"),Instance->GetActiveSlot()==0,Current->GetPackage()->GetName());
                Advance(EStage::Defaults);break;
            case EStage::Defaults:
            {
                const auto* Hero=Player->GetClass()->GetDefaultObject<ADWPlayerCharacter>();const auto* Controller=PC->GetClass()->GetDefaultObject<ADWPlayerController>();
                Check(TEXT("Authored Controller and applied camera key use MMB"),Controller->KeyCameraDrag==EKeys::MiddleMouseButton&&PC->GetAppliedActionKey(EDWInputAction::CameraDrag)==EKeys::MiddleMouseButton,PC->GetClass()->GetPathName());
                Check(TEXT("Authored Hero sensitivity is the new .4 base"),FMath::IsNearlyEqual(Hero->CameraDragSensitivity,.4f,.001f)&&FMath::IsNearlyEqual(Player->CameraDragSensitivity,.4f,.001f),Player->GetClass()->GetPathName());
                Check(TEXT("Authored zoom bounds/step/smoothing and initial distance are valid"),Hero->MinCameraDistance>=100.f&&Hero->MaxCameraDistance>Hero->MinCameraDistance&&Hero->CameraZoomStep>0.f&&Hero->CameraZoomInterpSpeed>0.f&&Hero->CameraDistance>=Hero->MinCameraDistance&&Hero->CameraDistance<=Hero->MaxCameraDistance);
                float Expected;if(FParse::Value(FCommandLine::Get(),TEXT("DWGameplayUXExpectedSensitivity="),Expected))Check(TEXT("Sensitivity preference loaded after a separate process restart"),FMath::IsNearlyEqual(Settings->GetCameraSensitivityMultiplier(),Expected,.001f));
                VerifyMinimapCamera(Player,TEXT("Initial"));Shot(TEXT("MinimapBeforeTurn"));
                Sample(TEXT("Authored defaults"),Player);VerifyBindings(PC);Advance(EStage::Bindings);break;
            }
            case EStage::Bindings:
                if(Elapsed<.2)return true;Check(TEXT("Queued bindings apply outside input delegate iteration"),PC->GetAppliedActionKey(EDWInputAction::CameraDrag)==EKeys::MiddleMouseButton&&!PC->bInputBindingsPending);
                Settings->SetCameraSensitivityMultiplier(1.f,true);Remember(Player);Key(PC,EKeys::MiddleMouseButton,IE_Pressed);Mouse(PC,100.f);Advance(EStage::MiddleDrag);break;
            case EStage::MiddleDrag:
            {
                if(Elapsed<.15)return true;const float Delta=FMath::FindDeltaAngleDegrees(BeforeYaw,Arm(Player)->GetComponentRotation().Yaw);
                Check(TEXT("Real PC input pipeline rotates MMB drag by raw mouse amount, without .07 scaling"),FMath::IsNearlyEqual(Delta,100.f*Player->CameraDragSensitivity,.2f),FString::SanitizeFloat(Delta));
                VerifyMinimapCamera(Player,TEXT("MMBTurn"));Shot(TEXT("MinimapAfterTurn"));
                ReleaseMouse(PC);Remember(Player);Key(PC,EKeys::RightMouseButton,IE_Pressed);Mouse(PC,100.f);Advance(EStage::RightDrag);break;
            }
            case EStage::RightDrag:
                if(Elapsed<.15)return true;Check(TEXT("Old right mouse button no longer drags the camera"),SameCamera(Player));ReleaseMouse(PC);Remember(Player);Mouse(PC,100.f);Advance(EStage::NoDrag);break;
            case EStage::NoDrag:
                if(Elapsed<.15)return true;Check(TEXT("Mouse motion without held camera button does not rotate"),SameCamera(Player));Settings->SetCameraSensitivityMultiplier(.5f,true);Remember(Player);Key(PC,EKeys::MiddleMouseButton,IE_Pressed);Mouse(PC,100.f);Advance(EStage::LowSensitivity);break;
            case EStage::LowSensitivity:
                if(Elapsed<.15)return true;Check(TEXT("Live sensitivity .5 multiplies the same raw input"),FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(BeforeYaw,Arm(Player)->GetComponentRotation().Yaw),100.f*Player->CameraDragSensitivity*.5f,.2f));
                Settings->SetCameraSensitivityMultiplier(2.f,true);Remember(Player);Mouse(PC,100.f);Advance(EStage::HighSensitivity);break;
            case EStage::HighSensitivity:
                if(Elapsed<.15)return true;Check(TEXT("Live sensitivity 2 multiplies the same raw input"),FMath::IsNearlyEqual(FMath::FindDeltaAngleDegrees(BeforeYaw,Arm(Player)->GetComponentRotation().Yaw),100.f*Player->CameraDragSensitivity*2.f,.2f));ReleaseMouse(PC);
                VerifyMinimapCamera(Player,TEXT("SecondTurn"));
                if(auto* Map=Player->FindComponentByClass<UDWMinimapComponent>()){Map->bFollowCameraYaw=false;Map->RefreshMinimap();}
                Settings->SetCameraSensitivityMultiplier(-1.f,false);Check(TEXT("Sensitivity lower range is enforced"),FMath::IsNearlyEqual(Settings->GetCameraSensitivityMultiplier(),.25f));
                Settings->SetCameraSensitivityMultiplier(8.f,false);Check(TEXT("Sensitivity upper range is enforced"),FMath::IsNearlyEqual(Settings->GetCameraSensitivityMultiplier(),3.f));
                Settings->SetCameraSensitivityMultiplier(1.f,true);Player->CameraDistance=OriginalDistance;Player->PrepareCameraForReveal();Player->CameraZoomInterpSpeed=0.f;BeforeTarget=Player->CameraDistance;Wheel(PC,1.f);Advance(EStage::WheelOne);break;
            case EStage::WheelOne:
                if(Elapsed<.15)return true;Check(TEXT("One real wheel unit moves closer by the configured cm step and then stops"),FMath::IsNearlyEqual(Player->CameraDistance,BeforeTarget-Player->CameraZoomStep,.1f)&&FMath::IsNearlyEqual(Player->GetCurrentCameraDistance(),Player->CameraDistance,.1f));
                if(auto* Map=Player->FindComponentByClass<UDWMinimapComponent>())
                {
                    Check(TEXT("Editor follow toggle off restores fixed world north capture"),FMath::Abs(FMath::FindDeltaAngleDegrees(Map->GetCaptureYaw(),Map->NorthYaw))<.1f);
                    Map->bFollowCameraYaw=true;Map->RefreshMinimap();
                }
                Wheel(PC,100.f);Advance(EStage::WheelNear);break;
            case EStage::WheelNear:
                if(Elapsed<.2)return true;Check(TEXT("Wheel zoom clamps at the Blueprint nearest distance"),FMath::IsNearlyEqual(Player->CameraDistance,OriginalMin,.1f)&&FMath::IsNearlyEqual(Player->GetCurrentCameraDistance(),OriginalMin,.1f));Sample(TEXT("Nearest zoom"),Player);Shot(TEXT("NearestZoom"));Advance(EStage::NearShotWait);break;
            case EStage::NearShotWait:
                if(Elapsed<.35)return true;VerifyMinimapCamera(Player,TEXT("FollowReenabled"));Wheel(PC,-100.f);Advance(EStage::WheelFar);break;
            case EStage::WheelFar:
                if(Elapsed<.2)return true;Check(TEXT("Wheel zoom clamps at the Blueprint farthest distance"),FMath::IsNearlyEqual(Player->CameraDistance,OriginalMax,.1f)&&FMath::IsNearlyEqual(Player->GetCurrentCameraDistance(),OriginalMax,.1f));Sample(TEXT("Farthest zoom"),Player);Shot(TEXT("FarthestZoom"));Advance(EStage::FarShotWait);break;
            case EStage::FarShotWait:
                if(Elapsed<.35)return true;Player->CameraDistance=OriginalDistance;Player->PrepareCameraForReveal();Player->CameraZoomInterpSpeed=2.f;
                BeforeLength=Player->GetCurrentCameraDistance();BeforeTarget=Player->CameraDistance-Player->CameraZoomStep;bSawSmooth=false;Wheel(PC,1.f);Advance(EStage::SmoothObserve);break;
            case EStage::SmoothObserve:
                if(!FMath::IsNearlyEqual(Player->CameraDistance,BeforeTarget,.1f))return true;
                bSawSmooth=Player->GetCurrentCameraDistance()>BeforeTarget+.01f&&Player->GetCurrentCameraDistance()<BeforeLength-.01f;
                if(!bSawSmooth&&Elapsed<.5)return true;
                SmoothReference=Player->GetCurrentCameraDistance();SmoothTime=Current->GetTimeSeconds();Advance(EStage::SmoothCompare);break;
            case EStage::SmoothCompare:
            {
                if(Elapsed<.4)return true;const float Seconds=Current->GetTimeSeconds()-SmoothTime;
                const float Expected=BeforeTarget+(SmoothReference-BeforeTarget)*FMath::Exp(-2.f*Seconds);
                Check(TEXT("Positive interpolation visibly approaches rather than instantly snapping"),bSawSmooth);
                Check(TEXT("Actual smooth zoom follows elapsed simulation time independent of frame subdivisions"),FMath::IsNearlyEqual(Player->GetCurrentCameraDistance(),Expected,1.f),FString::Printf(TEXT("actual %.3f expected %.3f seconds %.4f"),Player->GetCurrentCameraDistance(),Expected,Seconds));
                Player->CameraZoomInterpSpeed=OriginalSpeed;Player->CameraDistance=OriginalDistance;Player->PrepareCameraForReveal();HUD->ShowMenu(EDWMenuPage::Settings);Widget->SelectSettingsBookTab(3);Advance(EStage::SettingsOpen);break;
            }
            case EStage::SettingsOpen:
                if(Elapsed<.7)return true;Remember(Player);Key(PC,EKeys::MiddleMouseButton,IE_Pressed);Mouse(PC,100.f);Wheel(PC,1.f);Player->DragCamera(100.f,100.f);Player->ZoomCamera(1.f);Advance(EStage::SettingsCheck);break;
            case EStage::SettingsCheck:
            {
                if(Elapsed<.2)return true;Check(TEXT("Settings/menu blocks both input pipeline and direct camera requests"),!PC->IsCameraInputAllowed()&&SameCamera(Player));ReleaseMouse(PC);
                auto* Panel=Cast<UDWSettingsPanel>(Widget->GetWidgetFromName(TEXT("ExtendedSettings")));
                auto* Slider=Panel?Cast<USlider>(Panel->GetWidgetFromName(TEXT("CameraSensitivitySlider"))):nullptr;
                Check(TEXT("Actual editable settings panel contains the sensitivity slider"),Slider!=nullptr);
                if(Slider){const float Value=(1.75f-.25f)/(3.f-.25f);Slider->SetValue(Value);Slider->OnValueChanged.Broadcast(Value);Check(TEXT("Actual settings slider delegate updates sensitivity"),FMath::IsNearlyEqual(Settings->GetCameraSensitivityMultiplier(),1.75f,.001f));}
                Settings->SetMasterVolume(.42f,true);Settings->SetCategoryVolume(EDWSoundCategory::Music,.31f,true);Settings->SetCategoryVolume(EDWSoundCategory::Voice,.57f,true);Settings->SetCategoryVolume(EDWSoundCategory::SFX,.79f,true);Settings->SetCameraSensitivityMultiplier(1.75f,true);
                if(Panel)Panel->RefreshPanel();
                // Capture all five book pages later, with the authored book-tab routing and settled layout.
                Advance(EStage::SettingsShotWait);break;
            }
            case EStage::SettingsShotWait:
                if(Elapsed<.35)return true;HUD->ClosePanels();Advance(EStage::Paused);break;
            case EStage::Paused:
                if(Elapsed<.3)return true;Remember(Player);Check(TEXT("Explicit pause is accepted"),PC->SetPause(true));Key(PC,EKeys::MiddleMouseButton,IE_Pressed);Mouse(PC,100.f);Wheel(PC,-1.f);Player->DragCamera(100.f,100.f);Player->ZoomCamera(-1.f);Advance(EStage::PauseCheck);break;
            case EStage::PauseCheck:
                if(Elapsed<.2)return true;Check(TEXT("Pause blocks drag and wheel without queued camera drift"),!PC->IsCameraInputAllowed()&&SameCamera(Player));ReleaseMouse(PC);PC->SetPause(false);Advance(EStage::Cinematic);break;
            case EStage::Cinematic:
            {
                if(Elapsed<.3)return true;Check(TEXT("Camera controls resume after pause"),PC->IsCameraInputAllowed());
                UDWCinematicComponent* Well=nullptr;for(TActorIterator<AActor> It(Current);It;++It)if(auto* Candidate=It->FindComponentByClass<UDWCinematicComponent>())if(Candidate->bViewOnly){Well=Candidate;break;}
                if(!Well||!Well->PlayCinematic(PC)){Skip(TEXT("No playable existing well cinematic; no new trigger/data asset was created"));Advance(EStage::AudioBegin);break;}
                Cinematic=Well;Remember(Player);Key(PC,EKeys::MiddleMouseButton,IE_Pressed);Mouse(PC,100.f);Wheel(PC,1.f);Player->DragCamera(100.f,100.f);Player->ZoomCamera(1.f);Advance(EStage::CinematicCheck);break;
            }
            case EStage::CinematicCheck:
                if(Elapsed<.3)return true;Check(TEXT("Existing cinematic blocks drag/zoom while another view owns the camera"),Cinematic.IsValid()&&Cinematic->IsPlaying()&&!PC->IsCameraInputAllowed()&&SameCamera(Player));ReleaseMouse(PC);if(Cinematic.IsValid())Cinematic->CancelCinematic();Advance(EStage::CinematicReturn);break;
            case EStage::CinematicReturn:
                if(Elapsed<.5)return true;Check(TEXT("Cinematic cancel restores the player's camera control"),PC->GetViewTarget()==Player&&PC->IsCameraInputAllowed());Advance(EStage::AudioBegin);break;
            case EStage::AudioBegin:
#if DW_GAMEPLAY_UX_AUDIO_CHECKS
                AudioCheck=DWBeginAudioUXRuntimeChecks(Settings,Current);Check(TEXT("Audio verification begins on the isolated current-world device"),AudioCheck.IsValid());Advance(EStage::AudioWait);break;
#else
                Skip(TEXT("Audio runtime helper was not compiled"));Advance(EStage::AudioWait);break;
#endif
            case EStage::AudioWait:
#if DW_GAMEPLAY_UX_AUDIO_CHECKS
                if(AudioCheck.IsValid()&&!AudioCheck->IsReady())return true;
                DWFinishAudioUXRuntimeChecks(AudioCheck,Settings,Current,Checker);DWCheckAudioUXSettingsPersistence(Settings,Checker);
#endif
                HUD->ShowMenu(EDWMenuPage::Settings);
                Widget->SelectSettingsBookTab(2);StableBookFrames=0;
                HUD->Notify(FText::GetEmpty());Advance(EStage::BookNavigationReady);break;
            case EStage::BookNavigationReady:
            {
                const bool bStable=BookFrameStable(Widget);
                if(Elapsed<FMath::Max(.7f,Widget->PageEnterSeconds+.1f))return true;
                if((!CurrentPageSettled(Widget)||!bStable)&&Elapsed<3.0)return true;
                auto* Panel=Cast<UDWSettingsPanel>(Widget->GetWidgetFromName(TEXT("ExtendedSettings")));
                FGeometry AudioButton;auto* Button=Widget->GetWidgetFromName(TEXT("SettingsBookAudioTabButton"));
                const bool bReady=Panel&&Widget->GetSettingsBookTab()==2&&Arranged(Button,AudioButton);
                Check(TEXT("settings.book.capture_navigation_has_actual_keys_page_and_audio_tab"),bReady);
                if(!bReady)return Finish(TEXT("Book navigation hit-route test is missing its actual page or button"));
                BookBindingsBefore=PC->GetConfiguredKeys();Panel->RequestBinding(EDWInputAction::CameraDrag);
                Check(TEXT("settings.book.camera_binding_capture_started"),Panel->IsCapturing());
                BookAudioPosition=AudioButton.LocalToAbsolute(AudioButton.GetLocalSize()*.5f);
                Advance(EStage::BookNavigationHover);break;
            }
            case EStage::BookNavigationHover:
                SlateMove(BookAudioPosition);Advance(EStage::BookNavigationDown);break;
            case EStage::BookNavigationDown:
            {
                auto* Button=Cast<UButton>(Widget->GetWidgetFromName(TEXT("SettingsBookAudioTabButton")));
                Check(TEXT("settings.book.audio_tab_has_global_hit_path_while_capturing"),HitContains(BookAudioPosition,Button));
                SlateDown(BookAudioPosition);
                auto* Panel=Cast<UDWSettingsPanel>(Widget->GetWidgetFromName(TEXT("ExtendedSettings")));
                Check(TEXT("settings.book.navigation_down_cancels_capture_and_reaches_button"),Panel&&!Panel->IsCapturing()&&Button&&Button->IsPressed());
                Advance(EStage::BookNavigationUp);break;
            }
            case EStage::BookNavigationUp:
            {
                Check(TEXT("settings.book.navigation_press_and_release_are_separate_frames"),GFrameCounter>LastSlateDownFrame);
                auto* AudioButton=Cast<UButton>(Widget->GetWidgetFromName(TEXT("SettingsBookAudioTabButton")));
                ReenterHeldButton(AudioButton,BookAudioPosition);
                Check(TEXT("settings.book.release_has_hover_and_capture"),AudioButton&&AudioButton->IsHovered()&&AudioButton->GetCachedWidget().IsValid()&&AudioButton->GetCachedWidget()->HasMouseCapture(),ButtonState(AudioButton));
                SlateUp(BookAudioPosition);auto* Panel=Cast<UDWSettingsPanel>(Widget->GetWidgetFromName(TEXT("ExtendedSettings")));
                const auto Keys=PC->GetConfiguredKeys();
                Check(TEXT("settings.book.global_audio_click_cancels_capture_without_rebinding"),Panel&&!Panel->IsCapturing()&&Widget->GetSettingsBookTab()==1
                    &&Keys==BookBindingsBefore&&Keys.IsValidIndex(int32(EDWInputAction::CameraDrag))&&Keys[int32(EDWInputAction::CameraDrag)]==EKeys::MiddleMouseButton);
                BookTab=0;StableBookFrames=0;Widget->SelectSettingsBookTab(0);HUD->Notify(FText::GetEmpty());Advance(EStage::BookPageReady);break;
            }
            case EStage::BookPageReady:
            {
                const bool bStable=BookFrameStable(Widget);
                const double Minimum=BookTab==0?FMath::Max(.7f,Widget->PageEnterSeconds+.1f):.35;
                if(Elapsed<Minimum)return true;
                if((!CurrentPageSettled(Widget)||!bStable)&&Elapsed<3.0)return true;
                Check(FString::Printf(TEXT("settings.book.page%d.stable_before_screenshot"),BookTab),CurrentPageSettled(Widget)&&bStable);
                VerifyBookPage(Widget);
                static const TCHAR* Names[]={TEXT("SettingsGraphics"),TEXT("SettingsMasterAndCategoryAudio"),TEXT("SettingsKeys"),TEXT("SettingsSensitivityControls"),TEXT("SettingsGeneral")};
                Shot(Names[BookTab]);Advance(EStage::BookShotWait);break;
            }
            case EStage::BookShotWait:
                // Screenshot consumption and page switching occur in separate rendered frames.
                if(Elapsed<.35)return true;
                if(++BookTab<5){StableBookFrames=0;Widget->SelectSettingsBookTab(BookTab);Advance(EStage::BookPageReady);}
                else Advance(EStage::InventoryOpen);break;
            case EStage::InventoryOpen:
                if(Elapsed<.35)return true;
                HUD->ShowMenu(EDWMenuPage::Inventory);Advance(EStage::InventoryCheck);break;
            case EStage::InventoryCheck:
                if(Elapsed<.6)return true;
#if DW_GAMEPLAY_UX_INVENTORY_CHECKS
                DWRunInventoryUXRuntimeChecks(Player,Widget,Checker);
#else
                Skip(TEXT("Inventory runtime helper was not compiled"));
#endif
                HUD->Notify(FText::GetEmpty());Advance(EStage::InventoryPageReady);break;
            case EStage::InventoryPageReady:
                if(Elapsed<.35)return true;Shot(TEXT("InventoryControls"));Advance(EStage::InventoryShotWait);break;
            case EStage::InventoryShotWait:
                if(Elapsed<.35)return true;
                Widget->CancelInventoryDiscard();
                Player->GetInventory()->SetSlots({FDWItemStack(TEXT("Water"),16),FDWItemStack(TEXT("Flour"),3),FDWItemStack(TEXT("Water"),4)});
                Widget->RefreshFromHUD(true);HUD->Notify(FText::GetEmpty());GlobalCycle=0;Advance(EStage::GlobalInventoryReady);break;
            case EStage::GlobalInventoryReady:
            {
                if(Elapsed<.35)return true;
                const bool bReady=CurrentPageSettled(Widget)&&PrepareGlobalDrag(Widget);
                if(!bReady&&Elapsed<3.0)return true;
                Check(FString::Printf(TEXT("inventory.global.cycle%d.slot_and_edge_hit_paths"),GlobalCycle),bReady);
                if(!bReady)return Finish(TEXT("Actual rendered slot or backdrop has no valid global hit-test path"));
                Check(FString::Printf(TEXT("inventory.global.cycle%d.no_preexisting_mouse_capture"),GlobalCycle),!FSlateApplication::Get().HasUserMouseCapture(FSlateApplication::Get().GetUserIndexForMouse()));
                const auto* Bag=Player->GetInventory();GlobalWaterBefore=Bag->CountItem(TEXT("Water"));GlobalRevisionBefore=Bag->GetInventoryRevision();
                GlobalStackQuantity=Bag->GetSlots().Num()>0?Bag->GetSlots()[0].Quantity:0;
                Check(FString::Printf(TEXT("inventory.global.cycle%d.known_isolated_source_water16"),GlobalCycle),Bag->GetSlots().Num()>0&&Bag->GetSlots()[0].ItemId==FName(TEXT("Water"))&&GlobalStackQuantity==16&&GlobalWaterBefore==20);
                Advance(EStage::GlobalSlotHover);break;
            }
            case EStage::GlobalSlotHover:
                SlateMove(SlotPosition);Advance(EStage::GlobalSlotDown);break;
            case EStage::GlobalSlotDown:
                // Each event occupies its own ticker/frame. Down's return value is not a success assertion.
                SlateDown(SlotPosition,Widget->InventoryControls.DragButton);Advance(EStage::GlobalDragMove);break;
            case EStage::GlobalDragMove:
                if(const auto* Selected=FindFProperty<FIntProperty>(Widget->GetClass(),TEXT("SelectedSlot")))
                    Check(FString::Printf(TEXT("inventory.global.cycle%d.routed_slot_down_selects_source"),GlobalCycle),Selected->GetPropertyValue_InContainer(Widget)==0);
                else Check(TEXT("inventory.global.actual_selected_slot_property_exists"),false);
                Check(FString::Printf(TEXT("inventory.global.cycle%d.down_and_move_are_separate_frames"),GlobalCycle),GFrameCounter>LastSlateDownFrame);
                SlateMove(DropPosition);
                Check(FString::Printf(TEXT("inventory.global.cycle%d.slate_detects_live_drag"),GlobalCycle),FSlateApplication::Get().IsDragDropping());
                Advance(EStage::GlobalDrop);break;
            case EStage::GlobalDrop:
                Check(FString::Printf(TEXT("inventory.global.cycle%d.move_and_release_are_separate_frames"),GlobalCycle),GFrameCounter>LastSlateMoveFrame);
                SlateUp(DropPosition);
                Check(FString::Printf(TEXT("inventory.global.cycle%d.release_opens_popup_without_discard"),GlobalCycle),!FSlateApplication::Get().IsDragDropping()&&Widget->IsInventoryDiscardPending()
                    &&Player->GetInventory()->CountItem(TEXT("Water"))==GlobalWaterBefore&&Player->GetInventory()->GetInventoryRevision()==GlobalRevisionBefore);
                HUD->Notify(FText::GetEmpty());Advance(EStage::GlobalPopupReady);break;
            case EStage::GlobalPopupReady:
            {
                if(Elapsed<.35)return true;
                FGeometry Popup;const bool bVisible=Widget->IsInventoryDiscardPending()&&Arranged(Widget->GetWidgetFromName(TEXT("InventoryDiscardModal")),Popup);
                if(!bVisible&&Elapsed<3.0)return true;
                Check(FString::Printf(TEXT("inventory.global.cycle%d.actual_popup_visible"),GlobalCycle),bVisible);
                if(!bVisible)return Finish(TEXT("Routed inventory drag did not produce the actual visible popup"));
                if(!VerifyPopupGeometry(Widget))return Finish(TEXT("The visible popup has no arranged confirmation/cancel button"));
                Shot(GlobalCycle==0?TEXT("InventoryDiscardNearCursorCancel"):TEXT("InventoryDiscardEdgeConfirm"));
                Advance(EStage::GlobalPopupShotWait);break;
            }
            case EStage::GlobalPopupShotWait:
                if(Elapsed<.35)return true;Advance(EStage::GlobalButtonHover);break;
            case EStage::GlobalButtonHover:
                SlateMove(PopupButtonPosition);Advance(EStage::GlobalButtonDown);break;
            case EStage::GlobalButtonDown:
            {
                auto* Button=Cast<UButton>(Widget->GetWidgetFromName(GlobalCycle==0?TEXT("InventoryDiscardCancelButton"):TEXT("InventoryDiscardConfirmButton")));
                Check(FString::Printf(TEXT("inventory.global.cycle%d.actual_button_hit_path"),GlobalCycle),HitContains(PopupButtonPosition,Button));
                SlateDown(PopupButtonPosition);
                Check(FString::Printf(TEXT("inventory.global.cycle%d.button_down_reaches_button_without_preview_cancel"),GlobalCycle),Widget->IsInventoryDiscardPending()&&Button&&Button->IsPressed(),
                    FString::Printf(TEXT("Pending=%d IsPressed=%d"),Widget->IsInventoryDiscardPending(),Button?Button->IsPressed():false));
                Advance(EStage::GlobalButtonUp);break;
            }
            case EStage::GlobalButtonUp:
            {
                Check(FString::Printf(TEXT("inventory.global.cycle%d.button_press_and_release_are_separate_frames"),GlobalCycle),GFrameCounter>LastSlateDownFrame);
                auto* Button=Cast<UButton>(Widget->GetWidgetFromName(GlobalCycle==0?TEXT("InventoryDiscardCancelButton"):TEXT("InventoryDiscardConfirmButton")));
                Check(FString::Printf(TEXT("inventory.global.cycle%d.pending_and_button_press_survive_until_release"),GlobalCycle),Widget->IsInventoryDiscardPending()&&Button&&Button->IsPressed());
                ReenterHeldButton(Button,PopupButtonPosition);
                Check(FString::Printf(TEXT("inventory.global.cycle%d.release_has_hover_and_capture"),GlobalCycle),Button&&Button->IsHovered()&&Button->GetCachedWidget().IsValid()&&Button->GetCachedWidget()->HasMouseCapture(),ButtonState(Button));
                SlateUp(PopupButtonPosition);const auto* Bag=Player->GetInventory();
                const bool bExpected=GlobalCycle==0?Bag->CountItem(TEXT("Water"))==GlobalWaterBefore&&Bag->GetInventoryRevision()==GlobalRevisionBefore
                    :Bag->CountItem(TEXT("Water"))==GlobalWaterBefore-GlobalStackQuantity&&Bag->GetInventoryRevision()>GlobalRevisionBefore;
                Check(GlobalCycle==0?TEXT("inventory.global.routed_no_preserves_exact_inventory"):TEXT("inventory.global.routed_yes_discards_exact_dragged_stack"),
                    !Widget->IsInventoryDiscardPending()&&bExpected&&Bag->CountItem(TEXT("Flour"))==3,FString::Printf(TEXT("Water=%d Expected=%d Revision=%lld"),Bag->CountItem(TEXT("Water")),GlobalCycle==0?GlobalWaterBefore:GlobalWaterBefore-GlobalStackQuantity,Bag->GetInventoryRevision()));
                Advance(EStage::GlobalCycleWait);break;
            }
            case EStage::GlobalCycleWait:
                if(Elapsed<.35)return true;
                if(++GlobalCycle<2){HUD->Notify(FText::GetEmpty());Advance(EStage::GlobalInventoryReady);}
                else Advance(EStage::FinalWait);break;
            case EStage::FinalWait:
            {
                if(Elapsed<.8)return true;bool Ready=true;for(const auto& File:Shots)Ready&=IFileManager::Get().FileSize(*File)>512;
                if(!Ready&&Elapsed<3.0)return true;Check(TEXT("Screenshot requests produced nonempty PNG evidence before exit"),Ready);
                Widget->CancelInventoryDiscard();Settings->SetCameraSensitivityMultiplier(1.75f,true);
                Check(TEXT("Only isolated slot0 remains active"),Instance->GetActiveSlot()==0&&IsolationValid());return Finish();
            }
        }
        return true;
    }
};
static TSharedPtr<FRun> ActiveRun;
static bool bStartupDecided=false;
static void Start()
{
    if(bStartupDecided||GIsEditor||!FParse::Param(FCommandLine::Get(),TEXT("DWGameplayUXQA")))return;
    UWorld* Current=nullptr;if(GEngine)for(const FWorldContext& Context:GEngine->GetWorldContexts())
        if(Context.WorldType==EWorldType::Game&&Context.World()){Current=Context.World();break;}
    auto* Instance=Current?Current->GetGameInstance<UDWGameInstance>():nullptr;if(!Current||!Instance)return;
    bStartupDecided=true;FString Report,Prefix,Directory,Ini;
    const bool Flags=FParse::Value(FCommandLine::Get(),TEXT("DWGameplayUXQAReport="),Report)
        &&FParse::Value(FCommandLine::Get(),TEXT("DWVerificationSavePrefix="),Prefix)
        &&FParse::Value(FCommandLine::Get(),TEXT("DWSaveTestDirectory="),Directory)
        &&FParse::Value(FCommandLine::Get(),TEXT("GameUserSettingsINI="),Ini);
    const bool Guard=Flags&&!FPaths::IsRelative(Report)&&!FPaths::IsRelative(Directory)&&!FPaths::IsRelative(Ini)
        &&Prefix.StartsWith(TEXT("DW_QA_"))&&Prefix.Len()<80&&!Prefix.Contains(TEXT("/"))&&!Prefix.Contains(TEXT("\\"))
        &&FPaths::GetExtension(Report).Equals(TEXT("json"),ESearchCase::IgnoreCase)
        &&FPaths::GetExtension(Ini).Equals(TEXT("ini"),ESearchCase::IgnoreCase)&&FPaths::GetCleanFilename(Ini).StartsWith(TEXT("DW_QA_"))
        &&OutsideProject(Report)&&OutsideProject(Directory)&&OutsideProject(Ini)
        &&OutsideFormalDocuments(Report)&&OutsideFormalDocuments(Directory)&&OutsideFormalDocuments(Ini)
        &&SamePath(Instance->GetSaveDirectory(),Directory)&&SamePath(ActiveSettingsPath(),Ini)
        &&!IFileManager::Get().FileExists(*Report);
    if(!Guard)
    {
        UE_LOG(LogTemp,Error,TEXT("DW_GAMEPLAY_UX_QA paths: Flags=%d Report=[%s] Prefix=[%s] Save=[%s] ActualSave=[%s] Ini=[%s] ActiveIni=[%s] Project=[%s] Saved=[%s] Fresh=%d Outside=%d/%d/%d Same=%d/%d"),Flags,*Report,*Prefix,*Directory,*Instance->GetSaveDirectory(),*Ini,*ActiveSettingsPath(),*FPaths::ProjectDir(),*FPaths::ProjectSavedDir(),!IFileManager::Get().FileExists(*Report),OutsideProject(Report),OutsideProject(Directory),OutsideProject(Ini),SamePath(Instance->GetSaveDirectory(),Directory),SamePath(ActiveSettingsPath(),Ini));
        UE_LOG(LogTemp,Error,TEXT("DW_GAMEPLAY_UX_QA refused: require absolute fresh external JSON, isolated DW_QA_ prefix/save directory, external DW_QA_*.ini matching GameUserSettingsINI and actual GameInstance storage."));
        if(GEngine)GEngine->Exec(Current,TEXT("QUIT"));return;
    }
    auto Run=MakeShared<FRun>();Run->GI=Instance;Run->Report=FullPath(Report);Run->Prefix=Prefix;
    // ProcessReply deliberately refuses pointer capture in inactive packaged applications.
    // This process is an explicitly requested, path-guarded QA session; no OS activation is used.
    if(FSlateApplication::IsInitialized())
    {
        Run->bOldBackgroundInput=FSlateApplication::Get().GetHandleDeviceInputWhenApplicationNotActive();
        Run->bManagedBackgroundInput=true;
        FSlateApplication::Get().SetHandleDeviceInputWhenApplicationNotActive(true);
    }
    Run->SaveDirectory=FullPath(Directory);Run->UserIni=FullPath(Ini);Run->ShotDirectory=FPaths::Combine(FPaths::GetPath(Run->Report),TEXT("Screenshots"));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Run->Report),true);IFileManager::Get().MakeDirectory(*Run->ShotDirectory,true);
    ActiveRun=Run;FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float Dt){return Run->Tick(Dt);}));
}
static FDelayedAutoRegisterHelper Startup(EDelayedRegisterRunPhase::EndOfEngineInit,[]
{
    if(GIsEditor||!FParse::Param(FCommandLine::Get(),TEXT("DWGameplayUXQA")))return;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){Start();return !bStartupDecided;}));
});
}
#endif
