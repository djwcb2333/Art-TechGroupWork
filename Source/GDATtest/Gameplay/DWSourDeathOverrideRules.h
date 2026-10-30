#pragma once

// Works with UE TArray and test array adapters. This is a decision only;
// actual mutation must use UMeshComponent public material APIs on the game thread.
namespace DWSourDeathOverrideRules
{
template<class T> auto Raw(const T& Value)->decltype(Value.Get()){return Value.Get();}
inline int Raw(int Value){return Value;}
template<class TOriginal,class TCurrent,class TBindings>
bool CanRebuild(const TOriginal& Original,const TCurrent& Current,const TBindings& Bindings,
                int OverlayCount,int MeshSlotCount)
{
    if(Bindings.Num()<=0||OverlayCount<0||OverlayCount>MeshSlotCount)return false;
    const int Expected=Original.Num()>Bindings.Num()?Original.Num():Bindings.Num();
    if(Current.Num()!=Expected)return false;
    for(int I=0;I<Bindings.Num();++I)
        if(Bindings[I].Slot!=I||!Current.IsValidIndex(I)||Raw(Current[I])!=Raw(Bindings[I].Instance))return false;
    for(int I=Bindings.Num();I<Original.Num();++I)
        if(!Current.IsValidIndex(I)||Raw(Current[I])!=Raw(Original[I]))return false;
    return true;
}
}
