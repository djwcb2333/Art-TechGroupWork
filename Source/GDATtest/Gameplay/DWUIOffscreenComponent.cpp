#include "DWUIOffscreenComponent.h"
#include "Components/Widget.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Widgets/Layout/SBox.h"
#include "HAL/PlatformTime.h"
#include "UObject/UObjectIterator.h"

TSharedRef<SWidget> UDWUIOffscreenComponent::RebuildWidgetWithContent(TSharedRef<SWidget> Content)
{
 TWeakPtr<SWidget> Child=Content;TWeakObjectPtr<UDWUIOffscreenComponent> Weak=this;
 auto Box=SNew(SBox).Visibility_Lambda([Weak,Child](){auto C=Child.Pin();if(!C||!Weak.IsValid())return EVisibility::Collapsed;if(Weak->bFullyHidden)return EVisibility::Hidden;return C->GetVisibility();})[Content];
 Wrapper=Box;Apply();return Box;
}
void UDWUIOffscreenComponent::OnPreConstruct(bool D){Super::OnPreConstruct(D);bDesign=D;if(D)RestoreImmediately();}
void UDWUIOffscreenComponent::OnConstruct(){Super::OnConstruct();bConstructed=true;bDesign=!GetOwner().IsValid()||GetOwner()->IsDesignTime();RestoreImmediately();}
void UDWUIOffscreenComponent::OnDestruct(){bConstructed=false;RestoreImmediately();Super::OnDestruct();}
void UDWUIOffscreenComponent::BeginDestroy(){StopTicker();Super::BeginDestroy();}
FVector2D UDWUIOffscreenComponent::Direction()const
{switch(ExitEdge){case EDWUIExitEdge::Bottom:return {0,1};case EDWUIExitEdge::Left:return {-1,0};case EDWUIExitEdge::Right:return {1,0};default:return {0,-1};}}
float UDWUIOffscreenComponent::ComputeDistance()const
{
 if(!bAutoExitDistance||!GetOwner().IsValid())return FMath::Max(0.f,ExitDistance);
 auto* W=GetOwner().Get();const FGeometry V=UWidgetLayoutLibrary::GetViewportWidgetGeometry(W);const FGeometry G=W->GetCachedGeometry();
 const FVector2D P=V.AbsoluteToLocal(G.LocalToAbsolute(FVector2D::ZeroVector));const FVector2D Q=V.AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()));
 const FVector2D Size=V.GetLocalSize();if(Size.X<=0||Size.Y<=0)return ExitDistance;
 float D=0;switch(ExitEdge){case EDWUIExitEdge::Top:D=FMath::Max(P.Y,Q.Y);break;case EDWUIExitEdge::Bottom:D=Size.Y-FMath::Min(P.Y,Q.Y);break;case EDWUIExitEdge::Left:D=FMath::Max(P.X,Q.X);break;default:D=Size.X-FMath::Min(P.X,Q.X);break;}
 const float Scale=V.GetAccumulatedLayoutTransform().GetScale()/FMath::Max(.01f,G.GetAccumulatedLayoutTransform().GetScale());
 return FMath::Max(0.f,D+OffscreenPadding)*Scale;
}
void UDWUIOffscreenComponent::HideForCinematic()
{if(!bConstructed||bDesign||bFullyHidden||bExiting)return;bExiting=true;StartOffset=Offset;EndOffset=Direction()*ComputeDistance();Started=FPlatformTime::Seconds();StartTicker();}
void UDWUIOffscreenComponent::ReturnToScreen()
{if(!bConstructed||bDesign)return;bExiting=false;bFullyHidden=false;StartOffset=Offset;Started=FPlatformTime::Seconds();StartTicker();}
void UDWUIOffscreenComponent::RestoreImmediately(){StopTicker();bExiting=bFullyHidden=false;Offset=FVector2D::ZeroVector;Apply();}
void UDWUIOffscreenComponent::StartTicker(){if(Handle.IsValid())return;Handle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::TickAnimation));}
void UDWUIOffscreenComponent::StopTicker(){if(Handle.IsValid())FTSTicker::RemoveTicker(Handle);Handle.Reset();}
float UDWUIOffscreenComponent::ReturnFraction(float T,float D,float O)
{
 if(T>=1)return 0;if(T<=0)return 1;D=FMath::Clamp(D,.2f,1.f);const float W=FMath::Clamp(O,1.f,4.f)*2*PI;
 if(D>=.999f)return (1+W*T)*FMath::Exp(-W*T);
 const float WD=W*FMath::Sqrt(1-D*D);return FMath::Exp(-D*W*T)*(FMath::Cos(WD*T)+D*W/WD*FMath::Sin(WD*T));
}
bool UDWUIOffscreenComponent::TickAnimation(float Dt)
{
 if(!bConstructed||bDesign||!GetOwner().IsValid()){Handle.Reset();return false;}
 const float E=FPlatformTime::Seconds()-Started;
 bool Done=false;
 if(bExiting){const float A=FMath::Max(0.f,AnticipationSeconds);const FVector2D Inward=-Direction()*AnticipationDistance;
 if(A>0&&E<A){const float T=E/A;Offset=FMath::Lerp(StartOffset,Inward,T*T*(3-2*T));}
 else{const float T=FMath::Clamp((E-A)/FMath::Max(.01f,ExitSeconds),0.f,1.f);Offset=FMath::Lerp(Inward,EndOffset,T*T);Done=T>=1;if(Done){bFullyHidden=true;bExiting=false;}}}
 else{const float T=FMath::Clamp(E/FMath::Max(.01f,ReturnSeconds),0.f,1.f);Offset=StartOffset*ReturnFraction(T,DampingRatio,ReturnOscillations);Done=T>=1;}
 Apply();if(Done){Handle.Reset();return false;}return true;
}
void UDWUIOffscreenComponent::Apply(){if(auto B=Wrapper.Pin())B->SetRenderTransform(FSlateRenderTransform(Offset));}
TArray<UDWUIOffscreenComponent*> UDWUIOffscreenComponent::FindForPlayer(APlayerController* PC)
{
 TArray<UDWUIOffscreenComponent*> Out;for(TObjectIterator<UDWUIOffscreenComponent> It;It;++It){auto* C=*It;auto* W=C->GetOwner().Get();auto* U=W?W->GetTypedOuter<UUserWidget>():nullptr;
 if(C->bFollowCinematics&&C->bConstructed&&!C->bDesign&&U&&U->GetOwningPlayer()==PC)Out.Add(C);}return Out;
}
