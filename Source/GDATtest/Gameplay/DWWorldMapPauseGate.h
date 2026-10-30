#pragma once

// During a map-only release, do not query or consume an external owner's gate.
// All other clear requests retain that owner's normal delegate semantics.
namespace DWWorldMapPauseGate
{
    template<typename Evaluate>
    bool AllowExternalClear(bool bMapOnlyRelease,Evaluate&& EvaluateOwner)
    {
        return !bMapOnlyRelease && EvaluateOwner();
    }
}
