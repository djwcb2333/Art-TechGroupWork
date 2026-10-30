#include "DWBuildingAuthoringViewport.h"
#include "DWBuildingAuthoringLibrary.h"
#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "SceneManagement.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "InputKeyEventArgs.h"

class FDWBuildingPreviewClient : public FEditorViewportClient
{
public:
    FDWBuildingPreviewClient(FAdvancedPreviewScene* Scene,const TSharedRef<SDWBuildingAuthoringViewport>& Widget)
        :FEditorViewportClient(nullptr,Scene,Widget),Owner(Widget)
    {
        SetViewRotation(FRotator(-25,-45,0));SetViewLocation(FVector(-550,550,450));
        SetRealtime(true);EngineShowFlags.SetGrid(false);bUsingOrbitCamera=true;
    }
    virtual bool ShouldOrbitCamera()const override{return true;}
    virtual bool InputKey(const FInputKeyEventArgs& Args)override
    {if(Args.Key==EKeys::F&&Args.Event==IE_Pressed){if(auto W=Owner.Pin())W->FocusModels();return true;}return FEditorViewportClient::InputKey(Args);}
    virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) override
    {
        FEditorViewportClient::Draw(View,PDI);
        for(int32 N=-15;N<=15;++N)
        {
            float V=N*20.f;FLinearColor C=N%5==0?FLinearColor(.22f,.3f,.35f):FLinearColor(.09f,.13f,.16f);
            PDI->DrawLine(FVector(-300,V,.2),FVector(300,V,.2),C,SDPG_World);
            PDI->DrawLine(FVector(V,-300,.2),FVector(V,300,.2),C,SDPG_World);
        }
        if(auto W=Owner.Pin())
        {
            const FBox B=W->GetFootprint();
            if(B.IsValid)
            {
                FVector V[8];for(int32 I=0;I<8;++I)V[I]=FVector(I&1?B.Max.X:B.Min.X,I&2?B.Max.Y:B.Min.Y,I&4?B.Max.Z:B.Min.Z);
                for(int32 I=0;I<8;++I)for(int32 Bit=1;Bit<=4;Bit*=2)if(!(I&Bit))PDI->DrawLine(V[I],V[I|Bit],FLinearColor(.1f,1.f,.45f),SDPG_World,1.5f);
            }
        }
    }
    virtual void Draw(FViewport* V,FCanvas* Canvas) override
    {
        FEditorViewportClient::Draw(V,Canvas);
        if(auto W=Owner.Pin();W&&GEngine)
        {FCanvasTextItem T(FVector2D(12,12),FText::FromString(W->GetDimensionsText()),GEngine->GetSmallFont(),FLinearColor::White);T.EnableShadow(FLinearColor::Black);Canvas->DrawItem(T);}
    }
private:TWeakPtr<SDWBuildingAuthoringViewport> Owner;
};

void SDWBuildingAuthoringViewport::Construct(const FArguments& Args)
{
    PreviewScene=MakeUnique<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false).SetTransactional(false).SetEditor(true));
    PreviewScene->SetFloorVisibility(false,true);PreviewScene->SetEnvironmentVisibility(false,true);PreviewScene->SetLightDirection(FRotator(-45,-45,0));PreviewScene->SetLightBrightness(3.f);PreviewScene->SetSkyBrightness(1.f);
    SEditorViewport::Construct(SEditorViewport::FArguments());
    DimensionsText=TEXT("每小格 20 cm；粗线间隔 100 cm。F 聚焦。\n绿色线框：放置占地 / 高度。玩家仅静态参照，不运行游戏逻辑。");
}
SDWBuildingAuthoringViewport::~SDWBuildingAuthoringViewport(){if(BuildingActor)BuildingActor->Destroy();if(PlayerActor)PlayerActor->Destroy();}
void SDWBuildingAuthoringViewport::AddReferencedObjects(FReferenceCollector& C){C.AddReferencedObject(Draft);C.AddReferencedObject(BuildingActor);C.AddReferencedObject(PlayerActor);}
TSharedRef<FEditorViewportClient> SDWBuildingAuthoringViewport::MakeEditorViewportClient(){return MakeShared<FDWBuildingPreviewClient>(PreviewScene.Get(),SharedThis(this));}
void SDWBuildingAuthoringViewport::SetDraft(UDWBuildingAuthoringDraft* D){Draft=D;RefreshPreview();}
FBox SDWBuildingAuthoringViewport::GetFootprint()const
{return Draft?FBox(FVector(-Draft->FootprintHalfExtent.X,-Draft->FootprintHalfExtent.Y,0),FVector(Draft->FootprintHalfExtent.X,Draft->FootprintHalfExtent.Y,Draft->FootprintHalfExtent.Z*2)):FBox(ForceInit);}

void SDWBuildingAuthoringViewport::RefreshPreview()
{
    if(BuildingActor)BuildingActor->Destroy();BuildingActor=nullptr;if(PlayerActor)PlayerActor->Destroy();PlayerActor=nullptr;
    VisibleBounds=FBox(ForceInit);if(!Draft||!PreviewScene)return;
    FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
    auto MakeActor=[&](){auto* A=PreviewScene->GetWorld()->SpawnActor<AActor>(FVector::ZeroVector,FRotator::ZeroRotator,Params);A->SetActorEnableCollision(false);auto* Root=NewObject<USceneComponent>(A);A->SetRootComponent(Root);A->AddInstanceComponent(Root);Root->RegisterComponent();return A;};
    BuildingActor=MakeActor();FBox BuildingBounds(ForceInit);
    for(const auto& Part:Draft->Parts)
    {
        UStaticMesh* Mesh=Part.Mesh.LoadSynchronous();if(!Mesh)continue;
        auto* C=NewObject<UStaticMeshComponent>(BuildingActor);BuildingActor->AddInstanceComponent(C);C->SetupAttachment(BuildingActor->GetRootComponent());C->SetMobility(EComponentMobility::Movable);C->SetStaticMesh(Mesh);C->SetRelativeTransform(Part.RelativeTransform);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);
        for(int32 I=0;I<Part.Materials.Num();++I)if(auto* M=Part.Materials[I].LoadSynchronous())C->SetMaterial(I,M);
        C->RegisterComponent();BuildingBounds+=C->Bounds.GetBox();
    }
    VisibleBounds+=BuildingBounds;VisibleBounds+=GetFootprint();
    FString PlayerInfo=TEXT("玩家参考未载入：请检查实际玩家 Blueprint 类。");
    if(UClass* Class=Draft->PlayerReferenceClass.LoadSynchronous())if(const auto* CDO=Cast<ACharacter>(Class->GetDefaultObject()))
    {
        PlayerActor=MakeActor();const auto* SourceMesh=CDO->GetMesh();const auto* SourceCapsule=CDO->GetCapsuleComponent();
        const FVector Scale=CDO->GetActorScale3D();const float Half=SourceCapsule?SourceCapsule->GetUnscaledCapsuleHalfHeight():90.f;
        const float Radius=SourceCapsule?SourceCapsule->GetUnscaledCapsuleRadius():35.f;
        PlayerActor->SetActorScale3D(Scale);PlayerActor->SetActorLocation(FVector(Draft->FootprintHalfExtent.X+Radius*FMath::Abs(Scale.X)+80.f,0,Half*FMath::Abs(Scale.Z)));
        if(SourceMesh&&SourceMesh->GetSkeletalMeshAsset())
        {
            auto* C=NewObject<USkeletalMeshComponent>(PlayerActor);PlayerActor->AddInstanceComponent(C);C->SetupAttachment(PlayerActor->GetRootComponent());C->SetSkeletalMeshAsset(SourceMesh->GetSkeletalMeshAsset());C->SetRelativeTransform(SourceMesh->GetRelativeTransform());
            C->SetAnimationMode(EAnimationMode::AnimationSingleNode);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);C->SetComponentTickEnabled(false);
            for(int32 I=0;I<SourceMesh->GetNumMaterials();++I)C->SetMaterial(I,SourceMesh->GetMaterial(I));C->RegisterComponent();VisibleBounds+=C->Bounds.GetBox();
        }
        auto* Capsule=NewObject<UCapsuleComponent>(PlayerActor);PlayerActor->AddInstanceComponent(Capsule);Capsule->SetupAttachment(PlayerActor->GetRootComponent());Capsule->SetCapsuleSize(Radius,Half);Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);Capsule->SetHiddenInGame(false);Capsule->ShapeColor=FColor::Yellow;Capsule->RegisterComponent();VisibleBounds+=Capsule->Bounds.GetBox();
        PlayerInfo=FString::Printf(TEXT("真实玩家胶囊：高 %.1f cm，直径 %.1f cm（默认静态姿势）"),Half*2*FMath::Abs(Scale.Z),Radius*2*FMath::Max(FMath::Abs(Scale.X),FMath::Abs(Scale.Y)));
    }
    const FVector Size=BuildingBounds.IsValid?BuildingBounds.GetSize():FVector::ZeroVector;
    DimensionsText=FString::Printf(TEXT("模型范围 XYZ：%.1f × %.1f × %.1f cm\n占地：%.1f × %.1f cm；高度 %.1f cm\n%s\n每小格 20 cm；粗线 100 cm。F 聚焦；鼠标环绕/缩放。"),Size.X,Size.Y,Size.Z,Draft->FootprintHalfExtent.X*2,Draft->FootprintHalfExtent.Y*2,Draft->FootprintHalfExtent.Z*2,*PlayerInfo);
    FocusModels();GetViewportClient()->Invalidate();
}
void SDWBuildingAuthoringViewport::FocusModels(){if(VisibleBounds.IsValid)GetViewportClient()->FocusViewportOnBox(VisibleBounds.ExpandBy(40),true);}
