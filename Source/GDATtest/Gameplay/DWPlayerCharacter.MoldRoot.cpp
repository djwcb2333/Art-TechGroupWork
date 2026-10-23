#include "DWCombatMineSubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWGameInstance.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatFeedbackComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include <limits>

bool ADWPlayerCharacter::PrepareFormalRootEntry(FDWMoldRootConfig& Out) const
{
    if(!HasCombatVitalsReady()||!IsCombatActionAllowed()||IsFormallyRooted()||
        GetCombatBaseForm()!=EDWCombatBaseForm::Mold)return false;
    const auto* GI=GetGameInstance<UDWGameInstance>();FString Error;
    auto* Movement=GetCharacterMovement();
    if(!GI||!GI->GetCombatRules()||!Movement||!GetCapsuleComponent()||!Movement->IsMovingOnGround()||
        !FDWMoldRootConfig::Build(*GI->GetCombatRules(),Out,Error))return false;
    FFindFloorResult Floor;Movement->FindFloor(GetActorLocation(),Floor,false);
    return FormalRoot.CanStart(Out,GetCombatBaseForm(),GetCombatSpecial(),Floor.IsWalkableFloor(),GetActorLocation())==EDWMoldRootFailure::None;
}
bool ADWPlayerCharacter::TryToggleFormalRoot()
{
    if(!IsCombatActionAllowed()||bGameplayActionInProgress||!HasCombatVitalsReady())return false;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return false;
    const auto Life=A->CaptureActorLife(this);const uint64 Epoch=A->GetWorldEpoch();
    const TWeakObjectPtr<UWorld> World(GetWorld());
    const auto Current=[&]()
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
        if(!Authority||IsActorBeingDestroyed()||!World.IsValid()||GetWorld()!=World.Get()||World->bIsTearingDown||
            Authority->GetWorldEpoch()!=Epoch||!IsCombatActionAllowed())return false;
        const auto L=Authority->CaptureActorLife(this);return L.Entity==Life.Entity&&L.Life==Life.Life;
    };
    if(!Life.Entity)return false;
    if(IsFormallyRooted())
    {
        TGuardValue<bool> Guard(bGameplayActionInProgress,true);
        EndFormalRootFacts(EDWMoldRootEnd::Manual);
        CancelCombatActions();if(!Current())return true;
        PublishFormalRootMovementEffects();return true;
    }
    FDWMoldRootConfig Config;if(!PrepareFormalRootEntry(Config))return false; // Invalid E never cancels an action.
    TGuardValue<bool> Guard(bGameplayActionInProgress,true);
    const uint64 Revision=CombatVitals.GetMutationRevision(),DamageRevision=CombatDamageRevision;
    CancelCombatActions();if(!Current())return false;
    if(CombatVitals.GetMutationRevision()!=Revision||CombatDamageRevision!=DamageRevision||
        (WeaponAnimationComponent&&WeaponAnimationComponent->IsBusy()))return false;
    TGuardValue<bool> Due(bRootEntryDue,true);
    return A->CommitFormalRootEntry(this);
}
bool ADWPlayerCharacter::GetFormalRootIdentity(FDWMoldRootIdentity& Out) const
{
    if(!FormalRoot.IsActive()||IsDead()||IsActorBeingDestroyed()||bRestoringSave||!GetWorld()||GetWorld()->bIsTearingDown)return false;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A||A->GetWorldEpoch()!=FormalRootIdentity.WorldEpoch)return false;
    const auto Life=A->CaptureActorLife(const_cast<ADWPlayerCharacter*>(this));
    if(!FDWMoldRootPlan::ValidateBinding(FormalRoot,FormalRootIdentity,A->GetWorldEpoch(),Life))return false;
    Out=FormalRootIdentity;return true;
}
bool ADWPlayerCharacter::IsFormallyRootedAtFrameOffset(double Seconds) const
{
    if(!bHasCombatFormFrameSlice||!FMath::IsFinite(Seconds)||Seconds<0.)return IsFormallyRooted();
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return IsFormallyRooted();
    const auto Life=A->CaptureActorLife(const_cast<ADWPlayerCharacter*>(this));
    if(Life.Entity!=CombatFrameLife.Entity||Life.Life!=CombatFrameLife.Life)return IsFormallyRooted();
    return bCombatFrameStartRooted&&Seconds<CombatFrameRootExitOffset&&GetCombatBaseFormAtFrameOffset(Seconds)==EDWCombatBaseForm::Mold;
}
void ADWPlayerCharacter::EndFormalRootFacts(EDWMoldRootEnd Reason)
{
    if(!FormalRoot.IsActive())return;
    const uint64 Serial=FormalRoot.GetSerial();
    FormalRoot.End(Reason);bRootEntryDue=false;bRootMovementPending=true;
    // D11 native hook EndRootFacts(this, Serial) retires root emitters before EndAction. No Destroy/spawn/Blueprint here.
    if(auto* Mines=UDWCombatMineSubsystem::Get(this))Mines->EndRootFacts(this,Serial);
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(FormalRootIdentity.Action);
    FormalRootIdentity={};
    SyncCombatVitalsCompatibility();
    // Ordinary attacks/reload and released non-root emitters are deliberately preserved.
}
bool ADWPlayerCharacter::PublishFormalRootMovementEffects()
{
    if(!bRootMovementPending)return true;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);auto* Movement=GetCharacterMovement();
    if(!A||!Movement||IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)return false;
    const auto Life=A->CaptureActorLife(this);const uint64 Epoch=A->GetWorldEpoch(),Serial=FormalRoot.GetSerial();
    const bool Rooted=IsFormallyRooted();const TWeakObjectPtr<UWorld> World(GetWorld());
    bRootMovementPending=false;
    const auto Current=[&]()
    {
        auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
        if(!Authority||IsActorBeingDestroyed()||!World.IsValid()||GetWorld()!=World.Get()||World->bIsTearingDown||
            Authority->GetWorldEpoch()!=Epoch||FormalRoot.GetSerial()!=Serial||IsFormallyRooted()!=Rooted)return false;
        const auto L=Authority->CaptureActorLife(this);return L.Entity==Life.Entity&&L.Life==Life.Life;
    };
    if(Rooted)
    {
        ConsumeMovementInputVector();Movement->Velocity=FVector::ZeroVector;
        Movement->ClearAccumulatedForces();Movement->CurrentRootMotion.Clear();
        Movement->SetMovementMode(MOVE_None);if(!Current())return false;
        PublishCombatKnockbackEffects();if(!Current())return false;
    }
    else if(!IsDead()&&!bRestoringSave&&Movement->MovementMode==MOVE_None)
    {Movement->SetMovementMode(MOVE_Walking);if(!Current())return false;}
    if(auto* Mines=UDWCombatMineSubsystem::Get(this))Mines->FlushRootCleanup(this,0);
    if(!Current())return false;
    // Pure presentation, only after the real stance/movement facts are committed and rechecked.
    // Restore applies movement separately and bypasses this entry/exit notification.
    if(CombatFeedback)CombatFeedback->PublishMoldRootFeedback(Rooted);
    return Current();
}
