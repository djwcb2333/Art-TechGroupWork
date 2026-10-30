#include "DWBuildingDemolitionSession.h"
#include "DWGatheringWorld.h"
#include "DWGatheringProfile.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWCombatAuthoritySubsystem.h"
#include "Engine/World.h"

ADWGatheringStation* FDWBuildingDemolitionSession::GetTarget()const{return Active()?Target.Get():nullptr;}

bool FDWBuildingDemolitionSession::Begin(ADWPlayerCharacter& Player,ADWGatheringStation& NewTarget,FName ToolId,double WorkSeconds)
{
    if(Active()||!FMath::IsFinite(WorkSeconds)||WorkSeconds<=0.||Player.IsDead())return false;
    auto* World=Player.GetWorld();auto* Bag=Player.GetInventory();
    auto* Profile=UDWGameplayConfig::AsGatheringConfig(Player.GetGameplayConfig());
    auto* Authority=UDWCombatAuthoritySubsystem::Get(&Player);
    const auto* Spec=Profile?Profile->Tools.Find(ToolId):nullptr;
    if(!World||World->bIsTearingDown||World->IsPaused()||!Bag||!Authority||!Spec||Spec->ToolType!=TEXT("Hammer")||
       Bag->CountItem(ToolId)<1||!FMath::IsFinite(Spec->GatheringSpeedMultiplier)||Spec->GatheringSpeedMultiplier<=0.f||
       !FMath::IsFinite(Spec->ReachCm)||Spec->ReachCm<=0.f||!Player.CanPerformGatheringAction()||
       NewTarget.bDemolished||(NewTarget.bRuntimeSpawned?(NewTarget.DemolitionCostOrigin!=1&&NewTarget.DemolitionCostOrigin!=3&&NewTarget.DemolitionCostOrigin!=4):!NewTarget.bAllowAuthoredDemolition)||
       NewTarget.IsStaged()||NewTarget.IsActorBeingDestroyed()||
       NewTarget.GetWorld()!=World||NewTarget.PersistentId.IsNone()||!NewTarget.IsWithinReach(&Player,Spec->ReachCm))return false;
    auto* PC=Cast<ADWPlayerController>(Player.GetController());if(!PC||PC->IsGameplayBlocked())return false;
    const auto PL=Authority->CaptureActorLife(&Player),TL=Authority->CaptureActorLife(&NewTarget);
    if(!PL.Entity||!PL.Life||!TL.Entity||!TL.Life)return false;
    if(!Work.Begin(NewTarget.GetUniqueID(),1,WorkSeconds))return false;
    Target=&NewTarget;Inventory=Bag;Tool=ToolId;ToolType=Spec->ToolType;Reach=Spec->ReachCm;Speed=Spec->GatheringSpeedMultiplier;
    InventoryRevision=Bag->GetInventoryRevision();DamageRevision=Player.GetCombatDamageRevision();Epoch=Authority->GetWorldEpoch();
    PlayerEntity=PL.Entity;PlayerLife=PL.Life;TargetEntity=TL.Entity;TargetLife=TL.Life;return true;
}

bool FDWBuildingDemolitionSession::Current(ADWPlayerCharacter& Player,ADWGatheringStation* Focus,FName EquippedTool,bool Held)const
{
    auto* T=Target.Get();auto* Bag=Inventory.Get();auto* World=Player.GetWorld();
    auto* Profile=UDWGameplayConfig::AsGatheringConfig(Player.GetGameplayConfig());
    auto* Authority=UDWCombatAuthoritySubsystem::Get(&Player);
    const auto* Spec=Profile?Profile->Tools.Find(Tool):nullptr;
    auto* PC=Cast<ADWPlayerController>(Player.GetController());
    if(!Active()||!Held||Tool!=EquippedTool||!IsValid(T)||T!=Focus||T->bDemolished||
       (T->bRuntimeSpawned?(T->DemolitionCostOrigin!=1&&T->DemolitionCostOrigin!=3&&T->DemolitionCostOrigin!=4):!T->bAllowAuthoredDemolition)||T->IsStaged()||T->IsActorBeingDestroyed()||
       !World||World->bIsTearingDown||World->IsPaused()||T->GetWorld()!=World||!IsValid(Bag)||Player.GetInventory()!=Bag||
       !PC||PC->IsGameplayBlocked()||Player.IsDead()||!Player.CanPerformGatheringAction()||
       Player.GetCombatDamageRevision()!=DamageRevision||Bag->GetInventoryRevision()!=InventoryRevision||Bag->CountItem(Tool)<1||
       !Spec||Spec->ToolType!=ToolType||Spec->ToolType!=TEXT("Hammer")||Spec->ReachCm!=Reach||Spec->GatheringSpeedMultiplier!=Speed||
       !T->IsWithinReach(&Player,Reach)||!Authority||Authority->GetWorldEpoch()!=Epoch)return false;
    const auto PL=Authority->CaptureActorLife(&Player),TL=Authority->CaptureActorLife(T);
    return PL.Entity==PlayerEntity&&PL.Life==PlayerLife&&TL.Entity==TargetEntity&&TL.Life==TargetLife;
}
bool FDWBuildingDemolitionSession::Advance(ADWPlayerCharacter& Player,ADWGatheringStation* Focus,FName EquippedTool,bool Held,double DeltaSeconds)
{
    if(!Current(Player,Focus,EquippedTool,Held)){Cancel();return false;}
    return Work.Advance(Target->GetUniqueID(),1,DeltaSeconds,Speed,true);
}
