#include "DWEnemyCharacter.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"

void ADWEnemyCharacter::BeginPlayerDeathReturnFacts()
{
    if(!IsAlive()||IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)return;
    bPlayerDeathReturn=true;
    // Abort only this outstanding strike. Keep health, home, engagement, drop and attack lock.
    if(FormalMeleeAction.Id)if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(FormalMeleeAction);
    FormalMeleeAction={};MeleeTarget.Reset();bFormalFacingLocked=false;bAttackResolved=true;
    AttackPhase=EDWCombatPhase::None;AttackPhaseEndTime=0.;bHasAttackSnapshot=false;
}
bool ADWEnemyCharacter::TickPlayerDeathReturn(float Dt)
{
    if(!bPlayerDeathReturn)return false;
    if(!IsAlive()||IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)return true;
    if(GetWorld()->IsPaused()||UDWWorldEventSubsystem::IsPlaying(this)){StopAIMovement();return true;}
    const double Acceptance=FMath::IsFinite(NavigationAcceptanceRadius)?FMath::Max(0.f,NavigationAcceptanceRadius):0.;
    const double Radius=FMath::Max(0.f,GetNavAgentPropertiesRef().AgentRadius);
    if(FVector::Dist2D(GetActorLocation(),HomeLocation)<=Acceptance+Radius)
    {
        bPlayerDeathReturn=false;StopAIMovement();return true;
    }
    SetAIState(EDWEnemyState::ReturningHome);
    if(IsValid(this)&&!IsActorBeingDestroyed()&&IsAlive()&&GetWorld()&&!GetWorld()->bIsTearingDown&&!GetWorld()->IsPaused())MoveToward(HomeLocation,Dt);
    return true;
}
