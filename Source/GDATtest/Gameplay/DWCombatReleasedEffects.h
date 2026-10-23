#pragma once
class UWorld;
namespace DWCombatReleasedEffects
{
    // Read-only temporary guard until D22 serializes every released carrier and ground area.
    bool HasUnserialized(UWorld* World);
}
