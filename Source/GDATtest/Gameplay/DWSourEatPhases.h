#pragma once
#include "CoreMinimal.h"
enum class EDWSourEatPhase : uint8 { None, Enter, Loop, Exit };
struct FDWSourEatPhases
{
    EDWSourEatPhase Phase=EDWSourEatPhase::None;
    float Elapsed=0.f;
    EDWSourEatPhase Step(bool bEligible,bool bRequested,float Delta,float EnterSeconds,float ExitSeconds)
    {
        if (!bEligible) { Phase=EDWSourEatPhase::None; Elapsed=0.f; return Phase; }
        const float Dt=FMath::IsFinite(Delta) ? FMath::Clamp(Delta,0.f,1.f) : 0.f;
        const float Enter=FMath::IsFinite(EnterSeconds) ? FMath::Max(EnterSeconds,.001f) : .001f;
        const float Exit=FMath::IsFinite(ExitSeconds) ? FMath::Max(ExitSeconds,.001f) : .001f;
        switch (Phase)
        {
        case EDWSourEatPhase::None: if (bRequested) { Phase=EDWSourEatPhase::Enter; Elapsed=0.f; } break;
        case EDWSourEatPhase::Enter:
            if (!bRequested) { Phase=EDWSourEatPhase::None; Elapsed=0.f; }
            else { Elapsed+=Dt; if (Elapsed>=Enter) { Phase=EDWSourEatPhase::Loop; Elapsed=0.f; } } break;
        case EDWSourEatPhase::Loop: if (!bRequested) { Phase=EDWSourEatPhase::Exit; Elapsed=0.f; } break;
        case EDWSourEatPhase::Exit: Elapsed+=Dt; if (Elapsed>=Exit) { Phase=EDWSourEatPhase::None; Elapsed=0.f; } break;
        }
        return Phase;
    }
};
