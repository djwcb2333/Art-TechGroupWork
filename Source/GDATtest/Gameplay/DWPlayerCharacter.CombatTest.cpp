#include "DWPlayerCharacter.h"
#include "DWCombatResourceVerificationLibrary.h"
#include "DWInventoryComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatEquipment.h"
#include "DWCombatItemRegistry.h"
#include "DWGameInstance.h"
#include "DWPlayerController.h"
#include "DWWeaponAnimationComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

bool ADWPlayerCharacter::TryGiveCombatTestWeapons()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    if(bCombatTestWeaponsFinished)return true;
    if(bGivingCombatTestWeapons)return false;
    auto* World=GetWorld();auto* GI=GetGameInstance<UDWGameInstance>();
    auto* Weapon=WeaponAnimationComponent.Get();
    const auto EligibleWorld=[&]()
    {
        return GiveCombatTestWeapons&&IsValid(World)&&!World->bIsTearingDown&&World->WorldType==EWorldType::PIE&&
            World->GetNetMode()==NM_Standalone&&IsValid(GI)&&!GI->HasActiveSlot()&&
            UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())==
                TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_CombatTest");
    };
    if(!EligibleWorld()||IsActorBeingDestroyed()||IsDead()||IsCombatRestoreInProgress())
    {bCombatTestWeaponsFinished=true;return true;}
    if(!HasCombatVitalsReady()||!IsValid(Weapon)||Weapon->IsBeingDestroyed())return true;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return true;
    if(!CombatTestWeaponGrantEpoch)
    {
        CombatTestWeaponGrantWorld=World;CombatTestWeaponGrantComponent=Weapon;
        CombatTestWeaponGrantLife=Authority->CaptureActorLife(this);
        CombatTestWeaponGrantEpoch=Authority->GetWorldEpoch();
        CombatTestWeaponGrantRestoreEpoch=Weapon->GetCombatWeaponRestoreEpoch();
    }
    const TWeakObjectPtr<ADWPlayerCharacter> WeakSelf(this);
    const TWeakObjectPtr<UDWCombatAuthoritySubsystem> WeakAuthority(Authority);
    const auto Current=[&]()
    {
        if(!WeakSelf.IsValid()||IsActorBeingDestroyed()||IsDead()||IsCombatRestoreInProgress()||
            GetWorld()!=World||GetGameInstance<UDWGameInstance>()!=GI||!EligibleWorld()||
            CombatTestWeaponGrantWorld.Get()!=World||CombatTestWeaponGrantComponent.Get()!=Weapon||
            !IsValid(Weapon)||Weapon->IsBeingDestroyed()||WeaponAnimationComponent.Get()!=Weapon||
            !WeakAuthority.IsValid()||UDWCombatAuthoritySubsystem::Get(this)!=Authority||
            Authority->GetWorldEpoch()!=CombatTestWeaponGrantEpoch||
            Weapon->GetCombatWeaponRestoreEpoch()!=CombatTestWeaponGrantRestoreEpoch)return false;
        const auto Life=Authority->FindRegisteredActorLife(this);
        return Life.Entity!=0&&Life.Entity==CombatTestWeaponGrantLife.Entity&&Life.Life==CombatTestWeaponGrantLife.Life;
    };
    if(!Current()){bCombatTestWeaponsFinished=true;return false;}
    // BeginPlay can precede possession, the test director and the first physical-input reconciliation.
    const auto* PC=Cast<ADWPlayerController>(GetController());
    if(!PC||PC->GetPawn()!=this||!IsCombatActionAllowed())return true;
    bGivingCombatTestWeapons=true;
    ON_SCOPE_EXIT {if(WeakSelf.IsValid())bGivingCombatTestWeapons=false;};
    const auto Action=Authority->BeginAction(this);
    if(!Action.Id){bCombatTestWeaponsFinished=true;return true;}
    ON_SCOPE_EXIT
    {
        if(WeakAuthority.IsValid()&&Authority->GetWorldEpoch()==Action.Epoch)Authority->EndAction(Action);
    };
    constexpr EDWAnimatedWeapon Weapons[]={EDWAnimatedWeapon::Spear,EDWAnimatedWeapon::Greatsword,
        EDWAnimatedWeapon::ShieldAxe,EDWAnimatedWeapon::DualDaggers,EDWAnimatedWeapon::SporeGun,
        EDWAnimatedWeapon::Shotgun,EDWAnimatedWeapon::MoldBow};
    for(int32 Index=0;Index<static_cast<int32>(UE_ARRAY_COUNT(Weapons));++Index)
    {
        if(!Current())
        {if(WeakSelf.IsValid())bCombatTestWeaponsFinished=true;return false;}
        // A grant notification may pause the game. Keep the remaining missing items for the next actionable tick.
        if(!IsCombatActionAllowed())return false;
        const auto* Binding=FDWCombatItemRegistry::FindPresentation(Weapons[Index]);
        if(!Binding||Binding->ItemRole!=EDWCombatItemRole::PermanentWeapon)
        {bCombatTestWeaponsFinished=true;return true;}
        if(Weapon->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Binding->ItemId))continue;
        const FDWCombatEquipmentGrantRequest Request{Action,CombatTestWeaponGrantLife,
            static_cast<uint64>(Index)+1,CombatTestWeaponGrantRestoreEpoch,Binding->ItemId};
        const auto Result=FDWCombatEquipmentTransaction::Grant(*Weapon,Request);
        if(!Current())
        {if(WeakSelf.IsValid())bCombatTestWeaponsFinished=true;return false;}
        if(!Result.bCommittedThisCall)
        {
            // This is a one-shot testing supply, not an endless retry or a production acquisition route.
            bCombatTestWeaponsFinished=true;
            UE_LOG(LogTemp,Warning,TEXT("CombatTest weapon grant rejected for %s; remaining test grants stopped."),*Binding->ItemId.ToString());
            return true;
        }
    }
    bCombatTestWeaponsFinished=true;
    return IsCombatActionAllowed();
#else
    return true;
#endif
}


bool ADWPlayerCharacter::TryGiveCombatTestCores()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    if(bCombatTestCoresFinished)return true;
    auto* World=GetWorld();auto* GI=GetGameInstance<UDWGameInstance>();
    if(!GiveCombatTestCores||CombatTestCoresPerType<=0||CombatTestCoresPerType>1000||
        !World||World->bIsTearingDown||World->WorldType!=EWorldType::PIE||World->GetNetMode()!=NM_Standalone||
        !GI||GI->HasActiveSlot()||UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())!=
        TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_CombatTest")||
        IsActorBeingDestroyed()||IsDead()||IsCombatRestoreInProgress())
    {bCombatTestCoresFinished=true;return true;}
    const auto* PC=Cast<ADWPlayerController>(GetController());
    if(!HasCombatVitalsReady()||!GetInventory()||!PC||PC->GetPawn()!=this||!IsCombatActionAllowed())return true;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority)return true;
    const auto Life=Authority->CaptureActorLife(this);const uint64 Epoch=Authority->GetWorldEpoch();
    const TWeakObjectPtr<ADWPlayerCharacter> Self(this);const TWeakObjectPtr<UWorld> WeakWorld(World);
    const TWeakObjectPtr<UDWInventoryComponent> OriginalInventory(GetInventory());
    // Mark before the final inventory delegate: reentry cannot refill the just-consumed test stock.
    bCombatTestCoresFinished=true;
    const bool bApplied=UDWCombatResourceVerificationLibrary::ApplyCombatTestCores(this);
    if(!Self.IsValid()||IsActorBeingDestroyed()||!WeakWorld.IsValid()||World->bIsTearingDown||GetWorld()!=World||
        GetGameInstance<UDWGameInstance>()!=GI||GetInventory()!=OriginalInventory.Get()||IsDead()||IsCombatRestoreInProgress())return false;
    Authority=UDWCombatAuthoritySubsystem::Get(this);if(!Authority||Authority->GetWorldEpoch()!=Epoch)return false;
    const auto After=Authority->FindRegisteredActorLife(this);
    if(After.Entity!=Life.Entity||After.Life!=Life.Life)return false;
    if(!bApplied)UE_LOG(LogTemp,Warning,TEXT("CombatTest core stock did not fit or was rejected; no core supply was committed. Free capacity and restart PIE to retry."));
    return IsCombatActionAllowed();
#else
    return true;
#endif
}
