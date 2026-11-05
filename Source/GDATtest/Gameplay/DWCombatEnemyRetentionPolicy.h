#pragma once
#include "DWEnemyCombatProfile.h"

/** Selects which already-dead source needs retention. Does not authorize death, loot or saving.
 * Lifecycle, staged-world, health, numeric identity and strict snapshot checks stay at the caller.
 */
namespace DWCombatEnemyRetentionPolicy
{
 inline bool NeedsSourceTombstone(EDWEnemyCombatProfile Profile,bool HasCreatureDefinition,bool GenericDropsCommitted)
 {
  // Generic schema-2 death facts outlive the corpse for Legacy hosts too, including empty/failed groups.
  return Profile!=EDWEnemyCombatProfile::Legacy||HasCreatureDefinition||GenericDropsCommitted;
 }
}
