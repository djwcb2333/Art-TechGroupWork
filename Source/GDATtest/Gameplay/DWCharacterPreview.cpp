#include "DWCharacterPreview.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/Image.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

bool UDWCharacterPreview::IsActiveFor(const USkeletalMeshComponent* Source) const
{
 return IsValid(Source)&&SourceMesh.Get()==Source&&IsValid(Host)&&IsValid(Mesh)&&IsValid(Capture)&&IsValid(Target)&&OutputImage.IsValid()
  &&Source->GetWorld()&&Source->GetWorld()==Host->GetWorld()&&Source->GetSkeletalMeshAsset()
  &&Mesh->GetSkeletalMeshAsset()==Source->GetSkeletalMeshAsset();
}

bool UDWCharacterPreview::Start(USkeletalMeshComponent* Source,UImage* Output,UAnimSequenceBase* OptionalIdle)
{
 Stop();
 if(!IsValid(Source)||!IsValid(Output)||!Source->GetWorld()||!Source->GetSkeletalMeshAsset())return false;
 FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 Host=Source->GetWorld()->SpawnActor<AActor>(Params);if(!Host)return false;Host->SetActorEnableCollision(false);
 auto* Root=NewObject<USceneComponent>(Host,NAME_None,RF_Transient);Host->SetRootComponent(Root);Host->AddInstanceComponent(Root);Root->RegisterComponent();
 Mesh=NewObject<USkeletalMeshComponent>(Host,NAME_None,RF_Transient);Host->AddInstanceComponent(Mesh);Mesh->SetupAttachment(Root);
 Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);Mesh->SetCanEverAffectNavigation(false);Mesh->SetVisibleInSceneCaptureOnly(true);Mesh->SetCastShadow(false);
 Mesh->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 Mesh->PrimaryComponentTick.bTickEvenWhenPaused=true;Mesh->bSuppressNotifyEventDispatch=true;Mesh->RegisterComponent();
 for(int32 I=0;I<Source->GetNumMaterials();++I)Mesh->SetMaterial(I,Source->GetMaterial(I));
 // A single-node sequence is evaluated only from the wall clock in Refresh.
 // Montage/additive/invalid data use the existing follower pose fallback.
 const float IdleLength=IsValid(OptionalIdle)?OptionalIdle->GetPlayLength():0.f;
 if(IsValid(OptionalIdle)&&!OptionalIdle->IsA<UAnimMontage>()&&!OptionalIdle->IsValidAdditive()&&
    Source->GetSkeletalMeshAsset()->GetSkeleton()&&OptionalIdle->GetSkeleton()==Source->GetSkeletalMeshAsset()->GetSkeleton()&&
    FMath::IsFinite(IdleLength)&&IdleLength>UE_SMALL_NUMBER)
 {
  Mesh->PlayAnimation(OptionalIdle,true);
  if(auto* Single=Mesh->GetSingleNodeInstance();Single&&Single->GetAnimationAsset()==OptionalIdle)
  {
   Single->SetPlaying(false);Mesh->SetComponentTickEnabled(false);
   PreviewIdle=OptionalIdle;PreviewIdleStartSeconds=FPlatformTime::Seconds();
  }
 }
 if(!PreviewIdle)Mesh->SetLeaderPoseComponent(Source,true,false);
 Target=NewObject<UTextureRenderTarget2D>(this,NAME_None,RF_Transient);Target->RenderTargetFormat=RTF_RGBA16f;
 Target->ClearColor=FLinearColor::Transparent;Target->InitAutoFormat(512,768);Target->UpdateResourceImmediate(true);
 Capture=NewObject<USceneCaptureComponent2D>(Host,NAME_None,RF_Transient);Host->AddInstanceComponent(Capture);Capture->SetupAttachment(Root);
 Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;Capture->ShowOnlyComponent(Mesh);Capture->TextureTarget=Target;
 // SceneColorHDR stores inverse opacity. The UI material reverses it for Slate.
 Capture->CaptureSource=SCS_SceneColorHDR;
 Capture->bCaptureEveryFrame=false;Capture->bCaptureOnMovement=false;Capture->ProjectionType=ECameraProjectionMode::Orthographic;Capture->RegisterComponent();
 SourceMesh=Source;OutputImage=Output;
 auto* AlphaMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DoughWorld/UI/Shared/M_DW_CharacterPreviewAlpha.M_DW_CharacterPreviewAlpha"));
 PreviewMaterial=AlphaMaterial?UMaterialInstanceDynamic::Create(AlphaMaterial,this):nullptr;
 if(PreviewMaterial){PreviewMaterial->SetTextureParameterValue(TEXT("PreviewCapture"),Target);Output->SetBrushFromMaterial(PreviewMaterial);}
 else {Output->SetBrushResourceObject(Target);UE_LOG(LogTemp,Warning,TEXT("Character preview alpha material unavailable; fallback is opaque."));}
 Output->SetVisibility(ESlateVisibility::HitTestInvisible);Refresh();return true;
}

void UDWCharacterPreview::Refresh()
{
 auto* Source=SourceMesh.Get();
 if(!IsValid(Source)||!IsValid(Host)||!IsValid(Mesh)||!IsValid(Capture)||!IsValid(Target)||!OutputImage.IsValid()||Source->GetWorld()!=Host->GetWorld()){Stop();return;}
 if(Mesh->GetSkeletalMeshAsset()!=Source->GetSkeletalMeshAsset()){Stop();return;}
 const double Now=FPlatformTime::Seconds();if(LastCaptureSeconds>0.&&Now-LastCaptureSeconds<1./15.)return;LastCaptureSeconds=Now;
 if(PreviewIdle)
 {
  auto* Single=Mesh->GetSingleNodeInstance();const float Length=PreviewIdle->GetPlayLength();
  if(!Single||Single->GetAnimationAsset()!=PreviewIdle.Get()||!FMath::IsFinite(Length)||Length<=UE_SMALL_NUMBER){Stop();return;}
  const double Elapsed=FMath::Max(0.,Now-PreviewIdleStartSeconds);
  Single->SetPosition(float(FMath::Fmod(Elapsed,double(Length))),false);
  // SetPosition alone changes time but does not evaluate the pose. Zero-delta update
  // cannot advance a second clock; suppress event dispatch and evaluate synchronously.
  Mesh->TickAnimation(0.f,false);Mesh->RefreshBoneTransforms();Mesh->MarkRenderDynamicDataDirty();
 }
 for(int32 I=0;I<Source->GetNumMaterials();++I)if(Mesh->GetMaterial(I)!=Source->GetMaterial(I))Mesh->SetMaterial(I,Source->GetMaterial(I));
 Host->SetActorLocation(Source->GetComponentLocation()+FVector(0,0,10000.f));Mesh->SetWorldRotation(Source->GetComponentRotation());Mesh->SetWorldScale3D(Source->GetComponentScale());
 const FBoxSphereBounds Bounds=Mesh->Bounds;const FVector Center=Bounds.Origin;const float Radius=FMath::Max(30.f,Bounds.SphereRadius);
 const FVector Forward=Source->GetOwner()?Source->GetOwner()->GetActorForwardVector():FVector::ForwardVector;
 const FVector Eye=Center+Forward*Radius*3.f;Capture->SetWorldLocation(Eye);Capture->SetWorldRotation((Center-Eye).Rotation());Capture->OrthoWidth=Radius*1.8f;Capture->CaptureScene();
}

void UDWCharacterPreview::Stop()
{
 if(auto* Image=OutputImage.Get()){Image->SetBrushResourceObject(nullptr);Image->SetVisibility(ESlateVisibility::Hidden);}
 OutputImage.Reset();SourceMesh.Reset();if(IsValid(Capture)){Capture->TextureTarget=nullptr;Capture->ClearShowOnlyComponents();}
 if(IsValid(Mesh))Mesh->SetLeaderPoseComponent(nullptr);if(IsValid(Host))Host->Destroy();
 Host=nullptr;Mesh=nullptr;Capture=nullptr;Target=nullptr;PreviewMaterial=nullptr;PreviewIdle=nullptr;PreviewIdleStartSeconds=0.;LastCaptureSeconds=0.;
}
