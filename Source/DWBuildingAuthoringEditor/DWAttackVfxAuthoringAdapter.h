#pragma once
#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "DWAttackVfxAuthoringBridge.h"
class UDWAttackVfxProfile;
TSharedPtr<IDWAttackVfxAuthoringBridge> MakeDWAttackVfxAuthoringBridge(UDWAttackVfxProfile* Profile,FString& Error);
TArray<FAssetData> GetDWAttackVfxProfileCatalog();
// One lifetime per existing Editor module. Registers only the concrete cosmetic Profile class.
void RegisterDWAttackVfxProfileAssetActions();
void UnregisterDWAttackVfxProfileAssetActions();
void OpenDWAttackVfxProfile(UDWAttackVfxProfile* Profile);
void OpenDefaultDWAttackVfxProfile();
void RegisterDWAttackVfxProfileCatalogMenu();

#include "DWWeaponVisualSampler.h"
#include "DWAttackVfxResolver.h"
namespace DWAttackVfxAuthoringMath
{
inline bool Valid(const FTransform& T){return T.IsValid()&&T.GetScale3D().GetAbsMin()>KINDA_SMALL_NUMBER;}
inline FTransform TrailWorld(const FTransform& Weapon,const FVector& Tip,const FTransform& Local,bool AtTip,const FTransform& Owner)
{return DWWeaponVisualSampler::ResolveTrailAttachment(Weapon,Tip,Local.GetTranslation(),Local.Rotator(),Local.GetScale3D(),AtTip)*Owner;}
inline bool InverseTrailWorld(const FTransform& World,const FTransform& Weapon,const FVector& Tip,bool AtTip,const FTransform& Owner,FTransform& Out)
{
 if(!Valid(World)||!Valid(Weapon)||!Valid(Owner)||Tip.ContainsNaN())return false;
 auto Desired=World.GetRelativeTransform(Owner);
 if(AtTip)Desired.SetTranslation(Desired.GetTranslation()-Tip+Weapon.GetTranslation());
 Out=Desired.GetRelativeTransform(Weapon);return Valid(Out);
}
inline bool EventWorld(const FDWCombatEffectBinding& Binding,double Radius,const FTransform& Parent,FTransform& Out)
{
 FTransform Local;if(!DWAttackVfxResolver::ResolveEventLocalTransform(Binding,Radius,Local)||!Valid(Parent))return false;
 Out=Local*Parent;return Valid(Out);
}
inline bool InverseEventWorld(const FTransform& World,const FDWCombatEffectBinding& Binding,double Radius,const FTransform& Parent,FTransform& Out)
{
 if(!Valid(World)||!Valid(Parent))return false;
 return DWAttackVfxResolver::RemoveEventRadiusScale(Binding,Radius,World.GetRelativeTransform(Parent),Out)&&Valid(Out);
}
}
