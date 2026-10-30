#pragma once
#include "CoreMinimal.h"
#include "DWDemolitionWork.h"
class ADWPlayerCharacter;
class ADWGatheringStation;
class UDWInventoryComponent;

// Per-player, transient, game-thread session. It does not mutate the building, inventory or saves.
class FDWBuildingDemolitionSession
{
public:
    bool Begin(ADWPlayerCharacter& Player,ADWGatheringStation& Target,FName ToolId,double WorkSeconds);
    bool Current(ADWPlayerCharacter& Player,ADWGatheringStation* Focus,FName EquippedTool,bool bHeld) const;
    bool Advance(ADWPlayerCharacter& Player,ADWGatheringStation* Focus,FName EquippedTool,bool bHeld,double DeltaSeconds);
    bool ConsumeCompletion(){return Work.Commit();}
    void Cancel(){Work.Reset();Target.Reset();Inventory.Reset();Tool=NAME_None;}
    bool Active()const{return Work.State==DWDemolitionWork::EState::Working||Work.State==DWDemolitionWork::EState::Ready;}
    bool Ready()const{return Work.State==DWDemolitionWork::EState::Ready;}
    double Progress()const{return Active()?Work.Progress():0.;}
    ADWGatheringStation* GetTarget()const;
private:
    DWDemolitionWork::FWork Work;
    TWeakObjectPtr<ADWGatheringStation> Target;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    FName Tool,ToolType;
    float Reach=0.f,Speed=1.f;
    int64 InventoryRevision=0;
    uint64 Epoch=0,PlayerEntity=0,PlayerLife=0,TargetEntity=0,TargetLife=0,DamageRevision=0;
};
