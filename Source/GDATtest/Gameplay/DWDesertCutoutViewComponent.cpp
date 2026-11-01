#include "DWDesertCutoutViewComponent.h"
#include "DWDesertCutoutMath.h"
#include "DWGameplayHUD.h"
#include "DWWorldEvent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "SceneView.h"
#include "UnrealClient.h"

namespace
{
 TMap<TWeakObjectPtr<UWorld>,TWeakObjectPtr<UDWDesertCutoutViewComponent>> ViewOwners;
}
UDWDesertCutoutViewComponent::UDWDesertCutoutViewComponent()
{
 bAutoActivate=true;PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bTickEvenWhenPaused=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
 Parameters=FSoftObjectPath(TEXT("/Game/DoughWorld/Environment/Materials/CameraFade/MPC_DWDesertPlayerCutout.MPC_DWDesertPlayerCutout"));
}
void UDWDesertCutoutViewComponent::BeginPlay()
{Super::BeginPlay();LoadedParameters=Parameters.LoadSynchronous();}
void UDWDesertCutoutViewComponent::ClearOwnedView()
{
 UWorld* W=GetWorld();auto* Owner=ViewOwners.Find(W);
 if(!Owner||Owner->Get()!=this)return;
 if(W&&!W->bIsTearingDown&&LoadedParameters)if(auto* I=W->GetParameterCollectionInstance(LoadedParameters))I->SetVectorParameterValue(TEXT("CutoutState"),FLinearColor(0,0,0,1));
 ViewOwners.Remove(W);
}
void UDWDesertCutoutViewComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* Tick)
{
 Super::TickComponent(Dt,Type,Tick);UWorld* W=GetWorld();auto* Pawn=Cast<APawn>(GetOwner());auto* PC=Pawn?Cast<APlayerController>(Pawn->GetController()):nullptr;
 const auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 if(!W||!W->IsGameWorld()||W->bIsTearingDown||!bCutoutEnabled||!IsActive()||!IsValid(Pawn)||Pawn->IsActorBeingDestroyed()||
    !PC||!PC->IsLocalController()||PC!=UGameplayStatics::GetPlayerController(W,0)||PC->GetPawn()!=Pawn||PC->GetViewTarget()!=Pawn||
    !HUD||!HUD->IsSessionStarted()||HUD->GetMenuPage()!=EDWMenuPage::None||UDWWorldEventSubsystem::IsPlaying(this)||!LoadedParameters)
 {ClearOwnedView();return;}
 auto* Other=ViewOwners.Find(W);
 if(Other&&Other->IsValid()&&Other->Get()!=this&&Other->Get()->GetOwner()==Pawn)return;
 auto* Local=PC->GetLocalPlayer();FSceneViewProjectionData Projection;
 if(!Local||!Local->ViewportClient||!Local->ViewportClient->Viewport||!Local->GetProjectionData(Local->ViewportClient->Viewport,Projection)){ClearOwnedView();return;}
 const FIntRect Rect=Projection.GetConstrainedViewRect();
 FVector Camera;FRotator Rotation;PC->GetPlayerViewPoint(Camera,Rotation);
 const FVector Focus=Pawn->GetActorLocation(),Right=FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y);
 const double Radius=FMath::IsFinite(RadiusCm)?FMath::Clamp(double(RadiusCm),30.,2000.):180.;
 const double Feather=FMath::IsFinite(FeatherCm)?FMath::Clamp(double(FeatherCm),0.,Radius):25.;
 FVector2D Center,Edge;DWDesertCutoutMath::Frame F;
 if(FVector::DotProduct(Focus-Camera,Rotation.Vector())<=1.||!PC->ProjectWorldLocationToScreen(Focus,Center,true)||
    !PC->ProjectWorldLocationToScreen(Focus+Right*Radius,Edge,true)||
    !DWDesertCutoutMath::Make(Center.X,Center.Y,Edge.X,Edge.Y,Rect.Width(),Rect.Height(),Radius,Feather,F))
 {ClearOwnedView();return;}
 auto* I=W->GetParameterCollectionInstance(LoadedParameters);if(!I){ClearOwnedView();return;}
 float FeetZ=Focus.Z;if(const auto* Capsule=Pawn->FindComponentByClass<UCapsuleComponent>())FeetZ=Capsule->GetComponentLocation().Z-Capsule->GetScaledCapsuleHalfHeight();
 const float Height=FMath::IsFinite(MinimumHeightAboveFeetCm)?FMath::Max(0.f,MinimumHeightAboveFeetCm):30.f;
 const float Inset=FMath::IsFinite(DepthInsetCm)?FMath::Max(0.f,DepthInsetCm):10.f;
 const float Power=FMath::IsFinite(EdgePower)?FMath::Clamp(EdgePower,.1f,64.f):27.385183334f;
 const FVector Forward=Rotation.Vector();ViewOwners.FindOrAdd(W)=this;
 I->SetVectorParameterValue(TEXT("CutoutView"),FLinearColor(F.X,F.Y,F.RadiusY,F.Aspect));
 I->SetVectorParameterValue(TEXT("CutoutPlayer"),FLinearColor(Focus.X,Focus.Y,Focus.Z,FeetZ));
 I->SetVectorParameterValue(TEXT("CutoutForward"),FLinearColor(Forward.X,Forward.Y,Forward.Z,Inset));
 I->SetVectorParameterValue(TEXT("CutoutState"),FLinearColor(1,F.FeatherY,Height,Power));
}
void UDWDesertCutoutViewComponent::EndPlay(const EEndPlayReason::Type Reason)
{ClearOwnedView();Super::EndPlay(Reason);}
void UDWDesertCutoutViewComponent::Deactivate(){ClearOwnedView();Super::Deactivate();}
void UDWDesertCutoutViewComponent::OnUnregister()
{ClearOwnedView();Super::OnUnregister();}
