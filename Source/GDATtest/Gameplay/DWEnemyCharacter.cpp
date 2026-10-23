#include "DWEnemyCharacter.h"
#include "DWCombatGroundAreaSubsystem.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWGameInstance.h"
#include "DWCombatRules.h"
#include "DWDamageVolumeLibrary.h"
#include "DWAudioLibrary.h"
#include "DWUserSettings.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyNest.h"
#include "DWWorldEvent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace DWEnemyCombat
{
    // Public Blueprint values can bypass editor ClampMin metadata.
    static float FiniteClamp(float Value, float Minimum, float Maximum, float Fallback)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Fallback;
    }

    static int32 SelectVariant(const TArray<FDWEnemyAttackVariant>& Variants)
    {
        double Total = 0.;
        for (const FDWEnemyAttackVariant& Variant : Variants)
            if (FMath::IsFinite(Variant.SelectionWeight) && Variant.SelectionWeight > 0.f) Total += Variant.SelectionWeight;
        if (!FMath::IsFinite(Total) || Total <= 0.) return INDEX_NONE;
        const double Draw = FMath::FRand() * Total;
        double Cumulative = 0.;
        int32 LastEligible = INDEX_NONE;
        for (int32 Index = 0; Index < Variants.Num(); ++Index)
        {
            const float Weight = Variants[Index].SelectionWeight;
            if (!FMath::IsFinite(Weight) || Weight <= 0.f) continue;
            LastEligible = Index;
            Cumulative += Weight;
            if (Draw < Cumulative) return Index;
        }
        return LastEligible;
    }
}

ADWEnemyCharacter::ADWEnemyCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    // A paused game must cancel telegraphed hits instead of resuming a hidden pending strike.
    PrimaryActorTick.bTickEvenWhenPaused = true;
    GetCapsuleComponent()->InitCapsuleSize(38.f, 78.f);
    AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
    AIControllerClass = AAIController::StaticClass();
    bUseControllerRotationYaw = false;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0.f, 540.f, 0.f);
    GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;
    GetCharacterMovement()->bUseRVOAvoidance = true;
    PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
    PlaceholderBody->SetupAttachment(RootComponent);
    PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> BodyMeshFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (BodyMeshFinder.Succeeded()) PlaceholderBody->SetStaticMesh(BodyMeshFinder.Object);
    PlaceholderBody->SetRelativeScale3D(FVector(0.75f, 0.75f, 1.35f));
    DetectionRange = CreateDefaultSubobject<USphereComponent>(TEXT("DetectionRange"));
    DetectionRange->SetupAttachment(RootComponent);
    DetectionRange->SetUsingAbsoluteScale(true);
    DetectionRange->SetSphereRadius(DetectionRadius);
    DetectionRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DetectionRange->SetGenerateOverlapEvents(false);
    DetectionRange->SetHiddenInGame(true);
    DetectionRange->SetVisibility(true);
    DetectionRange->ShapeColor = FColor::Red;
    Tags.AddUnique(TEXT("DoughWorldEnemy"));
}

void ADWEnemyCharacter::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    DetectionRange->SetSphereRadius(DWEnemyCombat::FiniteClamp(DetectionRadius, 1.f, 100000.f, 1000.f));
    GetCharacterMovement()->MaxWalkSpeed = DWEnemyCombat::FiniteClamp(MoveSpeed, 0.f, 100000.f, 250.f);
}

void ADWEnemyCharacter::BeginPlay()
{
    Super::BeginPlay();
    if (PlaceholderBody && !bPlaceholderTransformCaptured)
    {
        PlaceholderBaseTransform = PlaceholderBody->GetRelativeTransform();
        bPlaceholderTransformCaptured = true;
    }
    if (!bHealthRestoredFromSave) Health = DWEnemyCombat::FiniteClamp(MaxHealth, 1.f, 100000000.f, 100.f);

    if(!bFormalHomeCaptured)HomeLocation=GetActorLocation();
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy&&!bFormalCombatReady)
    {
        const auto* GI=GetGameInstance<UDWGameInstance>();FString Error;
        if(!GI||!GI->GetCombatRules()||!InitializeFormalCombat(*GI->GetCombatRules(),Error))
            UE_LOG(LogTemp,Warning,TEXT("Formal enemy disabled without valid rules/art contract: %s"),*Error);
    }
    NextPatrolTime = GetWorld()->GetTimeSeconds() + FMath::FRandRange(0.f, DWEnemyCombat::FiniteClamp(PatrolWaitTime, 0.f, 3600.f, 1.5f));
}

bool ADWEnemyCharacter::HasSightTo(const AActor* Target) const
{
    if (!IsValid(Target) || !GetWorld() || Target->GetActorLocation().ContainsNaN() || GetActorLocation().ContainsNaN()) return false;
    if (CombatProfile==EDWEnemyCombatProfile::Legacy&&!bRequireLineOfSight) return true;
    FHitResult Hit;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWEnemySight), false, this);
    const FVector Start = GetActorLocation() + FVector(0.f, 0.f, 30.f);
    const FVector End = Target->GetActorLocation() + FVector(0.f, 0.f, 25.f);
    const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
    return !bBlocked || Hit.GetActor() == Target;
}

bool ADWEnemyCharacter::CanDetectTarget_Implementation(AActor* Target) const
{
    if(HasDaggerDiscoveryGrace(Cast<ADWPlayerCharacter>(Target)))return false;
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy)
        return IsHostileToCombatPlayer(Cast<ADWPlayerCharacter>(Target))&&IsCombatTargetAlive(Target)&&
            FVector::Dist2D(GetActorLocation(),Target->GetActorLocation())<=FormalCombatConfig.AlertRadiusCm&&HasSightTo(Target);
    return IsCombatTargetAlive(Target) && FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()) <= DWEnemyCombat::FiniteClamp(DetectionRadius, 1.f, 100000.f, 1000.f) && HasSightTo(Target);
}

bool ADWEnemyCharacter::CanAttackTarget_Implementation(AActor* Target) const
{
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy)return CanResolveFormalMeleeHit(Target,false);
    if (!IsCombatTargetAlive(Target)) return false;
    const float Range = AttackPhase != EDWCombatPhase::None ? CurrentAttackRange : DWEnemyCombat::FiniteClamp(AttackRange, 1.f, 100000.f, 150.f);
    return UDWDamageVolumeLibrary::IsTargetInDamageSphere(Target, GetActorLocation(), Range) && HasSightTo(Target);
}

float ADWEnemyCharacter::GetAttackDamage_Implementation(AActor* Target) const
{
    return AttackPhase != EDWCombatPhase::None ? CurrentAttackDamage : DWEnemyCombat::FiniteClamp(AttackDamage, 0.f, 100000000.f, 0.f);
}

void ADWEnemyCharacter::SetAIState(EDWEnemyState NewState)
{
    if (State == NewState) return;
    const EDWEnemyState PreviousState = State;
    State = NewState;
    OnStateChanged(PreviousState, NewState);
}

void ADWEnemyCharacter::ChoosePatrolGoal()
{
    FNavLocation NavPoint;
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    const float SafeRadius = DWEnemyCombat::FiniteClamp(PatrolRadius, 0.f, 100000.f, 450.f);
    if (Nav && Nav->GetRandomReachablePointInRadius(HomeLocation, SafeRadius, NavPoint))
        PatrolGoal = NavPoint.Location;
    else
    {
        const float Angle = FMath::FRandRange(0.f, 2.f * PI);
        const float Distance = FMath::FRandRange(0.25f, 1.f) * SafeRadius;
        PatrolGoal = HomeLocation + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
    }
    bHasPatrolGoal = true;
}

void ADWEnemyCharacter::StopAIMovement()
{
    if (AAIController* AI = Cast<AAIController>(GetController())) AI->StopMovement();
    ConsumeMovementInputVector();
    GetCharacterMovement()->StopMovementImmediately();
}

void ADWEnemyCharacter::MoveToward(const FVector& Goal, float DeltaSeconds)
{
    AAIController* AI = Cast<AAIController>(GetController());
    UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
    FNavLocation OnNav;
    const bool bHasNav = Nav && Nav->ProjectPointToNavigation(GetActorLocation(), OnNav, FVector(100.f, 100.f, 250.f));
    if (AI && bHasNav)
    {
        if (GetWorld()->GetTimeSeconds() >= NextPathRequestTime)
        {
            AI->MoveToLocation(Goal, DWEnemyCombat::FiniteClamp(NavigationAcceptanceRadius,0.f,100000.f,35.f), true, true, true, false, nullptr, true);
            NextPathRequestTime = GetWorld()->GetTimeSeconds() + DWEnemyCombat::FiniteClamp(NavigationRepathInterval,0.f,60.f,.35f);
        }
    }
    else
    {
        // Movement still uses CharacterMovement sweeps when no nav data is present.
        AddMovementInput((Goal - GetActorLocation()).GetSafeNormal2D(), 1.f);
    }
}

void ADWEnemyCharacter::Tick(float DeltaSeconds)
{
    if(bCombatWorldStaged)return;
    Super::Tick(DeltaSeconds);
    if(IsValid(this)&&!IsActorBeingDestroyed()&&IsAlive()&&GetWorld()&&!GetWorld()->bIsTearingDown)
    {auto* DaggerPlayer=Cast<ADWPlayerCharacter>(UGameplayStatics::GetPlayerPawn(this,0));SyncDaggerAwareness(DaggerPlayer); /* D20 Shipping HUD paints the ring. */}
    ON_SCOPE_EXIT { if(IsValid(this)&&!IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown)UpdatePlaceholderPresentation(); };
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy){if(IsAlive())TickFormalCombat(DeltaSeconds);return;}
    if (!IsAlive() || !GetWorld()) return;
    if (GetWorld()->IsPaused() || UDWWorldEventSubsystem::IsPlaying(this))
    {
        CancelMeleeAttack();
        StopAIMovement();
        return;
    }
    UpdateSlowEffects();
    const float Now = GetWorld()->GetTimeSeconds();
    if (bIsCombatKnockedBack)
    {
        UpdateCombatKnockback(DeltaSeconds);
        DrawCombatDebug();
        return;
    }
    if(TickPlayerDeathReturn(DeltaSeconds))return;
    if (AttackPhase != EDWCombatPhase::None)
    {
        StopAIMovement();
        UpdateMeleeAttack(Now);
        DrawCombatDebug();
        return;
    }
    APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
    if (IsCombatTargetAlive(Player))
    {
        const float Distance = FVector::Dist2D(GetActorLocation(), Player->GetActorLocation());
        if (!TargetPawn.IsValid() && !HasDaggerDiscoveryGrace(Cast<ADWPlayerCharacter>(Player)) && CanDetectTarget(Player)) BeginAlert(Player);
        else if (TargetPawn.IsValid() && Distance > FMath::Max(
            DWEnemyCombat::FiniteClamp(DetectionRadius, 1.f, 100000.f, 1000.f),
            DWEnemyCombat::FiniteClamp(LoseTargetRadius, 1.f, 100000.f, 1500.f))) TargetPawn.Reset();
    }
    else TargetPawn.Reset();
    if (!IsAlive()) return; // State callbacks may kill or deactivate the actor.

    if (APawn* Target = TargetPawn.Get())
    {
        const FVector Delta = Target->GetActorLocation() - GetActorLocation();
        if (State == EDWEnemyState::Alert && Now < AlertEndTime)
        {
            StopAIMovement();
            if (!Delta.IsNearlyZero()) SetActorRotation(FRotator(0.f, Delta.Rotation().Yaw, 0.f));
            DrawCombatDebug();
            return;
        }
        if (CanAttackTarget(Target))
        {
            StopAIMovement();
            if (!Delta.IsNearlyZero()) SetActorRotation(FRotator(0.f, Delta.Rotation().Yaw, 0.f));
            // During cooldown show a stationary alert pose, not a repeating attack.
            SetAIState(EDWEnemyState::Chase);
            if (Now >= NextAttackTime) TryStartMeleeAttack(Target);
            DrawCombatDebug();
        }
        else
        {
            SetAIState(EDWEnemyState::Chase);
            MoveToward(Target->GetActorLocation(), DeltaSeconds);
        }
        return;
    }

    SetAIState(EDWEnemyState::Patrol);
    if (!bHasPatrolGoal && Now >= NextPatrolTime) ChoosePatrolGoal();
    if (bHasPatrolGoal)
    {
        if (FVector::Dist2D(GetActorLocation(), PatrolGoal) < 80.f)
        {
            StopAIMovement();
            bHasPatrolGoal = false;
            NextPatrolTime = Now + DWEnemyCombat::FiniteClamp(PatrolWaitTime, 0.f, 3600.f, 1.5f);
        }
        else MoveToward(PatrolGoal, DeltaSeconds);
    }
}

bool ADWEnemyCharacter::IsCombatTargetAlive(const AActor* Target) const
{
    if (!IsValid(Target) || Target == this || Target->IsActorBeingDestroyed() || !Target->CanBeDamaged()) return false;
    if (const ADWPlayerCharacter* Player = Cast<ADWPlayerCharacter>(Target)) return !Player->IsDead();
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy)return false;
    if (const ADWEnemyCharacter* Enemy = Cast<ADWEnemyCharacter>(Target)) return Enemy->IsAlive();
    if (const ADWEnemyNest* Nest = Cast<ADWEnemyNest>(Target)) return Nest->IsAlive();
    return true;
}

void ADWEnemyCharacter::BeginAlert(APawn* NewTarget)
{
    if (!IsAlive() || !IsCombatTargetAlive(NewTarget) || !GetWorld()) return;

    if(CombatProfile!=EDWEnemyCombatProfile::Legacy)
    {
        if(!IsHostileToCombatPlayer(Cast<ADWPlayerCharacter>(NewTarget)))return;
        FormalMemory.Engage();FormalAlertEndTime=GetWorld()->GetTimeSeconds()+FormalCombatConfig.AlertDelaySeconds;
    }
    TargetPawn = NewTarget;
    StopAIMovement();
    AlertEndTime = GetWorld()->GetTimeSeconds() + DWEnemyCombat::FiniteClamp(AlertDuration, 0.f, 60.f, 0.45f);
    SetAIState(EDWEnemyState::Alert);
}

EDWCombatAnimationState ADWEnemyCharacter::GetCombatAnimationState() const
{
    if (!IsAlive()) return EDWCombatAnimationState::Death;
    if (AttackPhase != EDWCombatPhase::None) return EDWCombatAnimationState::Attack;
    if (State == EDWEnemyState::Alert || (State == EDWEnemyState::Chase && GetVelocity().SizeSquared2D() < 4.f)) return EDWCombatAnimationState::Alert;
    if (GetVelocity().SizeSquared2D() >= 4.f) return EDWCombatAnimationState::Move;
    return EDWCombatAnimationState::Idle;
}

float ADWEnemyCharacter::GetCombatAttackElapsedSeconds() const
{
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy)return GetWorld()&&AttackPhase!=EDWCombatPhase::None?FMath::Clamp(static_cast<float>(GetWorld()->GetTimeSeconds()-FormalAttackStartedTime),0.f,GetCombatAttackDuration()):0.f;
    return GetWorld() && AttackPhase != EDWCombatPhase::None
        ? FMath::Clamp(GetWorld()->GetTimeSeconds() - AttackStartedTime, 0.f, GetCombatAttackDuration()) : 0.f;
}

UAnimationAsset* ADWEnemyCharacter::GetCurrentAttackAnimation() const
{
    return bHasAttackSnapshot ? CurrentAttackAnimation.Get() : AttackAnimation.Get();
}

bool ADWEnemyCharacter::TryStartMeleeAttack(AActor* Target)
{
    if(bCombatWorldStaged||bPlayerDeathReturn)return false;
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy)return TryStartFormalMeleeAttack(Target);
    if (bStartingMeleeAttack || bDeathTransitionInProgress || bCombatRestoreInProgress || !IsAlive() || !GetWorld() || IsActorBeingDestroyed() ||
        GetWorld()->bIsTearingDown || GetWorld()->IsPaused() || UDWWorldEventSubsystem::IsPlaying(this) ||
        bIsCombatKnockedBack || AttackPhase != EDWCombatPhase::None || GetWorld()->GetTimeSeconds() < NextAttackTime) return false;
    TGuardValue<bool> StartGuard(bStartingMeleeAttack, true);
    // A designer rule may further restrict a hit, but never bypass the hard range/visibility guard.
    if (!IsCombatTargetAlive(Target) || !CanAttackTarget(Target) || !IsAlive() || bIsCombatKnockedBack ||
        !UDWDamageVolumeLibrary::IsTargetInDamageSphere(Target, GetActorLocation(), DWEnemyCombat::FiniteClamp(AttackRange, 1.f, 100000.f, 150.f)) || !HasSightTo(Target)) return false;
    // Only this concrete melee entry classifies legacy enemy -> player damage. Generic
    // ApplyDamage stays unclassified; the existing cancellation owns this action's end.
    DWCombatCommit::FActionId NativeMeleeAction;DWCombatCommit::FEntityLife NativeTargetLife;
    if(Cast<ADWPlayerCharacter>(Target))
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return false;
        NativeTargetLife=Authority->CaptureActorLife(Target);if(!NativeTargetLife.Entity||!NativeTargetLife.Life)return false;
        NativeMeleeAction=Authority->BeginAction(this);if(!NativeMeleeAction.Id)return false;
    }
    FormalMeleeAction=NativeMeleeAction;FormalMeleeTargetLife=NativeTargetLife;
    const float Now = GetWorld()->GetTimeSeconds();
    CurrentAttackWindup = DWEnemyCombat::FiniteClamp(AttackWindup, 0.f, 60.f, 0.55f);
    CurrentAttackActiveDuration = DWEnemyCombat::FiniteClamp(AttackActiveDuration, 0.01f, 60.f, 0.12f);
    CurrentAttackRecovery = DWEnemyCombat::FiniteClamp(AttackRecovery, 0.f, 60.f, 0.4f);
    CurrentAttackRange = DWEnemyCombat::FiniteClamp(AttackRange, 1.f, 100000.f, 150.f);
    CurrentAttackDamage = DWEnemyCombat::FiniteClamp(AttackDamage, 0.f, 100000000.f, 0.f);
    CurrentAttackAnimation = AttackAnimation;
    float Interval = DWEnemyCombat::FiniteClamp(AttackInterval, 0.05f, 3600.f, 1.2f);
    // Draw exactly once after the ordinary approach/start gate has accepted this attack.
    CurrentAttackVariantIndex = DWEnemyCombat::SelectVariant(AttackVariants);
    if (AttackVariants.IsValidIndex(CurrentAttackVariantIndex))
    {
        const FDWEnemyAttackVariant& Variant = AttackVariants[CurrentAttackVariantIndex];
        CurrentAttackAnimation = Variant.Animation ? Variant.Animation : AttackAnimation;
        CurrentAttackWindup = DWEnemyCombat::FiniteClamp(Variant.Windup, 0.f, 60.f, CurrentAttackWindup);
        CurrentAttackActiveDuration = DWEnemyCombat::FiniteClamp(Variant.Active, 0.01f, 60.f, CurrentAttackActiveDuration);
        CurrentAttackRecovery = DWEnemyCombat::FiniteClamp(Variant.Recovery, 0.f, 60.f, CurrentAttackRecovery);
        CurrentAttackRange = DWEnemyCombat::FiniteClamp(Variant.Range, 1.f, 100000.f, CurrentAttackRange);
        CurrentAttackDamage = DWEnemyCombat::FiniteClamp(Variant.Damage, 0.f, 100000000.f, CurrentAttackDamage);
        Interval = DWEnemyCombat::FiniteClamp(Variant.Interval, 0.05f, 3600.f, Interval);
    }
    bHasAttackSnapshot = true;
    NextAttackTime = Now + FMath::Max(Interval, GetCombatAttackDuration());
    MeleeTarget = Target;
    bAttackResolved = false;
    AttackSerial = AttackSerial == MAX_int32 ? 1 : AttackSerial + 1;
    AttackPhase = EDWCombatPhase::Windup;
    AttackStartedTime = Now;
    AttackPhaseEndTime = Now + CurrentAttackWindup;
    StopAIMovement();
    const FVector ToTarget = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
    if (!ToTarget.IsNearlyZero()) SetActorRotation(FRotator(0.f, ToTarget.Rotation().Yaw, 0.f));
    LockedAttackDirection = GetActorForwardVector().GetSafeNormal2D();
    const int32 StartedSerial = AttackSerial;
    SetAIState(EDWEnemyState::Attack);
    if (IsAlive() && AttackPhase == EDWCombatPhase::Windup && AttackSerial == StartedSerial) OnAttackStarted(Target);
    return IsAlive() && AttackPhase != EDWCombatPhase::None && AttackSerial == StartedSerial;
}

void ADWEnemyCharacter::CancelMeleeAttack()
{
    if(FormalMeleeAction.Id)if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))Authority->EndAction(FormalMeleeAction);
    FormalMeleeAction={};bFormalFacingLocked=false;
    AttackPhase = EDWCombatPhase::None;
    AttackPhaseEndTime = 0.f;
    MeleeTarget.Reset();
    bAttackResolved = true;
    if (State == EDWEnemyState::Attack && !bDeathTransitionInProgress)
    {
        const EDWEnemyState NextState = TargetPawn.IsValid() ? EDWEnemyState::Chase : EDWEnemyState::Patrol;
        // Save restoration is silent. Ordinary cancellation preserves the existing state event.
        if (bCombatRestoreInProgress) State = NextState;
        else
        {
            TGuardValue<bool> CancelGuard(bStartingMeleeAttack, true);
            SetAIState(NextState);
        }
    }
}

bool ADWEnemyCharacter::CanResolveMeleeHit(AActor* Target) const
{
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy)return CanResolveFormalMeleeHit(Target,true);
    if (!IsAlive() || !IsCombatTargetAlive(Target) || bIsCombatKnockedBack) return false;
    const float Range = AttackPhase != EDWCombatPhase::None ? CurrentAttackRange : DWEnemyCombat::FiniteClamp(AttackRange, 1.f, 100000.f, 150.f);
    if (!UDWDamageVolumeLibrary::IsTargetInDamageSphere(Target, GetActorLocation(), Range) || !HasSightTo(Target)) return false;
    const FVector ToTarget = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
    const FVector Facing = AttackPhase == EDWCombatPhase::Active ? LockedAttackDirection : GetActorForwardVector().GetSafeNormal2D();
    const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(DWEnemyCombat::FiniteClamp(AttackHalfAngle, 1.f, 180.f, 65.f)));
    return ToTarget.IsNearlyZero() || FVector::DotProduct(Facing, ToTarget) >= CosHalfAngle;
}

void ADWEnemyCharacter::ResolveMeleeHit()
{
    AActor* Target = MeleeTarget.Get();
    if (AttackPhase != EDWCombatPhase::Active || bAttackResolved || !CanResolveMeleeHit(Target)) return;
    const int32 ResolvingSerial = AttackSerial;
    if (!CanAttackTarget(Target) || !IsAlive() || AttackPhase != EDWCombatPhase::Active || AttackSerial != ResolvingSerial || !CanResolveMeleeHit(Target)) return;
    const float Damage = DWEnemyCombat::FiniteClamp(GetAttackDamage(Target), 0.f, 100000000.f, 0.f);
    if (!IsAlive() || AttackPhase != EDWCombatPhase::Active || AttackSerial != ResolvingSerial || !CanResolveMeleeHit(Target)) return;
    // Mark before the sole authority can publish reentrant damage callbacks.
    bAttackResolved = true;
    float AppliedDamage=0.f;
    if(Cast<ADWPlayerCharacter>(Target))
    {
        // A retained target life prevents a restored/replaced player from receiving an
        // old windup. Never fall back to untyped damage if the native action went stale.
        if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this);Authority&&FormalMeleeAction.Id)
        {
            FDWCombatDamageCommand Command;Command.Identity={FormalMeleeAction,1,DWCombatCommit::EActionDamageKind::EnemyMelee,1};
            Command.Source=this;Command.Target=Target;Command.TargetLife=FormalMeleeTargetLife;
            Command.Instigator=GetController();Command.Amount=Damage;
            AppliedDamage=Authority->SubmitActionDamage(Command).AppliedThisCall;
        }
    }
    else AppliedDamage=UGameplayStatics::ApplyDamage(Target,Damage,GetController(),this,UDamageType::StaticClass());
    if (!IsAlive() || AttackPhase != EDWCombatPhase::Active || AttackSerial != ResolvingSerial) return;
    if (AttackSound) UDWAudioLibrary::PlayWorldSound(this, AttackSound, GetActorLocation());
    OnEnemyAttack(Target);
    if (IsAlive() && AttackPhase == EDWCombatPhase::Active && AttackSerial == ResolvingSerial) OnAttackResolved(Target, AppliedDamage);
}

void ADWEnemyCharacter::UpdateMeleeAttack(float Now)
{
    TGuardValue<bool> UpdateGuard(bStartingMeleeAttack, true);
    if (!IsAlive() || bIsCombatKnockedBack || !IsCombatTargetAlive(MeleeTarget.Get()))
    {
        CancelMeleeAttack();
        return;
    }
    if (AttackPhase == EDWCombatPhase::Windup)
    {
        const FVector ToTarget = (MeleeTarget->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
        if (!ToTarget.IsNearlyZero()) SetActorRotation(FRotator(0.f, ToTarget.Rotation().Yaw, 0.f));
        if (Now < AttackPhaseEndTime) return;
        // Last windup sample chooses the attack facing. Do not home after this point.
        LockedAttackDirection = GetActorForwardVector().GetSafeNormal2D();
        AttackPhase = EDWCombatPhase::Active;
        AttackPhaseEndTime += CurrentAttackActiveDuration;
    }
    if (AttackPhase == EDWCombatPhase::Active)
    {
        // Even a long frame that crosses the entire window resolves at most one current-world query.
        ResolveMeleeHit();
        if (!IsAlive() || AttackPhase != EDWCombatPhase::Active) return;
        if (Now < AttackPhaseEndTime) return;
        AttackPhase = EDWCombatPhase::Recovery;
        AttackPhaseEndTime += CurrentAttackRecovery;
    }
    if (AttackPhase == EDWCombatPhase::Recovery && Now >= AttackPhaseEndTime)
    {
        CancelMeleeAttack();
    }
}

void ADWEnemyCharacter::ApplyCombatKnockback(FVector Direction, float Distance, float Duration)
{
    if(CombatProfile!=EDWEnemyCombatProfile::Legacy)
    {
        const auto Snapshot=PrepareFormalKnockback(Direction,Distance,Duration);
        if(Snapshot.bValid){CommitFormalKnockbackFacts(Snapshot);StopAIMovement();}
        return;
    }
    if (!IsAlive() || !GetWorld() || IsActorBeingDestroyed() || Direction.ContainsNaN() ||
        !FMath::IsFinite(Distance) || Distance <= 0.f || !FMath::IsFinite(Duration) || Duration <= 0.f) return;
    Direction = Direction.GetSafeNormal2D();
    if (Direction.IsNearlyZero()) return;
    CancelMeleeAttack();
    if (!IsAlive()) return;
    StopAIMovement();
    bIsCombatKnockedBack = true;
    KnockbackDirection = Direction;
    KnockbackDistanceRemaining = FMath::Clamp(Distance, 0.f, 100000.f);
    KnockbackTimeRemaining = FMath::Clamp(Duration, 0.01f, 60.f);
    KnockbackSpeed = KnockbackDistanceRemaining / KnockbackTimeRemaining;
    NextPathRequestTime = GetWorld()->GetTimeSeconds() + KnockbackTimeRemaining;
    NextAttackTime = FMath::Max(NextAttackTime, NextPathRequestTime);
}

void ADWEnemyCharacter::UpdateCombatKnockback(float DeltaSeconds)
{
    StopAIMovement();
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
    const float StepSeconds = FMath::Min(DeltaSeconds, KnockbackTimeRemaining);
    const float StepDistance = FMath::Min(KnockbackDistanceRemaining, KnockbackSpeed * StepSeconds);
    FHitResult Hit;
    AddActorWorldOffset(KnockbackDirection * StepDistance, true, &Hit, ETeleportType::None);
    KnockbackDistanceRemaining = FMath::Max(0.f, KnockbackDistanceRemaining - StepDistance);
    KnockbackTimeRemaining = FMath::Max(0.f, KnockbackTimeRemaining - StepSeconds);
    if (Hit.bBlockingHit || KnockbackTimeRemaining <= KINDA_SMALL_NUMBER || KnockbackDistanceRemaining <= KINDA_SMALL_NUMBER)
    {
        bIsCombatKnockedBack = false;
        KnockbackDistanceRemaining = KnockbackTimeRemaining = 0.f;
        NextPathRequestTime = GetWorld()->GetTimeSeconds();
    }
}

void ADWEnemyCharacter::DrawCombatDebug() const
{
#if ENABLE_DRAW_DEBUG
    if (!bShowCombatDebug || !GetWorld()) return;
    if (State == EDWEnemyState::Alert)
        DrawDebugSphere(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 115.f), 12.f, 8, FColor::Yellow, false, 0.f, 0, 2.f);
    if (AttackPhase == EDWCombatPhase::None) return;
    const FColor Color = AttackPhase == EDWCombatPhase::Windup ? FColor::Yellow : (AttackPhase == EDWCombatPhase::Active ? FColor::Red : FColor::Silver);
    const float Range = CurrentAttackRange;
    const float Angle = DWEnemyCombat::FiniteClamp(AttackHalfAngle, 1.f, 180.f, 65.f);
    const FVector Facing = AttackPhase == EDWCombatPhase::Windup ? GetActorForwardVector() : LockedAttackDirection;
    DrawDebugSphere(GetWorld(), GetActorLocation(), Range, 24, Color, false, 0.f, 0, 1.f);
    for (float Side : {-1.f, 1.f})
        DrawDebugLine(GetWorld(), GetActorLocation(), GetActorLocation() + Facing.RotateAngleAxis(Angle * Side, FVector::UpVector) * Range, Color, false, 0.f, 0, 3.f);
#endif
}

void ADWEnemyCharacter::RestorePlaceholderPresentation()
{
    if (PlaceholderBody && bPlaceholderTransformCaptured && bPlaceholderPresentationApplied)
        PlaceholderBody->SetRelativeTransform(PlaceholderBaseTransform);
    bPlaceholderPresentationApplied = false;
}

void ADWEnemyCharacter::UpdatePlaceholderPresentation()
{
    if (!PlaceholderBody) return;
    if (!bAnimatePlaceholder)
    {
        RestorePlaceholderPresentation();
        bPlaceholderTransformCaptured = false;
        return;
    }
    if (!bPlaceholderTransformCaptured)
    {
        PlaceholderBaseTransform = PlaceholderBody->GetRelativeTransform();
        bPlaceholderTransformCaptured = true;
    }
    FTransform VisualTransform = PlaceholderBaseTransform;
    FVector ScaleMultiplier(1.f);
    FVector Offset = FVector::ZeroVector;
    FRotator RotationOffset = FRotator::ZeroRotator;
    const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    if (!IsAlive())
    {
        ScaleMultiplier = FVector(1.3f, 1.3f, 0.2f);
        Offset.Z = -42.f;
        RotationOffset.Roll = -12.f;
    }
    else if (AttackPhase == EDWCombatPhase::Windup)
    {
        const float Progress = FMath::Clamp((Now - AttackStartedTime) / FMath::Max(0.01f, CurrentAttackWindup), 0.f, 1.f);
        ScaleMultiplier = FVector(1.f + 0.12f * Progress, 1.f + 0.12f * Progress, 1.f - 0.2f * Progress);
        Offset.Z = -8.f * Progress;
        RotationOffset.Pitch = -10.f * Progress;
    }
    else if (AttackPhase == EDWCombatPhase::Active)
    {
        const float Progress = FMath::Clamp((Now - AttackStartedTime - CurrentAttackWindup) / FMath::Max(0.01f, CurrentAttackActiveDuration), 0.f, 1.f);
        const float Strike = FMath::Sin(Progress * PI);
        ScaleMultiplier = FVector(1.08f, 1.f, 0.94f);
        Offset.X = 10.f + 12.f * Strike;
        RotationOffset.Pitch = 12.f + 15.f * Strike;
    }
    else if (AttackPhase == EDWCombatPhase::Recovery)
    {
        const float Progress = FMath::Clamp((Now - AttackStartedTime - CurrentAttackWindup - CurrentAttackActiveDuration) / FMath::Max(0.01f, CurrentAttackRecovery), 0.f, 1.f);
        const float Remaining = 1.f - Progress;
        ScaleMultiplier = FVector(1.f + 0.08f * Remaining, 1.f, 1.f - 0.06f * Remaining);
        Offset.X = 10.f * Remaining;
        RotationOffset.Pitch = 12.f * Remaining;
    }
    else if (State == EDWEnemyState::Alert && Now < AlertEndTime)
    {
        const float Duration = DWEnemyCombat::FiniteClamp(AlertDuration, 0.01f, 60.f, 0.45f);
        const float Progress = 1.f - FMath::Clamp((AlertEndTime - Now) / Duration, 0.f, 1.f);
        Offset.Z = 9.f * FMath::Sin(Progress * PI);
    }
    VisualTransform.SetScale3D(PlaceholderBaseTransform.GetScale3D() * ScaleMultiplier);
    VisualTransform.SetTranslation(PlaceholderBaseTransform.GetTranslation() + Offset);
    VisualTransform.SetRotation(PlaceholderBaseTransform.GetRotation() * RotationOffset.Quaternion());
    PlaceholderBody->SetRelativeTransform(VisualTransform);
    bPlaceholderPresentationApplied = true;
}

void ADWEnemyCharacter::ApplySlow(float Percent, float Duration)
{
    if (!IsAlive() || !GetWorld() || !FMath::IsFinite(Percent) || !FMath::IsFinite(Duration) || Duration <= 0.f) return;
    const float Clamped = FMath::Clamp(Percent, 0.f, 100.f);
    const float Until = GetWorld()->GetTimeSeconds() + FMath::Min(Duration, 3600.f);
    // Coalesce identical strengths so a long-lived pool does not grow this array.
    for (FVector2D& Effect : SlowEffects)
    {
        if (FMath::IsNearlyEqual(static_cast<float>(Effect.X), Clamped))
        {
            Effect.Y = FMath::Max(Effect.Y, static_cast<double>(Until));
            UpdateSlowEffects();
            return;
        }
    }
    SlowEffects.Emplace(Clamped, Until);
    UpdateSlowEffects();
}

void ADWEnemyCharacter::UpdateSlowEffects()
{
    const float Now = GetWorld()->GetTimeSeconds();
    SlowEffects.RemoveAll([Now](const FVector2D& Effect) { return Effect.Y <= Now; });
    float Strongest = static_cast<float>(GetCombatGroundAreaSlowFraction()*100.);
    for (const FVector2D& Effect : SlowEffects) Strongest = FMath::Max(Strongest, static_cast<float>(Effect.X));
    GetCharacterMovement()->MaxWalkSpeed = DWEnemyCombat::FiniteClamp(MoveSpeed, 0.f, 100000.f, 250.f) * (1.f - Strongest / 100.f) * GetGatheringEnvironmentMovementMultiplier();
}

float ADWEnemyCharacter::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
    if (auto* Authority = UDWCombatAuthoritySubsystem::Get(this))
        return Authority->SubmitLegacyDamage(this, DamageAmount, DamageEvent, EventInstigator, DamageCauser);
    return 0.f;
}

void ADWEnemyCharacter::SetHealthForLoad(float NewHealth)
{
    if (bDeathTransitionInProgress || bCombatRestoreInProgress || !FMath::IsFinite(NewHealth) || !GetWorld()) return;
    if (auto* Authority = UDWCombatAuthoritySubsystem::Get(this)) Authority->NotifyActorRestored(this);

    bCombatDeathEffectsPending = false;
    ForcedFall.Clear();
    FormalMemory={};FormalObservedPlayerLife={};FormalObservedPlayerDamageRevision=0;
    DaggerAwareness={};DaggerPlayerLife={};PendingDaggerAlertTarget.Reset();
    FormalAlertEndTime=FormalNextAttackTime=0.;bFormalNeedsChaseState=false;bFormalAttackContinuesDuringKnockback=false;
    FormalKnockbackFrame=MAX_uint64;FormalKnockbackInitialDistance=0.;FormalCoreDrop={};
    TGuardValue<bool> RestoreGuard(bCombatRestoreInProgress, true);
    RestorePlaceholderPresentation();
    bHealthRestoredFromSave = true;
    CancelMeleeAttack();
    bHasAttackSnapshot = false;
    CurrentAttackAnimation = nullptr;
    CurrentAttackVariantIndex = INDEX_NONE;
    bIsCombatKnockedBack = false;
    KnockbackDistanceRemaining = KnockbackTimeRemaining = 0.f;
    StopAIMovement();
    TargetPawn.Reset();
    SlowEffects.Reset();
    GetWorldTimerManager().ClearTimer(DeathCleanupTimer);
    SetLifeSpan(0.f);
    Health = FMath::Clamp(NewHealth, 0.f, DWEnemyCombat::FiniteClamp(MaxHealth, 1.f, 100000000.f, 100.f));
    if (Health <= 0.f)
    {
        if (State == EDWEnemyState::Dead)
        {
            // A second load must not accidentally clear the only pending corpse cleanup.
            if (!PersistentId.IsNone()) FinishPersistentDeath();
            else SetLifeSpan(DWEnemyCombat::FiniteClamp(DeathDestroyDelay, 0.1f, 3600.f, 1.5f));
        }
        else Die(true);
        return;
    }
    // Restoring an older live save can revive the same placed actor without replaying effects.
    State = EDWEnemyState::Patrol;
    if (bHasPreDeathState)
    {
        SetActorHiddenInGame(bPreDeathHidden);
        SetActorEnableCollision(bPreDeathCollision);
        SetActorTickEnabled(bPreDeathTick);
        if (GetMesh()) GetMesh()->SetComponentTickEnabled(bPreDeathMeshTick);
        GetCharacterMovement()->SetMovementMode(static_cast<EMovementMode>(PreDeathMovementMode), PreDeathCustomMovementMode);
        bHasPreDeathState = false;
    }
    UpdateSlowEffects();
    bHasPatrolGoal = false;
    const float Now = GetWorld()->GetTimeSeconds();
    NextPatrolTime = Now + DWEnemyCombat::FiniteClamp(PatrolWaitTime, 0.f, 3600.f, 1.5f);
    NextPathRequestTime = Now;
    NextAttackTime = Now;
    AlertEndTime = 0.f;
    UpdatePlaceholderPresentation();
}

void ADWEnemyCharacter::CommitCombatDeathFacts()
{
    if (State == EDWEnemyState::Dead || bCombatDeathEffectsPending) return;
    // Only native reads and assignments: no movement, state or Blueprint dispatch.
    bPreDeathHidden = IsHidden();
    bPreDeathCollision = GetActorEnableCollision();
    bPreDeathTick = IsActorTickEnabled();
    bPreDeathMeshTick = GetMesh() && GetMesh()->IsComponentTickEnabled();
    PreDeathMovementMode = static_cast<uint8>(GetCharacterMovement()->MovementMode);
    PreDeathCustomMovementMode = GetCharacterMovement()->CustomMovementMode;
    bHasPreDeathState = true;
    ForcedFall.Clear();
    CombatDeathPreviousState = State;
    Health = 0.f; State = EDWEnemyState::Dead;
    if(FormalMeleeAction.Id)if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))Authority->EndAction(FormalMeleeAction);
    FormalMeleeAction={};bFormalAttackContinuesDuringKnockback=false;
    AttackPhase = EDWCombatPhase::None; AttackPhaseEndTime = 0.f; bAttackResolved = true;
    MeleeTarget.Reset(); TargetPawn.Reset();
    bIsCombatKnockedBack = false;
    KnockbackDistanceRemaining = KnockbackTimeRemaining = 0.f;
    bCombatDeathEffectsPending = true;
}

void ADWEnemyCharacter::PublishCombatDeathEffects(bool bFromSave)
{
    const TWeakObjectPtr<UWorld> DeathWorld(GetWorld());
    const bool bHadBegunPlay=HasActorBegunPlay();
    const auto CanContinue=[this,DeathWorld,bHadBegunPlay]()
    {
        return IsValid(this)&&!IsActorBeingDestroyed()&&DeathWorld.IsValid()&&GetWorld()==DeathWorld.Get()
            &&!DeathWorld->bIsTearingDown&&(!bHadBegunPlay||HasActorBegunPlay());
    };
    if(!CanContinue())return;
    if (!bCombatDeathEffectsPending || bDeathTransitionInProgress || IsActorBeingDestroyed()) return;
    bCombatDeathEffectsPending = false;
    TGuardValue<bool> DeathGuard(bDeathTransitionInProgress, true);
    if (!bFromSave) OnStateChanged(CombatDeathPreviousState, EDWEnemyState::Dead);
    if (!CanContinue()) return;
    StopAIMovement();
    if (!CanContinue()) return;
    GetCharacterMovement()->DisableMovement();
    if (!CanContinue()) return;
    SetActorEnableCollision(false);
    if (!CanContinue()) return;
    if (!bFromSave)
    {
        if (DeathSound) UDWAudioLibrary::PlayWorldSound(this, DeathSound, GetActorLocation());
        if (!CanContinue()) return;
        OnEnemyDied();
        if (!CanContinue()) return;
    }
    if (bFromSave && !PersistentId.IsNone()) FinishPersistentDeath();
    else if (PersistentId.IsNone()) SetLifeSpan(DWEnemyCombat::FiniteClamp(DeathDestroyDelay, 0.1f, 3600.f, 1.5f));
    else GetWorldTimerManager().SetTimer(DeathCleanupTimer, this, &ADWEnemyCharacter::FinishPersistentDeath, DWEnemyCombat::FiniteClamp(DeathDestroyDelay, 0.1f, 3600.f, 1.5f), false);
    UpdatePlaceholderPresentation();
}

void ADWEnemyCharacter::Die(bool bFromSave)
{
    if (bDeathTransitionInProgress || (State == EDWEnemyState::Dead && !bCombatDeathEffectsPending)) return;
    CommitCombatDeathFacts();
    if (auto* Authority = UDWCombatAuthoritySubsystem::Get(this)) Authority->NotifyActorDeath(this);
    PublishCombatDeathEffects(bFromSave);
}

void ADWEnemyCharacter::FinishPersistentDeath()
{
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
    // AlwaysTickPoseAndRefreshBones ignores visibility; stop its independent component tick
    // only after the death presentation, while retaining this actor for save enumeration.
    if (GetMesh()) GetMesh()->SetComponentTickEnabled(false);
    // Authored IDs remain queryable by save capture after the death visual expires.
}

double ADWEnemyCharacter::GetCombatGroundAreaSlowFraction()const
{const auto* A=UDWCombatGroundAreaSubsystem::Get(this);return A&&GetWorld()?A->GetStrongestSlowAt(*this,GetWorld()->GetTimeSeconds()):0.;}
void ADWEnemyCharacter::RefreshCombatGroundAreaSlow()
{
    if(!GetWorld()||GetWorld()->bIsTearingDown||!IsAlive()||IsActorBeingDestroyed())return;UpdateSlowEffects();
    if(IsFormalCombatReady())
    {
        double Strongest=GetCombatGroundAreaSlowFraction();for(const auto& E:SlowEffects)Strongest=FMath::Max(Strongest,E.X/100.);
        GetCharacterMovement()->MaxWalkSpeed=FormalCombatConfig.MoveSpeedCmPerSecond*(1.-Strongest)*(FormalMemory.bReturningHome?FormalCombatConfig.ReturnSpeedMultiplier:1.)*GetGatheringEnvironmentMovementMultiplier();
    }
}
