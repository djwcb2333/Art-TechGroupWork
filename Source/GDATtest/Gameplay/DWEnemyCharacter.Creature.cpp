#include "DWEnemyCharacter.h"
#include "DWCreatureDropConfiguration.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimationAsset.h"
#include "DWCreaturePatrolComponent.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
bool ADWEnemyCharacter::ApplyCreatureDefinition(FString& Error)
{
 RegisteredAmbientPatrol=FindComponentByClass<UDWCreaturePatrolComponent>();
 FDWCreatureResolvedDrops Drops;if(!ResolveCreatureDrops(Drops,Error))return false;
 if(Drops.Policy==EDWCreatureDropPolicy::ConfiguredItems)
 {
  auto* GI=GetGameInstance<UDWGameInstance>();const auto* C=GI&&GetWorld()?GI->GetConfigForMap(UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())):nullptr;
  if(!C||!DWCreatureDropPolicy::ValidateEntriesForConfig(Drops.Entries,C,Error))return false;
 }
 if(!CreatureDefinition)return true;
 const auto& D=*CreatureDefinition;
 if(D.bExternalAmbientPatrol)AmbientPatrolOwner=RegisteredAmbientPatrol;
 if(D.DropPolicy==EDWCreatureDropPolicy::ConfiguredFormalCore&&
  (!FMath::IsFinite(D.CoreDropProbability)||D.CoreDropProbability<0.f||D.CoreDropProbability>1.f||D.CoreDropCount<1||D.CoreDropCount>100000))
 {Error=TEXT("Invalid configured core drop probability/count");return false;}
 if(D.NumericSource==EDWCreatureNumericSource::DefinitionLegacyHost&&D.DropPolicy!=EDWCreatureDropPolicy::None&&D.DropPolicy!=EDWCreatureDropPolicy::ConfiguredItems)
 {Error=TEXT("Legacy creature supports empty drops; formal core drops require a registered formal profile");return false;}
 if(!FMath::IsFinite(D.AmbientGoalTimeoutSeconds)||D.AmbientGoalTimeoutSeconds<=0.f||D.AmbientGoalTimeoutSeconds>3600.f||
  !FMath::IsFinite(D.AmbientArrivalRadiusCm)||D.AmbientArrivalRadiusCm<=0.f||D.AmbientArrivalRadiusCm>100000.f||
  !FMath::IsFinite(D.AmbientFailureWaitSeconds)||D.AmbientFailureWaitSeconds<=0.f||D.AmbientFailureWaitSeconds>3600.f)
 {Error=TEXT("Invalid ambient patrol timeout/arrival/retry tuning");return false;}
 const bool Formal=D.NumericSource==EDWCreatureNumericSource::ExistingFormalProfile;
 if(Formal?(D.FormalProfile==EDWEnemyCombatProfile::Legacy||(CombatProfile!=EDWEnemyCombatProfile::Legacy&&CombatProfile!=D.FormalProfile)):
   (CombatProfile!=EDWEnemyCombatProfile::Legacy||D.FormalProfile!=EDWEnemyCombatProfile::Legacy))
 {Error=TEXT("Creature numeric source/profile conflict");return false;}
 if(!Formal)
 {
  for(float V:{D.MaxHealth,D.MoveSpeed,D.Damage,D.AttackRange,D.Windup,D.Active,D.Recovery,D.AttackInterval,D.PatrolRadius,D.PatrolWaitSeconds})
   if(!FMath::IsFinite(V)||V<0.f){Error=TEXT("Creature contains invalid numeric tuning");return false;}
  if(D.MaxHealth<1.f||D.AttackRange<1.f||D.Active<=0.f||D.AttackInterval<=0.f){Error=TEXT("Creature health/range/cycle invalid");return false;}
  MaxHealth=D.MaxHealth;MoveSpeed=D.MoveSpeed;AttackDamage=D.Damage;AttackRange=D.AttackRange;
  AttackWindup=D.Windup;AttackActiveDuration=D.Active;AttackRecovery=D.Recovery;AttackInterval=D.AttackInterval;
  PatrolRadius=D.PatrolRadius;PatrolWaitTime=D.PatrolWaitSeconds;
  GetCharacterMovement()->MaxWalkSpeed=MoveSpeed;
 }
 else CombatProfile=D.FormalProfile;
 if(auto* M=D.PlaceholderMesh.LoadSynchronous())PlaceholderBody->SetStaticMesh(M);
 if(!D.IdleAnimation.IsNull())IdleAnimation=D.IdleAnimation.LoadSynchronous();
 if(!D.MoveAnimation.IsNull())MoveAnimation=D.MoveAnimation.LoadSynchronous();
 if(!D.AlertAnimation.IsNull())AlertAnimation=D.AlertAnimation.LoadSynchronous();
 // Formal initialization deliberately validates authored attack slots before gameplay.
 if(!D.AttackAnimation.IsNull())AttackAnimation=D.AttackAnimation.LoadSynchronous();
 if(!D.DeathAnimation.IsNull())DeathAnimation=D.DeathAnimation.LoadSynchronous();
 Error.Reset();return true;
}
FDWCreatureSnapshot ADWEnemyCharacter::GetCreatureSnapshot() const
{
 FDWCreatureSnapshot S;S.Health=Health;S.MaxHealth=MaxHealth;S.bAlive=IsAlive();
 S.EffectiveMoveSpeed=GetCharacterMovement()?GetCharacterMovement()->MaxWalkSpeed:0.f;
 S.State=static_cast<uint8>(State);S.AttackDamage=AttackDamage;S.AttackRange=AttackRange;S.WindupSeconds=AttackWindup;
 S.ActiveSeconds=AttackActiveDuration;S.RecoverySeconds=AttackRecovery;S.AttackIntervalSeconds=AttackInterval;
 S.Definition=CreatureDefinition;
 if(CreatureDefinition){S.Disposition=CreatureDefinition->Disposition;S.DropPolicy=CreatureDefinition->DropPolicy;}return S;
}
bool ADWEnemyCharacter::CanUseAmbientPatrol(UActorComponent* R) const
{
 return IsValid(R)&&R->IsA<UDWCreaturePatrolComponent>()&&R->IsRegistered()&&R->IsActive()&&
  RegisteredAmbientPatrol==R&&Cast<UDWCreaturePatrolComponent>(R)->PatrolMode==EDWCreaturePatrolMode::BuildingPatrol&&R->GetOwner()==this&&(!AmbientPatrolOwner.IsValid()||AmbientPatrolOwner==R)&&
  CreatureDefinition&&CreatureDefinition->bExternalAmbientPatrol&&bCreatureDefinitionValid&&IsAlive()&&
  !bCombatWorldStaged&&!bCombatRestoreInProgress&&!IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown&&
  !GetWorld()->IsPaused()&&!UDWWorldEventSubsystem::IsPlaying(this)&&!bIsCombatKnockedBack&&
  AttackPhase==EDWCombatPhase::None&&!TargetPawn.IsValid()&&!FormalMemory.bEngaged&&!FormalMemory.bReturningHome&&!bPlayerDeathReturn;
}
FDWAmbientPatrolState ADWEnemyCharacter::GetAmbientPatrolState() const
{
 FDWAmbientPatrolState S;S.bHasGoal=bHasPatrolGoal;S.Goal=PatrolGoal;
 S.RemainingWaitSeconds=GetWorld()?FMath::Max(0.f,NextPatrolTime-float(GetWorld()->GetTimeSeconds())):0.f;
 S.bNeedsGoal=CanUseAmbientPatrol(RegisteredAmbientPatrol.Get())&&!S.bHasGoal&&S.RemainingWaitSeconds<=0.f;return S;
}
bool ADWEnemyCharacter::TryRequestAmbientPatrolGoal(UActorComponent* R,FVector G,float Wait)
{
 if(!CanUseAmbientPatrol(R)||G.ContainsNaN()||!FMath::IsFinite(Wait)||Wait<0.f||Wait>3600.f||!GetAmbientPatrolState().bNeedsGoal)return false;
 auto* Patrol=Cast<UDWCreaturePatrolComponent>(R);
 auto* AI=Cast<AAIController>(GetController());
 if(!Patrol||!AI||!AI->GetPathFollowingComponent())return false;
 const auto* Follow=AI->GetPathFollowingComponent();
 if(Follow->GetStatus()!=EPathFollowingStatus::Idle&&Follow->GetCurrentRequestId()!=AmbientMoveRequest)return false;
 auto Path=Patrol->AcquireValidatedRoute(G);
 if(!Path.IsValid()||!CanUseAmbientPatrol(R))return false;
 FAIMoveRequest Move(G); Move.SetAllowPartialPath(false); Move.SetProjectGoalLocation(false);
 Move.SetAcceptanceRadius(FMath::Min(NavigationAcceptanceRadius,CreatureDefinition->AmbientArrivalRadiusCm));
 Move.SetReachTestIncludesAgentRadius(false); Move.SetReachTestIncludesGoalRadius(false);
 const FAIRequestID Request=AI->RequestMove(Move,Path);
 if(!Request.IsValid())return false;
 // RequestMove may call completion/delegates; recheck ownership before publishing the goal.
 if(!IsValid(this)||!IsValid(Patrol)||!IsValid(AI)||!CanUseAmbientPatrol(R)||!Patrol->IsRouteCurrent(Path)||AI!=GetController()||
  AI->GetPathFollowingComponent()->GetCurrentRequestId()!=Request)
 {
  if(IsValid(AI)&&AI==GetController()&&AI->GetPathFollowingComponent()->GetCurrentRequestId()==Request)AI->StopMovement();
  return false;
 }
 AmbientValidatedPath=Path; AmbientMoveRequest=Request; AmbientMoveController=AI;
 AmbientPatrolOwner=R;PatrolGoal=G;bHasPatrolGoal=true;AmbientWaitSeconds=Wait;
 AmbientGoalDeadline=GetWorld()->GetTimeSeconds()+CreatureDefinition->AmbientGoalTimeoutSeconds;return true;
}
void ADWEnemyCharacter::StopOwnedAmbientMovement()
{
 auto* AI=AmbientMoveController.Get();
 if(AI&&AI->GetPawn()==this&&AI->GetPathFollowingComponent()&&AmbientMoveRequest.IsValid()&&
  AI->GetPathFollowingComponent()->GetCurrentRequestId()==AmbientMoveRequest)AI->StopMovement();
 AmbientMoveRequest=FAIRequestID(); AmbientValidatedPath.Reset(); AmbientMoveController.Reset();
}
void ADWEnemyCharacter::StopAmbientPatrol(UActorComponent* R)
{
 // Cleanup follows the old request identity, never an arbitrary newer combat MoveTo.
 if(!IsValid(R)||R->GetOwner()!=this||RegisteredAmbientPatrol!=R||!R->IsA<UDWCreaturePatrolComponent>())return;
 if(AmbientPatrolOwner.IsValid()&&AmbientPatrolOwner!=R)return;
 StopOwnedAmbientMovement();
 AmbientPatrolOwner.Reset();bHasPatrolGoal=false;AmbientGoalDeadline=0.;
 NextPatrolTime=GetWorld()?GetWorld()->GetTimeSeconds():0.f;
}
bool ADWEnemyCharacter::TickAmbientPatrol(float Dt)
{
 if(!CreatureDefinition||!CreatureDefinition->bExternalAmbientPatrol)return false;
 auto* Patrol=Cast<UDWCreaturePatrolComponent>(RegisteredAmbientPatrol.Get());
 if(Patrol)Patrol->MaintainPatrolSchedule();
 SetAIState(EDWEnemyState::Patrol);
 if(!Patrol||!CanUseAmbientPatrol(Patrol))
 {StopOwnedAmbientMovement();bHasPatrolGoal=false;AmbientGoalDeadline=0.;return true;}
 if(bHasPatrolGoal)
 {
  const double Now=GetWorld()->GetTimeSeconds();
  const bool Arrived=FVector::Dist2D(GetActorLocation(),PatrolGoal)<CreatureDefinition->AmbientArrivalRadiusCm;
  const auto* AI=Cast<AAIController>(GetController());
  const auto* Follow=AI?AI->GetPathFollowingComponent():nullptr;
  const bool ValidMove=Patrol->IsRouteCurrent(AmbientValidatedPath)&&Follow&&AI==AmbientMoveController.Get()&&AmbientMoveRequest.IsValid()&&
   Follow->GetCurrentRequestId()==AmbientMoveRequest&&Follow->GetStatus()==EPathFollowingStatus::Moving;
  // Restored legacy goals have no live validated path/request: discard, then reacquire normally.
  if(Arrived||!ValidMove||AmbientGoalDeadline<=0.||Now>=AmbientGoalDeadline)
  {
   StopOwnedAmbientMovement();bHasPatrolGoal=false;AmbientGoalDeadline=0.;
   NextPatrolTime=Now+(Arrived?AmbientWaitSeconds:FMath::Max(CreatureDefinition->AmbientFailureWaitSeconds,AmbientWaitSeconds));
  }
 }
 return true;
}

bool DWCreaturePersistencePolicy::ValidateSource(const UDWCreatureDefinition* Instance,const UDWCreatureDefinition* ClassDefault,
 EDWEnemyCombatProfile Profile,float SavedMax,float AppliedMax,float AppliedMove,FString& Error)
{
 if(Instance!=ClassDefault)
 {Error=TEXT("CreatureDefinition must match the actor class default. Instance overrides cannot be saved or restored without definition identity data.");return false;}
 if(!ClassDefault||ClassDefault->NumericSource!=EDWCreatureNumericSource::DefinitionLegacyHost){Error.Reset();return true;}
 if(Profile!=EDWEnemyCombatProfile::Legacy||!FMath::IsFinite(ClassDefault->MaxHealth)||ClassDefault->MaxHealth<1.f||
  !FMath::IsFinite(ClassDefault->MoveSpeed)||ClassDefault->MoveSpeed<0.f||SavedMax!=ClassDefault->MaxHealth)
 {Error=TEXT("Saved creature MaxHealth differs from the current class DefinitionLegacyHost projection. Configuration mismatch; saved data was not imported.");return false;}
 if(AppliedMax!=ClassDefault->MaxHealth||AppliedMove!=ClassDefault->MoveSpeed)
 {Error=TEXT("Live creature health/speed projection differs from its class definition. Mixed numeric sources cannot be saved or restored.");return false;}
 Error.Reset();return true;
}
