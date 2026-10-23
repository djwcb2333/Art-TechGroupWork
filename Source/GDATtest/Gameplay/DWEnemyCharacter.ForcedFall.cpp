#include "DWEnemyCharacter.h"
#include "DWCombatWorldObjectsPersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Engine/World.h"

bool ADWEnemyCharacter::FindStandableFallPoint(FVector Position,FVector& Out)const
{
    const auto* Movement=GetCharacterMovement();
    if(!Movement||!GetWorld()||GetWorld()->bIsTearingDown||Position.ContainsNaN())return false;
    FFindFloorResult Floor;Movement->FindFloor(Position,Floor,false);
    if(!Floor.IsWalkableFloor()||Floor.GetDistanceToFloor()>UCharacterMovementComponent::MAX_FLOOR_DIST+UE_KINDA_SMALL_NUMBER||
        Floor.HitResult.ImpactPoint.ContainsNaN())return false;
    Out=Floor.HitResult.ImpactPoint;return true;
}

void ADWEnemyCharacter::BeginForcedFallMove()
{
    if(ForcedFall.Saved.Stage==EDWForcedFallStage::None)return;
    FVector Ground=FVector::ZeroVector;const FVector Start=GetActorLocation();
    const bool Supported=FindStandableFallPoint(Start,Ground);
    ForcedFall.BeginMove(Supported,Start,Ground);
}

void ADWEnemyCharacter::CompleteForcedFallMove()
{
    if(!ForcedFall.bMoving)return;
    const FVector Start=ForcedFall.MoveStart,End=GetActorLocation();
    const bool Moved=!Start.Equals(End,UE_KINDA_SMALL_NUMBER);
    FVector Ground=ForcedFall.Saved.LastStandablePoint;
    const bool Supported=FindStandableFallPoint(End,Ground);
    if(Moved&&!Supported&&ForcedFall.bMoveStartedSupported)
    {
        // Collision precision only, not a new combat distance/time. Keep the last supported
        // capsule position before this real swept move crossed the edge, then save its floor contact.
        FVector Low=Start,High=End;
        for(int32 I=0;I<32&&!Low.Equals(High,UE_KINDA_SMALL_NUMBER);++I)
        {
            const FVector Mid=(Low+High)*.5;FVector Contact;
            if(FindStandableFallPoint(Mid,Contact)){Low=Mid;Ground=Contact;}else High=Mid;
        }
    }
    ForcedFall.CompleteMove(Moved,Supported,Ground);
}

void ADWEnemyCharacter::ObserveForcedFallLanding()
{
    if(!ForcedFall.HasCredit()||ForcedFall.bMoving)return;
    FVector Ground;if(FindStandableFallPoint(GetActorLocation(),Ground))ForcedFall.Clear();
}

bool ADWEnemyCharacter::CaptureForcedFallCredit(DWCombatCommit::FEntityLife& Player,FVector& Point)
{
    // A swept movement can synchronously enter a fatal volume before AddActorWorldOffset returns.
    // Resolve the actual current displacement first; a pre-move pending token alone grants nothing.
    CompleteForcedFallMove();
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return false;
    if(!ForcedFall.IsCurrent(A->GetWorldEpoch(),A->CaptureActorLife(this),FormalKnockbackRevision))return false;
    ObserveForcedFallLanding();if(!ForcedFall.HasCredit())return false;
    Player={ForcedFall.Saved.PlayerEntity,ForcedFall.Saved.PlayerLife};Point=ForcedFall.Saved.LastStandablePoint;return true;
}

FDWEnemyForcedFallSave ADWEnemyCharacter::CaptureForcedFallSave()const
{return ForcedFall.Saved;}

bool ADWEnemyCharacter::RestoreForcedFallBeforeBeginPlay(const FDWEnemyForcedFallSave& S,FString& Error)
{
    auto* A=UDWCombatAuthoritySubsystem::Get(this);
    if(!A||HasActorBegunPlay()||GetWorld()->HasBegunPlay()||!FormalCombatOperational()||ForcedFall.bMoving)
    {Error=TEXT("Forced-fall restore requires the coordinated cold-world enemy/movement snapshot.");return false;}
    const uint64 Revision=FormalKnockbackRevision?FormalKnockbackRevision:1;
    FDWEnemyForcedFall Candidate;
    if(!Candidate.Restore(S,FormalEnemyId,A->GetWorldEpoch(),A->CaptureActorLife(this),Revision,bIsCombatKnockedBack))
    {Error=TEXT("Invalid forced-fall provenance or missing armed movement snapshot.");return false;}
    ForcedFall=Candidate;FormalKnockbackRevision=Revision;Error.Reset();return true;
}

void ADWEnemyCharacter::Landed(const FHitResult& Hit)
{
    ForcedFall.Clear(); // Clear before Blueprint/native landing listeners can cause a later unrelated death.
    Super::Landed(Hit);
}
void ADWEnemyCharacter::OnMovementModeChanged(EMovementMode Previous,uint8 PreviousCustom)
{
    const auto* Move=GetCharacterMovement();
    if(!bCombatWorldStaged&&Move&&Previous==MOVE_Falling&&Move->MovementMode!=MOVE_Falling)ForcedFall.Clear();
    Super::OnMovementModeChanged(Previous,PreviousCustom);
}
void ADWEnemyCharacter::EndPlay(EEndPlayReason::Type Reason)
{
    if(Reason==EEndPlayReason::Destroyed)FDWCombatWorldObjectsPersistence::RecordDestroyedEnemy(*this);
    ForcedFall.Clear();Super::EndPlay(Reason); // EndPlay is never an invitation to manufacture a death receipt.
}
void ADWEnemyCharacter::FellOutOfWorld(const UDamageType& DamageType)
{
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))
    {A->SubmitEnemyEnvironmentFatal(*this,EDWEnemyEnvironmentFatal::FellOutOfWorld,DamageType);return;}
    Super::FellOutOfWorld(DamageType); // World shutdown/unregistered worlds do not create a drop record.
}
