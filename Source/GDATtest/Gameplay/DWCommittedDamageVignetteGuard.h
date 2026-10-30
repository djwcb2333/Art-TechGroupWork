#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Presentation-only gate. Identity is supplied by the widget, not inferred from HP.
// A blocked serial cannot replay after a menu or during the mandatory clear interval.
struct FDWCommittedDamageVignetteGuard
{
    bool bBlocked=true;
    std::uint64_t BlockedSerial=0;
    double BurstStarted=-1.;
    double BlockedUntil=0.;

    void Block(std::uint64_t Serial,double Now,double Cooldown=0.)
    {
        bBlocked=true;BlockedSerial=Serial;BurstStarted=-1.;
        BlockedUntil=std::isfinite(Now)&&std::isfinite(Cooldown)?Now+std::max(0.,Cooldown):0.;
    }
    // Called only on a change of player/component identity. The first valid
    // binding may adopt a real active pulse; all later bindings fail closed.
    void Bind(std::uint64_t Serial,double Now,bool FirstValidBinding,bool Allowed,float Intensity)
    {
        Block(Serial,Now);
        if(FirstValidBinding&&Allowed&&Serial!=0&&std::isfinite(Now)&&std::isfinite(Intensity)&&Intensity>0.f)
            bBlocked=false;
    }
    float Observe(bool Allowed,std::uint64_t Serial,float Intensity,double Now,double MaxBurst,double ClearInterval)
    {
        if(!std::isfinite(Now)||!std::isfinite(Intensity)) { Block(Serial,0.);return 0.f; }
        if(!Allowed) { Block(Serial,Now);return 0.f; }
        if(bBlocked)
        {
            if(Now<BlockedUntil) { BlockedSerial=Serial;return 0.f; }
            if(Serial==BlockedSerial)return 0.f;
            bBlocked=false;BurstStarted=-1.;
        }
        const float Value=std::clamp(Intensity,0.f,1.f);
        if(Value<=0.f) { BurstStarted=-1.;return 0.f; }
        if(BurstStarted<0.||Now<BurstStarted) BurstStarted=Now;
        const double Limit=std::isfinite(MaxBurst)?std::clamp(MaxBurst,.2,3.):.85;
        if(Now-BurstStarted>=Limit)
        {
            const double Clear=std::isfinite(ClearInterval)?std::clamp(ClearInterval,.05,1.):.15;
            Block(Serial,Now,Clear);return 0.f;
        }
        return Value;
    }
};
