#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatShieldAxe.h"
#include "DWCombatAuthoritySubsystem.generated.h"

struct FDWCombatObjectBinding;
struct FDWCombatAuthoritySave;
class FDWCombatAuthorityRestorePlan;
class FDWCombatShieldRestorePlan;
struct FDWShieldPacketSave;
struct FDWCombatEffectsSave;
struct FDamageEvent;
class UDamageType;
struct FDWCombatAuthorityState;
struct FDWCombatAuthorityStateDeleter
{
    void operator()(FDWCombatAuthorityState* Value) const;
};
struct FDWCombatRecoveryNotice;
struct FDWCombatWeaponResourceNotice;
struct FDWCombatPlayerStateNotice;
struct FDWCombatCorePickupNotice;
struct FDWCombatCoreInjectionNotice;
struct FDWCombatMoldRootNotice;
class ADWCombatBowProjectile;
struct FDWGroundAreaTick;
class ADWCombatSporeProjectile;
class ADWCombatBottleProjectile;
class ADWPlayerCharacter;
class ADWEnemyCharacter;
class UDWWeaponAnimationComponent;
enum class EDWSpearForm:uint8;
class ADWCombatMine;
class UDWCombatMineSubsystem;
class FDWCombatInventoryTransaction;
class AActor;
class AController;

// Native-only envelope: the collision/query owner supplies an explicit hit identity.
// Player melee/dash -> Enemy/Nest and explicit EnemyMelee -> Player; periodic sources use the typed pair.
struct FDWCombatDamageCommand
{
    DWCombatCommit::FActionDamage Identity;
    DWCombatCommit::FEntityLife TargetLife;
    TWeakObjectPtr<AActor> Source;
    TWeakObjectPtr<AActor> Target;
    TWeakObjectPtr<AController> Instigator;
    float Amount = 0.f;
    EDWShieldIncomingKind ShieldIncomingKind=EDWShieldIncomingKind::Unblockable;
    FVector ShieldIncomingDirection=FVector::ZeroVector; // From victim toward incoming source; typed adapters only.
    FVector KnockbackDirection = FVector::ZeroVector;
    float KnockbackDistance = 0.f;
    float KnockbackSeconds = .15f;
};

struct FDWCombatVitalsDamagePair
{
    TWeakObjectPtr<ADWPlayerCharacter> Target;
    uint64 WorldEpoch=0;
    DWCombatCommit::FEntityLife SourceLife;
    uint64 DehydrationOrdinal=0,StarvationOrdinal=0;
    float DehydrationAmount=0.f,StarvationAmount=0.f;
};

// Only a current continuous forced fall credits the first two reasons. Other hazards never inherit it.
enum class EDWEnemyEnvironmentFatal:uint8 { FellOutOfWorld, FallImpact, OtherEnvironment };

struct FDWCombatDamageResult
{
    DWCombatCommit::EAdmission Admission = DWCombatCommit::EAdmission::Invalid;
    DWCombatCommit::FReceipt Receipt;
    // Zero for duplicate/rejected/pending requests. Never use an old result to replay effects.
    float AppliedThisCall = 0.f;
};

/** Sole native damage/resource authority. No Blueprint mutation API. */
UCLASS()
class GDATTEST_API UDWCombatAuthoritySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    UDWCombatAuthoritySubsystem();
    virtual ~UDWCombatAuthoritySubsystem() override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    static UDWCombatAuthoritySubsystem* Get(const UObject* Context);

    DWCombatCommit::FEntityLife CaptureActorLife(AActor* Actor);
    DWCombatCommit::FActionId BeginAction(AActor* Owner);
    void EndAction(DWCombatCommit::FActionId Action);
    // Admission owns the immutable hit snapshot. Source exit cannot revoke it.
    FDWCombatDamageResult AdmitActionDamage(const FDWCombatDamageCommand& Command);
    FDWCombatDamageResult ResolvePendingDamage(DWCombatCommit::FReceiptId Receipt, DWCombatCommit::EOutcome Outcome);
    FDWCombatDamageResult SubmitActionDamage(const FDWCombatDamageCommand& Command);
    // Every independent old call receives a distinct LegacyExternal identity.
    // Same-frame/value/causer guessing is deliberately not used to identify duplicates.
    float SubmitLegacyDamage(AActor* Target, float Amount, const FDamageEvent& Event,
        AController* Instigator, AActor* Causer);

    // Fixed two-channel batch: all same-instant health facts precede the first notification.
    void SubmitVitalsDamagePair(const FDWCombatVitalsDamagePair& Pair);
    FDWCombatDamageResult SubmitEnemyEnvironmentFatal(ADWEnemyCharacter&,EDWEnemyEnvironmentFatal,const UDamageType&);
    bool UseLegacyHealingItem(ADWPlayerCharacter* Player,int32 SlotIndex);
    bool CommitCoreInjection(ADWPlayerCharacter* Player);
    bool CommitFormalRootEntry(ADWPlayerCharacter* Player);
    void PublishRestoredPlayerState(const FDWCombatPlayerStateNotice& Notice);
    void NotifyActorDeath(AActor* Actor);
    void NotifyActorRestored(AActor* Actor);
    uint64 GetWorldEpoch() const;
    DWCombatCommit::FEntityLife FindRegisteredActorLife(const AActor*)const;
    bool CaptureWorldIdentity(const TArray<FDWCombatObjectBinding>&,FDWCombatAuthoritySave&,FString& Error);
    bool PrepareWorldIdentityRestore(const FDWCombatAuthoritySave&,const TArray<FDWCombatObjectBinding>&,const TArray<uint64>& KeepActions,FDWCombatAuthorityRestorePlan&,FString& Error);
    bool CanCommitWorldIdentity(const FDWCombatAuthorityRestorePlan&,FString& Error)const;
    bool BeginWorldRestoreBarrier(FDWCombatAuthorityRestorePlan&,FString& Error);
    void AbortWorldRestoreBarrier();
    void CommitWorldIdentityUnchecked(FDWCombatAuthorityRestorePlan&);
    void FinishWorldRestoreBarrier();
    bool CaptureShieldPackets(const ADWPlayerCharacter&,const TArray<FDWCombatObjectBinding>&,TArray<FDWShieldPacketSave>&,FString& Error)const;
    bool PrepareShieldRestore(const TArray<FDWShieldPacketSave>&,const FDWCombatEffectsSave&,const FDWCombatAuthorityRestorePlan&,FDWCombatShieldRestorePlan&,FString& Error);
    void AdoptShieldFactsUnchecked(FDWCombatShieldRestorePlan&);
    int32 GetPendingDamageCount() const;
    double GetNextShieldDamageBoundary(const ADWPlayerCharacter*,double SliceStart)const;
    uint64 GetShieldDamageRevision()const;
    int32 GetPendingShieldDamageCount(const ADWPlayerCharacter*)const;
    void ResolveShieldDamageAt(ADWPlayerCharacter*,double WorldGameSeconds);
    void ParryEligibleShieldDamage(ADWPlayerCharacter*,double WorldGameSeconds);
    std::optional<DWCombatCommit::FReceipt> FindReceipt(DWCombatCommit::FReceiptId Receipt) const;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
    friend class UDWCombatMineSubsystem;
    friend class FDWCombatEffectsPersistence;
    friend class UDWWeaponAnimationComponent;
    bool CommitFormalShotgun(UDWWeaponAnimationComponent&,EDWSpearForm);
    FDWCombatDamageResult SubmitDaggerBackstab(UDWWeaponAnimationComponent&,ADWEnemyCharacter&,DWCombatCommit::FActionId);
    friend class FDWCombatBowShotTransaction;
    friend class ADWCombatBowProjectile;
    friend class UDWCombatGroundAreaSubsystem;
    void SubmitBowImpact(ADWCombatBowProjectile&,const FHitResult&,double ImpactSeconds);
    void RetireBowProjectile(const ADWCombatBowProjectile&);
    void SubmitGroundAreaTickBatch(const TArray<FDWGroundAreaTick>&);
    friend class FDWCombatSporeShotTransaction;
    friend class FDWCombatBottleTransaction;
    friend class ADWCombatBottleProjectile;
    void RetireBottleProjectile(const ADWCombatBottleProjectile&);
    friend class ADWCombatSporeProjectile;
    FDWCombatDamageResult SubmitSporeImpact(ADWCombatSporeProjectile& Bullet,const FHitResult& Hit);
    void RetireSporeProjectile(const ADWCombatSporeProjectile& Bullet);
    friend class FDWCombatInventoryTransaction;
    friend class FDWCombatRecoveryTransaction;
    bool ReserveRecoveryNoticeSlot();
    void EnqueueCommittedRecovery(const FDWCombatRecoveryNotice&);
    friend class FDWCombatPlayerStateTransaction;
    friend class FDWCombatDashTransaction;
    friend class FDWCombatEquipmentTransaction;
    friend class FDWCombatCorePickupTransaction;
    friend class FDWCombatCoreInjectionTransaction;
    friend class FDWCombatMoldRootTransaction;
    TUniquePtr<FDWCombatAuthorityState, FDWCombatAuthorityStateDeleter> State;
    bool IsActionOwnedBy(DWCombatCommit::FActionId Action, AActor* Owner) const;
    DWCombatCommit::FLedger* GetLedgerForResourceTransaction();
    bool ReserveWeaponResourceNoticeSlot();
    bool ReservePlayerStateNoticeSlot();
    bool ReserveCorePickupNoticeSlot();
    bool ReserveCoreInjectionNoticeSlot();
    bool ReserveMoldRootNoticeSlot();
    void EnqueueCommittedMoldRoot(const FDWCombatMoldRootNotice& Notice);
    void EnqueueCommittedCoreInjection(const FDWCombatCoreInjectionNotice& Notice);
    void EnqueueCommittedCorePickup(const FDWCombatCorePickupNotice& Notice);
    bool CanRestorePlayerState(AActor* Actor) const;
    void EnqueueCommittedPlayerState(const FDWCombatPlayerStateNotice& Notice);
    void EnqueueCommittedWeaponResource(const FDWCombatWeaponResourceNotice& Notice);
    bool IsOperational() const;
    UFUNCTION() void HandleActorEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
    void InvalidateActor(AActor* Actor, bool bRestore, bool bEnded);
    void PruneFinalDamageSnapshots();
    bool SubmitRootMineExplosion(ADWCombatMine& Mine);
    FDWCombatDamageResult AdmitPrepared(const FDWCombatDamageCommand& Command, const FDamageEvent& Event, bool bLegacy,
        const DWCombatCommit::FRootMineDamage* MineIdentity=nullptr,DWCombatCommit::FEntityLife ReleasedSourceLife={},const DWCombatCommit::FEmitterDamage* EmitterIdentity=nullptr,bool bDaggerBackstab=false);
    FDWCombatDamageResult CommitPending(DWCombatCommit::FReceiptId Receipt, DWCombatCommit::EOutcome Outcome);
    void DrainNotifications();
    bool IsNoticeRecipientCurrent(AActor* Actor, uint64 Revision) const;
};
