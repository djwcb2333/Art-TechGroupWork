#include "PlayerCharacter.h"

#include "Tree.h"
#include "Components/SphereComponent.h"

#include "EnhancedInputComponent.h"
#include "InputAction.h"


// =========================================================
// Constructor
// =========================================================

APlayerCharacter::APlayerCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    ChopTime = 3.0f;

    ChopProgress = 0.0f;

    bIsChopping = false;

    CurrentTargetTree = nullptr;

    NearestTree = nullptr;

    Wood = 0;

    /*
     * InteractionRange 不在 C++ 中创建。
     *
     * 它应该已经存在于 BP_Player 中。
     */
}


// =========================================================
// BeginPlay
// =========================================================

void APlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    // =====================================================
    // Find InteractionRange
    // =====================================================

    TArray<USphereComponent*> SphereComponents;

    GetComponents<USphereComponent>(SphereComponents);

    for (USphereComponent* Sphere : SphereComponents)
    {
        if (!IsValid(Sphere))
        {
            continue;
        }

        // 通过 Component Tag 找到 InteractionRange
        if (Sphere->ComponentTags.Contains(
            FName(TEXT("InteractionRange"))))
        {
            InteractionRange = Sphere;
            break;
        }
    }


    // =====================================================
    // Check
    // =====================================================

    if (!InteractionRange)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("APlayerCharacter: InteractionRange was not found!")
        );

        return;
    }


    // =====================================================
    // Bind Overlap Events
    // =====================================================

    InteractionRange->OnComponentBeginOverlap.AddDynamic(
        this,
        &APlayerCharacter::OnInteractionBeginOverlap
    );

    InteractionRange->OnComponentEndOverlap.AddDynamic(
        this,
        &APlayerCharacter::OnInteractionEndOverlap
    );


    UE_LOG(
        LogTemp,
        Log,
        TEXT("APlayerCharacter: InteractionRange initialized.")
    );
}


// =========================================================
// Tick
// =========================================================

void APlayerCharacter::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    FindNearestTree();

    UpdateChop(DeltaTime);
}


// =========================================================
// Setup Input
// =========================================================

void APlayerCharacter::SetupPlayerInputComponent(
    UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);


    UEnhancedInputComponent* EnhancedInputComponent =
        Cast<UEnhancedInputComponent>(PlayerInputComponent);

    if (!EnhancedInputComponent)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("APlayerCharacter: EnhancedInputComponent not found.")
        );

        return;
    }


    if (!ChopAction)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("APlayerCharacter: ChopAction is not assigned.")
        );

        return;
    }


    // =====================================================
    // Start
    // =====================================================

    EnhancedInputComponent->BindAction(
        ChopAction,
        ETriggerEvent::Started,
        this,
        &APlayerCharacter::StartChop
    );


    // =====================================================
    // Release
    // =====================================================

    EnhancedInputComponent->BindAction(
        ChopAction,
        ETriggerEvent::Completed,
        this,
        &APlayerCharacter::StopChop
    );


    // =====================================================
    // Cancel
    // =====================================================

    EnhancedInputComponent->BindAction(
        ChopAction,
        ETriggerEvent::Canceled,
        this,
        &APlayerCharacter::StopChop
    );
}


// =========================================================
// Tree Begin Overlap
// =========================================================

void APlayerCharacter::OnInteractionBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    ATree* Tree = Cast<ATree>(OtherActor);

    if (!IsValid(Tree))
    {
        return;
    }


    /*
     * 只保存 InteractionRange 内的 Tree。
     *
     * AddUnique 可以避免同一棵树重复添加。
     */
    NearbyTrees.AddUnique(Tree);


    /*
     * Tree 数量变化后立即重新计算最近树。
     */
    FindNearestTree();
}


// =========================================================
// Tree End Overlap
// =========================================================

void APlayerCharacter::OnInteractionEndOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex)
{
    ATree* Tree = Cast<ATree>(OtherActor);

    if (!IsValid(Tree))
    {
        return;
    }


    /*
     * Tree 离开范围。
     */
    NearbyTrees.Remove(Tree);


    /*
     * 重新寻找最近树。
     */
    FindNearestTree();


    /*
     * 如果离开范围的正好是当前正在砍的 Tree，
     * 取消砍树。
     */
    if (Tree == CurrentTargetTree)
    {
        StopChop();
    }
}


// =========================================================
// Find Nearest Tree
// =========================================================

ATree* APlayerCharacter::FindNearestTree()
{
    NearestTree = nullptr;


    if (NearbyTrees.Num() == 0)
    {
        return nullptr;
    }


    /*
     * 直接使用 InteractionRange 的实际半径。
     *
     * 不再存在一个独立的 ChopRange。
     */
    const float InteractionRadius =
        InteractionRange
        ? InteractionRange->GetScaledSphereRadius()
        : 0.0f;


    const float MaxDistanceSquared =
        FMath::Square(InteractionRadius);


    float BestDistanceSquared =
        MaxDistanceSquared;


    /*
     * 遍历 NearbyTrees。
     */
    for (int32 Index = NearbyTrees.Num() - 1;
        Index >= 0;
        --Index)
    {
        ATree* Tree = NearbyTrees[Index];


        /*
         * 清理已经失效的 Tree。
         */
        if (!IsValid(Tree))
        {
            NearbyTrees.RemoveAt(Index);
            continue;
        }


        const float DistanceSquared =
            FVector::DistSquared(
                GetActorLocation(),
                Tree->GetActorLocation()
            );


        /*
         * 找到最近 Tree。
         */
        if (DistanceSquared < BestDistanceSquared)
        {
            BestDistanceSquared = DistanceSquared;
            NearestTree = Tree;
        }
    }


    return NearestTree;
}


// =========================================================
// Start Chop
// =========================================================

void APlayerCharacter::StartChop()
{
    /*
     * 已经在砍树，不重复开始。
     */
    if (bIsChopping)
    {
        return;
    }


    /*
     * 按下交互键时重新确认最近 Tree。
     */
    CurrentTargetTree = FindNearestTree();


    /*
     * 范围内没有 Tree。
     */
    if (!IsValid(CurrentTargetTree))
    {
        return;
    }


    /*
     * 开始砍树。
     */
    bIsChopping = true;


    /*
     * 每次新的砍树从 0 开始。
     */
    ChopProgress = 0.0f;
}


// =========================================================
// Stop Chop
// =========================================================

void APlayerCharacter::StopChop()
{
    /*
     * 没有正在砍树。
     */
    if (!bIsChopping)
    {
        return;
    }


    /*
     * 停止。
     */
    bIsChopping = false;


    /*
     * AC07：
     *
     * 提前松开交互键，
     * Harvest Progress 必须归零。
     */
    ChopProgress = 0.0f;


    /*
     * 清除当前目标。
     */
    CurrentTargetTree = nullptr;
}


// =========================================================
// Update Chop
// =========================================================

void APlayerCharacter::UpdateChop(float DeltaTime)
{
    /*
     * 当前没有砍树。
     */
    if (!bIsChopping)
    {
        return;
    }


    /*
     * 当前 Tree 已经无效。
     */
    if (!IsValid(CurrentTargetTree))
    {
        StopChop();
        return;
    }


    // =====================================================
    // Check Range
    // =====================================================

    /*
     * 使用 InteractionRange 的实际半径。
     */
    const float InteractionRadius =
        InteractionRange
        ? InteractionRange->GetScaledSphereRadius()
        : 0.0f;


    const float DistanceSquared =
        FVector::DistSquared(
            GetActorLocation(),
            CurrentTargetTree->GetActorLocation()
        );


    /*
     * 玩家在砍树过程中离开交互范围。
     */
    if (DistanceSquared >
        FMath::Square(InteractionRadius))
    {
        StopChop();
        return;
    }


    // =====================================================
    // Update Progress
    // =====================================================

    if (ChopTime <= 0.0f)
    {
        ChopProgress = 1.0f;
    }
    else
    {
        /*
         * 进度为 0~1。
         */
        ChopProgress +=
            DeltaTime / ChopTime;
    }


    ChopProgress =
        FMath::Clamp(
            ChopProgress,
            0.0f,
            1.0f
        );


    // =====================================================
    // Complete
    // =====================================================

    if (ChopProgress >= 1.0f)
    {
        /*
         * 保存目标。
         */
        ATree* HarvestedTree =
            CurrentTargetTree;


        /*
         * AC09：
         *
         * Wood +3
         */
        Wood += 3;


        /*
         * 砍树。
         */
        if (IsValid(HarvestedTree))
        {
            HarvestedTree->Chop();
        }


        /*
         * 完成。
         */
        bIsChopping = false;


        /*
         * 完成时保持 100%。
         *
         * 后续 UI 可以显示完整进度。
         */
        ChopProgress = 1.0f;


        /*
         * 清除当前目标。
         */
        CurrentTargetTree = nullptr;


        /*
         * 从 NearbyTrees 中删除。
         */
        NearbyTrees.Remove(HarvestedTree);


        /*
         * 重新寻找最近的 Tree。
         */
        FindNearestTree();
    }
}