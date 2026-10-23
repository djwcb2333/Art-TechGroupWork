#include "DWEnemyCharacter.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatRules.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWDamageVolumeLibrary.h"
#include "DWWorldEvent.h"
#include "DWAudioLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

bool ADWEnemyCharacter::InitializeFormalCombat(const FDWCombatRules& Rules,FString& Error)
{
    if(!IsInGameThread()||IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown||
        CombatProfile==EDWEnemyCombatProfile::Legacy||bFormalCombatReady||AttackPhase!=EDWCombatPhase::None)
    {Error=TEXT("Formal combat initialization requires an uninitialized opt-in enemy.");return false;}
    // These are old authored attacks. Never clear them or silently remap their timings.
    if(!AttackVariants.IsEmpty()||AttackAnimation)
    {Error=TEXT("New profiles require empty attack slots until new enemy art/timing is coordinated; existing art was preserved.");return false;}
    FDWEnemyCombatConfig Candidate;FDWEnemyCoreDropConfig Drops;
    if(!DWEnemyFormal::BuildConfig(Rules,CombatProfile,Candidate,Error)||!DWEnemyFormal::BuildCoreDropConfig(Rules,Drops,Error))return false;
    if(bHealthRestoredFromSave&&(!FMath::IsFinite(Health)||Health<0.f||Health>Candidate.Health))
    {Error=TEXT("Restored health is outside this formal profile; initialization changed no state.");return false;}
    FormalCombatConfig=Candidate;FormalCoreDropConfig=Drops;bFormalCombatReady=true;
    MaxHealth=Candidate.Health;if(!bHealthRestoredFromSave)Health=MaxHealth;
    DetectionRadius=Candidate.AlertRadiusCm;LoseTargetRadius=Candidate.DisengageDistanceCm;MoveSpeed=Candidate.MoveSpeedCmPerSecond;
    AlertDuration=Candidate.AlertDelaySeconds;AttackRange=Candidate.RangeCm;AttackDamage=Candidate.Damage;
    AttackWindup=Candidate.WindupSeconds;AttackActiveDuration=Candidate.ActiveSeconds;AttackRecovery=Candidate.RecoverySeconds;
    AttackInterval=Candidate.AttackCycleSeconds();AttackHalfAngle=Candidate.AttackFullAngleDegrees*.5;
    if(!bFormalHomeCaptured){HomeLocation=GetActorLocation();bFormalHomeCaptured=true;}
    if(!FormalEnemyId.IsValid())FormalEnemyId=FGuid::NewGuid();
    if(PersistentId.IsNone())
    {PersistentId=FName(*(TEXT("DWFormal_")+FormalEnemyId.ToString(EGuidFormats::Digits)));bFormalGeneratedPersistentId=true;}
    // All identity/config facts are ready first. This query sphere does not need overlap notifications.
    DetectionRange->SetSphereRadius(DetectionRadius,false);
    if(!IsValid(this)||IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)
    {Error=TEXT("Enemy lifecycle ended while updating the query sphere.");return false;}
    GetCharacterMovement()->MaxWalkSpeed=MoveSpeed;
    Error.Reset();return true;
}

bool ADWEnemyCharacter::FormalCombatOperational() const
{
    return !bCombatWorldStaged&&bFormalCombatReady&&CombatProfile==FormalCombatConfig.Profile&&CombatProfile!=EDWEnemyCombatProfile::Legacy&&
        IsValid(this)&&!IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown&&IsAlive();
}

bool ADWEnemyCharacter::IsHostileToCombatPlayer(const ADWPlayerCharacter* Player) const
{
    if(!IsValid(Player)||Player->IsActorBeingDestroyed()||Player->IsDead())return false;
    if(CombatProfile==EDWEnemyCombatProfile::Legacy)return true;
    return FormalCombatOperational()&&Player->HasCombatVitalsReady()&&
        DWEnemyFormal::IsHostile(CombatProfile,Player->GetCombatBaseForm(),FormalMemory.bEngaged);
}

bool ADWEnemyCharacter::CanResolveFormalMeleeHit(AActor* Target,bool bFacing) const
{
    const auto* Player=Cast<ADWPlayerCharacter>(Target);
    if(!FormalCombatOperational()||!IsHostileToCombatPlayer(Player)||!IsCombatTargetAlive(Target)||!HasSightTo(Target))return false;
    if(bIsCombatKnockedBack&&!bFormalAttackContinuesDuringKnockback)return false;
    const FVector Origin=GetActorLocation();
    const FVector Facing=bFormalFacingLocked?LockedAttackDirection:GetActorForwardVector().GetSafeNormal2D();
    TInlineComponentArray<UPrimitiveComponent*> Components(Target);
    for(UPrimitiveComponent* Component:Components)
    {
        if(!UDWDamageVolumeLibrary::IsDamageHitComponent(Component))continue;
        // Current actor contracts use explicit UBoxComponent hurtboxes. Unsupported shapes fail closed.
        const auto* Box=Cast<UBoxComponent>(Component);if(!Box)continue;
        if(DWEnemyFormal::BoxIntersectsSector(Box->GetComponentTransform(),Box->GetUnscaledBoxExtent(),Origin,Facing,
            FormalCombatConfig.RangeCm,FormalCombatConfig.AttackHeightCm,bFacing?FormalCombatConfig.AttackFullAngleDegrees:360.))return true;
    }
    return false;
}

void ADWEnemyCharacter::TickFormalCombat(float Dt)
{
    if(!FormalCombatOperational()){StopAIMovement();return;}
    if(GetWorld()->IsPaused()){StopAIMovement();return;} // Game time and the accepted attack freeze, not cancel.
    if(UDWWorldEventSubsystem::IsPlaying(this)){CancelMeleeAttack();StopAIMovement();return;}
    const double Now=GetWorld()->GetTimeSeconds();
    auto* Player=Cast<ADWPlayerCharacter>(TargetPawn.Get());
    if(!IsValid(Player))Player=Cast<ADWPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
    if(IsValid(Player)&&Player->HasCombatVitalsReady()&&!Player->IsDead()&&!Player->IsActorBeingDestroyed())
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
        const auto Life=Authority?Authority->CaptureActorLife(Player):DWCombatCommit::FEntityLife{};
        const uint64 Revision=Player->GetCombatDamageRevision();
        const bool SameLife=Life.Entity==FormalObservedPlayerLife.Entity&&Life.Life==FormalObservedPlayerLife.Life;
        const bool Damaged=SameLife&&Revision!=FormalObservedPlayerDamageRevision;
        FormalObservedPlayerLife=Life;FormalObservedPlayerDamageRevision=Revision;
        const bool Sight=HasSightTo(Player);const double Distance=FVector::Dist2D(GetActorLocation(),Player->GetActorLocation());
        if(FormalMemory.bEngaged&&FormalMemory.ObserveDisengage(Now,Distance,Sight,Damaged,FormalCombatConfig))
        {CancelMeleeAttack();TargetPawn.Reset();bFormalNeedsChaseState=false;}
        if(!FormalMemory.bEngaged&&!HasDaggerDiscoveryGrace(Player)&&IsHostileToCombatPlayer(Player)&&Sight&&Distance<=FormalCombatConfig.AlertRadiusCm)
        {
            FormalMemory.Engage();TargetPawn=Player;FormalAlertEndTime=Now+FormalCombatConfig.AlertDelaySeconds;
            AlertEndTime=FormalAlertEndTime;StopAIMovement();SetAIState(EDWEnemyState::Alert);
        }
    }
    else if(FormalMemory.bEngaged)
    {
        // S09: death begins returning, not instant disengagement. Keep normal E05 observation rules.
        if(IsValid(Player)&&Player->IsDead())
        {
            if(!bPlayerDeathReturn)BeginPlayerDeathReturnFacts();
            FormalMemory.ObserveDisengage(Now,FVector::Dist2D(GetActorLocation(),Player->GetActorLocation()),HasSightTo(Player),false,FormalCombatConfig);
        }
    }
    if(!FormalCombatOperational())return;
    ObserveForcedFallLanding();
    UpdateSlowEffects();
    float StrongestSlow=static_cast<float>(GetCombatGroundAreaSlowFraction()*100.);for(const FVector2D& Effect:SlowEffects)StrongestSlow=FMath::Max(StrongestSlow,static_cast<float>(Effect.X));
    GetCharacterMovement()->MaxWalkSpeed=FormalCombatConfig.MoveSpeedCmPerSecond*(1.-StrongestSlow/100.)*
        ((FormalMemory.bReturningHome||bPlayerDeathReturn)?FormalCombatConfig.ReturnSpeedMultiplier:1.)*GetGatheringEnvironmentMovementMultiplier();
    if(bIsCombatKnockedBack)
    {
        if(!UpdateFormalKnockback(Dt))return;
        if(AttackPhase!=EDWCombatPhase::None)UpdateFormalMeleeAttack(Now);
        DrawCombatDebug();return;
    }
    bFormalAttackContinuesDuringKnockback=false;
    if(TickPlayerDeathReturn(Dt))return;
    if(AttackPhase!=EDWCombatPhase::None){StopAIMovement();UpdateFormalMeleeAttack(Now);DrawCombatDebug();return;}
    if(FormalMemory.bEngaged&&IsValid(Player)&&Player->HasCombatVitalsReady()&&!Player->IsDead())
    {
        TargetPawn=Player;
        if(bFormalNeedsChaseState){bFormalNeedsChaseState=false;SetAIState(EDWEnemyState::Chase);}
        if(!FormalCombatOperational())return;
        if(Now<FormalAlertEndTime)
        {StopAIMovement();const FVector To=Player->GetActorLocation()-GetActorLocation();if(!To.IsNearlyZero())SetActorRotation(To.Rotation());DrawCombatDebug();return;}
        SetAIState(EDWEnemyState::Chase);if(!FormalCombatOperational())return;
        if(CanResolveFormalMeleeHit(Player,false))
        {StopAIMovement();if(Now>=FormalNextAttackTime)TryStartFormalMeleeAttack(Player);}
        else MoveToward(Player->GetActorLocation(),Dt);
        DrawCombatDebug();return;
    }
    if(FormalMemory.bReturningHome)
    {
        SetAIState(EDWEnemyState::ReturningHome);if(!FormalCombatOperational())return;
        const double Acceptance=FMath::IsFinite(NavigationAcceptanceRadius)?FMath::Clamp(NavigationAcceptanceRadius,0.f,100000.f):35.f;
        const double AgentRadius=FMath::Max(0.f,GetNavAgentPropertiesRef().AgentRadius);
        if(FVector::Dist2D(GetActorLocation(),HomeLocation)<=Acceptance+AgentRadius)
        {StopAIMovement();FormalMemory.bReturningHome=false;SetAIState(EDWEnemyState::Patrol);}
        else MoveToward(HomeLocation,Dt);
        return;
    }
    // New profiles wait at their birth point when not engaged. Legacy patrol settings are not new ecology rules.
    StopAIMovement();SetAIState(EDWEnemyState::Patrol);
}

bool ADWEnemyCharacter::TryStartFormalMeleeAttack(AActor* Target)
{
    if(!FormalCombatOperational()||bPlayerDeathReturn||bStartingMeleeAttack||bDeathTransitionInProgress||bCombatRestoreInProgress||
        GetWorld()->IsPaused()||UDWWorldEventSubsystem::IsPlaying(this)||bIsCombatKnockedBack||
        AttackPhase!=EDWCombatPhase::None||GetWorld()->GetTimeSeconds()<FormalNextAttackTime||!CanResolveFormalMeleeHit(Target,false))return false;
    TGuardValue<bool> StartGuard(bStartingMeleeAttack,true);
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return false;
    const auto Action=Authority->BeginAction(this);if(!Action.Id)return false;
    FormalMeleeAction=Action;FormalMeleeTargetLife=Authority->CaptureActorLife(Target);
    if(!FormalMeleeTargetLife.Entity){Authority->EndAction(Action);FormalMeleeAction={};return false;}
    FormalMemory.Engage();TargetPawn=Cast<APawn>(Target);MeleeTarget=Target;
    FormalAttackStartedTime=GetWorld()->GetTimeSeconds();FormalPhaseEndTime=FormalAttackStartedTime+FormalCombatConfig.WindupSeconds;
    FormalNextAttackTime=FormalAttackStartedTime+FormalCombatConfig.AttackCycleSeconds();
    CurrentAttackWindup=FormalCombatConfig.WindupSeconds;CurrentAttackActiveDuration=FormalCombatConfig.ActiveSeconds;
    CurrentAttackRecovery=FormalCombatConfig.RecoverySeconds;CurrentAttackRange=FormalCombatConfig.RangeCm;CurrentAttackDamage=FormalCombatConfig.Damage;
    CurrentAttackAnimation=nullptr;CurrentAttackVariantIndex=INDEX_NONE;bHasAttackSnapshot=true;
    AttackStartedTime=FormalAttackStartedTime;AttackPhaseEndTime=FormalPhaseEndTime;NextAttackTime=FormalNextAttackTime;
    AttackPhase=EDWCombatPhase::Windup;bAttackResolved=false;bFormalFacingLocked=FormalCombatConfig.WindupLockFraction<=0.;
    AttackSerial=AttackSerial==MAX_int32?1:AttackSerial+1;const int32 Serial=AttackSerial;
    const FVector To=(Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    if(!To.IsNearlyZero())SetActorRotation(FRotator(0.,To.Rotation().Yaw,0.));
    LockedAttackDirection=GetActorForwardVector().GetSafeNormal2D();StopAIMovement();
    if(!FormalCombatOperational()||AttackSerial!=Serial||AttackPhase!=EDWCombatPhase::Windup)return false;
    SetAIState(EDWEnemyState::Attack);
    if(FormalCombatOperational()&&AttackSerial==Serial&&AttackPhase==EDWCombatPhase::Windup)OnAttackStarted(Target);
    return FormalCombatOperational()&&AttackSerial==Serial&&AttackPhase!=EDWCombatPhase::None;
}

void ADWEnemyCharacter::UpdateFormalMeleeAttack(double Now)
{
    TGuardValue<bool> UpdateGuard(bStartingMeleeAttack,true);
    if(!FormalCombatOperational()){CancelMeleeAttack();return;}
    if(AttackPhase==EDWCombatPhase::Recovery)
    {if(Now>=FormalPhaseEndTime)CancelMeleeAttack();return;}
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
    const auto Life=Authority?Authority->CaptureActorLife(MeleeTarget.Get()):DWCombatCommit::FEntityLife{};
    if(!IsCombatTargetAlive(MeleeTarget.Get())||Life.Entity!=FormalMeleeTargetLife.Entity||Life.Life!=FormalMeleeTargetLife.Life)
    {CancelMeleeAttack();return;}
    if(AttackPhase==EDWCombatPhase::Windup)
    {
        const double LockAt=FormalAttackStartedTime+FormalCombatConfig.WindupSeconds*FormalCombatConfig.WindupLockFraction;
        // Once the boundary has passed, keep the last legal facing sample. Do not sample a post-lock target position.
        if(!bFormalFacingLocked&&Now>=LockAt){bFormalFacingLocked=true;LockedAttackDirection=GetActorForwardVector().GetSafeNormal2D();}
        if(!bFormalFacingLocked)
        {
            const FVector To=(MeleeTarget->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
            if(!To.IsNearlyZero())SetActorRotation(FRotator(0.,To.Rotation().Yaw,0.));
            LockedAttackDirection=GetActorForwardVector().GetSafeNormal2D();
        }
        if(Now<FormalPhaseEndTime)return;
        bFormalFacingLocked=true;AttackPhase=EDWCombatPhase::Active;FormalPhaseEndTime+=FormalCombatConfig.ActiveSeconds;
        AttackPhaseEndTime=FormalPhaseEndTime;
    }
    if(AttackPhase==EDWCombatPhase::Active)
    {
        ResolveFormalMeleeHit();if(!FormalCombatOperational()||AttackPhase!=EDWCombatPhase::Active)return;
        if(Now<FormalPhaseEndTime)return;
        AttackPhase=EDWCombatPhase::Recovery;FormalPhaseEndTime+=FormalCombatConfig.RecoverySeconds;AttackPhaseEndTime=FormalPhaseEndTime;
    }
    if(AttackPhase==EDWCombatPhase::Recovery&&Now>=FormalPhaseEndTime)CancelMeleeAttack();
}

void ADWEnemyCharacter::ResolveFormalMeleeHit()
{
    AActor* Target=MeleeTarget.Get();
    if(AttackPhase!=EDWCombatPhase::Active||bAttackResolved||!CanResolveFormalMeleeHit(Target,true))return;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return;
    const auto Action=FormalMeleeAction;const int32 Serial=AttackSerial;
    FDWCombatDamageCommand Command;Command.Identity={Action,1,DWCombatCommit::EActionDamageKind::EnemyMelee,1};
    Command.Source=this;Command.Target=Target;Command.TargetLife=FormalMeleeTargetLife;Command.Instigator=GetController();
    Command.Amount=CurrentAttackDamage;Command.KnockbackDirection=LockedAttackDirection;
    Command.KnockbackDistance=FormalCombatConfig.OutgoingKnockbackCm;Command.KnockbackSeconds=FormalCombatConfig.OutgoingKnockbackSeconds;
    bAttackResolved=true; // The logical action/phase/target life also deduplicates in the sole authority.
    const auto Result=Authority->SubmitActionDamage(Command);
    if(!FormalCombatOperational()||AttackSerial!=Serial||FormalMeleeAction.Id!=Action.Id||AttackPhase!=EDWCombatPhase::Active)return;
    if(Result.AppliedThisCall>0.)FormalMemory.RecordDamage();
    if(AttackSound)UDWAudioLibrary::PlayWorldSound(this,AttackSound,GetActorLocation());
    OnEnemyAttack(Target);
    if(FormalCombatOperational()&&AttackSerial==Serial&&FormalMeleeAction.Id==Action.Id&&AttackPhase==EDWCombatPhase::Active)
        OnAttackResolved(Target,Result.AppliedThisCall);
}

FDWEnemyFormalKnockback ADWEnemyCharacter::PrepareFormalKnockback(FVector Direction,float Distance,float Seconds) const
{
    FDWEnemyFormalKnockback K;
    if(!FormalCombatOperational()||Direction.ContainsNaN()||!FMath::IsFinite(Distance)||Distance<=0.f||!FMath::IsFinite(Seconds)||Seconds<=0.f)return K;
    K.Direction=Direction.GetSafeNormal2D();if(K.Direction.IsNearlyZero())return K;
    const bool Attacking=AttackPhase==EDWCombatPhase::Windup||AttackPhase==EDWCombatPhase::Active;
    K.DistanceCm=Distance*(Attacking?FormalCombatConfig.AttackKnockbackMultiplier:FormalCombatConfig.KnockbackMultiplier);
    K.DurationSeconds=Seconds;
    if(!FMath::IsFinite(K.DistanceCm)||K.DistanceCm<=0.||!FMath::IsFinite(K.DistanceCm/K.DurationSeconds))return {};
    if(FormalKnockbackFrame==GFrameCounter&&K.DistanceCm<=FormalKnockbackInitialDistance)return {};
    K.bContinueAttack=Attacking&&CombatProfile==EDWEnemyCombatProfile::DoughElite;
    K.bInterruptAttack=Attacking&&!K.bContinueAttack;K.bValid=true;return K;
}

void ADWEnemyCharacter::CommitFormalKnockbackFacts(const FDWEnemyFormalKnockback& K,
    DWCombatCommit::FReceiptId Receipt,DWCombatCommit::FEntityLife Player,DWCombatCommit::FEntityLife Enemy)
{
    if(!K.bValid||!FormalCombatOperational())return;
    if(FormalKnockbackRevision<MAX_uint64)++FormalKnockbackRevision;
    ForcedFall.Accept(Receipt,Player,Enemy,FormalEnemyId,Receipt.Epoch,FormalKnockbackRevision);
    if(FormalKnockbackFrame!=GFrameCounter)FormalKnockbackCommittedTravelThisFrame=0.;
    FormalKnockbackFrame=GFrameCounter;FormalKnockbackInitialDistance=K.DistanceCm;
    bFormalAttackContinuesDuringKnockback=K.bContinueAttack;
    bIsCombatKnockedBack=true;KnockbackDirection=K.Direction;KnockbackDistanceRemaining=FMath::Max(0.,K.DistanceCm-FormalKnockbackCommittedTravelThisFrame);
    KnockbackTimeRemaining=K.DurationSeconds;KnockbackSpeed=KnockbackDistanceRemaining/K.DurationSeconds;
    NextPathRequestTime=GetWorld()->GetTimeSeconds()+K.DurationSeconds;
    if(K.bInterruptAttack)
    {
        if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))Authority->EndAction(FormalMeleeAction);
        FormalMeleeAction={};MeleeTarget.Reset();bAttackResolved=true;
        FormalAttackStartedTime=GetWorld()->GetTimeSeconds()-FormalCombatConfig.WindupSeconds-FormalCombatConfig.ActiveSeconds;
        AttackStartedTime=FormalAttackStartedTime;
        AttackPhase=EDWCombatPhase::Recovery;FormalPhaseEndTime=GetWorld()->GetTimeSeconds()+FormalCombatConfig.RecoverySeconds;
        FormalNextAttackTime=FormalPhaseEndTime+FormalCombatConfig.ExtraCooldownSeconds;
        AttackPhaseEndTime=FormalPhaseEndTime;NextAttackTime=FormalNextAttackTime;
    }
}

bool ADWEnemyCharacter::UpdateFormalKnockback(float Dt)
{
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return false;
    const auto Life=Authority->CaptureActorLife(this);const TWeakObjectPtr<UWorld> World(GetWorld());
    const uint64 Revision=FormalKnockbackRevision;const bool HadBegunPlay=HasActorBegunPlay();
    auto Current=[&]()
    {
        if(!FormalCombatOperational()||!World.IsValid()||GetWorld()!=World.Get()||
            (HadBegunPlay&&!HasActorBegunPlay())||FormalKnockbackRevision!=Revision)return false;
        auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return false;
        const auto NowLife=A->CaptureActorLife(this);return NowLife.Entity==Life.Entity&&NowLife.Life==Life.Life;
    };
    StopAIMovement();if(!Current())return false;
    if(!FMath::IsFinite(Dt)||Dt<=0.f)return true;
    const float Seconds=FMath::Min(Dt,KnockbackTimeRemaining);
    const float Distance=FMath::Min(KnockbackDistanceRemaining,KnockbackSpeed*Seconds);
    // Debit before overlap callbacks so a same-frame stronger replacement cannot add this travel again.
    KnockbackDistanceRemaining=FMath::Max(0.f,KnockbackDistanceRemaining-Distance);
    KnockbackTimeRemaining=FMath::Max(0.f,KnockbackTimeRemaining-Seconds);
    if(FormalKnockbackFrame==GFrameCounter)FormalKnockbackCommittedTravelThisFrame+=Distance;
    BeginForcedFallMove();if(!Current())return false;
    FHitResult Hit;AddActorWorldOffset(KnockbackDirection*Distance,true,&Hit,ETeleportType::None);
    if(!Current())return false; // Sweep can execute overlap/hit callbacks, including restore/death/new knockback.
    CompleteForcedFallMove();if(!Current())return false;
    if(Hit.bBlockingHit||KnockbackTimeRemaining<=KINDA_SMALL_NUMBER||KnockbackDistanceRemaining<=KINDA_SMALL_NUMBER)
    {bIsCombatKnockedBack=false;KnockbackDistanceRemaining=KnockbackTimeRemaining=0.f;NextPathRequestTime=GetWorld()->GetTimeSeconds();ForcedFall.FinishDriver();}
    return true;
}

void ADWEnemyCharacter::RecordFormalDamageFacts(AActor* PlayerSource,DWCombatCommit::FEntityLife SourceLife,
    DWCombatCommit::FReceiptId Receipt,DWCombatCommit::FEntityLife TargetLife,bool bFatal,bool bSourceWasPlayer)
{
    if(!bFormalCombatReady||CombatProfile!=FormalCombatConfig.Profile)return;
    FormalMemory.RecordDamage();
    if(auto* Player=Cast<ADWPlayerCharacter>(PlayerSource);bSourceWasPlayer&&Player&&SourceLife.Entity&&SourceLife.Life)
    {
        FormalMemory.Engage();TargetPawn=Player;FormalAlertEndTime=GetWorld()->GetTimeSeconds();
        AlertEndTime=FormalAlertEndTime;bFormalNeedsChaseState=true;
    }
    if(bFatal)
    {
        FormalCoreDrop.FatalReceipt=Receipt;FormalCoreDrop.EnemyLife=TargetLife;
        FormalCoreDrop.PlayerLife=bSourceWasPlayer?SourceLife:DWCombatCommit::FEntityLife{};
    }
}

FDWEnemyFormalSaveData ADWEnemyCharacter::CaptureFormalCombatSave() const
{
    FDWEnemyFormalSaveData S;if(!bFormalCombatReady||CombatProfile!=FormalCombatConfig.Profile||!GetWorld())return S;
    const double Now=GetWorld()->GetTimeSeconds();S.Schema=1;S.Profile=CombatProfile;S.StableEnemyId=FormalEnemyId;S.BirthLocation=HomeLocation;
    S.bPlayerDeathReturn=IsAlive()&&bPlayerDeathReturn;
    S.bEngaged=FormalMemory.bEngaged;S.bReturningHome=FormalMemory.bReturningHome;S.bDisengageCandidate=FormalMemory.bDisengageCandidate;
    S.DisengageElapsedSeconds=FormalMemory.DisengageElapsedSeconds;S.AlertRemainingSeconds=FMath::Max(0.,FormalAlertEndTime-Now);
    S.AttackCooldownRemainingSeconds=FMath::Max(0.,FormalNextAttackTime-Now);
    S.CoreDropDeathId=FormalCoreDrop.DeathId;S.DropDecision=FormalCoreDrop.Decision;S.FatalReceiptEpoch=FormalCoreDrop.FatalReceipt.Epoch;S.FatalReceiptId=FormalCoreDrop.FatalReceipt.Id;
    S.DeathEntityId=FormalCoreDrop.EnemyLife.Entity;S.DeathLife=FormalCoreDrop.EnemyLife.Life;
    S.CreditedPlayerEntityId=FormalCoreDrop.PlayerLife.Entity;S.CreditedPlayerLife=FormalCoreDrop.PlayerLife.Life;
    S.PickupId=FormalCoreDrop.PickupId;S.CoreItemId=FormalCoreDrop.CoreItemId;S.CoreCount=FormalCoreDrop.Count;return S;
}

bool ADWEnemyCharacter::RestoreFormalCombatSave(const FDWEnemyFormalSaveData& S,float SavedHealth,FString& Error)
{
    if(!IsInGameThread()||!GetWorld()||IsActorBeingDestroyed()||GetWorld()->bIsTearingDown||
        bDeathTransitionInProgress||bCombatRestoreInProgress||S.Schema!=1||S.Profile!=CombatProfile||!bFormalCombatReady||
        !S.StableEnemyId.IsValid()||S.BirthLocation.ContainsNaN()||!FMath::IsFinite(SavedHealth)||SavedHealth<0.f||SavedHealth>FormalCombatConfig.Health||
        (SavedHealth<=0.f&&S.bPlayerDeathReturn)||(S.bEngaged&&S.bReturningHome)||!FMath::IsFinite(S.DisengageElapsedSeconds)||S.DisengageElapsedSeconds<0.||
        S.DisengageElapsedSeconds>FormalCombatConfig.DisengageSeconds||(!S.bDisengageCandidate&&S.DisengageElapsedSeconds!=0.)||
        (S.bDisengageCandidate&&!S.bEngaged)||!FMath::IsFinite(S.AlertRemainingSeconds)||S.AlertRemainingSeconds<0.||S.AlertRemainingSeconds>FormalCombatConfig.AlertDelaySeconds||
        !FMath::IsFinite(S.AttackCooldownRemainingSeconds)||S.AttackCooldownRemainingSeconds<0.||S.AttackCooldownRemainingSeconds>FormalCombatConfig.AttackCycleSeconds())
    {Error=TEXT("Invalid/mismatched formal enemy save; no enemy state changed.");return false;}
    auto* CoreDrops=UDWCombatCoreDropSubsystem::Get(this);
    if(!CoreDrops||!CoreDrops->ValidateEnemySavedDrop(S,SavedHealth))
    {Error=TEXT("Enemy core facts must exactly match the validated world drop snapshot.");return false;}
    SetHealthForLoad(SavedHealth);
    if(!IsValid(this)||IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown||Health!=SavedHealth)
    {Error=TEXT("Enemy lifecycle changed during health restoration; formal memory was not adopted.");return false;}
    if(UDWCombatCoreDropSubsystem::Get(this)!=CoreDrops||!CoreDrops->ValidateEnemySavedDrop(S,SavedHealth))
    {Error=TEXT("Core snapshot changed during health restoration; formal state was not adopted.");return false;}
    FormalEnemyId=S.StableEnemyId;HomeLocation=S.BirthLocation;bFormalHomeCaptured=true;
    if(bFormalGeneratedPersistentId)PersistentId=FName(*(TEXT("DWFormal_")+FormalEnemyId.ToString(EGuidFormats::Digits)));
    bPlayerDeathReturn=SavedHealth>0.f&&S.bPlayerDeathReturn;
    FormalMemory.bEngaged=SavedHealth>0.f&&S.bEngaged;FormalMemory.bReturningHome=SavedHealth>0.f&&S.bReturningHome;
    FormalMemory.bDisengageCandidate=FormalMemory.bEngaged&&S.bDisengageCandidate;
    FormalMemory.DisengageElapsedSeconds=FormalMemory.bDisengageCandidate?S.DisengageElapsedSeconds:0.;
    FormalMemory.LastObservationTime=GetWorld()->GetTimeSeconds();FormalObservedPlayerLife={};FormalObservedPlayerDamageRevision=0;
    FormalAlertEndTime=GetWorld()->GetTimeSeconds()+S.AlertRemainingSeconds;FormalNextAttackTime=GetWorld()->GetTimeSeconds()+S.AttackCooldownRemainingSeconds;
    AlertEndTime=FormalAlertEndTime;NextAttackTime=FormalNextAttackTime;
    if(SavedHealth>0.f)State=FormalMemory.bReturningHome?EDWEnemyState::ReturningHome:(FormalMemory.bEngaged?EDWEnemyState::Chase:EDWEnemyState::Patrol);
    FormalCoreDrop.DeathId=S.CoreDropDeathId;FormalCoreDrop.Decision=S.DropDecision;FormalCoreDrop.PickupId=S.PickupId;
    FormalCoreDrop.CoreItemId=S.CoreItemId;FormalCoreDrop.Count=S.CoreCount;
    FormalCoreDrop.FatalReceipt={S.FatalReceiptEpoch,S.FatalReceiptId};FormalCoreDrop.EnemyLife={S.DeathEntityId,S.DeathLife};
    FormalCoreDrop.PlayerLife={S.CreditedPlayerEntityId,S.CreditedPlayerLife};
    if(!CoreDrops->BindRestoredEnemy(*this,S)){Error=TEXT("Core world lifecycle changed during enemy restoration.");return false;}
    Error.Reset();return true;
}
