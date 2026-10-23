#include "DWPlayerCharacter.h"
#include "DWGatheringComponent.h"
#include "DWCombatHUDModel.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatResourceVerificationLibrary.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWCombatReleasedEffects.h"
#include "DWCombatPlayerStateTransaction.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>
#include "Misc/ScopeExit.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatFeedbackComponent.h"
#include "DWDamageVolumeLibrary.h"
#include "DWLoadingTransition.h"
#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/DamageType.h"
#include "DrawDebugHelpers.h"
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
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"

ADWPlayerCharacter::ADWPlayerCharacter()
{
    GameplayCameraShake=CreateDefaultSubobject<UDWGameplayCameraShakeComponent>(TEXT("GameplayCameraShake"));
    Inventory=CreateDefaultSubobject<UDWInventoryComponent>(TEXT("DoughWorldInventory"));
    GatheringComponent=CreateDefaultSubobject<UDWGatheringComponent>(TEXT("GatheringComponent"));
    WeaponAnimationComponent=CreateDefaultSubobject<UDWWeaponAnimationComponent>(TEXT("WeaponAnimationComponent"));
    CombatFeedback=CreateDefaultSubobject<UDWCombatFeedbackComponent>(TEXT("CombatFeedback"));
    SwordPlaceholder=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SwordPlaceholder"));
    SwordPlaceholder->SetupAttachment(GetMesh());SwordPlaceholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);SwordPlaceholder->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if(Cube.Succeeded())SwordPlaceholder->SetStaticMesh(Cube.Object);
    SwordPlaceholder->SetRelativeScale3D(FVector(.065f,.065f,.45f));SwordPlaceholder->SetRelativeLocation(FVector(0,0,20.f));
    MeleeDamageVolume=CreateDefaultSubobject<UBoxComponent>(TEXT("MeleeDamageVolume"));
    MeleeDamageVolume->SetupAttachment(GetRootComponent());
    MeleeDamageVolume->SetAbsolute(false,false,true);
    // This is query geometry, never a player hurtbox; native actors may use fallback collision rules.
    MeleeDamageVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    MeleeDamageVolume->SetCollisionObjectType(ECC_WorldDynamic);
    MeleeDamageVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    MeleeDamageVolume->SetGenerateOverlapEvents(false);
    MeleeDamageVolume->SetCanEverAffectNavigation(false);
    MeleeDamageVolume->SetHiddenInGame(true);
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
    UpdateAnimatedMeleeWeaponVisibility();
    if(auto* GI=GetGameInstance<UDWGameInstance>())GameplayConfig=GI->GetConfig();
    if(!GameplayConfig)GameplayConfig=NewObject<UDWGameplayConfig>(this);
    GameplayConfig->RefreshDefinitionsFromTables();Inventory->InitializeInventory(GameplayConfig);
    if(!GetMesh()->DoesSocketExist(SwordSocket)&&GetMesh()->DoesSocketExist(TEXT("bone.005.R")))SwordSocket=TEXT("bone.005.R");
    if(GetMesh()->DoesSocketExist(SwordSocket))SwordPlaceholder->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,SwordSocket);
    else SwordPlaceholder->SetVisibility(false);
    InitializeCombatVitals();
    if(GatheringComponent)GatheringComponent->InitializeGathering();
    if(IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)return;
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
    CameraDistance=ClampCameraDistance(CameraDistance);
    CameraArm->TargetArmLength=CameraDistance;
    // The title pauses the world before its first tick, so initialize the view immediately.
    CameraArm->SetWorldRotation(FRotator(CameraPitch,CameraYaw,0));
    InitializeFormVisuals();
    if(auto* GI=GetGameInstance<UDWGameInstance>())GI->ApplyPendingSave(this);
    CameraArm->TickComponent(0.f,LEVELTICK_All,nullptr);
    UpdateFormAppearance(true);UpdateAnimation();
    PreviousVFXLocation=GetActorLocation();bHasVFXLocationSample=true;
    UDWCombatResourceVerificationLibrary::ApplyCombatTestAmmo(this);
}
void ADWPlayerCharacter::PrepareCameraForReveal()
{
 if(!HasActorBegunPlay()||!CameraArm)return;
 const bool Lag=CameraArm->bEnableCameraLag,RotLag=CameraArm->bEnableCameraRotationLag;
 CameraArm->bEnableCameraLag=false;CameraArm->bEnableCameraRotationLag=false;
 CameraArm->SetWorldRotation(FRotator(CameraPitch,CameraYaw,0));
  CameraDistance=ClampCameraDistance(CameraDistance);
  CameraArm->TargetArmLength=CameraDistance;
 CameraArm->TickComponent(0.f,LEVELTICK_All,nullptr);
 CameraArm->bEnableCameraLag=Lag;CameraArm->bEnableCameraRotationLag=RotLag;
}
void ADWPlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    EndFormalRootFacts(EDWMoldRootEnd::EndPlay);
    ResetCombatKnockbackFacts();
    CancelCombatActions();
    StopGatherFeedback(false);
    StopMovementAudio(true);
    StopPlayerVFX(true);
    if(GetMesh()&&FormBoneFinalizedHandle.IsValid())GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(FormBoneFinalizedHandle);
    FormBoneFinalizedHandle.Reset();
    Super::EndPlay(EndPlayReason);
}
void ADWPlayerCharacter::UnPossessed()
{
    ResetCombatKnockbackFacts();
    if(auto* PC=Cast<ADWPlayerController>(GetController()))PC->InvalidateCombatInputLife();
    CancelCombatActions();
    Super::UnPossessed();
}

void ADWPlayerCharacter::Tick(float Dt)
{
    if(auto* PC=Cast<ADWPlayerController>(GetController()))PC->PrepareCombatInputForSimulation();
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    if(!TryGiveCombatTestWeapons())return;
    if(!TryGiveCombatTestCores())return;
#endif
    if(!PublishFormalRootMovementEffects()||!AdvanceCombatKnockback(Dt))return;
    Super::Tick(Dt);
    TickGameplayClocks(Dt,false);
    if(IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)return;
    UpdateCombat(Dt);
    UpdateAnimatedMeleeWeaponVisibility();
    if(!GameplayConfig){StopPlayerVFX(true);StopMovementAudio();bHasVFXLocationSample=false;CurrentVFXGroundSpeed=0.f;return;}
    bSprinting=false; // Formal combat has no generic sprint or sprint-produced alcohol.
    if(!IsDead())UpdateHarvest(Dt);
    UpdatePlayerVFX(Dt);
    // All movement effects now use the retained Niagara components; no mesh actors are spawned.
    if(CameraArm)
    {
        CameraDistance=ClampCameraDistance(CameraDistance);
        const auto* CameraController=Cast<ADWPlayerController>(GetController());
        if(CameraController&&CameraController->IsCameraInputAllowed())
        {
            const float Speed=FMath::IsFinite(CameraZoomInterpSpeed)?FMath::Max(0.f,CameraZoomInterpSpeed):10.f;
            const float Seconds=FMath::IsFinite(Dt)?FMath::Max(0.f,Dt):0.f;
            const float Alpha=Speed>0.f?1.f-FMath::Exp(-Speed*Seconds):1.f;
            const float Current=ClampCameraDistance(CameraArm->TargetArmLength);
            CameraArm->TargetArmLength=FMath::Lerp(Current,CameraDistance,Alpha);
        }
        ShakeElapsed+=Dt;const float Alpha=FMath::Clamp(1.f-ShakeElapsed/FMath::Max(0.01f,TransformShakeSeconds),0.f,1.f);
        const FVector Shake(0,FMath::Sin(ShakeElapsed*TransformShakeFrequency)*TransformShakeMagnitude*Alpha,FMath::Cos(ShakeElapsed*TransformShakeFrequency*1.2f)*TransformShakeMagnitude*Alpha);
        CameraArm->SocketOffset=CameraRestOffset+Shake;CameraArm->SetWorldRotation(FRotator(CameraPitch,CameraYaw,0));
    }
    UpdateAnimation();
    TickFormAppearance(Dt);
}
void ADWPlayerCharacter::InitializeCombatVitals()
{
    Health=0.f;GetCharacterMovement()->MaxWalkSpeed=0.f;
    auto* GI=GetGameInstance<UDWGameInstance>();FString Error;FDWCombatPlayerVitalsConfig Config;
    if(!GI||!GI->GetCombatRules()||!FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),Config,Error)||
        !Inventory||!Inventory->TryApplyCombatStackPolicy(*GI->GetCombatRules(),Error))
    {Health=0.f;GetCharacterMovement()->MaxWalkSpeed=0.f;UE_LOG(LogTemp,Error,TEXT("D03 player rules unavailable: %s"),*Error);return;}
    int32 CoreLimit=0;
    if(IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown||GetGameInstance<UDWGameInstance>()!=GI||!Inventory||Inventory->IsBeingDestroyed()||
        !GI->GetCombatRules()->TryGetCount(TEXT("core.stack_limit"),CoreLimit,Error)||
        Inventory->GetEffectiveStackLimit(FDWCombatItemRegistry::MoldCoreId())!=CoreLimit||
        Inventory->GetEffectiveStackLimit(FDWCombatItemRegistry::YeastCoreId())!=CoreLimit||
        !CombatVitals.Initialize(Config,EDWCombatVitalsReset::NewCharacter))return;
    Health=DWCombatPlayerSave::NativeHealth(Config.Vitals.HealthMax*Config.Vitals.InitialHealthFraction,Config);
    SyncCombatVitalsCompatibility();
}
float ADWPlayerCharacter::GetCombatHealthFloor() const
{return HasCombatVitalsReady()?DWCombatPlayerSave::HealthFloor(CombatVitals.GetConfig()):0.f;}
bool ADWPlayerCharacter::IsDead() const {return !FMath::IsFinite(Health)||Health<=GetCombatHealthFloor();}
void ADWPlayerCharacter::SyncCombatVitalsCompatibility()
{
    Transformation=static_cast<float>(CombatVitals.GetSpecial());bYeastForm=GetCombatBaseForm()==EDWCombatBaseForm::Yeast;
    if(WeaponAnimationComponent)WeaponAnimationComponent->NotifyCombatBaseFormChanged(static_cast<uint8>(GetCombatBaseForm()));
    SprintAlcoholElapsed=TransformationDecayElapsed=0.f;bSprinting=false;
    RefreshCombatMovementSpeed();
}
void ADWPlayerCharacter::RefreshCombatMovementSpeed()
{
    if(!HasCombatVitalsReady()||!GetCharacterMovement())return;
    const double FormSpeed=CombatVitals.GetConfig().BaseMoveCentimetersPerSecond*
        (IsFormallyRooted()?0.:(GetCombatBaseForm()==EDWCombatBaseForm::Yeast?CombatVitals.GetConfig().YeastMoveMultiplier:1.));
    const double ReloadMultiplier=WeaponAnimationComponent?WeaponAnimationComponent->GetReloadMovementMultiplier():1.;
    GetCharacterMovement()->MaxWalkSpeed=static_cast<float>(FormSpeed*ReloadMultiplier*GetGatheringEnvironmentMovementMultiplier());
}
double ADWPlayerCharacter::GetCombatIncomingMultiplier() const
{
    const auto& C=CombatVitals.GetConfig();
    if(IsFormallyRooted())return FormalRoot.GetConfig().IncomingMultiplier;
    if(GetCombatBaseForm()==EDWCombatBaseForm::Yeast)return C.YeastIncomingMultiplier;
    if(GetCombatBaseForm()==EDWCombatBaseForm::Mold)return C.MoldIncomingMultiplier;
    return C.NormalIncomingMultiplier;
}
bool ADWPlayerCharacter::CanReceiveCombatDamage() const
{return HasCombatVitalsReady()&&!IsDead()&&!bCombatRecoveryPositionPending&&!bRestoringSave&&!IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown&&
    !IsCombatSimulationSuspended()&&!UDWWorldEventSubsystem::IsPlaying(this);}
EDWCombatBaseForm ADWPlayerCharacter::GetCombatBaseFormAtFrameOffset(double Seconds) const
{
    if(!bHasCombatFormFrameSlice||!FMath::IsFinite(Seconds)||Seconds<0.)return GetCombatBaseForm();
    auto* A=UDWCombatAuthoritySubsystem::Get(this);
    if(!A)return GetCombatBaseForm();
    const auto Life=A->CaptureActorLife(const_cast<ADWPlayerCharacter*>(this));
    if(Life.Entity!=CombatFrameLife.Entity||Life.Life!=CombatFrameLife.Life)return GetCombatBaseForm();
    // Slice is invalidated by restore/death before any subsequent animation callback.
    return Seconds>=CombatFrameExpiryOffset?EDWCombatBaseForm::Normal:CombatFrameStartForm;
}
double ADWPlayerCharacter::GetCombatFrameSliceStartWorldSeconds(double DeltaSeconds) const
{
    if(bHasCombatFormFrameSlice)return CombatSliceStartWorldSeconds;
    return (GetWorld()?GetWorld()->GetTimeSeconds():0.)-(FMath::IsFinite(DeltaSeconds)?FMath::Max(0.,DeltaSeconds):0.);
}
void ADWPlayerCharacter::TickGameplayClocks(float Dt,bool bActuallySprinting)
{
    if(!FMath::IsFinite(Dt)||Dt<0.f||!HasCombatVitalsReady()||!IsCombatActionAllowed()||bGameplayActionInProgress||bAdvancingGameplayClocks)return;
    TGuardValue<bool> ClockGuard(bAdvancingGameplayClocks,true);
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return;
    const TWeakObjectPtr<UWorld> ClockWorld(GetWorld());
    const uint64 ClockEpoch=Authority->GetWorldEpoch();
    const auto ClockLife=Authority->CaptureActorLife(this);if(!ClockLife.Entity)return;
    const bool bHadBegunPlay=HasActorBegunPlay();
    auto IsCurrent=[&]()
    {
        if(!IsValid(this)||IsActorBeingDestroyed()||!ClockWorld.IsValid()||GetWorld()!=ClockWorld.Get()||
            ClockWorld->bIsTearingDown||(bHadBegunPlay&&!HasActorBegunPlay()))return false;
        Authority=UDWCombatAuthoritySubsystem::Get(this);
        if(!Authority||Authority->GetWorldEpoch()!=ClockEpoch)return false;
        const auto Life=Authority->CaptureActorLife(this);
        return Life.Entity==ClockLife.Entity&&Life.Life==ClockLife.Life&&!IsDead()&&!bRestoringSave;
    };
    ON_SCOPE_EXIT { bHasCombatFormFrameSlice=false; };
    double Remaining=Dt,Consumed=0.;
    const double FrameStart=ClockWorld->GetTimeSeconds()-static_cast<double>(Dt);
    int32 ConsecutiveZeroBoundaries=0;
    bool bStopWeaponCarry=false;
    const uint64 CoreSerialAtClockStart=CoreInjection.Plan.GetSerial();
    auto AdvancePaidWeaponSlice=[&](bool bCreatedThisFrame)
    {
        if(!PaidCombatWeaponSlice.bPending)return true;
        // Real Tick calls PrepareCombatInputForSimulation before this function: a paused release
        // cancels/changes the action before it can consume this already-paid continuation.
        const FPaidCombatWeaponSlice Paid=PaidCombatWeaponSlice;
        PaidCombatWeaponSlice={};bHasCombatFormFrameSlice=false;
        const auto* W=Paid.Weapon.Get();
        const bool bSameAction=Paid.World==ClockWorld&&Paid.WorldEpoch==ClockEpoch&&
            Paid.Life.Entity==ClockLife.Entity&&Paid.Life.Life==ClockLife.Life&&
            W&&!W->IsBeingDestroyed()&&WeaponAnimationComponent.Get()==W&&W->ActionSerial==Paid.ActionSerial&&
            static_cast<uint8>(W->SelectedWeapon)==Paid.WeaponSelection&&W->GetActiveRole()==Paid.Role&&W->IsBusy()==Paid.bWasBusy&&
            W->GetCombatBoundaryRevision()==Paid.BoundaryRevision;
        if(!bSameAction){if(bCreatedThisFrame)bStopWeaponCarry=true;return true;}
        CombatFrameStartForm=Paid.StartForm;CombatFrameLife=Paid.Life;CombatSliceStartWorldSeconds=Paid.StartWorldSeconds;
        CombatFrameExpiryOffset=Paid.FormExpiryOffset;
        bCombatFrameStartRooted=Paid.bStartRooted;CombatFrameRootExitOffset=Paid.RootExitOffset;
        bHasCombatFormFrameSlice=true;
        const bool bCompleted=Paid.Weapon->AdvanceAnimation(Paid.Seconds);
        bHasCombatFormFrameSlice=false;
        if(!IsCurrent()||!IsCombatActionAllowed())return false;
        if(!bCompleted)bStopWeaponCarry=true;
        return true;
    };
    // Resource callbacks may pause before the corresponding weapon clock reaches the same point.
    // Resume that fixed old slice first, without consuming a second unit of resource time.
    if(!PumpFormalDashSlice()||!IsCurrent()||!IsCombatActionAllowed())return;
    Authority->ResolveShieldDamageAt(this,PaidCombatWeaponSlice.bPending?
        PaidCombatWeaponSlice.StartWorldSeconds+PaidCombatWeaponSlice.Seconds:FrameStart);
    if(!IsCurrent()||!IsCombatActionAllowed())return;
    if(!AdvancePaidWeaponSlice(false))return;
    while(IsCurrent()&&IsCombatActionAllowed())
    {
        const TWeakObjectPtr<UDWWeaponAnimationComponent> Weapon(WeaponAnimationComponent);
        const bool bAdvanceWeapon=!bStopWeaponCarry&&!IsFormalDashing()&&Weapon.IsValid()&&!Weapon->IsBeingDestroyed();
        if(IsCoreInjecting()&&!IsCoreInjectionCurrent())CancelCoreInjectionFacts();
        const bool bAdvanceCore=IsCoreInjecting()&&CoreInjection.Plan.GetSerial()==CoreSerialAtClockStart;
        const double CoreBoundary=bAdvanceCore?CoreInjection.Plan.GetNextBoundarySeconds():std::numeric_limits<double>::infinity();
        const double SliceStart=FrameStart+Consumed;
        const double ShieldBoundary=Authority->GetNextShieldDamageBoundary(this,SliceStart);
        const double WeaponBoundary=bAdvanceWeapon?Weapon->GetNextCombatBoundarySeconds(SliceStart):std::numeric_limits<double>::infinity();
        if(WeaponBoundary<0.||FMath::IsNaN(WeaponBoundary))
        {UE_LOG(LogTemp,Error,TEXT("Combat clock rejected an invalid weapon boundary."));return;}
        bool bDashWall=false;
        const double DashBoundary=GetFormalDashBoundary(Remaining,bDashWall);
        if(Remaining<=0.&&WeaponBoundary!=0.&&CoreBoundary!=0.&&DashBoundary!=0.&&ShieldBoundary!=0.)return;
        if(ShieldBoundary==0.)
        {
            const uint64 BeforeShield=Authority->GetShieldDamageRevision();Authority->ResolveShieldDamageAt(this,SliceStart);
            if(!IsCurrent()||!IsCombatActionAllowed())return;
            if(Authority->GetShieldDamageRevision()==BeforeShield){UE_LOG(LogTemp,Error,TEXT("Shield deadline made no progress."));return;}
            continue;
        }
        if(IsFormalDashing()&&DashBoundary==0.)
        {
            FDWYeastDashStep Zero;
            if(bDashWall)EndFormalDashFacts(EDWYeastDashEnd::Wall);
            else if(FormalDash.Advance(0.,GetCombatSpecial(),GetCombatFormRemaining(),Zero)&&Zero.End!=EDWYeastDashEnd::None)EndFormalDashFacts(Zero.End);
            else{UE_LOG(LogTemp,Error,TEXT("Dash clock rejected a zero boundary without a reason."));return;}
            continue;
        }
        if(CoreBoundary==0.)
        {
            CompleteCoreInjectionAtBoundary();
            if(!IsCurrent()||!IsCombatActionAllowed())return;
            // New actions started by form/inventory observers cannot consume this old frame's carry.
            bStopWeaponCarry=true;bHasCombatFormFrameSlice=false;
            continue;
        }
        CombatFrameStartForm=GetCombatBaseForm();CombatFrameLife=ClockLife;CombatSliceStartWorldSeconds=SliceStart;
        CombatFrameExpiryOffset=CombatFrameStartForm==EDWCombatBaseForm::Normal?std::numeric_limits<double>::infinity():CombatVitals.GetFormRemaining();
        bCombatFrameStartRooted=IsFormallyRooted();
        CombatFrameRootExitOffset=bCombatFrameStartRooted?
            FMath::Min(CombatFrameExpiryOffset,FormalRoot.EstimateResourceBoundarySeconds(GetCombatSpecial())):0.;
        bHasCombatFormFrameSlice=true;
        if(WeaponBoundary==0.)
        {
            // A real event at the present timestamp consumes no resource time. Never invent epsilon time.
            if(++ConsecutiveZeroBoundaries>256)
            {UE_LOG(LogTemp,Error,TEXT("Combat clock stopped a non-terminating zero-time weapon boundary chain."));return;}
            const uint64 Before=Weapon->GetCombatBoundaryRevision();
            const bool bCompleted=Weapon->AdvanceAnimation(0.);
            bHasCombatFormFrameSlice=false;
            if(!IsCurrent()||!IsCombatActionAllowed())return;
            if(!Weapon.IsValid()||Weapon->IsBeingDestroyed()||!bCompleted)
            {bStopWeaponCarry=true;continue;}
            if(Weapon->GetCombatBoundaryRevision()==Before)
            {UE_LOG(LogTemp,Error,TEXT("Combat clock stopped a zero-time weapon boundary with no progress."));return;}
            continue;
        }
        ConsecutiveZeroBoundaries=0;
        FDWCombatPlayerVitalsStep Step;
        const double Requested=FMath::Min(Remaining,FMath::Min(ShieldBoundary,FMath::Min(WeaponBoundary,FMath::Min(CoreBoundary,DashBoundary))));
        // AdvanceNext is native/transactional and also stops at the next resource or form boundary.
        // Adopt these facts before any notification at that timestamp, never the rest of the frame.
        const bool bDashThisSlice=IsFormalDashing();
        auto VitalsCandidate=CombatVitals;auto DashCandidate=FormalDash;FDWYeastDashStep DashStep;
        if(!VitalsCandidate.AdvanceNext(Requested,Health,Step,IsFormallyRooted()?FormalRoot.GetConfig().SpendPerSecond:(bDashThisSlice?FormalDash.GetConfig().SpendPerSecond:0.))||Step.AdvancedSeconds<=0.)return;
        if(bDashThisSlice)
        {
            if(!DashCandidate.AdvanceAtResourceBoundary(Step.AdvancedSeconds,Step.bFormExpired,
                VitalsCandidate.GetSpecial()==VitalsCandidate.GetConfig().Vitals.SpecialFloor,DashStep))return;
        }
        else if(!DashCandidate.AdvanceCooldown(Step.AdvancedSeconds))return;
        CombatVitals=MoveTemp(VitalsCandidate);FormalDash=MoveTemp(DashCandidate);
        if(IsFormallyRooted()&&(Step.bFormExpired||GetCombatSpecial()==CombatVitals.GetConfig().Vitals.SpecialFloor))
        {
            CombatFrameRootExitOffset=Step.AdvancedSeconds; // Actual committed boundary wins over prediction rounding.
            EndFormalRootFacts(Step.bFormExpired?EDWMoldRootEnd::FormExpired:EDWMoldRootEnd::SpecialExhausted);
        }
        if(bDashThisSlice)QueueFormalDashSlice(DashStep,bDashWall&&Step.AdvancedSeconds>=DashBoundary);
        SyncCombatVitalsCompatibility();
        // Only prepare Ready here. Same-instant resource damage still precedes core consumption.
        if(bAdvanceCore&&!CoreInjection.Plan.Advance(Step.AdvancedSeconds))
        {CancelCoreInjectionFacts();UE_LOG(LogTemp,Error,TEXT("Core injection clock rejected a prepared slice."));return;}
        Consumed+=Step.AdvancedSeconds;
        Remaining=FMath::Max(0.,Remaining-Step.AdvancedSeconds);
        if(bAdvanceWeapon)
        {
            PaidCombatWeaponSlice.bPending=true;PaidCombatWeaponSlice.bWasBusy=Weapon->IsBusy();
            PaidCombatWeaponSlice.Seconds=Step.AdvancedSeconds;PaidCombatWeaponSlice.StartWorldSeconds=SliceStart;
            PaidCombatWeaponSlice.FormExpiryOffset=CombatFrameExpiryOffset;PaidCombatWeaponSlice.StartForm=CombatFrameStartForm;
            PaidCombatWeaponSlice.bStartRooted=bCombatFrameStartRooted;PaidCombatWeaponSlice.RootExitOffset=CombatFrameRootExitOffset;
            PaidCombatWeaponSlice.Life=ClockLife;PaidCombatWeaponSlice.WorldEpoch=ClockEpoch;
            PaidCombatWeaponSlice.ActionSerial=Weapon->ActionSerial;PaidCombatWeaponSlice.WeaponSelection=static_cast<uint8>(Weapon->SelectedWeapon);
            PaidCombatWeaponSlice.BoundaryRevision=Weapon->GetCombatBoundaryRevision();
            PaidCombatWeaponSlice.Role=Weapon->GetActiveRole();PaidCombatWeaponSlice.World=ClockWorld;PaidCombatWeaponSlice.Weapon=Weapon;
        }
        FDWCombatVitalsDamagePair Pair;Pair.Target=this;Pair.SourceLife=ClockLife;Pair.WorldEpoch=ClockEpoch;
        Pair.DehydrationOrdinal=Step.DehydrationOrdinal;Pair.StarvationOrdinal=Step.StarvationOrdinal;
        Pair.DehydrationAmount=Step.Dehydration.EventCount?static_cast<float>(Step.Dehydration.DamagePerEvent):0.f;
        Pair.StarvationAmount=Step.Starvation.EventCount?static_cast<float>(Step.Starvation.DamagePerEvent):0.f;
        // HC05: hunger/thirst facts precede an uncommitted weapon event at the same timestamp.
        // The fixed pair commits both channels before its first callback. Fatal damage cancels the weapon.
        const uint64 AdoptedRevision=CombatVitals.GetMutationRevision();
        Authority->SubmitVitalsDamagePair(Pair);
        if(!IsCurrent())return;
        Authority->ResolveShieldDamageAt(this,SliceStart+Step.AdvancedSeconds);
        if(!IsCurrent()||!IsCombatActionAllowed())return;
        if(!PublishFormalRootMovementEffects()||!IsCurrent())return;
        if(CombatVitals.GetMutationRevision()!=AdoptedRevision)
        {PaidCombatWeaponSlice={};EndFormalDashFacts(EDWYeastDashEnd::Cancelled);UE_LOG(LogTemp,Warning,TEXT("Combat clock stopped after a resource callback changed the current resource timeline."));return;}
        // Resource facts/damage precede this timestamp's pending dash motion/hits (HC05).
        // Paused callbacks keep the already-paid slice; resume never charges those seconds twice.
        if(!PumpFormalDashSlice()||!IsCurrent())return;
        if(Step.bFormExpired)
        {
            UpdateFormAppearance();if(!IsCurrent())return;
            PlayFormTransitionVFX(false);if(!IsCurrent())return;
            OnFormChanged(false);if(!IsCurrent())return;
        }
        if(!IsCombatActionAllowed())return;
        if(bAdvanceCore&&CoreInjection.Plan.GetSerial()==CoreSerialAtClockStart&&CoreInjection.Plan.IsReady())
        {
            CompleteCoreInjectionAtBoundary();
            if(!IsCurrent()||!IsCombatActionAllowed())return;
            bStopWeaponCarry=true;
        }
        if(bAdvanceWeapon)
        {
            // A resource/form notification may replace the action before the weapon reaches this point.
            // Only stop old weapon carry. Cancelling a weapon must never extend resource/form duration.
            if(!PaidCombatWeaponSlice.bPending)
            {bStopWeaponCarry=true;bHasCombatFormFrameSlice=false;}
            else if(!AdvancePaidWeaponSlice(true))return;
        }
        else bHasCombatFormFrameSlice=false;
    }
}
float ADWPlayerCharacter::GetSprintAlcoholProgress()const {return 0.f;}
void ADWPlayerCharacter::MoveCameraRelative(float Forward,float Right)
{
    if(!FMath::IsFinite(Forward)||!FMath::IsFinite(Right))return;
    if(IsCombatActionAllowed()&&(Forward!=0.f||Right!=0.f))
    {
        CancelCoreInjectionFacts();
        if(WeaponAnimationComponent&&WeaponAnimationComponent->IsFormalSporeActive())WeaponAnimationComponent->CancelAction();
        if(!IsValid(this)||IsActorBeingDestroyed()||!IsCombatActionAllowed())return;
    }
    if(IsFormallyRooted()||IsDead()||CombatKnockback.bActive||bIsRolling||IsFormalDashing()||AttackPhase!=EDWCombatPhase::None||(WeaponAnimationComponent&&WeaponAnimationComponent->LocksMovement()))return;
    const FVector2D V=FVector2D(Forward,Right).GetClampedToMaxSize(1.f);const FRotationMatrix R(FRotator(0,CameraYaw,0));
    AddMovementInput(R.GetUnitAxis(EAxis::X),V.X);AddMovementInput(R.GetUnitAxis(EAxis::Y),V.Y);
}
void ADWPlayerCharacter::DragCamera(float X,float Y)
{
    const auto* PC=Cast<ADWPlayerController>(GetController());
    if(!CameraArm||!PC||!PC->IsCameraInputAllowed()||!FMath::IsFinite(X)||!FMath::IsFinite(Y))return;
    const auto* Settings=UDWUserSettings::Resolve(this);
    const float Multiplier=Settings?Settings->GetCameraSensitivityMultiplier():1.f;
    const float Base=FMath::IsFinite(CameraDragSensitivity)?FMath::Clamp(CameraDragSensitivity,.01f,4.f):.4f;
    const float Sensitivity=Base*Multiplier;
    CameraYaw=FMath::UnwindDegrees(CameraYaw+X*Sensitivity);
    CameraPitch=FMath::Clamp(CameraPitch+Y*Sensitivity,MinCameraPitch,MaxCameraPitch);
}
float ADWPlayerCharacter::ClampCameraDistance(float Distance) const
{
    const float Near=FMath::IsFinite(MinCameraDistance)?FMath::Max(100.f,MinCameraDistance):600.f;
    const float Far=FMath::IsFinite(MaxCameraDistance)?FMath::Max(100.f,MaxCameraDistance):3000.f;
    return FMath::Clamp(FMath::IsFinite(Distance)?Distance:1800.f,FMath::Min(Near,Far),FMath::Max(Near,Far));
}
float ADWPlayerCharacter::GetCurrentCameraDistance() const
{
    return CameraArm?CameraArm->TargetArmLength:ClampCameraDistance(CameraDistance);
}
void ADWPlayerCharacter::ZoomCamera(float WheelDelta)
{
    const auto* PC=Cast<ADWPlayerController>(GetController());
    if(!CameraArm||!PC||!PC->IsCameraInputAllowed()||!FMath::IsFinite(WheelDelta)||FMath::IsNearlyZero(WheelDelta))return;
    const float Step=FMath::IsFinite(CameraZoomStep)?FMath::Max(1.f,CameraZoomStep):150.f;
    CameraDistance=ClampCameraDistance(ClampCameraDistance(CameraDistance)-WheelDelta*Step);
    if(FMath::IsFinite(CameraZoomInterpSpeed)&&CameraZoomInterpSpeed<=0.f)CameraArm->TargetArmLength=CameraDistance;
}
void ADWPlayerCharacter::ClearHeldActions(){CancelCombatActions();bSprintHeld=false;SetHarvestHeld(false);bSprinting=false;ConsumeMovementInputVector();StopPlayerVFX(true);StopMovementAudio();bHasVFXLocationSample=false;CurrentVFXGroundSpeed=0.f;}
void ADWPlayerCharacter::PerformDash()
{
    // Compatibility Blueprint entry consumes a real D02 press; it cannot manufacture a held action.
    if(auto* PC=Cast<ADWPlayerController>(GetController()))PC->RequestFormalDashInput();
}
namespace DWPlayerCombat
{
    static float Safe(float Value,float Fallback,float Minimum=0.f)
    {return FMath::IsFinite(Value)?FMath::Max(Minimum,Value):Fallback;}
}

bool ADWPlayerCharacter::IsCombatSimulationSuspended() const
{
    const auto* PC=Cast<ADWPlayerController>(GetController());
    return UGameplayStatics::IsGamePaused(this)||(PC&&PC->IsCombatInputSuspended());
}
bool ADWPlayerCharacter::IsCombatActionAllowed() const
{
    if(IsCombatSimulationSuspended()||!HasCombatVitalsReady()||bCombatRecoveryPositionPending)return false;
    if(!GetWorld()||GetWorld()->bIsTearingDown||IsActorBeingDestroyed()||IsDead()||bRestoringSave||UGameplayStatics::IsGamePaused(this)||UDWWorldEventSubsystem::IsPlaying(this))return false;
    if(const auto* PC=Cast<ADWPlayerController>(GetController());PC&&PC->IsGameplayBlocked())return false;
    if(const auto* GI=GetGameInstance<UDWGameInstance>())
    {
        if(GI->IsPlayerRestorePending())return false;
        if(const auto* Loading=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();Loading&&Loading->IsTransitionActive())return false;
    }
    return true;
}

bool ADWPlayerCharacter::PerformMeleeAttack(FVector TargetLocation)
{
    if(HasCombatVitalsReady())
    {
        if(!WeaponAnimationComponent)return false;
        const bool Accepted=WeaponAnimationComponent->PrimaryPressed(TargetLocation);
        if(Accepted)WeaponAnimationComponent->PublishAcceptedWeaponStartEffects();
        return Accepted;
    }
    if(!IsCombatActionAllowed()||bGameplayActionInProgress||bApplyingCombatDamage||bIsRolling||IsFormalDashing()||TargetLocation.ContainsNaN())return false;
    const float Now=GetWorld()->GetTimeSeconds();
    if(Now<TransformUntil)return false;
    const bool bComboEnabled=bEnableMeleeCombo&&!MeleeComboSteps.IsEmpty();
    if(AttackPhase!=EDWCombatPhase::None)
    {
        // One press queues one following strike within the configured tail window.
        // Buffering never changes this strike's damage clock or starts the next strike early.
        const float BufferSeconds=DWPlayerCombat::Safe(MeleeComboInputBufferSeconds,.25f);
        const float BufferStart=FMath::Max(MeleeStartTime,MeleeEndTime-BufferSeconds);
        if(!bComboEnabled||!bActiveMeleeCombo||bMeleeComboInputBuffered||BufferSeconds<=0.f
            ||!MeleeComboSteps.IsValidIndex(CurrentMeleeComboStep+1)||Now<=MeleeStartTime||Now<BufferStart||Now>MeleeEndTime)return false;
        bMeleeComboInputBuffered=true;BufferedMeleeTarget=TargetLocation;
        return true;
    }
    if(bMeleeComboInputBuffered||Now<NextMeleeTime)return false;
    int32 ComboStep=INDEX_NONE;
    if(bComboEnabled)
        ComboStep=MeleeComboSteps.IsValidIndex(NextMeleeComboStep)&&Now<=MeleeComboExpiresAt?NextMeleeComboStep:0;
    return BeginMeleeStep(TargetLocation,ComboStep);
}

bool ADWPlayerCharacter::BeginMeleeStep(FVector TargetLocation,int32 ComboStep)
{
    if(!IsCombatActionAllowed()||bGameplayActionInProgress||bApplyingCombatDamage||bIsRolling||IsFormalDashing()
        ||AttackPhase!=EDWCombatPhase::None||TargetLocation.ContainsNaN())return false;
    const float Now=GetWorld()->GetTimeSeconds();
    if(Now<NextMeleeTime||Now<TransformUntil)return false;
    if(ComboStep!=INDEX_NONE&&(!bEnableMeleeCombo||!MeleeComboSteps.IsValidIndex(ComboStep)))return false;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
    if(!Authority)return false;
    MeleeCommitAction=Authority->BeginAction(this);
    if(!MeleeCommitAction.Id)return false;
    CancelCoreInjectionFacts();
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    bActiveMeleeCombo=ComboStep!=INDEX_NONE;
    CurrentMeleeComboStep=ComboStep;
    if(bActiveMeleeCombo)ActiveMeleeStep=MeleeComboSteps[ComboStep];
    ActiveMeleeClip=bActiveMeleeCombo?ActiveMeleeStep.Animation:MeleeAnimation;
    bHasMeleeAnimationSnapshot=true;
    ClearMeleeComboContinuation();
    MeleeDirection=(TargetLocation-GetActorLocation()).GetSafeNormal2D();
    if(MeleeDirection.IsNearlyZero())MeleeDirection=GetActorForwardVector().GetSafeNormal2D();
    if(MeleeDirection.IsNearlyZero())MeleeDirection=FVector::ForwardVector;
    SetActorRotation(MeleeDirection.Rotation());
    MeleeHitActors.Reset();bMeleeWindowSampled=false;
    MeleeStartTime=Now;FrozenMeleeAnimationElapsed=0.f;
    MeleeActiveStartTime=Now+DWPlayerCombat::Safe(bActiveMeleeCombo?ActiveMeleeStep.WindupSeconds:MeleeWindupSeconds,.2f);
    MeleeActiveEndTime=MeleeActiveStartTime+DWPlayerCombat::Safe(bActiveMeleeCombo?ActiveMeleeStep.ActiveSeconds:MeleeActiveSeconds,.12f,.01f);
    MeleeEndTime=MeleeActiveEndTime+DWPlayerCombat::Safe(bActiveMeleeCombo?ActiveMeleeStep.RecoverySeconds:MeleeRecoverySeconds,.3f);
    ActiveMeleeDuration=MeleeEndTime-MeleeStartTime;
    const bool bHasNextStep=bActiveMeleeCombo&&MeleeComboSteps.IsValidIndex(ComboStep+1);
    NextMeleeTime=MeleeEndTime+DWPlayerCombat::Safe(bHasNextStep?MeleeComboStepCooldown:MeleeCooldown,bHasNextStep?.04f:.15f);
    AttackPhase=EDWCombatPhase::Windup;++AttackSerial;
    AttackUntil=0.f;bRestartAttackAnimation=false;
    SetHarvestHeld(false);bSprinting=false;
    ConsumeMovementInputVector();GetCharacterMovement()->StopMovementImmediately();StopMovementAudio();
    bMeleePreviousOrientToMovement=GetCharacterMovement()->bOrientRotationToMovement;
    GetCharacterMovement()->bOrientRotationToMovement=false;
    UpdateAnimatedMeleeWeaponVisibility();
    BeginMeleeWeaponPlaceholder();
    bRestartMeleeAnimation=true;
    if(!bUseCombatAnimBlueprint)ActiveAnimation=nullptr;
    UpdateMeleeDamageVolume();UpdateAnimation();
    return true;
}

UAnimationAsset* ADWPlayerCharacter::GetCurrentMeleeAnimation() const
{
    if(WeaponAnimationComponent&&WeaponAnimationComponent->IsEnabled()&&WeaponAnimationComponent->GetActiveSequence())return WeaponAnimationComponent->GetActiveSequence();
    // Retain the outgoing sequence during its short Idle/Move blend.
    return bHasMeleeAnimationSnapshot?ActiveMeleeClip.Get():MeleeAnimation.Get();
}

void ADWPlayerCharacter::ClearMeleeComboContinuation()
{
    NextMeleeComboStep=INDEX_NONE;MeleeComboExpiresAt=0.f;
    bMeleeComboInputBuffered=false;BufferedMeleeTarget=FVector::ZeroVector;
}

void ADWPlayerCharacter::FinishMeleeStep()
{
    if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))Authority->EndAction(MeleeCommitAction);
    MeleeCommitAction={};
    const bool bCanContinue=bActiveMeleeCombo&&bEnableMeleeCombo&&MeleeComboSteps.IsValidIndex(CurrentMeleeComboStep+1);
    const int32 FollowingStep=CurrentMeleeComboStep+1;
    FrozenMeleeAnimationElapsed=ActiveMeleeDuration;
    if(GetCharacterMovement())GetCharacterMovement()->bOrientRotationToMovement=bMeleePreviousOrientToMovement;
    RestoreMeleeWeaponPlaceholder();
    AttackPhase=EDWCombatPhase::None;MeleeHitActors.Reset();bMeleeWindowSampled=false;
    CurrentMeleeComboStep=INDEX_NONE;bActiveMeleeCombo=false;bRestartMeleeAnimation=false;
    UpdateAnimatedMeleeWeaponVisibility();
    if(bCanContinue)
    {
        NextMeleeComboStep=FollowingStep;
        MeleeComboExpiresAt=NextMeleeTime+DWPlayerCombat::Safe(MeleeComboResetSeconds,.65f);
    }
    else ClearMeleeComboContinuation();
}

void ADWPlayerCharacter::TryStartBufferedMeleeStep()
{
    if(AttackPhase!=EDWCombatPhase::None||!bMeleeComboInputBuffered||!GetWorld())return;
    if(!bEnableMeleeCombo||!MeleeComboSteps.IsValidIndex(NextMeleeComboStep)
        ||bIsRolling||IsFormalDashing()||GetWorld()->GetTimeSeconds()<TransformUntil)
    {ClearMeleeComboContinuation();return;}
    if(GetWorld()->GetTimeSeconds()<NextMeleeTime)return;
    const int32 Step=NextMeleeComboStep;
    const FVector Target=BufferedMeleeTarget;
    // BeginMeleeStep consumes the one stored input. A low-rate tick begins the next
    // strike now; it never fast-forwards several unobserved damage windows.
    if(!BeginMeleeStep(Target,Step))ClearMeleeComboContinuation();
}

void ADWPlayerCharacter::CancelCombatActions()
{
    EndFormalDashFacts(IsDead()?EDWYeastDashEnd::Death:EDWYeastDashEnd::Cancelled);
    CancelCoreInjectionFacts();
    PaidCombatWeaponSlice={}; // Death/restore/unpossess cancel unpaid weapon continuation; pause does not.
    if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))
    {Authority->EndAction(MeleeCommitAction);Authority->EndAction(DashCommitAction);}
    MeleeCommitAction={};DashCommitAction={};
    if(WeaponAnimationComponent)WeaponAnimationComponent->CancelAction();
    if(AttackPhase!=EDWCombatPhase::None)
    {
        FrozenMeleeAnimationElapsed=GetWorld()?FMath::Clamp(GetWorld()->GetTimeSeconds()-MeleeStartTime,0.f,ActiveMeleeDuration):0.f;
        if(GetCharacterMovement())GetCharacterMovement()->bOrientRotationToMovement=bMeleePreviousOrientToMovement;
    }
    RestoreMeleeWeaponPlaceholder();
    AttackPhase=EDWCombatPhase::None;MeleeHitActors.Reset();bMeleeWindowSampled=false;
    CurrentMeleeComboStep=INDEX_NONE;bActiveMeleeCombo=false;bRestartMeleeAnimation=false;
    UpdateAnimatedMeleeWeaponVisibility();
    ClearMeleeComboContinuation();
    bDashCombatActive=false;DashHitActors.Reset();
    AttackUntil=0.f;bPlayingAttackAnimation=false;bRestartAttackAnimation=false;
}

void ADWPlayerCharacter::UpdateMeleeDamageVolume()
{
    if(!MeleeDamageVolume)return;
    const float Range=DWPlayerCombat::Safe(bActiveMeleeCombo?ActiveMeleeStep.Range:MeleeRange,180.f,1.f);
    const FVector Direction=AttackPhase==EDWCombatPhase::None?GetActorForwardVector().GetSafeNormal2D():MeleeDirection;
    MeleeDamageVolume->SetBoxExtent(FVector(Range*.5f,DWPlayerCombat::Safe(bActiveMeleeCombo?ActiveMeleeStep.HalfWidth:MeleeHalfWidth,65.f,1.f),DWPlayerCombat::Safe(bActiveMeleeCombo?ActiveMeleeStep.HalfHeight:MeleeHalfHeight,70.f,1.f)),false);
    MeleeDamageVolume->SetWorldLocationAndRotation(GetActorLocation()+Direction*Range*.5f,Direction.Rotation());
    MeleeDamageVolume->SetWorldScale3D(FVector::OneVector);
}

void ADWPlayerCharacter::UpdateAnimatedMeleeWeaponVisibility()
{
    USkeletalMeshComponent* CombatMesh=GetMesh();
    if(!CombatMesh)return;
    if(LastAnimatedMeleeWeaponBone!=NAME_None&&LastAnimatedMeleeWeaponBone!=AnimatedMeleeWeaponBone
        &&CombatMesh->GetBoneIndex(LastAnimatedMeleeWeaponBone)!=INDEX_NONE)
        CombatMesh->UnHideBoneByName(LastAnimatedMeleeWeaponBone);
    LastAnimatedMeleeWeaponBone=AnimatedMeleeWeaponBone;
    if(AnimatedMeleeWeaponBone==NAME_None||CombatMesh->GetBoneIndex(AnimatedMeleeWeaponBone)==INDEX_NONE)return;
    const bool bShouldHide=AttackPhase==EDWCombatPhase::None||IsDead();
    if(CombatMesh->IsBoneHiddenByName(AnimatedMeleeWeaponBone)==bShouldHide)return;
    if(bShouldHide)CombatMesh->HideBoneByName(AnimatedMeleeWeaponBone,PBO_None);
    else CombatMesh->UnHideBoneByName(AnimatedMeleeWeaponBone);
}

void ADWPlayerCharacter::BeginMeleeWeaponPlaceholder()
{
    RestoreMeleeWeaponPlaceholder();
    if(AnimatedMeleeWeaponBone!=NAME_None&&GetMesh()&&GetMesh()->GetBoneIndex(AnimatedMeleeWeaponBone)!=INDEX_NONE)return;
    if(!bEnableMeleeWeaponPlaceholder||!SwordPlaceholder||!SwordPlaceholder->GetStaticMesh()||!GetCapsuleComponent())return;
    USceneComponent* Parent=SwordPlaceholder->GetAttachParent();
    const FName Socket=SwordPlaceholder->GetAttachSocketName();
    const bool bHasSocket=Parent&&!Socket.IsNone()&&Parent->DoesSocketExist(Socket);
    const auto* Clip=Cast<UAnimSequenceBase>(GetCurrentMeleeAnimation());
    const auto* CombatSkeletalAsset=GetMesh()?GetMesh()->GetSkeletalMeshAsset():nullptr;
    const USkeleton* TargetSkeleton=CombatSkeletalAsset?CombatSkeletalAsset->GetSkeleton():nullptr;
    const USkeleton* SourceSkeleton=Clip?Clip->GetSkeleton():nullptr;
    bool bHasClip=Clip&&!Clip->IsA<UAnimMontage>()&&Clip->GetPlayLength()>KINDA_SMALL_NUMBER&&TargetSkeleton&&SourceSkeleton==TargetSkeleton;
    if(!bHasClip&&Clip&&!Clip->IsA<UAnimMontage>()&&Clip->GetPlayLength()>KINDA_SMALL_NUMBER&&TargetSkeleton&&SourceSkeleton)
        for(const TSoftObjectPtr<USkeleton>& Compatible:TargetSkeleton->GetCompatibleSkeletons())
            if(Compatible.ToSoftObjectPath()==FSoftObjectPath(SourceSkeleton)){bHasClip=true;break;}
    // Authored combo animation supplies the swing; do not add the old procedural
    // cube swing even if its weapon attachment still needs a separate art setup.
    if(bHasClip&&(bActiveMeleeCombo||bHasSocket))return;
    MeleeWeaponRestParent=Parent;MeleeWeaponRestSocket=Socket;
    MeleeWeaponRestTransform=SwordPlaceholder->GetRelativeTransform();
    bMeleeWeaponRestVisible=SwordPlaceholder->IsVisible();bMeleeWeaponRestHidden=SwordPlaceholder->bHiddenInGame;
    bMeleeWeaponRestAbsoluteLocation=SwordPlaceholder->IsUsingAbsoluteLocation();
    bMeleeWeaponRestAbsoluteRotation=SwordPlaceholder->IsUsingAbsoluteRotation();
    bMeleeWeaponRestAbsoluteScale=SwordPlaceholder->IsUsingAbsoluteScale();
    // A valid bone/socket alone cannot supply a visible swing when its melee clip is missing.
    bMeleeWeaponCapsuleFallback=!bHasSocket||!bHasClip;
    bMeleeWeaponDefaultCube=SwordPlaceholder->GetStaticMesh()->GetPathName()==TEXT("/Engine/BasicShapes/Cube.Cube");
    bMeleeWeaponPlaceholderActive=true;
    if(bMeleeWeaponCapsuleFallback)
    {
        SwordPlaceholder->AttachToComponent(GetCapsuleComponent(),FAttachmentTransformRules::KeepRelativeTransform);
        SwordPlaceholder->SetAbsolute(false,false,false);
        // Only the original engine cube gets a horizontal blade silhouette. User meshes/materials stay intact.
        if(bMeleeWeaponDefaultCube)SwordPlaceholder->SetRelativeScale3D(FVector(1.f,.12f,.12f));
    }
    SwordPlaceholder->SetVisibility(true);SwordPlaceholder->SetHiddenInGame(false);
    UpdateMeleeWeaponPlaceholder();
}

void ADWPlayerCharacter::UpdateMeleeWeaponPlaceholder()
{
    if(!bMeleeWeaponPlaceholderActive||!SwordPlaceholder)return;
    if(!bEnableMeleeWeaponPlaceholder){RestoreMeleeWeaponPlaceholder();return;}
    const float Now=GetWorld()?GetWorld()->GetTimeSeconds():MeleeStartTime;
    float Angle=-70.f;
    if(Now>=MeleeActiveStartTime)
    {
        const float StrikeAlpha=FMath::Clamp((Now-MeleeActiveStartTime)/FMath::Max(.01f,MeleeActiveEndTime-MeleeActiveStartTime),0.f,1.f);
        Angle=FMath::Lerp(-70.f,75.f,StrikeAlpha);
    }
    if(bMeleeWeaponCapsuleFallback)
    {
        const FVector Direction=FRotator(0.f,Angle,0.f).Vector();
        const float Reach=GetCapsuleComponent()->GetUnscaledCapsuleRadius()+38.f;
        SwordPlaceholder->SetRelativeLocation(Direction*Reach+FVector(0.f,0.f,10.f));
        SwordPlaceholder->SetRelativeRotation(bMeleeWeaponDefaultCube?FRotator(0.f,Angle,0.f):MeleeWeaponRestTransform.Rotator()+FRotator(0.f,Angle,0.f));
    }
    else SwordPlaceholder->SetRelativeRotation(MeleeWeaponRestTransform.Rotator()+FRotator(0.f,Angle,0.f));
}

void ADWPlayerCharacter::RestoreMeleeWeaponPlaceholder()
{
    if(!bMeleeWeaponPlaceholderActive)return;
    bMeleeWeaponPlaceholderActive=false;
    if(SwordPlaceholder)
    {
        if(USceneComponent* Parent=MeleeWeaponRestParent.Get())
            SwordPlaceholder->AttachToComponent(Parent,FAttachmentTransformRules::KeepRelativeTransform,MeleeWeaponRestSocket);
        else SwordPlaceholder->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
        SwordPlaceholder->SetAbsolute(bMeleeWeaponRestAbsoluteLocation,bMeleeWeaponRestAbsoluteRotation,bMeleeWeaponRestAbsoluteScale);
        SwordPlaceholder->SetRelativeTransform(MeleeWeaponRestTransform);
        SwordPlaceholder->SetVisibility(bMeleeWeaponRestVisible);SwordPlaceholder->SetHiddenInGame(bMeleeWeaponRestHidden);
    }
    MeleeWeaponRestParent.Reset();MeleeWeaponRestSocket=NAME_None;
    bMeleeWeaponCapsuleFallback=false;bMeleeWeaponDefaultCube=false;
}

bool ADWPlayerCharacter::CanDamageCombatTarget(AActor* Target) const
{
    if(!IsValid(Target)||Target==this||Target->IsActorBeingDestroyed()||!Target->CanBeDamaged()||!Target->GetActorEnableCollision()||Target->IsA<ADWPlayerCharacter>())return false;
    if(const auto* Enemy=Cast<ADWEnemyCharacter>(Target);Enemy&&!Enemy->IsAlive())return false;
    if(const auto* Nest=Cast<ADWEnemyNest>(Target);Nest&&!Nest->IsAlive())return false;
    return true;
}

bool ADWPlayerCharacter::HasCombatLineOfSight(AActor* Target,const FVector& Origin,const FVector& End) const
{
    if(!bCombatRequireLineOfSight&&!IsFormalDashing())return true;
    if(!GetWorld()||Origin.ContainsNaN()||End.ContainsNaN())return false;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWPlayerCombatSight),false,this);
    Params.AddIgnoredActor(Target);
    FHitResult Obstacle;
    return !GetWorld()->LineTraceSingleByChannel(Obstacle,Origin,End,ECC_Visibility,Params);
}

TArray<AActor*> ADWPlayerCharacter::CollectMeleeTargets()
{
    TArray<AActor*> Result;
    if(!GetWorld()||!MeleeDamageVolume)return Result;
    UpdateMeleeDamageVolume();
    const FVector Center=MeleeDamageVolume->GetComponentLocation();
    const FQuat Rotation=MeleeDamageVolume->GetComponentQuat();
    const FCollisionShape Shape=FCollisionShape::MakeBox(MeleeDamageVolume->GetScaledBoxExtent());
    TArray<FOverlapResult> Overlaps;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWPlayerMeleeDamage),false,this);
    GetWorld()->OverlapMultiByObjectType(Overlaps,Center,Rotation,FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),Shape,Params);
    TArray<AActor*> Candidates;
    for(const auto& Hit:Overlaps)if(UDWDamageVolumeLibrary::IsDamageHitComponent(Hit.GetComponent()))Candidates.AddUnique(Hit.GetActor());
    for(AActor* Target:Candidates)
    {
        if(!CanDamageCombatTarget(Target))continue;
        TInlineComponentArray<UPrimitiveComponent*> Components(Target);
        for(UPrimitiveComponent* Component:Components)
        {
            if(!UDWDamageVolumeLibrary::IsDamageHitComponent(Component)||!Component->OverlapComponent(Center,Rotation,Shape))continue;
            FVector AimPoint=Component->Bounds.Origin;
            Component->GetClosestPointOnCollision(GetActorLocation(),AimPoint);
            if(HasCombatLineOfSight(Target,GetActorLocation(),AimPoint)){Result.Add(Target);break;}
        }
    }
    return Result;
}

void ADWPlayerCharacter::DamageCombatTarget(AActor* Target,float Amount,float KnockbackDistance,float KnockbackSeconds,const FVector& Direction,TSet<TWeakObjectPtr<AActor>>& HitActors,DWCombatCommit::FActionDamage Identity)
{
    if(!IsCombatActionAllowed()||!CanDamageCombatTarget(Target)||HitActors.Contains(Target))return;
    if(Identity.Action.Id!=0&&(Target->IsA<ADWEnemyCharacter>()||Target->IsA<ADWEnemyNest>()))
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return;
        FDWCombatDamageCommand Command;Command.Identity=Identity;Command.TargetLife=Authority->CaptureActorLife(Target);
        Command.Source=this;Command.Target=Target;Command.Instigator=GetController();Command.Amount=DWPlayerCombat::Safe(Amount,0.f);
        Command.KnockbackDirection=Direction;Command.KnockbackDistance=DWPlayerCombat::Safe(KnockbackDistance,0.f);
        Command.KnockbackSeconds=DWPlayerCombat::Safe(KnockbackSeconds,.15f,.01f);
        HitActors.Add(Target);Authority->SubmitActionDamage(Command);return;
    }
    // Compatibility for the not-yet-migrated weapon path and other damageable actor types.
    if(bApplyingCombatDamage)return;
    HitActors.Add(Target);TGuardValue<bool> DamageGuard(bApplyingCombatDamage,true);
    const float Applied=UGameplayStatics::ApplyDamage(Target,DWPlayerCombat::Safe(Amount,0.f),GetController(),this,UDamageType::StaticClass());
    if(Applied>0.f&&IsValid(Target))
        if(auto* Enemy=Cast<ADWEnemyCharacter>(Target);Enemy&&Enemy->IsAlive())
            Enemy->ApplyCombatKnockback(Direction,DWPlayerCombat::Safe(KnockbackDistance,0.f),DWPlayerCombat::Safe(KnockbackSeconds,.15f,.01f));
}

void ADWPlayerCharacter::SweepDashCombat(const FVector& Start,const FVector& End)
{
    if(!bDashCombatActive||(!bEnableDashDamage&&!IsFormalDashing())||!IsCombatActionAllowed()||bApplyingCombatDamage||Start.ContainsNaN()||End.ContainsNaN())return;
    const auto SweepingAction=DashCommitAction;
    const float Radius=IsFormalDashing()?static_cast<float>(FormalDash.GetConfig().QueryRadiusCm):DWPlayerCombat::Safe(DashDamageRadius,60.f,1.f);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DWPlayerDashDamage),false,this);
    const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
    const FCollisionShape Shape=FCollisionShape::MakeSphere(Radius);
    TArray<FHitResult> Hits;
    GetWorld()->SweepMultiByObjectType(Hits,Start,End,FQuat::Identity,Objects,Shape,Params);
    TArray<FOverlapResult> EndOverlaps;
    GetWorld()->OverlapMultiByObjectType(EndOverlaps,End,FQuat::Identity,Objects,Shape,Params);
    TArray<UPrimitiveComponent*> Components;
    for(const auto& Hit:Hits)if(Hit.GetComponent())Components.AddUnique(Hit.GetComponent());
    for(const auto& Hit:EndOverlaps)if(Hit.GetComponent())Components.AddUnique(Hit.GetComponent());
    for(UPrimitiveComponent* Component:Components)
    {
        if(!UDWDamageVolumeLibrary::IsDamageHitComponent(Component))continue;
        AActor* Target=Component->GetOwner();
        if(!CanDamageCombatTarget(Target)||DashHitActors.Contains(Target))continue;
        if(IsFormalDashing()&&!Target->IsA<ADWEnemyCharacter>()&&!Target->IsA<ADWEnemyNest>())continue;
        const FVector Origin=FMath::ClosestPointOnSegment(Component->Bounds.Origin,Start,End);
        FVector AimPoint=Component->Bounds.Origin;
        Component->GetClosestPointOnCollision(Origin,AimPoint);
        const bool bFormal=IsFormalDashing();
        if(HasCombatLineOfSight(Target,Origin,AimPoint))DamageCombatTarget(Target,
            bFormal?static_cast<float>(FormalDash.GetConfig().Damage):DashDamage,
            bFormal?static_cast<float>(FormalDash.GetConfig().KnockbackCm):DashKnockbackDistance,
            bFormal?static_cast<float>(FormalDash.GetConfig().KnockbackSeconds):DashKnockbackSeconds,
            DashCombatDirection,DashHitActors,{SweepingAction,1,DWCombatCommit::EActionDamageKind::Dash,1});
        if(!bDashCombatActive||DashCommitAction.Id!=SweepingAction.Id||!IsCombatActionAllowed())break;
    }
    if(bDrawCombatDebug)
    {
        DrawDebugLine(GetWorld(),Start,End,FColor::Cyan,false,.1f,0,2.f);
        DrawDebugSphere(GetWorld(),End,Radius,16,FColor::Cyan,false,.1f);
    }
}

void ADWPlayerCharacter::NotifyHit(UPrimitiveComponent* MyComp,AActor* Other,UPrimitiveComponent* OtherComp,bool bSelfMoved,FVector HitLocation,FVector HitNormal,FVector NormalImpulse,const FHitResult& Hit)
{
    // The base callback can finish a roll on this exact impact. Sample before it clears bIsRolling.
    if(bIsRolling&&bDashCombatActive)
    {
        SweepDashCombat(LastDashCombatLocation,GetActorLocation());
        LastDashCombatLocation=GetActorLocation();
    }
    Super::NotifyHit(MyComp,Other,OtherComp,bSelfMoved,HitLocation,HitNormal,NormalImpulse,Hit);
}

void ADWPlayerCharacter::UpdateCombat(float DeltaSeconds)
{
    if(IsCombatSimulationSuspended())return;
    if(!IsCombatActionAllowed()){CancelCombatActions();return;}
    if(bDashCombatActive&&!IsFormalDashing())
    {
        SweepDashCombat(LastDashCombatLocation,GetActorLocation());LastDashCombatLocation=GetActorLocation();
        if(!bIsRolling)
        {
            if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))Authority->EndAction(DashCommitAction);
            DashCommitAction={};bDashCombatActive=false;DashHitActors.Reset();
        }
    }
    if(AttackPhase==EDWCombatPhase::None)
    {
        if(!bEnableMeleeCombo||MeleeComboSteps.IsEmpty()||bIsRolling||IsFormalDashing()||GetWorld()->GetTimeSeconds()<TransformUntil)
            ClearMeleeComboContinuation();
        TryStartBufferedMeleeStep();
        if(AttackPhase==EDWCombatPhase::None)return;
    }
    const float Now=GetWorld()->GetTimeSeconds();
    SetActorRotation(MeleeDirection.Rotation());UpdateMeleeDamageVolume();
    if(Now>=MeleeActiveStartTime&&(Now<MeleeActiveEndTime||!bMeleeWindowSampled))
    {
        // A skipped short window still gets one query; never replay an already sampled window.
        AttackPhase=EDWCombatPhase::Active;bMeleeWindowSampled=true;
        const auto ResolvingAction=MeleeCommitAction;
        const auto Targets=CollectMeleeTargets();
        for(AActor* Target:Targets)
        {
            DamageCombatTarget(Target,bActiveMeleeCombo?ActiveMeleeStep.Damage:MeleeDamage,
                bActiveMeleeCombo?ActiveMeleeStep.KnockbackDistance:MeleeKnockbackDistance,
                bActiveMeleeCombo?ActiveMeleeStep.KnockbackSeconds:MeleeKnockbackSeconds,MeleeDirection,MeleeHitActors,{ResolvingAction,1,DWCombatCommit::EActionDamageKind::Melee,1});
            if(AttackPhase==EDWCombatPhase::None||MeleeCommitAction.Id!=ResolvingAction.Id||!IsCombatActionAllowed())return;
        }
    }
    if(Now>=MeleeEndTime)
    {
        if(bActiveMeleeCombo){FinishMeleeStep();TryStartBufferedMeleeStep();}
        else CancelCombatActions();
        UpdateAnimation();return;
    }
    if(Now<MeleeActiveStartTime)AttackPhase=EDWCombatPhase::Windup;
    else if(Now>=MeleeActiveEndTime)AttackPhase=EDWCombatPhase::Recovery;
    UpdateMeleeWeaponPlaceholder();
    if(bDrawCombatDebug)DrawDebugBox(GetWorld(),MeleeDamageVolume->GetComponentLocation(),MeleeDamageVolume->GetScaledBoxExtent(),MeleeDamageVolume->GetComponentQuat(),AttackPhase==EDWCombatPhase::Active?FColor::Red:FColor::Yellow,false,-1.f,0,2.f);
}

EDWCombatAnimationState ADWPlayerCharacter::GetCombatAnimationState() const
{
    if(IsDead())return EDWCombatAnimationState::Death;
    const float Now=GetWorld()?GetWorld()->GetTimeSeconds():0.f;
    if(Now<TransformUntil)return EDWCombatAnimationState::Transform;
    if(bIsRolling||IsFormalDashing())return EDWCombatAnimationState::Dash;
    if(WeaponAnimationComponent&&WeaponAnimationComponent->IsEnabled()&&WeaponAnimationComponent->HasWeaponPresentation())
        return !WeaponAnimationComponent->GetActiveSequence()?EDWCombatAnimationState::Idle:EDWCombatAnimationState::Melee;
    if(AttackPhase!=EDWCombatPhase::None)return EDWCombatAnimationState::Melee;
    if(Now<AttackUntil&&ActiveAttackClip)return EDWCombatAnimationState::Throw;
    return GetVelocity().SizeSquared2D()>25.f?EDWCombatAnimationState::Move:EDWCombatAnimationState::Idle;
}
float ADWPlayerCharacter::GetCombatThrowElapsedSeconds() const
{return GetWorld()?FMath::Max(0.f,GetWorld()->GetTimeSeconds()-ThrowAnimationStartTime):0.f;}
float ADWPlayerCharacter::GetMeleeAnimationElapsedSeconds() const
{
    if(WeaponAnimationComponent&&WeaponAnimationComponent->IsEnabled()&&WeaponAnimationComponent->GetActiveSequence())return static_cast<float>(WeaponAnimationComponent->GetPresentationSourceSeconds());
    return AttackPhase!=EDWCombatPhase::None&&GetWorld()?FMath::Clamp(GetWorld()->GetTimeSeconds()-MeleeStartTime,0.f,ActiveMeleeDuration):FrozenMeleeAnimationElapsed;
}
float ADWPlayerCharacter::GetMeleeAnimationDuration() const
{
    if(WeaponAnimationComponent&&WeaponAnimationComponent->IsEnabled()&&WeaponAnimationComponent->GetActiveSequence())return WeaponAnimationComponent->GetActionDuration();
    return ActiveMeleeDuration;
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
    return bMovementAudioDesired&&CanPlayMovementFeedback()&&!bIsRolling&&!IsFormalDashing()&&
        IsValid(MovementLoopSound)&&IsValid(MovementAudioComponent)&&
        MovementAudioComponent->GetSound()==MovementLoopSound&&MovementSoundVolume>0.f&&
        GetCharacterMovement()->IsMovingOnGround()&&
        CurrentVFXGroundSpeed>=FMath::Max(.01f,MovementVFXMinSpeed)&&
        GetVelocity().SizeSquared2D()>=FMath::Square(FMath::Max(.01f,MovementVFXMinSpeed));
}
void ADWPlayerCharacter::UpdateMovementAudio(bool bActuallyMoving)
{
    const float Volume=FMath::IsFinite(MovementSoundVolume)?FMath::Clamp(MovementSoundVolume,0.f,4.f):0.f;
    if(!bActuallyMoving||bIsRolling||IsFormalDashing()||!CanPlayMovementFeedback()||!IsValid(MovementLoopSound)||Volume<=0.f)
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
    SetLoopingPlayerVFX(RunTrailComponent,RunTrailNiagara,bMoving&&bSprinting&&!bIsRolling&&!IsFormalDashing(),bRunTrailEmitting,RunTrailOffset,RunTrailRotation,RunTrailScale);
    SetLoopingPlayerVFX(DashTrailComponent,DashNiagara,bMoving&&(bIsRolling||IsFormalDashing()),bDashTrailEmitting,DashTrailOffset,DashTrailRotation,DashTrailScale);
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
    const bool bPreviousHeld=bHarvestHeld;
    if(bHarvestHeld&&!bHeld)HarvestElapsed=0.f;
    bHarvestHeld=bHeld&&!IsDead()&&AttackPhase==EDWCombatPhase::None;
    if(!bHarvestHeld)StopGatherFeedback();
    // Keep tap edges even when press and release are delivered within one frame.
    // Held-repeat remains in UpdateHarvest; the gathering cooldown prevents a second hit.
    if(bPreviousHeld!=bHarvestHeld&&GatheringComponent&&GatheringComponent->IsGatheringEnabled())
        GatheringComponent->HandleInteraction(0.f,bHarvestHeld);
}
void ADWPlayerCharacter::CancelHarvestInteraction(){SetHarvestHeld(false);}
bool ADWPlayerCharacter::CanPerformGatheringAction() const
{
    return !bGameplayActionInProgress&&!bRestoringSave&&!IsDead()&&HasCombatVitalsReady()&&!bIsRolling&&!IsFormalDashing()&&!IsFormallyRooted()&&AttackPhase==EDWCombatPhase::None&&(!WeaponAnimationComponent||!WeaponAnimationComponent->IsBusy());
}
void ADWPlayerCharacter::UpdateHarvest(float Dt)
{
    if(bGameplayActionInProgress||bRestoringSave)return;
    if(GatheringComponent&&GatheringComponent->HandleInteraction(Dt,bHarvestHeld)){StopGatherFeedback();HarvestTarget.Reset();HarvestElapsed=0.f;return;}
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    ScanElapsed+=Dt;
    if(ScanElapsed>=FMath::Max(0.01f,InteractionScanInterval))
    {
        ScanElapsed=0.f;ADWResourceNode* Best=nullptr;float BestDist=TNumericLimits<float>::Max();
        for(TActorIterator<ADWResourceNode> It(GetWorld());It;++It)
        {if(!It->IsAvailable())continue;const float Dist=FVector::DistSquared2D(GetActorLocation(),It->GetActorLocation());if(Dist<FMath::Square(It->InteractionRadius)&&Dist<BestDist){Best=*It;BestDist=Dist;}}
        if(HarvestTarget.Get()!=Best){HarvestTarget=Best;HarvestElapsed=0.f;}
    }
    auto* Node=HarvestTarget.Get();if(!bHarvestHeld||!Node||bIsRolling||IsFormalDashing()||!Node->IsAvailable()){StopGatherFeedback();return;}
    if(FVector::DistSquared2D(GetActorLocation(),Node->GetActorLocation())>FMath::Square(Node->InteractionRadius)){HarvestElapsed=0.f;StopGatherFeedback();return;}
    UpdateGatherFeedback(Node);
    const float Period=FMath::Max(0.05f,Node->HarvestInterval);HarvestElapsed+=Dt;
    while(HarvestElapsed>=Period&&IsValid(Node)&&Node->IsAvailable())
    {
        const int32 Amount=Node->bInfinite?Node->HarvestAmount:FMath::Min(Node->HarvestAmount,Node->RemainingAmount);
        if(Amount<=0)break;
        const FName HarvestedItemId=Node->ItemId;
        const TWeakObjectPtr<ADWPlayerCharacter> HarvestOwner(this);
        const TWeakObjectPtr<UDWInventoryComponent> HarvestInventory(Inventory);
        const TWeakObjectPtr<UWorld> HarvestWorld(GetWorld());
        auto* HarvestAuthority=UDWCombatAuthoritySubsystem::Get(this);
        const auto HarvestLife=HarvestAuthority?HarvestAuthority->CaptureActorLife(this):DWCombatCommit::FEntityLife{};
        const uint64 HarvestEpoch=HarvestAuthority?HarvestAuthority->GetWorldEpoch():0;
        const auto IsHarvestOwnerCurrent=[&]()
        {
            const auto* Owner=HarvestOwner.Get();
            if(!Owner||Owner->IsActorBeingDestroyed()||Owner->IsDead()||Owner->bRestoringSave||
                !HarvestWorld.IsValid()||Owner->GetWorld()!=HarvestWorld.Get()||HarvestWorld->bIsTearingDown||
                Owner->GetInventory()!=HarvestInventory.Get()||!HarvestInventory.IsValid())return false;
            auto* CurrentAuthority=UDWCombatAuthoritySubsystem::Get(Owner);
            if(HarvestAuthority)
            {
                if(!CurrentAuthority||CurrentAuthority->GetWorldEpoch()!=HarvestEpoch)return false;
                if(HarvestLife.Entity)
                {
                    const auto Current=CurrentAuthority->CaptureActorLife(HarvestOwner.Get());
                    if(Current.Entity!=HarvestLife.Entity||Current.Life!=HarvestLife.Life)return false;
                }
            }
            return true;
        };
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
        // Inventory callbacks can restore the player/world. Do not continue the old harvest.
        if(!IsHarvestOwnerCurrent())return;
        if(IsValid(Node))Node->CommitHarvest(Amount);
        if(!IsHarvestOwnerCurrent())return;
        HarvestElapsed-=Period;
        bGatherFailureLatched=false;
        UDWAudioLibrary::PlayEvent(this,GameplayConfig,EDWAudioEvent::GatherSuccess);
        if(HarvestedItemId==TEXT("Yeast"))Transformation=FMath::Clamp(Transformation+GameplayConfig->MaxTransformation*GameplayConfig->YeastGainPercentPerItem*Amount/100.f,0.f,GameplayConfig->MaxTransformation);
        OnResourceHarvested(IsValid(Node)?Node:nullptr,HarvestedItemId,Amount);
        if(!IsHarvestOwnerCurrent())return;
        // The accepted inventory harvest and existing resource callback path completed. This never changes inventory.
        if(auto* PC=Cast<APlayerController>(GetController()))if(auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD()))
            HUD->NotifyItemAcquired(HarvestedItemId,Amount);
        if(!IsHarvestOwnerCurrent())return;
    }
    if(!IsValid(Node)||!Node->IsAvailable())StopGatherFeedback();
}
bool ADWPlayerCharacter::CanContinueGatherFeedback()const
{
    if(!bHarvestHeld||IsDead()||bIsRolling||IsFormalDashing()||bRestoringSave||!GameplayConfig||!GetWorld()||GetWorld()->IsPaused())return false;
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
    if(!TryToggleFormalRoot())DWCombatHUD::NotifyRejected(this,EDWCombatHUDRequest::Root); // E stance; injection remains inventory Use.
}
void ADWPlayerCharacter::NotifyAlcoholHit()
{
    // Legacy hit meter is retired. Formal Special is advanced once by CombatVitals.
}
bool ADWPlayerCharacter::ThrowAlcoholAt(FVector Target)
{
    if(HasCombatVitalsReady())return false; // Formal stocked bottles use the unique rules-clock/emitter transaction.
    if(!IsCombatActionAllowed()||AttackPhase!=EDWCombatPhase::None||bIsRolling||IsFormalDashing()||!AlcoholProjectileClass||Target.ContainsNaN()||GetWorld()->GetTimeSeconds()<NextThrowTime||bGameplayActionInProgress)return false;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    const int32 Cost=FMath::Max(1,AlcoholCostPerThrow);
    if(Inventory->CountItem(TEXT("Alcohol"))<Cost){Notify(DWText(this,TEXT("酒精不足：通过疾跑或制作获得"),TEXT("Not enough Alcohol. Obtain it through sprinting or crafting.")));return false;}
    if(!CanThrowAlcohol(Target))return false;
    FVector Delta=Target-GetActorLocation();if(Delta.Size2D()>MaxThrowRange){const FVector D=Delta.GetSafeNormal2D()*MaxThrowRange;Target=GetActorLocation()+D;Target.Z=GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight();}
    const FVector Facing=(Target-GetActorLocation()).GetSafeNormal2D();if(!Facing.IsNearlyZero())SetActorRotation(Facing.Rotation());
    const FVector SpawnLocation=GetActorLocation()+GetActorTransform().TransformVectorNoScale(ThrowOriginOffset);
    FActorSpawnParameters Params;Params.Owner=this;Params.Instigator=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Bottle=GetWorld()->SpawnActor<ADWAlcoholProjectile>(AlcoholProjectileClass,SpawnLocation,GetActorRotation(),Params);if(!Bottle)return false;
    if(!Inventory->TryRemoveItem(TEXT("Alcohol"),Cost)){Bottle->Destroy();return false;}
    CancelCoreInjectionFacts();
    Bottle->LaunchAtTarget(Target);NextThrowTime=GetWorld()->GetTimeSeconds()+ThrowCooldown;
    // The new Alcohol clip already owns its source clock; do not restart the legacy Throw state at release.
    if(!(WeaponAnimationComponent&&WeaponAnimationComponent->IsEnabled()&&WeaponAnimationComponent->IsBusy()&&WeaponAnimationComponent->SelectedWeapon==EDWAnimatedWeapon::Alcohol))StartAttackAnimation();
    UDWAudioLibrary::PlayWorldSound(this,ThrowSound,SpawnLocation,ThrowSoundVolume,ThrowSoundPitch);
    OnAlcoholThrown(IsValid(Bottle)?Bottle:nullptr,Target,Cost);return true;
}
bool ADWPlayerCharacter::UseInventoryItem(int32 Index)
{
    if(IsDead()||!HasCombatVitalsReady()||!GameplayConfig||!Inventory||bGameplayActionInProgress||bRestoringSave)return false;
    FDWInventorySlotSnapshot Slot;
    if(Inventory->CaptureSlotSnapshot(Index,Slot))
    {
        if(GatheringComponent&&GatheringComponent->TryUseTool(Slot.ItemId))return true;
        const auto ItemRole=FDWCombatItemRegistry::GetItemRole(Slot.ItemId);
        if(ItemRole==EDWCombatItemRole::MoldCore||ItemRole==EDWCombatItemRole::YeastCore)return RequestCoreInjection(Slot);
    }
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))return Authority->UseLegacyHealingItem(this,Index);
    return false;
}
bool ADWPlayerCharacter::CraftRecipe(FName RecipeId)
{
    if(IsDead()||!Inventory||bGameplayActionInProgress||bRestoringSave)return false;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    const bool Result=Inventory->TryCraft(RecipeId,bYeastForm);
    const FDWRecipeDefinition* FeedbackRecipe=GameplayConfig?GameplayConfig->GetRecipeDefinition(RecipeId):nullptr;
    const bool bBlockedByForm=GameplayConfig&&GameplayConfig->bRequireTransformationForCrafting&&FeedbackRecipe&&FeedbackRecipe->bRequiresYeast&&!bYeastForm;
    Notify(Result?DWText(this,TEXT("制作完成"),TEXT("Crafting complete."))
        :(bBlockedByForm?DWText(this,TEXT("该配方需要先变身为酵母形态"),TEXT("This recipe requires Yeast form."))
        :DWText(this,TEXT("制作失败：检查配方、材料和背包空间"),TEXT("Crafting failed. Check the recipe, ingredients and inventory space."))));
    if(Result)OnRecipeCrafted(RecipeId);return Result;
}
float ADWPlayerCharacter::TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer)
{
    if(!CanReceiveCombatDamage()||!FMath::IsFinite(Amount)||Amount<=0.f)return 0.f;
    if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))return Authority->SubmitLegacyDamage(this,Amount,Event,InstigatorController,Causer);
    return 0.f;
}
void ADWPlayerCharacter::RecordCombatDamageFacts(float Applied)
{
    if(Applied<=0.f)return;
    CancelCoreInjectionFacts(); // Actual positive damage cancels before any damage notification.
    if(CombatDamageRevision<MAX_uint64)++CombatDamageRevision;
    LastCombatDamageWorldTime=GetWorld()?GetWorld()->GetTimeSeconds():-1.;
}
void ADWPlayerCharacter::CommitCombatDeathFacts()
{
    if(bDeathHandled)return;
    bDeathHandled=true;bCombatDeathEffectsPending=true;Health=GetCombatHealthFloor();
    if(WeaponAnimationComponent)WeaponAnimationComponent->ClearShieldCounterFacts();
    EndFormalRootFacts(EDWMoldRootEnd::Death);
    CancelCoreInjectionFacts();
    ResetCombatKnockbackFacts();
    CombatVitals.EndForm();bHasCombatFormFrameSlice=false;SyncCombatVitalsCompatibility();
}
void ADWPlayerCharacter::PublishCombatDeathEffects()
{
    if(!bCombatDeathEffectsPending)return;bCombatDeathEffectsPending=false;
    auto* DeathAuthority=UDWCombatAuthoritySubsystem::Get(this);if(!DeathAuthority)return;
    const auto DeathLife=DeathAuthority->CaptureActorLife(this);
    auto Current=[this,DeathLife]()
    {
        auto* A=UDWCombatAuthoritySubsystem::Get(this);
        if(!A||!IsValid(this)||IsActorBeingDestroyed()||!bDeathHandled||!IsDead())return false;
        const auto Life=A->CaptureActorLife(this);return Life.Entity==DeathLife.Entity&&Life.Life==DeathLife.Life;
    };
    if(!Current())return;
    if(auto* PC=Cast<ADWPlayerController>(GetController()))PC->InvalidateCombatInputLife();
    if(!Current())return;ClearHeldActions();if(!Current())return;
    GetCharacterMovement()->StopMovementImmediately();GetCharacterMovement()->DisableMovement();
    if(auto* PC=Cast<APlayerController>(GetController()))if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->ShowDefeat();
    if(!Current())return;UpdateAnimation();if(!Current())return;
    if(!bRestoringSave){TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);OnPlayerDied();}
    if(!Current()||bRestoringSave)return;
    if(auto* GI=GetGameInstance<UDWGameInstance>())GI->QueueCommittedCombatDeath(this,DeathLife);
}
void ADWPlayerCharacter::Die()
{
    CommitCombatDeathFacts();
    if(auto* Authority=UDWCombatAuthoritySubsystem::Get(this))Authority->NotifyActorDeath(this);
    PublishCombatDeathEffects();
}
void ADWPlayerCharacter::Notify(const FText& Message)const
{if(auto* PC=Cast<APlayerController>(GetController()))if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->Notify(Message);}
void ADWPlayerCharacter::RequestSave()
{StopMovementAudio();if(auto* GI=GetGameInstance<UDWGameInstance>()){const bool Ok=GI->SaveCurrentGame();Notify(Ok?DWText(this,TEXT("存档已保存"),TEXT("Game saved.")):GI->LastSaveError);}}
void ADWPlayerCharacter::StartAttackAnimation()
{
    ClearMeleeComboContinuation();
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
    ThrowAnimationStartTime=GetWorld()->GetTimeSeconds();
    ActiveAttackEndSeconds=ActiveAttackStartSeconds+Duration*FMath::Abs(ActiveAttackAnimationRate);
    if(const auto* Clip=Cast<UAnimSequenceBase>(ActiveAttackClip))
    {
        const float SourceRate=FMath::IsFinite(Clip->RateScale)&&!FMath::IsNearlyZero(Clip->RateScale)?FMath::Abs(Clip->RateScale):1.f;
        ActiveAttackEndSeconds=FMath::Min(Clip->GetPlayLength(),ActiveAttackStartSeconds+Duration*FMath::Abs(ActiveAttackAnimationRate)*SourceRate);
    }
    ++ThrowAnimationSerial;
    if(bUseCombatAnimBlueprint)++AttackAnimationPlayCount;
    AttackUntil=ThrowAnimationStartTime+Duration;
    bRestartAttackAnimation=true;
    UpdateAnimation();
}
float ADWPlayerCharacter::GetAttackAnimationPositionSeconds()const
{
    if(bUseCombatAnimBlueprint)
    {
        if(GetCombatAnimationState()!=EDWCombatAnimationState::Throw)return -1.f;
        const auto* Clip=Cast<UAnimSequenceBase>(ActiveAttackClip);
        const float SourceRate=Clip&&FMath::IsFinite(Clip->RateScale)&&!FMath::IsNearlyZero(Clip->RateScale)?FMath::Abs(Clip->RateScale):1.f;
        return FMath::Clamp(ActiveAttackStartSeconds+GetCombatThrowElapsedSeconds()*FMath::Abs(ActiveAttackAnimationRate)*SourceRate,ActiveAttackStartSeconds,ActiveAttackEndSeconds);
    }
    const auto* Anim=GetMesh()?GetMesh()->GetSingleNodeInstance():nullptr;
    return bPlayingAttackAnimation&&Anim&&Anim->GetAnimationAsset()==ActiveAttackClip?Anim->GetCurrentTime():-1.f;
}
void ADWPlayerCharacter::UpdateAnimation()
{
    if(!GetMesh()||!GetWorld())return;
    if(bUseCombatAnimBlueprint)
    {
        bPlayingAttackAnimation=GetCombatAnimationState()==EDWCombatAnimationState::Throw;
        bRestartAttackAnimation=false;bRestartMeleeAnimation=false;
        return;
    }
    if(WeaponAnimationComponent&&WeaponAnimationComponent->IsEnabled()&&WeaponAnimationComponent->HasWeaponPresentation()&&WeaponAnimationComponent->GetActiveSequence())
    {
        UAnimSequenceBase* Sequence=WeaponAnimationComponent->GetActiveSequence();
        if(Sequence&&(!GetMesh()->GetSingleNodeInstance()||GetMesh()->GetSingleNodeInstance()->GetAnimationAsset()!=Sequence))GetMesh()->PlayAnimation(Sequence,false);
        if(auto* Instance=GetMesh()->GetSingleNodeInstance()){Instance->SetPlaying(false);Instance->SetPosition(WeaponAnimationComponent->GetPositionSeconds(),false);}
        ActiveAnimation=Sequence;return;
    }
    UAnimationAsset* Wanted=IdleAnimation;bool bLoop=true;const float Now=GetWorld()->GetTimeSeconds();float Rate=1.f;
    bPlayingAttackAnimation=false;
    if(IsDead()){Wanted=DeathAnimation;bLoop=false;}
    else if(Now<TransformUntil&&TransformAnimation){Wanted=TransformAnimation;bLoop=false;if(const auto* Clip=Cast<UAnimSequenceBase>(Wanted))Rate=Clip->GetPlayLength()/FMath::Max(.05f,TransformAnimationSeconds);}
    else if(AttackPhase!=EDWCombatPhase::None&&GetCurrentMeleeAnimation()){Wanted=GetCurrentMeleeAnimation();bLoop=false;if(const auto* Clip=Cast<UAnimSequenceBase>(Wanted))Rate=Clip->GetPlayLength()/FMath::Max(.01f,ActiveMeleeDuration);}
    else if(Now<AttackUntil&&ActiveAttackClip){Wanted=ActiveAttackClip;bLoop=false;Rate=ActiveAttackAnimationRate;bPlayingAttackAnimation=true;}
    else if((bIsRolling||IsFormalDashing())&&DashAnimation){Wanted=DashAnimation;bLoop=false;}
    else if(GetVelocity().SizeSquared2D()>25.f){Wanted=WalkAnimation;Rate=bSprinting?GameplayConfig->SprintSpeedMultiplier:1.f;}
    auto* Existing=GetMesh()->GetSingleNodeInstance();
    const bool bStartAttack=bPlayingAttackAnimation&&bRestartAttackAnimation;
    const bool bStartMelee=AttackPhase!=EDWCombatPhase::None&&Wanted==GetCurrentMeleeAnimation()&&bRestartMeleeAnimation;
    if(Wanted&&(Wanted!=ActiveAnimation||!Existing||Existing->GetAnimationAsset()!=Wanted||bStartAttack||bStartMelee))
    {
        GetMesh()->PlayAnimation(Wanted,bLoop);ActiveAnimation=Wanted;
        if(bStartMelee)
        {
            if(auto* Anim=GetMesh()->GetSingleNodeInstance())Anim->SetPosition(0.f,false);
            bRestartMeleeAnimation=false;
        }
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
namespace
{
FString DWActorPath(const AActor* Actor) { return UWorld::RemovePIEPrefix(Actor->GetPathName()); }
FString DWLegacyId(const TCHAR* Prefix, const AActor* Actor, FName Id) { return FString(Prefix) + (Id.IsNone() ? Actor->GetName() : Id.ToString()); }
template<class T> int32 DWIdCount(const TCHAR* Prefix, const T* Actor, FName Id)
{
    int32 Count = 0; const FString Key = DWLegacyId(Prefix, Actor, Id);
    for (TActorIterator<T> It(Actor->GetWorld()); It; ++It)
        if (!It->IsActorBeingDestroyed() && DWLegacyId(Prefix, *It, It->PersistentId) == Key) ++Count;
    return Count;
}
template<class T> FString DWActorSaveId(const TCHAR* Prefix, const T* Actor, FName Id)
{
    const FString Legacy = DWLegacyId(Prefix, Actor, Id);
    return DWIdCount(Prefix, Actor, Id) > 1 ? Legacy + TEXT("|") + DWActorPath(Actor) : Legacy;
}
template<class T> bool DWMatchesSave(const FString& Key, const FString& Path, const TCHAR* Prefix, const T* Actor)
{
    if (!Path.IsEmpty()) return Path == DWActorPath(Actor);
    // Older saves retain their IDs. Never apply an ambiguous old record to several actors.
    return Key == DWLegacyId(Prefix, Actor, Actor->PersistentId) && DWIdCount(Prefix, Actor, Actor->PersistentId) == 1;
}
}
void ADWPlayerCharacter::CaptureSaveData(UDWSaveGame* Save)const
{
    if(!Save)return;Save->CompletedWorldEvents=GetWorld()->GetSubsystem<UDWWorldEventSubsystem>()->ExportCompleted();Save->PlayerTransform=GetActorTransform();Save->bHasPlayerTransform=true;Save->Health=Health;Save->Transformation=Transformation;Save->bYeastForm=bYeastForm;
    Save->SprintAlcoholElapsed=0.f;Save->TransformationDecayElapsed=0.f;Save->Inventory=Inventory->GetSlots();Save->WorldActors.Reset();
    Save->CombatPlayerResources={};
    if(HasCombatVitalsReady()&&WeaponAnimationComponent)
    {
        auto& Block=Save->CombatPlayerResources;Block.SubblockSchema=DWCombatPlayerSave::SubblockSchema;
        Block.Vitals=CombatVitals.Capture(Health);
        Block.bRequiresReleasedEffectsSchema=DWCombatReleasedEffects::HasUnserialized(GetWorld());
        if(const auto* Recovery=UDWCombatRecoverySubsystem::Get(this))Block.bRequiresRecoveryStateSchema=Recovery->HasRecoveryFacts();
        const auto* SaveAuthority=UDWCombatAuthoritySubsystem::Get(this);
        Block.bRequiresShieldStateSchema=WeaponAnimationComponent->HasUnserializedShieldState()||
            (SaveAuthority&&SaveAuthority->GetPendingShieldDamageCount(this)>0);
        const auto& Weapon=WeaponAnimationComponent->GetCombatWeaponStateSnapshot();
        Block.OwnedPermanentWeaponIds=Weapon.OwnedPermanentWeaponIds.Array();Block.CurrentEquipmentId=Weapon.CurrentEquipmentId;
        Block.OwnedPermanentWeaponIds.Sort(FNameLexicalLess());Block.LoadedShotgunShells=Weapon.LoadedShotgunShells;
        Save->Transformation=static_cast<float>(CombatVitals.GetSpecial());Save->bYeastForm=GetCombatBaseForm()==EDWCombatBaseForm::Yeast;
    }
    for(TActorIterator<ADWResourceNode> It(GetWorld());It;++It)
    {FDWPersistedActorState S;S.ActorId=DWActorSaveId(TEXT("Resource:"),*It,It->PersistentId);S.ActorPath=DWActorPath(*It);S.Health=It->RemainingAmount;S.bDestroyed=!It->IsAvailable();S.Transform=It->GetActorTransform();S.bHasTransform=true;Save->WorldActors.Add(S);}
    for(TActorIterator<ADWEnemyNest> It(GetWorld());It;++It)
    {FDWPersistedActorState S;S.ActorId=DWActorSaveId(TEXT("Nest:"),*It,It->PersistentId);S.ActorPath=DWActorPath(*It);S.Health=It->Health;S.bDestroyed=It->Health<=0;S.Transform=It->GetActorTransform();S.bHasTransform=true;S.SpawnProgress=It->SpawnProgress;Save->WorldActors.Add(S);}
    for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
    {if(It->IsActorBeingDestroyed())continue;FDWPersistedActorState S;S.ActorId=DWActorSaveId(TEXT("Enemy:"),*It,It->PersistentId);S.ActorPath=DWActorPath(*It);S.Health=It->Health;S.bDestroyed=It->Health<=0;S.Transform=It->GetActorTransform();S.bHasTransform=true;
     if(auto* Nest=It->SourceNest.Get()){if(!It->IsAlive())continue;S.bRuntimeSpawned=true;S.ActorClass=FSoftClassPath(It->GetClass());S.OwnerId=DWActorSaveId(TEXT("Nest:"),Nest,Nest->PersistentId);S.OwnerPath=DWActorPath(Nest);}
     Save->WorldActors.Add(S);}
}
void ADWPlayerCharacter::ApplySaveData(const UDWSaveGame* Save) {TryApplySaveData(Save);}
bool ADWPlayerCharacter::TryApplySaveData(const UDWSaveGame* InSave)
{
    if(!InSave||IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)return false;
    if(const auto* Recovery=UDWCombatRecoverySubsystem::Get(this);Recovery&&Recovery->HasRecoveryFacts())return false;
    TStrongObjectPtr<UDWSaveGame> Frozen(DuplicateObject<UDWSaveGame>(InSave,GetTransientPackage()));
    const UDWSaveGame* Save=Frozen.Get();
    auto* GI=GetGameInstance<UDWGameInstance>();FString Error;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
    if(!Authority||!Save||!GameplayConfig||!Inventory||bRestoringSave||bGameplayActionInProgress||!GI||!GI->GetCombatRules()||
        !DWCombatPlayerSave::Validate(*GI->GetCombatRules(),*Save,Error)||!Inventory->AreSlotsValid(Save->Inventory))return false;
    const TWeakObjectPtr<UWorld> RestoreWorld(GetWorld());
    const TWeakObjectPtr<UDWGameInstance> RestoreGI(GI);
    auto ExpectedLife=Authority->CaptureActorLife(this);
    bool bExpectLiving=false;
    const bool HadBegunPlay=HasActorBegunPlay();
    auto Current=[&]()
    {
        if(!IsValid(this)||IsActorBeingDestroyed()||!RestoreWorld.IsValid()||GetWorld()!=RestoreWorld.Get()||
            GetWorld()->bIsTearingDown||!RestoreGI.IsValid()||GetGameInstance<UDWGameInstance>()!=RestoreGI.Get()||
            (HadBegunPlay&&!HasActorBegunPlay())||(bExpectLiving&&IsDead()))return false;
        auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return false;
        const auto Life=A->CaptureActorLife(this);return Life.Entity==ExpectedLife.Entity&&Life.Life==ExpectedLife.Life;
    };
    if(!ExpectedLife.Entity||!Current())return false;
    // Until D22 owns the independent pending packets and counter clocks, reject before any
    // cancellation/lifecycle reset. Loading an older snapshot cannot wash the live shield state.
    if((WeaponAnimationComponent&&WeaponAnimationComponent->HasUnserializedShieldState())||
        Authority->GetPendingShieldDamageCount(this)>0)return false;
    if(DWCombatReleasedEffects::HasUnserialized(GetWorld()))return false;
    FDWCombatPlayerStateNotice Notice;
    {
        TGuardValue<bool> RestoreGuard(bRestoringSave,true);
        if(auto* PC=Cast<ADWPlayerController>(GetController()))PC->InvalidateCombatInputLife();
        if(!Current())return false;
        EndFormalRootFacts(EDWMoldRootEnd::Restore);
        CancelCombatActions();if(!Current())return false;
        StopGatherFeedback(false);if(!Current())return false;
        StopPlayerVFX(true);if(!Current())return false;
        StopMovementAudio();if(!Current())return false;
        bHasVFXLocationSample=false;CurrentVFXGroundSpeed=0.f;
        if(!FDWCombatPlayerStateTransaction::Restore(*this,*Save,*GI->GetCombatRules(),Notice,Error))return false;
        ExpectedLife=Notice.Life;bExpectLiving=!IsDead();
        if(!Current())return false;
        if(bExpectLiving)GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        if(!Current())return false;
        GetWorld()->GetSubsystem<UDWWorldEventSubsystem>()->ImportCompleted(Save->CompletedWorldEvents);
        if(!Current())return false;
        if(Save->bHasPlayerTransform)SetActorTransform(Save->PlayerTransform,false,nullptr,ETeleportType::TeleportPhysics);
        if(!Current())return false; // Teleport can synchronously destroy/end the restoring actor.
        TArray<TWeakObjectPtr<ADWEnemyCharacter>> OldSpawned;
        for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)if(It->SourceNest)OldSpawned.Add(*It);
        for(auto Weak:OldSpawned){if(auto* E=Weak.Get();E&&!E->IsActorBeingDestroyed())E->Destroy();if(!Current())return false;}
        for(const auto& S:Save->WorldActors)
        {
            for(TActorIterator<ADWResourceNode> It(GetWorld());It;++It)
                if(DWMatchesSave(S.ActorId,S.ActorPath,TEXT("Resource:"),*It))
                {
                    const TWeakObjectPtr<ADWResourceNode> Actor(*It);
                    It->RestoreRemainingAmount(FMath::Max(0,FMath::RoundToInt(S.Health)));
                    if(!Current())return false;
                    if(Actor.IsValid()&&!Actor->IsActorBeingDestroyed()&&S.bHasTransform)Actor->SetActorTransform(S.Transform);
                    if(!Current())return false;
                }
            for(TActorIterator<ADWEnemyNest> It(GetWorld());It;++It)
                if(DWMatchesSave(S.ActorId,S.ActorPath,TEXT("Nest:"),*It))
                {
                    const TWeakObjectPtr<ADWEnemyNest> Actor(*It);
                    It->SetHealthForLoad(S.bDestroyed?0:S.Health);if(!Current())return false;
                    if(Actor.IsValid()&&!Actor->IsActorBeingDestroyed())
                    {Actor->SpawnProgress=FMath::Clamp(S.SpawnProgress,0.f,Actor->SpawnInterval);if(S.bHasTransform)Actor->SetActorTransform(S.Transform);}
                    if(!Current())return false;
                }
            if(!S.bRuntimeSpawned)for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
                if(!It->IsActorBeingDestroyed()&&DWMatchesSave(S.ActorId,S.ActorPath,TEXT("Enemy:"),*It))
                {
                    const TWeakObjectPtr<ADWEnemyCharacter> Actor(*It);
                    It->SetHealthForLoad(S.bDestroyed?0:S.Health);if(!Current())return false;
                    if(Actor.IsValid()&&!Actor->IsActorBeingDestroyed()&&S.bHasTransform)Actor->SetActorTransform(S.Transform,false,nullptr,ETeleportType::TeleportPhysics);
                    if(!Current())return false;
                }
            if(S.bRuntimeSpawned&&!S.bDestroyed&&S.bHasTransform)
            {
                TWeakObjectPtr<ADWEnemyNest> SavedNest;
                for(TActorIterator<ADWEnemyNest> It(GetWorld());It;++It)if(DWMatchesSave(S.OwnerId,S.OwnerPath,TEXT("Nest:"),*It)){SavedNest=*It;break;}
                UClass* C=S.ActorClass.TryLoadClass<ADWEnemyCharacter>();if(!Current())return false;
                if(SavedNest.IsValid()&&!SavedNest->IsActorBeingDestroyed()&&C)
                {
                    auto* E=GetWorld()->SpawnActorDeferred<ADWEnemyCharacter>(C,S.Transform,SavedNest.Get(),nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
                    if(!Current())return false;
                    if(IsValid(E)&&!E->IsActorBeingDestroyed())
                    {
                        E->SourceNest=SavedNest.Get();E->SetHealthForLoad(S.Health);if(!Current())return false;
                        if(IsValid(E)&&!E->IsActorBeingDestroyed())E->FinishSpawning(S.Transform);
                        if(!Current())return false;
                        if(IsValid(E)&&!E->IsActorBeingDestroyed()&&SavedNest.IsValid()&&!SavedNest->IsActorBeingDestroyed())SavedNest->RegisterRestoredEnemy(E);
                        if(!Current())return false;
                    }
                }
            }
        }
        UpdateFormAppearance(true);if(!Current())return false;
        if(IsDead())Die();if(!Current())return false;
    }
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->PublishRestoredPlayerState(Notice);
    return Current();
}

bool ADWPlayerCharacter::CanSprint_Implementation()const{return false;}
bool ADWPlayerCharacter::CanDash_Implementation()const{return true;}
bool ADWPlayerCharacter::CanThrowAlcohol_Implementation(FVector TargetLocation)const{return true;}
bool ADWPlayerCharacter::CanEnterYeastForm_Implementation()const{return true;}
