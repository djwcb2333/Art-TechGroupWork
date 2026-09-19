#pragma once

#include "CoreMinimal.h"
#include "InputAction.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Character.h"
#include "PlayerCharacter.generated.h"


class ATree;
class USphereComponent;
class UInputAction;

UCLASS()
class ARTTECHCOLLABORATION_API APlayerCharacter : public ACharacter
{
    GENERATED_BODY()

public:

    APlayerCharacter();

protected:

    virtual void BeginPlay() override;

public:

    virtual void Tick(float DeltaTime) override;

    virtual void SetupPlayerInputComponent(
        class UInputComponent* PlayerInputComponent
    ) override;


    // =========================================================
    // Interaction Range
    // =========================================================

protected:

    /**
     * 玩家现有的 InteractionRange。
     *
     * 这个组件在 BP_Player 中创建。
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Interaction"
    )
    TObjectPtr<USphereComponent> InteractionRange;


    // =========================================================
    // Nearby Trees
    // =========================================================

protected:

    /**
     * 当前 InteractionRange 范围内的 Tree。
     */
    UPROPERTY(
        VisibleInstanceOnly,
        BlueprintReadOnly,
        Category = "Tree Interaction"
    )
    TArray<TObjectPtr<ATree>> NearbyTrees;


    /**
     * 当前距离玩家最近的 Tree。
     */
    UPROPERTY(
        BlueprintReadOnly,
        Category = "Tree Interaction"
    )
    TObjectPtr<ATree> NearestTree;


    /**
     * Tree 进入 InteractionRange。
     */
    UFUNCTION()
    void OnInteractionBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult
    );


    /**
     * Tree 离开 InteractionRange。
     */
    UFUNCTION()
    void OnInteractionEndOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex
    );


    /**
     * 从 NearbyTrees 中寻找距离玩家最近的 Tree。
     */
    UFUNCTION(BlueprintCallable, Category = "Tree Interaction")
    ATree* FindNearestTree();


    // =========================================================
    // Chop
    // =========================================================

protected:

    /**
     * 完成一次砍树需要持续按住交互键的时间。
     * 策划可以在 BP_Player 中修改。
     */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "Tree Interaction",
        meta = (ClampMin = "0.01")
    )
    float ChopTime = 3.0f;


    /**
     * 当前砍树进度。
     * 0-1
     */
    UPROPERTY(
        BlueprintReadOnly,
        Category = "Tree Interaction",
        meta = (ClampMin = "0.0", ClampMax = "1.0")
    )
    float ChopProgress = 0.0f;


    /**
     * 当前是否正在砍树。
     */
    UPROPERTY(
        BlueprintReadOnly,
        Category = "Tree Interaction"
    )
    bool bIsChopping = false;


    /**
     * 当前正在砍的 Tree。
     */
    UPROPERTY(
        BlueprintReadOnly,
        Category = "Tree Interaction"
    )
    TObjectPtr<ATree> CurrentTargetTree;


    /**
     * 玩家当前 Wood 数量。
     */
    UPROPERTY(
        BlueprintReadOnly,
        Category = "Inventory"
    )
    int32 Wood = 0;


    /**
     * 开始砍树。
     */
    UFUNCTION(BlueprintCallable, Category = "Tree Interaction")
    void StartChop();


    /**
     * 停止砍树。
     */
    UFUNCTION(BlueprintCallable, Category = "Tree Interaction")
    void StopChop();


    /**
     * 每帧更新砍树进度。
     */
    void UpdateChop(float DeltaTime);

protected:

    /**
     * 砍树 Input Action。
     */
    UPROPERTY(
        EditDefaultsOnly,
        BlueprintReadOnly,
        Category = "Input"
    )
    TObjectPtr<UInputAction> ChopAction;
};