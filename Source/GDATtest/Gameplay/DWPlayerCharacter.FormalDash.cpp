#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWCombatDashTransaction.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWGameInstance.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWAudioLibrary.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include <limits>

bool ADWPlayerCharacter::TryStartFormalDash(const FDWCombatInputPress& Press,FVector Move,FVector Aim)
{
    const auto& NewContext=Press.Edge.Context;const auto& OldContext=LastFormalDashEdge.Context;
    const bool LaterContext=!OldContext.IsValid()||
        (NewContext.WorldEpoch!=OldContext.WorldEpoch?NewContext.WorldEpoch>OldContext.WorldEpoch:
         NewContext.LifeEpoch!=OldContext.LifeEpoch?NewContext.LifeEpoch>OldContext.LifeEpoch:
         NewContext.BindingGeneration>OldContext.BindingGeneration);
    if(!Press.Edge.Context.IsValid()||!Press.Edge.bDown||Press.Edge.bDuringSuspension||Press.Action!=TEXT("Dash")||
        !Press.Edge.Sequence||LastFormalDashRequestOrdinal==MAX_uint64||
        (!LaterContext&&(NewContext!=OldContext||Press.Edge.Sequence<=LastFormalDashEdge.Sequence)))return false;
    LastFormalDashEdge=Press.Edge;++LastFormalDashRequestOrdinal; // Failed physical requests never queue themselves.
    if(WeaponAnimationComponent&&!WeaponAnimationComponent->AllowsFormalSwordDashInterruption())return false;
    if(!IsCombatActionAllowed()||bGameplayActionInProgress||bAdvancingGameplayClocks||CombatKnockback.bActive||
        GetCombatBaseForm()!=EDWCombatBaseForm::Yeast||Move.ContainsNaN()||Aim.ContainsNaN())return false;
    auto* GI=GetGameInstance<UDWGameInstance>();auto* A=UDWCombatAuthoritySubsystem::Get(this);FString Error;FDWYeastDashConfig Config;
    if(!A||!GI||!GI->GetCombatRules()||!FDWYeastDashConfig::Build(*GI->GetCombatRules(),Config,Error))return false;
    auto Candidate=FormalDash;
    if(!Candidate.Start(Config,GetCombatSpecial(),GetCombatFormRemaining(),FVector2D(Move.X,Move.Y),FVector2D(Aim.X,Aim.Y),LastFormalDashRequestOrdinal))return false;
    const auto Life=A->CaptureActorLife(this);const uint64 Epoch=A->GetWorldEpoch();const TWeakObjectPtr<UWorld> World(GetWorld());
    const auto Current=[&]()
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
        if(!IsValid(this)||IsActorBeingDestroyed()||!World.IsValid()||GetWorld()!=World.Get()||World->bIsTearingDown||
            !Authority||Authority->GetWorldEpoch()!=Epoch||!IsCombatActionAllowed()||GetCombatBaseForm()!=EDWCombatBaseForm::Yeast)return false;
        const auto Now=Authority->CaptureActorLife(this);return Now.Entity==Life.Entity&&Now.Life==Life.Life;
    };
    TGuardValue<bool> Guard(bGameplayActionInProgress,true);
    if(!CanDash()||!Current())return false;
    // Only a qualified dash interrupts a weapon. Completed ammunition/projectiles remain committed.
    CancelCombatActions();if(!Current())return false;
    if(!FormalDash.CanStart(Config,GetCombatSpecial(),GetCombatFormRemaining())||CombatKnockback.bActive)return false;
    return FDWCombatDashTransaction::Start(*this,Press,Candidate);
}
void ADWPlayerCharacter::PublishFormalDashStartEffects()
{
    if(!IsFormalDashCurrent()||!IsCombatActionAllowed()||LastPublishedFormalDashSerial==FormalDash.Serial())return;
    const uint64 Serial=FormalDash.Serial();
    LastPublishedFormalDashSerial=Serial; // Reentrant presentation cannot replay this startup.
    const auto Current=[&](){return IsFormalDashCurrent()&&FormalDash.Serial()==Serial&&IsCombatActionAllowed();};
    if(GetCharacterMovement()){ConsumeMovementInputVector();GetCharacterMovement()->StopMovementImmediately();}
    if(!Current())return;
    SetActorRotation(DashCombatDirection.Rotation());if(!Current())return;
    SetHarvestHeld(false);if(!Current())return;
    StopMovementAudio();if(!Current())return;
    TrailElapsed=DashTrailInterval;
    if(CanPlayPlayerVFX())
    {
        PlayPlayerBurstVFX(DashStartComponent,DashStartNiagara,DashStartOffset,DashStartRotation,DashStartScale);
        if(!Current())return;
        DashStartVFXStopTime=GetWorld()->GetTimeSeconds()+FMath::Max(.01f,DashStartVFXMaxSeconds);
        DashStartVFXCleanupTime=DashStartVFXStopTime+FMath::Max(0.f,DashStartVFXTailSeconds);
    }
    UDWAudioLibrary::PlayWorldSound(this,DashStartSound,GetActorLocation(),DashStartSoundVolume,DashStartSoundPitch);
    if(!Current())return;
    // Source art is retained; no roll source, mesh somersault or animation retiming is installed.
    OnDashStarted();
}
bool ADWPlayerCharacter::ApplyFormalDashRelease(const FDWCombatInputPress& Press)
{
    if(!IsFormalDashing()||Press.Action!=TEXT("Dash")||Press.Edge.Context!=DashInputOrigin.Edge.Context||
        Press.Edge.Sequence!=DashInputOrigin.Edge.Sequence)return false;
    return FormalDash.Release(FormalDash.OriginPress());
}
void ADWPlayerCharacter::CancelFormalDashInput(const FDWCombatInputPress& Press)
{
    if(Press.Edge.Context==DashInputOrigin.Edge.Context&&Press.Edge.Sequence==DashInputOrigin.Edge.Sequence)
        EndFormalDashFacts(EDWYeastDashEnd::Cancelled);
}
bool ADWPlayerCharacter::IsFormalDashCurrent()const
{
    if(!FormalDash.IsActive()||!IsValid(this)||IsActorBeingDestroyed()||!FormalDashWorld.IsValid()||GetWorld()!=FormalDashWorld.Get()||
        FormalDashWorld->bIsTearingDown||IsDead()||bRestoringSave)return false;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A||A->GetWorldEpoch()!=FormalDashEpoch||!FDWCombatDashTransaction::IsCurrent(*this))return false;
    const auto L=A->CaptureActorLife(const_cast<ADWPlayerCharacter*>(this));return L.Entity==FormalDashLife.Entity&&L.Life==FormalDashLife.Life;
}
void ADWPlayerCharacter::EndFormalDashFacts(EDWYeastDashEnd Reason)
{
    if(!FormalDash.End(Reason))return;
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(DashCommitAction);
    DashCommitAction={};bDashCombatActive=false;DashHitActors.Reset();PaidFormalDashSlice={};
    // No callbacks/mesh changes: safe during native damage facts and life invalidation.
}
double ADWPlayerCharacter::GetFormalDashBoundary(double Maximum,bool& bWall)const
{
    bWall=false;const double Boundary=FormalDash.NextBoundary(GetCombatSpecial(),GetCombatFormRemaining());
    if(!IsFormalDashing()||Boundary<=0.||Maximum<=0.)return Boundary;
    const double Step=FMath::Min(Maximum,Boundary);auto* Capsule=GetCapsuleComponent();
    if(!Capsule||!GetWorld())return 0.;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWFormalDashObstacle),false,this);
    Params.AddIgnoredActors(Capsule->GetMoveIgnoreActors());
    // Only actual enemies are pass-through. Walls, nests and other solid obstacles keep their response.
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)Params.AddIgnoredActor(*It);
    FHitResult Hit;
    const FVector End=GetActorLocation()+DashCombatDirection*(FormalDash.GetConfig().SpeedCmPerSecond*Step);
    const FCollisionShape Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight());
    if(GetWorld()->SweepSingleByChannel(Hit,GetActorLocation(),End,Capsule->GetComponentQuat(),Capsule->GetCollisionObjectType(),
        Shape,Params,FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))&&Hit.bBlockingHit)
    {bWall=true;return Step*FMath::Clamp(static_cast<double>(Hit.Time),0.,1.);}
    return Boundary;
}
void ADWPlayerCharacter::QueueFormalDashSlice(const FDWYeastDashStep& Step,bool bWall)
{
    PaidFormalDashSlice.bPending=true;PaidFormalDashSlice.Serial=FormalDash.Serial();PaidFormalDashSlice.Start=GetActorLocation();
    PaidFormalDashSlice.DistanceCm=Step.DistanceCm;PaidFormalDashSlice.End=bWall?EDWYeastDashEnd::Wall:Step.End;
}
bool ADWPlayerCharacter::PumpFormalDashSlice()
{
    if(!PaidFormalDashSlice.bPending)return true;
    if(!IsFormalDashCurrent()||PaidFormalDashSlice.Serial!=FormalDash.Serial())
    {PaidFormalDashSlice={};return true;}
    if(!IsCombatActionAllowed())return false;
    auto Paid=PaidFormalDashSlice;PaidFormalDashSlice={};
    if(!GetActorLocation().Equals(Paid.bMoved?Paid.Finish:Paid.Start,UE_DOUBLE_SMALL_NUMBER))
    {EndFormalDashFacts(EDWYeastDashEnd::Cancelled);return true;}
    auto* Capsule=GetCapsuleComponent();if(!Capsule){EndFormalDashFacts(EDWYeastDashEnd::Cancelled);return true;}
    TArray<TWeakObjectPtr<AActor>> AddedIgnores;
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
        if(!Capsule->GetMoveIgnoreActors().Contains(*It)){Capsule->IgnoreActorWhenMoving(*It,true);AddedIgnores.Add(*It);}
    const TWeakObjectPtr<UCapsuleComponent> WeakCapsule(Capsule);
    ON_SCOPE_EXIT {if(WeakCapsule.IsValid())for(const auto& Actor:AddedIgnores)if(Actor.IsValid())WeakCapsule->IgnoreActorWhenMoving(Actor.Get(),false);};
    if(!Paid.bMoved)
    {
        FHitResult Hit;
        {TGuardValue<bool> Moving(bApplyingFormalDashMotion,true);AddActorWorldOffset(DashCombatDirection*Paid.DistanceCm,true,&Hit,ETeleportType::None);}
        if(!IsFormalDashCurrent()||FormalDash.Serial()!=Paid.Serial)return IsCombatActionAllowed();
        Paid.bMoved=true;Paid.Finish=GetActorLocation();
        if(Hit.bBlockingHit)Paid.End=EDWYeastDashEnd::Wall;
    }
    LastDashCombatLocation=Paid.Finish;
    if(!IsCombatActionAllowed()){PaidFormalDashSlice=Paid;return false;}
    // Fatal resource damage has already invalidated this action; form expiration cancels its endpoint hit.
    if(Paid.End!=EDWYeastDashEnd::FormExpired&&IsCombatActionAllowed())SweepDashCombat(Paid.Start,LastDashCombatLocation);
    if(!IsFormalDashCurrent()||FormalDash.Serial()!=Paid.Serial)return IsCombatActionAllowed();
    if(!IsCombatActionAllowed()){PaidFormalDashSlice=Paid;return false;}
    if(Paid.End!=EDWYeastDashEnd::None)EndFormalDashFacts(Paid.End);
    return IsValid(this)&&!IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown&&!IsDead()&&!bRestoringSave;
}
