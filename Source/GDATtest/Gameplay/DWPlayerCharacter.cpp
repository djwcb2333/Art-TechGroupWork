#include "DWPlayerCharacter.h"
#include "DWGameplayCameraShake.h"
#include "DWWorldEvent.h"
#include "DWUserSettings.h"
#include "DWLocalizationLibrary.h"
#include "DWAudioLibrary.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWGameplayHUD.h"
#include "DWGameInstance.h"
#include "DWSaveGame.h"
#include "DWResourceNode.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWAlcoholProjectile.h"
#include "DWPlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraShakeBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequenceBase.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"

ADWPlayerCharacter::ADWPlayerCharacter()
{
    GameplayCameraShake=CreateDefaultSubobject<UDWGameplayCameraShakeComponent>(TEXT("GameplayCameraShake"));
    Inventory=CreateDefaultSubobject<UDWInventoryComponent>(TEXT("DoughWorldInventory"));
    SwordPlaceholder=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwordPlaceholder"));
    SwordPlaceholder->SetupAttachment(GetMesh());SwordPlaceholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);SwordPlaceholder->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(Cube.Succeeded())SwordPlaceholder->SetStaticMesh(Cube.Object);
    SwordPlaceholder->SetRelativeScale3D(FVector(.065f,.065f,.45f));SwordPlaceholder->SetRelativeLocation(FVector(0,0,20.f));
    bBindDefaultActionKeys=false;bUseSomersaultVisual=false;
    bUseControllerRotationYaw=false;
    GetCharacterMovement()->bOrientRotationToMovement=true;
    GetCharacterMovement()->NavAgentProps.bCanJump=false;JumpMaxCount=0;
    AlcoholProjectileClass=ADWAlcoholProjectile::StaticClass();
    // A fixed set of components is reused: movement never creates a new actor/system each tick.
    MovementDustComponent=CreateDefaultSubobject<UNiagaraComponent>(TEXT("MovementDustVFX"));
    RunTrailComponent=CreateDefaultSubobject<UNiagaraComponent>(TEXT("RunTrailVFX"));
    DashTrailComponent=CreateDefaultSubobject<UNiagaraComponent>(TEXT("DashTrailVFX"));
    DashStartComponent=CreateDefaultSubobject<UNiagaraComponent>(TEXT("DashStartVFX"));
    TransformBurstComponent=CreateDefaultSubobject<UNiagaraComponent>(TEXT("TransformBurstVFX"));
    for(UNiagaraComponent* Component:{MovementDustComponent.Get(),RunTrailComponent.Get(),DashTrailComponent.Get(),DashStartComponent.Get(),TransformBurstComponent.Get()})
    {
        Component->SetupAttachment(GetRootComponent());
        Component->SetAutoActivate(false);Component->SetAutoDestroy(false);
        // Prevent a second editable Niagara asset slot from overriding the canonical actor settings.
        Component->bEditableWhenInherited=false;
        Component->SetCanEverAffectNavigation(false);
    }
    // Dash ignition is grounded; the transform body effect follows the capsule by default.
    DashStartComponent->SetAbsolute(true,true,true);
    TransformBurstComponent->SetAbsolute(false,false,true);
    Tags.Add(TEXT("DoughWorldPlayer"));
}
void ADWPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();
    if(auto* GI=GetGameInstance<UDWGameInstance>())GameplayConfig=GI->GetConfig();
    if(!GameplayConfig)GameplayConfig=NewObject<UDWGameplayConfig>(this);
    GameplayConfig->RefreshDefinitionsFromTables();Inventory->InitializeInventory(GameplayConfig);
    if(!GetMesh()->DoesSocketExist(SwordSocket)&&GetMesh()->DoesSocketExist(TEXT("bone.005.R")))SwordSocket=TEXT("bone.005.R");
    if(GetMesh()->DoesSocketExist(SwordSocket))SwordPlaceholder->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,SwordSocket);
    else SwordPlaceholder->SetVisibility(false);
    Health=FMath::Max(1.f,GameplayConfig->MaxHealth);
    GetCharacterMovement()->MaxWalkSpeed=GameplayConfig->BaseMoveSpeed;
    GetCharacterMovement()->NavAgentProps.bCanJump=false;JumpMaxCount=0;
    CameraArm=FindComponentByClass<USpringArmComponent>();
    if(!CameraArm)
    {
        CameraArm=NewObject<USpringArmComponent>(this,TEXT("GameplayCameraArm"));CameraArm->SetupAttachment(GetRootComponent());CameraArm->bDoCollisionTest=false;CameraArm->RegisterComponent();
        auto* Cam=NewObject<UCameraComponent>(this,TEXT("GameplayCamera"));Cam->SetupAttachment(CameraArm,USpringArmComponent::SocketName);Cam->RegisterComponent();
    }
    CameraYaw=CameraArm->GetComponentRotation().Yaw;
    CameraPitch=FMath::Clamp(CameraArm->GetComponentRotation().Pitch,MinCameraPitch,MaxCameraPitch);
    CameraArm->SetUsingAbsoluteRotation(true);CameraArm->bUsePawnControlRotation=false;CameraRestOffset=CameraArm->SocketOffset;
    CameraArm->TargetArmLength=FMath::Max(100.f,CameraDistance);
    // The title pauses the world before its first tick, so initialize the view immediately.
    CameraArm->SetWorldRotation(FRotator(CameraPitch,CameraYaw,0));
    InitializeFormVisuals();
    if(auto* GI=GetGameInstance<UDWGameInstance>())GI->ApplyPendingSave(this);
    CameraArm->TickComponent(0.f,LEVELTICK_All,nullptr);
    UpdateFormAppearance(true);UpdateAnimation();
    PreviousVFXLocation=GetActorLocation();bHasVFXLocationSample=true;
}
void ADWPlayerCharacter::PrepareCameraForReveal()
{
 if(!HasActorBegunPlay()||!CameraArm)return;
 const bool Lag=CameraArm->bEnableCameraLag,RotLag=CameraArm->bEnableCameraRotationLag;
 CameraArm->bEnableCameraLag=false;CameraArm->bEnableCameraRotationLag=false;
 CameraArm->SetWorldRotation(FRotator(CameraPitch,CameraYaw,0));
 CameraArm->TargetArmLength=FMath::Max(100.f,CameraDistance);
 CameraArm->TickComponent(0.f,LEVELTICK_All,nullptr);
 CameraArm->bEnableCameraLag=Lag;CameraArm->bEnableCameraRotationLag=RotLag;
}
void ADWPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopGatherFeedback(false);
    StopMovementAudio(true);
    StopPlayerVFX(true);
    if(GetMesh()&&FormBoneFinalizedHandle.IsValid())GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(FormBoneFinalizedHandle);
    FormBoneFinalizedHandle.Reset();
    Super::EndPlay(EndPlayReason);
}
void ADWPlayerCharacter::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!GameplayConfig){StopPlayerVFX(true);StopMovementAudio();bHasVFXLocationSample=false;CurrentVFXGroundSpeed=0.f;return;}
    const bool bSprintAllowed=bSprintHeld&&!bIsRolling&&!IsDead()&&!bRestoringSave&&CanSprint();
    bSprinting=bSprintAllowed&&GetVelocity().SizeSquared2D()>25.f&&GetCharacterMovement()->IsMovingOnGround();
    GetCharacterMovement()->MaxWalkSpeed=GameplayConfig->BaseMoveSpeed*(bSprintAllowed?FMath::Max(1.f,GameplayConfig->SprintSpeedMultiplier):1.f);
    TickGameplayClocks(Dt,bSprinting);
    if(!IsDead())UpdateHarvest(Dt);
    UpdatePlayerVFX(Dt);
    // All movement effects now use the retained Niagara components; no mesh actors are spawned.
    if(CameraArm)
    {
        ShakeElapsed+=Dt;const float Alpha=FMath::Clamp(1.f-ShakeElapsed/FMath::Max(0.01f,TransformShakeSeconds),0.f,1.f);
        const FVector Shake(0,FMath::Sin(ShakeElapsed*TransformShakeFrequency)*TransformShakeMagnitude*Alpha,FMath::Cos(ShakeElapsed*TransformShakeFrequency*1.2f)*TransformShakeMagnitude*Alpha);
        CameraArm->SocketOffset=CameraRestOffset+Shake;CameraArm->SetWorldRotation(FRotator(CameraPitch,CameraYaw,0));
    }
    UpdateAnimation();
    TickFormAppearance(Dt);
}
void ADWPlayerCharacter::TickGameplayClocks(float Dt,bool bActuallySprinting)
{
    if(!GameplayConfig||!Inventory||IsDead()||Dt<=0.f||bGameplayActionInProgress||bRestoringSave||UDWWorldEventSubsystem::IsPlaying(this))return;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    const float DecayInterval=FMath::Max(0.01f,GameplayConfig->TransformationDecayInterval);
    TransformationDecayElapsed+=Dt;
    while(TransformationDecayElapsed>=DecayInterval)
    {TransformationDecayElapsed-=DecayInterval;Transformation=FMath::Max(0.f,Transformation-GameplayConfig->MaxTransformation*GameplayConfig->TransformationDecayPercent/100.f);}
    if(bYeastForm&&Transformation<=KINDA_SMALL_NUMBER){bYeastForm=false;UpdateFormAppearance();PlayFormTransitionVFX(false);Notify(DWText(this,TEXT("变身值耗尽，恢复面团形态"),TEXT("Transformation depleted. Returned to Dough form.")));OnFormChanged(false);}
    const float Period=FMath::Max(0.01f,GameplayConfig->SprintAlcoholInterval);
    if(bActuallySprinting&&CanSprint())
    {
        Health=FMath::Max(0.f,Health-GameplayConfig->MaxHealth*GameplayConfig->SprintHealthCostPercentPerSecond*Dt/100.f);
        SprintAlcoholElapsed+=Dt;
    }
    // A full bag keeps the completed period pending; stopping never clears partial progress.
    while(SprintAlcoholElapsed>=Period)
    {
        const int32 Produced=FMath::Max(1,GameplayConfig->SprintAlcoholAmount);
        if(!Inventory->TryAddItem(TEXT("Alcohol"),Produced))break;
        SprintAlcoholElapsed-=Period;
        OnSprintAlcoholProduced(Produced);
        if(IsActorBeingDestroyed())return;
    }
    if(IsDead())Die();
}
float ADWPlayerCharacter::GetSprintAlcoholProgress()const
{return GameplayConfig?FMath::Clamp(SprintAlcoholElapsed/FMath::Max(0.01f,GameplayConfig->SprintAlcoholInterval),0.f,1.f):0.f;}
void ADWPlayerCharacter::MoveCameraRelative(float Forward,float Right)
{
    if(IsDead()||bIsRolling)return;
    const FVector2D V=FVector2D(Forward,Right).GetClampedToMaxSize(1.f);const FRotationMatrix R(FRotator(0,CameraYaw,0));
    AddMovementInput(R.GetUnitAxis(EAxis::X),V.X);AddMovementInput(R.GetUnitAxis(EAxis::Y),V.Y);
}
void ADWPlayerCharacter::DragCamera(float X,float Y){CameraYaw+=X*CameraDragSensitivity;CameraPitch=FMath::Clamp(CameraPitch+Y*CameraDragSensitivity,MinCameraPitch,MaxCameraPitch);}
void ADWPlayerCharacter::ClearHeldActions(){bSprintHeld=false;SetHarvestHeld(false);bSprinting=false;ConsumeMovementInputVector();StopPlayerVFX(true);StopMovementAudio();bHasVFXLocationSample=false;CurrentVFXGroundSpeed=0.f;}
void ADWPlayerCharacter::PerformDash()
{
    if(IsDead()||bGameplayActionInProgress||bRestoringSave)return;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    if(!CanDash())return;
    const bool WasRolling=bIsRolling;StartForwardRoll();
    if(!WasRolling&&bIsRolling)
    {
        SetHarvestHeld(false);TrailElapsed=DashTrailInterval;StopMovementAudio();
        if(CanPlayPlayerVFX())
        {
            PlayPlayerBurstVFX(DashStartComponent,DashStartNiagara,DashStartOffset,DashStartRotation,DashStartScale);
            DashStartVFXStopTime=GetWorld()->GetTimeSeconds()+FMath::Max(.01f,DashStartVFXMaxSeconds);
            DashStartVFXCleanupTime=DashStartVFXStopTime+FMath::Max(0.f,DashStartVFXTailSeconds);
        }
        UDWAudioLibrary::PlayWorldSound(this,DashStartSound,GetActorLocation(),DashStartSoundVolume,DashStartSoundPitch);
        OnDashStarted();
    }
}
bool ADWPlayerCharacter::CanPlayPlayerVFX()const
{
    return bEnablePlayerVFX&&CanPlayMovementFeedback();
}
bool ADWPlayerCharacter::CanPlayMovementFeedback()const
{
    if(IsDead()||bRestoringSave||IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown||GetWorld()->IsPaused())return false;
    if(const auto* PC=Cast<ADWPlayerController>(GetController()))if(PC->IsGameplayBlocked())return false;
    return true;
}
bool ADWPlayerCharacter::CanContinueMovementAudio()const
{
    return bMovementAudioDesired&&CanPlayMovementFeedback()&&!bIsRolling&&
        IsValid(MovementLoopSound)&&IsValid(MovementAudioComponent)&&
        MovementAudioComponent->GetSound()==MovementLoopSound&&MovementSoundVolume>0.f&&
        GetCharacterMovement()->IsMovingOnGround()&&
        CurrentVFXGroundSpeed>=FMath::Max(.01f,MovementVFXMinSpeed)&&
        GetVelocity().SizeSquared2D()>=FMath::Square(FMath::Max(.01f,MovementVFXMinSpeed));
}
void ADWPlayerCharacter::UpdateMovementAudio(bool bActuallyMoving)
{
    const float Volume=FMath::IsFinite(MovementSoundVolume)?FMath::Clamp(MovementSoundVolume,0.f,4.f):0.f;
    if(!bActuallyMoving||bIsRolling||!CanPlayMovementFeedback()||!IsValid(MovementLoopSound)||Volume<=0.f)
    {
        StopMovementAudio();return;
    }
    const bool bWasDesired=bMovementAudioDesired;
    bMovementAudioDesired=true;
    const float BasePitch=FMath::IsFinite(MovementSoundPitch)?FMath::Clamp(MovementSoundPitch,.25f,4.f):1.f;
    const float SprintPitch=FMath::IsFinite(SprintMovementPitchMultiplier)?FMath::Clamp(SprintMovementPitchMultiplier,.25f,4.f):1.f;
    CurrentMovementAudioPitch=FMath::Clamp(BasePitch*(bSprinting?SprintPitch:1.f),.25f,4.f);
    if(!IsValid(MovementAudioComponent))
    {
        const FVector FootOffset(0.f,0.f,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()+3.f);
        MovementAudioComponent=UDWAudioLibrary::CreateWorldLoop(this,MovementLoopSound,GetRootComponent(),NAME_None,FootOffset,Volume,CurrentMovementAudioPitch);
        bMovementAudioRestartPending=false;
        if(MovementAudioComponent)MovementAudioComponent->OnAudioFinished.AddUniqueDynamic(this,&ADWPlayerCharacter::HandleMovementAudioFinished);
        return;
    }
    bool bReplay=!bWasDesired||bMovementAudioRestartPending;
    if(MovementAudioComponent->GetSound()!=MovementLoopSound)
    {
        // Unbind before Stop; Stop may synchronously broadcast OnAudioFinished.
        MovementAudioComponent->OnAudioFinished.RemoveAll(this);
        MovementAudioComponent->Stop();
        if(!IsValid(MovementAudioComponent)||IsActorBeingDestroyed())return;
        MovementAudioComponent->SetSound(MovementLoopSound);bReplay=true;
    }
    MovementAudioComponent->SetVolumeMultiplier(Volume);
    MovementAudioComponent->SetPitchMultiplier(CurrentMovementAudioPitch);
    MovementAudioComponent->OnAudioFinished.AddUniqueDynamic(this,&ADWPlayerCharacter::HandleMovementAudioFinished);
    if(bReplay)
    {
        bMovementAudioRestartPending=false;
        MovementAudioComponent->Play();
    }
}
void ADWPlayerCharacter::StopMovementAudio(bool bDestroyComponent)
{
    bMovementAudioDesired=false;bMovementAudioRestartPending=false;
    UAudioComponent* Component=MovementAudioComponent.Get();
    if(bDestroyComponent)MovementAudioComponent=nullptr;
    if(!IsValid(Component))return;
    Component->OnAudioFinished.RemoveAll(this);
    if(Component->IsPlaying())Component->Stop();
    if(bDestroyComponent)Component->DestroyComponent();
}
void ADWPlayerCharacter::HandleMovementAudioFinished()
{
    // Never call Play from this callback: a zero-length source can finish synchronously.
    // A valid non-looping clip is restarted at most once on the next gameplay tick.
    if(CanContinueMovementAudio())bMovementAudioRestartPending=true;
    else StopMovementAudio();
}
void ADWPlayerCharacter::SetLoopingPlayerVFX(UNiagaraComponent* Component,UNiagaraSystem* System,bool bEmit,bool& bWasEmitting,const FVector& FootOffset,const FRotator& Rotation,const FVector& Scale)
{
    if(!IsValid(Component)){bWasEmitting=false;return;}
    if(Component->GetAsset()!=System)
    {
        Component->DeactivateImmediate();Component->SetAsset(System);bWasEmitting=false;
        if(Component==MovementDustComponent)bMovementDustRateSystemStarted=false;
    }
    const FVector RelativeFoot(0.f,0.f,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    Component->SetRelativeLocationAndRotation(RelativeFoot+FootOffset,Rotation);
    Component->SetRelativeScale3D(Scale);
    const bool bShouldEmit=bEmit&&System;
    if(Component==MovementDustComponent&&System&&!MovementDustBackwardDirectionParameter.IsNone()&&
        System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(),MovementDustBackwardDirectionParameter))!=INDEX_NONE)
    {
        Component->SetVariableVec3(MovementDustBackwardDirectionParameter,-GetActorForwardVector().GetSafeNormal());
    }
    // Check the source asset, not component overrides: SetVariableFloat can create an
    // override even when the replacement asset has no matching exposed float input.
    const bool bHasDustSpawnRateParameter=Component==MovementDustComponent&&System&&
        !MovementDustSpawnRateParameter.IsNone()&&
        System->GetExposedParameters().IndexOf(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),MovementDustSpawnRateParameter))!=INDEX_NONE;
    if(bHasDustSpawnRateParameter)
    {
        // This optional contract keeps the SAME live simulation across walk/stop/walk:
        // the asset's looping spawn-rate modules must all read this user parameter.
        const float SpawnRate=bShouldEmit?FMath::Max(0.f,MovementDustSpawnRate)*(bSprinting?FMath::Max(0.f,SprintDustSpawnRateMultiplier):1.f):0.f;
        Component->SetVariableFloat(MovementDustSpawnRateParameter,SpawnRate);
        if(SpawnRate>0.f&&(!bMovementDustRateSystemStarted||!Component->IsActive()))
        {
            Component->Activate(true);
            bMovementDustRateSystemStarted=true;
        }
        // No Deactivate/Activate pair on ordinary stop/resume: old smoke finishes naturally.
        bWasEmitting=SpawnRate>0.f;
        return;
    }
    // Generic third-party fallback requires no custom Niagara parameters.
    const bool bLeavingRateMode=Component==MovementDustComponent&&bMovementDustRateSystemStarted;
    if(bShouldEmit&&!bWasEmitting)Component->Activate(true);
    else if(!bShouldEmit&&(bWasEmitting||bLeavingRateMode))Component->Deactivate();
    if(Component==MovementDustComponent)bMovementDustRateSystemStarted=false;
    // Do not re-trigger an accidentally assigned one-shot system every frame.
    bWasEmitting=bShouldEmit;
}
void ADWPlayerCharacter::UpdatePlayerVFX(float Dt)
{
    const FVector Location=GetActorLocation();
    const FVector Displacement=Location-PreviousVFXLocation;
    const bool bValidSample=bHasVFXLocationSample&&FMath::IsFinite(Dt)&&Dt>KINDA_SMALL_NUMBER&&
        Displacement.SizeSquared()<=FMath::Square(FMath::Max(1.f,MovementVFXMaxSampleDistance));
    CurrentVFXGroundSpeed=bValidSample?static_cast<float>(Displacement.Size2D()/Dt):0.f;
    PreviousVFXLocation=Location;bHasVFXLocationSample=true;
    if(!CanPlayMovementFeedback()){StopPlayerVFX(true);StopMovementAudio();bHasVFXLocationSample=false;CurrentVFXGroundSpeed=0.f;return;}
    const bool bMoving=bValidSample&&GetCharacterMovement()->IsMovingOnGround()&&
        CurrentVFXGroundSpeed>=FMath::Max(.01f,MovementVFXMinSpeed);
    UpdateMovementAudio(bMoving);
    // Visual settings never mute the movement sound or reset its displacement sample.
    if(!bEnablePlayerVFX){StopPlayerVFX(true);return;}
    // Feet use one shared source. The optional body effect is strictly sprint-only.
    SetLoopingPlayerVFX(MovementDustComponent,MovementDustNiagara,bMoving,bMovementDustEmitting,MovementDustOffset,MovementDustRotation,MovementDustScale);
    SetLoopingPlayerVFX(RunTrailComponent,RunTrailNiagara,bMoving&&bSprinting&&!bIsRolling,bRunTrailEmitting,RunTrailOffset,RunTrailRotation,RunTrailScale);
    SetLoopingPlayerVFX(DashTrailComponent,DashNiagara,bMoving&&bIsRolling,bDashTrailEmitting,DashTrailOffset,DashTrailRotation,DashTrailScale);
    const float Now=GetWorld()->GetTimeSeconds();
    if(DashStartVFXStopTime>0.f&&Now>=DashStartVFXStopTime){if(DashStartComponent)DashStartComponent->Deactivate();DashStartVFXStopTime=0.f;}
    if(TransformBurstVFXStopTime>0.f&&Now>=TransformBurstVFXStopTime){if(TransformBurstComponent)TransformBurstComponent->Deactivate();TransformBurstVFXStopTime=0.f;}
    // Some third-party systems ignore inactive emission or contain infinite-lived particles.
    // A separate deadline guarantees bounded burst tails without allocating more components.
    if(DashStartVFXCleanupTime>0.f&&Now>=DashStartVFXCleanupTime){if(DashStartComponent)DashStartComponent->DeactivateImmediate();DashStartVFXCleanupTime=0.f;}
    if(TransformBurstVFXCleanupTime>0.f&&Now>=TransformBurstVFXCleanupTime){if(TransformBurstComponent)TransformBurstComponent->DeactivateImmediate();TransformBurstVFXCleanupTime=0.f;}
}
void ADWPlayerCharacter::StopPlayerVFX(bool bImmediate)
{
    for(UNiagaraComponent* Component:{MovementDustComponent.Get(),RunTrailComponent.Get(),DashTrailComponent.Get(),DashStartComponent.Get(),TransformBurstComponent.Get()})
    {
        if(!IsValid(Component))continue;
        if(bImmediate)Component->DeactivateImmediate();else Component->Deactivate();
    }
    bMovementDustEmitting=false;bRunTrailEmitting=false;bDashTrailEmitting=false;
    bMovementDustRateSystemStarted=false;
    // This is a visual-only stop. Movement audio shares the measured displacement,
    // so callers that cancel gameplay reset its sample explicitly instead.
    DashStartVFXStopTime=0.f;TransformBurstVFXStopTime=0.f;
    if(bImmediate){DashStartVFXCleanupTime=0.f;TransformBurstVFXCleanupTime=0.f;}
    else if(GetWorld())
    {
        const float Now=GetWorld()->GetTimeSeconds();
        DashStartVFXCleanupTime=Now+FMath::Max(.01f,DashStartVFXTailSeconds);
        TransformBurstVFXCleanupTime=Now+FMath::Max(.01f,TransformBurstVFXTailSeconds);
    }
}
void ADWPlayerCharacter::PlayPlayerBurstVFX(UNiagaraComponent* Component,UNiagaraSystem* System,const FVector& FootOffset,const FRotator& Rotation,const FVector& Scale)
{
    if(!CanPlayPlayerVFX()||!IsValid(Component))return;
    Component->DeactivateImmediate();
    if(Component->GetAsset()!=System)Component->SetAsset(System);
    if(!System)return;
    const FVector RelativeFoot(0.f,0.f,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
    const bool bFollowCharacter=Component==TransformBurstComponent&&bTransformVFXFollowCharacter;
    Component->SetAbsolute(!bFollowCharacter,!bFollowCharacter,true);
    if(bFollowCharacter)Component->SetRelativeLocationAndRotation(RelativeFoot+FootOffset,Rotation);
    else
    {
        const FVector WorldLocation=GetActorTransform().TransformPosition(RelativeFoot+FootOffset);
        Component->SetWorldLocationAndRotation(WorldLocation,GetActorQuat()*Rotation.Quaternion());
    }
    Component->SetWorldScale3D(Scale);
    Component->Activate(true);
}
void ADWPlayerCharacter::PlayFormTransitionVFX(bool bEntering)
{
    if(!CanPlayPlayerVFX())return;
    UNiagaraSystem* System=bEntering?TransformEnterNiagara.Get():TransformExitNiagara.Get();
    // Set the color before activation so even an initial burst receives the correct form tint.
    if(TransformBurstComponent)
    {
        TransformBurstComponent->DeactivateImmediate();
        if(TransformBurstComponent->GetAsset()!=System)TransformBurstComponent->SetAsset(System);
        if(!TransformVFXColorParameter.IsNone())TransformBurstComponent->SetVariableLinearColor(TransformVFXColorParameter,bEntering?YeastTint:DoughTint);
    }
    PlayPlayerBurstVFX(TransformBurstComponent,System,TransformVFXOffset,TransformVFXRotation,TransformVFXScale);
    TransformBurstVFXStopTime=GetWorld()->GetTimeSeconds()+FMath::Max(.01f,TransformBurstVFXMaxSeconds);
    TransformBurstVFXCleanupTime=TransformBurstVFXStopTime+FMath::Max(0.f,TransformBurstVFXTailSeconds);
    if(!bEntering&&TransformExitSound)UDWAudioLibrary::PlayWorldSound(this,TransformExitSound,GetActorLocation());
}
void ADWPlayerCharacter::SetHarvestHeld(bool bHeld)
{
    if(bHarvestHeld&&!bHeld)HarvestElapsed=0.f;
    bHarvestHeld=bHeld&&!IsDead();
    if(!bHarvestHeld)StopGatherFeedback();
}
void ADWPlayerCharacter::CancelHarvestInteraction(){SetHarvestHeld(false);}
void ADWPlayerCharacter::UpdateHarvest(float Dt)
{
    if(bGameplayActionInProgress||bRestoringSave)return;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    ScanElapsed+=Dt;
    if(ScanElapsed>=FMath::Max(0.01f,InteractionScanInterval))
    {
        ScanElapsed=0.f;ADWResourceNode* Best=nullptr;float BestDist=TNumericLimits<float>::Max();
        for(TActorIterator<ADWResourceNode> It(GetWorld());It;++It)
        {if(!It->IsAvailable())continue;const float Dist=FVector::DistSquared2D(GetActorLocation(),It->GetActorLocation());if(Dist<FMath::Square(It->InteractionRadius)&&Dist<BestDist){Best=*It;BestDist=Dist;}}
        if(HarvestTarget.Get()!=Best){HarvestTarget=Best;HarvestElapsed=0.f;}
    }
    auto* Node=HarvestTarget.Get();if(!bHarvestHeld||!Node||bIsRolling||!Node->IsAvailable()){StopGatherFeedback();return;}
    if(FVector::DistSquared2D(GetActorLocation(),Node->GetActorLocation())>FMath::Square(Node->InteractionRadius)){HarvestElapsed=0.f;StopGatherFeedback();return;}
    UpdateGatherFeedback(Node);
    const float Period=FMath::Max(0.05f,Node->HarvestInterval);HarvestElapsed+=Dt;
    while(HarvestElapsed>=Period&&IsValid(Node)&&Node->IsAvailable())
    {
        const int32 Amount=Node->bInfinite?Node->HarvestAmount:FMath::Min(Node->HarvestAmount,Node->RemainingAmount);
        if(Amount<=0)break;
        const FName HarvestedItemId=Node->ItemId;
        if(!Inventory->TryAddItem(HarvestedItemId,Amount))
        {
            HarvestElapsed=Period;
            if(!bGatherFailureLatched)
            {
                bGatherFailureLatched=true;StopGatherFeedback(false,false);
                UDWAudioLibrary::PlayEvent(this,GameplayConfig,EDWAudioEvent::GatherFailed);
            }
            break;
        }
        if(IsValid(Node))Node->CommitHarvest(Amount);
        HarvestElapsed-=Period;
        bGatherFailureLatched=false;
        UDWAudioLibrary::PlayEvent(this,GameplayConfig,EDWAudioEvent::GatherSuccess);
        if(HarvestedItemId==TEXT("Yeast"))Transformation=FMath::Clamp(Transformation+GameplayConfig->MaxTransformation*GameplayConfig->YeastGainPercentPerItem*Amount/100.f,0.f,GameplayConfig->MaxTransformation);
        OnResourceHarvested(IsValid(Node)?Node:nullptr,HarvestedItemId,Amount);
        if(IsActorBeingDestroyed())return;
    }
    if(!IsValid(Node)||!Node->IsAvailable())StopGatherFeedback();
}
bool ADWPlayerCharacter::CanContinueGatherFeedback()const
{
    if(!bHarvestHeld||IsDead()||bIsRolling||bRestoringSave||!GameplayConfig||!GetWorld()||GetWorld()->IsPaused())return false;
    if(const auto* PC=Cast<ADWPlayerController>(GetController()))if(PC->IsGameplayBlocked())return false;
    return GatherFeedbackTarget.IsValid()&&GetFocusedResource()==GatherFeedbackTarget.Get();
}
void ADWPlayerCharacter::UpdateGatherFeedback(ADWResourceNode* Resource)
{
    if(GatherFeedbackTarget.Get()!=Resource){StopGatherFeedback();GatherFeedbackTarget=Resource;}
    if(!CanContinueGatherFeedback()){StopGatherFeedback();return;}
    if(bGatherFeedbackActive||bGatherFailureLatched)return;
    bGatherFeedbackActive=true;
    UDWAudioLibrary::PlayEvent(this,GameplayConfig,EDWAudioEvent::GatherStart);
    GatherLoopComponent=UDWAudioLibrary::CreateGatherLoop(this,GameplayConfig);
    if(GatherLoopComponent)GatherLoopComponent->OnAudioFinished.AddDynamic(this,&ADWPlayerCharacter::HandleGatherLoopFinished);
}
void ADWPlayerCharacter::StopGatherFeedback(bool bPlayStop,bool bResetSession)
{
    const bool bWasActive=bGatherFeedbackActive;
    bGatherFeedbackActive=false;
    if(bResetSession){GatherFeedbackTarget.Reset();bGatherFailureLatched=false;}
    UAudioComponent* Loop=GatherLoopComponent.Get();GatherLoopComponent=nullptr;
    // Stop can fire OnAudioFinished: clear ownership and unbind before stopping to prevent a restart.
    if(IsValid(Loop)){Loop->OnAudioFinished.RemoveAll(this);Loop->Stop();Loop->DestroyComponent();}
    if(bPlayStop&&bWasActive&&!bRestoringSave)UDWAudioLibrary::PlayEvent(this,GameplayConfig,EDWAudioEvent::GatherStop);
}
void ADWPlayerCharacter::HandleGatherLoopFinished()
{
    if(bGatherFeedbackActive&&!bGatherFailureLatched&&CanContinueGatherFeedback()&&IsValid(GatherLoopComponent))
        GatherLoopComponent->Play();
    else StopGatherFeedback(false);
}
ADWResourceNode* ADWPlayerCharacter::GetFocusedResource()const
{
    ADWResourceNode* Node=HarvestTarget.Get();
    if(IsDead()||bRestoringSave||!IsValid(Node)||Node->IsActorBeingDestroyed()||!Node->IsAvailable())return nullptr;
    if(!FMath::IsFinite(Node->InteractionRadius)||Node->InteractionRadius<=0.f)return nullptr;
    const float DistanceSquared=FVector::DistSquared2D(GetActorLocation(),Node->GetActorLocation());
    return FMath::IsFinite(DistanceSquared)&&DistanceSquared<=FMath::Square(Node->InteractionRadius)?Node:nullptr;
}
float ADWPlayerCharacter::GetHarvestProgress()const
{
    const ADWResourceNode* Node=GetFocusedResource();
    return Node&&bHarvestHeld?FMath::Clamp(HarvestElapsed/FMath::Max(0.05f,Node->HarvestInterval),0.f,1.f):0.f;
}
FText ADWPlayerCharacter::GetInteractionPrompt()const
{
    const auto* Node=GetFocusedResource();if(!Node)return FText::GetEmpty();
    const auto* Item=GameplayConfig?GameplayConfig->GetItemDefinition(Node->ItemId):nullptr;
    const FText Name=Item?UDWLocalizationLibrary::GetItemDisplayName(this,*Item):FText::FromName(Node->ItemId);
    const auto* PC=Cast<ADWPlayerController>(GetController());
    const FText Key=PC?PC->GetActionKeyLabel(EDWInputAction::Harvest):FText::FromString(TEXT("F"));
    return FText::Format(DWText(this,TEXT("按住 {0} 采集{1}  ·  {2}%"),TEXT("Hold {0} to gather {1}  ·  {2}%")),Key,Name,FText::AsNumber(FMath::RoundToInt(GetHarvestProgress()*100.f)));
}
void ADWPlayerCharacter::TryTransform()
{
    if(IsDead()||!GameplayConfig||bYeastForm||bGameplayActionInProgress||bRestoringSave)return;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    if(Transformation+KINDA_SMALL_NUMBER<GameplayConfig->MaxTransformation)
    {
        const auto* PC=Cast<ADWPlayerController>(GetController());
        const FText Key=PC?PC->GetActionKeyLabel(EDWInputAction::Transform):FText::FromString(TEXT("E"));
        Notify(FText::Format(DWText(this,TEXT("变身值达到 100% 后按 {0} 变身"),TEXT("Reach 100% transformation, then press {0} to transform.")),Key));return;
    }
    if(!CanEnterYeastForm())return;
    bYeastForm=true;TransformUntil=GetWorld()->GetTimeSeconds()+TransformAnimationSeconds;ShakeElapsed=0.f;
    UpdateFormAppearance();
    PlayFormTransitionVFX(true);
    if(TransformSound)UDWAudioLibrary::PlayWorldSound(this,TransformSound,GetActorLocation());
    if(TransformCameraShake)if(auto* PC=Cast<APlayerController>(GetController()))PC->ClientStartCameraShake(TransformCameraShake);
    Notify(DWText(this,TEXT("酵母形态：已开放合成"),TEXT("Yeast form: crafting unlocked.")));
    OnFormChanged(true);
}
void ADWPlayerCharacter::NotifyAlcoholHit()
{if(GameplayConfig&&!IsDead()&&!bRestoringSave)Transformation=FMath::Clamp(Transformation+GameplayConfig->MaxTransformation*GameplayConfig->AttackGainPercent/100.f,0.f,GameplayConfig->MaxTransformation);}
bool ADWPlayerCharacter::ThrowAlcoholAt(FVector Target)
{
    if(IsDead()||bIsRolling||!AlcoholProjectileClass||!GetWorld()||GetWorld()->GetTimeSeconds()<NextThrowTime||bGameplayActionInProgress||bRestoringSave)return false;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    const int32 Cost=FMath::Max(1,AlcoholCostPerThrow);
    if(Inventory->CountItem(TEXT("Alcohol"))<Cost){Notify(DWText(this,TEXT("酒精不足：疾跑累计或酵母形态合成可获得"),TEXT("Not enough Alcohol. Sprint to produce it, or craft in Yeast form.")));return false;}
    if(!CanThrowAlcohol(Target))return false;
    FVector Delta=Target-GetActorLocation();if(Delta.Size2D()>MaxThrowRange){const FVector D=Delta.GetSafeNormal2D()*MaxThrowRange;Target=GetActorLocation()+D;Target.Z=GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight();}
    const FVector Facing=(Target-GetActorLocation()).GetSafeNormal2D();if(!Facing.IsNearlyZero())SetActorRotation(Facing.Rotation());
    const FVector SpawnLocation=GetActorLocation()+GetActorTransform().TransformVectorNoScale(ThrowOriginOffset);
    FActorSpawnParameters Params;Params.Owner=this;Params.Instigator=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Bottle=GetWorld()->SpawnActor<ADWAlcoholProjectile>(AlcoholProjectileClass,SpawnLocation,GetActorRotation(),Params);if(!Bottle)return false;
    if(!Inventory->TryRemoveItem(TEXT("Alcohol"),Cost)){Bottle->Destroy();return false;}
    Bottle->LaunchAtTarget(Target);NextThrowTime=GetWorld()->GetTimeSeconds()+ThrowCooldown;
    StartAttackAnimation();
    UDWAudioLibrary::PlayWorldSound(this,ThrowSound,SpawnLocation,ThrowSoundVolume,ThrowSoundPitch);
    OnAlcoholThrown(IsValid(Bottle)?Bottle:nullptr,Target,Cost);return true;
}
bool ADWPlayerCharacter::UseInventoryItem(int32 Index)
{
    if(IsDead()||!GameplayConfig||!Inventory||!Inventory->GetSlots().IsValidIndex(Index)||bGameplayActionInProgress||bRestoringSave)return false;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    const auto& Slot=Inventory->GetSlots()[Index];const auto* Def=GameplayConfig->GetItemDefinition(Slot.ItemId);
    if(!Def||!Def->bUsable){Notify(DWText(this,TEXT("此物品用于合成或投掷"),TEXT("This item is used for crafting or throwing.")));return false;}
    if((Def->HealAmount<=0||Health>=GameplayConfig->MaxHealth)&&(Def->TransformationGainPercent<=0||Transformation>=GameplayConfig->MaxTransformation)){Notify(DWText(this,TEXT("当前无需使用此物品"),TEXT("You do not need to use this item right now.")));return false;}
    // OnChanged is a Blueprint extension point too; retain values before it can refresh the data table.
    const FName UsedItemId=Slot.ItemId;const float HealAmount=Def->HealAmount,GainPercent=Def->TransformationGainPercent;
    const float PreviousHealth=Health,PreviousTransformation=Transformation;
    if(!Inventory->ConsumeOneAtSlot(Index))return false;
    Health=FMath::Clamp(Health+HealAmount,0.f,GameplayConfig->MaxHealth);
    Transformation=FMath::Clamp(Transformation+GainPercent*GameplayConfig->MaxTransformation/100.f,0.f,GameplayConfig->MaxTransformation);
    OnInventoryItemUsed(UsedItemId,Health-PreviousHealth,Transformation-PreviousTransformation);return true;
}
bool ADWPlayerCharacter::CraftRecipe(FName RecipeId)
{
    if(IsDead()||!Inventory||bGameplayActionInProgress||bRestoringSave)return false;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    const bool Result=Inventory->TryCraft(RecipeId,bYeastForm);
    Notify(Result?DWText(this,TEXT("制作完成"),TEXT("Crafting complete.")):DWText(this,TEXT("制作失败：检查酵母形态、材料和背包空间"),TEXT("Crafting failed. Check Yeast form, ingredients and inventory space.")));
    if(Result)OnRecipeCrafted(RecipeId);return Result;
}
float ADWPlayerCharacter::TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer)
{
    if(IsDead()||Amount<=0.f||bRestoringSave||UDWWorldEventSubsystem::IsPlaying(this))return 0.f;const float Applied=FMath::Min(Health,Amount);Health-=Applied;if(IsDead())Die();return Applied;
}
void ADWPlayerCharacter::Die()
{
    if(bDeathHandled)return;bDeathHandled=true;ClearHeldActions();GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->DisableMovement();
    if(auto* PC=Cast<APlayerController>(GetController()))if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->ShowDefeat();
    UpdateAnimation();
    if(!bRestoringSave){TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);OnPlayerDied();}
}
void ADWPlayerCharacter::Notify(const FText& Message)const
{if(auto* PC=Cast<APlayerController>(GetController()))if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->Notify(Message);}
void ADWPlayerCharacter::RequestSave()
{StopMovementAudio();if(auto* GI=GetGameInstance<UDWGameInstance>()){const bool Ok=GI->SaveCurrentGame();Notify(Ok?DWText(this,TEXT("存档已保存"),TEXT("Game saved.")):GI->LastSaveError);}}
void ADWPlayerCharacter::StartAttackAnimation()
{
    AttackUntil=0.f;bRestartAttackAnimation=false;ActiveAttackClip=AttackAnimation;ActiveAttackStartSeconds=0.f;
    if(!AttackAnimation||!GetMesh()||!GetWorld())return;
    ActiveAttackAnimationRate=FMath::IsFinite(AttackAnimationPlayRate)?FMath::Clamp(AttackAnimationPlayRate,.05f,4.f):1.f;
    float Duration=FMath::IsFinite(AttackAnimationSeconds)?FMath::Max(.05f,AttackAnimationSeconds):.55f;
    if(const auto* Clip=Cast<UAnimSequenceBase>(AttackAnimation))
    {
        // The engine multiplies SingleNode play rate by the source asset's RateScale.
        // Preserve that authored multiplier, but never squeeze the whole clip into .55/.65 seconds.
        const float SourceRate=FMath::IsFinite(Clip->RateScale)?Clip->RateScale:1.f;
        if(SourceRate<0.f)ActiveAttackAnimationRate=-ActiveAttackAnimationRate;
        const float EffectiveRate=FMath::Abs(ActiveAttackAnimationRate*SourceRate);
        const float Length=Clip->GetPlayLength();
        if(FMath::IsFinite(Length)&&Length>0.f&&EffectiveRate>KINDA_SMALL_NUMBER)
        {
            const float MinSegment=FMath::Min(.001f,Length);
            ActiveAttackStartSeconds=FMath::IsFinite(AttackAnimationStartSeconds)
                ? FMath::Clamp(AttackAnimationStartSeconds,0.f,Length-MinSegment):0.f;
            const float End=FMath::IsFinite(AttackAnimationEndSeconds)&&AttackAnimationEndSeconds>0.f
                ? FMath::Clamp(AttackAnimationEndSeconds,ActiveAttackStartSeconds+MinSegment,Length):Length;
            Duration=(End-ActiveAttackStartSeconds)/EffectiveRate;
        }
    }
    AttackUntil=GetWorld()->GetTimeSeconds()+Duration;
    bRestartAttackAnimation=true;
    UpdateAnimation();
}
float ADWPlayerCharacter::GetAttackAnimationPositionSeconds()const
{
    const auto* Anim=GetMesh()?GetMesh()->GetSingleNodeInstance():nullptr;
    return bPlayingAttackAnimation&&Anim&&Anim->GetAnimationAsset()==ActiveAttackClip?Anim->GetCurrentTime():-1.f;
}
void ADWPlayerCharacter::UpdateAnimation()
{
    if(!GetMesh()||!GetWorld())return;
    UAnimationAsset* Wanted=IdleAnimation;bool bLoop=true;const float Now=GetWorld()->GetTimeSeconds();float Rate=1.f;
    bPlayingAttackAnimation=false;
    if(IsDead()){Wanted=DeathAnimation;bLoop=false;}
    else if(Now<TransformUntil&&TransformAnimation){Wanted=TransformAnimation;bLoop=false;if(const auto* Clip=Cast<UAnimSequenceBase>(Wanted))Rate=Clip->GetPlayLength()/FMath::Max(.05f,TransformAnimationSeconds);}
    else if(Now<AttackUntil&&ActiveAttackClip){Wanted=ActiveAttackClip;bLoop=false;Rate=ActiveAttackAnimationRate;bPlayingAttackAnimation=true;}
    else if(bIsRolling&&DashAnimation){Wanted=DashAnimation;bLoop=false;}
    else if(GetVelocity().SizeSquared2D()>25.f){Wanted=WalkAnimation;Rate=bSprinting?GameplayConfig->SprintSpeedMultiplier:1.f;}
    auto* Existing=GetMesh()->GetSingleNodeInstance();
    const bool bStartAttack=bPlayingAttackAnimation&&bRestartAttackAnimation;
    if(Wanted&&(Wanted!=ActiveAnimation||!Existing||Existing->GetAnimationAsset()!=Wanted||bStartAttack))
    {
        GetMesh()->PlayAnimation(Wanted,bLoop);ActiveAnimation=Wanted;
        if(bStartAttack)
        {
            if(auto* Anim=GetMesh()->GetSingleNodeInstance())Anim->SetPosition(ActiveAttackStartSeconds,false);
            bRestartAttackAnimation=false;++AttackAnimationPlayCount;
        }
    }
    if(auto* Anim=GetMesh()->GetSingleNodeInstance())
    {
        // Also enforce the one-shot flag when an asset stays the same across states.
        // Only a new accepted throw restarts it; Tick never resets its play position.
        Anim->SetLooping(bLoop);Anim->SetPlayRate(Rate);
    }
    if(Now>=AttackUntil||IsDead())bRestartAttackAnimation=false;
}
void ADWPlayerCharacter::InitializeFormVisuals()
{
    if(bFormVisualsInitialized||!GetMesh())return;
    bFormVisualsInitialized=true;DoughMeshScale=GetMesh()->GetRelativeScale3D();
    FormBoneFinalizedHandle=GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(
        FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this,&ADWPlayerCharacter::HandleFormBonesFinalized));
}
void ADWPlayerCharacter::UpdateFormAppearance(bool bImmediate)
{
    InitializeFormVisuals();
    FormBlendStartAlpha=CurrentFormBlendAlpha;FormBlendTargetAlpha=bYeastForm?1.f:0.f;FormBlendElapsed=0.f;
    const float EnterSeconds=bMatchFormEnterBlendToAnimation?TransformAnimationSeconds:FormEnterBlendSeconds;
    FormBlendDuration=FMath::Max(.01f,bYeastForm?EnterSeconds:FormExitBlendSeconds);
    if(bImmediate){CurrentFormBlendAlpha=FormBlendTargetAlpha;FormBlendStartAlpha=CurrentFormBlendAlpha;FormBlendElapsed=FormBlendDuration;}
    TickFormAppearance(0.f);
}
void ADWPlayerCharacter::TickFormAppearance(float DeltaSeconds)
{
    InitializeFormVisuals();if(!bFormVisualsInitialized)return;
    FormBlendElapsed=FMath::Min(FormBlendDuration,FormBlendElapsed+FMath::Max(0.f,DeltaSeconds));
    const float TimeAlpha=FMath::Clamp(FormBlendElapsed/FMath::Max(.01f,FormBlendDuration),0.f,1.f);
    const float SmoothAlpha=FMath::InterpEaseInOut(0.f,1.f,TimeAlpha,FMath::Max(1.f,FormBlendExponent));
    CurrentFormBlendAlpha=FMath::Lerp(FormBlendStartAlpha,FormBlendTargetAlpha,SmoothAlpha);
    CurrentFormVisualScale=FMath::Lerp(1.f,FMath::Max(.1f,YeastFormScaleMultiplier),CurrentFormBlendAlpha);
    const FLinearColor Tint=FMath::Lerp(DoughTint,YeastTint,CurrentFormBlendAlpha);
    for(int32 I=0;I<GetMesh()->GetNumMaterials();++I)
    {
        auto* MID=Cast<UMaterialInstanceDynamic>(GetMesh()->GetMaterial(I));
        if(!MID)MID=GetMesh()->CreateDynamicMaterialInstance(I);
        if(MID)MID->SetVectorParameterValue(TEXT("FormTint"),Tint);
    }
    ApplyFormMeshScale();
}
void ADWPlayerCharacter::HandleFormBonesFinalized()
{
    // A newly selected clip can coexist with the previous clip's cached pose for one frame.
    // Compensate the observed bone transforms, never the animation asset's requested identity.
    ApplyFormMeshScale();
}
void ADWPlayerCharacter::ApplyFormMeshScale()
{
    if(!bFormVisualsInitialized||bApplyingFormScale||!GetMesh())return;
    TGuardValue<bool> ScaleGuard(bApplyingFormScale,true);
    CurrentAnimationRootScale=FVector::OneVector;
    if(bCompensateTransformRootScale)
    {
        const USkeletalMesh* Asset=GetMesh()->GetSkeletalMeshAsset();
        const int32 BoneIndex=GetMesh()->GetBoneIndex(FormScaleRootBone);
        if(Asset&&BoneIndex!=INDEX_NONE&&GetMesh()->GetComponentSpaceTransforms().IsValidIndex(BoneIndex))
        {
            const FReferenceSkeleton& Skeleton=Asset->GetRefSkeleton();
            const auto& ReferencePose=Skeleton.GetRefBonePose();
            if(ReferencePose.IsValidIndex(BoneIndex))
            {
                FTransform ReferenceCS=ReferencePose[BoneIndex];
                for(int32 Parent=Skeleton.GetParentIndex(BoneIndex);Parent!=INDEX_NONE;Parent=Skeleton.GetParentIndex(Parent))
                    ReferenceCS=ReferenceCS*ReferencePose[Parent];
                const FVector AuthoredScale=GetMesh()->GetBoneTransform(BoneIndex,FTransform::Identity).GetScale3D();
                const FVector ReferenceScale=ReferenceCS.GetScale3D();
                for(int32 Axis=0;Axis<3;++Axis)
                    if(FMath::IsFinite(AuthoredScale[Axis])&&FMath::Abs(ReferenceScale[Axis])>KINDA_SMALL_NUMBER)
                        CurrentAnimationRootScale[Axis]=FMath::Max(.01,AuthoredScale[Axis]/ReferenceScale[Axis]);
            }
        }
    }
    // Scale only the visual mesh. Capsule, movement speeds, interaction range, camera and
    // the repaired mesh rotation/location stay unchanged; the hand-attached sword follows.
    const FVector DesiredScale=DoughMeshScale*CurrentFormVisualScale/CurrentAnimationRootScale;
    if(!GetMesh()->GetRelativeScale3D().Equals(DesiredScale,UE_KINDA_SMALL_NUMBER))GetMesh()->SetRelativeScale3D(DesiredScale);
}
namespace {FString DWActorSaveId(const TCHAR* Prefix,const AActor* A,FName Id){return FString(Prefix)+(Id.IsNone()?A->GetName():Id.ToString());}}
void ADWPlayerCharacter::CaptureSaveData(UDWSaveGame* Save)const
{
    if(!Save)return;Save->CompletedWorldEvents=GetWorld()->GetSubsystem<UDWWorldEventSubsystem>()->ExportCompleted();Save->PlayerTransform=GetActorTransform();Save->bHasPlayerTransform=true;Save->Health=Health;Save->Transformation=Transformation;Save->bYeastForm=bYeastForm;
    Save->SprintAlcoholElapsed=SprintAlcoholElapsed;Save->TransformationDecayElapsed=TransformationDecayElapsed;Save->Inventory=Inventory->GetSlots();Save->WorldActors.Reset();
    for(TActorIterator<ADWResourceNode> It(GetWorld());It;++It)
    {FDWPersistedActorState S;S.ActorId=DWActorSaveId(TEXT("Resource:"),*It,It->PersistentId);S.Health=It->RemainingAmount;S.bDestroyed=!It->IsAvailable();S.Transform=It->GetActorTransform();S.bHasTransform=true;Save->WorldActors.Add(S);}
    for(TActorIterator<ADWEnemyNest> It(GetWorld());It;++It)
    {FDWPersistedActorState S;S.ActorId=DWActorSaveId(TEXT("Nest:"),*It,It->PersistentId);S.Health=It->Health;S.bDestroyed=It->Health<=0;S.Transform=It->GetActorTransform();S.bHasTransform=true;S.SpawnProgress=It->SpawnProgress;Save->WorldActors.Add(S);}
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
    {if(It->IsActorBeingDestroyed())continue;FDWPersistedActorState S;S.ActorId=DWActorSaveId(TEXT("Enemy:"),*It,It->PersistentId);S.Health=It->Health;S.bDestroyed=It->Health<=0;S.Transform=It->GetActorTransform();S.bHasTransform=true;
     if(auto* Nest=It->SourceNest.Get()){if(!It->IsAlive())continue;S.bRuntimeSpawned=true;S.ActorClass=FSoftClassPath(It->GetClass());S.OwnerId=DWActorSaveId(TEXT("Nest:"),Nest,Nest->PersistentId);}
     Save->WorldActors.Add(S);}
}
void ADWPlayerCharacter::ApplySaveData(const UDWSaveGame* Save)
{
    if(!Save||!GameplayConfig||!Inventory||bRestoringSave||bGameplayActionInProgress)return;
    TGuardValue<bool> RestoreGuard(bRestoringSave,true);
    StopGatherFeedback(false);
    StopPlayerVFX(true);
    StopMovementAudio();bHasVFXLocationSample=false;CurrentVFXGroundSpeed=0.f;
    if(!Inventory->SetSlots(Save->Inventory))return;
    Health=FMath::Clamp(Save->Health,0.f,GameplayConfig->MaxHealth);Transformation=FMath::Clamp(Save->Transformation,0.f,GameplayConfig->MaxTransformation);bYeastForm=Save->bYeastForm&&Transformation>0;
    SprintAlcoholElapsed=FMath::Max(0.f,Save->SprintAlcoholElapsed);TransformationDecayElapsed=FMath::Max(0.f,Save->TransformationDecayElapsed);
    // Apply permanent state before teleporting: teleport can synchronously fire trigger overlaps.
    GetWorld()->GetSubsystem<UDWWorldEventSubsystem>()->ImportCompleted(Save->CompletedWorldEvents);
    if(Save->bHasPlayerTransform)SetActorTransform(Save->PlayerTransform,false,nullptr,ETeleportType::TeleportPhysics);
    // Replace only nest-owned runtime enemies; placed enemies and user scenery stay intact.
    TArray<ADWEnemyCharacter*> OldSpawned;for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)if(It->SourceNest)OldSpawned.Add(*It);
    for(auto* E:OldSpawned)E->Destroy();
    for(const auto& S:Save->WorldActors)
    {
        for(TActorIterator<ADWResourceNode> It(GetWorld());It;++It)if(S.ActorId==DWActorSaveId(TEXT("Resource:"),*It,It->PersistentId)){It->RestoreRemainingAmount(FMath::Max(0,FMath::RoundToInt(S.Health)));if(S.bHasTransform)It->SetActorTransform(S.Transform);}
        for(TActorIterator<ADWEnemyNest> It(GetWorld());It;++It)if(S.ActorId==DWActorSaveId(TEXT("Nest:"),*It,It->PersistentId)){It->SetHealthForLoad(S.bDestroyed?0:S.Health);It->SpawnProgress=FMath::Clamp(S.SpawnProgress,0.f,It->SpawnInterval);if(S.bHasTransform)It->SetActorTransform(S.Transform);}
        if(!S.bRuntimeSpawned)for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)if(!It->IsActorBeingDestroyed()&&S.ActorId==DWActorSaveId(TEXT("Enemy:"),*It,It->PersistentId)){It->SetHealthForLoad(S.bDestroyed?0:S.Health);if(S.bHasTransform)It->SetActorTransform(S.Transform,false,nullptr,ETeleportType::TeleportPhysics);}
        if(S.bRuntimeSpawned&&!S.bDestroyed&&S.bHasTransform){ADWEnemyNest* SavedNest=nullptr;for(TActorIterator<ADWEnemyNest> It(GetWorld());It;++It)if(S.OwnerId==DWActorSaveId(TEXT("Nest:"),*It,It->PersistentId)){SavedNest=*It;break;}
         UClass* C=S.ActorClass.TryLoadClass<ADWEnemyCharacter>();if(SavedNest&&C){auto* E=GetWorld()->SpawnActorDeferred<ADWEnemyCharacter>(C,S.Transform,SavedNest,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);if(E){E->SourceNest=SavedNest;E->SetHealthForLoad(S.Health);E->FinishSpawning(S.Transform);SavedNest->RegisterRestoredEnemy(E);}}}

    }
    UpdateFormAppearance(true);if(IsDead())Die();
}

bool ADWPlayerCharacter::CanSprint_Implementation()const{return true;}
bool ADWPlayerCharacter::CanDash_Implementation()const{return true;}
bool ADWPlayerCharacter::CanThrowAlcohol_Implementation(FVector TargetLocation)const{return true;}
bool ADWPlayerCharacter::CanEnterYeastForm_Implementation()const{return true;}
