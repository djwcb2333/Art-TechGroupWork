#include "DWCombatReleasedEffects.h"
#include "DWCombatSporeProjectile.h"
#include "DWCombatBowProjectile.h"
#include "DWCombatBottleProjectile.h"
#include "DWCombatGroundAreaSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
bool DWCombatReleasedEffects::HasUnserialized(UWorld* World)
{
    if(!World||World->bIsTearingDown)return false;
    for(TActorIterator<ADWCombatSporeProjectile> It(World);It;++It)
        if(!It->IsActorBeingDestroyed()&&It->IsReleased()&&!It->IsSpent())return true;
    if(ADWCombatBowProjectile::HasUnserializedReleasedArrows(World)||
        ADWCombatBottleProjectile::HasReleasedBottle(World))return true;
    const auto* Areas=UDWCombatGroundAreaSubsystem::Get(World);
    return Areas&&Areas->HasUnserializedReleasedAreas();
}
