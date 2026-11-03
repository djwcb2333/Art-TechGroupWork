#pragma once
#include "CoreMinimal.h"
#include "DWEnemyCombatProfile.h"

/** Exact authored identity; other enemies and their drop policies remain unchanged. */
namespace DWThugDropPolicy
{
inline bool IsTargetSavedClass(const FString& ClassPath,EDWEnemyCombatProfile Profile)
{
 return (Profile==EDWEnemyCombatProfile::MoldGrunt&&ClassPath==TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWCreature_MoldThug.BP_DWCreature_MoldThug_C"))||
        (Profile==EDWEnemyCombatProfile::YeastGrunt&&ClassPath==TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWCreature_YeastThug.BP_DWCreature_YeastThug_C"));
}
inline bool CanAttemptSpawn(bool Bounded,int32 Attempts,double Now,double Next)
{return !Bounded||(Attempts<3&&Now>=Next);}
inline bool IsTarget(const FString& Definition, EDWEnemyCombatProfile Profile)
{
 return (Profile==EDWEnemyCombatProfile::MoldGrunt&&Definition==TEXT("/Game/DoughWorld/Core/Data/DA_DWCreature_MoldThug.DA_DWCreature_MoldThug"))||
        (Profile==EDWEnemyCombatProfile::YeastGrunt&&Definition==TEXT("/Game/DoughWorld/Core/Data/DA_DWCreature_YeastThug.DA_DWCreature_YeastThug"));
}
inline bool IsCoreAlias(FName Id, EDWEnemyCombatProfile Profile)
{
 return Profile==EDWEnemyCombatProfile::MoldGrunt?(Id==TEXT("DWCore_Mold")||Id==TEXT("MoldEssence")):
        Profile==EDWEnemyCombatProfile::YeastGrunt&&(Id==TEXT("DWCore_Yeast")||Id==TEXT("YeastEssence"));
}
inline FName CoreId(EDWEnemyCombatProfile Profile,bool Essence)
{
 return Profile==EDWEnemyCombatProfile::MoldGrunt?FName(Essence?TEXT("MoldEssence"):TEXT("DWCore_Mold")):
        Profile==EDWEnemyCombatProfile::YeastGrunt?FName(Essence?TEXT("YeastEssence"):TEXT("DWCore_Yeast")):NAME_None;
}
}
