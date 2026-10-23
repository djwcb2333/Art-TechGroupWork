#include "DWPlayerCharacter.h"
#include "DWCombatCoreInjectionTransaction.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

namespace DWCoreInjection
{
EDWCombatBaseForm FormForItem(FName Item)
{
    switch(FDWCombatItemRegistry::GetItemRole(Item))
    {
    case EDWCombatItemRole::MoldCore:return EDWCombatBaseForm::Mold;
    case EDWCombatItemRole::YeastCore:return EDWCombatBaseForm::Yeast;
    default:return EDWCombatBaseForm::Normal;
    }
}
bool SameLife(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B)
{return A.Entity&&A.Entity==B.Entity&&A.Life==B.Life;}
}

bool ADWPlayerCharacter::IsCoreInjecting() const {return CoreInjection.Plan.IsPending();}
float ADWPlayerCharacter::GetCoreInjectionRemainingSeconds() const
{return IsCoreInjecting()?static_cast<float>(CoreInjection.Plan.GetRemainingSeconds()):0.f;}
bool ADWPlayerCharacter::CanRequestCoreInjection(const FDWInventorySlotSnapshot& Slot,bool bAllowPausedMenu) const
{
    const auto* World=GetWorld();const auto* GI=GetGameInstance<UDWGameInstance>();
    if(!World||World->bIsTearingDown||IsActorBeingDestroyed()||IsDead()||bRestoringSave||
        !HasCombatVitalsReady()||GetCombatBaseForm()!=EDWCombatBaseForm::Normal||!Inventory||
        !Inventory->IsSlotSnapshotCurrent(Slot)||!GI||!GI->GetCombatRules()||
        DWCoreInjection::FormForItem(Slot.ItemId)==EDWCombatBaseForm::Normal)return false;
    if(!bAllowPausedMenu&&!IsCombatActionAllowed())return false;
    FDWCoreInjectionConfig Config;FString Error;
    return FDWCoreInjectionConfig::Build(*GI->GetCombatRules(),Config,Error)&&Slot.Quantity>=Config.CoreCost;
}
bool ADWPlayerCharacter::RequestCoreInjection(const FDWInventorySlotSnapshot& Slot)
{
    if(bGameplayActionInProgress||!CanRequestCoreInjection(Slot))return false;
    TGuardValue<bool> ActionGuard(bGameplayActionInProgress,true);
    auto* A=UDWCombatAuthoritySubsystem::Get(this);if(!A)return false;
    const auto Life=A->CaptureActorLife(this);const uint64 Epoch=A->GetWorldEpoch();
    const uint64 DamageRevision=CombatDamageRevision;
    const TWeakObjectPtr<ADWPlayerCharacter> Self(this);
    const TWeakObjectPtr<UDWInventoryComponent> OriginalInventory(Inventory);
    const auto Current=[&]()
    {
        if(!Self.IsValid()||Self->IsActorBeingDestroyed()||Self->Inventory.Get()!=OriginalInventory.Get())return false;
        auto* CurrentAuthority=UDWCombatAuthoritySubsystem::Get(Self.Get());
        return CurrentAuthority&&CurrentAuthority->GetWorldEpoch()==Epoch&&
            DWCoreInjection::SameLife(CurrentAuthority->CaptureActorLife(Self.Get()),Life)&&
            Self->CombatDamageRevision==DamageRevision&&Self->CanRequestCoreInjection(Slot);
    };
    FDWCoreInjectionConfig Config;FString Error;
    if(!FDWCoreInjectionConfig::Build(*GetGameInstance<UDWGameInstance>()->GetCombatRules(),Config,Error))return false;
    // Qualification precedes cancellation; invalid stock/form never destroys a valid action.
    CancelCombatActions();if(!Current())return false;
    SetHarvestHeld(false);if(!Current())return false;bSprinting=false;
    if(GetCharacterMovement())GetCharacterMovement()->StopMovementImmediately();
    if(!Current())return false;
    ConsumeMovementInputVector();
    A=UDWCombatAuthoritySubsystem::Get(this);
    if(!A||!Current())return false;
    FDWCoreInjectionPlan Plan=CoreInjection.Plan;
    if(!Plan.Start(Config.DurationSeconds))return false;
    const auto Action=A->BeginAction(this);if(!Action.Id)return false;
    CoreInjection.Plan=MoveTemp(Plan);CoreInjection.Config=MoveTemp(Config);CoreInjection.Slot=Slot;
    CoreInjection.Inventory=Inventory;CoreInjection.Action=Action;CoreInjection.Life=Life;
    CoreInjection.WorldEpoch=Epoch;CoreInjection.DamageRevision=DamageRevision;
    CoreInjection.StationaryLocation=GetActorLocation();CoreInjection.TargetForm=DWCoreInjection::FormForItem(Slot.ItemId);
    bCoreInjectionCompletionDue=false;
    return true;
}
void ADWPlayerCharacter::CancelCoreInjectionFacts()
{
    CoreInjection.Plan.Cancel();bCoreInjectionCompletionDue=false;
    if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(CoreInjection.Action);
    CoreInjection.Action={};
}
bool ADWPlayerCharacter::IsCoreInjectionCurrent() const
{
    const auto& S=CoreInjection;auto* A=UDWCombatAuthoritySubsystem::Get(this);
    return S.Plan.IsPending()&&S.Action.Id&&S.Config.IsValid()&&A&&A->GetWorldEpoch()==S.WorldEpoch&&
        DWCoreInjection::SameLife(A->CaptureActorLife(const_cast<ADWPlayerCharacter*>(this)),S.Life)&&
        !IsDead()&&!bRestoringSave&&!IsActorBeingDestroyed()&&GetCombatBaseForm()==EDWCombatBaseForm::Normal&&
        CombatDamageRevision==S.DamageRevision&&Inventory&&S.Inventory.Get()==Inventory.Get()&&
        Inventory->IsSlotSnapshotCurrent(S.Slot)&&S.Slot.Quantity>=S.Config.CoreCost&&
        GetActorLocation().Equals(S.StationaryLocation,UE_SMALL_NUMBER);
}
bool ADWPlayerCharacter::CompleteCoreInjectionAtBoundary()
{
    if(!CoreInjection.Plan.IsReady()||!IsCoreInjectionCurrent())return false;
    const uint64 Serial=CoreInjection.Plan.GetSerial();
    bCoreInjectionCompletionDue=true;
    auto* A=UDWCombatAuthoritySubsystem::Get(this);
    const bool bCommitted=A&&A->CommitCoreInjection(this);
    // Publication can restore/destroy/start a different session. Never cancel that replacement.
    if(!bCommitted&&CoreInjection.Plan.GetSerial()==Serial)CancelCoreInjectionFacts();
    return bCommitted;
}
UAnimationAsset* ADWPlayerCharacter::GetCoreInjectionAnimationForPlayback() const
{
    const auto* Sequence=Cast<UAnimSequenceBase>(CoreInjectionAnimation);
    if(!Sequence||Sequence->IsA<UAnimMontage>()||!IsCoreInjecting()||
        !FMath::IsNearlyEqual(static_cast<double>(Sequence->GetPlayLength()),CoreInjection.Config.DurationSeconds,1.e-6)||
        !FMath::IsNearlyEqual(Sequence->RateScale,1.f,1.e-6f))return nullptr;
    return CoreInjectionAnimation;
}
