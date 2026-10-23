#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerPersistence.h"
#include "DWCombatWorldPersistence.h"
#include "DWCombatEffectsPersistence.h"
#include <algorithm>
#include "DWCombatRecoveryTransaction.h"
#include "DWCombatBowProjectile.h"
#include "DWCombatGroundAreaSubsystem.h"
#include "EngineUtils.h"
#include "DWCombatSporeProjectile.h"
#include "DWCombatBottleProjectile.h"
#include "DWDamageVolumeLibrary.h"
#include <limits>
#include "DWCombatMineSubsystem.h"
#include "DWCombatMine.h"
#include "DWCombatInventoryTransaction.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatCorePickupTransaction.h"
#include "DWCombatCoreInjectionTransaction.h"
#include "DWCombatMoldRootTransaction.h"
#include "DWCombatPlayerStateTransaction.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWPlayerCharacter.h"
#include "DWCombatFeedbackComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "UObject/StrongObjectPtr.h"

namespace DWCombatAuthority
{
using namespace DWCombatCommit;
// Game-thread only; unlike a per-world counter this separates concurrent PIE worlds.
static uint64 LastWorldEpoch = 0;
static bool Next(uint64& Value) { if (Value == MAX_uint64) return false; ++Value; return true; }
static bool Same(FEntityLife A, FEntityLife B) { return A.Entity == B.Entity && A.Life == B.Life; }
static bool NativeTargetAlive(AActor* Actor)
{
    if (const auto* Player = Cast<ADWPlayerCharacter>(Actor)) return Player->HasCombatVitalsReady() && !Player->IsDead();
    if (const auto* Enemy = Cast<ADWEnemyCharacter>(Actor)) return Enemy->IsAlive();
    if (const auto* Nest = Cast<ADWEnemyNest>(Actor)) return !Nest->bAmbientSpawner && Nest->IsAlive();
    return false;
}
struct FEventSnapshot
{
    FDamageEvent Generic;
    FPointDamageEvent Point;
    FRadialDamageEvent Radial;
    TStrongObjectPtr<UClass> TypeClass;
    int32 Kind = FDamageEvent::ClassID;
    bool Capture(const FDamageEvent& Event)
    {
        Kind = Event.GetTypeID();
        if (Kind == FDamageEvent::ClassID) Generic = Event;
        else if (Kind == FPointDamageEvent::ClassID) Point = static_cast<const FPointDamageEvent&>(Event);
        else if (Kind == FRadialDamageEvent::ClassID) Radial = static_cast<const FRadialDamageEvent&>(Event);
        else return false; // A custom event must supply its own explicit typed adapter.
        TypeClass.Reset(Event.DamageTypeClass ? Event.DamageTypeClass.Get() : UDamageType::StaticClass());
        return true;
    }
    const FDamageEvent& Get() const
    {
        if (Kind == FPointDamageEvent::ClassID) return Point;
        if (Kind == FRadialDamageEvent::ClassID) return Radial;
        return Generic;
    }
};
struct FRegistration
{
    FEntityLife Identity;
    uint64 RestoreRevision = 1;
    FString StableId;
    uint8 SavedKind=0;
    bool bDead = false;
    bool bEnded = false;
};
struct FActionOwner { TWeakObjectPtr<AActor> Actor; FEntityLife Life; };
struct FDamageSnapshot
{
    FReceiptId Receipt;
    FDWCombatDamageCommand Command;
    FEventSnapshot Event;
    uint64 TargetRevision = 0;
    float NotificationAmount = 0.f;
    float PreparedDamageAmount = 0.f;
    FEntityLife SourceLife;
    bool bSourceWasPlayer=false;
    float Applied = 0.f;
    bool bPrepared = false;
    bool bEngineNotifications = false;
    bool bFatal = false;
    bool bPurePush = false;
    bool bForcedFallDropPoint=false;
    FReceiptId ForcedFallReceipt;
    FVector ForcedFallDropPoint=FVector::ZeroVector;
    bool bDaggerBackstab=false;FVector DaggerVictimOrigin=FVector::ZeroVector;
    double DaggerRefreshRadius=0.,DaggerGraceSeconds=0.;
    bool bShieldDeferred=false,bShieldPerfectNow=false,bImpactRooted=false,bOrdinaryShieldBlock=false;
    double ShieldImpactTime=0.,ShieldDeadline=0.;FVector ShieldImpactDirection=FVector::ZeroVector;
};
}

struct FDWShieldFeedbackNotice { TWeakObjectPtr<ADWPlayerCharacter> Player; uint64 Revision=0; };

struct FDWCombatAuthorityState
{
    DWCombatCommit::FLedger Ledger;
    uint64 Epoch = 0;
    uint64 LastEntity = 1; // Entity 1 is a world-owned legacy-call origin, never a physical actor.
    TMap<TWeakObjectPtr<AActor>, DWCombatAuthority::FRegistration> Actors;
    TMap<uint64, DWCombatAuthority::FActionOwner> Actions;
    TMap<uint64, DWCombatAuthority::FDamageSnapshot> Damage;
    using FNotice = std::variant<FDWShieldFeedbackNotice, FDWCombatRecoveryNotice, DWCombatAuthority::FDamageSnapshot, FDWCombatWeaponResourceNotice, FDWCombatPlayerStateNotice, FDWCombatCorePickupNotice, FDWCombatCoreInjectionNotice, FDWCombatMoldRootNotice>;
    TArray<FNotice> Notices;
    uint64 ShieldDamageRevision=0;
    int32 ReadNotice = 0;
    bool bDraining = false;
    bool bRestoring=false,bRestoreFactsCommitted=false;
    TMap<uint64,FDWCombatActorIdentitySave> RestoredTombstones;
};

void FDWCombatAuthorityStateDeleter::operator()(FDWCombatAuthorityState* Value) const
{ delete Value; }

UDWCombatAuthoritySubsystem::UDWCombatAuthoritySubsystem() = default;
UDWCombatAuthoritySubsystem::~UDWCombatAuthoritySubsystem() = default;
bool UDWCombatAuthoritySubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
void UDWCombatAuthoritySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    check(IsInGameThread());
    if (!DWCombatAuthority::Next(DWCombatAuthority::LastWorldEpoch)) return;
    State.Reset(new FDWCombatAuthorityState());
    State->Epoch = DWCombatAuthority::LastWorldEpoch;
    State->Ledger.SetWorldEpoch(State->Epoch);
    State->Ledger.SetLife({1, 1});
}
void UDWCombatAuthoritySubsystem::Deinitialize()
{
    if (State)
        for (const auto& Pair : State->Actors)
            if (AActor* Actor = Pair.Key.Get()) Actor->OnEndPlay.RemoveDynamic(this, &UDWCombatAuthoritySubsystem::HandleActorEndPlay);
    State.Reset();
    Super::Deinitialize();
}
TStatId UDWCombatAuthoritySubsystem::GetStatId() const
{ RETURN_QUICK_DECLARE_CYCLE_STAT(UDWCombatAuthoritySubsystem, STATGROUP_Tickables); }
void UDWCombatAuthoritySubsystem::Tick(float DeltaTime)
{ DrainNotifications(); }
bool UDWCombatAuthoritySubsystem::IsOperational() const
{ return IsInGameThread() && State && !State->bRestoring && GetWorld() && !GetWorld()->bIsTearingDown && GetWorld()->GetNetMode() != NM_Client; }
UDWCombatAuthoritySubsystem* UDWCombatAuthoritySubsystem::Get(const UObject* Context)
{
    UWorld* World = Context ? Context->GetWorld() : nullptr;
    auto* Authority = World ? World->GetSubsystem<UDWCombatAuthoritySubsystem>() : nullptr;
    return Authority && Authority->IsOperational() ? Authority : nullptr;
}
uint64 UDWCombatAuthoritySubsystem::GetWorldEpoch() const { return State ? State->Epoch : 0; }
int32 UDWCombatAuthoritySubsystem::GetPendingDamageCount() const { return State ? State->Damage.Num() : 0; }
std::optional<DWCombatCommit::FReceipt> UDWCombatAuthoritySubsystem::FindReceipt(DWCombatCommit::FReceiptId Receipt) const
{ return State ? State->Ledger.FindReceipt(Receipt) : std::nullopt; }
DWCombatCommit::FEntityLife UDWCombatAuthoritySubsystem::CaptureActorLife(AActor* Actor)
{
    if (!IsOperational() || !IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->GetWorld() != GetWorld() || !Actor->HasAuthority()) return {};
    if (const auto* Existing = State->Actors.Find(Actor)) return Existing->bEnded ? DWCombatCommit::FEntityLife{} : Existing->Identity;
    if (!DWCombatAuthority::Next(State->LastEntity)) return {};
    DWCombatAuthority::FRegistration Registration;
    Registration.Identity = {State->LastEntity, 1};
    Registration.SavedKind=static_cast<uint8>(Actor->IsA<ADWPlayerCharacter>()?EDWCombatSavedActorKind::Player:
        Actor->IsA<ADWEnemyCharacter>()?EDWCombatSavedActorKind::Enemy:Actor->IsA<ADWEnemyNest>()?EDWCombatSavedActorKind::Nest:
        Actor->IsA<AController>()?EDWCombatSavedActorKind::Controller:EDWCombatSavedActorKind::Other);
    if (!State->Ledger.SetLife(Registration.Identity)) return {};
    State->Actors.Add(Actor, Registration);
    Actor->OnEndPlay.AddUniqueDynamic(this, &UDWCombatAuthoritySubsystem::HandleActorEndPlay);
    return Registration.Identity;
}
DWCombatCommit::FActionId UDWCombatAuthoritySubsystem::BeginAction(AActor* Owner)
{
    const auto Life = CaptureActorLife(Owner);
    if (!Life.Entity) return {};
    if (const auto* P = Cast<ADWPlayerCharacter>(Owner); P && P->IsDead()) return {};
    if (const auto* E = Cast<ADWEnemyCharacter>(Owner); E && !E->IsAlive()) return {};
    const auto* Registered = State->Actors.Find(Owner);
    if (!Registered || Registered->bDead || Registered->bEnded) return {};
    const auto Action = State->Ledger.BeginAction(Life);
    if (Action.Id) State->Actions.Add(Action.Id, {Owner, Life});
    return Action;
}
void UDWCombatAuthoritySubsystem::EndAction(DWCombatCommit::FActionId Action)
{
    if (!IsOperational() || Action.Epoch != State->Epoch) return;
    State->Ledger.EndAction(Action);
    State->Actions.Remove(Action.Id);
}
bool UDWCombatAuthoritySubsystem::IsActionOwnedBy(DWCombatCommit::FActionId Action, AActor* Owner) const
{
    if (!IsOperational() || Action.Epoch != State->Epoch || !IsValid(Owner) || Owner->IsActorBeingDestroyed()) return false;
    const auto* Registered = State->Actors.Find(Owner);
    const auto* ActionOwner = State->Actions.Find(Action.Id);
    return Registered && !Registered->bDead && !Registered->bEnded && ActionOwner && ActionOwner->Actor.Get() == Owner &&
        DWCombatAuthority::Same(ActionOwner->Life, Registered->Identity);
}
DWCombatCommit::FLedger* UDWCombatAuthoritySubsystem::GetLedgerForResourceTransaction()
{ return IsOperational() ? &State->Ledger : nullptr; }
bool UDWCombatAuthoritySubsystem::ReserveWeaponResourceNoticeSlot()
{
    if (!IsOperational() || State->Notices.Num() == MAX_int32) return false;
    State->Notices.Reserve(State->Notices.Num() + 1); return true;
}
void UDWCombatAuthoritySubsystem::EnqueueCommittedWeaponResource(const FDWCombatWeaponResourceNotice& Notice)
{
    check(IsOperational());
    check(State->Notices.Num() < State->Notices.Max());
    State->Notices.Add(FDWCombatAuthorityState::FNotice(Notice));
}
bool UDWCombatAuthoritySubsystem::ReserveMoldRootNoticeSlot(){return ReserveWeaponResourceNoticeSlot();}
void UDWCombatAuthoritySubsystem::EnqueueCommittedMoldRoot(const FDWCombatMoldRootNotice& Notice)
{check(IsOperational());check(State->Notices.Num()<State->Notices.Max());State->Notices.Add(FDWCombatAuthorityState::FNotice(Notice));}
bool UDWCombatAuthoritySubsystem::CommitFormalRootEntry(ADWPlayerCharacter* Player)
{return IsOperational()&&IsValid(Player)&&Player->GetWorld()==GetWorld()&&FDWCombatMoldRootTransaction::CommitEntry(*Player);}
bool UDWCombatAuthoritySubsystem::ReserveCoreInjectionNoticeSlot(){return ReserveWeaponResourceNoticeSlot();}
void UDWCombatAuthoritySubsystem::EnqueueCommittedCoreInjection(const FDWCombatCoreInjectionNotice& Notice)
{check(IsOperational());check(State->Notices.Num()<State->Notices.Max());State->Notices.Add(FDWCombatAuthorityState::FNotice(Notice));}
bool UDWCombatAuthoritySubsystem::CommitCoreInjection(ADWPlayerCharacter* Player)
{return IsOperational()&&IsValid(Player)&&Player->GetWorld()==GetWorld()&&FDWCombatCoreInjectionTransaction::Commit(*Player);}
bool UDWCombatAuthoritySubsystem::ReserveCorePickupNoticeSlot(){return ReserveWeaponResourceNoticeSlot();}
void UDWCombatAuthoritySubsystem::EnqueueCommittedCorePickup(const FDWCombatCorePickupNotice& Notice)
{check(IsOperational());check(State->Notices.Num()<State->Notices.Max());State->Notices.Add(FDWCombatAuthorityState::FNotice(Notice));}
bool UDWCombatAuthoritySubsystem::ReserveRecoveryNoticeSlot(){return ReserveWeaponResourceNoticeSlot();}
void UDWCombatAuthoritySubsystem::EnqueueCommittedRecovery(const FDWCombatRecoveryNotice& N)
{check(IsOperational());check(State->Notices.Num()<State->Notices.Max());State->Notices.Add(FDWCombatAuthorityState::FNotice(N));}
bool UDWCombatAuthoritySubsystem::ReservePlayerStateNoticeSlot() {return ReserveWeaponResourceNoticeSlot();}
bool UDWCombatAuthoritySubsystem::CanRestorePlayerState(AActor* Actor) const
{
    if(!IsOperational()||!IsValid(Actor)||Actor->IsActorBeingDestroyed()||Actor->GetWorld()!=GetWorld())return false;
    const auto* Entry=State->Actors.Find(Actor);
    return Entry&&!Entry->bEnded&&Entry->Identity.Life<MAX_uint64&&Entry->RestoreRevision<MAX_uint64;
}
void UDWCombatAuthoritySubsystem::EnqueueCommittedPlayerState(const FDWCombatPlayerStateNotice& Notice)
{
    check(IsOperational());check(State->Notices.Num()<State->Notices.Max());
    State->Notices.Add(FDWCombatAuthorityState::FNotice(Notice));
}
bool UDWCombatAuthoritySubsystem::UseLegacyHealingItem(ADWPlayerCharacter* Player,int32 SlotIndex)
{return IsOperational()&&IsValid(Player)&&Player->GetWorld()==GetWorld()&&FDWCombatPlayerStateTransaction::UseLegacyHealingItem(*Player,SlotIndex);}
void UDWCombatAuthoritySubsystem::PublishRestoredPlayerState(const FDWCombatPlayerStateNotice& Notice)
{if(ReservePlayerStateNoticeSlot()){EnqueueCommittedPlayerState(Notice);DrainNotifications();}}
void UDWCombatAuthoritySubsystem::InvalidateActor(AActor* Actor, bool bRestore, bool bEnded)
{
    if (!IsOperational() || !Actor) return;
    auto* Entry = State->Actors.Find(Actor);
    if (!Entry)
    {
        if (bEnded || !CaptureActorLife(Actor).Entity) return;
        Entry = State->Actors.Find(Actor);
    }
    if (!Entry || Entry->bEnded || (!bRestore && !bEnded && Entry->bDead)) return;
    if (!DWCombatAuthority::Next(Entry->Identity.Life)) { Entry->bEnded = true; return; }
    if (bRestore || bEnded)
        if (!DWCombatAuthority::Next(Entry->RestoreRevision)) { Entry->bEnded = true; return; }
    Entry->bDead = !bRestore;
    Entry->bEnded = bEnded;
    State->Ledger.SetLife(Entry->Identity);
    PruneFinalDamageSnapshots();
    // Never retire released emitters here. Their rule-owned lifecycle is independent.
}
void UDWCombatAuthoritySubsystem::PruneFinalDamageSnapshots()
{
    if (!State) return;
    for (auto It = State->Damage.CreateIterator(); It; ++It)
    {
        const auto Receipt = State->Ledger.FindReceipt(It.Value().Receipt);
        if (!Receipt || Receipt->State != DWCombatCommit::EReceiptState::Pending) It.RemoveCurrent();
    }
}
void UDWCombatAuthoritySubsystem::NotifyActorDeath(AActor* Actor) { InvalidateActor(Actor, false, false); }
double UDWCombatAuthoritySubsystem::GetNextShieldDamageBoundary(const ADWPlayerCharacter* Player,double Start)const
{
    double Result=std::numeric_limits<double>::infinity();
    if(!IsOperational()||!IsValid(Player)||!FMath::IsFinite(Start))return Result;
    for(const auto& Pair:State->Damage)
    {
        const auto& S=Pair.Value;if(S.bShieldDeferred&&S.Command.Target.Get()==Player)
            Result=FMath::Min(Result,FMath::Max(0.,S.ShieldDeadline-Start));
    }
    return Result;
}
uint64 UDWCombatAuthoritySubsystem::GetShieldDamageRevision()const{return State?State->ShieldDamageRevision:0;}
int32 UDWCombatAuthoritySubsystem::GetPendingShieldDamageCount(const ADWPlayerCharacter* Player)const
{
    int32 Count=0;if(State)for(const auto& Pair:State->Damage)if(Pair.Value.bShieldDeferred&&Pair.Value.Command.Target.Get()==Player)++Count;
    return Count;
}
void UDWCombatAuthoritySubsystem::ParryEligibleShieldDamage(ADWPlayerCharacter* Player,double Now)
{
    if(!IsOperational()||!Player||!Player->CanReceiveCombatDamage()||!FMath::IsFinite(Now)||!Player->WeaponAnimationComponent||
        !Player->WeaponAnimationComponent->CanPerfectShieldAt(Now))return;
    TArray<DWCombatCommit::FReceiptId> Receipts;
    for(const auto& Pair:State->Damage)
    {
        const auto& S=Pair.Value;
        if(S.bShieldDeferred&&S.Command.Target.Get()==Player&&Now<=S.ShieldDeadline&&Now>=S.ShieldImpactTime)Receipts.Add(S.Receipt);
    }
    Receipts.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});
    // Reject each impact independently. Back/DOT/unarmed-at-impact packages never enter this list.
    for(const auto Receipt:Receipts)
    {
        auto* Snapshot=State->Damage.Find(Receipt.Id);if(!Snapshot||!Snapshot->bShieldDeferred)continue;
        const auto Before=State->Ledger.FindReceipt(Receipt);if(!Before||Before->State!=DWCombatCommit::EReceiptState::Pending)continue;
        Snapshot->bShieldDeferred=false;
        const auto Result=CommitPending(Receipt,DWCombatCommit::EOutcome::Rejected);
        if(Result.Receipt.State==DWCombatCommit::EReceiptState::Rejected)
        {
            Player->WeaponAnimationComponent->RecordPerfectShieldFacts();++State->ShieldDamageRevision;
            if(const auto* Entry=State->Actors.Find(Player))State->Notices.Add(FDWShieldFeedbackNotice{Player,Entry->RestoreRevision});
        }
    }
    // Parry facts have no damage/knockback/death notifications to roll back.
}
void UDWCombatAuthoritySubsystem::ResolveShieldDamageAt(ADWPlayerCharacter* Player,double Now)
{
    if(!IsOperational()||!Player||!Player->CanReceiveCombatDamage()||!FMath::IsFinite(Now))return;
    TArray<DWCombatCommit::FReceiptId> Due;
    for(const auto& Pair:State->Damage)
    {const auto& S=Pair.Value;if(S.bShieldDeferred&&S.Command.Target.Get()==Player&&S.ShieldDeadline<=Now)Due.Add(S.Receipt);}
    Due.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});
    for(const auto Receipt:Due)
    {
        auto* Snapshot=State->Damage.Find(Receipt.Id);if(!Snapshot||!Snapshot->bShieldDeferred)continue;
        Snapshot->bShieldDeferred=false;CommitPending(Receipt,DWCombatCommit::EOutcome::Committed);++State->ShieldDamageRevision;
    }
    DrainNotifications(); // Same due boundary native facts finish before the first callback.
}

void UDWCombatAuthoritySubsystem::NotifyActorRestored(AActor* Actor) { InvalidateActor(Actor, true, false); }
void UDWCombatAuthoritySubsystem::HandleActorEndPlay(AActor* Actor, EEndPlayReason::Type Reason) { InvalidateActor(Actor, false, true); }
bool UDWCombatAuthoritySubsystem::IsNoticeRecipientCurrent(AActor* Actor, uint64 Revision) const
{
    if (!IsOperational() || !IsValid(Actor) || Actor->IsActorBeingDestroyed()) return false;
    const auto* Entry = State->Actors.Find(Actor);
    return Entry && !Entry->bEnded && Entry->RestoreRevision == Revision;
}

FDWCombatDamageResult UDWCombatAuthoritySubsystem::AdmitActionDamage(const FDWCombatDamageCommand& Command)
{
    using namespace DWCombatCommit;
    if (!IsOperational() || Command.Identity.Action.Epoch != State->Epoch ||
        (Command.Identity.Kind != EActionDamageKind::Melee && Command.Identity.Kind != EActionDamageKind::Dash && Command.Identity.Kind != EActionDamageKind::EnemyMelee)) return {};
    const auto* Owner = State->Actions.Find(Command.Identity.Action.Id);
    if (!Owner || Owner->Actor != Command.Source || !Command.Source.IsValid()) return {};
    const bool bEnemyHit=Command.Identity.Kind==EActionDamageKind::EnemyMelee;
    if(bEnemyHit?(!Cast<ADWEnemyCharacter>(Command.Source.Get())||!Cast<ADWPlayerCharacter>(Command.Target.Get())):
        (!Cast<ADWPlayerCharacter>(Command.Source.Get())||(!Cast<ADWEnemyCharacter>(Command.Target.Get())&&!Cast<ADWEnemyNest>(Command.Target.Get()))))return {};
    FDamageEvent Event(UDamageType::StaticClass());
    return AdmitPrepared(Command, Event, false);
}
FDWCombatDamageResult UDWCombatAuthoritySubsystem::AdmitPrepared(const FDWCombatDamageCommand& Command, const FDamageEvent& Event, bool bLegacy,
    const DWCombatCommit::FRootMineDamage* MineIdentity,DWCombatCommit::FEntityLife ReleasedSourceLife,const DWCombatCommit::FEmitterDamage* EmitterIdentity,bool bDaggerBackstab)
{
    using namespace DWCombatCommit;
    using namespace DWCombatAuthority;
    FDWCombatDamageResult Result;
    AActor* Target = Command.Target.Get();
    if (!IsOperational() || !IsValid(Target) || Target->IsActorBeingDestroyed() || Target->GetWorld() != GetWorld() ||
        !FMath::IsFinite(Command.Amount) || Command.Amount <= 0.f ||
        !FMath::IsFinite(Command.KnockbackDistance) || Command.KnockbackDistance < 0.f ||
        !FMath::IsFinite(Command.KnockbackSeconds) || Command.KnockbackSeconds <= 0.f || Command.KnockbackDirection.ContainsNaN()) return Result;
    if (!bLegacy && (!Target->CanBeDamaged() || !Target->GetActorEnableCollision())) return Result;
    if(const auto* Player=Cast<ADWPlayerCharacter>(Target);Player&&!Player->CanReceiveCombatDamage())return Result;
    const auto Life = CaptureActorLife(Target);
    if (!Life.Entity || !Same(Life, Command.TargetLife)) { Result.Admission = EAdmission::Stale; return Result; }
    FEventSnapshot EventCopy;
    if (!EventCopy.Capture(Event))
    {
        UE_LOG(LogTemp, Warning, TEXT("D05 rejected unsupported custom FDamageEvent type %d; a typed adapter is required."), Event.GetTypeID());
        return Result;
    }
    FEntityLife SourceLife;
    const bool bSourceWasPlayer=EmitterIdentity||Cast<ADWPlayerCharacter>(Command.Source.Get())!=nullptr;
    if(MineIdentity||EmitterIdentity)SourceLife=ReleasedSourceLife;
    else if(bLegacy)SourceLife=CaptureActorLife(Command.Source.Get());
    else if(const auto* ActionOwner=State->Actions.Find(Command.Identity.Action.Id))SourceLife=ActionOwner->Life;
    const auto Admission = State->Ledger.AdmitDamage({EmitterIdentity?FDamageSource(*EmitterIdentity):MineIdentity?FDamageSource(*MineIdentity):FDamageSource(Command.Identity),Command.TargetLife});
    Result.Admission = Admission.Result; Result.Receipt = Admission.Receipt;
    if (Admission.Result != EAdmission::Admitted) return Result;
    FDamageSnapshot Snapshot;
    Snapshot.Receipt = Admission.Receipt.Handle; Snapshot.Command = Command; Snapshot.Event = MoveTemp(EventCopy);
    Snapshot.SourceLife=SourceLife;Snapshot.bSourceWasPlayer=bSourceWasPlayer;
    Snapshot.TargetRevision = State->Actors.FindChecked(Target).RestoreRevision;
    Snapshot.PreparedDamageAmount = Command.Amount;Snapshot.bDaggerBackstab=bDaggerBackstab;
    State->Damage.Add(Snapshot.Receipt.Id, Snapshot); // Reserve before any overridable engine hook.
    if (!NativeTargetAlive(Target))
    {
        State->Ledger.Resolve(Snapshot.Receipt, EOutcome::Rejected);
        State->Damage.Remove(Snapshot.Receipt.Id);
        Result.Receipt = *State->Ledger.FindReceipt(Snapshot.Receipt); return Result;
    }
    bool bNotify = true;
    if(auto* Player=Cast<ADWPlayerCharacter>(Target))
    {
        const double Scaled=static_cast<double>(Command.Amount)*Player->GetCombatIncomingMultiplier();
        if(!FMath::IsFinite(Scaled)||Scaled>MAX_flt)
        {State->Ledger.Resolve(Snapshot.Receipt,EOutcome::Rejected);State->Damage.Remove(Snapshot.Receipt.Id);Result.Receipt=*State->Ledger.FindReceipt(Snapshot.Receipt);return Result;}
        float Final=static_cast<float>(Scaled);
        if(auto* Weapon=Player->WeaponAnimationComponent.Get())
        {
            const bool EnemyMelee=!bLegacy&&Command.Identity.Kind==EActionDamageKind::EnemyMelee;
            const auto Kind=EnemyMelee?EDWShieldIncomingKind::Melee:Command.ShieldIncomingKind;
            const FVector Incoming=EnemyMelee&&Command.Source.IsValid()?
                Command.Source->GetActorLocation()-Player->GetActorLocation():Command.ShieldIncomingDirection;
            const double Impact=Weapon->GetCombatEventWorldSeconds();
            const auto Policy=Weapon->PrepareShieldImpact(Impact,Kind,Incoming);
            Final=static_cast<float>(static_cast<double>(Final)*Policy.OrdinaryDamageMultiplier);
            auto* Pending=State->Damage.Find(Snapshot.Receipt.Id);
            if(Pending&&Policy.bEligible)
            {
                Pending->bShieldPerfectNow=Policy.bPerfectNow;Pending->bShieldDeferred=!Policy.bPerfectNow;Pending->bOrdinaryShieldBlock=Policy.OrdinaryDamageMultiplier<1.;
                Pending->ShieldImpactTime=Impact;Pending->ShieldDeadline=Impact+Policy.AfterSeconds;
                Pending->ShieldImpactDirection=Incoming.GetSafeNormal2D();Pending->bImpactRooted=Player->IsFormallyRooted();
                if(Pending->bShieldDeferred)++State->ShieldDamageRevision;
            }
        }
        if(!IsNoticeRecipientCurrent(Target,Snapshot.TargetRevision)||!NativeTargetAlive(Target)||!FMath::IsFinite(Final))
        {
            if(IsOperational())
            {State->Ledger.Resolve(Snapshot.Receipt,EOutcome::Rejected);State->Damage.Remove(Snapshot.Receipt.Id);
             if(const auto Terminal=State->Ledger.FindReceipt(Snapshot.Receipt))Result.Receipt=*Terminal;}
            else Result.Receipt.State=EReceiptState::Invalidated;
            return Result;
        }
        if(auto* Pending=State->Damage.Find(Snapshot.Receipt.Id))Pending->PreparedDamageAmount=FMath::IsFinite(Final)?FMath::Max(0.f,Final):0.f;
        bNotify=Final>0.f;
    }
    if (auto* Enemy = Cast<ADWEnemyCharacter>(Target); Enemy && !MineIdentity && !bDaggerBackstab)
        bNotify = Enemy->ShouldTakeDamage(Command.Amount, Snapshot.Event.Get(), Command.Instigator.Get(), Command.Source.Get());
    if (!IsNoticeRecipientCurrent(Command.Target.Get(), Snapshot.TargetRevision))
    {
        Result.Admission = EAdmission::Stale;
        if (State) { if (const auto R = State->Ledger.FindReceipt(Snapshot.Receipt)) Result.Receipt = *R; }
        else Result.Receipt.State = EReceiptState::Invalidated;
        return Result;
    }
    float NotificationAmount = bNotify ? Command.Amount : 0.f;
    // These native virtual hooks are preparation, outside the fact-commit section.
    if (bNotify && Snapshot.Event.Kind == FPointDamageEvent::ClassID)
    {
        if (auto* Enemy = Cast<ADWEnemyCharacter>(Target)) NotificationAmount = Enemy->InternalTakePointDamage(Command.Amount, Snapshot.Event.Point, Command.Instigator.Get(), Command.Source.Get());
        else if (auto* Nest = Cast<ADWEnemyNest>(Target)) NotificationAmount = Nest->InternalTakePointDamage(Command.Amount, Snapshot.Event.Point, Command.Instigator.Get(), Command.Source.Get());
    }
    else if (bNotify && Snapshot.Event.Kind == FRadialDamageEvent::ClassID)
    {
        if (auto* Enemy = Cast<ADWEnemyCharacter>(Target)) NotificationAmount = Enemy->InternalTakeRadialDamage(Command.Amount, Snapshot.Event.Radial, Command.Instigator.Get(), Command.Source.Get());
        else if (auto* Nest = Cast<ADWEnemyNest>(Target)) NotificationAmount = Nest->InternalTakeRadialDamage(Command.Amount, Snapshot.Event.Radial, Command.Instigator.Get(), Command.Source.Get());
    }
    if (!IsNoticeRecipientCurrent(Target, Snapshot.TargetRevision))
    {
        Result.Admission = EAdmission::Stale;
        if (State) { if (const auto R = State->Ledger.FindReceipt(Snapshot.Receipt)) Result.Receipt = *R; }
        else Result.Receipt.State = EReceiptState::Invalidated;
        return Result;
    }
    auto* Prepared = State->Damage.Find(Snapshot.Receipt.Id);
    if (!Prepared) return Result;
    Prepared->NotificationAmount = Cast<ADWPlayerCharacter>(Target)?Prepared->PreparedDamageAmount:(FMath::IsFinite(NotificationAmount)?NotificationAmount:0.f);
    Prepared->bEngineNotifications = bNotify && Prepared->NotificationAmount != 0.f;
    Prepared->bPrepared = true;
    if(Prepared->bShieldPerfectNow)
    {
        const auto ResultPerfect=CommitPending(Snapshot.Receipt,EOutcome::Rejected);
        if(ResultPerfect.Receipt.State==EReceiptState::Rejected)
            if(auto* Player=Cast<ADWPlayerCharacter>(Target);Player&&Player->WeaponAnimationComponent)
            {
                Player->WeaponAnimationComponent->RecordPerfectShieldFacts();
                if(const auto* Entry=State->Actors.Find(Player))State->Notices.Add(FDWShieldFeedbackNotice{Player,Entry->RestoreRevision});
            }
        return ResultPerfect;
    }
    const auto Receipt = State->Ledger.FindReceipt(Snapshot.Receipt);
    if (Receipt) Result.Receipt = *Receipt;
    return Result;
}
FDWCombatDamageResult UDWCombatAuthoritySubsystem::CommitPending(DWCombatCommit::FReceiptId Receipt, DWCombatCommit::EOutcome Outcome)
{
    using namespace DWCombatCommit;
    FDWCombatDamageResult Result;
    if (!IsOperational() || Receipt.Epoch != State->Epoch) { Result.Admission = EAdmission::Stale; return Result; }
    const auto Existing = State->Ledger.FindReceipt(Receipt);
    if (!Existing) return Result;
    Result.Receipt = *Existing; Result.Admission = EAdmission::Duplicate;
    if (Existing->State != EReceiptState::Pending) return Result;
    auto* Snapshot = State->Damage.Find(Receipt.Id);
    if (!Snapshot || !Snapshot->bPrepared) return Result;
    AActor* Target = Snapshot->Command.Target.Get();
    const auto* Entry = State->Actors.Find(Target);
    if (!IsNoticeRecipientCurrent(Target, Snapshot->TargetRevision) || !Entry || !DWCombatAuthority::Same(Entry->Identity, Snapshot->Command.TargetLife) || !DWCombatAuthority::NativeTargetAlive(Target))
    {
        State->Ledger.Resolve(Receipt, EOutcome::Rejected);
        if (const auto R = State->Ledger.FindReceipt(Receipt)) Result.Receipt = *R;
        State->Damage.Remove(Receipt.Id);
        Result.Admission = EAdmission::Stale; return Result;
    }
    if (Outcome == EOutcome::Rejected)
    {
        State->Ledger.Resolve(Receipt, Outcome);
        State->Damage.Remove(Receipt.Id);
        Result.Receipt = *State->Ledger.FindReceipt(Receipt); Result.Admission = EAdmission::Admitted; return Result;
    }
    if (Outcome != EOutcome::Committed) return Result;
    if(Snapshot->bShieldDeferred)return Result; // Only the owning deadline processor may commit this pending impact.
    if(const auto* Player=Cast<ADWPlayerCharacter>(Target);Player&&!Player->CanReceiveCombatDamage())
    {State->Ledger.Resolve(Receipt,EOutcome::Rejected);State->Damage.Remove(Receipt.Id);
     if(const auto Terminal=State->Ledger.FindReceipt(Receipt))Result.Receipt=*Terminal;return Result;}
    float* Health = nullptr;float HealthFloor=0.f;
    if(auto* Player=Cast<ADWPlayerCharacter>(Target)){Health=&Player->Health;HealthFloor=Player->GetCombatHealthFloor();}
    else if (auto* Enemy = Cast<ADWEnemyCharacter>(Target)) Health = &Enemy->Health;
    else if (auto* Nest = Cast<ADWEnemyNest>(Target)) Health = &Nest->Health;
    if (!Health || !FMath::IsFinite(*Health) || *Health <= HealthFloor)
    {
        State->Ledger.Resolve(Receipt, EOutcome::Rejected);
        State->Damage.Remove(Receipt.Id);
        if (const auto R = State->Ledger.FindReceipt(Receipt)) Result.Receipt = *R;
        return Result;
    }
    const float After=FMath::Max(HealthFloor,*Health-Snapshot->PreparedDamageAmount);
    const float Applied=*Health-After;
    FDWEnemyFormalKnockback EnemyKnockback;FDWPlayerCombatKnockback PlayerKnockback;
    if(Applied>0.f&&After>HealthFloor)
    {
        if(auto* Enemy=Cast<ADWEnemyCharacter>(Target);Enemy&&Enemy->IsFormalCombatReady())
            EnemyKnockback=Enemy->PrepareFormalKnockback(Snapshot->Command.KnockbackDirection,Snapshot->Command.KnockbackDistance,Snapshot->Command.KnockbackSeconds);
        if(auto* Player=Cast<ADWPlayerCharacter>(Target))
            PlayerKnockback=Snapshot->ShieldDeadline>0.?
                Player->PrepareCombatKnockbackAtImpact(Snapshot->Command.KnockbackDirection,Snapshot->Command.KnockbackDistance,Snapshot->Command.KnockbackSeconds,Snapshot->Command.TargetLife,Snapshot->bImpactRooted):
                Player->PrepareCombatKnockback(Snapshot->Command.KnockbackDirection,Snapshot->Command.KnockbackDistance,Snapshot->Command.KnockbackSeconds,Snapshot->Command.TargetLife);
    }
    FDWPreparedCoreDrop CoreDrop;
    UDWCombatCoreDropSubsystem* CoreDrops=nullptr;
    if(After<=HealthFloor)if(auto* Enemy=Cast<ADWEnemyCharacter>(Target);Enemy&&Enemy->IsFormalCombatReady())
    {
        CoreDrops=UDWCombatCoreDropSubsystem::Get(Enemy);
        if(!CoreDrops||!CoreDrops->PrepareFatal(*Enemy,Receipt,Snapshot->Command.TargetLife,
            Snapshot->bSourceWasPlayer?Snapshot->SourceLife:FEntityLife{},CoreDrop,
            Snapshot->bForcedFallDropPoint?&Snapshot->ForcedFallDropPoint:nullptr,Snapshot->ForcedFallReceipt))return Result;
    }
    FDWPreparedCombatDeath RecoveryDeath;
    if(After<=HealthFloor)if(auto* Player=Cast<ADWPlayerCharacter>(Target))
        if(!FDWCombatRecoveryTransaction::PrepareFatal(*Player,RecoveryDeath))
        {State->Ledger.Resolve(Receipt,EOutcome::Rejected);State->Damage.Remove(Receipt.Id);
         Result.Receipt=*State->Ledger.FindReceipt(Receipt);return Result;}
    auto Notice = *Snapshot;
    State->Notices.Reserve(State->Notices.Num() + (RecoveryDeath.bValid?2:1));
    // No external/overridable calls, Blueprint hooks or delegates between Resolve and these assignments.
    // Validated damage facts and the fixed fatal inventory/bag transaction commit in this callback-free interval.
    if (State->Ledger.Resolve(Receipt, EOutcome::Committed) != EResolve::Resolved) return Result;
    *Health = After;
    const bool HasRecoveryDeath=RecoveryDeath.bValid;
    if(HasRecoveryDeath)FDWCombatRecoveryTransaction::CommitFatalUnchecked(MoveTemp(RecoveryDeath));
    if(auto* Player=Cast<ADWPlayerCharacter>(Target))
    {Player->RecordCombatDamageFacts(Applied);if(Applied>0.f&&Player->WeaponAnimationComponent)Player->WeaponAnimationComponent->BreakDaggerGraceForDamage();}
    Snapshot->Applied = Applied; Snapshot->bFatal = *Health <= HealthFloor;
    Notice.Applied = Applied; Notice.bFatal = *Health <= HealthFloor;
    if(Applied>0.f)if(auto* Enemy=Cast<ADWEnemyCharacter>(Target))
    {
        Enemy->RecordFormalDamageFacts(Notice.Command.Source.Get(),Notice.SourceLife,Receipt,Notice.Command.TargetLife,Notice.bFatal,Notice.bSourceWasPlayer);
        Enemy->RecordDaggerDamageFacts(Notice.Command.Source.Get(),Notice.bSourceWasPlayer);
        if(!Notice.bFatal)Enemy->CommitFormalKnockbackFacts(EnemyKnockback,Receipt,
            Notice.bSourceWasPlayer?Notice.SourceLife:FEntityLife{},Notice.Command.TargetLife);
    }
    if(!Notice.bFatal)if(auto* Player=Cast<ADWPlayerCharacter>(Target))Player->CommitCombatKnockbackFacts(PlayerKnockback);
    if (Notice.bFatal)
    {
        if(auto* Player=Cast<ADWPlayerCharacter>(Target))Player->CommitCombatDeathFacts();
        else if (auto* Enemy = Cast<ADWEnemyCharacter>(Target)) Enemy->CommitCombatDeathFacts();
        else if (auto* Nest = Cast<ADWEnemyNest>(Target)) Nest->CommitCombatDeathFacts();
    }
    if(CoreDrop.bValid)CoreDrops->CommitFatalUnchecked(MoveTemp(CoreDrop));
    if(Notice.bDaggerBackstab&&Applied>0.f)
        if(auto* Player=Cast<ADWPlayerCharacter>(Notice.Command.Source.Get()))
            for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
                if(*It!=Target&&FVector::DistSquared2D(It->GetActorLocation(),Notice.DaggerVictimOrigin)<=FMath::Square(Notice.DaggerRefreshRadius))
                    It->RefreshDaggerGraceFacts(Player,GetWorld()->GetTimeSeconds(),Notice.DaggerGraceSeconds);
    Result.Receipt = *State->Ledger.FindReceipt(Receipt); Result.Admission = EAdmission::Admitted; Result.AppliedThisCall = Applied;
    State->Damage.Remove(Receipt.Id);
    if (Notice.bFatal) NotifyActorDeath(Target); // Ledger-only lifecycle invalidation, no gameplay callback.
    if(HasRecoveryDeath)
    {RecoveryDeath.Notice.Life=CaptureActorLife(Target);EnqueueCommittedRecovery(RecoveryDeath.Notice);}
    State->Notices.Add(FDWCombatAuthorityState::FNotice(MoveTemp(Notice)));
    return Result;
}
FDWCombatDamageResult UDWCombatAuthoritySubsystem::ResolvePendingDamage(DWCombatCommit::FReceiptId Receipt, DWCombatCommit::EOutcome Outcome)
{ const auto Result = CommitPending(Receipt, Outcome); DrainNotifications(); return Result; }
FDWCombatDamageResult UDWCombatAuthoritySubsystem::SubmitActionDamage(const FDWCombatDamageCommand& Command)
{
    const auto Admission = AdmitActionDamage(Command);
    if (Admission.Admission != DWCombatCommit::EAdmission::Admitted || Admission.Receipt.State != DWCombatCommit::EReceiptState::Pending) {DrainNotifications();return Admission;}
    return ResolvePendingDamage(Admission.Receipt.Handle, DWCombatCommit::EOutcome::Committed);
}
float UDWCombatAuthoritySubsystem::SubmitLegacyDamage(AActor* Target, float Amount, const FDamageEvent& Event, AController* Instigator, AActor* Causer)
{
    using namespace DWCombatCommit;
    if (!IsOperational()) return 0.f;
    const auto Action = State->Ledger.BeginAction({1, 1});
    if (!Action.Id) return 0.f;
    FDWCombatDamageCommand Command;
    Command.Identity = {Action, 1, EActionDamageKind::LegacyExternal, 1};
    Command.TargetLife = CaptureActorLife(Target); Command.Source = Causer; Command.Target = Target;
    Command.Instigator = Instigator; Command.Amount = Amount;
    const auto Admission = AdmitPrepared(Command, Event, true);
    FDWCombatDamageResult Result;
    if (Admission.Admission == EAdmission::Admitted && Admission.Receipt.State == EReceiptState::Pending)
        Result = CommitPending(Admission.Receipt.Handle, EOutcome::Committed);
    if (IsOperational()) State->Ledger.EndAction(Action);
    DrainNotifications();
    return Result.AppliedThisCall;
}

FDWCombatDamageResult UDWCombatAuthoritySubsystem::SubmitEnemyEnvironmentFatal(
    ADWEnemyCharacter& Enemy,EDWEnemyEnvironmentFatal Reason,const UDamageType& DamageType)
{
    using namespace DWCombatCommit;using namespace DWCombatAuthority;
    if(!IsOperational()||Reason>EDWEnemyEnvironmentFatal::OtherEnvironment||!IsValid(&Enemy)||
        Enemy.GetWorld()!=GetWorld()||Enemy.IsActorBeingDestroyed()||!Enemy.IsAlive()||
        !FMath::IsFinite(Enemy.Health)||Enemy.Health<=0.f)return {};
    const auto Life=CaptureActorLife(&Enemy);const auto* Entry=State->Actors.Find(&Enemy);if(!Entry||!Life.Entity)return {};
    const uint64 Revision=Entry->RestoreRevision;
    FEntityLife Player;FVector DropPoint=FVector::ZeroVector;
    const bool Credited=Reason!=EDWEnemyEnvironmentFatal::OtherEnvironment&&Enemy.CaptureForcedFallCredit(Player,DropPoint);
    if(!IsNoticeRecipientCurrent(&Enemy,Revision)||!Enemy.IsAlive()||!Same(CaptureActorLife(&Enemy),Life))return {};
    const auto Action=State->Ledger.BeginAction({1,1});if(!Action.Id)return {};
    const FActionDamage Identity{Action,1,EActionDamageKind::EnvironmentFatal,1};
    const auto Admission=State->Ledger.AdmitDamage({Identity,Life});
    FDWCombatDamageResult Result;Result.Admission=Admission.Result;Result.Receipt=Admission.Receipt;
    if(Admission.Result==EAdmission::Admitted)
    {
        FDamageSnapshot S;S.Receipt=Admission.Receipt.Handle;S.Command.Identity=Identity;
        S.Command.Target=&Enemy;S.Command.TargetLife=Life;S.TargetRevision=Revision;
        // Full actual remaining HP is the fatal fact. No synthetic chip damage or old source action is used.
        S.Command.Amount=Enemy.Health;S.PreparedDamageAmount=Enemy.Health;S.NotificationAmount=Enemy.Health;
        S.SourceLife=Credited?Player:FEntityLife{};S.bSourceWasPlayer=Credited;
        S.bForcedFallDropPoint=Credited;S.ForcedFallDropPoint=DropPoint;
        if(Credited)S.ForcedFallReceipt={Enemy.ForcedFall.Saved.ReceiptEpoch,Enemy.ForcedFall.Saved.ReceiptId};
        S.bPrepared=true;S.bEngineNotifications=true;S.Event.Capture(FDamageEvent(DamageType.GetClass()));
        State->Damage.Add(S.Receipt.Id,MoveTemp(S));Result=CommitPending(Admission.Receipt.Handle,EOutcome::Committed);
    }
    if(IsOperational())State->Ledger.EndAction(Action);
    DrainNotifications();return Result;
}

void UDWCombatAuthoritySubsystem::SubmitVitalsDamagePair(const FDWCombatVitalsDamagePair& Pair)
{
    using namespace DWCombatCommit;using namespace DWCombatAuthority;
    auto* Player=Pair.Target.Get();
    if(!IsOperational()||Pair.WorldEpoch!=State->Epoch||!Player||!Player->CanReceiveCombatDamage()||
        !Same(CaptureActorLife(Player),Pair.SourceLife)||!FMath::IsFinite(Pair.DehydrationAmount)||Pair.DehydrationAmount<0.f||
        !FMath::IsFinite(Pair.StarvationAmount)||Pair.StarvationAmount<0.f||
        (Pair.DehydrationAmount>0.f&&!Pair.DehydrationOrdinal)||(Pair.StarvationAmount>0.f&&!Pair.StarvationOrdinal))return;
    const float Amounts[]={Pair.DehydrationAmount,Pair.StarvationAmount};
    const uint64 Ordinals[]={Pair.DehydrationOrdinal,Pair.StarvationOrdinal};
    FReceiptId Pending[2];
    for(int32 Index=0;Index<2;++Index)
    {
        if(Amounts[Index]<=0.f)continue;
        FVitalsDamage Source{Pair.WorldEpoch,Pair.SourceLife,Index==0?EVitalsSource::Dehydration:EVitalsSource::Starvation,Ordinals[Index]};
        const auto Admission=State->Ledger.AdmitDamage({Source,Pair.SourceLife});
        if(Admission.Result!=EAdmission::Admitted)continue;
        FDamageSnapshot Snapshot;Snapshot.Receipt=Admission.Receipt.Handle;
        Snapshot.Command.Source=Player;Snapshot.Command.Target=Player;Snapshot.Command.TargetLife=Pair.SourceLife;
        Snapshot.Command.Amount=Snapshot.PreparedDamageAmount=Snapshot.NotificationAmount=Amounts[Index];
        Snapshot.TargetRevision=State->Actors.FindChecked(Player).RestoreRevision;
        Snapshot.bPrepared=true;Snapshot.bEngineNotifications=true;
        FDamageEvent Event(UDamageType::StaticClass());Snapshot.Event.Capture(Event);
        Pending[Index]=Snapshot.Receipt;State->Damage.Add(Snapshot.Receipt.Id,MoveTemp(Snapshot));
    }
    // No external call between the two commits. Death invalidates a still-pending second packet.
    for(const auto Receipt:Pending)if(Receipt.Id)CommitPending(Receipt,EOutcome::Committed);
    DrainNotifications();
}

void UDWCombatAuthoritySubsystem::DrainNotifications()
{
    if (!IsOperational() || State->bDraining) return;
    const uint64 DrainingEpoch = State->Epoch;
    State->bDraining = true;
    // Facts are synchronous. The finite notification budget prevents an extension
    // that repeatedly heals and damages from monopolizing the game thread.
    for (int32 Budget = 256; Budget > 0 && IsOperational() && State->Epoch == DrainingEpoch && State->ReadNotice < State->Notices.Num(); --Budget)
    {
        const auto Queued = State->Notices[State->ReadNotice++]; // Copy before any callback can append/reallocate.
        if(const auto* Shield=std::get_if<FDWShieldFeedbackNotice>(&Queued))
        {
            if(auto* P=Shield->Player.Get();IsNoticeRecipientCurrent(P,Shield->Revision)&&P->CombatFeedback)
                P->CombatFeedback->PlayFeedback(EDWCombatFeedbackCue::PerfectBlock);
            continue;
        }
        if (const auto* Resource = std::get_if<FDWCombatWeaponResourceNotice>(&Queued))
        {
            FDWCombatInventoryTransaction::PublishWeaponResourceNotice(*Resource);
            continue;
        }
        if(const auto* Recovery=std::get_if<FDWCombatRecoveryNotice>(&Queued))
        {FDWCombatRecoveryTransaction::Publish(*Recovery);continue;}
        if(const auto* PlayerState=std::get_if<FDWCombatPlayerStateNotice>(&Queued))
        {FDWCombatPlayerStateTransaction::Publish(*PlayerState);continue;}
        if(const auto* Root=std::get_if<FDWCombatMoldRootNotice>(&Queued))
        {FDWCombatMoldRootTransaction::Publish(*Root);continue;}
        if(const auto* Injection=std::get_if<FDWCombatCoreInjectionNotice>(&Queued))
        {FDWCombatCoreInjectionTransaction::Publish(*Injection);continue;}
        if(const auto* Core=std::get_if<FDWCombatCorePickupNotice>(&Queued))
        {FDWCombatCorePickupTransaction::Publish(*Core);continue;}
        const auto& Notice = std::get<DWCombatAuthority::FDamageSnapshot>(Queued);
        AActor* Target = Notice.Command.Target.Get();
        const auto Current = [this, WeakTarget = Notice.Command.Target, Revision = Notice.TargetRevision]() { return IsNoticeRecipientCurrent(WeakTarget.Get(), Revision); };
        if (!Current()) continue;
        if(auto* Player=Cast<ADWPlayerCharacter>(Target))
        {
            if(Notice.bOrdinaryShieldBlock&&!Notice.bFatal&&Player->CombatFeedback)Player->CombatFeedback->PlayFeedback(EDWCombatFeedbackCue::Block);
            if(!Current())continue;
            Player->PublishCombatKnockbackEffects();if(!Current())continue;
        }
        const auto Instigator = [&Notice]() { return Notice.Command.Instigator.Get(); };
        const auto Causer = [&Notice]() { return Notice.Command.Source.Get(); };
        const UDamageType* DamageType = Notice.Event.TypeClass.IsValid() ? Notice.Event.TypeClass->GetDefaultObject<UDamageType>() : GetDefault<UDamageType>();
        // UE5.8 Actor.cpp:3417-3480 / Pawn.cpp:567-596 event order, with reentry
        // deferred to the FIFO and lifecycle checks after every external call.
        if (Notice.bEngineNotifications)
        {
            const float Damage = Notice.NotificationAmount;
            if (Notice.Event.Kind == FPointDamageEvent::ClassID)
            {
                const auto& Point = Notice.Event.Point;
                Target->ReceivePointDamage(Damage, DamageType, Point.HitInfo.ImpactPoint, Point.HitInfo.ImpactNormal, Point.HitInfo.Component.Get(), Point.HitInfo.BoneName, Point.ShotDirection, Instigator(), Causer(), Point.HitInfo);
                if (!Current()) continue;
                Target->OnTakePointDamage.Broadcast(Target, Damage, Instigator(), Point.HitInfo.ImpactPoint, Point.HitInfo.Component.Get(), Point.HitInfo.BoneName, Point.ShotDirection, DamageType, Causer());
                if (!Current()) continue;
                if (UPrimitiveComponent* Component = Point.HitInfo.Component.Get()) Component->ReceiveComponentDamage(Notice.Command.Amount, Notice.Event.Get(), Instigator(), Causer());
                if (!Current()) continue;
            }
            else if (Notice.Event.Kind == FRadialDamageEvent::ClassID)
            {
                const auto& Radial = Notice.Event.Radial;
                const FHitResult Hit = Radial.ComponentHits.IsEmpty() ? FHitResult() : Radial.ComponentHits[0];
                Target->ReceiveRadialDamage(Damage, DamageType, Radial.Origin, Hit, Instigator(), Causer());
                if (!Current()) continue;
                Target->OnTakeRadialDamage.Broadcast(Target, Damage, DamageType, Radial.Origin, Hit, Instigator(), Causer());
                if (!Current()) continue;
                for (const auto& ComponentHit : Radial.ComponentHits)
                {
                    if (UPrimitiveComponent* Component = ComponentHit.Component.Get(); Component && Component->GetOwner() == Target)
                        Component->ReceiveComponentDamage(Notice.Command.Amount, Notice.Event.Get(), Instigator(), Causer());
                    if (!Current()) break;
                }
                if (!Current()) continue;
            }
            Target->ReceiveAnyDamage(Damage, DamageType, Instigator(), Causer());
            if (!Current()) continue;
            Target->OnTakeAnyDamage.Broadcast(Target, Damage, DamageType, Instigator(), Causer());
            if (!Current()) continue;
            if (AController* Controller = Instigator()) Controller->InstigatedAnyDamage(Damage, DamageType, Target, Causer());
            if (!Current()) continue;
            if (auto* Enemy = Cast<ADWEnemyCharacter>(Target); Enemy && Instigator() && Instigator() != Enemy->GetController()) Enemy->LastHitBy = Instigator();
        }
        if (auto* Enemy = Cast<ADWEnemyCharacter>(Target))
        {
            if(Notice.bPurePush){if(Enemy->IsAlive())Enemy->StopAIMovement();continue;}
            if (Notice.bFatal) Enemy->PublishCombatDeathEffects();
            else
            {
                Enemy->PublishDaggerDamageAlertEffects();if(!Current())continue;
                if (AController* Controller = Instigator(); Enemy->CombatProfile==EDWEnemyCombatProfile::Legacy && Controller && Controller->IsPlayerController() && !Enemy->TargetPawn.IsValid() && Enemy->IsAlive()) Enemy->BeginAlert(Controller->GetPawn());
            }
            if (!Current()) continue;
            if (Enemy->CombatProfile==EDWEnemyCombatProfile::Legacy && Enemy->IsAlive() && Notice.Command.KnockbackDistance > 0.f)
                Enemy->ApplyCombatKnockback(Notice.Command.KnockbackDirection, Notice.Command.KnockbackDistance, Notice.Command.KnockbackSeconds);
        }
        else if (auto* Nest = Cast<ADWEnemyNest>(Target); Nest && Notice.bFatal) Nest->PublishCombatDeathEffects();
        else if(auto* Player=Cast<ADWPlayerCharacter>(Target);Player&&Notice.bFatal)Player->PublishCombatDeathEffects();
    }
    if (IsOperational() && State->Epoch == DrainingEpoch)
    {
        State->bDraining = false;
        if (State->ReadNotice == State->Notices.Num()) { State->Notices.Reset(); State->ReadNotice = 0; }
        else if (State->ReadNotice > 0) { State->Notices.RemoveAt(0, State->ReadNotice, EAllowShrinking::No); State->ReadNotice = 0; }
    }
}

bool UDWCombatAuthoritySubsystem::SubmitRootMineExplosion(ADWCombatMine& Mine)
{
    using namespace DWCombatCommit;
    if(!IsOperational()||GetWorld()->IsPaused())return false;
    auto* Mines=UDWCombatMineSubsystem::Get(this);FDWCombatMineExplosion Explosion;
    if(!Mines||!Mines->PrepareExplosion(Mine,Explosion)||!Mines->ExplosionCurrent(Explosion)||
       !IsActionOwnedBy(Explosion.Root.Action,Explosion.Player.Get()))return false;
    // RootMine is a dedicated native envelope. It is not Melee, backstab, weapon fire, or LegacyExternal.
    // Collision/faction/LOS/value/life preparation is native and callback-free. The usual engine
    // damage delegates remain post-commit; no overridable ShouldTakeDamage hook runs in this batch.
    const FRootMineDamage Identity{Explosion.Emitter};
    FDamageEvent Event(UDamageType::StaticClass());TArray<FReceiptId> Receipts;TArray<uint64> Entities;
    for(const auto& Target:Explosion.Targets)
    {
        FDWCombatDamageCommand Command;Command.Source=Explosion.Player;Command.Target=Target.Target;
        Command.TargetLife=Target.Life;Command.Amount=static_cast<float>(Explosion.Config.Damage);
        Command.Instigator=Explosion.Player->GetController();
        Command.KnockbackDirection=(Target.Target->GetActorLocation()-Explosion.Center).GetSafeNormal2D();
        Command.KnockbackDistance=static_cast<float>(Explosion.Config.KnockbackCm);
        Command.KnockbackSeconds=static_cast<float>(Explosion.Config.KnockbackSeconds);
        const auto Admission=AdmitPrepared(Command,Event,false,&Identity,Explosion.Root.SourceLife);
        if(Admission.Admission==EAdmission::Admitted&&Admission.Receipt.State==EReceiptState::Pending)
        {Receipts.Add(Admission.Receipt.Handle);Entities.Add(Target.Life.Entity);}
    }
    // No external function was invoked since the validated snapshot; one trigger consumes one mine.
    Mines->CommitExplosionFacts(Explosion,Entities);
    for(const auto Receipt:Receipts)CommitPending(Receipt,EOutcome::Committed);
    DrainNotifications(); // All victim health/KB/death facts and ring dedup precede the first delegate.
    return true; // No source/mine dereference after dynamic listeners.
}

bool UDWCombatAuthoritySubsystem::CommitFormalShotgun(UDWWeaponAnimationComponent& W,EDWSpearForm Form)
{
    using namespace DWCombatCommit;using namespace DWCombatAuthority;
    auto* P=W.Player();const auto Action=W.MagazineResourceAction;
    if(!IsOperational()||GetWorld()->IsPaused()||!P||!W.IsCurrentFormalShotgun()||!W.bFormalShotgunActive||
        W.ShotgunPlan.Phase!=EDWShotgunPhase::Windup||W.ShotgunPlan.Remaining>0.||!W.CanUseShotgunResources()||!IsActionOwnedBy(Action,P))return false;
    const int32 Before=W.CombatWeaponResources.Snapshot.LoadedShotgunShells,Cost=W.CombatWeaponResources.ShotgunRoundsPerShot;
    if(Cost<1||Before<Cost||W.CombatWeaponResources.Revision==MAX_int64)return false;
    TArray<FDWShotgunTarget> Hits;FDWShotgunQuery::Collect(GetWorld(),P,P->GetActorLocation(),W.ShotgunForward,W.ShotgunConfig,Form,Hits);
    const auto Resource=State->Ledger.AdmitResource({Action,1,EResourceKind::WeaponUse,1});
    if(Resource.Result!=EAdmission::Admitted)return false;
    const auto SourceLife=CaptureActorLife(P);TArray<FReceiptId> Pending;
    for(const auto& Hit:Hits)
    {
        AActor* Target=Hit.Target.Get();if(!Target||!NativeTargetAlive(Target)||!FMath::IsFinite(Hit.Damage)||Hit.Damage<=0.||Hit.Damage>MAX_flt)continue;
        const auto Life=CaptureActorLife(Target);
        const FActionDamage Identity{Action,1,EActionDamageKind::Shotgun,1};
        const auto Admission=State->Ledger.AdmitDamage({Identity,Life});if(Admission.Result!=EAdmission::Admitted)continue;
        FDamageSnapshot S;S.Receipt=Admission.Receipt.Handle;S.Command.Identity=Identity;S.Command.Source=P;S.Command.Target=Target;S.Command.TargetLife=Life;
        S.Command.Instigator=P->GetController();S.Command.Amount=static_cast<float>(Hit.Damage);
        S.Command.KnockbackDirection=W.ShotgunForward;S.Command.KnockbackDistance=static_cast<float>(Hit.Knockback);S.Command.KnockbackSeconds=static_cast<float>(W.ShotgunConfig.KnockbackSeconds);
        S.PreparedDamageAmount=S.NotificationAmount=S.Command.Amount;S.SourceLife=SourceLife;S.bSourceWasPlayer=true;
        S.TargetRevision=State->Actors.FindChecked(Target).RestoreRevision;S.bPrepared=S.bEngineNotifications=true;
        FDamageEvent Event(UDamageType::StaticClass());S.Event.Capture(Event);Pending.Add(S.Receipt);State->Damage.Add(S.Receipt.Id,MoveTemp(S));
    }
    FDWCombatWeaponResourceNotice Notice;Notice.Owner=P;Notice.Weapon=&W;Notice.Inventory=P->GetInventory();Notice.OwnerLife=SourceLife;
    Notice.Operation=EDWWeaponResourceOperation::MagazineShot;Notice.Receipt=Resource.Receipt.Handle;
    Notice.WeaponRestoreEpoch=W.CombatWeaponResources.RestoreEpoch;Notice.WeaponRevision=W.CombatWeaponResources.Revision+1;
    Notice.LoadedBefore=Before;Notice.LoadedAfter=Before-Cost;
    Notice.bPublishCommittedAttack=true;Notice.CommittedWeapon=static_cast<uint8>(EDWAnimatedWeapon::Shotgun);Notice.CommittedRole=TEXT("Attack");Notice.CommittedEvent=0;
    State->Notices.Reserve(State->Notices.Num()+Pending.Num()+1);
    if(State->Ledger.Resolve(Resource.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved)
    {for(const auto Receipt:Pending)CommitPending(Receipt,EOutcome::Rejected);return false;}
    W.CombatWeaponResources.Snapshot.LoadedShotgunShells=Before-Cost;++W.CombatWeaponResources.Revision;
    W.AmmoInMagazine=Before-Cost;W.LastWeaponResourceReceipt=Resource.Receipt.Handle;
    W.ShotgunPlan.Fired(Form);++W.DispatchedEventCount;W.bMagazineShotEventDue=false;
    // One native shot adopts ammunition and all victim facts before any listener can re-enter.
    for(const auto Receipt:Pending)CommitPending(Receipt,EOutcome::Committed);
    EnqueueCommittedWeaponResource(Notice);DrainNotifications();return true;
}

FDWCombatDamageResult UDWCombatAuthoritySubsystem::SubmitSporeImpact(ADWCombatSporeProjectile& Bullet,const FHitResult& Hit)
{
    using namespace DWCombatCommit;
    if(!IsOperational()||Bullet.GetWorld()!=GetWorld()||!Bullet.bReleased||!Bullet.bSpent||
        Bullet.Emitter.Epoch!=GetWorldEpoch()||!Bullet.Emitter.Id||!Bullet.SourceLife.Entity||!Bullet.Shot.IsValid())return {};
    auto* Target=Hit.GetActor();
    if(auto* E=Cast<ADWEnemyCharacter>(Target)){if(!E->IsAlive())return {};}
    else if(auto* N=Cast<ADWEnemyNest>(Target)){if(N->bAmbientSpawner||!N->IsAlive())return {};}
    else return {}; // Town NPCs and the player can block a bullet, but never receive player weapon damage.
    if(!UDWDamageVolumeLibrary::IsDamageHitComponent(Hit.GetComponent()))return {};
    FDWCombatDamageCommand Command;Command.Source=Bullet.Source;Command.Target=Target;Command.TargetLife=CaptureActorLife(Target);
    Command.Instigator=Bullet.SourceController;Command.Amount=static_cast<float>(Bullet.Shot.Damage);
    Command.KnockbackDirection=Bullet.Direction;Command.KnockbackDistance=static_cast<float>(Bullet.Shot.KnockbackCm);
    Command.KnockbackSeconds=static_cast<float>(Bullet.Shot.KnockbackSeconds);
    const FEmitterDamage Identity{Bullet.Emitter,EEmitterDamageKind::ProjectileImpact,1};
    FPointDamageEvent Event(Command.Amount,Hit,Bullet.Direction,UDamageType::StaticClass());
    const auto Admitted=AdmitPrepared(Command,Event,false,nullptr,Bullet.SourceLife,&Identity);
    if(Admitted.Admission!=EAdmission::Admitted||Admitted.Receipt.State!=EReceiptState::Pending)return Admitted;
    const auto Result=CommitPending(Admitted.Receipt.Handle,EOutcome::Committed);if(IsOperational())State->Ledger.RetireEmitter(Bullet.Emitter);DrainNotifications();return Result;
}
void UDWCombatAuthoritySubsystem::RetireBottleProjectile(const ADWCombatBottleProjectile& Bottle)
{
    if(IsOperational()&&Bottle.GetWorld()==GetWorld()&&Bottle.bReleased&&!Bottle.bAreaOwned)State->Ledger.RetireEmitter(Bottle.Emitter);
}
void UDWCombatAuthoritySubsystem::RetireSporeProjectile(const ADWCombatSporeProjectile& Bullet)
{
    if(IsOperational()&&Bullet.GetWorld()==GetWorld()&&Bullet.bReleased)State->Ledger.RetireEmitter(Bullet.Emitter);
}

FDWCombatDamageResult UDWCombatAuthoritySubsystem::SubmitDaggerBackstab(UDWWeaponAnimationComponent& W,ADWEnemyCharacter& Enemy,DWCombatCommit::FActionId Action)
{
    using namespace DWCombatCommit;auto* P=W.Player();
    if(!IsOperational()||!P||!W.IsCurrentFormalDagger(Action)||W.DaggerPlan.GetPhase()!=EDWSpearPhase::Active||
        W.DaggerBackstabTarget.Get()!=&Enemy||!IsActionOwnedBy(Action,P)||
        !Enemy.IsEligibleForDaggerBackstab(P,W.DaggerTuning.BackArcDegrees))return {};
    FDWSpearHitCandidate Hit;const auto& T=W.DaggerTuning;
    if(!FDWSpearHitQuery::EvaluateSector(GetWorld(),P,&Enemy,P->GetActorLocation(),W.DaggerForward,T.Attack.RangeCm,T.Attack.FullArcDegrees,Hit))return {};
    const double Final=Enemy.CombatProfile==EDWEnemyCombatProfile::DoughElite?Enemy.MaxHealth*T.EliteMaxHealthFraction:Enemy.Health;
    if(!FMath::IsFinite(Final)||Final<=0.||Final>MAX_flt)return {};
    FDWCombatDamageCommand C;C.Identity={Action,1,EActionDamageKind::Melee,1};C.Source=P;C.Target=&Enemy;
    C.TargetLife=CaptureActorLife(&Enemy);C.Instigator=P->GetController();C.Amount=static_cast<float>(Final);
    C.KnockbackDistance=0.f;C.KnockbackSeconds=static_cast<float>(T.Attack.KnockbackSeconds);C.KnockbackDirection=W.DaggerForward;
    const FVector Origin=Enemy.GetActorLocation();FDamageEvent Event(UDamageType::StaticClass());
    const auto Admitted=AdmitPrepared(C,Event,false,nullptr,{},nullptr,true);
    if(Admitted.Admission!=EAdmission::Admitted||Admitted.Receipt.State!=EReceiptState::Pending)return Admitted;
    // No overridable hook is called for final backstab damage. Freeze refresh before committing health.
    auto* Snapshot=State->Damage.Find(Admitted.Receipt.Handle.Id);if(!Snapshot)return {};
    Snapshot->DaggerVictimOrigin=Origin;Snapshot->DaggerRefreshRadius=T.RefreshRadiusCm;Snapshot->DaggerGraceSeconds=T.GraceSeconds;
    const auto Result=CommitPending(Admitted.Receipt.Handle,EOutcome::Committed);DrainNotifications();return Result;
}

void UDWCombatAuthoritySubsystem::SubmitGroundAreaTickBatch(const TArray<FDWGroundAreaTick>& Batch)
{
    using namespace DWCombatCommit;using namespace DWCombatAuthority;
    if(!IsOperational()||GetWorld()->IsPaused())return;TArray<FReceiptId> Pending;
    for(const auto& T:Batch)
    {
        auto* E=T.Target.Get();if(!E||!E->IsAlive()||E->IsActorBeingDestroyed()||E->GetWorld()!=GetWorld()||!E->CanBeDamaged()||
            !T.SourceLife.Entity||!T.SourceLife.Life||!T.Ordinal||!FMath::IsFinite(T.Damage)||T.Damage<=0.f||
            !Same(CaptureActorLife(E),T.TargetLife))continue;
        const FEmitterDamage Identity{T.Emitter,EEmitterDamageKind::AreaTick,T.Ordinal};const auto R=State->Ledger.AdmitDamage({Identity,T.TargetLife});
        if(R.Result!=EAdmission::Admitted)continue;
        FDamageSnapshot S;S.Receipt=R.Receipt.Handle;S.Command.Source=T.Source;S.Command.Instigator=T.Instigator;S.Command.Target=T.Target;
        S.Command.TargetLife=T.TargetLife;S.Command.Amount=T.Damage;S.SourceLife=T.SourceLife;S.bSourceWasPlayer=true;
        S.PreparedDamageAmount=S.NotificationAmount=T.Damage;S.bPrepared=S.bEngineNotifications=true;S.TargetRevision=State->Actors.FindChecked(E).RestoreRevision;
        FDamageEvent Event(UDamageType::StaticClass());S.Event.Capture(Event);Pending.Add(S.Receipt);State->Damage.Add(S.Receipt.Id,MoveTemp(S));
    }
    State->Notices.Reserve(State->Notices.Num()+Pending.Num());for(const auto Receipt:Pending)CommitPending(Receipt,EOutcome::Committed);
    DrainNotifications();
}

void UDWCombatAuthoritySubsystem::SubmitBowImpact(ADWCombatBowProjectile& Arrow,const FHitResult& Hit,double ImpactSeconds)
{
    using namespace DWCombatCommit;using namespace DWCombatAuthority;
    if(!IsOperational()||GetWorld()->IsPaused()||Arrow.GetWorld()!=GetWorld()||!Arrow.bReleased||!Arrow.bSpent||
        !Arrow.Shot.IsValid()||!Arrow.SourceLife.Entity||!State->Ledger.IsEmitterActive(Arrow.Emitter))return;
    auto* Areas=UDWCombatGroundAreaSubsystem::Get(this);FVector Ground=FVector::ZeroVector;
    const bool GroundValid=Areas&&Areas->FindLegalGround(GetWorld(),Hit.ImpactPoint,Arrow.Source.Get(),Ground);
    const FVector Center=GroundValid?Ground:FVector(Hit.ImpactPoint);
    const FEmitterDamage Identity{Arrow.Emitter,EEmitterDamageKind::ProjectileImpact,1};
    AActor* Direct=Hit.GetActor();if(!NativeTargetAlive(Direct)||!Direct->CanBeDamaged()||!UDWDamageVolumeLibrary::IsDamageHitComponent(Hit.GetComponent()))Direct=nullptr;
    TArray<FReceiptId> Damage;TArray<FDamageSnapshot> PushNotices;
    if(Direct&&(Direct->IsA<ADWEnemyCharacter>()||Direct->IsA<ADWEnemyNest>()))
    {
        const auto Life=CaptureActorLife(Direct);const auto R=State->Ledger.AdmitDamage({Identity,Life});
        if(R.Result==EAdmission::Admitted)
        {
            FDamageSnapshot S;S.Receipt=R.Receipt.Handle;S.Command.Source=Arrow.Source;S.Command.Target=Direct;S.Command.TargetLife=Life;
            S.Command.Instigator=Arrow.SourceController;S.Command.Amount=static_cast<float>(Arrow.Shot.Damage);
            S.Command.KnockbackDirection=(Direct->GetActorLocation()-Center).GetSafeNormal2D();if(S.Command.KnockbackDirection.IsNearlyZero())S.Command.KnockbackDirection=Arrow.Direction;
            S.Command.KnockbackDistance=static_cast<float>(Arrow.Shot.KnockbackCm);S.Command.KnockbackSeconds=static_cast<float>(Arrow.Shot.KnockbackSeconds);
            S.SourceLife=Arrow.SourceLife;S.bSourceWasPlayer=true;S.PreparedDamageAmount=S.NotificationAmount=S.Command.Amount;
            S.bPrepared=S.bEngineNotifications=true;S.TargetRevision=State->Actors.FindChecked(Direct).RestoreRevision;
            FPointDamageEvent Event(S.Command.Amount,Hit,Arrow.Direction,UDamageType::StaticClass());S.Event.Capture(Event);
            Damage.Add(S.Receipt);State->Damage.Add(S.Receipt.Id,MoveTemp(S));
        }
    }
    if(Areas)
    {
        UDWCombatGroundAreaSubsystem::FArea Radial;Radial.Ground=Center;Radial.Source=Arrow.Source;Radial.Started=ImpactSeconds;
        Radial.Ends=ImpactSeconds+Arrow.Shot.Area.DurationSeconds;Radial.RadiusCm=Arrow.Shot.Area.RadiusCm;
        for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
        {
            auto* E=*It;if(E==Direct||!E->CanBeDamaged()||!Areas->Covers(Radial,*E,ImpactSeconds,true))continue;
            const auto Life=CaptureActorLife(E);const auto R=State->Ledger.AdmitDamage({Identity,Life});if(R.Result!=EAdmission::Admitted)continue;
            FDamageSnapshot S;S.Receipt=R.Receipt.Handle;S.Command.Source=Arrow.Source;S.Command.Instigator=Arrow.SourceController;
            S.Command.Target=E;S.Command.TargetLife=Life;S.SourceLife=Arrow.SourceLife;S.bSourceWasPlayer=true;S.bPurePush=true;
            S.Command.KnockbackDirection=(E->GetActorLocation()-Center).GetSafeNormal2D();if(S.Command.KnockbackDirection.IsNearlyZero())S.Command.KnockbackDirection=Arrow.Direction;
            S.Command.KnockbackDistance=static_cast<float>(Arrow.Shot.KnockbackCm);S.Command.KnockbackSeconds=static_cast<float>(Arrow.Shot.KnockbackSeconds);
            S.TargetRevision=State->Actors.FindChecked(E).RestoreRevision;FDamageEvent Event(UDamageType::StaticClass());S.Event.Capture(Event);PushNotices.Add(MoveTemp(S));
        }
    }
    State->Notices.Reserve(State->Notices.Num()+Damage.Num()+PushNotices.Num());
    // All direct HP and all nearby push facts precede the first damage or movement listener.
    for(const auto Receipt:Damage)CommitPending(Receipt,EOutcome::Committed);
    for(auto& S:PushNotices)
    {
        auto* E=Cast<ADWEnemyCharacter>(S.Command.Target.Get());if(!E||!E->IsAlive()||!IsNoticeRecipientCurrent(E,S.TargetRevision))
        {State->Ledger.Resolve(S.Receipt,EOutcome::Rejected);continue;}
        const auto K=E->PrepareFormalKnockback(S.Command.KnockbackDirection,S.Command.KnockbackDistance,S.Command.KnockbackSeconds);
        if(State->Ledger.Resolve(S.Receipt,EOutcome::Committed)!=EResolve::Resolved)continue;
        if(E->IsFormalCombatReady())E->CommitFormalKnockbackFacts(K,S.Receipt,
            S.bSourceWasPlayer?S.SourceLife:FEntityLife{},S.Command.TargetLife);
        else if(E->CombatProfile==EDWEnemyCombatProfile::Legacy&&S.Command.KnockbackDistance>0.f)
        {
            // Same native assignments as legacy knockback, with notifications deferred to the shared FIFO.
            if(E->FormalKnockbackFrame!=GFrameCounter||S.Command.KnockbackDistance>E->FormalKnockbackInitialDistance)
            {
                E->FormalKnockbackFrame=GFrameCounter;E->FormalKnockbackInitialDistance=S.Command.KnockbackDistance;
                E->AttackPhase=EDWCombatPhase::None;E->AttackPhaseEndTime=0.f;E->MeleeTarget.Reset();E->bAttackResolved=true;
                E->bIsCombatKnockedBack=true;E->KnockbackDirection=S.Command.KnockbackDirection;
                E->KnockbackDistanceRemaining=S.Command.KnockbackDistance;E->KnockbackTimeRemaining=S.Command.KnockbackSeconds;
                E->KnockbackSpeed=S.Command.KnockbackDistance/S.Command.KnockbackSeconds;
                E->NextPathRequestTime=GetWorld()->GetTimeSeconds()+S.Command.KnockbackSeconds;E->NextAttackTime=FMath::Max(E->NextAttackTime,E->NextPathRequestTime);
            }
        }
        State->Notices.Add(FDWCombatAuthorityState::FNotice(MoveTemp(S)));
    }
    if(GroundValid)
    {
        const auto Adopted=Areas->AdoptReleasedBowSlow(Arrow.Shot.Area,Arrow.Source,Arrow.SourceController,Arrow.SourceLife,Arrow.Emitter,Ground,ImpactSeconds);
        Arrow.bAreaOwned=Adopted!=EDWAreaAdoption::Invalid; // No callback occurred during adoption.
    }
    if(!Arrow.bAreaOwned)State->Ledger.RetireEmitter(Arrow.Emitter);
    DrainNotifications();
}
void UDWCombatAuthoritySubsystem::RetireBowProjectile(const ADWCombatBowProjectile& Arrow)
{if(IsOperational()&&Arrow.GetWorld()==GetWorld()&&Arrow.bReleased&&!Arrow.bAreaOwned)State->Ledger.RetireEmitter(Arrow.Emitter);}

namespace DWCombatEffectsAuthority
{
static EDWCombatSavedActorKind Kind(const AActor* A)
{
    if(A->IsA<ADWPlayerCharacter>())return EDWCombatSavedActorKind::Player;
    if(A->IsA<ADWEnemyCharacter>())return EDWCombatSavedActorKind::Enemy;
    if(A->IsA<ADWEnemyNest>())return EDWCombatSavedActorKind::Nest;
    if(A->IsA<AController>())return EDWCombatSavedActorKind::Controller;return EDWCombatSavedActorKind::Other;
}
static bool Bindings(UWorld* World,const TArray<FDWCombatObjectBinding>& B,FString& Error)
{
    TSet<TWeakObjectPtr<AActor>> Actors;TSet<FString> Names;
    for(const auto& V:B)
    {if(V.StableId.IsEmpty()||V.StableId.Len()>4096||V.StableId.Len()!=FCString::Strlen(*V.StableId)||Names.Contains(V.StableId)||!V.Actor.IsValid()||V.Actor->IsActorBeingDestroyed()||V.Actor->GetWorld()!=World||!V.Actor->HasAuthority()||Actors.Contains(V.Actor))
        {Error=TEXT("Invalid, duplicate, or ambiguous persistent binding");return false;}Actors.Add(V.Actor);Names.Add(V.StableId);}
    return true;
}
static AActor* Resolve(const TArray<FDWCombatObjectBinding>& B,const FString& Key)
{for(const auto& V:B)if(V.StableId.Equals(Key,ESearchCase::CaseSensitive))return V.Actor.Get();return nullptr;}
}
struct FDWCombatAuthorityRestoreState
{
    DWCombatCommit::FLedgerRestorePlan Ledger;
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Owner;
    TArray<FDWCombatActorIdentitySave> Saved;
    TArray<FDWCombatObjectBinding> Bindings;
    TMap<TWeakObjectPtr<AActor>,DWCombatAuthority::FRegistration> Actors;
    TMap<uint64,FDWCombatActorIdentitySave> Tombstones;
    TMap<uint64,DWCombatAuthority::FActionOwner> Actions;
    TArray<DWCombatCommit::FActionId> KeptActions;
    TMap<uint64,DWCombatCommit::FEntityLife> KeptOwners;
    uint64 LastEntity=0,OldEpoch=0,NewEpoch=0;
    bool bCommitted=false;
};
void FDWCombatAuthorityRestoreStateDeleter::operator()(FDWCombatAuthorityRestoreState* P)const{delete P;}
FDWCombatAuthorityRestorePlan::FDWCombatAuthorityRestorePlan()=default;
FDWCombatAuthorityRestorePlan::~FDWCombatAuthorityRestorePlan()=default;
FDWCombatAuthorityRestorePlan::FDWCombatAuthorityRestorePlan(FDWCombatAuthorityRestorePlan&&)=default;
FDWCombatAuthorityRestorePlan& FDWCombatAuthorityRestorePlan::operator=(FDWCombatAuthorityRestorePlan&&)=default;
uint64 FDWCombatAuthorityRestorePlan::NewEpoch()const{return State?State->NewEpoch:0;}
TArray<DWCombatCommit::FReceiptId> FDWCombatAuthorityRestorePlan::PendingDamageReceipts()const
{TArray<DWCombatCommit::FReceiptId> Out;if(State)for(const auto R:State->Ledger.PendingDamage())Out.Add(R);return Out;}
std::optional<DWCombatCommit::FReceipt> FDWCombatAuthorityRestorePlan::FindReceipt(uint64 Id)const
{return State?State->Ledger.FindReceipt(Id):std::nullopt;}
AActor* FDWCombatAuthorityRestorePlan::FindActor(uint64 Entity)const
{if(State)for(const auto& V:State->Saved)if(V.Life.Entity==Entity)return FindActor(V.StableId);return nullptr;}
AActor* FDWCombatAuthorityRestorePlan::FindActor(const FString& Id)const
{return State?DWCombatEffectsAuthority::Resolve(State->Bindings,Id):nullptr;}
DWCombatCommit::FEntityLife FDWCombatAuthorityRestorePlan::FindLife(uint64 Entity)const
{if(State)for(const auto& V:State->Saved)if(V.Life.Entity==Entity)return V.Life;return {};}
uint64 FDWCombatAuthorityRestorePlan::FindRestoreRevision(uint64 Entity)const
{if(State)for(const auto& V:State->Saved)if(V.Life.Entity==Entity)return V.RestoreRevision;return 0;}
TArray<DWCombatCommit::FActionId> FDWCombatAuthorityRestorePlan::ActiveActions()const
{return State?State->KeptActions:TArray<DWCombatCommit::FActionId>{};}
DWCombatCommit::FEntityLife FDWCombatAuthorityRestorePlan::FindActionOwner(uint64 Id)const
{if(State)if(const auto* Owner=State->KeptOwners.Find(Id))return *Owner;return {};}
DWCombatCommit::FEntityLife UDWCombatAuthoritySubsystem::FindRegisteredActorLife(const AActor* Actor)const
{if(State&&Actor)if(const auto* R=State->Actors.Find(const_cast<AActor*>(Actor)))return R->Identity;return {};}
bool UDWCombatAuthoritySubsystem::CaptureWorldIdentity(const TArray<FDWCombatObjectBinding>& B,FDWCombatAuthoritySave& Out,FString& Error)
{
    if(!IsOperational()||!DWCombatEffectsAuthority::Bindings(GetWorld(),B,Error))return false;
    for(const auto& V:B)
    {
        if(!CaptureActorLife(V.Actor.Get()).Entity){Error=TEXT("Cannot register persistent object");return false;}
        auto& R=State->Actors.FindChecked(V.Actor);if(!R.StableId.IsEmpty()&&!R.StableId.Equals(V.StableId,ESearchCase::CaseSensitive)){Error=TEXT("Persistent identity changed in same world");return false;}
        R.StableId=V.StableId;R.SavedKind=static_cast<uint8>(DWCombatEffectsAuthority::Kind(V.Actor.Get()));
    }
    FDWCombatAuthoritySave S;S.LastEntity=State->LastEntity;
    for(const auto& P:State->Actors)
    {
        const auto& R=P.Value;FDWCombatActorIdentitySave V;V.StableId=R.StableId;V.Life=R.Identity;V.RestoreRevision=R.RestoreRevision;
        V.Kind=static_cast<EDWCombatSavedActorKind>(R.SavedKind);V.bDead=R.bDead;V.bEnded=R.bEnded;
        if(V.bEnded&&V.StableId.IsEmpty())V.StableId=FString::Printf(TEXT("tombstone:%llu:%llu"),State->Epoch,V.Life.Entity);
        if(!V.bEnded&&(!P.Key.IsValid()||DWCombatEffectsAuthority::Resolve(B,V.StableId)!=P.Key.Get())){Error=TEXT("Missing live persistent object binding");return false;}
        S.Actors.Add(MoveTemp(V));
    }
    for(const auto& P:State->RestoredTombstones)S.Actors.Add(P.Value);
    S.Actors.Sort([](const auto& Left,const auto& Right){return Left.Life.Entity<Right.Life.Entity;});S.Ledger=State->Ledger.CaptureSnapshot();Out=MoveTemp(S);return true;
}
bool UDWCombatAuthoritySubsystem::PrepareWorldIdentityRestore(const FDWCombatAuthoritySave& Save,const TArray<FDWCombatObjectBinding>& B,
    const TArray<uint64>& Keep,FDWCombatAuthorityRestorePlan& Out,FString& Error)
{
    if(!IsOperational()||!DWCombatEffectsSave::ValidateAuthority(Save,Error)||!DWCombatEffectsAuthority::Bindings(GetWorld(),B,Error))return false;
    uint64 Next=FMath::Max(DWCombatAuthority::LastWorldEpoch,Save.Ledger.Epoch);
    if(!DWCombatAuthority::Next(Next)){Error=TEXT("World epoch exhausted");return false;}
    auto Candidate=MakeUnique<FDWCombatAuthorityRestoreState>();Candidate->Owner=this;Candidate->Saved=Save.Actors;Candidate->Bindings=B;
    Candidate->LastEntity=FMath::Max(State->LastEntity,Save.LastEntity);Candidate->OldEpoch=State->Epoch;Candidate->NewEpoch=Next;
    std::vector<DWCombatCommit::FId> KeepNative;for(uint64 Id:Keep)KeepNative.push_back(Id);std::string NativeError;
    if(!State->Ledger.PrepareSnapshotRestore(Save.Ledger,Next,KeepNative,Candidate->Ledger,NativeError)){Error=UTF8_TO_TCHAR(NativeError.c_str());return false;}
    for(const auto& V:Save.Actors)
    {
        auto* Actor=DWCombatEffectsAuthority::Resolve(B,V.StableId);
        if(V.bEnded){if(Actor){Error=TEXT("Tombstone cannot bind a live actor");return false;}Candidate->Tombstones.Add(V.Life.Entity,V);continue;}
        if(!Actor||DWCombatEffectsAuthority::Kind(Actor)!=V.Kind){Error=TEXT("Missing or wrong-class restored actor");return false;}
        DWCombatAuthority::FRegistration R;R.Identity=V.Life;R.RestoreRevision=V.RestoreRevision;R.bDead=V.bDead;R.bEnded=false;R.StableId=V.StableId;R.SavedKind=static_cast<uint8>(V.Kind);Candidate->Actors.Add(Actor,MoveTemp(R));
    }
    for(uint64 Id:Keep)
    {
        const auto I=std::find_if(Save.Ledger.Actions.begin(),Save.Ledger.Actions.end(),[&](const auto& V){return V.Id==Id;});
        AActor* Actor=nullptr;for(const auto& V:Save.Actors)if(V.Life.Entity==I->Owner.Entity)Actor=DWCombatEffectsAuthority::Resolve(B,V.StableId);
        if(!Actor){Error=TEXT("Kept action has no live owner");return false;}const auto* R=Candidate->Actors.Find(Actor);
        if(!R||R->bDead||R->bEnded||!DWCombatAuthority::Same(R->Identity,I->Owner)){Error=TEXT("Kept action owner is dead or old life");return false;}Candidate->Actions.Add(Id,{Actor,I->Owner});Candidate->KeptActions.Add({Next,Id});Candidate->KeptOwners.Add(Id,I->Owner);
    }
    // Reserve a unique epoch even for an abandoned candidate; no later candidate can accidentally share its callback identity.
    DWCombatAuthority::LastWorldEpoch=Next;FDWCombatAuthorityRestorePlan Plan;Plan.State.Reset(Candidate.Release());Out=MoveTemp(Plan);return true;
}
bool UDWCombatAuthoritySubsystem::CanCommitWorldIdentity(const FDWCombatAuthorityRestorePlan& P,FString& Error)const
{
    if(!IsOperational()||!P.State||P.State->Owner.Get()!=this||P.State->bCommitted||P.State->OldEpoch!=State->Epoch||!State->Ledger.CanCommitSnapshotRestore(P.State->Ledger))
    {Error=TEXT("Authority restore candidate stale");return false;}
    if(!DWCombatEffectsAuthority::Bindings(GetWorld(),P.State->Bindings,Error))return false;
    return true;
}
bool UDWCombatAuthoritySubsystem::BeginWorldRestoreBarrier(FDWCombatAuthorityRestorePlan& P,FString& Error)
{if(!CanCommitWorldIdentity(P,Error))return false;State->bRestoring=true;State->bRestoreFactsCommitted=false;return true;}
void UDWCombatAuthoritySubsystem::AbortWorldRestoreBarrier()
{if(State&&!State->bRestoreFactsCommitted)State->bRestoring=false;}
void UDWCombatAuthoritySubsystem::CommitWorldIdentityUnchecked(FDWCombatAuthorityRestorePlan& P)
{
    check(State&&State->bRestoring&&P.State&&P.State->Owner.Get()==this&&!P.State->bCommitted);
    auto& C=*P.State;State->Ledger.CommitSnapshotRestoreUnchecked(MoveTemp(C.Ledger));State->Epoch=C.NewEpoch;State->LastEntity=C.LastEntity;
    State->Actors=MoveTemp(C.Actors);State->RestoredTombstones=MoveTemp(C.Tombstones);State->Actions=MoveTemp(C.Actions);State->Damage.Reset();State->Notices.Reset();State->ReadNotice=0;State->bDraining=false;State->ShieldDamageRevision=0;
    for(const auto& Pair:State->Actors)if(auto* Actor=Pair.Key.Get())Actor->OnEndPlay.AddUniqueDynamic(this,&UDWCombatAuthoritySubsystem::HandleActorEndPlay);
    State->bRestoreFactsCommitted=true;C.bCommitted=true; // Root must adopt every retained pending payload before releasing the barrier.
}
void UDWCombatAuthoritySubsystem::FinishWorldRestoreBarrier()
{if(State&&State->bRestoreFactsCommitted){State->bRestoring=false;State->bRestoreFactsCommitted=false;}}

// D22 pending shield payloads. Included in the authority translation unit, beside its private snapshots.
struct FDWCombatShieldRestoreState
{
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Owner;
    TMap<uint64,DWCombatAuthority::FDamageSnapshot> Damage;
    uint64 NewEpoch=0;
    bool bAdopted=false;
};
void FDWCombatShieldRestoreStateDeleter::operator()(FDWCombatShieldRestoreState* P)const{delete P;}
FDWCombatShieldRestorePlan::FDWCombatShieldRestorePlan()=default;
FDWCombatShieldRestorePlan::~FDWCombatShieldRestorePlan()=default;
FDWCombatShieldRestorePlan::FDWCombatShieldRestorePlan(FDWCombatShieldRestorePlan&&)=default;
FDWCombatShieldRestorePlan& FDWCombatShieldRestorePlan::operator=(FDWCombatShieldRestorePlan&&)=default;

bool UDWCombatAuthoritySubsystem::CaptureShieldPackets(const ADWPlayerCharacter& Player,
    const TArray<FDWCombatObjectBinding>& Bindings,TArray<FDWShieldPacketSave>& Out,FString& Error)const
{
    using namespace DWCombatCommit;using namespace DWCombatAuthority;
    if(!IsOperational()){Error=TEXT("Shield snapshot requires an operational authority.");return false;}
    TArray<FDWShieldPacketSave> Result;const double Now=GetWorld()->GetTimeSeconds();
    for(const auto& Pair:State->Damage)
    {
        const auto& D=Pair.Value;const auto R=State->Ledger.FindReceipt(D.Receipt);
        if(!R||R->State!=EReceiptState::Pending)continue;
        if(!D.bShieldDeferred||!D.bPrepared||D.bPurePush||D.bFatal||D.bShieldPerfectNow||
            D.Command.Target.Get()!=&Player||D.Event.Kind!=FDamageEvent::ClassID)
        {Error=TEXT("An unfinished non-shield damage preparation cannot be serialized.");return false;}
        FDWShieldPacketSave S;S.Receipt=D.Receipt.Id;S.TargetEntity=D.Command.TargetLife.Entity;S.TargetLife=D.Command.TargetLife.Life;
        S.SourceEntity=D.SourceLife.Entity;S.SourceLife=D.SourceLife.Life;S.bSourceWasPlayer=D.bSourceWasPlayer;
        S.Action=D.Command.Identity.Action.Id;S.Phase=D.Command.Identity.Phase;S.PacketOrdinal=D.Command.Identity.PacketOrdinal;
        S.ActionKind=static_cast<uint8>(D.Command.Identity.Kind);S.IncomingKind=static_cast<uint8>(D.Command.ShieldIncomingKind);
        if(D.Command.Identity.Kind==EActionDamageKind::EnemyMelee)S.IncomingKind=static_cast<uint8>(EDWShieldIncomingKind::Melee);
        if(auto* Instigator=D.Command.Instigator.Get())
        {
            const auto* B=Bindings.FindByPredicate([&](const auto& V){return V.Actor.Get()==Instigator;});
            if(!B){Error=TEXT("Pending damage instigator has no stable binding.");return false;}S.InstigatorId=B->StableId;
        }
        S.DamageType=FSoftClassPath(D.Event.TypeClass.Get());
        S.Amount=D.Command.Amount;S.PreparedAmount=D.PreparedDamageAmount;S.NotificationAmount=D.NotificationAmount;
        S.KnockbackDirection=D.Command.KnockbackDirection;S.KnockbackDistance=D.Command.KnockbackDistance;S.KnockbackSeconds=D.Command.KnockbackSeconds;
        S.IncomingDirection=D.ShieldImpactDirection;S.ImpactAge=FMath::Max(0.,Now-D.ShieldImpactTime);
        S.Remaining=FMath::Max(0.,D.ShieldDeadline-Now);S.bImpactRooted=D.bImpactRooted;S.bEngineNotifications=D.bEngineNotifications;
        Result.Add(MoveTemp(S));
    }
    Result.Sort([](const auto& A,const auto& B){return A.Receipt<B.Receipt;});Out=MoveTemp(Result);Error.Reset();return true;
}

bool UDWCombatAuthoritySubsystem::PrepareShieldRestore(const TArray<FDWShieldPacketSave>& Saved,
    const FDWCombatEffectsSave& Effects,const FDWCombatAuthorityRestorePlan& Identity,FDWCombatShieldRestorePlan& Out,FString& Error)
{
    using namespace DWCombatCommit;using namespace DWCombatAuthority;
    if(!IsOperational()||!Identity.NewEpoch()||Saved.Num()>65536){Error=TEXT("Invalid pending shield restore context.");return false;}
    if(!FDWCombatWorldPersistence::ValidateShieldPackets(Saved,Effects,Error))return false;
    FDWCombatShieldRestorePlan Plan;Plan.State.Reset(new FDWCombatShieldRestoreState());
    auto& P=*Plan.State;P.Owner=this;P.NewEpoch=Identity.NewEpoch();const double Now=GetWorld()->GetTimeSeconds();TSet<uint64> Seen;
    for(const auto& S:Saved)
    {
        const auto R=std::find_if(Effects.Authority.Ledger.Receipts.begin(),Effects.Authority.Ledger.Receipts.end(),
            [&](const auto& V){return V.Receipt.Handle.Id==S.Receipt;});
        if(R==Effects.Authority.Ledger.Receipts.end()||!S.Receipt||Seen.Contains(S.Receipt)||R->Resource||R->Receipt.State!=EReceiptState::Pending||
            R->Target.Entity!=S.TargetEntity||R->Target.Life!=S.TargetLife||R->Source.Entity!=S.SourceEntity||R->Source.Life!=S.SourceLife||
            R->Action.Id!=S.Action||!S.Action||!S.Phase||!S.PacketOrdinal||
            S.ActionKind>static_cast<uint8>(EActionDamageKind::EnvironmentFatal)||
            (S.IncomingKind!=static_cast<uint8>(EDWShieldIncomingKind::Melee)&&S.IncomingKind!=static_cast<uint8>(EDWShieldIncomingKind::PhysicalProjectile)))
        {Error=TEXT("Pending shield receipt provenance is invalid or duplicated.");return false;}
        for(double V:{double(S.Amount),double(S.PreparedAmount),double(S.NotificationAmount),double(S.KnockbackDistance),double(S.KnockbackSeconds),S.ImpactAge,S.Remaining})
            if(!FMath::IsFinite(V)||V<0.){Error=TEXT("Pending shield damage has an invalid numeric payload.");return false;}
        if(S.Amount<=0.f||S.KnockbackSeconds<=0.f||S.KnockbackDirection.ContainsNaN()||S.IncomingDirection.ContainsNaN()||S.DamageType.IsNull())
        {Error=TEXT("Pending shield damage has an invalid vector or damage type.");return false;}
        auto* Target=Cast<ADWPlayerCharacter>(Identity.FindActor(S.TargetEntity));
        const auto TargetLife=Identity.FindLife(S.TargetEntity);const auto TargetRevision=Identity.FindRestoreRevision(S.TargetEntity);
        const auto Receipt=Identity.FindReceipt(S.Receipt);
        if(!Target||Target->GetWorld()!=GetWorld()||TargetLife.Entity!=S.TargetEntity||TargetLife.Life!=S.TargetLife||!TargetRevision||
            !Receipt||Receipt->State!=EReceiptState::Pending)
        {Error=TEXT("Pending shield target or receipt cannot be rebound.");return false;}
        UClass* Type=S.DamageType.TryLoadClass<UDamageType>();
        if(!Type||!IsOperational()){Error=TEXT("Pending damage class could not load safely.");return false;}
        FDamageSnapshot D;D.Receipt={P.NewEpoch,S.Receipt};D.Command.Identity={{P.NewEpoch,S.Action},S.Phase,static_cast<EActionDamageKind>(S.ActionKind),S.PacketOrdinal};
        D.Command.Target=Target;D.Command.TargetLife=TargetLife;D.Command.Source=Identity.FindActor(S.SourceEntity);
        D.Command.Instigator=S.InstigatorId.IsEmpty()?nullptr:Cast<AController>(Identity.FindActor(S.InstigatorId));
        if(!S.InstigatorId.IsEmpty()&&!D.Command.Instigator.IsValid())
        {Error=TEXT("Pending shield instigator has no restored controller.");return false;}
        D.Command.Amount=S.Amount;D.Command.ShieldIncomingKind=static_cast<EDWShieldIncomingKind>(S.IncomingKind);
        D.Command.ShieldIncomingDirection=S.IncomingDirection;D.Command.KnockbackDirection=S.KnockbackDirection;
        D.Command.KnockbackDistance=S.KnockbackDistance;D.Command.KnockbackSeconds=S.KnockbackSeconds;
        D.SourceLife={S.SourceEntity,S.SourceLife};D.bSourceWasPlayer=S.bSourceWasPlayer;D.TargetRevision=TargetRevision;
        D.PreparedDamageAmount=S.PreparedAmount;D.NotificationAmount=S.NotificationAmount;D.bPrepared=true;D.bShieldDeferred=true;
        D.bEngineNotifications=S.bEngineNotifications;D.bImpactRooted=S.bImpactRooted;
        D.ShieldImpactTime=Now-S.ImpactAge;D.ShieldDeadline=Now+S.Remaining;D.ShieldImpactDirection=S.IncomingDirection;
        if(!D.Event.Capture(FDamageEvent(Type)))return false;
        P.Damage.Add(S.Receipt,MoveTemp(D));Seen.Add(S.Receipt);
    }
    const auto Pending=Identity.PendingDamageReceipts();
    if(Pending.Num()!=Seen.Num()||Pending.ContainsByPredicate([&](const auto& R){return !Seen.Contains(R.Id);}))
    {Error=TEXT("Pending authority damage must have exactly one shield payload.");return false;}
    Out=MoveTemp(Plan);Error.Reset();return true;
}

void UDWCombatAuthoritySubsystem::AdoptShieldFactsUnchecked(FDWCombatShieldRestorePlan& Plan)
{
    check(Plan.State&&Plan.State->Owner.Get()==this&&!Plan.State->bAdopted&&State&&State->Epoch==Plan.State->NewEpoch);
    check(State->Damage.IsEmpty());State->Damage=MoveTemp(Plan.State->Damage);
    ++State->ShieldDamageRevision;Plan.State->bAdopted=true;
}
