#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Tree.generated.h"

UCLASS()
class ARTTECHCOLLABORATION_API ATree : public AActor
{
    GENERATED_BODY()

public:

    ATree();

    UFUNCTION(BlueprintCallable, Category = "Tree")
    void Chop();
};