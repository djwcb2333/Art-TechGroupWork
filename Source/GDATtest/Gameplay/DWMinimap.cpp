#include "DWMinimap.h"
#include "DWMinimapWidget.h"
#include "Camera/CameraComponent.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayHUD.h"
#include "DWGameplayWidget.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWWorldEvent.h"
#include "DWUIOffscreenComponent.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "DWResourceNode.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
static FString DWMinimapShaderCode()
{
    return TEXT(R"(
// Reconstruct each texel before averaging colors. Interpolating raw depth invents surfaces at silhouettes.
struct FClaySampling
{
    float3 Shade(Texture2D Tex,float2 SampleUV,int2 Size,float4 Info,float3 Right,float3 Up,float3 Forward,
        float3 Ground,float3 Surface,float3 Wall)
    {
        if(any(SampleUV<0)||any(SampleUV>1))return Ground;
        int2 P=clamp(int2(floor(SampleUV*Size)),int2(0,0),Size-1);
        float D=Tex.Load(int3(P,0)).r;
        if(D<=1||D>=10000000)return Ground;
        float L=Tex.Load(int3(max(P-int2(1,0),int2(0,0)),0)).r;
        float R=Tex.Load(int3(min(P+int2(1,0),Size-1),0)).r;
        float T=Tex.Load(int3(max(P-int2(0,1),int2(0,0)),0)).r;
        float B=Tex.Load(int3(min(P+int2(0,1),Size-1),0)).r;
        bool LV=L>1&&L<10000000, RV=R>1&&R<10000000;
        bool TV=T>1&&T<10000000, BV=B>1&&B<10000000;
        float DX=(!LV&&RV)?R-D:(!RV&&LV)?D-L:(abs(R-D)<=abs(D-L)?R-D:D-L);
        float DY=(!TV&&BV)?B-D:(!BV&&TV)?D-T:(abs(B-D)<=abs(D-T)?B-D:D-T);
        if(!LV&&!RV)DX=0; if(!TV&&!BV)DY=0;
        float3 Cross=cross(float3(Info.x/Size.x,0,DX),float3(0,-Info.x/Size.y,DY));
        float3 NC=normalize(Cross+float3(0,0,-1e-8));
        float3 N=normalize(NC.x*Right+NC.y*Up+NC.z*Forward);
        float2 Q=(float2(P)+.5)/Size;
        float3 Pos=float3((Q.x-.5)*Info.x,(.5-Q.y)*Info.x,D);
        float WorldZ=Info.z+Pos.x*Right.z+Pos.y*Up.z+D*Forward.z;
        float Height=saturate((WorldZ-Info.w)/400);
        float3 Top=lerp(Ground,Surface,Height);
        float3 Base=lerp(Wall,Top,saturate(N.z));
        float Light=.45+.55*saturate(dot(N,normalize(float3(-.35,-.45,.82))));
        return Base*Light;
    }
};
FClaySampling Clay;
uint Width,Height; DepthTex.GetDimensions(Width,Height);
int2 Size=int2(Width,Height);
float2 q=float2(UV.x+PlayerOffset.x,.5+(UV.y-.5+PlayerOffset.y)*CaptureInfo.y);
float2 FootprintX=ddx(q),FootprintY=ddy(q);
int Grid=clamp(int(EdgeSamples+.5),1,3);
float3 rgb=float3(0,0,0);
[unroll]for(int Y=0;Y<3;++Y)
[unroll]for(int X=0;X<3;++X)
    if(X<Grid&&Y<Grid)
    {
        float2 Offset=(float2(X,Y)+.5)/Grid-.5;
        rgb+=Clay.Shade(DepthTex,q+Offset.x*FootprintX+Offset.y*FootprintY,Size,CaptureInfo,
            CaptureRight.xyz,CaptureUp.xyz,CaptureForward.xyz,GroundTint.xyz,SurfaceTint.xyz,WallTint.xyz);
    }
rgb/=Grid*Grid;
float radius=length(UV-.5);
float aa=max(fwidth(radius),.0001);
float alpha=1-smoothstep(.5-aa,.5,radius);
float ring=smoothstep(.5-10.0/220.0-aa,.5-10.0/220.0+aa,radius);
rgb=lerp(rgb,float3(1,1,1),ring);
return float4(rgb,alpha);
)");
}
#endif

// Development-only runtime QA. Include in DWMinimap.cpp before BeginPlay, then call
// DWStartMinimapVerification(this) after Super::BeginPlay(). This file is not a new
// gameplay system: without all four explicit QA flags its entry point does nothing.
// It never edits assets, Config, maps or the existing gameplay data asset.
#if WITH_DEV_AUTOMATION_TESTS

#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWGameplayCinematic.h"
#include "DWGameplayHUD.h"
#include "DWLoadingTransition.h"
#include "DWMinimapWidget.h"
#include "DWPlayerCharacter.h"
#include "DWSaveSettings.h"
#include "DWSaveStorage.h"
#include "DWUIOffscreenComponent.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/TextBlock.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProperties.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"

namespace DWMinimapRuntimeQA
{
static const FString Map1=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_Map1");
static const FString MinimapWBP=TEXT("/Game/DoughWorld/UI/HUD/WBP_DWMinimap.WBP_DWMinimap_C");

static FString FullPath(FString Path)
{
    Path=FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeFilename(Path);
    FPaths::CollapseRelativeDirectories(Path);
    while(Path.EndsWith(TEXT("/")))Path.LeftChopInline(1);
    return Path;
}

static bool SamePath(const FString& A,const FString& B)
{
    return FullPath(A).Equals(FullPath(B),ESearchCase::IgnoreCase);
}

static TArray<TSharedPtr<FJsonValue>> VectorJSON(const FVector& Value)
{
    return {MakeShared<FJsonValueNumber>(Value.X),MakeShared<FJsonValueNumber>(Value.Y),MakeShared<FJsonValueNumber>(Value.Z)};
}

enum class EStage : uint8
{
    CreateSlot,WaitFirstMap,InsideReady,InsideShotWait,OutsideReady,OutsideShotWait,
    ActorReady,ActorMoved,ActorDisabled,PauseSettle,PauseHold,Resume,
    Deactivated,Reactivated,CinematicExit,CinematicHold,CinematicReturn,CinematicNatural,
    SaveFirstReload,WaitFirstReload,SaveSecondReload,WaitSecondReload,FinalShotWait
};

struct FRun
{
    TWeakObjectPtr<UDWGameInstance> GI;
    TWeakObjectPtr<ADWPlayerCharacter> ProtectedPlayer;
    TWeakObjectPtr<UDWMinimapComponent> Map;
    TWeakObjectPtr<AActor> TempActor;
    TWeakObjectPtr<UDWMinimapMarkerComponent> TempMarker;
    TWeakObjectPtr<UDWCinematicComponent> Cinematic;
    TWeakObjectPtr<UDWUIOffscreenComponent> MinimapOffscreen;
    TWeakObjectPtr<UWorld> PreviousWorld;
    TWeakObjectPtr<UDWMinimapWidget> PreviousWidget;
    FString Report,SaveDirectory,Prefix,ShotDirectory,ShotPrefix;
    TArray<TSharedPtr<FJsonValue>> Checks,Worlds,LayoutSamples,OptionalNotes;
    TArray<FString> Shots;
    EStage Stage=EStage::CreateSlot;
    double Started=FPlatformTime::Seconds(),At=Started;
    int32 PausedCaptures=0,InactiveCaptures=0,CinematicCaptures=0,LoadedMapCount=0;
    bool bPassed=true,bFinished=false,bSawOffscreenMovement=false,bOriginalDamage=true,bSawNaturalHidden=false,bSawNaturalReturn=false;
    FVector ReferencePosition=FVector::ZeroVector,MovedPosition=FVector::ZeroVector;
    static constexpr int32 Slot=0; // The game's public indices are 0..2, never 8.

    void Check(const FString& Name,bool bValue,const FString& Detail=FString())
    {
        auto Item=MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("name"),Name);Item->SetBoolField(TEXT("passed"),bValue);
        if(!Detail.IsEmpty())Item->SetStringField(TEXT("detail"),Detail);
        Checks.Add(MakeShared<FJsonValueObject>(Item));bPassed&=bValue;
        UE_LOG(LogTemp,Display,TEXT("DW_MINIMAP_RUNTIME_QA %s %s %s"),bValue?TEXT("PASS"):TEXT("FAIL"),*Name,*Detail);
    }

    void Optional(const FString& Reason)
    {
        OptionalNotes.Add(MakeShared<FJsonValueString>(Reason));
        UE_LOG(LogTemp,Display,TEXT("DW_MINIMAP_RUNTIME_QA OPTIONAL %s"),*Reason);
    }

    void Advance(EStage Next)
    {
        Stage=Next;At=FPlatformTime::Seconds();
        UE_LOG(LogTemp,Display,TEXT("DW_MINIMAP_RUNTIME_QA stage=%d"),int32(Stage));
    }

    bool IsolationValid() const
    {
        if(!GI.IsValid()||!SamePath(GI->GetSaveDirectory(),SaveDirectory))return false;
        return !SamePath(SaveDirectory,FPaths::ProjectSavedDir()/TEXT("SaveGames"))
            &&!SamePath(SaveDirectory,DWSaveStorage::DocumentsDirectory(GetDefault<UDWSaveSettings>()->DocumentsFolderName));
    }

    int32 CaptureCountFor(const ADWPlayerCharacter* Player) const
    {
        int32 Count=0;
        if(!Player||!Player->GetWorld())return Count;
        for(TActorIterator<ASceneCapture2D> It(Player->GetWorld());It;++It)
            if(It->GetOwner()==Player&&!It->IsActorBeingDestroyed())++Count;
        return Count;
    }

    int32 LiveWidgets(UWorld* World) const
    {
        TArray<UUserWidget*> Widgets;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UDWMinimapWidget::StaticClass(),true);
        return Widgets.Num();
    }

    UDWUIOffscreenComponent* FindMinimapOffscreen(APlayerController* Controller,UDWMinimapWidget* Widget)
    {
        for(UDWUIOffscreenComponent* Component:UDWUIOffscreenComponent::FindForPlayer(Controller))
        {
            UWidget* Owner=Component?Component->GetOwner().Get():nullptr;
            if(Owner&&(Owner==Widget||Owner->GetTypedOuter<UUserWidget>()==Widget))return Component;
        }
        return nullptr;
    }

    void Shot(const FString& Name)
    {
        const FString Filename=FPaths::Combine(ShotDirectory,ShotPrefix+TEXT("_")+Name+TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(Filename,true,true);
        Shots.Add(FScreenshotRequest::GetFilename());
    }

    void Protect(ADWPlayerCharacter* Player)
    {
        if(ProtectedPlayer.Get()!=Player)
        {
            if(ProtectedPlayer.IsValid())ProtectedPlayer->SetCanBeDamaged(bOriginalDamage);
            ProtectedPlayer=Player;bOriginalDamage=Player->CanBeDamaged();
        }
        Player->SetCanBeDamaged(false); // Only reached by the guarded QA singleton.
    }

    void RecordLoadedWorld(UWorld* World,UDWMinimapComponent* Component)
    {
        ++LoadedMapCount;
        auto Item=MakeShared<FJsonObject>();Item->SetNumberField(TEXT("loadNumber"),LoadedMapCount);
        Item->SetStringField(TEXT("world"),World->GetPathName());Item->SetNumberField(TEXT("worldUniqueId"),World->GetUniqueID());
        Item->SetStringField(TEXT("widget"),Component->GetMinimapWidget()?Component->GetMinimapWidget()->GetPathName():TEXT("None"));
        Worlds.Add(MakeShared<FJsonValueObject>(Item));
    }

    FVector2D ArrangedSize(UWidget* Control,TSharedPtr<FJsonObject> Sample,const FString& Name)
    {
        FVector2D Size=Control->GetPaintSpaceGeometry().GetLocalSize();
        Sample->SetNumberField(Name+TEXT("CachedWidth"),Size.X);
        Sample->SetNumberField(Name+TEXT("CachedHeight"),Size.Y);
        if(Size.IsNearlyZero()&&FSlateApplication::IsInitialized())
        {
            auto Slate=Control->GetCachedWrappedWidget();
            if(Slate.IsValid())
            {
                FWidgetPath Path;
                if(FSlateApplication::Get().GeneratePathToWidgetUnchecked(Slate.ToSharedRef(),Path,EVisibility::All))
                {
                    auto Arranged=Path.FindArrangedWidget(Slate.ToSharedRef());
                    if(Arranged.IsSet()){Size=Arranged->Geometry.GetLocalSize();Sample->SetStringField(Name+TEXT("GeometrySource"),TEXT("Actual arranged Slate WidgetPath"));}
                }
            }
        }
        return Size;
    }

    void InspectLayout(UDWMinimapComponent* Component,ADWPlayerCharacter* Player)
    {
        auto* Widget=Component->GetMinimapWidget();
        TArray<UDWMinimapComponent*> Components;Player->GetComponents<UDWMinimapComponent>(Components);
        Check(TEXT("Hero owns exactly one minimap component"),Components.Num()==1,FString::FromInt(Components.Num()));
        Check(TEXT("Cooked/standalone loads the real editable minimap WBP"),Widget&&Widget->GetClass()->GetPathName()==MinimapWBP);
        auto* RT=Component->GetDepthRenderTarget();
        Check(TEXT("Dedicated depth target matches configured resolution and R32f"),RT&&RT->SizeX==Component->CaptureResolution&&RT->SizeY==Component->CaptureResolution&&RT->RenderTargetFormat==RTF_R32f,
            RT?FString::Printf(TEXT("%dx%d; Edge AA grid %d"),RT->SizeX,RT->SizeY,Component->EdgeSampleGrid):TEXT("missing"));
        Check(TEXT("Dedicated map material instance is valid"),IsValid(Component->GetDisplayMaterial()));
        Check(TEXT("Exactly one minimap-owned capture exists"),CaptureCountFor(Player)==1);
        Check(TEXT("Exactly one minimap is in the viewport"),LiveWidgets(Player->GetWorld())==1);
        const auto* Gameplay=Cast<ADWGameplayHUD>(Player->GetController()?Cast<APlayerController>(Player->GetController())->GetHUD():nullptr);
        const auto* GameplayWidget=Gameplay?Gameplay->GetGameplayWidget():nullptr;
        const auto* Brewing=GameplayWidget?GameplayWidget->GetWidgetFromName(TEXT("BrewingCard")):nullptr;
        const float BrewingWidth=Brewing?float(Brewing->GetPaintSpaceGeometry().GetLocalSize().X):0.f;
        Check(TEXT("Minimap diameter equals the existing upper-right BrewingCard width"),BrewingWidth>0.f&&FMath::IsNearlyEqual(Component->Diameter,BrewingWidth,.25f));
        if(!Widget)return;
        UWidget* Surface=Widget->GetWidgetFromName(TEXT("MinimapView"));
        UWidget* Root=Widget->GetWidgetFromName(TEXT("MinimapRoot"));
        Check(TEXT("Actual WBP contains the authored minimap surface/root"),Surface&&Root);
        auto Sample=MakeShared<FJsonObject>();
        Sample->SetStringField(TEXT("geometrySource"),TEXT("GetPaintSpaceGeometry; desired size is not a layout substitute"));
        if(Surface&&Root)
        {
            const FVector2D SurfaceSize=ArrangedSize(Surface,Sample,TEXT("surface"));
            const FVector2D RootSize=ArrangedSize(Root,Sample,TEXT("root"));
            Check(TEXT("Painted surface is square at the configured diameter"),SurfaceSize.Equals(FVector2D(Component->Diameter,Component->Diameter),.25));
            Check(TEXT("Painted root reserves the 44-unit navigation footer"),RootSize.Equals(FVector2D(Component->Diameter,Component->Diameter+44.f),.25));
            Sample->SetNumberField(TEXT("surfaceWidth"),SurfaceSize.X);Sample->SetNumberField(TEXT("surfaceHeight"),SurfaceSize.Y);
            Sample->SetNumberField(TEXT("rootWidth"),RootSize.X);Sample->SetNumberField(TEXT("rootHeight"),RootSize.Y);
        }
        if(auto* Viewport=UGameViewportSubsystem::Get())
        {
            const FGameViewportWidgetSlot ViewportSlot=Viewport->GetWidgetSlot(Widget);
            Check(TEXT("Viewport preserves bottom-right anchors/alignment/z55"),Viewport->IsWidgetAdded(Widget)
                &&ViewportSlot.Anchors.Minimum.Equals(FVector2D(1.f,1.f))&&ViewportSlot.Anchors.Maximum.Equals(FVector2D(1.f,1.f))
                &&ViewportSlot.Alignment.Equals(FVector2D(1.f,1.f))&&ViewportSlot.ZOrder==55);
            Check(TEXT("Viewport offsets match minimap margin"),FMath::IsNearlyEqual(ViewportSlot.Offsets.Left,-float(Component->BottomRightMargin.X),.01f)
                &&FMath::IsNearlyEqual(ViewportSlot.Offsets.Top,-float(Component->BottomRightMargin.Y),.01f));
            Sample->SetNumberField(TEXT("slotLeft"),ViewportSlot.Offsets.Left);Sample->SetNumberField(TEXT("slotTop"),ViewportSlot.Offsets.Top);
        }
        else Check(TEXT("GameViewportSubsystem exists"),false);
        Sample->SetNumberField(TEXT("specifiedReferenceBorderUnits"),10);
        Sample->SetStringField(TEXT("borderEvidenceBoundary"),TEXT("10/220 is the approved source specification. Screenshot inspection must verify rasterized thickness and white appearance; these numeric fields are not a pixel measurement."));
        LayoutSamples.Add(MakeShared<FJsonValueObject>(Sample));
    }

    bool SaveAndReload(const FString& Label,UDWMinimapComponent* Component)
    {
        Check(Label+TEXT(" save isolation still matches command line"),IsolationValid()&&GI->GetActiveSlot()==Slot);
        if(!IsolationValid()||GI->GetActiveSlot()!=Slot)return false;
        const bool Saved=GI->SaveCurrentGame();Check(Label+TEXT(" saves only the isolated QA slot"),Saved,GI->LastSaveError.ToString());
        if(!Saved)return false;
        PreviousWorld=GI->GetWorld();PreviousWidget=Component->GetMinimapWidget();
        const bool Loaded=GI->LoadGameSlot(Slot);Check(Label+TEXT(" loads the same isolated slot"),Loaded,GI->LastSaveError.ToString());
        return Loaded;
    }

    bool Finish(const FString& Error=FString())
    {
        if(bFinished)return false;bFinished=true;
        if(!Error.IsEmpty())Check(Error,false);
        if(Cinematic.IsValid()&&Cinematic->IsPlaying())Cinematic->CancelCinematic();
        if(TempActor.IsValid())TempActor->Destroy();TempActor.Reset();TempMarker.Reset();
        if(GI.IsValid())
        {
            if(auto* World=GI->GetWorld())
            {
                UGameplayStatics::SetGamePaused(World,false);
                if(auto* Nav=World->GetSubsystem<UDWMinimapNavigationSubsystem>())Nav->ClearWaypoint();
            }
        }
        if(ProtectedPlayer.IsValid())ProtectedPlayer->SetCanBeDamaged(bOriginalDamage);
        auto Object=MakeShared<FJsonObject>();Object->SetBoolField(TEXT("passed"),bPassed);
        Object->SetStringField(TEXT("mode"),FPlatformProperties::RequiresCookedData()?TEXT("Windows Development cooked"):TEXT("Standalone -game"));
        Object->SetNumberField(TEXT("seconds"),FPlatformTime::Seconds()-Started);Object->SetNumberField(TEXT("slotIndex"),Slot);
        Object->SetStringField(TEXT("saveDirectory"),SaveDirectory);Object->SetStringField(TEXT("savePrefix"),Prefix);
        Object->SetNumberField(TEXT("map1WorldLoads"),LoadedMapCount);Object->SetArrayField(TEXT("checks"),Checks);
        Object->SetArrayField(TEXT("worlds"),Worlds);Object->SetArrayField(TEXT("layout"),LayoutSamples);Object->SetArrayField(TEXT("optionalChecksSkipped"),OptionalNotes);
        Object->SetStringField(TEXT("validationBoundary"),TEXT("Capture counts are requests, not GPU completions. UI screenshot thickness/color and overall appearance require inspection. This exercises Map1 World rebuilding; unrelated/new authored levels are not playtested."));
        TArray<TSharedPtr<FJsonValue>> Images;for(const FString& Filename:Shots)Images.Add(MakeShared<FJsonValueString>(Filename));Object->SetArrayField(TEXT("screenshots"),Images);
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Object,Writer);
        const bool Wrote=FFileHelper::SaveStringToFile(Text,*Report,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        UE_LOG(LogTemp,Display,TEXT("DW_MINIMAP_RUNTIME_QA FINISHED passed=%d reportSaved=%d report=%s"),bPassed,Wrote,*Report);
        if(GEngine)GEngine->Exec(GI.IsValid()?GI->GetWorld():nullptr,TEXT("QUIT"));
        return false;
    }

    bool Tick(float)
    {
        if(!GI.IsValid())return Finish(TEXT("QA GameInstance was lost"));
        const double Now=FPlatformTime::Seconds(),Elapsed=Now-At;
        if(Now-Started>120)return Finish(TEXT("QA scenario timed out at stage ")+FString::FromInt(int32(Stage)));
        if(!IsolationValid())return Finish(TEXT("QA save directory changed or became unsafe"));
        UWorld* World=GI->GetWorld();if(!World||World->bIsTearingDown)return true;
        auto* Loading=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();
        if(!Loading)return Finish(TEXT("Loading transition subsystem missing"));
        if(Loading->IsTransitionActive()||GI->IsPlayerRestorePending())return true;
        const FString MapPackage=UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
        if(Stage==EStage::CreateSlot)
        {
                if(Elapsed<1)return true;
                Check(TEXT("QA starts in the configured existing menu"),MapPackage==GI->GetConfig()->MainMenuMap);
                Check(TEXT("Existing gameplay data asset already routes NewGame to Map1"),GI->GetConfig()->GameplayMap==Map1);
                if(MapPackage!=GI->GetConfig()->MainMenuMap||GI->GetConfig()->GameplayMap!=Map1)return Finish(TEXT("QA will not rewrite the map data asset"));
                const FString SlotPath=FPaths::Combine(SaveDirectory,Prefix+TEXT("1.sav"));
                Check(TEXT("Isolated QA slot is fresh, with no deletion or overwrite"),!IFileManager::Get().FileExists(*SlotPath));
                if(IFileManager::Get().FileExists(*SlotPath))return Finish(TEXT("Choose a fresh QA directory/prefix; existing QA data is preserved"));
                const bool Created=GI->NewGame(Slot,TEXT("Minimap isolated runtime verification"));Check(TEXT("Menu NewGame creates isolated slot0"),Created,GI->LastSaveError.ToString());
                if(!Created)return Finish(TEXT("NewGame failed"));Advance(EStage::WaitFirstMap);return true;
        }
        auto* Controller=UGameplayStatics::GetPlayerController(World,0);
        auto* Player=Controller?Cast<ADWPlayerCharacter>(Controller->GetPawn()):nullptr;
        if(!Player)return true;
        Protect(Player);
        auto* Component=Player->FindComponentByClass<UDWMinimapComponent>();if(!Component)return true;
        Map=Component;
        auto* Navigation=World->GetSubsystem<UDWMinimapNavigationSubsystem>();if(!Navigation)return Finish(TEXT("World navigation subsystem missing"));


        switch(Stage)
        {
            case EStage::CreateSlot: break;
            case EStage::WaitFirstMap:
                if(MapPackage!=Map1||!Component->IsMinimapVisible()||Elapsed<.8)return true;
                RecordLoadedWorld(World,Component);InspectLayout(Component,Player);
                ReferencePosition=Player->GetActorLocation();
                Check(TEXT("Circle-inside waypoint accepted"),Navigation->SetWaypoint(ReferencePosition+FVector(1500.f,600.f,0.f),FText::FromString(TEXT("QA Inside"))));
                Advance(EStage::InsideReady);break;
            case EStage::InsideReady:
                if(Elapsed<.6)return true;
                Shot(TEXT("Map1_Inside"));Advance(EStage::InsideShotWait);break;
            case EStage::InsideShotWait:
                if(Elapsed<.3)return true;
                Check(TEXT("Circle-outside waypoint accepted"),Navigation->SetWaypoint(ReferencePosition+FVector(10000.f,0.f,18000.f),FText::FromString(TEXT("QA Outside"))));
                Advance(EStage::OutsideReady);break;
            case EStage::OutsideReady:
            {
                if(Elapsed<.6)return true;
                const auto Target=Navigation->GetNavigationTarget();
                Check(TEXT("Static destination retains Z and correct physical position"),Target.bValid&&!Target.bIsActorTarget
                    &&Target.NavigationPosition.Equals(ReferencePosition+FVector(10000.f,0.f,18000.f),.05)
                    &&Target.WorldPosition.Equals(Target.NavigationPosition,.05));
                Check(TEXT("10000cm waypoint reports 100m XY despite 180m height"),FMath::IsNearlyEqual(DWMinimapMath::DistanceXYMeters(ReferencePosition,Target.NavigationPosition),100.f,.001f));
                if(auto* Text=Cast<UTextBlock>(Component->GetMinimapWidget()->GetWidgetFromName(TEXT("NavigationInfoText"))))
                    Check(TEXT("Live navigation text shows outside target and physical meters"),Text->GetText().ToString().Contains(TEXT("QA Outside"))&&Text->GetText().ToString().Contains(TEXT("100 m")),Text->GetText().ToString());
                else Check(TEXT("NavigationInfoText is available at runtime"),false);
                Shot(TEXT("Map1_Outside"));Advance(EStage::OutsideShotWait);break;
            }
            case EStage::OutsideShotWait:
            {
                if(Elapsed<.3)return true;
                FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
                AActor* Point=World->SpawnActor<AActor>(Params);if(!Point)return Finish(TEXT("Transient QA Actor could not spawn"));
                TempActor=Point;
                auto* Root=NewObject<USceneComponent>(Point,TEXT("MinimapQARoot"));Point->SetRootComponent(Root);Point->AddInstanceComponent(Root);Root->RegisterComponent();
                Point->SetActorLocation(ReferencePosition+FVector(10000.f,0.f,2000.f));
                auto* Marker=NewObject<UDWMinimapMarkerComponent>(Point,TEXT("MinimapQAMarker"));Point->AddInstanceComponent(Marker);Marker->RegisterComponent();
                Marker->Label=FText::FromString(TEXT("QA Moving Actor"));Marker->WorldOffset=FVector(3000.f,0.f,1000.f);TempMarker=Marker;
                Check(TEXT("Transient same-World Actor marker can be selected"),Navigation->SetActorTarget(Marker));Advance(EStage::ActorReady);break;
            }
            case EStage::ActorReady:
            {
                if(Elapsed<.4)return true;
                if(!TempActor.IsValid()||!TempMarker.IsValid())return Finish(TEXT("QA Actor marker disappeared early"));
                const auto Target=Navigation->GetNavigationTarget();
                Check(TEXT("Actor target follows owner; display offset never changes physical distance"),Target.bValid&&Target.bIsActorTarget
                    &&Target.NavigationPosition.Equals(TempActor->GetActorLocation(),.05)
                    &&FMath::IsNearlyEqual(DWMinimapMath::DistanceXYMeters(ReferencePosition,Target.NavigationPosition),100.f,.001f)
                    &&FMath::IsNearlyEqual(DWMinimapMath::DistanceXYMeters(ReferencePosition,Target.WorldPosition),130.f,.001f));
                MovedPosition=ReferencePosition+FVector(0.f,10000.f,-2000.f);TempActor->SetActorLocation(MovedPosition);TempMarker->Label=FText::FromString(TEXT("QA Moved Actor"));
                Advance(EStage::ActorMoved);break;
            }
            case EStage::ActorMoved:
            {
                if(Elapsed<.3)return true;
                const auto Target=Navigation->GetNavigationTarget();
                Check(TEXT("Moving Actor position and annotation resolve live"),Target.NavigationPosition.Equals(MovedPosition,.05)&&Target.Label.ToString()==TEXT("QA Moved Actor"));
                TempMarker->bEnabled=false;Advance(EStage::ActorDisabled);break;
            }
            case EStage::ActorDisabled:
                if(Elapsed<.3)return true;
                Check(TEXT("Disabled actor marker clears selected navigation"),!Navigation->GetNavigationTarget().bValid);
                TempActor->Destroy();TempActor.Reset();TempMarker.Reset();
                Check(TEXT("Pause is accepted"),UGameplayStatics::SetGamePaused(World,true));Advance(EStage::PauseSettle);break;
            case EStage::PauseSettle:
                if(Elapsed<.3)return true;
                PausedCaptures=Component->GetCaptureRequestCount();Check(TEXT("Pause hides the minimap"),!Component->IsMinimapVisible());Advance(EStage::PauseHold);break;
            case EStage::PauseHold:
                if(Elapsed<.7)return true;
                Check(TEXT("Pause stops capture requests"),Component->GetCaptureRequestCount()==PausedCaptures);
                Check(TEXT("Unpause is accepted"),UGameplayStatics::SetGamePaused(World,false));Advance(EStage::Resume);break;
            case EStage::Resume:
                if(Elapsed<.8)return true;
                Check(TEXT("Unpause restores minimap and capture requests"),Component->IsMinimapVisible()&&Component->GetCaptureRequestCount()>PausedCaptures);
                PreviousWidget=Component->GetMinimapWidget();InactiveCaptures=Component->GetCaptureRequestCount();Component->Deactivate();
                Check(TEXT("Deactivate synchronously clears owned widget/render target/capture"),!Component->GetMinimapWidget()&&!Component->GetDepthRenderTarget()&&CaptureCountFor(Player)==0);
                Advance(EStage::Deactivated);break;
            case EStage::Deactivated:
                if(Elapsed<.5)return true;
                Check(TEXT("Deactivated component remains stopped and leaves no viewport map"),Component->GetCaptureRequestCount()==InactiveCaptures&&!Component->IsMinimapVisible()&&LiveWidgets(World)==0);
                Component->Activate(true);Advance(EStage::Reactivated);break;
            case EStage::Reactivated:
            {
                if(Elapsed<.8||!Component->IsMinimapVisible())return true;
                Check(TEXT("Reactivation rebuilds once without duplicate capture/UI"),Component->GetMinimapWidget()!=PreviousWidget.Get()
                    &&Component->GetCaptureRequestCount()>InactiveCaptures&&CaptureCountFor(Player)==1&&LiveWidgets(World)==1);
                MinimapOffscreen=FindMinimapOffscreen(Controller,Component->GetMinimapWidget());
                Check(TEXT("Minimap reuses the existing DWUIOffscreen component"),MinimapOffscreen.IsValid());
                UDWCinematicComponent* Well=nullptr;
                for(TActorIterator<AActor> It(World);It;++It)
                    if(auto* Candidate=It->FindComponentByClass<UDWCinematicComponent>())
                        if(Candidate->bViewOnly){Well=Candidate;break;}
                if(!Well||!MinimapOffscreen.IsValid())
                {
                    Optional(TEXT("Existing eligible well cinematic not present; no new map/event/animation component was created."));Advance(EStage::SaveFirstReload);break;
                }
                if(!Well->PlayCinematic(Controller))
                {
                    Optional(TEXT("Existing well cinematic rejected replay (may already be completed); its saved event identity was not reset."));Advance(EStage::SaveFirstReload);break;
                }
                Cinematic=Well;bSawOffscreenMovement=false;Advance(EStage::CinematicExit);break;
            }
            case EStage::CinematicExit:
                if(MinimapOffscreen.IsValid()&&!MinimapOffscreen->GetCurrentOffset().IsNearlyZero())bSawOffscreenMovement=true;
                if(Elapsed<.7)return true;
                Check(TEXT("Existing cinematic animates the minimap's reused offscreen wrapper"),MinimapOffscreen.IsValid()&&bSawOffscreenMovement&&MinimapOffscreen->IsOffscreen());
                CinematicCaptures=Component->GetCaptureRequestCount();Shot(TEXT("Cinematic_MinimapHidden"));Advance(EStage::CinematicHold);break;
            case EStage::CinematicHold:
                if(Elapsed<.4)return true;
                Check(TEXT("Hidden cinematic minimap stops capture requests"),Component->GetCaptureRequestCount()==CinematicCaptures);
                if(Cinematic.IsValid())Cinematic->CancelCinematic();
                Check(TEXT("Cinematic cancellation restores original minimap offscreen offset"),MinimapOffscreen.IsValid()
                    &&!MinimapOffscreen->IsOffscreen()&&!MinimapOffscreen->IsAnimating()&&MinimapOffscreen->GetCurrentOffset().IsNearlyZero());
                Advance(EStage::CinematicReturn);break;
            case EStage::CinematicReturn:
                if(Elapsed<.7)return true;
                Check(TEXT("Cancel restores player view and live minimap"),Controller->GetViewTarget()==Player&&Component->IsMinimapVisible()&&Component->GetCaptureRequestCount()>CinematicCaptures);
                if(Cinematic.IsValid()&&Cinematic->PlayCinematic(Controller))
                {
                    bSawNaturalHidden=bSawNaturalReturn=false;
                    Advance(EStage::CinematicNatural);break;
                }
                return Finish(TEXT("Existing view-only cinematic could not be retried after cancellation"));
            case EStage::CinematicNatural:
                if(MinimapOffscreen.IsValid())
                {
                    bSawNaturalHidden|=MinimapOffscreen->IsOffscreen();
                    if(bSawNaturalHidden&&MinimapOffscreen->IsAnimating()&&!MinimapOffscreen->IsOffscreen())bSawNaturalReturn=true;
                }
                if(Cinematic.IsValid()&&Cinematic->IsPlaying())return true;
                Check(TEXT("Natural cinematic hides the minimap and uses the existing return spring"),bSawNaturalHidden&&bSawNaturalReturn);
                Check(TEXT("Natural completion restores original offset and player view"),MinimapOffscreen.IsValid()&&!MinimapOffscreen->IsOffscreen()
                    &&!MinimapOffscreen->IsAnimating()&&MinimapOffscreen->GetCurrentOffset().IsNearlyZero()&&Controller->GetViewTarget()==Player);
                Shot(TEXT("Cinematic_NaturalReturned"));Advance(EStage::SaveFirstReload);break;
            case EStage::SaveFirstReload:
                if(Elapsed<.4||!Component->IsMinimapVisible())return true;
                Navigation->SetWaypoint(Player->GetActorLocation()+FVector(8000.f,-6000.f,0.f),FText::FromString(TEXT("QA Not Persisted")));
                Check(TEXT("First reload has a current-World waypoint to clear"),Navigation->GetNavigationTarget().bValid);
                if(!SaveAndReload(TEXT("First reload"),Component))return Finish(TEXT("First isolated save/load failed"));
                Advance(EStage::WaitFirstReload);break;
            case EStage::WaitFirstReload:
                if(MapPackage!=Map1||World==PreviousWorld.Get()||!Component->IsMinimapVisible()||Elapsed<.8)return true;
                RecordLoadedWorld(World,Component);
                Check(TEXT("First reload creates a fresh World/widget and clears navigation"),Component->GetMinimapWidget()!=PreviousWidget.Get()&&!Navigation->GetNavigationTarget().bValid);
                Check(TEXT("First reload does not duplicate capture/UI"),CaptureCountFor(Player)==1&&LiveWidgets(World)==1);
                Advance(EStage::SaveSecondReload);break;
            case EStage::SaveSecondReload:
                if(Elapsed<.4)return true;
                Navigation->SetWaypoint(Player->GetActorLocation()+FVector(-8000.f,6000.f,0.f),FText::FromString(TEXT("QA Not Persisted Twice")));
                if(!SaveAndReload(TEXT("Second reload"),Component))return Finish(TEXT("Second isolated save/load failed"));
                Advance(EStage::WaitSecondReload);break;
            case EStage::WaitSecondReload:
                if(MapPackage!=Map1||World==PreviousWorld.Get()||!Component->IsMinimapVisible()||Elapsed<.8)return true;
                RecordLoadedWorld(World,Component);
                Check(TEXT("Second same-slot reload creates fresh UI and clears navigation again"),Component->GetMinimapWidget()!=PreviousWidget.Get()&&!Navigation->GetNavigationTarget().bValid);
                Check(TEXT("Three Map1 Worlds were exercised"),LoadedMapCount==3);
                InspectLayout(Component,Player);Shot(TEXT("Map1_AfterTwoReloads"));Advance(EStage::FinalShotWait);break;
            case EStage::FinalShotWait:
            {
                if(Elapsed<.8)return true;
                bool bImagesReady=true;for(const FString& Filename:Shots)bImagesReady&=IFileManager::Get().FileSize(*Filename)>512;
                if(!bImagesReady&&Elapsed<3)return true;
                Check(TEXT("UI screenshot requests produced PNG evidence before exit"),bImagesReady);
                return Finish();
            }
        }
        return true;
    }
};

static TSharedPtr<FRun> ActiveRun;
}
#endif

static void DWStartMinimapVerification(UDWMinimapComponent* Component)
{
#if WITH_DEV_AUTOMATION_TESTS
    using namespace DWMinimapRuntimeQA;
    if(ActiveRun||!FParse::Param(FCommandLine::Get(),TEXT("DWMinimapQA")))return;
    UWorld* World=Component?Component->GetWorld():nullptr;
    if(!World&&GEngine)
        for(const FWorldContext& Context:GEngine->GetWorldContexts())
            if(Context.WorldType==EWorldType::Game&&Context.World()){World=Context.World();break;}
    auto* Instance=World?World->GetGameInstance<UDWGameInstance>():nullptr;
    if(!World||!World->IsGameWorld()||GIsEditor||!Instance)return;
    FString Report,Prefix,Directory;
    const bool bFlags=FParse::Value(FCommandLine::Get(),TEXT("DWMinimapQAReport="),Report)
        &&FParse::Value(FCommandLine::Get(),TEXT("DWVerificationSavePrefix="),Prefix)
        &&FParse::Value(FCommandLine::Get(),TEXT("DWSaveTestDirectory="),Directory);
    const bool bGuard=bFlags&&!FPaths::IsRelative(Report)&&!FPaths::IsRelative(Directory)
        &&Prefix.StartsWith(TEXT("DW_QA_"))&&Prefix.Len()<80&&!Prefix.Contains(TEXT("/"))&&!Prefix.Contains(TEXT("\\"))
        &&FPaths::GetExtension(Report).Equals(TEXT("json"),ESearchCase::IgnoreCase)
        &&!FPaths::IsUnderDirectory(FullPath(Report),FullPath(FPaths::ProjectDir()))
        &&!FPaths::IsUnderDirectory(FullPath(Directory),FullPath(FPaths::ProjectDir()))
        &&!SamePath(Directory,DWSaveStorage::DocumentsDirectory(GetDefault<UDWSaveSettings>()->DocumentsFolderName))
        &&SamePath(Instance->GetSaveDirectory(),Directory);
    if(!bGuard)
    {
        UE_LOG(LogTemp,Error,TEXT("DW_MINIMAP_RUNTIME_QA refused: require QA flag, absolute external JSON report, DW_QA_ prefix, absolute non-project/non-formal save directory matching GameInstance."));
        if(GEngine)GEngine->Exec(World,TEXT("QUIT"));return;
    }
    Report=FullPath(Report);Directory=FullPath(Directory);
    if(IFileManager::Get().FileExists(*Report))
    {
        UE_LOG(LogTemp,Error,TEXT("DW_MINIMAP_RUNTIME_QA refused to overwrite existing report: %s"),*Report);
        if(GEngine)GEngine->Exec(World,TEXT("QUIT"));return;
    }
    auto Run=MakeShared<FRun>();Run->GI=Instance;Run->Report=Report;Run->Prefix=Prefix;Run->SaveDirectory=Directory;
    Run->ShotDirectory=FPaths::Combine(FPaths::GetPath(Report),TEXT("Screenshots"));
    Run->ShotPrefix=FPaths::GetBaseFilename(Report)+TEXT("_")+FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"));
    if(!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Report),true)||!IFileManager::Get().MakeDirectory(*Run->ShotDirectory,true))
    {
        UE_LOG(LogTemp,Error,TEXT("DW_MINIMAP_RUNTIME_QA could not create the external report/screenshot directory."));
        if(GEngine)GEngine->Exec(World,TEXT("QUIT"));return;
    }
    ActiveRun=Run;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float Delta){return Run->Tick(Delta);}));
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
// In a monolithic packaged exe, static constructors run before FCommandLine is initialized.
// Defer even the flag check; without QA flags no test ticker is registered.
static FDelayedAutoRegisterHelper MinimapQAStartup(EDelayedRegisterRunPhase::EndOfEngineInit,[]
{
    if(GIsEditor||!FParse::Param(FCommandLine::Get(),TEXT("DWMinimapQA")))return;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
    {
        DWStartMinimapVerification(nullptr);
        return !DWMinimapRuntimeQA::ActiveRun.IsValid();
    }));
});
}
#endif

UDWMinimapComponent::UDWMinimapComponent()
{
    bAutoActivate=true;
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    MapMaterial=FSoftObjectPath(TEXT("/Game/DoughWorld/UI/M_DWMinimapClay.M_DWMinimapClay"));
    WidgetClass=FSoftObjectPath(TEXT("/Game/DoughWorld/UI/HUD/WBP_DWMinimap.WBP_DWMinimap_C"));
}
void UDWMinimapComponent::BeginPlay()
{
    Super::BeginPlay();
    DWStartMinimapVerification(this);
    CaptureElapsed=ScanElapsed=100.f;
}
bool UDWMinimapComponent::CanShow(bool bIgnoreCinematic) const
{
    const UWorld* World=GetWorld();
    const APawn* Pawn=Cast<APawn>(GetOwner());
    const APlayerController* PC=Pawn?Cast<APlayerController>(Pawn->GetController()):nullptr;
    if(!bMinimapEnabled||!IsActive()||!World||!World->IsGameWorld()||World->bIsTearingDown||!PC||!PC->IsLocalController()
        ||PC->IsPaused()||(!bIgnoreCinematic&&PC->GetViewTarget()!=Pawn))return false;
    // An inherited native component and a manually added component must not create two maps.
    TArray<UDWMinimapComponent*> Maps;
    Pawn->GetComponents<UDWMinimapComponent>(Maps);
    for(const UDWMinimapComponent* Map:Maps)
    {
        if(!IsValid(Map)||!Map->IsRegistered()||!Map->IsActive()||!Map->bMinimapEnabled)continue;
        if(Map!=this)return false;
        break;
    }
    const ADWPlayerCharacter* Hero=Cast<ADWPlayerCharacter>(Pawn);
    if(Hero&&Hero->IsDead())return false;
    const ADWGameplayHUD* HUD=Cast<ADWGameplayHUD>(PC->GetHUD());
    if(!HUD||!HUD->IsSessionStarted()||HUD->IsBlockingGameplay()||(!bIgnoreCinematic&&UDWWorldEventSubsystem::IsPlaying(this)))return false;
    const UDWGameInstance* GI=World->GetGameInstance<UDWGameInstance>();
    const UDWLoadingTransitionSubsystem* Loading=GI?GI->GetSubsystem<UDWLoadingTransitionSubsystem>():nullptr;
    return !GI||(!GI->IsPlayerRestorePending()&&(!Loading||!Loading->IsTransitionActive()));
}
bool UDWMinimapComponent::EnsureCapture()
{
    if(IsValid(Widget)&&IsValid(CaptureActor)&&IsValid(DepthTarget)&&IsValid(DisplayMaterial))return true;
    APawn* Pawn=Cast<APawn>(GetOwner());
    APlayerController* PC=Pawn?Cast<APlayerController>(Pawn->GetController()):nullptr;
    if(!PC)return false;
    UMaterialInterface* Material=MapMaterial.LoadSynchronous();
    if(!Material)
    {
        UE_LOG(LogTemp,Error,TEXT("DW Minimap: missing dedicated UI material %s; capture disabled."),*MapMaterial.ToString());
        SetComponentTickInterval(1.f);
        return false;
    }
    // Missing-material retry throttling must not persist after the material becomes available.
    SetComponentTickInterval(0.f);
    if(!IsValid(DepthTarget))
    {
        DepthTarget=NewObject<UTextureRenderTarget2D>(this,NAME_None,RF_Transient);
        DepthTarget->ClearColor=FLinearColor::Black;
        DepthTarget->RenderTargetFormat=RTF_R32f;
        DepthTarget->bAutoGenerateMips=false;
        DepthTarget->Filter=TF_Bilinear;
        const int32 Size=FMath::Clamp(CaptureResolution,128,1024);
        DepthTarget->InitAutoFormat(Size,Size);
        DepthTarget->UpdateResourceImmediate(true);
        CaptureElapsed=100.f;
    }
    if(!IsValid(CaptureActor))
    {
        FActorSpawnParameters Params;
        Params.Owner=GetOwner();Params.ObjectFlags|=RF_Transient;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        CaptureActor=GetWorld()->SpawnActor<ASceneCapture2D>(Params);
        if(!CaptureActor)return false;
        CaptureActor->SetActorEnableCollision(false);
        USceneCaptureComponent2D* Capture=CaptureActor->GetCaptureComponent2D();
        Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;
        Capture->bAlwaysPersistRenderingState=false;
        Capture->ProjectionType=ECameraProjectionMode::Orthographic;
        Capture->CaptureSource=ESceneCaptureSource::SCS_SceneDepth;
        Capture->bAutoCalculateOrthoPlanes=false;Capture->bUpdateOrthoPlanes=false;
        Capture->TextureTarget=DepthTarget;
        Capture->PostProcessBlendWeight=0.f;
        Capture->ShowFlags.SetPostProcessing(false);
        Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetVolumetricFog(false);
        Capture->ShowFlags.SetDynamicShadows(false);Capture->ShowFlags.SetMotionBlur(false);
        Capture->ShowFlags.SetParticles(false);Capture->ShowFlags.SetNiagara(false);
        ScanElapsed=CaptureElapsed=100.f;
    }
    // A recreated render target must also replace the surviving capture's target.
    CaptureActor->GetCaptureComponent2D()->TextureTarget=DepthTarget;
    if(!IsValid(DisplayMaterial))DisplayMaterial=UMaterialInstanceDynamic::Create(Material,this);
    if(!DisplayMaterial){Cleanup();return false;}
    DisplayMaterial->SetTextureParameterValue(TEXT("MinimapDepth"),DepthTarget);
    // Reuse any surviving UI when only the capture actor needs replacement.
    if(!IsValid(Widget))
    {
        UClass* Class=WidgetClass.LoadSynchronous();
        if(!Class)Class=UDWMinimapWidget::StaticClass();
        Widget=CreateWidget<UDWMinimapWidget>(PC,Class);
    }
    if(!Widget){Cleanup();return false;}
    Widget->InitializeMinimap(this);
    Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(!Widget->IsInViewport())Widget->AddToPlayerScreen(55);
    Widget->SetDesiredSizeInViewport(FVector2D(Diameter,Diameter+44.f));
    Widget->SetPositionInViewport(-BottomRightMargin,false);
    // UE viewport size/position setters reset anchors to top-left. Restore bottom-right last.
    Widget->SetAnchorsInViewport(FAnchors(1.f,1.f));
    Widget->SetAlignmentInViewport(FVector2D(1.f,1.f));
    return true;
}
void UDWMinimapComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* F)
{
    Super::TickComponent(Dt,Type,F);
    if(bMatchBrewingCardWidth)
    {
        const APawn* Pawn=Cast<APawn>(GetOwner());
        const APlayerController* PC=Pawn?Cast<APlayerController>(Pawn->GetController()):nullptr;
        const ADWGameplayHUD* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
        const UDWGameplayWidget* Gameplay=HUD?HUD->GetGameplayWidget():nullptr;
        const UWidget* Card=Gameplay?Gameplay->GetWidgetFromName(TEXT("BrewingCard")):nullptr;
        const float Width=Card?float(Card->GetPaintSpaceGeometry().GetLocalSize().X):0.f;
        if(Width>=160.f&&Width<=400.f)Diameter=Width;
    }
    // The existing cinematic system drives DW UI Offscreen. Keep its content alive until slide-out ends.
    if(IsValid(Widget)&&UDWWorldEventSubsystem::IsPlaying(this)&&CanShow(true))
    {
        auto* Extension=Widget->GetExtension<UUIComponentUserWidgetExtension>();
        auto* Offscreen=Extension?Cast<UDWUIOffscreenComponent>(Extension->GetComponent(UDWUIOffscreenComponent::StaticClass(),TEXT("MinimapRoot"))):nullptr;
        if(Offscreen)
        {
            bVisible=!Offscreen->IsOffscreen();
            Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
            Widget->UpdateNavigationFeedback(Dt,bVisible);
            CaptureElapsed=100.f;
            return;
        }
    }
    const bool WasVisible=bVisible;
    bVisible=CanShow();
    if(!bVisible)
    {
        if(Widget)Widget->SetVisibility(ESlateVisibility::Collapsed);
        if(Widget)Widget->UpdateNavigationFeedback(Dt,false);
        CaptureElapsed=100.f;
        return;
    }
    if(!EnsureCapture()){bVisible=false;return;}
    Widget->SetVisibility(ESlateVisibility::HitTestInvisible);
    Widget->SetDesiredSizeInViewport(FVector2D(FMath::Clamp(Diameter,160.f,400.f),FMath::Clamp(Diameter,160.f,400.f)+44.f));
    Widget->SetPositionInViewport(-BottomRightMargin,false);
    Widget->SetAnchorsInViewport(FAnchors(1.f,1.f));
    Widget->SetAlignmentInViewport(FVector2D(1.f,1.f));
    Widget->SetRenderTransformPivot(FVector2D(.5f,Diameter/(2.f*(Diameter+44.f))));
    Widget->UpdateNavigationFeedback(Dt,true);
    ScanElapsed+=Dt;CaptureElapsed+=Dt;
    if(ScanElapsed>=2.f||!WasVisible){ScanActors();ScanElapsed=0.f;}
    // A rotated depth image and its marker projection must be updated together.
    // PostUpdateWork runs after the player's camera update; recapture while turning,
    // then return to the configured low-frequency cadence when the camera stops.
    const bool bViewTurned=FMath::Abs(FMath::FindDeltaAngleDegrees(CapturedYaw,ResolveCaptureYaw()))>.05f;
    if(CaptureElapsed>=FMath::Max(.05f,CaptureInterval)||!WasVisible||bViewTurned){CaptureNow();CaptureElapsed=0.f;}
    // Recenter cached geometry between captures. Icons receive the exact same translation.
    const FVector2D Shift=Projection.Project(GetPlayerLocation());
    DisplayMaterial->SetVectorParameterValue(TEXT("PlayerOffset"),FLinearColor(Shift.X,Shift.Y,0,0));
    DisplayMaterial->SetVectorParameterValue(TEXT("GroundTint"),GroundTint);
    DisplayMaterial->SetVectorParameterValue(TEXT("SurfaceTint"),SurfaceTint);
    DisplayMaterial->SetVectorParameterValue(TEXT("WallTint"),WallTint);
    DisplayMaterial->SetScalarParameterValue(TEXT("EdgeSamples"),float(FMath::Clamp(EdgeSampleGrid,1,3)));
}
void UDWMinimapComponent::RefreshMinimap()
{
    ScanElapsed=CaptureElapsed=100.f;
}
void UDWMinimapComponent::ScanActors()
{
    CachedActors.Reset();
    auto* Capture=CaptureActor?CaptureActor->GetCaptureComponent2D():nullptr;
    if(Capture)Capture->HiddenActors.Reset();
    for(TActorIterator<AActor> It(GetWorld());It;++It)
    {
        AActor* Actor=*It;
        if(Actor==CaptureActor||!IsValid(Actor))continue;
        if(Actor->FindComponentByClass<UDWMinimapMarkerComponent>()||Actor->IsA<ADWResourceNode>()
            ||Actor->IsA<ADWEnemyCharacter>()||Actor->IsA<ADWEnemyNest>())CachedActors.Add(Actor);
        // Exclude moving character geometry from this capture only. Original visibility never changes.
        if(Capture&&Actor->IsA<APawn>())Capture->HiddenActors.Add(Actor);
    }
}
float UDWMinimapComponent::ResolveCaptureYaw() const
{
    if(bFollowCameraYaw&&GetOwner())
        if(const auto* Camera=GetOwner()->FindComponentByClass<UCameraComponent>())
            return FRotator::NormalizeAxis(Camera->GetComponentRotation().Yaw);
    return FRotator::NormalizeAxis(NorthYaw);
}
void UDWMinimapComponent::CaptureNow()
{
    if(!CaptureActor||!DisplayMaterial)return;
    USceneCaptureComponent2D* Capture=CaptureActor->GetCaptureComponent2D();
    CapturedYaw=ResolveCaptureYaw();
    const FRotator Rotation(FMath::Clamp(CapturePitch,-90.f,-45.f),CapturedYaw,0.f);
    const FRotationMatrix Basis(Rotation);
    Projection.Center=GetPlayerLocation();
    Projection.Right=Basis.GetUnitAxis(EAxis::Y);Projection.Up=Basis.GetUnitAxis(EAxis::Z);
    Projection.Width=FMath::Clamp(ViewRadius,500.f,100000.f)*2.f;
    Projection.NorthScale=FMath::Max(.5f,FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Rotation.Pitch))));
    const FVector Forward=Rotation.Vector();
    const FVector Origin=Projection.Center-Forward*(Projection.Width*2.f);
    CaptureActor->SetActorLocationAndRotation(Origin,Rotation);
    Capture->OrthoWidth=Projection.Width;
    Capture->MaxViewDistanceOverride=Projection.Width*4.f;
    DisplayMaterial->SetVectorParameterValue(TEXT("CaptureRight"),FLinearColor(Projection.Right.X,Projection.Right.Y,Projection.Right.Z,0));
    DisplayMaterial->SetVectorParameterValue(TEXT("CaptureUp"),FLinearColor(Projection.Up.X,Projection.Up.Y,Projection.Up.Z,0));
    DisplayMaterial->SetVectorParameterValue(TEXT("CaptureForward"),FLinearColor(Forward.X,Forward.Y,Forward.Z,0));
    const auto* Hero=Cast<ADWPlayerCharacter>(GetOwner());
    const float GroundZ=Projection.Center.Z-(Hero?Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight():0.f);
    DisplayMaterial->SetVectorParameterValue(TEXT("CaptureInfo"),FLinearColor(Projection.Width,Projection.NorthScale,Origin.Z,GroundZ));
    DisplayMaterial->SetVectorParameterValue(TEXT("PlayerOffset"),FLinearColor::Transparent);
    Capture->CaptureScene();
    ++CaptureRequestCount;
}
FVector UDWMinimapComponent::GetPlayerLocation() const {return GetOwner()?GetOwner()->GetActorLocation():FVector::ZeroVector;}
FVector2D UDWMinimapComponent::GetPlayerForwardOnMap() const
{
    const FVector Forward=GetOwner()?GetOwner()->GetActorForwardVector():FVector::XAxisVector;
    return GetWorldDirectionOnMap(Forward);
}
FDWMinimapNavigationTarget UDWMinimapComponent::GetNavigationTarget() const
{
    auto* Navigation=GetWorld()?GetWorld()->GetSubsystem<UDWMinimapNavigationSubsystem>():nullptr;
    return Navigation?Navigation->GetNavigationTarget():FDWMinimapNavigationTarget();
}
void UDWMinimapComponent::GatherDisplayMarkers(TArray<FDWMinimapDisplayMarker>& Out) const
{
    Out.Reset();
    auto* Navigation=GetWorld()?GetWorld()->GetSubsystem<UDWMinimapNavigationSubsystem>():nullptr;
    const auto* Selected=Navigation?Navigation->GetTargetMarker():nullptr;
    for(const auto& Weak:CachedActors)
    {
        AActor* Actor=Weak.Get();
        if(!IsValid(Actor)||Actor->IsActorBeingDestroyed())continue;
        if(const auto* Marker=Actor->FindComponentByClass<UDWMinimapMarkerComponent>())
        {
            if(Marker==Selected||!Marker->IsMarkerValid())continue;
            Out.Add({Marker->GetMarkerWorldPosition(),Marker->GetMarkerColor(),Marker->Shape,Marker->Priority});
            continue;
        }
        if(Actor->IsHidden())continue;
        if(const auto* Enemy=Cast<ADWEnemyCharacter>(Actor))
        {if(bShowExistingEnemies&&Enemy->IsAlive())Out.Add({Actor->GetActorLocation(),DWMinimapMath::CategoryColor(EDWMinimapMarkerKind::Enemy),EDWMinimapMarkerShape::Circle,150});}
        else if(const auto* Nest=Cast<ADWEnemyNest>(Actor))
        {if(bShowExistingEnemies&&Nest->IsAlive())Out.Add({Actor->GetActorLocation(),DWMinimapMath::CategoryColor(EDWMinimapMarkerKind::Enemy),EDWMinimapMarkerShape::Diamond,140});}
        else if(const auto* Resource=Cast<ADWResourceNode>(Actor))
        {if(bShowExistingResources&&Resource->IsAvailable())Out.Add({Actor->GetActorLocation(),DWMinimapMath::CategoryColor(EDWMinimapMarkerKind::Resource),EDWMinimapMarkerShape::Diamond,50});}
    }
    Out.Sort([](const auto& A,const auto& B){return A.Priority<B.Priority;});
}
void UDWMinimapComponent::Cleanup()
{
    bVisible=false;
    if(Widget)Widget->RemoveFromParent();Widget=nullptr;
    if(CaptureActor)CaptureActor->Destroy();CaptureActor=nullptr;
    DisplayMaterial=nullptr;
    if(DepthTarget)DepthTarget->ReleaseResource();DepthTarget=nullptr;
    CachedActors.Reset();
}
void UDWMinimapComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Cleanup();Super::EndPlay(Reason);
}
void UDWMinimapComponent::Activate(bool bReset)
{
    Super::Activate(bReset);
    if(IsActive())
    {
        SetComponentTickInterval(0.f);
        ScanElapsed=CaptureElapsed=100.f;
    }
}
void UDWMinimapComponent::Deactivate()
{
    bVisible=false;
    CaptureElapsed=100.f;
    if(IsValid(Widget))Widget->SetVisibility(ESlateVisibility::Collapsed);
    Cleanup();
    Super::Deactivate();
}
void UDWMinimapComponent::OnUnregister()
{
    Cleanup();
    Super::OnUnregister();
}

bool UDWMinimapComponent::CreateMinimapMaterial()
{
#if WITH_EDITOR
    const FString PackageName=TEXT("/Game/DoughWorld/UI/M_DWMinimapClay");
    if(LoadObject<UMaterialInterface>(nullptr,*(PackageName+TEXT(".M_DWMinimapClay"))))return true;
    UPackage* Package=CreatePackage(*PackageName);
    UMaterial* M=NewObject<UMaterial>(Package,TEXT("M_DWMinimapClay"),RF_Public|RF_Standalone);
    M->MaterialDomain=MD_UI;M->BlendMode=BLEND_Translucent;M->TwoSided=true;
    auto Add=[M](UMaterialExpression* E){M->GetExpressionCollection().AddExpression(E);};
    auto* Depth=NewObject<UMaterialExpressionTextureObjectParameter>(M);Depth->ParameterName=TEXT("MinimapDepth");
    Depth->Texture=LoadObject<UTexture>(nullptr,TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));Add(Depth);
    auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(M);Add(UV);
    auto* Custom=NewObject<UMaterialExpressionCustom>(M);Custom->OutputType=CMOT_Float4;
    Custom->Description=TEXT("Capture-only depth to warm clay; independent circular alpha and10px/220px white ring");
    auto Input=[Custom](FName Name,UMaterialExpression* E,int32 OutputIndex=0){FCustomInput I;I.InputName=Name;I.Input.Connect(OutputIndex,E);Custom->Inputs.Add(I);};
    Input(TEXT("DepthTex"),Depth);Input(TEXT("UV"),UV);
    auto* Edge=NewObject<UMaterialExpressionScalarParameter>(M);Edge->ParameterName=TEXT("EdgeSamples");Edge->DefaultValue=3.f;Add(Edge);Input(TEXT("EdgeSamples"),Edge);
    auto Vector=[M,&Add,&Input](FName Name,FLinearColor Default)
    {auto* E=NewObject<UMaterialExpressionVectorParameter>(M);E->ParameterName=Name;E->DefaultValue=Default;Add(E);Input(Name,E,5);};
    Vector(TEXT("CaptureRight"),FLinearColor(0,1,0,0));
    Vector(TEXT("CaptureUp"),FLinearColor(.9396926f,0,.3420201f,0));
    Vector(TEXT("CaptureForward"),FLinearColor(.3420201f,0,-.9396926f,0));
    Vector(TEXT("CaptureInfo"),FLinearColor(10000,.9396926f,18800,0));
    Vector(TEXT("PlayerOffset"),FLinearColor::Transparent);
    Vector(TEXT("GroundTint"),FLinearColor(.48f,.43f,.36f,1.f));
    Vector(TEXT("SurfaceTint"),FLinearColor(.94f,.91f,.85f,1.f));
    Vector(TEXT("WallTint"),FLinearColor(.30f,.27f,.23f,1.f));
    Custom->Code=DWMinimapShaderCode();
    Add(Custom);
    auto* RGB=NewObject<UMaterialExpressionComponentMask>(M);RGB->R=RGB->G=RGB->B=true;RGB->A=false;RGB->Input.Connect(0,Custom);Add(RGB);
    auto* Alpha=NewObject<UMaterialExpressionComponentMask>(M);Alpha->R=Alpha->G=Alpha->B=false;Alpha->A=true;Alpha->Input.Connect(0,Custom);Add(Alpha);
    M->GetEditorOnlyData()->EmissiveColor.Connect(0,RGB);
    M->GetEditorOnlyData()->Opacity.Connect(0,Alpha);
    M->PostEditChange();FAssetRegistryModule::AssetCreated(M);M->MarkPackageDirty();
    const FString File=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    return UPackage::SavePackage(Package,M,*File,Args);
#else
    return false;
#endif
}


// Merge into DWMinimap.cpp only after declaring CreateMinimapWidgetBlueprint() on UDWMinimapComponent.
// This fragment creates one missing asset in the existing UI/HUD directory; it never edits an existing asset.
#include "DWMinimapWidget.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Styling/CoreStyle.h"
#include "UObject/SavePackage.h"
#endif

bool UDWMinimapComponent::CreateMinimapWidgetBlueprint()
{
#if WITH_EDITOR
    const FString PackageName=TEXT("/Game/DoughWorld/UI/HUD/WBP_DWMinimap");
    const FString AssetName=TEXT("WBP_DWMinimap");
    const FString ObjectPath=PackageName+TEXT(".")+AssetName;
    // Preserve loaded/unsaved user objects too. Existing packages are not regenerated, reparented or saved.
    if(FindObject<UObject>(nullptr,*ObjectPath) || FPackageName::DoesPackageExist(PackageName)) return true;

    UPackage* Package=CreatePackage(*PackageName);
    UWidgetBlueprint* BP=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
        UDWMinimapWidget::StaticClass(),Package,FName(*AssetName),BPTYPE_Normal,
        UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass(),TEXT("DoughWorldMinimap")));
    if(!BP)
    {
        UE_LOG(LogTemp,Error,TEXT("Minimap WBP creation failed: %s. No package saved."),*ObjectPath);
        return false;
    }
    if(!BP->WidgetTree) BP->WidgetTree=NewObject<UWidgetTree>(BP,TEXT("WidgetTree"),RF_Transactional);
    UWidgetTree* Tree=BP->WidgetTree;

    auto* Root=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("MinimapRoot"));
    Root->bIsVariable=true;
    Root->SetVisibility(ESlateVisibility::HitTestInvisible);
    Tree->RootWidget=Root;

    auto* Surface=Tree->ConstructWidget<UDWMinimapView>(UDWMinimapView::StaticClass(),TEXT("MinimapView"));
    Surface->bIsVariable=true;
    Surface->PreviewDiameter=300.f;
    Surface->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* SurfaceSlot=Root->AddChildToVerticalBox(Surface);
    SurfaceSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    SurfaceSlot->SetHorizontalAlignment(HAlign_Center);
    SurfaceSlot->SetVerticalAlignment(VAlign_Top);
    SurfaceSlot->SetPadding(FMargin(0.f));

    // Only the footer has a fixed height. The map remains D x D when the component's Diameter changes.
    auto* Footer=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("NavigationFooter"));
    Footer->bIsVariable=true;
    Footer->SetHeightOverride(44.f);
    Footer->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* FooterSlot=Root->AddChildToVerticalBox(Footer);
    FooterSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    FooterSlot->SetHorizontalAlignment(HAlign_Fill);
    FooterSlot->SetPadding(FMargin(0.f));
    auto* FooterRows=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("NavigationRows"));
    FooterRows->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* FooterContentSlot=Cast<USizeBoxSlot>(Footer->AddChild(FooterRows));
    if(FooterContentSlot)
    {
        FooterContentSlot->SetHorizontalAlignment(HAlign_Fill);
        FooterContentSlot->SetVerticalAlignment(VAlign_Top);
        FooterContentSlot->SetPadding(FMargin(0.f));
    }

    UFont* Roboto=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    auto AddRow=[Tree,FooterRows,Roboto](const TCHAR* Name,const TCHAR* PreviewText,int32 FontSize,
        const FLinearColor& Color,float TopPadding)
    {
        auto* Row=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(Name));
        Row->bIsVariable=true;
        // This Chinese Designer sample is replaced by resolved runtime FText/navigation values.
        Row->SetText(FText::FromString(PreviewText));
        const FSlateFontInfo Font=Roboto?FSlateFontInfo(Roboto,FontSize,TEXT("Regular")):
            FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),float(FontSize));
        Row->SetFont(Font); // Engine composite fallback handles Chinese; no new font asset is imported.
        Row->SetJustification(ETextJustify::Center);
        Row->SetAutoWrapText(false);
        Row->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        Row->SetColorAndOpacity(FSlateColor(Color));
        Row->SetShadowColorAndOpacity(FLinearColor(.025f,.020f,.018f,.9f));
        Row->SetShadowOffset(FVector2D(1.f,1.f));
        Row->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Slot=FooterRows->AddChildToVerticalBox(Row);
        Slot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
        Slot->SetHorizontalAlignment(HAlign_Fill);
        Slot->SetVerticalAlignment(VAlign_Top);
        Slot->SetPadding(FMargin(0.f,TopPadding,0.f,0.f));
    };
    AddRow(TEXT("NavigationInfoText"),TEXT("目标地点 · N 120 m"),11,FLinearColor::White,5.f);
    AddRow(TEXT("OutsideInfoText"),TEXT("当前位置范围外"),10,FLinearColor(.85f,.81f,.72f,1.f),2.f);

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if(BP->Status==BS_Error || !BP->GeneratedClass)
    {
        UE_LOG(LogTemp,Error,TEXT("Minimap WBP compile failed: %s. No package saved."),*ObjectPath);
        return false;
    }
    const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
    // A second check also refuses an overwrite if a file appeared while the new asset compiled.
    if(IFileManager::Get().FileExists(*Filename))
    {
        UE_LOG(LogTemp,Error,TEXT("Minimap WBP destination appeared during authoring; preserved existing file: %s"),*Filename);
        return false;
    }
    BP->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags=RF_Public|RF_Standalone;
    Args.SaveFlags=SAVE_NoError;
    const bool bSaved=UPackage::SavePackage(Package,BP,*Filename,Args);
    if(bSaved) FAssetRegistryModule::AssetCreated(BP);
    return bSaved;
#else
    return false;
#endif
}

#if WITH_EDITOR
#include "UIComponentWidgetBlueprintExtension.h"
#include "WidgetBlueprintExtension.h"
#endif
bool UDWMinimapComponent::ConfigureMinimapPresentation()
{
#if WITH_EDITOR
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/UI/HUD/WBP_DWMinimap.WBP_DWMinimap"));
    if(!BP||BP->ParentClass!=UDWMinimapWidget::StaticClass()||!BP->WidgetTree||!BP->WidgetTree->FindWidget(TEXT("MinimapRoot")))return false;
    if(auto* View=Cast<UDWMinimapView>(BP->WidgetTree->FindWidget(TEXT("MinimapView"))))View->PreviewDiameter=300.f;
    for(const auto& Entry:TArray<TPair<FName,int32>>{{TEXT("NavigationInfoText"),13},{TEXT("OutsideInfoText"),11}})
        if(auto* Text=Cast<UTextBlock>(BP->WidgetTree->FindWidget(Entry.Key)))
        {auto Font=Text->GetFont();Font.Size=Entry.Value;Text->SetFont(Font);}
    auto* Extension=UWidgetBlueprintExtension::RequestExtension<UUIComponentWidgetBlueprintExtension>(BP);
    if(!Extension->GetComponent(UDWUIOffscreenComponent::StaticClass(),TEXT("MinimapRoot")))
    {
        FText Error;
        auto* Offscreen=Cast<UDWUIOffscreenComponent>(Extension->AddComponent(UDWUIOffscreenComponent::StaticClass(),TEXT("MinimapRoot"),Error));
        if(!Offscreen)return false;
        Offscreen->ExitEdge=EDWUIExitEdge::Bottom;
        // Explicit travel works before the first tick-space geometry is cached. Other spring timings keep existing defaults.
        Offscreen->bAutoExitDistance=false;
        Offscreen->ExitDistance=700.f;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
        FKismetEditorUtilities::CompileBlueprint(BP);
        if(BP->Status==BS_Error)return false;
        FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
        const FString File=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        if(!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args))return false;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    if(BP->Status==BS_Error)return false;
    {FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
     const FString File=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
     if(!UPackage::SavePackage(BP->GetOutermost(),BP,*File,Args))return false;}
    auto* M=LoadObject<UMaterial>(nullptr,TEXT("/Game/DoughWorld/UI/M_DWMinimapClay.M_DWMinimapClay"));
    if(!M||M->MaterialDomain!=MD_UI)return false;
    UMaterialExpressionCustom* Custom=nullptr;
    for(UMaterialExpression* Expression:M->GetExpressions())
        if(auto* Candidate=Cast<UMaterialExpressionCustom>(Expression))
            if(Candidate->Description==TEXT("Capture-only depth to warm clay; independent circular alpha and10px/220px white ring"))Custom=Candidate;
    if(!Custom)return false;
    UMaterialExpressionScalarParameter* Edge=nullptr;
    for(UMaterialExpression* Expression:M->GetExpressions())
        if(auto* Candidate=Cast<UMaterialExpressionScalarParameter>(Expression))
            if(Candidate->ParameterName==TEXT("EdgeSamples"))Edge=Candidate;
    if(!Edge)
    {
        Edge=NewObject<UMaterialExpressionScalarParameter>(M);Edge->ParameterName=TEXT("EdgeSamples");Edge->DefaultValue=3.f;
        M->GetExpressionCollection().AddExpression(Edge);
    }
    bool bHasEdgeInput=false;
    for(FCustomInput& Input:Custom->Inputs)if(Input.InputName==TEXT("EdgeSamples")){Input.Input.Connect(0,Edge);bHasEdgeInput=true;}
    if(!bHasEdgeInput){FCustomInput Input;Input.InputName=TEXT("EdgeSamples");Input.Input.Connect(0,Edge);Custom->Inputs.Add(Input);}
    auto ColorInput=[&](FName Name,const FLinearColor& Value)
    {
        UMaterialExpressionVectorParameter* Parameter=nullptr;
        for(UMaterialExpression* Expression:M->GetExpressions())
            if(auto* Candidate=Cast<UMaterialExpressionVectorParameter>(Expression))
                if(Candidate->ParameterName==Name)Parameter=Candidate;
        if(!Parameter)
        {
            Parameter=NewObject<UMaterialExpressionVectorParameter>(M);
            Parameter->ParameterName=Name;Parameter->DefaultValue=Value;
            M->GetExpressionCollection().AddExpression(Parameter);
        }
        for(FCustomInput& Input:Custom->Inputs)if(Input.InputName==Name){Input.Input.Connect(5,Parameter);return;}
        FCustomInput Input;Input.InputName=Name;Input.Input.Connect(5,Parameter);Custom->Inputs.Add(Input);
    };
    ColorInput(TEXT("GroundTint"),FLinearColor(.48f,.43f,.36f,1.f));
    ColorInput(TEXT("SurfaceTint"),FLinearColor(.94f,.91f,.85f,1.f));
    ColorInput(TEXT("WallTint"),FLinearColor(.30f,.27f,.23f,1.f));
    Custom->Code=DWMinimapShaderCode();
    M->PostEditChange();M->MarkPackageDirty();
    const FString File=FPackageName::LongPackageNameToFilename(M->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    return UPackage::SavePackage(M->GetOutermost(),M,*File,Args);
#else
    return false;
#endif
}

#if WITH_EDITOR
#include "ImageUtils.h"
#include "Widgets/SWindow.h"
#endif
bool UDWMinimapComponent::CaptureAuthoringScreenshot(const FString& Filename)
{
#if WITH_EDITOR
    if(!GIsEditor||!FSlateApplication::IsInitialized()||FPaths::IsRelative(Filename)||IFileManager::Get().FileExists(*Filename))return false;
    auto Window=FSlateApplication::Get().GetActiveTopLevelWindow();
    if(!Window.IsValid())return false;
    TArray<FColor> Pixels;FIntVector Size;
    if(!FSlateApplication::Get().TakeScreenshot(Window.ToSharedRef(),Pixels,Size)||Size.X<1||Size.Y<1)return false;
    TArray64<uint8> PNG;
    FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,PNG);
    return FFileHelper::SaveArrayToFile(PNG,*Filename);
#else
    return false;
#endif
}
