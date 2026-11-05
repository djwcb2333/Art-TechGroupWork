#include "DWEnemyCharacter.h"
#include "DWCreatureDropConfiguration.h"
#include "DWThugDropPolicy.h"

bool ADWEnemyCharacter::ResolveCreatureDrops(FDWCreatureResolvedDrops& Out,FString& Error)const
{
 const auto* Defaults=GetClass()->GetDefaultObject<ADWEnemyCharacter>();
 if(!Defaults){Error=TEXT("Missing creature class defaults");return false;}
 // Preserve existing live Definition admission, including NPC instances. Only the NEW override table
 // is class-default-only. Existing capture/restore Definition identity checks remain untouched.
 FDWCreatureDropInput In;const auto* Definition=CreatureDefinition.Get();
 In.Profile=Definition&&Definition->NumericSource==EDWCreatureNumericSource::ExistingFormalProfile?Definition->FormalProfile:CombatProfile;
 if(Definition){In.InheritedPolicy=Definition->DropPolicy;In.InheritedEntries=Definition->ItemDrops;In.Disposition=Definition->Disposition;}
 else In.InheritedPolicy=In.Profile==EDWEnemyCombatProfile::Legacy?EDWCreatureDropPolicy::None:EDWCreatureDropPolicy::ExistingFormalCore;
 In.bOverride=Defaults->bOverrideCreatureDropTable;In.OverrideEntries=Defaults->CreatureDropEntries;
 In.bFallback=Defaults->bUseDefaultHostileDropWhenEmpty;In.FallbackEntry=Defaults->DefaultHostileDrop;
 In.bFixedThugCore=Definition&&DWThugDropPolicy::IsTarget(Definition->GetPathName(),In.Profile);
 Out=DWCreatureDropConfiguration::Resolve(In);Error.Reset();return true;
}
bool ADWEnemyCharacter::UsesConfiguredCreatureDrops()const
{
 FDWCreatureResolvedDrops Drop;FString Error;
 // An invalid override must reach preparation and reject the fatal commit, never bypass it.
 return !ResolveCreatureDrops(Drop,Error)||Drop.Policy==EDWCreatureDropPolicy::ConfiguredItems;
}
