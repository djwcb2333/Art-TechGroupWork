#pragma once
#include "CoreMinimal.h"
#include "DWCombatMoldRoot.h"
class ADWPlayerCharacter;
struct FDWCombatMoldRootNotice
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    FDWMoldRootIdentity Identity;
};
// Fixed RootDeployment fee + stance transaction. No inventory, callbacks or caller supplied mutation.
class FDWCombatMoldRootTransaction
{
public:
    static bool CommitEntry(ADWPlayerCharacter& Player);
    static void Publish(const FDWCombatMoldRootNotice& Notice);
};
