#include "DWPlayerCharacter.h"
#include "DWCombatRecoverySubsystem.h"

bool ADWPlayerCharacter::CanCombatRespawn()const
{auto* R=UDWCombatRecoverySubsystem::Get(this);return R&&R->CanRespawn(*this);}
bool ADWPlayerCharacter::RequestCombatRespawn()
{auto* R=UDWCombatRecoverySubsystem::Get(this);return R&&R->RequestRespawn(*this);}
