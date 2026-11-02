#include "DWPlayerCharacter.h"
#include "DWGatheringPond.h"
#include "DWCharacterA01Component.h"
#include "DWGatheringProfile.h"
#include "Engine/World.h"
#include <cmath>

bool ADWPlayerCharacter::TryReceiveGatheringWater(ADWGatheringPond& Source,double RequestedAmount,double& OutAdded)
{
    OutAdded=0.;
    if(!HasAuthority()||!IsCombatActionAllowed()||!UDWGameplayConfig::AsGatheringConfig(GetGameplayConfig())||
        bGameplayActionInProgress||bAdvancingGameplayClocks||bApplyingCombatDamage||
        !IsValid(&Source)||Source.GetWorld()!=GetWorld()||!Source.HasAuthority()||
        !Source.IsWetAndOperational()||!Source.ContainsActor(this)||
        !std::isfinite(RequestedAmount)||RequestedAmount<=0.)return false;
    const double Offered=FMath::Min(RequestedAmount,Source.GetRemainingWater());
    if(CharacterA01&&CharacterA01->IsEnabled())
    {
        auto* A01=CharacterA01.Get();
        const uint64 Revision=A01->GetResourceRevision();
        const double ExpectedHealth=Health;
        if(Revision==MAX_uint64)return false;
        // Prepare on a copy of the same authoritative model read by HUD and saves.
        auto Candidate=A01->GetRuntime();
        Candidate.SyncCurrentHealth(ExpectedHealth);
        const double Added=Candidate.AddWater(Offered);
        if(!std::isfinite(Added)||Added<=0.||!A01->CanAdoptRuntime(Revision,ExpectedHealth))return false;
        // No callback between the finite source debit and authoritative player commit.
        // Rejection changes neither owner; full players do not drain the pond.
        if(!Source.DebitRefillWater(Added))return false;
        OutAdded=Added;
        A01->AdoptRuntimeUnchecked(MoveTemp(Candidate));
        return true;
    }
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
        !UDWGameplayConfig::AsGatheringConfig(GetGameplayConfig())||IsDead())return;
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
    if(!UDWGameplayConfig::AsGatheringConfig(GetGameplayConfig()))return 1.;
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
