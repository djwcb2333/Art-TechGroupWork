#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWCombatRecoveryData.h"
#include "DWCombatRecoverySubsystem.generated.h"
class ADWPlayerCharacter;
class ADWCombatDeathBag;
class ADWCombatSafePoint;
class FDWCombatRecoveryTransaction;
/** D21 native facts. Disk persistence is the D22 aggregate owner's responsibility. */
UCLASS()
class GDATTEST_API UDWCombatRecoverySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UDWCombatRecoverySubsystem* Get(const UObject* Context);
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual void Deinitialize() override;
    bool ActivateSafePoint(ADWPlayerCharacter&,const ADWCombatSafePoint&);
    bool CanRespawn(const ADWPlayerCharacter&) const;
    bool RequestRespawn(ADWPlayerCharacter&);
    bool BindNewPlayer(ADWPlayerCharacter&); // Native D22/new-session initialization, no grants or map placement.
    bool CaptureSave(FDWCombatRecoverySave& Out,FString& Error) const;
    // Fresh cold world only, before actors begin play. Never apply independently over live inventory.
    bool RestoreBeforeBeginPlay(const FDWCombatRecoverySave&,FString& Error);
    bool BindRestoredPlayer(ADWPlayerCharacter&);
    bool FindBag(FGuid Id,FDWDeathBagSave& Out) const;
    ADWCombatDeathBag* FindPhysicalBag(FGuid Id)const;
    bool HasRecoveryFacts()const{return bInitialized;}
    bool IsAwaitingRespawn()const{return bInitialized&&State.bAwaitingRespawn;}
    static FString MapPackage(const UWorld*);
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
    friend class FDWCombatWorldObjectsPersistence;
    friend class FDWCombatRecoveryTransaction;
    friend class ADWCombatDeathBag;
    FDWCombatRecoverySave State;
    TWeakObjectPtr<ADWPlayerCharacter> BoundPlayer;
    TMap<FGuid,TWeakObjectPtr<ADWCombatDeathBag>> Physical;
    bool bInitialized=false,bStopping=false,bUpdating=false,bRestoredCold=false,bPlacementPending=false;
    bool Operational()const;
    bool EnsurePlayer(ADWPlayerCharacter&);
    void Materialize(FGuid Id);
    bool IsPhysicalCurrent(const ADWCombatDeathBag&)const;
};
