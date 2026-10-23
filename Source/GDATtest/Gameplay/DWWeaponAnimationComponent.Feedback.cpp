#include "DWWeaponAnimationComponent.h"
#include "DWPlayerCharacter.h"
#include "DWCombatFeedbackComponent.h"

void UDWWeaponAnimationComponent::PublishFormalSwordChargeReadyEffects()
{
    const auto Action=SwordAction;
    if(!IsCurrentFormalSword(Action)||!SwordPlan.IsChargeReady())return;
    if(auto* P=Player();P&&P->CombatFeedback)P->CombatFeedback->PlayFeedback(EDWCombatFeedbackCue::ChargeReady);
}
