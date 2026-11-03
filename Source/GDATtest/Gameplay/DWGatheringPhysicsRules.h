#pragma once
#include "CoreMinimal.h"
namespace DWGatheringPhysicsRules
{
inline bool IsGroundContact(bool OtherValid,bool OwnActor,bool Pawn,bool OtherSimulating,float NormalZ,float Threshold)
{
    return OtherValid&&!OwnActor&&!Pawn&&!OtherSimulating&&FMath::IsFinite(NormalZ)&&NormalZ>=FMath::Clamp(Threshold,.1f,1.f);
}
inline bool SettlementDeadlineReached(float ActiveSeconds,float ConfiguredSeconds)
{
    if(!FMath::IsFinite(ActiveSeconds)||ActiveSeconds<0.f)return false;
    const float Limit=FMath::IsFinite(ConfiguredSeconds)?FMath::Clamp(ConfiguredSeconds,.25f,60.f):6.f;
    return ActiveSeconds>=Limit;
}
inline float DissolveAfterContact(float TimeSinceContact,float Hold,float Duration)
{
    if(!FMath::IsFinite(TimeSinceContact)||TimeSinceContact<0.f)return 0.f;
    return FMath::Clamp((TimeSinceContact-FMath::Max(0.f,Hold))/FMath::Max(.05f,Duration),0.f,1.f);
}
}
