#pragma once
#include "CoreMinimal.h"
namespace DWPickupPresentationMath
{
    inline float Safe(float V,float Fallback){return FMath::IsFinite(V)?V:Fallback;}
    inline float Ease(float V){V=FMath::Clamp(Safe(V,0.f),0.f,1.f);return V*V*(3.f-2.f*V);}
    struct FSample{float XY=1.f,Z=1.f,Lift=0.f,Flash=0.f,Opacity=1.f;};
    inline FSample Evaluate(float Progress,float Stretch=1.8f,float Squash=.16f,float Rise=35.f)
    {
        const float T=FMath::Clamp(Safe(Progress,0.f),0.f,1.f);
        Stretch=FMath::Clamp(Safe(Stretch,1.8f),1.f,3.f);Squash=FMath::Clamp(Safe(Squash,.16f),0.f,.45f);
        FSample S;
        if(T<.16f)S.Z=FMath::Lerp(1.f,1.f-Squash,Ease(T/.16f));
        else if(T<.50f)S.Z=FMath::Lerp(1.f-Squash,Stretch,Ease((T-.16f)/.34f));
        else S.Z=FMath::Lerp(Stretch,1.f+(Stretch-1.f)*.65f,Ease((T-.50f)/.22f));
        S.XY=1.f/FMath::Sqrt(FMath::Max(.1f,S.Z));
        const float Vanish=1.f-Ease((T-.72f)/.28f);S.XY*=Vanish;S.Z*=Vanish;
        S.Lift=FMath::Max(0.f,Safe(Rise,35.f))*Ease((T-.16f)/.84f);
        S.Flash=Ease(T/.38f)*Vanish;S.Opacity=Vanish;return S;
    }
    inline float NormalizeScale(float WorldXYAtUnitScale,float TargetWidth)
    {
        if(!FMath::IsFinite(WorldXYAtUnitScale)||WorldXYAtUnitScale<=KINDA_SMALL_NUMBER)return 1.f;
        return FMath::Clamp(Safe(TargetWidth,30.f),1.f,200.f)/WorldXYAtUnitScale;
    }
}
