#pragma once
#include "CoreMinimal.h"
#include "Engine/World.h"
/** Non-persistent editor capability context. Never a formal/trial save owner. */
namespace DWCombatSandbox
{
inline bool ModeAllowed(bool Editor,bool PIE,bool Commandlet,bool ExactMap)
{return Editor&&PIE&&!Commandlet&&ExactMap;}
inline bool IsWorld(const UWorld* W)
{
#if WITH_EDITOR
 return W&&!W->bIsTearingDown&&ModeAllowed(true,W->WorldType==EWorldType::PIE&&W->GetNetMode()==NM_Standalone,IsRunningCommandlet(),
  UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_CombatTest"));
#else
 return false;
#endif
}
}
