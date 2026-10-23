#include "DWC01HUDAnchor.h"
#include "DWUIOffscreenComponent.h"
#include "DWWorldEvent.h"
#include "HAL/PlatformTime.h"
TSharedRef<SWidget> UDWC01HUDAnchor::RebuildWidget()
{
 if(!Offscreen)
 {
  Offscreen=NewObject<UDWUIOffscreenComponent>(this);Offscreen->ExitEdge=EDWUIExitEdge::Bottom;
  Offscreen->bAutoExitDistance=false;Offscreen->ExitDistance=500.f;Offscreen->Initialize(this);
 }
 if(!MenuOffscreen)
 {
  MenuOffscreen=NewObject<UDWUIOffscreenComponent>(this);MenuOffscreen->bFollowCinematics=false;
  MenuOffscreen->ExitEdge=Offscreen->ExitEdge;MenuOffscreen->bAutoExitDistance=false;
  MenuOffscreen->ExitDistance=Offscreen->ExitDistance;
  MenuOffscreen->AnticipationSeconds=Offscreen->AnticipationSeconds;
  MenuOffscreen->AnticipationDistance=Offscreen->AnticipationDistance;
  MenuOffscreen->ExitSeconds=Offscreen->ExitSeconds;MenuOffscreen->ReturnSeconds=Offscreen->ReturnSeconds;
  MenuOffscreen->DampingRatio=Offscreen->DampingRatio;MenuOffscreen->ReturnOscillations=Offscreen->ReturnOscillations;
  MenuOffscreen->Initialize(this);
 }
 Offscreen->PreConstruct(IsDesignTime());
 MenuOffscreen->PreConstruct(IsDesignTime());
 // This anchor is transparent: drawing clients consume GetPresentationOffset().
 // The wrappers retain the original component's real-time lifecycle unchanged.
 auto Result=MenuOffscreen->RebuildWidgetWithContent(Offscreen->RebuildWidgetWithContent(Super::RebuildWidget()));
 Offscreen->Construct();MenuOffscreen->Construct();
 MenuExitFloor=MenuReturnCorrection=FVector2D::ZeroVector;
 if(bMenuHidden)MenuOffscreen->HideForCinematic();
 return Result;
}
void UDWC01HUDAnchor::SetMenuHidden(bool bHidden)
{
 if(bMenuHidden==bHidden)return;
 // If a cinematic has taken over while a menu was open, defer the menu
 // return until that owner releases. The screen supplies this request every
 // frame, so closing the menu cannot briefly expose the HUD mid-cinematic.
 if(!bHidden&&bMenuHidden&&UDWWorldEventSubsystem::IsPlaying(this))return;
 // A menu opened during a cinematic inherits the already-hidden position,
 // so a cinematic cancellation cannot reveal it before this channel catches up.
 const FVector2D VisibleOffset=bHidden?GetPresentationOffset():GetMenuOffset();
 const FVector2D RawOffset=MenuOffscreen?MenuOffscreen->GetCurrentOffset():FVector2D::ZeroVector;
 bMenuHidden=bHidden;
 if(bHidden)
 {
  // Reopening during return must not pull an already departing HUD back to the
  // component's fixed inward anticipation point. Keep the last positive offset.
  MenuExitFloor=FVector2D(FMath::Max(0.,VisibleOffset.X),FMath::Max(0.,VisibleOffset.Y));
  MenuReturnCorrection=FVector2D::ZeroVector;
  if(MenuOffscreen)MenuOffscreen->HideForCinematic();
 }
 else
 {
  // Closing during the guarded exit starts the original return spring at the
  // displayed position, not at the component's temporarily masked raw offset.
  MenuReturnCorrection=VisibleOffset-RawOffset;MenuReturnStarted=FPlatformTime::Seconds();
  if(MenuOffscreen)MenuOffscreen->ReturnToScreen();
 }
}
FVector2D UDWC01HUDAnchor::GetMenuOffset()const
{
 if(!MenuOffscreen)return FVector2D::ZeroVector;
 FVector2D Result=MenuOffscreen->GetCurrentOffset();
 if(bMenuHidden)
 {
  if(MenuExitFloor.X>0.)Result.X=FMath::Max(Result.X,MenuExitFloor.X);
  if(MenuExitFloor.Y>0.)Result.Y=FMath::Max(Result.Y,MenuExitFloor.Y);
 }
 else if(!MenuReturnCorrection.IsNearlyZero())
 {
  const float T=FMath::Clamp(float((FPlatformTime::Seconds()-MenuReturnStarted)/FMath::Max(.01f,MenuOffscreen->ReturnSeconds)),0.f,1.f);
  Result+=MenuReturnCorrection*UDWUIOffscreenComponent::ReturnFraction(T,MenuOffscreen->DampingRatio,MenuOffscreen->ReturnOscillations);
 }
 return Result;
}
FVector2D UDWC01HUDAnchor::ComposePresentationOffsets(FVector2D Cinematic,FVector2D Menu)
{
 const auto Axis=[](double A,double B)
 {
  if(A>=0.&&B>=0.)return FMath::Max(A,B);
  if(A<=0.&&B<=0.)return FMath::Min(A,B);
  // The positive Bottom displacement wins over the other channel's inward anticipation.
  return FMath::Max(A,B);
 };
 return FVector2D(Axis(Cinematic.X,Menu.X),Axis(Cinematic.Y,Menu.Y));
}
FVector2D UDWC01HUDAnchor::GetPresentationOffset()const
{return ComposePresentationOffsets(Offscreen?Offscreen->GetCurrentOffset():FVector2D::ZeroVector,GetMenuOffset());}
bool UDWC01HUDAnchor::IsMenuMotionActive()const
{return bMenuHidden||(MenuOffscreen&&(MenuOffscreen->IsAnimating()||MenuOffscreen->IsOffscreen()||!GetMenuOffset().IsNearlyZero(.5)));}
void UDWC01HUDAnchor::ReleaseSlateResources(bool ReleaseChildren)
{if(MenuOffscreen)MenuOffscreen->Destruct();if(Offscreen)Offscreen->Destruct();Super::ReleaseSlateResources(ReleaseChildren);}
