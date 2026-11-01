#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DWCreatureDefinition.h"
#include "DWCreatureInterface.generated.h"
class UActorComponent;
/** Blueprint facts/intent contract. Does not grant combat ledger or save participation. */
UINTERFACE(BlueprintType,Blueprintable)
class GDATTEST_API UDWCreatureInterface : public UInterface { GENERATED_BODY() };
class GDATTEST_API IDWCreatureInterface
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintNativeEvent,BlueprintCallable,Category="Creature") FDWCreatureSnapshot ReadCreatureFacts() const;
};
/** Optional locomotion capability. Stationary creatures only implement the facts interface above. */
UINTERFACE(BlueprintType,Blueprintable)
class GDATTEST_API UDWCreatureAmbientPatrolHost : public UInterface { GENERATED_BODY() };
class GDATTEST_API IDWCreatureAmbientPatrolHost
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintNativeEvent,BlueprintCallable,Category="Creature|Patrol") bool CanCreatureAmbientPatrol(UActorComponent* Requester) const;
 UFUNCTION(BlueprintNativeEvent,BlueprintCallable,Category="Creature|Patrol") FDWAmbientPatrolState ReadCreatureAmbientPatrol() const;
 UFUNCTION(BlueprintNativeEvent,BlueprintCallable,Category="Creature|Patrol") bool RequestCreatureAmbientGoal(UActorComponent* Requester,FVector Goal,float WaitSeconds);
 UFUNCTION(BlueprintNativeEvent,BlueprintCallable,Category="Creature|Patrol") void StopCreatureAmbientPatrol(UActorComponent* Requester);
};
