#include "DWWeaponAnimationComponent.h"
#include <limits>
#include "DWCombatInputOrigin.h"
#include "DWPlayerCharacter.h"
#include "DWCombatFeedbackComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWCombatItemRegistry.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWDamageVolumeLibrary.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "ProceduralMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

namespace DWWeaponAnimation
{
    static float Positive(float Value,float Fallback=.001f)
    {return FMath::IsFinite(Value)&&Value>0.f?Value:Fallback;}
    template<typename T> static int32 SampleIndex(const TArray<T>& Samples,double Time)
    {
        int32 Result=0;
        for(int32 I=1;I<Samples.Num()&&Samples[I].TimeSeconds<=Time;++I)Result=I;
        return Result;
    }
    static TArray<FVector> InterpolateVertices(const TArray<FVector>& A,const TArray<FVector>& B,float Alpha)
    {
        if(A.Num()!=B.Num()||Alpha<=0.f)return A;
        TArray<FVector> Result;Result.SetNumUninitialized(A.Num());
        for(int32 I=0;I<A.Num();++I)Result[I]=FMath::Lerp(A[I],B[I],Alpha);
        return Result;
    }
    static TArray<FVector> ScaleVerticesAboutPivot(const TArray<FVector>& Vertices,const FVector& Pivot,float Scale)
    {
        if(Scale==1.f)return Vertices;
        TArray<FVector> Result;Result.Reserve(Vertices.Num());
        for(const FVector& Vertex:Vertices)Result.Add(Pivot+(Vertex-Pivot)*Scale);
        return Result;
    }
}

UDWWeaponAnimationComponent::UDWWeaponAnimationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    WeaponSelectionKeys={EKeys::One,EKeys::Two,EKeys::Three,EKeys::Four,EKeys::Five,EKeys::Six,EKeys::Seven,EKeys::Eight};
}
ADWPlayerCharacter* UDWWeaponAnimationComponent::Player() const {return Cast<ADWPlayerCharacter>(GetOwner());}
UDWWeaponAnimationSet* UDWWeaponAnimationComponent::GetSelectedSet() const
{
    for(UDWWeaponAnimationSet* Set:AnimationSets)if(Set&&Set->Weapon==SelectedWeapon)return Set;
    return nullptr;
}
bool UDWWeaponAnimationComponent::IsEnabled() const
{
    if(!bEnabled||!HasCurrentEquipmentIdentity())return false;
    if(SelectedWeapon==EDWAnimatedWeapon::Unarmed)return bUnarmedConfigured;
    if(SelectedWeapon==EDWAnimatedWeapon::Alcohol)return bBottleConfigured;
    if(SelectedWeapon==EDWAnimatedWeapon::MoldBow)return bBowConfigured;
    if(SelectedWeapon==EDWAnimatedWeapon::SporeGun)return bSporeConfigured;
    if(SelectedWeapon==EDWAnimatedWeapon::ShieldAxe)return bShieldAxeConfigured;
    if(SelectedWeapon==EDWAnimatedWeapon::Greatsword)return bFormalSwordConfigured;
    if(SelectedWeapon==EDWAnimatedWeapon::DualDaggers)return bFormalDaggerConfigured;
    if(SelectedWeapon==EDWAnimatedWeapon::Shotgun)return bShotgunConfigured;
    // This also claims the controller input route. An explicitly required but broken binding
    // must not make the controller fall back to legacy Throw/Melee, even with no valid clip.
    if(SelectedWeapon==EDWAnimatedWeapon::Spear&&bEnableFormalSpearExplicitBinding)return true;
    const auto* Set=GetSelectedSet();
    const auto* Clip=Set?Set->FindClip(TEXT("Attack")):nullptr;
    return bEnabled&&Clip&&Clip->Animation&&FMath::IsFinite(Clip->DurationSeconds)&&Clip->DurationSeconds>0.f;
}
void UDWWeaponAnimationComponent::BeginPlay()
{
    Super::BeginPlay();
    if(auto* P=Player())if(P->GetMesh())AddTickPrerequisiteComponent(P->GetMesh());
    AmmoInMagazine=0;
    InitializeCombatWeaponRules();
    InitializeExplicitFormalSpearBinding();
    InitializeUnarmedRules();
    InitializeFormalSporeRules();
    InitializeFormalBowRules();
    InitializeFormalBottleRules();
    InitializeShieldAxeRules();
    InitializeFormalSwordRules();
    InitializeFormalDaggerRules();
    InitializeShotgunRules();
    UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    EndFormalBow();
    EndFormalSpore();
    EndFormalBottle();
    EndFormalSpear();
    EndFormalUnarmed();
    EndFormalAxe();
    EndFormalSword();
    EndFormalDagger();
    EndFormalShotgun();
    EndWeaponResourceActions();
    ResetTrails();
    for(UNiagaraComponent* Component:{PrimaryTrailComponent.Get(),SecondaryTrailComponent.Get()})if(Component)Component->SetAsset(nullptr);
    Super::EndPlay(EndPlayReason);
}
bool UDWWeaponAnimationComponent::IsPrimaryTrailActive() const
{return PrimaryTrailComponent&&PrimaryTrailComponent->IsActive();}
bool UDWWeaponAnimationComponent::IsSecondaryTrailActive() const
{return SecondaryTrailComponent&&SecondaryTrailComponent->IsActive();}
bool UDWWeaponAnimationComponent::LocksMovement() const
{if(bFormalShotgunReload)return false;if(bFormalBowActive)return BowPlan.Phase!=EDWBowPhase::Gap;if(bFormalBottleActive)return BottlePlan.GetPhase()!=EDWBottlePhase::Gap;if(bFormalSporeActive)return SporePlan.GetPhase()!=EDWSporePhase::Gap;if(bFormalSwordActive)return !IsFormalSwordGap();return bActive&&(bFormalDaggerActive||bFormalSpearActive||bFormalUnarmedActive||bFormalAxeActive||IsFormalShieldGuarding()||bFormalShotgunActive||(ActiveSet&&ActiveSet->bLockMovementDuringAction));}
bool UDWWeaponAnimationComponent::StartRole(FName Role,float StartSeconds)
{
    auto* P=Player();auto* Set=GetSelectedSet();
    const auto* Clip=Set?Set->FindClip(Role):nullptr;
    if(!IsEnabled()||!P||!P->IsCombatActionAllowed()||P->bIsRolling||P->IsFormalDashing()||!Clip||!Clip->Animation||!FMath::IsFinite(Clip->DurationSeconds)||Clip->DurationSeconds<=0.f)return false;
    if(GetCombatActionWorldSeconds()<P->TransformUntil)return false;
    if(!bActive)
    {
        P->CancelCombatActions();
        bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;
    }
    ActiveSet=Set;ActiveClip=*Clip;bActive=true;
    if(Role==TEXT("ReloadOne")&&bReloading&&!EnterReloadShellPhase()){FinishAction();return false;}
    ElapsedSeconds=FMath::Clamp(StartSeconds,0.f,Clip->DurationSeconds);
    FiredEvents.Reset();
    for(int32 I=0;I<ActiveClip.DamageEvents.Num();++I)if(ActiveClip.DamageEvents[I].TimeSeconds<ElapsedSeconds)FiredEvents.Add(I);
    ++ActionSerial;ResetTrails();
    FVector Facing=(TargetLocation-P->GetActorLocation()).GetSafeNormal2D();
    if(!Facing.IsNearlyZero())P->SetActorRotation(Facing.Rotation());
    if(LocksMovement())
    {
        P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();
        P->GetCharacterMovement()->bOrientRotationToMovement=false;
    }
    P->SetHarvestHeld(false);P->bSprinting=false;
    UpdateVisuals();return true;
}
bool UDWWeaponAnimationComponent::PlayRole(FName Role,FVector Target)
{

    if(bActive||!HasCurrentEquipmentIdentity()||SelectedWeapon==EDWAnimatedWeapon::Unarmed||SelectedWeapon==EDWAnimatedWeapon::ShieldAxe||SelectedWeapon==EDWAnimatedWeapon::Greatsword||SelectedWeapon==EDWAnimatedWeapon::DualDaggers||GetCombatActionWorldSeconds()<NextAttackTime||Target.ContainsNaN()||(SelectedWeapon==EDWAnimatedWeapon::Spear&&RequiresFormalSpearBinding()))return false;
    // Resource weapons cannot use the unrestricted visual hook to bypass the native request/receipt.
    if(SelectedWeapon==EDWAnimatedWeapon::Alcohol||SelectedWeapon==EDWAnimatedWeapon::Shotgun||SelectedWeapon==EDWAnimatedWeapon::SporeGun||SelectedWeapon==EDWAnimatedWeapon::MoldBow)return false;
    TargetLocation=Target;
    return StartRole(Role);
}
bool UDWWeaponAnimationComponent::PrimaryPressed(FVector Target)
{
    auto* P=Player();auto* Set=GetSelectedSet();
    if(!IsEnabled()||!P||!P->IsCombatActionAllowed()||P->IsFormalDashing()||Target.ContainsNaN())return false;

    if(SelectedWeapon==EDWAnimatedWeapon::Alcohol)return BeginFormalBottle(Target);
    if(SelectedWeapon==EDWAnimatedWeapon::MoldBow)return BeginFormalBow(Target);
    if(SelectedWeapon==EDWAnimatedWeapon::SporeGun)return BeginFormalSpore(Target);
    if(SelectedWeapon==EDWAnimatedWeapon::Unarmed)return BeginFormalUnarmed(Target);
    if(SelectedWeapon==EDWAnimatedWeapon::ShieldAxe)return BeginFormalAxeSwing(Target);
    if(SelectedWeapon==EDWAnimatedWeapon::Greatsword)return BeginFormalSword(Target);
    if(SelectedWeapon==EDWAnimatedWeapon::DualDaggers)return RequestFormalDagger(Target);
    if(SelectedWeapon==EDWAnimatedWeapon::Shotgun)return StartFormalShotgun(Target,false);
    if(SelectedWeapon==EDWAnimatedWeapon::Spear&&RequiresFormalSpearBinding())return BeginFormalSpear(Target);
    // Their real inventory/projectile adapters are not implemented in this resource slice.
    // Fail closed instead of retaining free legacy cone shots or temporary magazines.
    if(SelectedWeapon==EDWAnimatedWeapon::SporeGun||SelectedWeapon==EDWAnimatedWeapon::MoldBow)return false;
    const bool bShotgun=SelectedWeapon==EDWAnimatedWeapon::Shotgun;
    if(bShotgun&&!CanUseShotgunResources())return false;
    if(GetCombatActionWorldSeconds()<NextAttackTime)return false;
    if(bActive)
    {
        if(!bShotgun||!bReloading||CombatWeaponResources.Snapshot.LoadedShotgunShells<CombatWeaponResources.ShotgunRoundsPerShot)return false;
        CancelAction(); // R07: only a qualified loaded shot may interrupt an unfinished shell.
    }
    if(bShotgun&&CombatWeaponResources.Snapshot.LoadedShotgunShells<CombatWeaponResources.ShotgunRoundsPerShot)return RequestReload();
    if(!bShotgun&&Set->MagazineCapacity>0)return false;
    TargetLocation=Target;
    if(!StartRole(Set->FindClip(TEXT("Raise"))?FName(TEXT("Raise")):FName(TEXT("Attack"))))return false;

    if(bShotgun)
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(P);
        if(!Authority||!CanUseShotgunResources()||!bActive){FinishAction();return false;}
        MagazineResourceAction=Authority->BeginAction(P);
        if(!MagazineResourceAction.Id){FinishAction();return false;}
    }
    // CancelCombatActions above clears held inputs; latch only after a successful start.
    bPrimaryHeld=true;bCharging=Set->ChargeSeconds>0.f;bChargeReady=false;
    NextAttackTime=GetCombatActionWorldSeconds()+DWWeaponAnimation::Positive(Set->AttackIntervalSeconds);
    return true;
}
void UDWWeaponAnimationComponent::PrimaryReleased()
{
    bPrimaryHeld=false;
    if(bFormalSporeActive){CancelAction();return;}
    if(bFormalSwordActive)
    {if(SwordPlan.GetPhase()==EDWSwordPhase::Charge){if(SwordPlan.IsChargeReady())QueueFormalSwordChargeRelease();else CancelAction();}return;}
    if(!bActive||!bCharging||!ActiveSet)return;
    if(!bChargeReady&&ElapsedSeconds<ActiveSet->ChargeSeconds)
    {
        bCharging=false;
        if(!StartRole(TEXT("Cancel")))FinishAction();
    }
    else
    {
        const float Resume=ActiveSet->ChargeSeconds;bCharging=false;bChargeReady=false;
        StartRole(TEXT("Attack"),Resume);
    }
}
bool UDWWeaponAnimationComponent::SecondaryPressed()
{
    if(SelectedWeapon==EDWAnimatedWeapon::ShieldAxe&&bShieldAxeConfigured)return BeginFormalShieldGuard();
    if(!IsEnabled()||bActive||GetCombatActionWorldSeconds()<NextAttackTime||SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe)return false;
    if(auto* P=Player())TargetLocation=P->GetActorLocation()+P->GetActorForwardVector()*500.f;
    if(!StartRole(TEXT("BlockEnter")))return false;
    bSecondaryHeld=true;BlockStartTime=GetCombatActionWorldSeconds();return true;
}
void UDWWeaponAnimationComponent::SecondaryReleased()
{
    if(SelectedWeapon==EDWAnimatedWeapon::ShieldAxe&&bShieldAxeConfigured){ReleaseFormalShieldGuard();return;}
    bSecondaryHeld=false;
    if(bActive&&IsBlocking())if(!StartRole(TEXT("BlockExit")))FinishAction();
}
bool UDWWeaponAnimationComponent::RequestReload()
{
    if(SelectedWeapon==EDWAnimatedWeapon::Shotgun&&bShotgunConfigured)
    {auto* P=Player();if(!P||!StartFormalShotgun(P->GetActorLocation()+P->GetActorForwardVector()*1000.,true))return false;PublishFormalShotgunStart();return true;}
    auto* Set=GetSelectedSet();auto* P=Player();
    auto* Authority=UDWCombatAuthoritySubsystem::Get(P);
    if(!Authority||!P||P->IsFormalDashing()||!IsEnabled()||bActive||GetCombatActionWorldSeconds()<NextAttackTime||!Set||!HasShotgunReloadCapacityAndStock())return false;
    const auto* ReloadClip=Set->FindClip(TEXT("ReloadOne"));
    // Preserve the approved source clock; mismatched future rules/art must be coordinated, never auto-retimed.
    if(!ReloadClip||!FMath::IsFinite(ReloadClip->DurationSeconds)||
        !FMath::IsNearlyEqual(static_cast<double>(ReloadClip->DurationSeconds),CombatWeaponResources.ReloadOneSeconds,1.e-6))return false;
    if(!StartRole(Set->FindClip(TEXT("ReloadEnter"))?FName(TEXT("ReloadEnter")):FName(TEXT("ReloadOne"))))return false;
    if(!HasShotgunReloadCapacityAndStock()||!bActive||ActiveSet!=Set){FinishAction();return false;}
    ReloadResourceAction=Authority->BeginAction(P);
    if(!ReloadResourceAction.Id){FinishAction();return false;}
    bReloading=true;ReloadShellOrdinal=0;
    if(ActiveClip.Role==TEXT("ReloadOne")&&!EnterReloadShellPhase()){FinishAction();return false;}
    return true;
}
bool UDWWeaponAnimationComponent::IsBlocking() const
{
    if(bShieldAxeConfigured)return IsFormalShieldGuarding();
    const FName Role=ActiveClip.Role;
    return bActive&&SelectedWeapon==EDWAnimatedWeapon::ShieldAxe&&(Role==TEXT("BlockEnter")||Role==TEXT("BlockHold")||Role==TEXT("BlockReact")||Role==TEXT("ParryReact"));
}
float UDWWeaponAnimationComponent::FilterIncomingDamage(float Amount,AActor* Causer)
{
    if(bShieldAxeConfigured)return Amount; // The authority owns typed impact snapshots and pending decisions.
    if(!IsBlocking()||!ActiveSet||Amount<=0.f)return Amount;
    auto* P=Player();
    if(Causer&&P&&FVector::DotProduct((Causer->GetActorLocation()-P->GetActorLocation()).GetSafeNormal2D(),P->GetActorForwardVector())<0.f)return Amount;
    const bool bPerfect=GetCombatActionWorldSeconds()-BlockStartTime<=FMath::Max(0.f,ActiveSet->PerfectBlockSeconds);
    const float Result=Amount*(1.f-FMath::Clamp(ActiveSet->BlockDamageReduction,0.f,1.f));
    StartRole(bPerfect?FName(TEXT("ParryReact")):FName(TEXT("BlockReact")));
    return Result;
}
void UDWWeaponAnimationComponent::CancelAction()
{
    EndFormalBow();
    CancelFormalSporeFacts();
    EndFormalBottle();
    EndFormalSpear();
    EndFormalUnarmed();
    EndFormalAxe();
    CancelFormalSwordFacts();
    EndFormalDagger();
    EndFormalShotgun();
    EndWeaponResourceActions();
    if(bActive)if(auto* P=Player())if(P->GetCharacterMovement())P->GetCharacterMovement()->bOrientRotationToMovement=bOldOrient;
    bActive=bPrimaryHeld=bSecondaryHeld=bCharging=bChargeReady=bReloading=false;
    PrimaryInputOrigin.Reset();SecondaryInputOrigin.Reset();
    ResetTrails();
}
void UDWWeaponAnimationComponent::FinishAction()
{
    EndFormalBow();
    EndFormalSpore();
    EndFormalBottle();
    EndFormalSpear();
    EndFormalUnarmed();
    EndFormalAxe();
    EndFormalSword();
    EndFormalDagger();
    EndFormalShotgun();
    EndWeaponResourceActions();
    if(auto* P=Player())if(P->GetCharacterMovement())P->GetCharacterMovement()->bOrientRotationToMovement=bOldOrient;
    bActive=bCharging=bChargeReady=bReloading=false;
    // Keep the outgoing sequence and terminal pose for the existing AnimBP blend.
    ElapsedSeconds=ActiveClip.DurationSeconds;
    ResetTrails();
}
float UDWWeaponAnimationComponent::GetPositionSeconds() const
{
    const auto& Presentation=GetPresentationClip();
    return Presentation.Animation?static_cast<float>(Presentation.Animation->GetPlayLength()*FMath::Clamp(GetPresentationSourceSeconds()/DWWeaponAnimation::Positive(Presentation.DurationSeconds),0.,1.)):0.f;
}
void UDWWeaponAnimationComponent::DispatchEvent(int32 Index)
{
    if(FiredEvents.Contains(Index)||!ActiveClip.DamageEvents.IsValidIndex(Index))return;
    FiredEvents.Add(Index);++DispatchedEventCount;
    auto* P=Player();auto* Set=ActiveSet.Get();const auto Event=ActiveClip.DamageEvents[Index];
    if(!IsValid(P)||!IsValid(Set))return;
    // A damage/inventory/Blueprint callback can cancel or replace the current action.
    const EDWAnimatedWeapon EventWeapon=SelectedWeapon;
    const FName EventRole=ActiveClip.Role;
    const int32 EventSerial=ActionSerial;
    const TWeakObjectPtr<ADWPlayerCharacter> EventOwner(P);
    const TWeakObjectPtr<UDWWeaponAnimationSet> EventSet(Set);
    const float Damage=Set->Damage*FMath::Max(0.f,Event.DamageMultiplier);
    const float KnockbackDistance=Set->KnockbackDistance,KnockbackSeconds=Set->KnockbackSeconds;
    const bool bSingleTarget=Set->bSingleTarget;
    auto IsCurrentEvent=[this,EventOwner,EventSet,EventWeapon,EventRole,EventSerial]()
    {
        const auto* Owner=EventOwner.Get();
        return IsValid(this)&&!IsBeingDestroyed()&&bActive&&ActionSerial==EventSerial
            &&SelectedWeapon==EventWeapon&&ActiveClip.Role==EventRole&&ActiveSet==EventSet.Get()
            &&Owner&&Player()==Owner&&!Owner->IsActorBeingDestroyed()&&Owner->IsCombatActionAllowed();
    };
    if(!IsCurrentEvent())return;

    if(EventWeapon==EDWAnimatedWeapon::Alcohol)return; // Bottle release is a native rules boundary, never a visual marker.
    if(EventWeapon==EDWAnimatedWeapon::Shotgun&&bShotgunConfigured)return; // Source markers never spend or hit.
    if(EventWeapon==EDWAnimatedWeapon::Greatsword||EventWeapon==EDWAnimatedWeapon::DualDaggers)return; // Gameplay phases are independent of visual markers.
    if((EventWeapon==EDWAnimatedWeapon::Spear&&bFormalSpearActive)||(EventWeapon==EDWAnimatedWeapon::SporeGun&&bFormalSporeActive)||(EventWeapon==EDWAnimatedWeapon::MoldBow&&bFormalBowActive))
    {
        OnAnimationGameplayEvent.Broadcast(EventWeapon,EventRole,Index);
        return; // Formal spear uses its one phase packet; source markers cannot call legacy damage.
    }
    if(EventWeapon==EDWAnimatedWeapon::SporeGun||EventWeapon==EDWAnimatedWeapon::MoldBow)return;
    if(EventWeapon==EDWAnimatedWeapon::Shotgun)
    {
        bMagazineShotEventDue=true;
        const auto ResourceResult=FDWCombatInventoryTransaction::CommitMagazineShot(*this);
        if(!ResourceResult.bCommittedThisCall){bMagazineShotEventDue=false;return;}
        if(!IsCurrentEvent())return;
    }
    else if(Set->MagazineCapacity>0)return;
    if(Set->bApplyBasicDamage)
    {
        if(EventWeapon==EDWAnimatedWeapon::Alcohol)
        {
            // The weapon tuning limits horizontal reach; the existing throw path retains its hard limit and inventory rules.
            const float Range=FMath::Max(1.f,Set->DamageRange*FMath::Max(0.f,Event.RangeMultiplier));
            const FVector Origin=P->GetActorLocation(),Offset=TargetLocation-Origin;
            FVector ThrowTarget=TargetLocation;
            if(Offset.Size2D()>Range)
            {
                const FVector Clamped=Origin+Offset.GetSafeNormal2D()*Range;
                ThrowTarget.X=Clamped.X;ThrowTarget.Y=Clamped.Y;
            }
            TGuardValue<bool> ReleaseGuard(bAlcoholReleaseEventDue,true);
            P->ThrowAlcoholAt(ThrowTarget);
            if(!IsCurrentEvent())return;
        }
        else
        {
            const float Range=FMath::Max(1.f,Set->DamageRange*FMath::Max(0.f,Event.RangeMultiplier));
            const FVector Origin=P->GetActorLocation(),Forward=P->GetActorForwardVector().GetSafeNormal2D();
            TArray<FOverlapResult> Overlaps;
            FCollisionQueryParams Params(SCENE_QUERY_STAT(DWAnimatedWeapon),false,P);
            GetWorld()->OverlapMultiByObjectType(Overlaps,Origin,FQuat::Identity,FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),FCollisionShape::MakeSphere(Range),Params);
            TArray<AActor*> Targets;
            const float Cosine=FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Set->ArcDegrees,1.f,360.f)*.5f));
            for(const auto& Hit:Overlaps)
            {
                AActor* Target=Hit.GetActor();
                if(!Target||(!Target->IsA<ADWEnemyCharacter>()&&!Target->IsA<ADWEnemyNest>())||!UDWDamageVolumeLibrary::IsDamageHitComponent(Hit.GetComponent())||!P->CanDamageCombatTarget(Target))continue;
                const FVector Offset=Target->GetActorLocation()-Origin;
                if(FVector::DotProduct(Offset.GetSafeNormal2D(),Forward)<Cosine||!P->HasCombatLineOfSight(Target,Origin,Target->GetActorLocation()))continue;
                Targets.AddUnique(Target);
            }
            Targets.Sort([Origin](const AActor& A,const AActor& B){return FVector::DistSquared(A.GetActorLocation(),Origin)<FVector::DistSquared(B.GetActorLocation(),Origin);});
            TSet<TWeakObjectPtr<AActor>> HitActors;
            for(AActor* Target:Targets)
            {
                if(!IsCurrentEvent())return;
                P->DamageCombatTarget(Target,Damage,KnockbackDistance,KnockbackSeconds,Forward,HitActors);
                if(!IsCurrentEvent())return;
                if(bSingleTarget)break;
            }
        }
    }
    if(!IsCurrentEvent())return;
    OnAnimationGameplayEvent.Broadcast(EventWeapon,EventRole,Index);
}
double UDWWeaponAnimationComponent::GetCombatActionWorldSeconds() const
{return bAdvancingCombatClock?CombatAdvanceEndWorldSeconds:(GetWorld()?GetWorld()->GetTimeSeconds():0.);}
double UDWWeaponAnimationComponent::GetNextCombatBoundarySeconds(double Start) const
{
    const double Never=std::numeric_limits<double>::infinity();
    const auto* P=Player();const auto* Set=GetSelectedSet();
    if(!FMath::IsFinite(Start)||!bEnabled||!IsEnabled()||!P||!P->IsCombatActionAllowed())return Never;
    if(SelectedWeapon==EDWAnimatedWeapon::Alcohol)return bFormalBottleActive?BottlePlan.Remaining():Never;
    if(SelectedWeapon==EDWAnimatedWeapon::MoldBow)return GetNextBowBoundarySeconds(Start);
    if(SelectedWeapon==EDWAnimatedWeapon::SporeGun)return GetNextSporeBoundarySeconds(Start);
    if(SelectedWeapon==EDWAnimatedWeapon::Unarmed)return bFormalUnarmedActive?UnarmedPlan.GetPhaseRemainingSeconds():Never;
    if(SelectedWeapon==EDWAnimatedWeapon::ShieldAxe)return bFormalAxeActive?AxePlan.GetPhaseRemainingSeconds():Never;
    if(SelectedWeapon==EDWAnimatedWeapon::Greatsword)return bFormalSwordActive?(bSwordReleasePending?0.:FMath::Min(SwordPlan.GetRemainingSeconds(),GetNextSwordPresentationBoundary())):Never;
    if(SelectedWeapon==EDWAnimatedWeapon::DualDaggers)return bFormalDaggerActive?DaggerPlan.GetPhaseRemainingSeconds():Never;
    if(SelectedWeapon==EDWAnimatedWeapon::Shotgun&&bShotgunConfigured)return bFormalShotgunReload?ShotgunReloadRemaining:bFormalShotgunActive?ShotgunPlan.Remaining:Never;
    if(!Set)return Never;
    const bool Repeat=SelectedWeapon!=EDWAnimatedWeapon::Shotgun&&
        !(SelectedWeapon==EDWAnimatedWeapon::Spear&&RequiresFormalSpearBinding())&&bPrimaryHeld&&Set->bRepeatWhileHeld;
    if(!bActive)return Repeat&&FMath::IsFinite(NextAttackTime)?FMath::Max(0.,static_cast<double>(NextAttackTime)-Start):Never;
    if(!ActiveSet||!FMath::IsFinite(ActiveClip.DurationSeconds)||ActiveClip.DurationSeconds<=0.f)return Never;
    if(bFormalSpearActive)return GetNextFormalSpearBoundarySeconds();
    // An authored charge without a Hold clip can remain intentionally frozen at full charge.
    if(bCharging&&bChargeReady&&bPrimaryHeld&&ActiveClip.Role==TEXT("Attack")&&ActiveSet->bHoldAtFullCharge)return Never;
    double Boundary=FMath::Max(0.,static_cast<double>(ActiveClip.DurationSeconds)-ElapsedSeconds);
    if(ActiveClip.Role==TEXT("Attack")&&Repeat&&ElapsedSeconds>=ActiveClip.DurationSeconds&&
        (ActiveSet->MagazineCapacity<=0||AmmoInMagazine>0))
        Boundary=FMath::Max(0.,static_cast<double>(NextAttackTime)-Start);
    if(bCharging&&!bChargeReady&&ActiveClip.Role==TEXT("Attack")&&ActiveSet->ChargeSeconds>0.f)
        Boundary=FMath::Min(Boundary,FMath::Max(0.,static_cast<double>(ActiveSet->ChargeSeconds)-ElapsedSeconds));
    for(int32 I=0;I<ActiveClip.DamageEvents.Num();++I)
    {
        const double Time=ActiveClip.DamageEvents[I].TimeSeconds;
        if(!FiredEvents.Contains(I)&&FMath::IsFinite(Time)&&Time>=ElapsedSeconds&&Time<=ActiveClip.DurationSeconds)
            Boundary=FMath::Min(Boundary,Time-ElapsedSeconds);
    }
    return Boundary;
}
bool UDWWeaponAnimationComponent::AdvanceAnimation(double Dt)
{
    auto* P=Player();
    if(bAdvancingCombatClock||!FMath::IsFinite(Dt)||Dt<0.||!P||!P->IsCombatActionAllowed())return false;
    if(!bEnabled||(SelectedWeapon!=EDWAnimatedWeapon::Unarmed&&SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe&&SelectedWeapon!=EDWAnimatedWeapon::Shotgun&&SelectedWeapon!=EDWAnimatedWeapon::Greatsword&&SelectedWeapon!=EDWAnimatedWeapon::DualDaggers&&SelectedWeapon!=EDWAnimatedWeapon::MoldBow&&SelectedWeapon!=EDWAnimatedWeapon::SporeGun&&SelectedWeapon!=EDWAnimatedWeapon::Alcohol&&!GetSelectedSet())||!IsEnabled()){if(bActive)CancelAction();return IsValid(P)&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();}
    if(CombatBoundaryRevision==MAX_uint64){UE_LOG(LogTemp,Error,TEXT("Weapon boundary revision exhausted; frame stopped."));return false;}
    const auto* BeforeOwner=P;auto* Authority=UDWCombatAuthoritySubsystem::Get(P);
    const TWeakObjectPtr<UWorld> BeforeWorld(GetWorld());const bool HadBegunPlay=P->HasActorBegunPlay();
    const uint64 BeforeEpoch=Authority?Authority->GetWorldEpoch():0;
    const auto BeforeLife=Authority?Authority->CaptureActorLife(P):DWCombatCommit::FEntityLife{};
    const int32 Serial=ActionSerial,Fired=FiredEvents.Num();const double Clock=ElapsedSeconds,Lock=NextAttackTime;
    const auto Phase=SpearPlan.GetPhase(),UnarmedPhase=UnarmedPlan.GetPhase(),AxePhase=AxePlan.GetPhase();const auto DaggerPhase=DaggerPlan.GetPhase();const auto SwordPhase=SwordPlan.GetPhase();const auto Weapon=SelectedWeapon;
    const auto BowPhase=BowPlan.Phase;const auto SporePhase=SporePlan.GetPhase();const auto BottlePhase=BottlePlan.GetPhase();const bool Held=bPrimaryHeld;
    const auto ShotgunPhase=ShotgunPlan.Phase;const double ReloadRemaining=ShotgunReloadRemaining;
    const uint64 ReloadOrdinal=ReloadShellOrdinal;
    const bool Active=bActive,Charging=bCharging,Ready=bChargeReady,ReloadCommitted=bReloadShellCommitted;
    TGuardValue<bool> Advancing(bAdvancingCombatClock,true);
    CombatAdvanceEndWorldSeconds=P->GetCombatFrameSliceStartWorldSeconds(Dt)+Dt;bCombatAdvanceInterrupted=false;
    AdvanceAnimationStep(Dt);
    if(!IsValid(this)||IsBeingDestroyed())return false;
    if(Serial!=ActionSerial||Fired!=FiredEvents.Num()||Clock!=ElapsedSeconds||Lock!=NextAttackTime||
        Phase!=SpearPlan.GetPhase()||UnarmedPhase!=UnarmedPlan.GetPhase()||AxePhase!=AxePlan.GetPhase()||DaggerPhase!=DaggerPlan.GetPhase()||SwordPhase!=SwordPlan.GetPhase()||Weapon!=SelectedWeapon||Active!=bActive||Charging!=bCharging||
        Ready!=bChargeReady||ReloadCommitted!=bReloadShellCommitted||BowPhase!=BowPlan.Phase||SporePhase!=SporePlan.GetPhase()||BottlePhase!=BottlePlan.GetPhase()||Held!=bPrimaryHeld||ShotgunPhase!=ShotgunPlan.Phase||ReloadRemaining!=ShotgunReloadRemaining||ReloadOrdinal!=ReloadShellOrdinal)++CombatBoundaryRevision;
    P=Player();if(!P||P!=BeforeOwner||P->IsActorBeingDestroyed()||!P->IsCombatActionAllowed()||
        !BeforeWorld.IsValid()||GetWorld()!=BeforeWorld.Get()||(HadBegunPlay&&!P->HasActorBegunPlay()))return false;
    Authority=UDWCombatAuthoritySubsystem::Get(P);
    const auto AfterLife=Authority?Authority->CaptureActorLife(P):DWCombatCommit::FEntityLife{};
    return !bCombatAdvanceInterrupted&&Authority&&BeforeEpoch==Authority->GetWorldEpoch()&&
        BeforeLife.Entity==AfterLife.Entity&&BeforeLife.Life==AfterLife.Life;
}
void UDWWeaponAnimationComponent::AdvanceAnimationStep(double Dt)
{
    auto* P=Player();
    if(P&&P->IsCombatSimulationSuspended()){bCombatAdvanceInterrupted=true;return;}
    if(!bEnabled||(SelectedWeapon!=EDWAnimatedWeapon::Unarmed&&SelectedWeapon!=EDWAnimatedWeapon::ShieldAxe&&SelectedWeapon!=EDWAnimatedWeapon::Shotgun&&SelectedWeapon!=EDWAnimatedWeapon::Greatsword&&SelectedWeapon!=EDWAnimatedWeapon::DualDaggers&&SelectedWeapon!=EDWAnimatedWeapon::MoldBow&&SelectedWeapon!=EDWAnimatedWeapon::SporeGun&&SelectedWeapon!=EDWAnimatedWeapon::Alcohol&&!GetSelectedSet())||!IsEnabled()||!P||!P->IsCombatActionAllowed()){bCombatAdvanceInterrupted=true;CancelAction();return;}
    if(SelectedWeapon==EDWAnimatedWeapon::Alcohol){if(bFormalBottleActive)AdvanceFormalBottle(Dt);return;}
    if(SelectedWeapon==EDWAnimatedWeapon::MoldBow){AdvanceFormalBow(Dt);return;}
    if(SelectedWeapon==EDWAnimatedWeapon::SporeGun){AdvanceFormalSpore(Dt);return;}
    if(SelectedWeapon==EDWAnimatedWeapon::Unarmed){if(bFormalUnarmedActive)AdvanceFormalUnarmed(Dt);return;}
    if(SelectedWeapon==EDWAnimatedWeapon::ShieldAxe){if(bFormalAxeActive)AdvanceFormalAxe(Dt);return;}
    if(SelectedWeapon==EDWAnimatedWeapon::Greatsword){if(bFormalSwordActive)AdvanceFormalSword(Dt);else AdvanceFormalSwordVisualTail(Dt);return;}
    if(SelectedWeapon==EDWAnimatedWeapon::DualDaggers){if(bFormalDaggerActive)AdvanceFormalDagger(Dt);else AdvanceFormalDaggerVisualTail(Dt);return;}
    if(SelectedWeapon==EDWAnimatedWeapon::Shotgun&&bShotgunConfigured){if(bFormalShotgunActive||bFormalShotgunReload)AdvanceFormalShotgun(Dt);return;}
    if(bFormalSpearActive){AdvanceFormalSpear(Dt);return;}
    if(!bActive)
    {
        if(SelectedWeapon!=EDWAnimatedWeapon::Shotgun&&!(SelectedWeapon==EDWAnimatedWeapon::Spear&&RequiresFormalSpearBinding())&&bPrimaryHeld&&GetSelectedSet()->bRepeatWhileHeld&&GetCombatActionWorldSeconds()>=NextAttackTime)
        {
            // Internal continuation keeps the same physical origin. A new public/BP press does not.
            const auto HeldOrigin=PrimaryInputOrigin;const auto Weapon=SelectedWeapon;
            const int32 Expected=ActionSerial+1;
            if(PrimaryPressed(TargetLocation))
            {
                if(!bActive||ActionSerial!=Expected||SelectedWeapon!=Weapon||Player()!=P||!P->IsCombatActionAllowed())
                {bCombatAdvanceInterrupted=true;return;}
                PrimaryInputOrigin=HeldOrigin;
            }
        }
        return;
    }
    AdvanceInternal(Dt,64);
}
void UDWWeaponAnimationComponent::AdvanceInternal(double Dt,int32 TransitionBudget)
{
    if(!bActive||!FMath::IsFinite(Dt)||Dt<0.)return;
    if(TransitionBudget<=0){bCombatAdvanceInterrupted=true;UE_LOG(LogTemp,Error,TEXT("Weapon zero-boundary transition budget exhausted; frame stopped."));return;}
    const int32 Serial=ActionSerial;
    const TWeakObjectPtr<ADWPlayerCharacter> AdvancingOwner(Player());
    const EDWAnimatedWeapon AdvancingWeapon=SelectedWeapon;
    const FName AdvancingRole=ActiveClip.Role;
    const double Previous=ElapsedSeconds;
    const double Next=static_cast<double>(ElapsedSeconds)+Dt;
    if(bCharging&&ActiveClip.Role==TEXT("Attack")&&ActiveSet->ChargeSeconds>0.f&&Next>=ActiveSet->ChargeSeconds)
    {
        if(bPrimaryHeld&&ActiveSet->bHoldAtFullCharge)
        {
            bChargeReady=true;ElapsedSeconds=ActiveSet->ChargeSeconds;
            if(ActiveSet->FindClip(TEXT("ChargeHold")))
            {
                const int32 Expected=ActionSerial+1;
                if(!StartRole(TEXT("ChargeHold"))||!bActive||ActionSerial!=Expected||
                    SelectedWeapon!=AdvancingWeapon||ActiveClip.Role!=TEXT("ChargeHold")||
                    !AdvancingOwner.IsValid()||Player()!=AdvancingOwner.Get()||!AdvancingOwner->IsCombatActionAllowed())
                    bCombatAdvanceInterrupted=true;
            }
            return;
        }
        bCharging=false;
    }
    ElapsedSeconds=FMath::Min(Next,static_cast<double>(ActiveClip.DurationSeconds));
    for(int32 I=0;I<ActiveClip.DamageEvents.Num();++I)
    {
        const float Time=ActiveClip.DamageEvents[I].TimeSeconds;
        if(Time>=Previous&&Time<=ElapsedSeconds)DispatchEvent(I);
        const auto* Owner=AdvancingOwner.Get();
        if(!IsValid(this)||IsBeingDestroyed()||!bActive||Serial!=ActionSerial
            ||SelectedWeapon!=AdvancingWeapon||ActiveClip.Role!=AdvancingRole
            ||!Owner||Player()!=Owner||Owner->IsActorBeingDestroyed()||!Owner->IsCombatActionAllowed()){bCombatAdvanceInterrupted=true;return;}
    }
    if(Next<ActiveClip.DurationSeconds)return;
    const double Carry=FMath::Max(0.,Next-static_cast<double>(ActiveClip.DurationSeconds));
    auto Continue=[this,Carry,TransitionBudget,AdvancingOwner,AdvancingWeapon](FName NextRole)
    {
        const int32 Expected=ActionSerial+1;
        if(!StartRole(NextRole)){FinishAction();return;}
        auto* Owner=AdvancingOwner.Get();
        if(!bActive||ActionSerial!=Expected||SelectedWeapon!=AdvancingWeapon||ActiveClip.Role!=NextRole||
            !Owner||Player()!=Owner||Owner->IsActorBeingDestroyed()||!Owner->IsCombatActionAllowed())
        {bCombatAdvanceInterrupted=true;return;}
        AdvanceInternal(Carry,TransitionBudget-1);
    };
    // Source fire/reload clips may carry an authoring loop flag; gameplay owns repetition and ammunition.
    if(ActiveClip.bLoop&&ActiveClip.Role!=TEXT("Attack")&&ActiveClip.Role!=TEXT("ReloadOne")){ElapsedSeconds=FMath::Fmod(Next,static_cast<double>(ActiveClip.DurationSeconds));return;}
    const FName Role=ActiveClip.Role;
    if(Role==TEXT("Raise"))
    {
        NextAttackTime=GetCombatActionWorldSeconds()-Carry+DWWeaponAnimation::Positive(ActiveSet->AttackIntervalSeconds);
        Continue(TEXT("Attack"));return;
    }
    if(Role==TEXT("BlockEnter")||Role==TEXT("BlockReact")||Role==TEXT("ParryReact"))
    {Continue(bSecondaryHeld?FName(TEXT("BlockHold")):FName(TEXT("BlockExit")));return;}
    if(Role==TEXT("ReloadEnter")){Continue(TEXT("ReloadOne"));return;}
    if(Role==TEXT("ReloadOne"))
    {

        // A callback may pause after committing this shell. Resume only the transition, never pay twice.
        if(bReloadShellCommitted)
        {if(HasShotgunReloadCapacityAndStock())Continue(TEXT("ReloadOne"));else Continue(TEXT("ReloadExit"));return;}
        const auto ResourceAction=ReloadResourceAction;
        const uint64 ResourceRestoreEpoch=CombatWeaponResources.RestoreEpoch;
        bReloadShellCompletionDue=true;
        const auto ResourceResult=FDWCombatInventoryTransaction::CommitReloadShell(*this);
        if(!ResourceResult.bCommittedThisCall){bReloadShellCompletionDue=false;FinishAction();return;}
        auto* Owner=AdvancingOwner.Get();
        if(!IsValid(this)||IsBeingDestroyed()||!IsValid(Owner)||Owner->IsActorBeingDestroyed()||!Owner->IsCombatActionAllowed()||
            !bActive||!bReloading||Serial!=ActionSerial||ActiveClip.Role!=TEXT("ReloadOne")||
            ReloadResourceAction.Epoch!=ResourceAction.Epoch||ReloadResourceAction.Id!=ResourceAction.Id||
            CombatWeaponResources.RestoreEpoch!=ResourceRestoreEpoch){bCombatAdvanceInterrupted=true;return;}
        if(HasShotgunReloadCapacityAndStock())Continue(TEXT("ReloadOne"));
        else Continue(TEXT("ReloadExit"));
        return;
    }
    if(Role==TEXT("Attack")&&SelectedWeapon!=EDWAnimatedWeapon::Shotgun&&bPrimaryHeld&&ActiveSet->bRepeatWhileHeld&&(ActiveSet->MagazineCapacity<=0||AmmoInMagazine>0))
    {
        const double SliceEnd=GetCombatActionWorldSeconds();
        const double UntilNext=static_cast<double>(NextAttackTime)-SliceEnd;
        if(UntilNext>0.)return;
        const double BeginTime=FMath::Max(SliceEnd-Carry,static_cast<double>(NextAttackTime));
        const double RepeatCarry=FMath::Max(0.,SliceEnd-BeginTime);
        NextAttackTime=BeginTime+DWWeaponAnimation::Positive(ActiveSet->AttackIntervalSeconds);
        const int32 Expected=ActionSerial+1;
        if(StartRole(TEXT("Attack")))
        {
            if(!bActive||ActionSerial!=Expected||SelectedWeapon!=AdvancingWeapon||ActiveClip.Role!=TEXT("Attack"))
            {bCombatAdvanceInterrupted=true;return;}
            AdvanceInternal(RepeatCarry,TransitionBudget-1);
        }
        else FinishAction();
        return;
    }
    if(Role==TEXT("Attack")&&ActiveSet->FindClip(TEXT("Lower"))){Continue(TEXT("Lower"));return;}
    FinishAction();
}

void UDWWeaponAnimationComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{Super::TickComponent(Dt,TickType,TickFunction);UpdateVisuals();}

void UDWWeaponAnimationComponent::ApplyGeometry(UProceduralMeshComponent* Component,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,const TArray<FVector2D>& SourceUVs)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(DWWeapon_ApplyGeometry);
    if(!Component)return;
    if(Vertices.IsEmpty()||Triangles.IsEmpty()||Triangles.Num()%3!=0){Component->SetVisibility(false);return;}
    const FDWWeaponGeometryCache* Previous=GeometryCaches.Find(Component);
    const bool bSameTopology=Previous&&Previous->Vertices.Num()==Vertices.Num()&&Previous->Triangles==Triangles&&Component->GetProcMeshSection(0);
    // Idle/back-hover and repeated samples need no tangent work or render-thread upload.
    // Exact equality intentionally keeps every authored/sub-frame change, however small.
    if(bSameTopology&&Previous->Vertices==Vertices&&Previous->SourceUVs==SourceUVs)
    {++GeometryStats.UnchangedSkips;Component->SetVisibility(true);return;}
    for(int32 Index:Triangles)if(Index<0||Index>=Vertices.Num()){Component->SetVisibility(false);return;}
    for(const FVector& Vertex:Vertices)if(Vertex.ContainsNaN()){Component->SetVisibility(false);return;}
    TArray<FVector2D> UVs=SourceUVs;if(UVs.Num()!=Vertices.Num())UVs.Init(FVector2D::ZeroVector,Vertices.Num());
    TArray<FVector> Normals;TArray<FProcMeshTangent> Tangents;
    uint64 Comparisons=0;DWWeaponGeometry::CalculateTangents(Vertices,Triangles,UVs,Normals,Tangents,&Comparisons);
    ++GeometryStats.TangentBuilds;GeometryStats.OverlapComparisons+=Comparisons;
    if(!bSameTopology)
        Component->CreateMeshSection_LinearColor(0,Vertices,Triangles,Normals,UVs,TArray<FLinearColor>(),Tangents,false);
    else Component->UpdateMeshSection_LinearColor(0,Vertices,Normals,UVs,TArray<FLinearColor>(),Tangents);
    ++GeometryStats.MeshUploads;
    auto& Cache=GeometryCaches.FindOrAdd(Component);Cache.Vertices=Vertices;Cache.Triangles=Triangles;Cache.SourceUVs=SourceUVs;
    Component->SetVisibility(true);
}
void UDWWeaponAnimationComponent::ResetTrails()
{
    if(PrimaryTrailVisual)PrimaryTrailVisual->SetVisibility(false);
    if(SecondaryTrailVisual)SecondaryTrailVisual->SetVisibility(false);
    for(UNiagaraComponent* Component:{PrimaryTrailComponent.Get(),SecondaryTrailComponent.Get()})if(Component)Component->DeactivateImmediate();
    bPrimaryTrailEmitting=bSecondaryTrailEmitting=false;
}
void UDWWeaponAnimationComponent::UpdateParticleTrail(TObjectPtr<UNiagaraComponent>& Component,UNiagaraSystem* System,FName ComponentName,
    const FTransform& WeaponTransform,const FTransform& SourceWeaponTransform,const FVector& SourceRoot,const FVector& SourceTip,
    const FVector& Offset,const FRotator& Rotation,const FVector& Scale,bool bShouldEmit,bool& bWasEmitting)
{
    auto* P=Player();auto* Set=GetSelectedSet();if(!P||!Set)return;
    if(!System)
    {
        if(Component){Component->DeactivateImmediate();Component->SetAsset(nullptr);}
        bWasEmitting=false;return;
    }
    if(!Component)
    {
        if(!bShouldEmit)return;
        Component=NewObject<UNiagaraComponent>(P,ComponentName);P->AddInstanceComponent(Component);
        Component->SetupAttachment(P->GetRootComponent());Component->SetAutoActivate(false);Component->SetAutoDestroy(false);
        Component->SetAsset(System);Component->RegisterComponent();
    }
    if(Component->GetAsset()!=System){Component->DeactivateImmediate();Component->SetAsset(System);bWasEmitting=false;}
    // Source points already include authored stretch. Map through the weapon to apply grip and visual-size edits.
    const FVector Root=WeaponTransform.TransformPosition(SourceWeaponTransform.InverseTransformPosition(SourceRoot));
    const FVector Tip=WeaponTransform.TransformPosition(SourceWeaponTransform.InverseTransformPosition(SourceTip));
    FTransform Attachment=FTransform(Rotation,Offset,Scale)*WeaponTransform;
    if(Set->bAttachTrailAtAuthoredTip)Attachment.SetTranslation(Tip+WeaponTransform.TransformVector(Offset));
    Component->SetRelativeTransform(Attachment);
    if(!Set->TrailStartParameterName.IsNone())Component->SetVariableVec3(Set->TrailStartParameterName,P->GetActorTransform().TransformPosition(Root));
    if(!Set->TrailEndParameterName.IsNone())Component->SetVariableVec3(Set->TrailEndParameterName,P->GetActorTransform().TransformPosition(Tip));
    if(const auto* Feedback=P->CombatFeedback.Get();Feedback&&Feedback->bHighlightTrails&&Feedback->TrailHighlightMaterial&&!Feedback->NiagaraTrailMaterialParameter.IsNone())
        Component->SetVariableMaterial(Feedback->NiagaraTrailMaterialParameter,Feedback->TrailHighlightMaterial);
    if(bShouldEmit&&!bWasEmitting)Component->Activate(true);
    else if(!bShouldEmit&&bWasEmitting)Component->Deactivate();
    bWasEmitting=bShouldEmit;
}
void UDWWeaponAnimationComponent::UpdateVisuals()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(DWWeapon_UpdateVisuals);
    auto* P=Player();auto* Set=GetSelectedSet();
    if(!P)return;
    if(!bEnabled||!Set||!IsEnabled()||SelectedWeapon==EDWAnimatedWeapon::Unarmed)
    {
        for(USceneComponent* C:{static_cast<USceneComponent*>(PrimaryVisual.Get()),static_cast<USceneComponent*>(SecondaryVisual.Get()),static_cast<USceneComponent*>(PrimaryDeformedVisual.Get()),static_cast<USceneComponent*>(SecondaryDeformedVisual.Get()),static_cast<USceneComponent*>(PrimaryTrailVisual.Get()),static_cast<USceneComponent*>(SecondaryTrailVisual.Get())})if(C)C->SetVisibility(false);
        if(SelectedWeapon==EDWAnimatedWeapon::Unarmed&&P->SwordPlaceholder)P->SwordPlaceholder->SetVisibility(false);
        ResetTrails();return;
    }
    auto CreateStatic=[P](TObjectPtr<UStaticMeshComponent>& C,FName Name)
    {if(!C){C=NewObject<UStaticMeshComponent>(P,Name);P->AddInstanceComponent(C);C->SetupAttachment(P->GetRootComponent());C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);C->RegisterComponent();}};
    auto CreateProc=[P](TObjectPtr<UProceduralMeshComponent>& C,FName Name)
    {if(!C){C=NewObject<UProceduralMeshComponent>(P,Name);P->AddInstanceComponent(C);C->SetupAttachment(P->GetRootComponent());C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);C->RegisterComponent();}};
    CreateStatic(PrimaryVisual,TEXT("WeaponPrimaryProxy"));CreateStatic(SecondaryVisual,TEXT("WeaponSecondaryProxy"));
    CreateProc(PrimaryDeformedVisual,TEXT("WeaponPrimaryDeformation"));CreateProc(SecondaryDeformedVisual,TEXT("WeaponSecondaryDeformation"));
    const auto& Presentation=GetPresentationClip();
    const auto* Feedback=P->CombatFeedback.Get();
    const bool bUseGeometryTrail=Set->bUseGeometryTrail||(Feedback&&Feedback->bHighlightTrails&&!Presentation.GeometryFrames.IsEmpty());
    if(bUseGeometryTrail){CreateProc(PrimaryTrailVisual,TEXT("WeaponPrimaryTrail"));CreateProc(SecondaryTrailVisual,TEXT("WeaponSecondaryTrail"));}
    if(P->SwordPlaceholder)P->SwordPlaceholder->SetVisibility(false);
    const double SourceSeconds=GetPresentationSourceSeconds();
    const bool bPresentationActive=HasWeaponPresentation();
    const bool bHeld=bPresentationActive&&SourceSeconds>=Presentation.HeldStartSeconds&&SourceSeconds<Presentation.HeldEndSeconds;
    FTransform Primary=Set->PrimaryBack,Secondary=Set->SecondaryBack;
    bool bPrimaryVisible=!P->IsDead(),bSecondaryVisible=bPrimaryVisible;
    FDWWeaponVisualSample Sample;bool bHasSample=false;
    if(bPresentationActive&&Set->bUseBakedVisualSamples&&!Presentation.VisualSamples.IsEmpty())
    {
        const int32 Index=DWWeaponAnimation::SampleIndex(Presentation.VisualSamples,SourceSeconds);
        Sample=Presentation.VisualSamples[Index];
        if(Presentation.VisualSamples.IsValidIndex(Index+1))
        {
            const auto& B=Presentation.VisualSamples[Index+1];
            const float Alpha=static_cast<float>(FMath::Clamp((SourceSeconds-Sample.TimeSeconds)/DWWeaponAnimation::Positive(B.TimeSeconds-Sample.TimeSeconds),0.,1.));
            Sample.PrimaryTransform.Blend(Presentation.VisualSamples[Index].PrimaryTransform,B.PrimaryTransform,Alpha);
            Sample.SecondaryTransform.Blend(Presentation.VisualSamples[Index].SecondaryTransform,B.SecondaryTransform,Alpha);
            Sample.PrimaryTrailRoot=FMath::Lerp(Sample.PrimaryTrailRoot,B.PrimaryTrailRoot,Alpha);Sample.PrimaryTrailTip=FMath::Lerp(Sample.PrimaryTrailTip,B.PrimaryTrailTip,Alpha);
            Sample.SecondaryTrailRoot=FMath::Lerp(Sample.SecondaryTrailRoot,B.SecondaryTrailRoot,Alpha);Sample.SecondaryTrailTip=FMath::Lerp(Sample.SecondaryTrailTip,B.SecondaryTrailTip,Alpha);
        }
        Primary=bHeld?Set->PrimaryGrip*Sample.PrimaryTransform:Set->PrimaryBack;
        Secondary=bHeld?Set->SecondaryGrip*Sample.SecondaryTransform:Set->SecondaryBack;
        bPrimaryVisible=Sample.bPrimaryVisible;bSecondaryVisible=Sample.bSecondaryVisible;bHasSample=true;
        if(!bHeld){bPrimaryVisible=!P->IsDead();bSecondaryVisible=bPrimaryVisible;}
    }
    else if(bHeld&&P->GetMesh())
    {
        if(P->GetMesh()->DoesSocketExist(Set->PrimaryBone))Primary=Set->PrimaryGrip*P->GetMesh()->GetSocketTransform(Set->PrimaryBone)*P->GetActorTransform().Inverse();
        if(P->GetMesh()->DoesSocketExist(Set->SecondaryBone))Secondary=Set->SecondaryGrip*P->GetMesh()->GetSocketTransform(Set->SecondaryBone)*P->GetActorTransform().Inverse();
    }
    // The bow's secondary prop is a nocked arrow, never a back-hover accessory.
    // Preserve authored sample visibility during the attack; only idle/out-of-hand is suppressed here.
    if(SelectedWeapon==EDWAnimatedWeapon::MoldBow)bSecondaryVisible=bSecondaryVisible&&bHeld&&!P->IsDead()&&!(bFormalBowActive&&bBowShotCommitted);
    Primary.SetScale3D(Primary.GetScale3D()*FMath::Max(.01f,Set->VisualScale));Secondary.SetScale3D(Secondary.GetScale3D()*FMath::Max(.01f,Set->VisualScale));
    PrimaryVisual->SetStaticMesh(Set->PrimaryMesh);SecondaryVisual->SetStaticMesh(Set->SecondaryMesh);
    PrimaryVisual->SetRelativeTransform(Primary);SecondaryVisual->SetRelativeTransform(Secondary);
    PrimaryDeformedVisual->SetRelativeTransform(Primary);SecondaryDeformedVisual->SetRelativeTransform(Secondary);
    if(Set->WeaponMaterial)for(UMeshComponent* C:{static_cast<UMeshComponent*>(PrimaryVisual.Get()),static_cast<UMeshComponent*>(SecondaryVisual.Get()),static_cast<UMeshComponent*>(PrimaryDeformedVisual.Get()),static_cast<UMeshComponent*>(SecondaryDeformedVisual.Get())})C->SetMaterial(0,Set->WeaponMaterial);
    UMaterialInterface* TrailMaterial=Feedback&&Feedback->bHighlightTrails&&Feedback->TrailHighlightMaterial?Feedback->TrailHighlightMaterial.Get():Set->TrailMaterial.Get();
    if(bUseGeometryTrail&&TrailMaterial){PrimaryTrailVisual->SetMaterial(0,TrailMaterial);SecondaryTrailVisual->SetMaterial(0,TrailMaterial);}
    const FDWWeaponGeometryFrame* Frame=nullptr;const FDWWeaponGeometryFrame* NextFrame=nullptr;float Alpha=0.f;
    if(bPresentationActive&&!Presentation.GeometryFrames.IsEmpty())
    {
        const int32 Index=DWWeaponAnimation::SampleIndex(Presentation.GeometryFrames,SourceSeconds);Frame=&Presentation.GeometryFrames[Index];
        if(Presentation.GeometryFrames.IsValidIndex(Index+1)){NextFrame=&Presentation.GeometryFrames[Index+1];Alpha=static_cast<float>(FMath::Clamp((SourceSeconds-Frame->TimeSeconds)/DWWeaponAnimation::Positive(NextFrame->TimeSeconds-Frame->TimeSeconds),0.,1.));}
    }
    const TArray<FVector>& PV=Frame&&!Frame->PrimaryVertices.IsEmpty()?Frame->PrimaryVertices:Set->PrimaryGeometry.Vertices;
    const TArray<FVector>& SV=Frame&&!Frame->SecondaryVertices.IsEmpty()?Frame->SecondaryVertices:Set->SecondaryGeometry.Vertices;
    ApplyGeometry(PrimaryDeformedVisual,NextFrame?DWWeaponAnimation::InterpolateVertices(PV,NextFrame->PrimaryVertices,Alpha):PV,Set->PrimaryGeometry.Triangles,Set->PrimaryGeometry.UVs);
    ApplyGeometry(SecondaryDeformedVisual,NextFrame?DWWeaponAnimation::InterpolateVertices(SV,NextFrame->SecondaryVertices,Alpha):SV,Set->SecondaryGeometry.Triangles,Set->SecondaryGeometry.UVs);
    PrimaryDeformedVisual->SetVisibility(bPrimaryVisible&&!PV.IsEmpty()&&!Set->PrimaryGeometry.Triangles.IsEmpty());
    SecondaryDeformedVisual->SetVisibility(bSecondaryVisible&&!SV.IsEmpty()&&!Set->SecondaryGeometry.Triangles.IsEmpty());
    PrimaryVisual->SetVisibility(bPrimaryVisible&&Set->PrimaryMesh&&!PrimaryDeformedVisual->IsVisible());SecondaryVisual->SetVisibility(bSecondaryVisible&&Set->SecondaryMesh&&!SecondaryDeformedVisual->IsVisible());
    const bool bParticleWindow=bActive&&bHasSample&&Sample.bTrailActive&&!P->IsDead();
    UpdateParticleTrail(PrimaryTrailComponent,Set->PrimaryTrailSystem,TEXT("WeaponPrimaryTrailFX"),Primary,Sample.PrimaryTransform,Sample.PrimaryTrailRoot,Sample.PrimaryTrailTip,
        Set->PrimaryTrailOffset,Set->PrimaryTrailRotation,Set->PrimaryTrailScale,bParticleWindow&&bPrimaryVisible,bPrimaryTrailEmitting);
    UpdateParticleTrail(SecondaryTrailComponent,Set->SecondaryTrailSystem,TEXT("WeaponSecondaryTrailFX"),Secondary,Sample.SecondaryTransform,Sample.SecondaryTrailRoot,Sample.SecondaryTrailTip,
        Set->SecondaryTrailOffset,Set->SecondaryTrailRotation,Set->SecondaryTrailScale,bParticleWindow&&bSecondaryVisible,bSecondaryTrailEmitting);
    if(bUseGeometryTrail&&Frame&&(!bHasSample||Sample.bTrailActive))
    {
        // Trails are already actor-local, while weapons scale about their own translated origin.
        const float Scale=FMath::Max(.01f,Set->VisualScale);
        ApplyGeometry(PrimaryTrailVisual,DWWeaponAnimation::ScaleVerticesAboutPivot(Frame->TrailVertices,Primary.GetTranslation(),Scale),Frame->TrailTriangles,TArray<FVector2D>());
        ApplyGeometry(SecondaryTrailVisual,DWWeaponAnimation::ScaleVerticesAboutPivot(Frame->SecondaryTrailVertices,Secondary.GetTranslation(),Scale),Frame->SecondaryTrailTriangles,TArray<FVector2D>());
        PrimaryTrailVisual->SetRelativeTransform(FTransform::Identity);SecondaryTrailVisual->SetRelativeTransform(FTransform::Identity);
        if(SelectedWeapon==EDWAnimatedWeapon::MoldBow&&!bSecondaryVisible)SecondaryTrailVisual->SetVisibility(false);
    }
    else
    {
        if(PrimaryTrailVisual)PrimaryTrailVisual->SetVisibility(false);
        if(SecondaryTrailVisual)SecondaryTrailVisual->SetVisibility(false);
    }
}

bool UDWWeaponAnimationComponent::BindObservedInputPress(const FDWCombatInputPress& Press,bool bSecondary)
{
    if(!DWCombatInputOriginsMatch(Press,Press)||(bSecondary?!bSecondaryHeld:!bPrimaryHeld))return false;
    if(bSecondary)SecondaryInputOrigin=Press;else PrimaryInputOrigin=Press;
    return true;
}
bool UDWWeaponAnimationComponent::ApplyObservedInputRelease(const FDWCombatInputPress& Press)
{
    if(PrimaryInputOrigin.IsSet()&&DWCombatInputOriginsMatch(PrimaryInputOrigin.GetValue(),Press))
    {PrimaryInputOrigin.Reset();PrimaryReleased();return true;}
    if(SecondaryInputOrigin.IsSet()&&DWCombatInputOriginsMatch(SecondaryInputOrigin.GetValue(),Press))
    {SecondaryInputOrigin.Reset();SecondaryReleased();return true;}
    return false;
}
bool UDWWeaponAnimationComponent::CancelObservedInputAction(const FDWCombatInputPress& Press)
{
    if((PrimaryInputOrigin.IsSet()&&DWCombatInputOriginsMatch(PrimaryInputOrigin.GetValue(),Press))
        ||(SecondaryInputOrigin.IsSet()&&DWCombatInputOriginsMatch(SecondaryInputOrigin.GetValue(),Press)))
    {CancelAction();return true;}
    return false;
}
FString UDWWeaponAnimationComponent::GetObservedInputDebugState() const
{
    const auto Describe=[](const TOptional<FDWCombatInputPress>& Origin)
    {
        if(!Origin.IsSet())return FString(TEXT("None"));
        const auto& Press=Origin.GetValue();
        return FString::Printf(TEXT("%llu/%llu/%llu:%llu:%s:%s"),
            static_cast<unsigned long long>(Press.Edge.Context.WorldEpoch),static_cast<unsigned long long>(Press.Edge.Context.LifeEpoch),
            static_cast<unsigned long long>(Press.Edge.Context.BindingGeneration),static_cast<unsigned long long>(Press.Edge.Sequence),
            *Press.Edge.Key.ToString(),*Press.Action.ToString());
    };
    return FString::Printf(TEXT("PrimaryHeld=%d SecondaryHeld=%d PrimaryOrigin=%s SecondaryOrigin=%s"),
        bPrimaryHeld,bSecondaryHeld,*Describe(PrimaryInputOrigin),*Describe(SecondaryInputOrigin));
}
