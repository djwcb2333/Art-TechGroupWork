#include "DWWeaponAnimationSet.h"
const FDWWeaponAnimationClip* UDWWeaponAnimationSet::FindClip(FName Role) const
{
    return Clips.FindByPredicate([Role](const FDWWeaponAnimationClip& Clip){return Clip.Role==Role;});
}
