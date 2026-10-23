#include "DWPlayerCharacter.h"
#include "DWGatheringPond.h"
#include "DWGatheringProfile.h"
#include "Engine/World.h"
#include <cmath>

bool ADWPlayerCharacter::TryReceiveGatheringWater(ADWGatheringPond& Source,double RequestedAmount,double& OutAdded)
{
    OutAdded=0.;
    if(!HasAuthority()||!IsCombatActionAllowed()||!Cast<UDWGatheringProfile>(GetGameplayConfig())||
        bGameplayActionInProgress||bAdvancingGameplayClocks||bApplyingCombatDamage||
        !IsValid(&Source)||Source.GetWorld()!=GetWorld()||!Source.HasAuthority()||
        !Source.IsWetAndOperational()||!Source.ContainsActor(this)||
        !std::isfinite(RequestedAmount)||RequestedAmount<=0.)return false;
    const double Offered=FMath::Min(RequestedAmount,Source.GetRemainingWater());
    auto Candidate=CombatVitals;
    double Added=0.;
    if(!Candidate.AddWater(Offered,Health,Added)||Added<=0.)return false;
    // No user callback, Blueprint event, collision update or notification occurs between these two facts.
    // The source revalidates remaining reserve before changing it; rejected grants preserve both owners.
    if(!Source.DebitRefillWater(Added))return false;
    CombatVitals=MoveTemp(Candidate);
    OutAdded=Added;
    // Existing HUD reads GetCombatWater(). No UI event/layout or unrelated form notification is needed.
    return true;
}

void ADWPlayerCharacter::SetGatheringEnvironmentSlow(const AActor* Source,double Fraction)
{
    if(!IsValid(Source)||Source->GetWorld()!=GetWorld()||!std::isfinite(Fraction)||Fraction<0.||Fraction>1.)return;
    const auto* Pond=Cast<ADWGatheringPond>(Source);
    if(!Pond||!Pond->IsWetAndOperational()||!Pond->ContainsActor(this)||
        !Cast<UDWGatheringProfile>(GetGameplayConfig())||IsDead())return;
    const TWeakObjectPtr<AActor> Key(const_cast<AActor*>(Source));
    for(auto It=GatheringEnvironmentSlowSources.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    if(const double* Existing=GatheringEnvironmentSlowSources.Find(Key);Existing&&*Existing==Fraction)return;
    GatheringEnvironmentSlowSources.Add(Key,Fraction);
    RefreshCombatMovementSpeed();
}

void ADWPlayerCharacter::RemoveGatheringEnvironmentSlow(const AActor* Source)
{
    if(!Source)return;
    if(GatheringEnvironmentSlowSources.Remove(TWeakObjectPtr<AActor>(const_cast<AActor*>(Source)))>0&&
        IsValid(this)&&!IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown)
        RefreshCombatMovementSpeed();
}

double ADWPlayerCharacter::GetGatheringEnvironmentMovementMultiplier() const
{
    if(!Cast<UDWGatheringProfile>(GetGameplayConfig()))return 1.;
    double Strongest=0.;
    for(const auto& Entry:GatheringEnvironmentSlowSources)
    {
        const auto* Pond=Cast<ADWGatheringPond>(Entry.Key.Get());
        if(Pond&&Pond->IsWetAndOperational()&&Pond->ContainsActor(this)&&
            std::isfinite(Entry.Value)&&Entry.Value>=0.&&Entry.Value<=1.)
            Strongest=FMath::Max(Strongest,Entry.Value);
    }
    return 1.-Strongest;
}
