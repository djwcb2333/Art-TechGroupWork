#pragma once
#include "CoreMinimal.h"
#include "DWGameplayTypes.h"
namespace DWSugarMaterial
{
inline FName Canonical(FName Id){return Id==TEXT("HardSugarChunk")?FName(TEXT("SugarChunk")):Id;}
inline bool Matches(FName Actual,FName Required){return Canonical(Actual)==Canonical(Required);}
// Craft-only compatibility; snapshots, saves, item registration and slot limits keep exact IDs.
inline int32 Count(const TArray<FDWItemStack>& Slots,FName Required)
{
    int64 Total=0;for(const auto& S:Slots)if(Matches(S.ItemId,Required))Total+=S.Quantity;
    return static_cast<int32>(FMath::Min<int64>(Total,MAX_int32));
}
inline bool Remove(TArray<FDWItemStack>& Slots,FName Required,int32 Quantity)
{
    if(Quantity<1||Count(Slots,Required)<Quantity)return false;
    int32 Remaining=Quantity;
    for(auto& S:Slots)if(Matches(S.ItemId,Required))
    {const int32 N=FMath::Min(Remaining,S.Quantity);S.Quantity-=N;Remaining-=N;if(!Remaining)break;}
    Slots.RemoveAll([](const FDWItemStack& S){return S.Quantity==0;});return true;
}
}
