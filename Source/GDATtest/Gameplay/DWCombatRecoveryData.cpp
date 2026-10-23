#include "DWCombatRecoveryData.h"
#include "DWCombatItemRegistry.h"
#include "Misc/PackageName.h"
bool DWCombatRecovery::IsDeathDropItem(FName Id)
{
    switch(FDWCombatItemRegistry::GetItemRole(Id))
    {
        case EDWCombatItemRole::MoldCore:case EDWCombatItemRole::YeastCore:
        case EDWCombatItemRole::ReserveAmmo:case EDWCombatItemRole::ThrowableStock:return true;
        default:return false;
    }
}
bool DWCombatRecovery::Partition(const TArray<FDWItemStack>& Inventory,TArray<FDWItemStack>& Kept,TArray<FDWDeathBagStack>& Dropped)
{
    TArray<FDWItemStack> K;TArray<FDWDeathBagStack> D;
    for(const auto& Stack:Inventory)
    {
        if(Stack.ItemId.IsNone()||Stack.Quantity<=0)return false;
        if(!IsDeathDropItem(Stack.ItemId)){K.Add(Stack);continue;}
        FDWDeathBagStack Entry;Entry.ItemId=Stack.ItemId;Entry.Original=Entry.Remaining=Stack.Quantity;D.Add(Entry);
    }
    Kept=MoveTemp(K);Dropped=MoveTemp(D);return true;
}
bool DWCombatRecovery::IsEmpty(const FDWDeathBagSave& Bag)
{for(const auto& Stack:Bag.Contents)if(Stack.Remaining>0)return false;return true;}
bool DWCombatRecovery::ValidateBag(const FDWDeathBagSave& B,FString& Error)
{
    if(B.Schema!=1||!B.BagId.IsValid()||!B.PlayerId.IsValid()||!B.Revision||
        !FPackageName::IsValidLongPackageName(B.MapPackage)||B.Location.ContainsNaN())
    {Error=TEXT("Invalid death bag identity, map, revision or position.");return false;}
    for(const auto& S:B.Contents)if(!IsDeathDropItem(S.ItemId)||S.Original<=0||S.Remaining<0||S.Remaining>S.Original)
    {Error=TEXT("Death bag contains an unauthorized item or impossible quantity.");return false;}
    if(B.bClaimed!=IsEmpty(B)){Error=TEXT("Death bag terminal claim must match zero remaining content.");return false;}
    Error.Reset();return true;
}
bool DWCombatRecovery::Validate(const FDWCombatRecoverySave& S,FString& Error)
{
    if(S.Schema!=1||!S.PlayerId.IsValid()||!S.Revision)
    {Error=TEXT("Missing or invalid combat recovery snapshot.");return false;}
    const auto& P=S.SafePoint;
    if(P.bActive?(!P.PointId.IsValid()||!FPackageName::IsValidLongPackageName(P.MapPackage)||!P.Transform.IsValid()||
        P.Transform.GetScale3D()!=FVector::OneVector):(P.PointId.IsValid()||!P.MapPackage.IsEmpty()||!P.Transform.Equals(FTransform::Identity,0.)))
    {Error=TEXT("Invalid activated safe point; no scene location is invented.");return false;}
    TSet<FGuid> Seen;bool FoundCurrent=false;
    for(const auto& B:S.Bags)
    {
        if(!ValidateBag(B,Error))return false;
        if(B.PlayerId!=S.PlayerId||Seen.Contains(B.BagId))
        {Error=TEXT("Death bag owner or unique identity does not match recovery state.");return false;}
        Seen.Add(B.BagId);FoundCurrent|=B.BagId==S.CurrentDeathBagId;
    }
    if(S.bAwaitingRespawn?(!S.CurrentDeathBagId.IsValid()||!FoundCurrent):S.CurrentDeathBagId.IsValid())
    {Error=TEXT("Recovery state must identify its pending death; living state cannot replay it.");return false;}
    Error.Reset();return true;
}
bool DWCombatRecovery::PlanClaim(const FDWDeathBagSave& Bag,const TArray<FDWItemStack>& Inventory,int32 MaxSlots,
    const TMap<FName,int32>& Limits,TArray<FDWItemStack>& OutInventory,FDWDeathBagSave& OutBag,int64& OutTransferred)
{
    FString Error;if(!ValidateBag(Bag,Error)||Bag.bClaimed||Bag.Revision==MAX_uint64||MaxSlots<1||Inventory.Num()>MaxSlots)return false;
    TArray<FDWItemStack> Candidate=Inventory;auto B=Bag;int64 Total=0;
    for(const auto& S:Candidate)
    {
        const int32* Limit=Limits.Find(S.ItemId);
        if(!Limit||*Limit<1||S.Quantity<1||S.Quantity>*Limit)return false;
    }
    for(auto& S:B.Contents)
    {
        const int32* Limit=Limits.Find(S.ItemId);if(!Limit||*Limit<1)return false;
        if(S.Remaining<=0)continue;
        for(auto& Target:Candidate)if(Target.ItemId==S.ItemId&&Target.Quantity<*Limit&&S.Remaining>0)
        {
            const int32 Transfer=FMath::Min(S.Remaining,*Limit-Target.Quantity);
            Target.Quantity+=Transfer;S.Remaining-=Transfer;Total+=Transfer;
        }
        while(S.Remaining>0&&Candidate.Num()<MaxSlots)
        {
            const int32 Transfer=FMath::Min(S.Remaining,*Limit);FDWItemStack Target;Target.ItemId=S.ItemId;Target.Quantity=Transfer;
            Candidate.Add(Target);S.Remaining-=Transfer;Total+=Transfer;
        }
    }
    if(Total<=0)return false;
    ++B.Revision;B.bClaimed=IsEmpty(B);OutInventory=MoveTemp(Candidate);OutBag=MoveTemp(B);OutTransferred=Total;return true;
}
