#pragma once

#include "UObject/Interface.h"
#include "DWWorldPickup.generated.h"

class AActor;
class ADWPlayerCharacter;
class UStaticMeshComponent;

UINTERFACE(meta=(CannotImplementInterfaceInBlueprint))
class GDATTEST_API UDWWorldPickup : public UInterface
{
    GENERATED_BODY()
};

/** One interaction and presentation contract, with inventory ownership left to each pickup. */
class GDATTEST_API IDWWorldPickup
{
    GENERATED_BODY()
public:
    virtual UStaticMeshComponent* GetPickupMesh() const = 0;
    virtual FName GetPickupItemId() const = 0;
    virtual bool IsPickupAvailable() const = 0;
    virtual bool IsPickupInReach(const AActor* User) const = 0;
    virtual bool CanPlayPickupPresentation() const { return true; }
    virtual bool TryCollect(ADWPlayerCharacter* User) = 0;
    virtual void HandlePickupPresentationFinished() = 0;
};
