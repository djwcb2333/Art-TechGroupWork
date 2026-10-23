#pragma once
#include "CoreMinimal.h"
#include "DWCombatPlayerVitals.h"
#include "DWCombatCommitLedger.h"
class FDWCombatRules;

struct FDWMoldRootConfig
{
    double StartCost=0.,SpendPerSecond=0.,SpecialGainPerSecond=0.,SpecialFloor=0.;
    double DamageMultiplier=0.,RangedRateMultiplier=0.,IncomingMultiplier=0.;
    double MoveCentimetersPerSecond=0.,EnterVisualSeconds=0.,ExitVisualSeconds=0.;
    bool IsValid() const;
    static bool Build(const FDWCombatRules& Rules,FDWMoldRootConfig& Out,FString& Error);
};
enum class EDWMoldRootFailure:uint8 {None,AlreadyRooted,InvalidConfig,WrongForm,InsufficientSpecial,InvalidGround,InvalidPosition,IdentityExhausted};
enum class EDWMoldRootEnd:uint8 {Manual,SpecialExhausted,FormExpired,Death,Restore,EndPlay};

// D22 must serialize this with the matching ring snapshot. Runtime receipt IDs are never saved/reused.
struct FDWMoldRootSnapshot {bool bActive=false;FVector Anchor=FVector::ZeroVector;};
struct FDWMoldRootIdentity
{
    DWCombatCommit::FActionId Action;
    DWCombatCommit::FReceiptId Receipt;
    DWCombatCommit::FEntityLife SourceLife;
    uint64 WorldEpoch=0,RootSerial=0;
    FVector Anchor=FVector::ZeroVector;
};

// Stance facts only. Vitals remains the sole Special clock; this class never consumes frame time.
class GDATTEST_API FDWMoldRootPlan
{
public:
    EDWMoldRootFailure CanStart(const FDWMoldRootConfig& Config,EDWCombatBaseForm Form,
        double EffectiveSpecial,bool bLegalGround,FVector Position) const;
    bool Start(const FDWMoldRootConfig& Config,EDWCombatBaseForm Form,double EffectiveSpecial,
        bool bLegalGround,FVector Position);
    void End(EDWMoldRootEnd Reason);
    bool IsActive() const {return bActive;}
    uint64 GetSerial() const {return Serial;}
    FVector GetAnchor() const {return Anchor;}
    EDWMoldRootEnd GetEndReason() const {return EndReason;}
    const FDWMoldRootConfig& GetConfig() const {return Config;}
    // A prediction only; runtime clips through Vitals.AdvanceNext and checks its exact committed floor.
    double EstimateResourceBoundarySeconds(double EffectiveSpecial) const;
    FDWMoldRootSnapshot Capture() const {return {bActive,bActive?Anchor:FVector::ZeroVector};}
    static bool ValidateSnapshot(const FDWMoldRootConfig& Config,const FDWMoldRootSnapshot& Snapshot,
        EDWCombatBaseForm Form,double EffectiveSpecial,double FormRemainingSeconds);
    // Candidate-only restore. Caller must atomically rebind a fresh current-world identity with the ring.
    bool Restore(const FDWMoldRootConfig& Config,const FDWMoldRootSnapshot& Snapshot,
        EDWCombatBaseForm Form,double EffectiveSpecial,double FormRemainingSeconds);
    static bool ValidateBinding(const FDWMoldRootPlan& Candidate,const FDWMoldRootIdentity& Identity,
        uint64 ExpectedEpoch,DWCombatCommit::FEntityLife ExpectedLife);
private:
    bool bActive=false;
    uint64 Serial=0;
    FVector Anchor=FVector::ZeroVector;
    FDWMoldRootConfig Config;
    EDWMoldRootEnd EndReason=EDWMoldRootEnd::EndPlay;
};
