#pragma once
#include "CoreMinimal.h"
#include "DWCreatureDefinition.h"

/** Pure class-configuration projection. Never samples, grants items or creates a death. */
struct FDWCreatureDropInput
{
 EDWCreatureDropPolicy InheritedPolicy=EDWCreatureDropPolicy::None;
 EDWCreatureDisposition Disposition=EDWCreatureDisposition::InheritExisting;
 EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::Legacy;
 TArray<FDWCreatureDropEntry> InheritedEntries,OverrideEntries;
 bool bOverride=false,bFallback=true,bFixedThugCore=false;
 FDWCreatureDropEntry FallbackEntry;
};
struct FDWCreatureResolvedDrops
{
 EDWCreatureDropPolicy Policy=EDWCreatureDropPolicy::None;
 TArray<FDWCreatureDropEntry> Entries;
 bool bBlueprintOverride=false,bUsedHostileDefault=false;
};
namespace DWCreatureDropConfiguration
{
 inline FDWCreatureResolvedDrops Resolve(const FDWCreatureDropInput& In)
 {
  FDWCreatureResolvedDrops Out;Out.Policy=In.InheritedPolicy;Out.Entries=In.InheritedEntries;
  if(In.bOverride)
  {Out.Policy=EDWCreatureDropPolicy::ConfiguredItems;Out.Entries=In.OverrideEntries;Out.bBlueprintOverride=true;return Out;}
  const bool HasCore=In.InheritedPolicy==EDWCreatureDropPolicy::ConfiguredFormalCore||
   (In.InheritedPolicy==EDWCreatureDropPolicy::ExistingFormalCore&&
    (In.Profile==EDWEnemyCombatProfile::MoldGrunt||In.Profile==EDWEnemyCombatProfile::YeastGrunt));
  const bool HasItems=In.InheritedPolicy==EDWCreatureDropPolicy::ConfiguredItems&&!In.InheritedEntries.IsEmpty();
  if(In.bFallback&&!In.bFixedThugCore&&!HasCore&&!HasItems&&In.Disposition!=EDWCreatureDisposition::Neutral)
  {Out.Policy=EDWCreatureDropPolicy::ConfiguredItems;Out.Entries={In.FallbackEntry};Out.bUsedHostileDefault=true;}
  return Out;
 }
}
