#include "DWEnemyForcedFall.h"

void FDWEnemyForcedFall::Clear(){*this={};}
bool FDWEnemyForcedFall::Validate(const FDWEnemyForcedFallSave& S)
{
    if(S.Schema!=1||S.Stage>EDWForcedFallStage::Falling||S.LastStandablePoint.ContainsNaN())return false;
    if(S.Stage==EDWForcedFallStage::None)
        return !S.ReceiptEpoch&&!S.ReceiptId&&!S.PlayerEntity&&!S.PlayerLife&&!S.OriginalEnemyEntity&&
            !S.OriginalEnemyLife&&!S.bHasStandablePoint&&S.LastStandablePoint.IsZero();
    return S.StableEnemyId.IsValid()&&S.ReceiptEpoch&&S.ReceiptId&&S.PlayerEntity&&S.PlayerLife&&
        (S.bHasStandablePoint||S.LastStandablePoint.IsZero())&&
        S.OriginalEnemyEntity&&S.OriginalEnemyLife&&(S.Stage!=EDWForcedFallStage::Falling||S.bHasStandablePoint);
}
void FDWEnemyForcedFall::Accept(DWCombatCommit::FReceiptId R,DWCombatCommit::FEntityLife P,
    DWCombatCommit::FEntityLife E,FGuid Stable,uint64 Epoch,uint64 Revision)
{
    Clear(); // Every accepted replacement, including an unattributed push, supersedes the old cause.
    if(!Epoch||!R.Id||R.Epoch!=Epoch||!P.Entity||!P.Life||!E.Entity||!E.Life||!Stable.IsValid()||!Revision)return;
    Saved.StableEnemyId=Stable;Saved.Stage=EDWForcedFallStage::Armed;
    Saved.ReceiptEpoch=R.Epoch;Saved.ReceiptId=R.Id;Saved.PlayerEntity=P.Entity;Saved.PlayerLife=P.Life;
    Saved.OriginalEnemyEntity=E.Entity;Saved.OriginalEnemyLife=E.Life;
    CurrentEpoch=Epoch;CurrentEnemy=E;KnockbackRevision=Revision;
}
bool FDWEnemyForcedFall::IsCurrent(uint64 Epoch,DWCombatCommit::FEntityLife E,uint64 Revision)const
{
    return Saved.Stage!=EDWForcedFallStage::None&&CurrentEpoch==Epoch&&CurrentEnemy.Entity==E.Entity&&
        CurrentEnemy.Life==E.Life&&KnockbackRevision==Revision;
}
void FDWEnemyForcedFall::BeginMove(bool Supported,FVector Position,FVector Point)
{
    if(Saved.Stage==EDWForcedFallStage::None||Position.ContainsNaN()||Point.ContainsNaN())return;
    if(HasCredit()&&Supported){Clear();return;} // A completed landing ends this continuous fall.
    bMoving=true;bMoveStartedSupported=Supported;MoveStart=Position;
    if(Supported){Saved.bHasStandablePoint=true;Saved.LastStandablePoint=Point;}
}
void FDWEnemyForcedFall::CompleteMove(bool Moved,bool Supported,FVector Point)
{
    if(!bMoving)return;
    if(Point.ContainsNaN()){Clear();return;}
    if(Supported){Saved.bHasStandablePoint=true;Saved.LastStandablePoint=Point;}
    else if(Moved&&bMoveStartedSupported&&Saved.bHasStandablePoint)
    {Saved.LastStandablePoint=Point;Saved.Stage=EDWForcedFallStage::Falling;}
    bMoving=false;bMoveStartedSupported=false;
}
void FDWEnemyForcedFall::FinishDriver()
{if(Saved.Stage!=EDWForcedFallStage::Falling)Clear();else{bMoving=false;bMoveStartedSupported=false;}}
bool FDWEnemyForcedFall::Restore(const FDWEnemyForcedFallSave& In,FGuid Stable,uint64 Epoch,
    DWCombatCommit::FEntityLife E,uint64 Revision,bool DriverRestored)
{
    if(!Validate(In)||!Epoch||!E.Entity||!E.Life||!Revision||
        (In.Stage!=EDWForcedFallStage::None&&In.StableEnemyId!=Stable)||
        (In.Stage==EDWForcedFallStage::Armed&&!DriverRestored))return false;
    FDWEnemyForcedFall Candidate;Candidate.Saved=In;Candidate.CurrentEpoch=Epoch;
    Candidate.CurrentEnemy=E;Candidate.KnockbackRevision=Revision;*this=Candidate;return true;
}
