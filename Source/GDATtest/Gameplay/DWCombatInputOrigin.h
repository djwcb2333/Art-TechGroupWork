#pragma once
#include "DWCombatInputLedger.h"

/** Identity check only. Authority to observe/consume a physical edge stays in the input adapter. */
inline bool DWCombatInputOriginsMatch(const FDWCombatInputPress& A, const FDWCombatInputPress& B)
{
    return A.Edge.Context.IsValid() && A.Edge.Sequence != 0 && !A.Edge.Key.IsNone() && !A.Action.IsNone()
        && A.Edge.bDown && !A.Edge.bDuringSuspension && B.Edge.bDown && !B.Edge.bDuringSuspension
        && A.Edge.Context == B.Edge.Context && A.Edge.Sequence == B.Edge.Sequence
        && A.Edge.Key == B.Edge.Key && A.Action == B.Action;
}
