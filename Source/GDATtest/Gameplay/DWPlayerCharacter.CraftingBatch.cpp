#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWWeaponAnimationComponent.h"
#include "DWLocalizationLibrary.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

bool ADWPlayerCharacter::CanCraftRecipeBatch(FName Id,int32 Count) const
{return !IsActorBeingDestroyed()&&!IsDead()&&!bRestoringSave&&!bGameplayActionInProgress&&Inventory&&Inventory->CanCraftRecipeBatch(Id,bYeastForm,Count);}
bool ADWPlayerCharacter::CraftRecipeBatch(FName Id,int32 Count)
{
    if(Count==1)return CraftRecipe(Id); // Includes the existing permanent weapon joint transaction.
    if(!CanCraftRecipeBatch(Id,Count))return false;
    // A commit listener can destroy the player or collect garbage. Keep the guard's
    // referenced memory alive until it unwinds; weak/life checks still reject gameplay.
    const TStrongObjectPtr<ADWPlayerCharacter> KeepAlive(this);
    const TStrongObjectPtr<UDWInventoryComponent> KeepBagAlive(Inventory.Get());
    TGuardValue<bool> Guard(bGameplayActionInProgress,true);
    TWeakObjectPtr<ADWPlayerCharacter> WeakPlayer(this);TWeakObjectPtr<UDWInventoryComponent> Bag(Inventory.Get());
    auto* A=UDWCombatAuthoritySubsystem::Get(this);TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority(A);
    const auto Life=A?A->CaptureActorLife(this):DWCombatCommit::FEntityLife{};
    const uint64 Epoch=A?A->GetWorldEpoch():0;
    TWeakObjectPtr<UDWWeaponAnimationComponent> Weapon(WeaponAnimationComponent.Get());
    const bool HadWeapon=Weapon.IsValid();const uint64 Restore=HadWeapon?Weapon->GetCombatWeaponRestoreEpoch():0;
    TWeakObjectPtr<UWorld> World(GetWorld());
    const bool YeastAtPrepare=bYeastForm;
    auto Current=[&]()
    {
        return WeakPlayer.IsValid()&&!WeakPlayer->IsActorBeingDestroyed()&&!WeakPlayer->IsDead()&&!WeakPlayer->bRestoringSave&&WeakPlayer->bYeastForm==YeastAtPrepare&&WeakPlayer->GetInventory()==Bag.Get()&&Bag.IsValid()&&
            World.IsValid()&&!World->bIsTearingDown&&WeakPlayer->GetWorld()==World.Get()&&Bag->GetOwner()==WeakPlayer.Get()&&
            WeakPlayer->WeaponAnimationComponent.Get()==Weapon.Get()&&(!HadWeapon||(Weapon.IsValid()&&Weapon->GetCombatWeaponRestoreEpoch()==Restore))&&
            (!Epoch||(Authority.IsValid()&&UDWCombatAuthoritySubsystem::Get(WeakPlayer.Get())==Authority.Get()&&Authority->GetWorldEpoch()==Epoch&&
                Authority->FindRegisteredActorLife(WeakPlayer.Get()).Entity==Life.Entity&&Authority->FindRegisteredActorLife(WeakPlayer.Get()).Life==Life.Life));
    };
    FDWPreparedInventoryChange Plan;
    const bool Prepared=Bag.IsValid()&&Bag->PrepareCraftRecipeBatch(Id,bYeastForm,Count,Plan);
    const bool Done=Prepared&&Current()&&Bag->CommitPrepared(MoveTemp(Plan));
    // Facts are committed once before any notice. A listener may destroy or restore the player.
    if(Current())
    {
        Notify(Done?FText::Format(DWText(this,TEXT("制作完成 × {0}"),TEXT("Crafted x {0}")),FText::AsNumber(Count)):
            DWText(this,TEXT("制作失败：请检查材料、空间和制作条件"),TEXT("Crafting failed. Check materials, space and requirements.")));
        if(Done&&Current())OnRecipeCrafted(Id);
    }
    return Done;
}
