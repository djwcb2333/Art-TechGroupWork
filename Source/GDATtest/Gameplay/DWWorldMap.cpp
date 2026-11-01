#include "DWWorldMap.h"
#include "DWMapOpenPhases.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "HAL/PlatformTime.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWMinimap.h"
#include "DWMinimapNavigation.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "LandscapeComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/PackageName.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "UObject/SavePackage.h"
#endif

namespace DWWorldMapPrivate
{
static const FName IncludeTag(TEXT("DWWorldMapInclude")),ExcludeTag(TEXT("DWWorldMapExclude"));
static const TCHAR* MaterialPath=TEXT("/Game/DoughWorld/UI/M_DWWorldMapClay.M_DWWorldMapClay");
static FString Package(const UWorld* World)
{return World?UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()):FString();}
static bool FiniteVector(FVector P)
{return FMath::IsFinite(P.X)&&FMath::IsFinite(P.Y)&&FMath::IsFinite(P.Z)&&P.GetAbsMax()<=100000000.;}
static bool FiniteColor(const FLinearColor& C)
{return FMath::IsFinite(C.R)&&FMath::IsFinite(C.G)&&FMath::IsFinite(C.B)&&FMath::IsFinite(C.A);}
static DWWorldMapMath::Point XY(FVector V){return {V.X,V.Y};}
static DWWorldMapMath::Point P(FVector2D V){return {V.X,V.Y};}
static FVector2D UE(DWWorldMapMath::Point V){return FVector2D(V.X,V.Y);}
static bool Tagged(const AActor* Actor,const UActorComponent* Component,FName Tag)
{return (Actor&&Actor->ActorHasTag(Tag))||(Component&&Component->ComponentHasTag(Tag));}
static bool IsTerrainMeshPath(const FString& Path)
{
    return Path.StartsWith(TEXT("/Game/DoughWorld/Fab/SoStylized/Environment/Rocks/"),ESearchCase::IgnoreCase)||
        Path.StartsWith(TEXT("/Engine/BasicShapes/Cube."),ESearchCase::IgnoreCase);
}
static bool IsMapGeometry(const AActor* Actor,const UPrimitiveComponent* Component,float MaxExtent)
{
    if(!IsValid(Actor)||Actor->IsActorBeingDestroyed()||Actor->IsHidden()||Actor->IsA<APawn>()||Actor->IsA<ADWEnemyNest>()||
       !IsValid(Component)||!Component->IsRegistered()||!Component->IsVisible()||Component->bHiddenInGame||
       Tagged(Actor,Component,ExcludeTag))return false;
    const bool bLandscape=Component->IsA<ULandscapeComponent>();
    const auto* Mesh=Cast<UStaticMeshComponent>(Component);
    if(!bLandscape&&(!Mesh||!Mesh->GetStaticMesh())&&!Component->IsA<UProceduralMeshComponent>())return false;
    if(Component->ComponentHasTag(TEXT("DWGatheringPresentationProxy"))||
       Component->ComponentHasTag(TEXT("DWPickupPresentationProxy"))||Component->IsSimulatingPhysics())return false;
    const FBox Box=Component->Bounds.GetBox();
    if(!Box.IsValid||!FiniteVector(Box.Min)||!FiniteVector(Box.Max))return false;
    const bool Include=Tagged(Actor,Component,IncludeTag);
    // The actual maps use Landscape plus SoStylized desert rock meshes; the
    // gathering test uses a static cube as its whitebox floor. An explicit tag
    // allows other purpose-built terrain without capturing gameplay actors.
    if(!bLandscape&&!Include)
    {
        if(!Actor->IsA<AStaticMeshActor>()||!Mesh||!Mesh->GetStaticMesh())return false;
        const FString Path=Mesh->GetStaticMesh()->GetPathName();
        if(!IsTerrainMeshPath(Path))return false;
    }
    if(!Include&&!bLandscape)
    {
        if(Actor->GetName().Contains(TEXT("Sky"),ESearchCase::IgnoreCase)||Component->GetName().Contains(TEXT("Sky"),ESearchCase::IgnoreCase))return false;
        if(FMath::Max(Box.GetExtent().X,Box.GetExtent().Y)>MaxExtent)return false;
    }
    return true;
}
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMapTerrainPolicyTest,"DoughWorld.UI.Map.TerrainPolicy",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWMapTerrainPolicyTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Desert rock is terrain"),DWWorldMapPrivate::IsTerrainMeshPath(TEXT("/Game/DoughWorld/Fab/SoStylized/Environment/Rocks/Desert/SM_Rock.SM_Rock")));
    TestTrue(TEXT("Whitebox cube floor is terrain"),DWWorldMapPrivate::IsTerrainMeshPath(TEXT("/Engine/BasicShapes/Cube.Cube")));
    TestFalse(TEXT("Building is not terrain"),DWWorldMapPrivate::IsTerrainMeshPath(TEXT("/Game/DoughWorld/Fab/Fantastic_Village_Pack/meshes/buildings/House.House")));
    TestFalse(TEXT("Enemy is not terrain"),DWWorldMapPrivate::IsTerrainMeshPath(TEXT("/Game/DoughWorld/Characters/Enemies/Enemy.Enemy")));
    return true;
}
#endif

bool DWWorldMapData::Validate(const TArray<FDWWorldMapMarkerSet>& Sets,FString& Error)
{
    if(Sets.Num()>MaxMaps){Error=TEXT("Too many map marker sets.");return false;}
    TSet<FString> Maps;int32 Total=0;
    for(const auto& Set:Sets)
    {
        if(Set.MapPackage.IsEmpty()||Set.MapPackage.Len()>512||!Set.MapPackage.StartsWith(TEXT("/Game/"))||
           Set.MapPackage.Contains(TEXT("UEDPIE_"))||!FPackageName::IsValidLongPackageName(Set.MapPackage)||Maps.Contains(Set.MapPackage))
        {Error=TEXT("Invalid or duplicate world-map package.");return false;}
        Maps.Add(Set.MapPackage);Total+=Set.Markers.Num();
        if(Set.Markers.Num()>MaxPerMap||Total>MaxTotal){Error=TEXT("Too many world-map markers.");return false;}
        TSet<FGuid> Ids;
        for(const auto& Marker:Set.Markers)
        {
            if(!Marker.Id.IsValid()||Ids.Contains(Marker.Id)||!DWWorldMapPrivate::FiniteVector(Marker.Position)||Marker.Label.ToString().Len()>MaxLabelLength)
            {Error=TEXT("Invalid or duplicate world-map marker.");return false;}
            Ids.Add(Marker.Id);
        }
        if(Set.SelectedMarker.IsValid()&&!Ids.Contains(Set.SelectedMarker))
        {Error=TEXT("Selected world-map marker does not exist.");return false;}
    }
    Error.Reset();return true;
}

bool DWWorldMapData::ValidateSave(const FDWWorldMapMarkerSave& Save,FString& Error)
{
    if(Save.Schema==0&&Save.Sets.IsEmpty()){Error.Reset();return true;}
    if(Save.Schema!=1){Error=TEXT("Unknown world-map marker schema.");return false;}
    return Validate(Save.Sets,Error);
}

ADWWorldMapBounds::ADWWorldMapBounds()
{
    PrimaryActorTick.bCanEverTick=false;
    BoundsBox=CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsBox"));SetRootComponent(BoundsBox);
    BoundsBox->SetBoxExtent(FVector(10000,10000,3000));BoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    BoundsBox->SetGenerateOverlapEvents(false);BoundsBox->SetCanEverAffectNavigation(false);
    SetActorHiddenInGame(true);
}

bool UDWWorldMapSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{return Type==EWorldType::Game||Type==EWorldType::PIE||Type==EWorldType::GamePreview;}

bool UDWWorldMapSubsystem::CanUseBoundSlot(FString& OutError) const
{
    const UWorld* W=GetWorld();const auto* GI=W?W->GetGameInstance<UDWGameInstance>():nullptr;
    if(!W||W->bIsTearingDown){OutError=TEXT("地图世界已关闭。 / Map world is unavailable.");return false;}
    const auto* Transition=GI?GI->GetSubsystem<UDWLoadingTransitionSubsystem>():nullptr;
    if(GI&&(GI->IsPlayerRestorePending()||GI->HasPlayerRestoreFailure()||(Transition&&Transition->IsTransitionActive())))
    {OutError=TEXT("请等待读档完成。 / Finish loading before opening the map.");return false;}
    if(GI&&GI->HasActiveSlot())
    {
        if(!bSlotBound||BoundSlot!=GI->GetActiveSlot())
        {OutError=TEXT("地图标记尚未绑定当前存档，请重新加载该存档。 / Reload this slot to initialize its map markers.");return false;}
    }
    else if(bSlotBound)
    {OutError=TEXT("存档已关闭。 / The previous slot is no longer active.");return false;}
    OutError.Reset();return true;
}

bool UDWWorldMapSubsystem::OpenMap(APlayerController* LocalPlayer,FVector2D ViewportSize,FString& OutError)
{return OpenMapInternal(LocalPlayer,ViewportSize,OutError,false);}
bool UDWWorldMapSubsystem::OpenMapDeferred(APlayerController* LocalPlayer,FVector2D ViewportSize,FString& OutError)
{return OpenMapInternal(LocalPlayer,ViewportSize,OutError,true);}
bool UDWWorldMapSubsystem::OpenMapInternal(APlayerController* LocalPlayer,FVector2D ViewportSize,FString& OutError,bool bDeferred)
{
    if(!CanUseBoundSlot(OutError))return false;
    UWorld* W=GetWorld();
    if(!IsValid(LocalPlayer)||LocalPlayer->GetWorld()!=W||!LocalPlayer->IsLocalPlayerController()||
       ViewportSize.ContainsNaN()||ViewportSize.X<=0.||ViewportSize.Y<=0.)
    {OutError=TEXT("地图需要有效的本地玩家和画面尺寸。 / Invalid local player or map size.");return false;}
    CloseMap();OwnerPlayer=LocalPlayer;CurrentMapPackage=DWWorldMapPrivate::Package(W);
    const APawn* Pawn=LocalPlayer->GetPawn();const auto* Mini=Pawn?Pawn->FindComponentByClass<UDWMinimapComponent>():nullptr;
    View.NorthYaw=Mini&&FMath::IsFinite(Mini->NorthYaw)?Mini->NorthYaw:0.;View.Viewport=DWWorldMapPrivate::P(ViewportSize);
    auto* Nav=W->GetSubsystem<UDWMinimapNavigationSubsystem>();
    if(Nav)Nav->OnNavigationTargetChanged.AddUniqueDynamic(this,&UDWWorldMapSubsystem::OnNavigationChanged);
    bOpen=true;bDeferredFirstCapture=bDeferred;
    if(!bDeferred&&!RefreshMap(OutError)){CloseMap();return false;}
    return true;
}

bool UDWWorldMapSubsystem::RefreshMap(FString& OutError)
{
    UWorld* W=GetWorld();APlayerController* PC=OwnerPlayer.Get();
    if(!bOpen||!CanUseBoundSlot(OutError)||!IsValid(PC)||PC->GetWorld()!=W||!PC->IsLocalPlayerController())
    {if(OutError.IsEmpty())OutError=TEXT("The map is not open.");return false;}
    APawn* Pawn=PC->GetPawn();auto* Mini=Pawn?Pawn->FindComponentByClass<UDWMinimapComponent>():nullptr;
    if(!Mini||!Mini->ImageMap.IsValidCalibration()){OutError=TEXT("Invalid shared map image calibration.");return false;}
    ADWWorldMapBounds* Config=nullptr;
    for(TActorIterator<ADWWorldMapBounds> It(W);It;++It)
    {if(!IsValid(*It)||It->IsActorBeingDestroyed())continue;if(Config){OutError=TEXT("Use only one world-map bounds actor.");return false;}Config=*It;}
    MarkerLimit=Config?FMath::Clamp(Config->MaxMarkers,1,DWWorldMapData::MaxPerMap):128;
    const float MaxZoom=Config&&FMath::IsFinite(Config->MaxZoom)?FMath::Clamp(Config->MaxZoom,1.f,32.f):8.f;
    ImageSettings=Mini->ImageMap;ImageResource=Mini->GetImageResource();ImageGeneration=ImageResource->GetGeneration();
    const double Width=ImageSettings.WidthAtViewYaw(View.NorthYaw);
    View.CaptureCenter=DWWorldMapPrivate::P(ImageSettings.WorldCenterCm);View.Center=View.CaptureCenter;
    View.CaptureWidth=Width;View.MinZoom=1.;View.MaxZoom=MaxZoom;View.Zoom=1.;
    GroundZ=float(Pawn->GetActorLocation().Z);
    if(!DWWorldMapMath::Valid(View)){OutError=TEXT("Invalid calibrated map view.");return false;}
    bDeferredFirstCapture=false;bCapturePending=true;CapturePendingAt=FPlatformTime::Seconds();OutError.Reset();return true;
}
bool UDWWorldMapSubsystem::CaptureDeferredMap(FVector2D Size,FString& Error)
{
    if(!bOpen||!bDeferredFirstCapture||Size.ContainsNaN()||!FMath::IsFinite(Size.X)||!FMath::IsFinite(Size.Y)||Size.X<=0.||Size.Y<=0.)
    {Error=TEXT("地图图片加载状态无效。 / Invalid deferred map image request.");return false;}
    View.Viewport=DWWorldMapPrivate::P(Size);return RefreshMap(Error);
}
bool UDWWorldMapSubsystem::PollCaptureReadiness(bool& Ready,FString& Error)
{
    Ready=false;UWorld* W=GetWorld();
    if(!bOpen||!W||W->bIsTearingDown||!CanUseBoundSlot(Error))return false;
    const auto* PC=OwnerPlayer.Get();const APawn* Pawn=PC?PC->GetPawn():nullptr;
    const auto* Mini=Pawn?Pawn->FindComponentByClass<UDWMinimapComponent>():nullptr;
    if(!PC||PC->GetWorld()!=W||!PC->IsLocalPlayerController()||!Mini){Error=TEXT("Map image owner changed.");return false;}
    if(bDeferredFirstCapture){Error.Reset();return true;}
    if(!ImageResource||ImageGeneration!=ImageResource->GetGeneration()||!ImageSettings.SameAs(Mini->ImageMap))
    {Error=TEXT("Map image calibration changed; reopen the map.");return false;}
    if(!ImageResource->Poll(Ready,Error))return false;
    if(!Ready&&FPlatformTime::Seconds()-CapturePendingAt>15.){Error=TEXT("Map image resource was not ready in time.");return false;}
    if(Ready)bCapturePending=false;
    return true;
}
void UDWWorldMapSubsystem::PauseCaptureDeadline(double Seconds)
{if(bCapturePending&&FMath::IsFinite(Seconds)&&Seconds>0.)CapturePendingAt+=Seconds;}

void UDWWorldMapSubsystem::UpdateMaterialView()
{
    // Image UVs are calculated from the same View as markers at paint time; no MID or GPU work.
}
bool UDWWorldMapSubsystem::SetViewportSize(FVector2D S)
{
    if(!bOpen||S.ContainsNaN()||S.X<=0||S.Y<=0)return false;
    View.Viewport=DWWorldMapPrivate::P(S);if(bDeferredFirstCapture)return true;if(!DWWorldMapMath::Valid(View))return false;
    DWWorldMapMath::ClampCenter(View);UpdateMaterialView();return true;
}
bool UDWWorldMapSubsystem::WorldToPixel(FVector Position,FVector2D& OutPixel) const
{
    DWWorldMapMath::Point P;if(!bOpen||!DWWorldMapPrivate::FiniteVector(Position)||!DWWorldMapMath::WorldToPixel(View,DWWorldMapPrivate::XY(Position),P))return false;
    OutPixel=DWWorldMapPrivate::UE(P);return true;
}
bool UDWWorldMapSubsystem::PixelToWorld(FVector2D Pixel,FVector& OutPosition) const
{
    DWWorldMapMath::Point P;if(!bOpen||!DWWorldMapMath::ContainsPixel(View,DWWorldMapPrivate::P(Pixel))||
       !DWWorldMapMath::PixelToWorld(View,DWWorldMapPrivate::P(Pixel),P)||!DWWorldMapMath::WithinCapture(View,P))return false;
    OutPosition=FVector(P.X,P.Y,GroundZ);return DWWorldMapPrivate::FiniteVector(OutPosition);
}
bool UDWWorldMapSubsystem::PanPixels(FVector2D D)
{if(!bOpen||!DWWorldMapMath::Pan(View,DWWorldMapPrivate::P(D)))return false;UpdateMaterialView();return true;}
bool UDWWorldMapSubsystem::ZoomAtPixel(float Factor,FVector2D Anchor)
{if(!bOpen||!DWWorldMapMath::ZoomAt(View,Factor,DWWorldMapPrivate::P(Anchor)))return false;UpdateMaterialView();return true;}
FVector2D UDWWorldMapSubsystem::WorldDirectionToScreen(FVector D) const
{return DWWorldMapPrivate::UE(DWWorldMapMath::WorldDirectionToScreen(DWWorldMapPrivate::XY(D),View.NorthYaw));}
UMaterialInterface* UDWWorldMapSubsystem::GetMapMaterial() const{return Material;}

const USceneCaptureComponent2D* UDWWorldMapSubsystem::GetMotionCaptureComponent() const
{return nullptr;}
FGuid UDWWorldMapSubsystem::GetSelectedMarkerId() const
{FString Error;return CanUseBoundSlot(Error)?SelectedMarker:FGuid();}
bool UDWWorldMapSubsystem::FindMarkerAtPixel(FVector2D Pixel,float Radius,FGuid& OutId) const
{
    OutId.Invalidate();FString Error;
    if(!bOpen||bUpdatingNavigation||!CanUseBoundSlot(Error)||!FMath::IsFinite(Radius)||Radius<0.f||Radius>100.f)return false;
    const int32 Hit=DWWorldMapMath::NearestMarker(View,Markers.Num(),DWWorldMapPrivate::P(Pixel),double(Radius),
        [this](int32 I){return DWWorldMapPrivate::XY(Markers[I].Position);});
    if(!Markers.IsValidIndex(Hit))return false;OutId=Markers[Hit].Id;return OutId.IsValid();
}
bool UDWWorldMapSubsystem::SelectMarkerAtPixel(FVector2D Pixel,float Radius,FGuid& OutId,FString& OutError)
{
    OutId.Invalidate();FGuid Wanted;
    if(!FindMarkerAtPixel(Pixel,Radius,Wanted)){OutError=TEXT("没有可选标记。 / No marker at this position.");return false;}
    const FDWWorldMapMarker* Found=Markers.FindByPredicate([&](const FDWWorldMapMarker& M){return M.Id==Wanted;});
    if(!Found){OutError=TEXT("标记已不可用。 / Marker is no longer available.");return false;}
    const FVector Position=Found->Position;const FText Label=Found->Label;
    TStrongObjectPtr<UDWWorldMapSubsystem> KeepAlive(this);
    TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
    TWeakObjectPtr<UDWMinimapNavigationSubsystem> WeakNav(WeakWorld.IsValid()?WeakWorld->GetSubsystem<UDWMinimapNavigationSubsystem>():nullptr);
    auto* Navigation=WeakNav.Get();
    if(!Navigation){OutError=TEXT("导航不可用。 / Navigation is unavailable.");return false;}
    // Invalidate the prior selected ID before callbacks so nested external navigation cannot leave a stale owner.
    if(SelectedMarker.IsValid()){SelectedMarker.Invalidate();++MarkerRevision;}
    const uint64 MarkerVersion=MarkerRevision,NavigationVersion=Navigation->GetNavigationRevision();
    const auto Current=[&](){FString OwnerError;return IsValid(this)&&WeakWorld.IsValid()&&!WeakWorld->bIsTearingDown&&
        GetWorld()==WeakWorld.Get()&&bOpen&&MarkerRevision==MarkerVersion&&CanUseBoundSlot(OwnerError)&&WeakNav.IsValid()&&
        WeakNav->GetWorld()==WeakWorld.Get()&&WeakNav->GetNavigationRevision()==NavigationVersion+1;};
    TGuardValue<bool> Guard(bUpdatingNavigation,true);
    if(!Navigation->SetWaypoint(Position,Label)||!Current())
    {OutError=TEXT("导航状态已变更，请重新选择。 / Navigation changed; select the marker again.");return false;}
    Navigation=WeakNav.Get();const FDWMinimapNavigationTarget Target=Navigation->GetNavigationTarget();
    if(!Current()||!Target.bValid||Target.bIsActorTarget||!Target.NavigationPosition.Equals(Position,.01)||
       !Markers.ContainsByPredicate([&](const FDWWorldMapMarker& M){return M.Id==Wanted;}))
    {OutError=TEXT("导航状态已变更，请重新选择。 / Navigation changed; select the marker again.");return false;}
    SelectedMarker=Wanted;++MarkerRevision;OutId=Wanted;OutError.Reset();return true;
}

void UDWWorldMapSubsystem::OnNavigationChanged()
{if(!bUpdatingNavigation){if(SelectedMarker.IsValid()){SelectedMarker.Invalidate();++MarkerRevision;}PendingNavigationClear.Invalidate();PendingNavigationSelection.Invalidate();}}
TArray<FDWWorldMapMarker> UDWWorldMapSubsystem::GetMarkers() const
{FString Error;return CanUseBoundSlot(Error)?Markers:TArray<FDWWorldMapMarker>();}
bool UDWWorldMapSubsystem::ToggleMarkerAtPixel(FVector2D Pixel,float Radius,FString& OutError)
{
    if(!bOpen||bUpdatingNavigation||!CanUseBoundSlot(OutError)||!FMath::IsFinite(Radius)||Radius<0.f||Radius>100.f)return false;
    FVector P;if(!PixelToWorld(Pixel,P)){OutError=TEXT("只能在地图范围内标记。 / Choose a point inside the captured map.");return false;}
    const int32 Hit=DWWorldMapMath::NearestMarker(View,Markers.Num(),DWWorldMapPrivate::P(Pixel),double(Radius),
        [this](int32 I){return DWWorldMapPrivate::XY(Markers[I].Position);});
    TStrongObjectPtr<UDWWorldMapSubsystem> KeepAlive(this);
    TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
    TWeakObjectPtr<UDWMinimapNavigationSubsystem> WeakNav(WeakWorld->GetSubsystem<UDWMinimapNavigationSubsystem>());
    if(Hit!=INDEX_NONE)
    {
        const FGuid Removed=Markers[Hit].Id;Markers.RemoveAt(Hit);++MarkerRevision;
        if(Removed==SelectedMarker)
        {
            SelectedMarker.Invalidate();++MarkerRevision;
            if(auto* Nav=WeakNav.Get()){TGuardValue<bool> Guard(bUpdatingNavigation,true);Nav->ClearWaypoint();}
        }
        OutError.Reset();return true;
    }
    if(Markers.Num()>=MarkerLimit){OutError=TEXT("地图标记数量已达到上限。 / The map marker limit was reached.");return false;}
    FDWWorldMapMarker Marker;Marker.Id=FGuid::NewGuid();Marker.Position=P;Marker.Label=FText::FromString(TEXT("标记点"));
    Markers.Add(Marker);++MarkerRevision;
    if(auto* Nav=WeakNav.Get())
    {
        if(SelectedMarker.IsValid()){SelectedMarker.Invalidate();++MarkerRevision;}
        const uint64 Revision=MarkerRevision, NavigationVersion=Nav->GetNavigationRevision();
        TGuardValue<bool> Guard(bUpdatingNavigation,true);
        const bool bSet=Nav->SetWaypoint(P,Marker.Label);
        FString OwnerError;
        if(!IsValid(this)||!WeakWorld.IsValid()||WeakWorld->bIsTearingDown||GetWorld()!=WeakWorld.Get()||
           !bOpen||MarkerRevision!=Revision||!CanUseBoundSlot(OwnerError)){OutError=TEXT("标记已创建，但导航状态已变更。 / Marker created; navigation owner changed.");return false;}
        Nav=WeakNav.Get();
        if(bSet&&Nav&&Nav->GetNavigationRevision()==NavigationVersion+1)
        {
            const FDWMinimapNavigationTarget Current=Nav->GetNavigationTarget();
            Nav=WeakNav.Get();FString OwnerErrorAfterQuery;
            if(IsValid(this)&&WeakWorld.IsValid()&&!WeakWorld->bIsTearingDown&&GetWorld()==WeakWorld.Get()&&bOpen&&
               MarkerRevision==Revision&&CanUseBoundSlot(OwnerErrorAfterQuery)&&Nav&&Nav->GetNavigationRevision()==NavigationVersion+1&&
               Current.bValid&&!Current.bIsActorTarget&&Current.NavigationPosition.Equals(P,.01))
            {SelectedMarker=Marker.Id;++MarkerRevision;}
        }
        if(SelectedMarker!=Marker.Id){OutError=TEXT("标记已创建，但导航状态已变更。 / Marker created; navigation owner changed.");return false;}
    }
    else {OutError=TEXT("标记已创建，但导航不可用。 / Marker created; navigation is unavailable.");return false;}
    OutError.Reset();return true;
}

bool UDWWorldMapSubsystem::PrepareLoadedMarkers(int32 Slot,const FDWWorldMapMarkerSave& Save,FDWWorldMapRestorePlan& Out,FString& Error) const
{
    UWorld* W=GetWorld();const auto* GI=W?W->GetGameInstance<UDWGameInstance>():nullptr;
    if(!W||W->bIsTearingDown||Slot<0||Slot>=UDWGameInstance::SaveSlotCount||!GI||GI->GetActiveSlot()!=Slot||!DWWorldMapData::ValidateSave(Save,Error))return false;
    const FString Package=DWWorldMapPrivate::Package(W);
    if(Package.IsEmpty()){Error=TEXT("Current world has no package.");return false;}
    FDWWorldMapRestorePlan Plan;Plan.World=W;Plan.Owner=const_cast<UDWWorldMapSubsystem*>(this);Plan.MapPackage=Package;Plan.Slot=Slot;Plan.SourceRevision=MarkerRevision;
    for(const auto& S:Save.Sets)if(S.MapPackage==Package){Plan.Markers=S.Markers;Plan.SelectedMarker=S.SelectedMarker;break;}
    Out=MoveTemp(Plan);Error.Reset();return true;
}
bool UDWWorldMapSubsystem::CanAdoptLoadedMarkers(const FDWWorldMapRestorePlan& Plan,FString& Error) const
{
    const UWorld* W=GetWorld();const auto* GI=W?W->GetGameInstance<UDWGameInstance>():nullptr;
    if(!W||W->bIsTearingDown||Plan.World.Get()!=W||Plan.Owner.Get()!=this||!GI||GI->GetActiveSlot()!=Plan.Slot||
       DWWorldMapPrivate::Package(W)!=Plan.MapPackage||MarkerRevision!=Plan.SourceRevision)
    {Error=TEXT("World-map restore owner or revision changed before adoption.");return false;}
    Error.Reset();return true;
}
void UDWWorldMapSubsystem::AdoptLoadedMarkersUnchecked(FDWWorldMapRestorePlan&& Plan)
{
    // Fixed native facts only. No broadcasts, widget operations, file IO or fallible work.
    PendingNavigationClear=SelectedMarker;PendingNavigationSelection=Plan.SelectedMarker;SelectedMarker.Invalidate();Markers=MoveTemp(Plan.Markers);
    CurrentMapPackage=MoveTemp(Plan.MapPackage);BoundSlot=Plan.Slot;bSlotBound=true;++MarkerRevision;
}
bool UDWWorldMapSubsystem::PublishLoadedMarkers(int32 Slot,TFunctionRef<bool()> IsCurrentRequest,FString& Error)
{
    TStrongObjectPtr<UDWWorldMapSubsystem> KeepAlive(this);
    UWorld* W=GetWorld();const TWeakObjectPtr<UWorld> WeakWorld(W);
    const TWeakObjectPtr<UDWGameInstance> WeakGI(W?W->GetGameInstance<UDWGameInstance>():nullptr);
    const uint64 Revision=MarkerRevision;
    const auto Current=[&](){const UWorld* LiveWorld=WeakWorld.Get();const auto* GI=WeakGI.Get();
        return IsValid(this)&&LiveWorld&&!LiveWorld->bIsTearingDown&&GetWorld()==LiveWorld&&GI&&GI->GetWorld()==LiveWorld&&
            GI->GetActiveSlot()==Slot&&bSlotBound&&BoundSlot==Slot&&CurrentMapPackage==DWWorldMapPrivate::Package(LiveWorld)&&
            MarkerRevision==Revision&&IsCurrentRequest();};
    if(!Current()){Error=TEXT("World-map publication owner changed.");return false;}
    TWeakObjectPtr<UDWMinimapNavigationSubsystem> WeakNav(W->GetSubsystem<UDWMinimapNavigationSubsystem>());
    if(auto* Nav=WeakNav.Get())Nav->OnNavigationTargetChanged.AddUniqueDynamic(this,&UDWWorldMapSubsystem::OnNavigationChanged);
    uint64 NavigationBeforeClear=0;bool bDidAttemptClear=false;
    if(PendingNavigationClear.IsValid()&&WeakNav.IsValid())
    {NavigationBeforeClear=WeakNav->GetNavigationRevision();bDidAttemptClear=true;PendingNavigationClear.Invalidate();TGuardValue<bool> Guard(bUpdatingNavigation,true);WeakNav->ClearWaypoint();}
    if(!Current()){Error=TEXT("World-map owner changed during navigation publication.");return false;}
    if(bDidAttemptClear&&WeakNav.IsValid()&&WeakNav->GetNavigationRevision()!=NavigationBeforeClear&&
       WeakNav->GetNavigationRevision()!=NavigationBeforeClear+1)
    {PendingNavigationSelection.Invalidate();Error.Reset();return true;}
    PendingNavigationClear.Invalidate();
    const FGuid Wanted=PendingNavigationSelection;PendingNavigationSelection.Invalidate();
    if(auto* Nav=WeakNav.Get();Nav&&Wanted.IsValid())
    {
        // Querying an invalid Actor target may clear it and synchronously broadcast.
        const uint64 QueryVersion=Nav->GetNavigationRevision();
        const FDWMinimapNavigationTarget PreviousTarget=Nav->GetNavigationTarget();
        if(!Current()){Error=TEXT("World-map owner changed during navigation query.");return false;}
        Nav=WeakNav.Get();
        if(!Nav||Nav->GetNavigationRevision()!=QueryVersion||PreviousTarget.bValid){Error.Reset();return true;}
        const auto* Marker=Markers.FindByPredicate([&](const FDWWorldMapMarker& M){return M.Id==Wanted;});
        if(Marker)
        {
            const FVector Position=Marker->Position;const FText Label=Marker->Label;
            const uint64 NavigationVersion=Nav->GetNavigationRevision();
            TGuardValue<bool> Guard(bUpdatingNavigation,true);
            const bool bSet=Nav->SetWaypoint(Position,Label);
            if(!Current()){Error=TEXT("World-map owner changed during navigation publication.");return false;}
            Nav=WeakNav.Get();
            if(bSet&&Nav&&Nav->GetNavigationRevision()==NavigationVersion+1)
            {
                const auto NavigationSnapshot=Nav->GetNavigationTarget();
                if(!Current()){Error=TEXT("World-map owner changed during navigation publication.");return false;}
                Nav=WeakNav.Get();
                if(Nav&&Nav->GetNavigationRevision()==NavigationVersion+1&&NavigationSnapshot.bValid&&!NavigationSnapshot.bIsActorTarget&&NavigationSnapshot.NavigationPosition.Equals(Position,.01)&&
                   Markers.ContainsByPredicate([&](const FDWWorldMapMarker& M){return M.Id==Wanted;})){SelectedMarker=Wanted;++MarkerRevision;}
            }
        }
    }
    Error.Reset();return true;
}
bool UDWWorldMapSubsystem::MergeMarkersForSave(int32 Slot,const FDWWorldMapMarkerSave& Existing,FDWWorldMapMarkerSave& Out,FString& Error) const
{
    if(!DWWorldMapData::ValidateSave(Existing,Error))return false;
    if(!bSlotBound&&Markers.IsEmpty()){Out=Existing;return true;} // No map activity since this world was created.
    if(!bSlotBound||BoundSlot!=Slot||CurrentMapPackage!=DWWorldMapPrivate::Package(GetWorld()))
    {Error=TEXT("World-map marker owner does not match the active slot/world.");return false;}
    FDWWorldMapMarkerSave Result=Existing;Result.Schema=1;
    auto* Set=Result.Sets.FindByPredicate([&](const FDWWorldMapMarkerSet& S){return S.MapPackage==CurrentMapPackage;});
    if(!Set){Set=&Result.Sets.AddDefaulted_GetRef();Set->MapPackage=CurrentMapPackage;}
    Set->Markers=Markers;Set->SelectedMarker=SelectedMarker;if(!DWWorldMapData::ValidateSave(Result,Error))return false;
    Out=MoveTemp(Result);return true;
}
void UDWWorldMapSubsystem::CloseMap()
{
    bCapturePending=false;bDeferredFirstCapture=false;ImageResource=nullptr;++ImageGeneration;
    CaptureActor=nullptr;DepthTarget=nullptr;Material=nullptr;OwnerPlayer.Reset();bOpen=false;
}
void UDWWorldMapSubsystem::OnWorldEndPlay(UWorld& W)
{CloseMap();if(auto* Nav=W.GetSubsystem<UDWMinimapNavigationSubsystem>())Nav->OnNavigationTargetChanged.RemoveDynamic(this,&UDWWorldMapSubsystem::OnNavigationChanged);Markers.Reset();SelectedMarker.Invalidate();bSlotBound=false;BoundSlot=INDEX_NONE;Super::OnWorldEndPlay(W);}
void UDWWorldMapSubsystem::Deinitialize()
{CloseMap();if(UWorld* W=GetWorld())if(auto* Nav=W->GetSubsystem<UDWMinimapNavigationSubsystem>())Nav->OnNavigationTargetChanged.RemoveDynamic(this,&UDWWorldMapSubsystem::OnNavigationChanged);Markers.Reset();SelectedMarker.Invalidate();bSlotBound=false;BoundSlot=INDEX_NONE;Super::Deinitialize();}


#if WITH_EDITOR
static FString DWWorldMapShaderCode()
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
float2 q=(UV-.5)*WorldMapView.xy+WorldMapView.zw;
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
return float4(rgb,1);
)");
}
#endif

bool UDWWorldMapSubsystem::CreateWorldMapMaterial(FString& OutError)
{
#if WITH_EDITOR
    const FString PackageName=TEXT("/Game/DoughWorld/UI/M_DWWorldMapClay");
    const FString AssetName=TEXT("M_DWWorldMapClay");
    const FString ObjectPath=PackageName+TEXT(".")+AssetName;
    // Never overwrite an existing disk asset, loaded asset, or unsaved package.
    if(FindObject<UObject>(nullptr,*ObjectPath)||FindPackage(nullptr,*PackageName)||FPackageName::DoesPackageExist(PackageName))
    {OutError=TEXT("大地图材质已存在；未覆盖。 / World-map material already exists; it was not overwritten.");return false;}
    UPackage* Package=CreatePackage(*PackageName);
    if(!Package){OutError=TEXT("无法创建材质包。 / Could not create material package.");return false;}
    UMaterial* M=NewObject<UMaterial>(Package,FName(*AssetName),RF_Public|RF_Standalone);
    if(!M){OutError=TEXT("无法创建材质。 / Could not create material.");return false;}
    M->MaterialDomain=MD_UI;M->BlendMode=BLEND_Translucent;M->TwoSided=true;
    auto Add=[M](UMaterialExpression* E){M->GetExpressionCollection().AddExpression(E);};
    auto* Depth=NewObject<UMaterialExpressionTextureObjectParameter>(M);Depth->ParameterName=TEXT("WorldMapDepth");
    Depth->Texture=LoadObject<UTexture>(nullptr,TEXT("/Engine/EngineResources/DefaultTexture.DefaultTexture"));Add(Depth);
    auto* UV=NewObject<UMaterialExpressionTextureCoordinate>(M);Add(UV);
    auto* Custom=NewObject<UMaterialExpressionCustom>(M);Custom->OutputType=CMOT_Float4;Custom->Description=TEXT("World-map depth to warm clay, no circle crop");
    auto Input=[Custom](FName Name,UMaterialExpression* E,int32 OutputIndex=0){FCustomInput I;I.InputName=Name;I.Input.Connect(OutputIndex,E);Custom->Inputs.Add(I);};
    Input(TEXT("DepthTex"),Depth);Input(TEXT("UV"),UV);
    auto* Edge=NewObject<UMaterialExpressionScalarParameter>(M);Edge->ParameterName=TEXT("EdgeSamples");Edge->DefaultValue=2.f;Add(Edge);Input(TEXT("EdgeSamples"),Edge);
    auto Vector=[M,&Add,&Input](FName Name,FLinearColor Default)
    {auto* E=NewObject<UMaterialExpressionVectorParameter>(M);E->ParameterName=Name;E->DefaultValue=Default;Add(E);Input(Name,E,5);};
    Vector(TEXT("WorldMapView"),FLinearColor(1,1,.5f,.5f));
    Vector(TEXT("CaptureRight"),FLinearColor(0,1,0,0));Vector(TEXT("CaptureUp"),FLinearColor(1,0,0,0));
    Vector(TEXT("CaptureForward"),FLinearColor(0,0,-1,0));Vector(TEXT("CaptureInfo"),FLinearColor(10000,1,12000,0));
    Vector(TEXT("GroundTint"),FLinearColor(.48f,.43f,.36f,1.f));Vector(TEXT("SurfaceTint"),FLinearColor(.94f,.91f,.85f,1.f));
    Vector(TEXT("WallTint"),FLinearColor(.30f,.27f,.23f,1.f));
    Custom->Code=DWWorldMapShaderCode();Add(Custom);
    auto* RGB=NewObject<UMaterialExpressionComponentMask>(M);RGB->R=RGB->G=RGB->B=true;RGB->A=false;RGB->Input.Connect(0,Custom);Add(RGB);
    auto* Alpha=NewObject<UMaterialExpressionComponentMask>(M);Alpha->R=Alpha->G=Alpha->B=false;Alpha->A=true;Alpha->Input.Connect(0,Custom);Add(Alpha);
    M->GetEditorOnlyData()->EmissiveColor.Connect(0,RGB);M->GetEditorOnlyData()->Opacity.Connect(0,Alpha);
    M->PostEditChange();FAssetRegistryModule::AssetCreated(M);M->MarkPackageDirty();
    const FString Filename=FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
    if(!UPackage::SavePackage(Package,M,*Filename,Args)){OutError=TEXT("保存大地图材质失败；请检查资产状态。 / Failed to save the map material; inspect the package.");return false;}
    OutError.Reset();return true;
#else
    OutError=TEXT("仅编辑器可创建材质。 / This authoring operation is editor-only.");return false;
#endif
}
