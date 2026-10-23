#include "DWEnemyCharacter.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

namespace DWDaggerEnemy
{
bool Equipped(const ADWPlayerCharacter* P)
{return IsValid(P)&&!P->IsActorBeingDestroyed()&&!P->IsDead()&&P->HasCombatVitalsReady()&&P->WeaponAnimationComponent&&
    P->WeaponAnimationComponent->IsEnabled()&&P->WeaponAnimationComponent->GetCurrentEquipmentId()==TEXT("DWWeapon_DualDaggers");}
}
double ADWEnemyCharacter::GetDaggerAlertRadius()const
{return IsFormalCombatReady()?FormalCombatConfig.AlertRadiusCm:(FMath::IsFinite(DetectionRadius)?FMath::Max(0.f,DetectionRadius):0.);}
bool ADWEnemyCharacter::IsEngagedForDagger()const
{return IsFormalCombatReady()?FormalMemory.bEngaged:(PendingDaggerAlertTarget.IsValid()||TargetPawn.IsValid()||State==EDWEnemyState::Attack||State==EDWEnemyState::Alert);}
void ADWEnemyCharacter::SyncDaggerAwareness(ADWPlayerCharacter* P,bool EquipmentChange)
{
    if(!IsAlive()||!GetWorld()||GetWorld()->bIsTearingDown||GetWorld()->IsPaused()||!IsValid(P)||P->IsActorBeingDestroyed())return;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return;const auto Life=A->CaptureActorLife(P);
    const bool Equipped=DWDaggerEnemy::Equipped(P),Inside=FVector::Dist2D(GetActorLocation(),P->GetActorLocation())<=GetDaggerAlertRadius();
    if(EquipmentChange||Life.Entity!=DaggerPlayerLife.Entity||Life.Life!=DaggerPlayerLife.Life)
    {DaggerAwareness.EquipmentChanged(Inside,Equipped);DaggerPlayerLife=Life;}
    const auto* W=P->WeaponAnimationComponent.Get();
    const double Grace=W?W->GetDaggerGraceSeconds():0.;
    DaggerAwareness.Observe(GetWorld()->GetTimeSeconds(),Inside,Equipped,IsEngagedForDagger(),Grace);
}
bool ADWEnemyCharacter::HasDaggerDiscoveryGrace(const ADWPlayerCharacter* P)const
{
    return IsAlive()&&GetWorld()&&DWDaggerEnemy::Equipped(P)&&
        DaggerAwareness.IsProtected(GetWorld()->GetTimeSeconds(),true,IsEngagedForDagger());
}
bool ADWEnemyCharacter::IsEligibleForDaggerBackstab(ADWPlayerCharacter* P,double BackArc)
{
    SyncDaggerAwareness(P);
    if(!IsAlive()||!DWDaggerEnemy::Equipped(P)||IsEngagedForDagger()||!FMath::IsFinite(BackArc)||BackArc<=0.||BackArc>360.)return false;
    // Awareness is the AI's actual target memory. No global stealth timer or rear-position reset.
    if(TargetPawn.IsValid()&&!HasDaggerDiscoveryGrace(P))return false;
    const FVector ToPlayer=(P->GetActorLocation()-GetActorLocation()).GetSafeNormal2D();
    return !ToPlayer.IsNearlyZero()&&FVector::DotProduct(-GetActorForwardVector().GetSafeNormal2D(),ToPlayer)>=FMath::Cos(FMath::DegreesToRadians(BackArc*.5));
}
void ADWEnemyCharacter::RecordDaggerDamageFacts(AActor* Source,bool SourceWasPlayer)
{
    DaggerAwareness.BreakProtection();
    if(CombatProfile==EDWEnemyCombatProfile::Legacy&&SourceWasPlayer&&!TargetPawn.IsValid()&&!PendingDaggerAlertTarget.IsValid())
        if(auto* P=Cast<ADWPlayerCharacter>(Source))PendingDaggerAlertTarget=P;
}
void ADWEnemyCharacter::PublishDaggerDamageAlertEffects()
{
    const auto Pending=PendingDaggerAlertTarget;PendingDaggerAlertTarget.Reset();
    // Damage facts block another backstab immediately; only the original alert path owns AI effects and duration.
    if(CombatProfile==EDWEnemyCombatProfile::Legacy&&!TargetPawn.IsValid()&&IsAlive()&&Pending.IsValid())BeginAlert(Pending.Get());
}
void ADWEnemyCharacter::RefreshDaggerGraceFacts(ADWPlayerCharacter* P,double Now,double Grace)
{
    if(!IsAlive()||IsEngagedForDagger()||!DWDaggerEnemy::Equipped(P))return;
    // Synchronize circle position first; refresh is a reset, never an additive duration.
    SyncDaggerAwareness(P);DaggerAwareness.Refresh(Now,Grace,true,false);
}
void ADWEnemyCharacter::DrawDaggerAlertRing(ADWPlayerCharacter* P)const
{
    if(!IsAlive()||!DWDaggerEnemy::Equipped(P)||!GetWorld()||GetWorld()->IsNetMode(NM_DedicatedServer))return;
    const FVector Center=GetActorLocation()-FVector(0,0,GetCapsuleComponent()?GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-2.:0.);
    const FColor Color=HasDaggerDiscoveryGrace(P)?FColor::Green:IsEngagedForDagger()?FColor::Red:FColor::Yellow;
    DrawDebugCircle(GetWorld(),Center,static_cast<float>(GetDaggerAlertRadius()),48,Color,false,-1.f,0,1.5f,FVector::ForwardVector,FVector::RightVector,false);
}
FDWDaggerAwarenessSave ADWEnemyCharacter::CaptureDaggerAwarenessSave()const
{return DaggerAwareness.Capture(GetWorld()?GetWorld()->GetTimeSeconds():0.);}
bool ADWEnemyCharacter::RestoreDaggerAwarenessSave(const FDWDaggerAwarenessSave& Saved,ADWPlayerCharacter* P)
{
    auto* A=UDWCombatAuthoritySubsystem::Get(this);
    if(!A||!IsValid(P)||P->GetWorld()!=GetWorld()||!GetWorld()||GetWorld()->bIsTearingDown||!FDWDaggerAwareness::Validate(Saved))return false;
    if((Saved.bInitialized&&Saved.bEquipped!=DWDaggerEnemy::Equipped(P))||((!IsAlive()||IsEngagedForDagger())&&Saved.GraceRemainingSeconds>0.))return false;
    auto Candidate=DaggerAwareness;if(!Candidate.Restore(Saved,GetWorld()->GetTimeSeconds()))return false;
    DaggerAwareness=Candidate;DaggerPlayerLife=A->CaptureActorLife(P);return true;
}
