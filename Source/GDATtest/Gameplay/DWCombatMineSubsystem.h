#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWCombatMines.h"
#include "DWCombatMoldRoot.h"
#include "DWCombatMineSubsystem.generated.h"

class ADWPlayerCharacter;
class ADWEnemyCharacter;
class ADWCombatMine;
class UDWCombatAuthoritySubsystem;
struct FDWCombatMineTarget
{
    TWeakObjectPtr<ADWEnemyCharacter> Target;
    DWCombatCommit::FEntityLife Life;
    FGuid FormalEnemyId;
    FName LegacyEnemyId;
};
struct FDWCombatMineExplosion
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    FDWMoldRootIdentity Root;
    FGuid RingId,MineId;
    DWCombatCommit::FEmitterId Emitter;
    FVector Center=FVector::ZeroVector;
    FDWMoldMineConfig Config;
    TArray<FDWCombatMineTarget> Targets;
};

/** Ring/cooldown facts only; health and receipts stay in the existing authority. */
UCLASS()
class GDATTEST_API UDWCombatMineSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UDWCombatMineSubsystem* Get(const UObject* Context);
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    void OnRootCommitted(ADWPlayerCharacter* Player,const FDWMoldRootIdentity& Root);
    // Native facts only: may be called inside a lethal/resource commit before any external notification.
    void EndRootFacts(ADWPlayerCharacter* Player,uint64 RootSerial);
    // 0 flushes all retired rings belonging to this player. No live ring is destroyed.
    void FlushRootCleanup(ADWPlayerCharacter* Player,uint64 RootSerial=0);
    double GetCooldownRemaining(const ADWPlayerCharacter* Player) const;
    int32 GetLiveMineCount(const ADWPlayerCharacter* Player) const;
    bool CaptureSave(const ADWPlayerCharacter* Player,FDWMoldMineRingSave& Out,FString& Error) const;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
    friend class ADWCombatMine;
    friend class UDWCombatAuthoritySubsystem;
    friend class FDWCombatMinePersistence;
    friend struct FDWCombatMineRestoreState;
    struct FMine
    {
        FDWMoldMineSave Save;
        DWCombatCommit::FEmitterId Emitter;
        TWeakObjectPtr<ADWCombatMine> Actor;
        bool bReleased=false;
    };
    struct FRing
    {
        FGuid Id;
        TWeakObjectPtr<ADWPlayerCharacter> Player;
        FDWMoldRootIdentity Root;
        FDWMoldMineConfig Config;
        TArray<FMine> Mines;
        TSet<uint64> HitEntities;
        TSet<FGuid> HitFormalEnemies;
        TSet<FName> HitLegacyEnemies;
        bool bActive=true,bUnsaveableHit=false;
    };
    struct FPlayerState
    {
        double CooldownDeadline=0.;
        DWCombatCommit::FReceiptId LastAttempt;
        FGuid ActiveRing;
    };
    TMap<TWeakObjectPtr<ADWPlayerCharacter>,FPlayerState> Players;
    TMap<TWeakObjectPtr<ADWPlayerCharacter>,FDWMoldRootIdentity> PendingEntries;
    TMap<FGuid,FRing> Rings;
    bool bStopping=false,bUpdating=false,bResolving=false,bDeploying=false;
    bool Operational() const;
    void RetireEmitter(DWCombatCommit::FEmitterId Emitter);
    bool RootCurrent(const FRing& Ring) const;
    FMine* CurrentMine(ADWCombatMine& Mine,FRing*& OutRing);
    bool TryGround(ADWPlayerCharacter& Player,const FDWMoldRootIdentity& Root,
        const FDWMoldMineConfig& Config,int32 Ordinal,FVector& Out) const;
    bool HasSight(ADWPlayerCharacter& Player,ADWEnemyCharacter& Enemy,FVector Center,float Radius) const;
    bool TryTrigger(ADWCombatMine& Mine);
    bool PrepareExplosion(ADWCombatMine& Mine,FDWCombatMineExplosion& Out);
    bool ExplosionCurrent(const FDWCombatMineExplosion& Explosion) const;
    void CommitExplosionFacts(const FDWCombatMineExplosion& Explosion,const TArray<uint64>& AdmittedEntities);
    void PhysicalBegan(ADWCombatMine& Mine);
    void PhysicalEnded(ADWCombatMine& Mine);
};
