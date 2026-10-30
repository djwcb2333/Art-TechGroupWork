#pragma once

#include <algorithm>
#include <cmath>

// Plain source-clock mapping. This never calls combat events or animation notifies.
namespace DWGatheringActionTiming
{
struct FProfile
{
    double Windup = .40;
    double Downstroke = .15;
    double Hold = .09;
    double SourceStart = .75;
    double SourceRaised = 1.60;
    double SourceContact = 1.80;
    double SourceEnd = 1.95;

    double ContactProgress() const
    {
        return Windup + Downstroke * std::clamp((SourceContact-SourceRaised) /
            std::max(.001, SourceEnd-SourceRaised), 0., 1.);
    }
    double SourceSeconds(double Progress) const
    {
        const double P = std::clamp(Progress, 0., 1.);
        if(P < Windup) return SourceStart + (SourceRaised-SourceStart)*P/Windup;
        if(P < Windup+Downstroke)
            return SourceRaised+(SourceEnd-SourceRaised)*(P-Windup)/Downstroke;
        if(P < Windup+Downstroke+Hold) return SourceEnd;
        const double A = std::clamp((P-Windup-Downstroke-Hold) /
            std::max(.001, 1.-Windup-Downstroke-Hold), 0., 1.);
        // Reuse genuine two-hand skeleton poses for recovery, excluding the source's side sweep.
        const double Smooth = A*A*(3.-2.*A);
        return SourceEnd+(SourceStart-SourceEnd)*Smooth;
    }
};

// Reject impossible rates instead of silently clamping away their intended ratio.
inline bool TryScaleDuration(double Baseline, double Multiplier, double& OutDuration)
{
    if (!std::isfinite(Baseline) || Baseline < .05 || !std::isfinite(Multiplier) || Multiplier <= 0.) return false;
    const double Scaled = Baseline / Multiplier;
    if (!std::isfinite(Scaled) || Scaled < .05) return false;
    OutDuration = Scaled;
    return true;
}

struct FStep { bool bImpact = false; bool bFinished = false; };
struct FClock
{
    double Elapsed = 0.;
    double Duration = 1.;
    double Contact = .4857142857;
    bool bActive = false;
    bool bImpactConsumed = false;
    void Start(double Seconds, double ContactProgress)
    {
        Duration = std::isfinite(Seconds) ? std::max(.05, Seconds) : 1.;
        Contact = std::isfinite(ContactProgress) ? std::clamp(ContactProgress, 0., 1.) : .5;
        Elapsed = 0.; bImpactConsumed = false; bActive = true;
    }
    void Cancel() { bActive = false; bImpactConsumed = true; }
    double Progress() const { return std::clamp(Elapsed/Duration, 0., 1.); }
    FStep Advance(double DeltaSeconds)
    {
        FStep Result;
        if(!bActive || !std::isfinite(DeltaSeconds) || DeltaSeconds < 0.) return Result;
        Elapsed = std::min(Duration, Elapsed+DeltaSeconds);
        Result.bImpact = !bImpactConsumed && Progress() >= Contact;
        // Consume before an external callback can re-enter or advance the clock again.
        if(Result.bImpact) bImpactConsumed = true;
        Result.bFinished = Elapsed >= Duration;
        return Result;
    }
};
}
