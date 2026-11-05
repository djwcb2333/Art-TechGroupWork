#pragma once
#include "CoreMinimal.h"
#include "DWCombatSpear.h"
namespace DWSimpleSpearPolicy
{
 inline FName Item(){return FName(TEXT("SimpleSugarSpear"));}
 struct FFacts
 {
  FName ItemId;int32 Slot=INDEX_NONE,Quantity=0;int64 InventoryRevision=-1,EquipmentRevision=-1;
  uint64 Epoch=0,Entity=0,Life=0,Restore=0,Action=0;bool bSelected=false,bAlive=false;
 };
 inline bool Current(const FFacts& Expected,const FFacts& Actual)
 {
  return Expected.ItemId==Item()&&Expected.Quantity>0&&Expected.Slot>=0&&Expected.InventoryRevision>=0&&
   Expected.EquipmentRevision>=0&&Expected.Epoch&&Expected.Entity&&Expected.Life&&Actual.bSelected&&Actual.bAlive&&
   Expected.ItemId==Actual.ItemId&&Expected.Slot==Actual.Slot&&Expected.Quantity==Actual.Quantity&&
   Expected.InventoryRevision==Actual.InventoryRevision&&Expected.EquipmentRevision==Actual.EquipmentRevision&&
   Expected.Epoch==Actual.Epoch&&Expected.Entity==Actual.Entity&&Expected.Life==Actual.Life&&
   Expected.Restore==Actual.Restore&&Expected.Action==Actual.Action;
 }
 inline bool Build(double Damage,double Period,double Range,double SharedArc,FVector2D SourceFractions,
                   double RootDamage,FDWSpearTuning& Out,double KnockbackCm=0.,double KnockbackSeconds=.15)
 {
  if(!FMath::IsFinite(Damage)||Damage<=0.||!FMath::IsFinite(Period)||Period<=0.||!FMath::IsFinite(Range)||Range<=0.||
    !FMath::IsFinite(SourceFractions.X)||!FMath::IsFinite(SourceFractions.Y)||SourceFractions.X<0.||
    SourceFractions.Y<=SourceFractions.X||SourceFractions.Y>1.)return false;
  FDWSpearTuning T;T.Damage=Damage;T.RangeCm=Range;T.FullArcDegrees=SharedArc;T.IntervalSeconds=Period;
  T.WindupSeconds=Period*SourceFractions.X;T.ActiveSeconds=Period*(SourceFractions.Y-SourceFractions.X);
  T.RecoverySeconds=Period-T.WindupSeconds-T.ActiveSeconds;
  T.EdgeBandFraction=1.;T.MinimumDamageFraction=1.;T.KnockbackCm=KnockbackCm;T.KnockbackSeconds=KnockbackSeconds;
  T.YeastDamageMultiplier=T.YeastRateMultiplier=1.;T.RootedDamageMultiplier=RootDamage;T.SwitchLockSeconds=0.;
  if(!T.IsValid())return false;Out=T;return true;
 }
}
