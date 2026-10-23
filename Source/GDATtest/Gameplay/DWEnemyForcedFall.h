#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
#include "DWEnemyForcedFall.generated.h"

UENUM()
enum class EDWForcedFallStage:uint8 { None, Armed, Falling };

/** D22 DTO: historical receipt/source identity is evidence, never a replayable current-world action. */
USTRUCT()
struct FDWEnemyForcedFallSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=1;
    UPROPERTY(SaveGame) FGuid StableEnemyId;
    UPROPERTY(SaveGame) EDWForcedFallStage Stage=EDWForcedFallStage::None;
    UPROPERTY(SaveGame) uint64 ReceiptEpoch=0;
    UPROPERTY(SaveGame) uint64 ReceiptId=0;
    UPROPERTY(SaveGame) uint64 PlayerEntity=0;
    UPROPERTY(SaveGame) uint64 PlayerLife=0;
    UPROPERTY(SaveGame) uint64 OriginalEnemyEntity=0;
    UPROPERTY(SaveGame) uint64 OriginalEnemyLife=0;
    UPROPERTY(SaveGame) bool bHasStandablePoint=false;
    UPROPERTY(SaveGame) FVector LastStandablePoint=FVector::ZeroVector;
};

/** Native facts only. No damage, clocks, traces, actor access or callbacks. */
struct FDWEnemyForcedFall
{
    FDWEnemyForcedFallSave Saved;
    uint64 CurrentEpoch=0,KnockbackRevision=0;
    DWCombatCommit::FEntityLife CurrentEnemy;
    bool bMoving=false,bMoveStartedSupported=false;
    FVector MoveStart=FVector::ZeroVector;

    void Clear();
    void Accept(DWCombatCommit::FReceiptId Receipt,DWCombatCommit::FEntityLife Player,
        DWCombatCommit::FEntityLife Enemy,FGuid StableEnemy,uint64 Epoch,uint64 Revision);
    bool IsCurrent(uint64 Epoch,DWCombatCommit::FEntityLife Enemy,uint64 Revision)const;
    void BeginMove(bool Supported,FVector Position,FVector StandablePoint);
    void CompleteMove(bool Moved,bool Supported,FVector LastStandablePoint);
    void FinishDriver();
    bool HasCredit()const{return Saved.Stage==EDWForcedFallStage::Falling&&Saved.bHasStandablePoint;}
    static bool Validate(const FDWEnemyForcedFallSave& In);
    bool Restore(const FDWEnemyForcedFallSave& In,FGuid StableEnemy,uint64 Epoch,
        DWCombatCommit::FEntityLife Enemy,uint64 Revision,bool DriverRestored);
};
