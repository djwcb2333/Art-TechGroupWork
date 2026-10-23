#include "DWCombatResourceVerificationLibrary.h"
#include "DWPlayerCharacter.h"
#include "DWCombatItemRegistry.h"
#include "DWGameInstance.h"
#include "DWInventoryComponent.h"
#include "Engine/World.h"

bool UDWCombatResourceVerificationLibrary::ApplyCombatTestCores(ADWPlayerCharacter* P)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
    if(!IsValid(P)||P->IsActorBeingDestroyed()||P->IsDead()||P->IsCombatRestoreInProgress()||
        !P->GiveCombatTestCores||P->CombatTestCoresPerType<=0||P->CombatTestCoresPerType>1000||
        !P->HasCombatVitalsReady()||!P->IsCombatActionAllowed())return false;
    auto* World=P->GetWorld();auto* GI=P->GetGameInstance<UDWGameInstance>();auto* I=P->GetInventory();
    if(!World||World->bIsTearingDown||World->WorldType!=EWorldType::PIE||World->GetNetMode()!=NM_Standalone||
        !GI||GI->HasActiveSlot()||UWorld::RemovePIEPrefix(World->GetOutermost()->GetName())!=
        TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_CombatTest")||!I||!I->CanWriteInventory())return false;
    // Both core types share one inventory revision and one final notification. Existing stock is kept.
    TArray<FDWItemStack> Slots=I->GetSlots();bool bChanged=false;
    for(FName Id:{FDWCombatItemRegistry::MoldCoreId(),FDWCombatItemRegistry::YeastCoreId()})
    {
        const int32 Missing=FMath::Max(0,P->CombatTestCoresPerType-I->CountItem(Id));
        if(Missing>0){if(!I->AddToSlots(Slots,Id,Missing))return false;bChanged=true;}
    }
    if(!bChanged)return true;
    FDWPreparedInventoryChange Prepared;
    if(!I->MakePrepared(MoveTemp(Slots),Prepared))return false;
    return I->CommitPrepared(MoveTemp(Prepared)); // No player/inventory access after the dynamic callback.
#else
    return false;
#endif
}
