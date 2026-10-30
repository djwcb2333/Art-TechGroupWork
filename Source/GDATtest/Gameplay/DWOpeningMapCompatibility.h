#pragma once
#include "CoreMinimal.h"

class UDWSaveGame;
class UDWGameplayConfig;
class FDWCombatRules;
class FJsonObject;
namespace DWLanguage { struct FCatalog; }

/** One approved map relocation. No redirector discovery, general rename table or slot I/O. */
namespace DWOpeningMapCompatibility
{
 enum class EReadProjection:uint8 { Unchanged,Projected,Rejected };
 const TCHAR* LegacyPackage();
 const TCHAR* CurrentPackage();
 /** Exact case-sensitive package alias only; object paths and arbitrary strings are not rewritten. */
 FString CanonicalPackage(const FString& Package);
 /** Pending/phase-2 only. Completed historical metadata and its current formal world remain wholly untouched. */
 bool NeedsReadProjection(const UDWSaveGame& Source);
 /** Deep transient copy; Source is never mutated. Full existing D22 validation runs before and after projection. */
 EReadProjection BuildReadCopy(const UDWSaveGame& Source,const FDWCombatRules& Rules,const DWLanguage::FCatalog* Catalog,const TSharedPtr<FJsonObject>& Trial,UDWSaveGame*& Out,FString& Error);
 /** Canonical lookup without mutating the authored registry. Old+new keys are ambiguous even if values match. */
 bool FindProfileOverride(const UDWGameplayConfig& Config,const FString& CanonicalMap,
  const TSoftObjectPtr<UDWGameplayConfig>*& Out,FString& Error);
}
