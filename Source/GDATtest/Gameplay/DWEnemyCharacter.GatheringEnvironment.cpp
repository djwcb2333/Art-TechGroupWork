#include "DWEnemyCharacter.h"
#include "DWGatheringPond.h"
#include "Engine/World.h"
#include <cmath>

void ADWEnemyCharacter::SetGatheringEnvironmentSlow(const AActor* Source,double Fraction)
{
    if(!IsValid(Source)||Source->GetWorld()!=GetWorld()||!IsAlive()||
        !std::isfinite(Fraction)||Fraction<0.||Fraction>1.)return;
    const auto* Pond=Cast<ADWGatheringPond>(Source);
    if(!Pond||!Pond->IsWetAndOperational()||!Pond->ContainsActor(this))return;
    const TWeakObjectPtr<AActor> Key(const_cast<AActor*>(Source));
    for(auto It=GatheringEnvironmentSlowSources.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    if(const double* Existing=GatheringEnvironmentSlowSources.Find(Key);Existing&&*Existing==Fraction)return;
    GatheringEnvironmentSlowSources.Add(Key,Fraction);
    RefreshCombatGroundAreaSlow();
}

void ADWEnemyCharacter::RemoveGatheringEnvironmentSlow(const AActor* Source)
{
    if(!Source)return;
    if(GatheringEnvironmentSlowSources.Remove(TWeakObjectPtr<AActor>(const_cast<AActor*>(Source)))>0&&
        IsValid(this)&&!IsActorBeingDestroyed()&&GetWorld()&&!GetWorld()->bIsTearingDown&&IsAlive())
        RefreshCombatGroundAreaSlow();
}

double ADWEnemyCharacter::GetGatheringEnvironmentMovementMultiplier() const
{
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
