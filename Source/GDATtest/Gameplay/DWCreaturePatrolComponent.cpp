#include "DWCreaturePatrolComponent.h"
#include "GameFramework/Character.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "DWCreatureInterface.h"
#include "DWCreaturePatrolRoutePolicy.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

UDWCreaturePatrolComponent::UDWCreaturePatrolComponent() { PrimaryComponentTick.bCanEverTick=false; }
bool UDWCreaturePatrolComponent::ValidConfiguration() const
{
 return IsValid(AnchorActor) && AnchorActor->GetWorld()==GetWorld() && !AnchorActor->IsActorBeingDestroyed()
 && !AnchorOffset.ContainsNaN() && FMath::IsFinite(PatrolRadius) && PatrolRadius>=1 && PatrolRadius<=100000
 && FMath::IsFinite(InnerRadius) && InnerRadius>=0 && InnerRadius<=PatrolRadius
 && FMath::IsFinite(WaitSeconds) && WaitSeconds>=0.1f && WaitSeconds<=3600
 && FMath::IsFinite(DecisionInterval) && DecisionInterval>=0.25f && DecisionInterval<=5
 && NavigationAttempts>=1 && NavigationAttempts<=12 && !ProjectionExtent.ContainsNaN()
 && ProjectionExtent.GetMin()>0 && ProjectionExtent.GetMax()<=10000
 && FMath::IsFinite(MaxVerticalDifference) && MaxVerticalDifference>=0 && MaxVerticalDifference<=10000;
}
void UDWCreaturePatrolComponent::MaintainPatrolSchedule()
{
 UWorld* W=GetWorld(); AActor* H=GetOwner();
 if(!bRequested||!IsActive()||PatrolMode!=EDWCreaturePatrolMode::BuildingPatrol||!W||!W->IsGameWorld()||
  !IsValid(H)||!H->HasAuthority()||W->bIsTearingDown)return;
 if(H->GetClass()->ImplementsInterface(UDWCreatureInterface::StaticClass())&&
  !IDWCreatureInterface::Execute_ReadCreatureFacts(H).bAlive)return;
 if(!IsValid(this)||!IsValid(H)||H->IsActorBeingDestroyed()||!bRequested||!IsActive()||PatrolMode!=EDWCreaturePatrolMode::BuildingPatrol)return;
 if(!FMath::IsFinite(DecisionInterval)||DecisionInterval<0.25f||DecisionInterval>5.f)return;
 if(!W->GetTimerManager().IsTimerActive(DecisionTimer)||W->GetTimerManager().GetTimerRate(DecisionTimer)!=DecisionInterval)
  W->GetTimerManager().SetTimer(DecisionTimer,this,&ThisClass::Decide,DecisionInterval,true,0.1f);
}
void UDWCreaturePatrolComponent::StartPatrol() { bRequested=true; ++DecisionGeneration; MaintainPatrolSchedule(); }
void UDWCreaturePatrolComponent::StopPatrol()
{
 bRequested=false; ++DecisionGeneration; ValidatedRoute.Reset();
 if(UWorld* W=GetWorld())W->GetTimerManager().ClearTimer(DecisionTimer);
 if(AActor* H=GetOwner();IsValid(H)&&H->GetClass()->ImplementsInterface(UDWCreatureAmbientPatrolHost::StaticClass()))
  IDWCreatureAmbientPatrolHost::Execute_StopCreatureAmbientPatrol(H,this);
 LastDecision=TEXT("Stopped");
}
void UDWCreaturePatrolComponent::RefreshPatrol()
{
 ++DecisionGeneration; ValidatedRoute.Reset();
 if(UWorld* W=GetWorld())W->GetTimerManager().ClearTimer(DecisionTimer);
 if(AActor* H=GetOwner();IsValid(H)&&H->GetClass()->ImplementsInterface(UDWCreatureAmbientPatrolHost::StaticClass()))
  IDWCreatureAmbientPatrolHost::Execute_StopCreatureAmbientPatrol(H,this);
 LastDecision=(!bRequested||!IsActive()||PatrolMode==EDWCreaturePatrolMode::Idle)?TEXT("Idle"):
  ValidConfiguration()?TEXT("Refreshing"):TEXT("InvalidConfiguration");
 MaintainPatrolSchedule();
}
void UDWCreaturePatrolComponent::SetPatrolAnchor(AActor* Anchor) { AnchorActor=Anchor; RefreshPatrol(); }
void UDWCreaturePatrolComponent::SetPatrolMode(EDWCreaturePatrolMode Mode) { PatrolMode=Mode; RefreshPatrol(); }
void UDWCreaturePatrolComponent::SetPatrolRadii(float Outer,float Inner) { PatrolRadius=Outer; InnerRadius=Inner; RefreshPatrol(); }
void UDWCreaturePatrolComponent::Deactivate() { StopPatrol(); Super::Deactivate(); }
void UDWCreaturePatrolComponent::EndPlay(const EEndPlayReason::Type Reason) { StopPatrol(); Super::EndPlay(Reason); }
void UDWCreaturePatrolComponent::CaptureSettings()
{
 RouteStartLocation=GetOwner()->GetActorLocation();
 RouteAnchor=AnchorActor; RouteAnchorTransform=AnchorActor->GetActorTransform(); RouteOffset=AnchorOffset;
 RouteProjectionExtent=ProjectionExtent; RouteRadius=PatrolRadius; RouteInner=InnerRadius;
 RouteVertical=MaxVerticalDifference; RouteWait=WaitSeconds; RouteInterval=DecisionInterval; RouteAttempts=NavigationAttempts;
}
bool UDWCreaturePatrolComponent::SettingsMatch() const
{
 return ValidConfiguration()&&RouteAnchor==AnchorActor&&RouteAnchorTransform.Equals(AnchorActor->GetActorTransform(),0.f)&&
  RouteOffset==AnchorOffset&&RouteProjectionExtent==ProjectionExtent&&RouteRadius==PatrolRadius&&RouteInner==InnerRadius&&
  RouteVertical==MaxVerticalDifference&&RouteWait==WaitSeconds&&RouteInterval==DecisionInterval&&RouteAttempts==NavigationAttempts;
}
bool UDWCreaturePatrolComponent::IsRouteCurrent(const FNavPathSharedPtr& Path) const
{
 const auto* H=Cast<ACharacter>(GetOwner());
 const auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
 return Nav&&H&&Nav->GetNavDataForProps(H->GetNavAgentPropertiesRef(),H->GetActorLocation())&&bRequested&&IsActive()&&PatrolMode==EDWCreaturePatrolMode::BuildingPatrol&&H&&
  Path.IsValid()&&Path==ValidatedRoute&&Path->IsValid()&&!Path->IsPartial()&&SettingsMatch()&&
  DWCreaturePatrolRoutePolicy::Inside(H->GetActorLocation(),AnchorActor->GetActorTransform().TransformPosition(AnchorOffset),
   PatrolRadius,H->GetCapsuleComponent()->GetScaledCapsuleRadius());
}
bool UDWCreaturePatrolComponent::ValidateRoute(const FNavPathSharedPtr& Path) const
{
 const auto* H=Cast<ACharacter>(GetOwner());
 if(!H||!Cast<AAIController>(H->GetController())||!ValidConfiguration()||!Path.IsValid()||!Path->IsValid()||Path->IsPartial())return false;
 const auto* Capsule=H->GetCapsuleComponent();
 const float Radius=Capsule->GetScaledCapsuleRadius(),Half=Capsule->GetScaledCapsuleHalfHeight();
 const FVector Center=AnchorActor->GetActorTransform().TransformPosition(AnchorOffset);
 if(!DWCreaturePatrolRoutePolicy::Inside(H->GetActorLocation(),Center,PatrolRadius,Radius))return false;
 const auto& Points=Path->GetPathPoints();
 if(Points.Num()<2||!DWCreaturePatrolRoutePolicy::Target(Points.Last().Location,Center,InnerRadius,PatrolRadius,Radius))return false;
 FCollisionQueryParams Query(SCENE_QUERY_STAT(CreaturePatrolRoute),false,H);
 FVector Previous=Points[0].Location+FVector(0,0,Half+1.f);
 // Nav points are on the walkable surface; lift the capsule by its half height.
 // The extra 1cm is query tolerance for floor contact, not additional gameplay range.
 for(const auto& Point:Points)
 {
  // Custom links may execute arbitrary traversal; only ordinary validated walk corridors are owned here.
  if(Point.CustomNavLinkId.IsValid()||!DWCreaturePatrolRoutePolicy::Inside(Point.Location,Center,PatrolRadius,Radius)||
   FMath::Abs(Point.Location.Z-Points[0].Location.Z)>MaxVerticalDifference)return false;
  const FVector Next=Point.Location+FVector(0,0,Half+1.f);
  if(GetWorld()->SweepTestByProfile(Previous,Next,Capsule->GetComponentQuat(),Capsule->GetCollisionProfileName(),
   FCollisionShape::MakeCapsule(Radius,Half),Query))return false;
  Previous=Next;
 }
 return true;
}
FNavPathSharedPtr UDWCreaturePatrolComponent::AcquireValidatedRoute(const FVector& Goal)
{
 if(!bRequested||!IsActive()||PatrolMode!=EDWCreaturePatrolMode::BuildingPatrol)return nullptr;
 if(GetOwner()&&GetOwner()->GetActorLocation()==RouteStartLocation&&IsRouteCurrent(ValidatedRoute)&&FVector::DistSquared(ValidatedRoute->GetEndLocation(),Goal)<1.&&ValidateRoute(ValidatedRoute))return ValidatedRoute;
 ValidatedRoute.Reset();
 auto* H=Cast<ACharacter>(GetOwner());
 auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
 if(!H||!Nav||!ValidConfiguration()||Goal.ContainsNaN())return nullptr;
 const auto* Data=Nav->GetNavDataForProps(H->GetNavAgentPropertiesRef(),H->GetActorLocation());
 FNavLocation Start,End;
 if(!Data||!Nav->ProjectPointToNavigation(H->GetNavAgentLocation(),Start,ProjectionExtent,Data)||
  !Nav->ProjectPointToNavigation(Goal,End,ProjectionExtent,Data)||FVector::DistSquared(End.Location,Goal)>1.)return nullptr;
 FPathFindingQuery Q(H,*Data,Start.Location,End.Location); Q.SetAllowPartialPaths(false);
 auto Result=Nav->FindPathSync(Q,EPathFindingMode::Regular);
 if(!Result.IsSuccessful()||!ValidateRoute(Result.Path))return nullptr;
 const auto* Capsule=H->GetCapsuleComponent();
 FCollisionQueryParams Query(SCENE_QUERY_STAT(CreaturePatrolStart),false,H);
 if(GetWorld()->SweepTestByProfile(H->GetActorLocation(),Start.Location+FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()+1.f),
  Capsule->GetComponentQuat(),Capsule->GetCollisionProfileName(),FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),Query))return nullptr;
 Result.Path->EnableRecalculationOnInvalidation(false);
 ValidatedRoute=Result.Path; CaptureSettings(); return ValidatedRoute;
}
bool UDWCreaturePatrolComponent::ChooseGoal(FVector& Goal)
{
 const auto* H=Cast<ACharacter>(GetOwner());
 auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
 if(!H||!Nav)return false;
 const auto* Data=Nav->GetNavDataForProps(H->GetNavAgentPropertiesRef(),H->GetActorLocation());
 if(!Data)return false;
 const float Margin=H->GetCapsuleComponent()->GetScaledCapsuleRadius();
 const float Min=InnerRadius+Margin,Max=PatrolRadius-Margin;
 const FVector Center=AnchorActor->GetActorTransform().TransformPosition(AnchorOffset);
 if(Min>Max||!DWCreaturePatrolRoutePolicy::Inside(H->GetActorLocation(),Center,PatrolRadius,Margin))return false;
 for(int32 I=0;I<NavigationAttempts;++I)
 {
  const float A=FMath::FRandRange(0.f,2.f*PI);
  const float R=FMath::Sqrt(FMath::Lerp(FMath::Square(Min),FMath::Square(Max),FMath::FRand()));
  FNavLocation T;
  if(!Nav->ProjectPointToNavigation(Center+FVector(FMath::Cos(A)*R,FMath::Sin(A)*R,0),T,ProjectionExtent,Data))continue;
  if(AcquireValidatedRoute(T.Location).IsValid()){Goal=T.Location;return true;}
 }
 return false;
}
void UDWCreaturePatrolComponent::Decide()
{
 APawn* H=Cast<APawn>(GetOwner()); UWorld* W=GetWorld();
 if(!bRequested||!IsActive()||!H||!H->HasAuthority()||!W||!H->GetClass()->ImplementsInterface(UDWCreatureAmbientPatrolHost::StaticClass()))return;
 const uint64 Generation=DecisionGeneration;
 const TWeakObjectPtr<UDWCreaturePatrolComponent> Self(this);
 const auto Current=[&](){return Self.IsValid()&&IsValid(H)&&!H->IsActorBeingDestroyed()&&GetWorld()==W&&
  !W->bIsTearingDown&&bRequested&&IsActive()&&DecisionGeneration==Generation;};
 const bool Alive=!H->GetClass()->ImplementsInterface(UDWCreatureInterface::StaticClass())||IDWCreatureInterface::Execute_ReadCreatureFacts(H).bAlive;
 if(!Current())return;
 if(!Alive||PatrolMode!=EDWCreaturePatrolMode::BuildingPatrol||!ValidConfiguration())
 {
  ValidatedRoute.Reset(); IDWCreatureAmbientPatrolHost::Execute_StopCreatureAmbientPatrol(H,this);
  if(!Current())return;
  LastDecision=!Alive?TEXT("DeadIdle"):PatrolMode==EDWCreaturePatrolMode::Idle?TEXT("Idle"):TEXT("InvalidConfiguration");
  if(!Alive||PatrolMode==EDWCreaturePatrolMode::Idle)W->GetTimerManager().ClearTimer(DecisionTimer);
  return;
 }
 if(ValidatedRoute.IsValid()&&(!IsRouteCurrent(ValidatedRoute)||!ValidateRoute(ValidatedRoute)))
 {
  ValidatedRoute.Reset(); IDWCreatureAmbientPatrolHost::Execute_StopCreatureAmbientPatrol(H,this);
  if(!Current())return;
  LastDecision=TEXT("RouteInvalidatedIdle"); return;
 }
 const bool Can=IDWCreatureAmbientPatrolHost::Execute_CanCreatureAmbientPatrol(H,this);
 if(!Current())return;
 if(!Can)
 {
  ValidatedRoute.Reset(); IDWCreatureAmbientPatrolHost::Execute_StopCreatureAmbientPatrol(H,this);
  if(Current())LastDecision=TEXT("HostSuspended"); return;
 }
 const auto State=IDWCreatureAmbientPatrolHost::Execute_ReadCreatureAmbientPatrol(H);
 if(!Current()||!State.bNeedsGoal)return;
 FVector Goal;
 if(!ChooseGoal(Goal)){LastDecision=TEXT("NavigationFailedIdle");return;}
 const bool Accepted=IDWCreatureAmbientPatrolHost::Execute_RequestCreatureAmbientGoal(H,this,Goal,WaitSeconds);
 if(Current())LastDecision=Accepted?TEXT("GoalAccepted"):TEXT("HostRejected");
}
