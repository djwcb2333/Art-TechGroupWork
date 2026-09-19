#include "TopDownActionCharacter.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

ATopDownActionCharacter::ATopDownActionCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
}

void ATopDownActionCharacter::BeginPlay()
{
    Super::BeginPlay();
    // The top-down template can constrain Z. Jump needs free vertical movement.
    GetCharacterMovement()->SetPlaneConstraintEnabled(false);
    GetCharacterMovement()->JumpZVelocity = JumpLaunchSpeed;
    GetCharacterMovement()->NavAgentProps.bCanJump = true;
    JumpMaxCount = 1;
}

void ATopDownActionCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    if (!bBindDefaultActionKeys) return;
    PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ATopDownActionCharacter::StartActionJump);
    PlayerInputComponent->BindKey(EKeys::SpaceBar, IE_Released, this, &ATopDownActionCharacter::StopJumping);
    PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &ATopDownActionCharacter::StartForwardRoll);
    PlayerInputComponent->BindKey(EKeys::RightShift, IE_Pressed, this, &ATopDownActionCharacter::StartForwardRoll);
}

void ATopDownActionCharacter::StartActionJump()
{
    if (bIsRolling || !GetCharacterMovement()->IsMovingOnGround() || !CanJump()) return;
    if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->StopMovement();
    GetCharacterMovement()->JumpZVelocity = JumpLaunchSpeed;
    Jump();
}

void ATopDownActionCharacter::StartForwardRoll()
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (bIsRolling || !Movement->IsMovingOnGround() || GetWorld()->GetTimeSeconds() < NextRollTime) return;

    StopJumping();
    if (APlayerController* PC = Cast<APlayerController>(GetController())) PC->StopMovement();
    LockedController = GetController();
    if (LockedController.IsValid()) LockedController->SetIgnoreMoveInput(true);
    ConsumeMovementInputVector();
    Movement->StopMovementImmediately();
    RollDirection = GetActorForwardVector().GetSafeNormal2D();
    ActiveRollDuration = FMath::Max(0.1f, RollDuration);
    RollElapsed = 0.f;
    bPreviousOrientToMovement = Movement->bOrientRotationToMovement;
    Movement->bOrientRotationToMovement = false;
    MeshRestTransform = GetMesh()->GetRelativeTransform();
    bPreviousPauseAnims = GetMesh()->bPauseAnims;
    GetMesh()->bPauseAnims = bUseSomersaultVisual;
    bIsRolling = true;

    // CharacterMovement handles swept capsule collision and slopes, not teleportation.
    TSharedPtr<FRootMotionSource_ConstantForce> Motion = MakeShared<FRootMotionSource_ConstantForce>();
    Motion->InstanceName = TEXT("TopDownForwardRoll");
    Motion->Priority = 1000;
    Motion->AccumulateMode = ERootMotionAccumulateMode::Override;
    Motion->Duration = ActiveRollDuration;
    Motion->Force = RollDirection * (FMath::Max(1.f, RollDistance) / ActiveRollDuration);
    // A roll has a fixed distance: trim its final movement tick at low frame rates.
    Motion->Settings.UnSetFlag(ERootMotionSourceSettingsFlags::DisablePartialEndTick);
    Motion->FinishVelocityParams.Mode = ERootMotionFinishVelocityMode::SetVelocity;
    Motion->FinishVelocityParams.SetVelocity = FVector::ZeroVector;
    RollMotionId = Movement->ApplyRootMotionSource(Motion);
}

void ATopDownActionCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bIsRolling) return;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    const TSharedPtr<FRootMotionSource> Motion = Movement->GetRootMotionSourceByID(RollMotionId);
    // Follow the movement source's clock, including partial ticks, instead of a second timer.
    if (!Motion.IsValid() || Motion->Status.HasFlag(ERootMotionSourceStatusFlags::Finished) ||
        Motion->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval) || Movement->IsFalling())
    {
        FinishRoll();
        return;
    }
    RollElapsed = Motion->GetTime();
    if (!bUseSomersaultVisual) return;
    // Placeholder somersault about the capsule centre. Camera and capsule remain upright.
    const float Alpha = FMath::Clamp(RollElapsed / ActiveRollDuration, 0.f, 1.f);
    const FQuat Turn(FVector::RightVector, Alpha * 2.f * PI);
    GetMesh()->SetRelativeLocationAndRotation(Turn.RotateVector(MeshRestTransform.GetLocation()),
        Turn * MeshRestTransform.GetRotation());
}

void ATopDownActionCharacter::FinishRoll()
{
    if (!bIsRolling) return;
    bIsRolling = false;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->RemoveRootMotionSourceByID(RollMotionId);
    Movement->bOrientRotationToMovement = bPreviousOrientToMovement;
    Movement->Velocity.X = 0.f;
    Movement->Velocity.Y = 0.f;
    GetMesh()->SetRelativeTransform(MeshRestTransform);
    GetMesh()->bPauseAnims = bPreviousPauseAnims;
    if (LockedController.IsValid())
    {
        // Discard clicks queued during the roll before normal controls resume.
        if (APlayerController* PC = Cast<APlayerController>(LockedController.Get())) PC->StopMovement();
        LockedController->SetIgnoreMoveInput(false);
    }
    LockedController.Reset();
    NextRollTime = GetWorld()->GetTimeSeconds() + FMath::Max(0.f, RollCooldown);
}

void ATopDownActionCharacter::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp,
    bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
    Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
    if (bIsRolling && FMath::Abs(HitNormal.Z) < 0.5f && FVector::DotProduct(RollDirection, HitNormal) < -0.2f)
        FinishRoll();
}

void ATopDownActionCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FinishRoll();
    Super::EndPlay(EndPlayReason);
}
