#include "DWCombatPlayerPersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatEquipment.h"
#include "DWCombatRules.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWSaveGame.h"
#include "DWWorldEvent.h"
#include "DWGameplayHUD.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include <algorithm>
#include <cmath>
#include <limits>

struct FDWCombatPlayerRestoreState
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    TWeakObjectPtr<UDWWeaponAnimationComponent> Weapon;
    TWeakObjectPtr<UDWCombatAuthoritySubsystem> Authority;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AController> Controller;
    TArray<uint64> ExpectedIds;TArray<double> ExpectedNumbers;TArray<bool> ExpectedFlags;
    FTransform ExpectedTransform,Destination;
    bool bPlace=false,bAdopted=false,bPublished=false,bFinished=false,bHadBegunPlay=false,bSavedDead=false;
    double At=0.,WeaponLock=0.,MeleeLock=0.,ThrowLock=0.,SwitchLock=0.;
    uint64 OldEpoch=0,NewEpoch=0,SavedDamageRevision=0,AdoptedWeaponRestoreEpoch=0;
    DWCombatCommit::FEntityLife OldLife,NewLife;
    FDWPreparedInventoryChange PreparedInventory;
    FDWCombatPlayerVitalsRuntime Vitals;
    FDWCombatWeaponResourceState WeaponState;
    FDWShieldAxeDefense Defense;
    FDWYeastDashPlan Dash;
    FDWMoldRootPlan Root;
    FDWMoldRootIdentity RootIdentity;
    EDWAnimatedWeapon Selection=EDWAnimatedWeapon::Unarmed;
    float Health=0.f;
};
void FDWCombatPlayerRestoreStateDeleter::operator()(FDWCombatPlayerRestoreState* P)const{delete P;}
FDWCombatPlayerRestorePlan::FDWCombatPlayerRestorePlan()=default;
FDWCombatPlayerRestorePlan::~FDWCombatPlayerRestorePlan()=default;
FDWCombatPlayerRestorePlan::FDWCombatPlayerRestorePlan(FDWCombatPlayerRestorePlan&&)=default;
FDWCombatPlayerRestorePlan& FDWCombatPlayerRestorePlan::operator=(FDWCombatPlayerRestorePlan&&)=default;
bool FDWCombatPlayerRestorePlan::CanCommit(FString& Error)const{return FDWCombatPlayerPersistence::CanCommit(*this,Error);}
const FDWMoldRootIdentity& FDWCombatPlayerRestorePlan::RestoredRootIdentity()const
{static const FDWMoldRootIdentity Empty;return State?State->RootIdentity:Empty;}
namespace
{
bool SameLife(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B){return A.Entity==B.Entity&&A.Life==B.Life;}
FDWShieldAxeDefenseSnapshot Shield(const FDWShieldDefenseSave& S)
{return {S.Schema,S.CounterCharges,S.PerfectRemaining,S.ReopenRemaining,S.bPerfectEligible,S.BaseForm};}
FDWShieldDefenseSave Shield(const FDWShieldAxeDefenseSnapshot& S)
{
    FDWShieldDefenseSave R;R.Schema=S.Schema;R.CounterCharges=S.CounterCharges;R.PerfectRemaining=S.PerfectRemainingSeconds;
    R.ReopenRemaining=S.ReopenRemainingSeconds;R.bPerfectEligible=S.bPerfectEligible;R.BaseForm=S.BaseForm;return R;
}
bool Nonnegative(double V){return FMath::IsFinite(V)&&V>=0.;}
float LegacyDeadline(double V)
{float F=static_cast<float>(V);return double(F)<V?std::nextafter(F,std::numeric_limits<float>::infinity()):F;}
}

bool FDWCombatPlayerPersistence::Fingerprint(ADWPlayerCharacter& P,TArray<uint64>& Ids,TArray<double>& Numbers,TArray<bool>& Flags,FString& Error)
{
    auto* I=P.Inventory.Get();auto* W=P.WeaponAnimationComponent.Get();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    if(!IsValid(&P)||P.IsActorBeingDestroyed()||!P.GetWorld()||P.GetWorld()->bIsTearingDown||!A||!I||!W||W->IsBeingDestroyed()||
        I->GetOwner()!=&P||W->GetOwner()!=&P||!I->CanWriteInventory()||!P.HasCombatVitalsReady()||P.bRestoringSave||P.bApplyingFormalDashMotion||P.bApplyingCombatDamage||
        P.bGameplayActionInProgress||P.bAdvancingGameplayClocks||W->bAdvancingCombatClock||
        W->CombatWeaponResources.Revision==MAX_int64||W->CombatWeaponResources.RestoreEpoch==MAX_uint64||P.CombatKnockbackRevision==MAX_uint64)
    {Error=TEXT("Player snapshot requires a stable native boundary.");return false;}
    const auto V=P.CombatVitals.Capture(P.Health);const auto D=W->ShieldDefense.Capture(P.GetWorld()->GetTimeSeconds());
    const auto L=A->FindRegisteredActorLife(&P);if(!L.Entity||!L.Life){Error=TEXT("Player has no registered life.");return false;}
    Ids={L.Entity,L.Life,P.CombatVitals.GetMutationRevision(),P.CombatDamageRevision,P.CombatKnockbackRevision,
        static_cast<uint64>(I->GetInventoryRevision()),static_cast<uint64>(W->CombatWeaponResources.Revision),W->CombatWeaponResources.RestoreEpoch,
        W->CombatBoundaryRevision,static_cast<uint64>(W->ActionSerial),P.FormalRoot.GetSerial(),P.FormalDash.Serial(),
        P.CoreInjection.Plan.GetSerial(),static_cast<uint64>(P.CoreInjection.Plan.GetPhase()),static_cast<uint64>(W->SelectedWeapon),
        V.DehydrationOrdinal,V.StarvationOrdinal,P.LastFormalDashRequestOrdinal,
        P.CombatKnockback.Life.Entity,P.CombatKnockback.Life.Life,P.CombatKnockback.Frame};
    Numbers={V.Health,V.Water,V.Satiety,V.Special,V.WaterEmptyElapsedSeconds,V.SatietyEmptyElapsedSeconds,
        V.WaterDrainCarry,V.SatietyDrainCarry,V.WaterEmptyTimeCarry,V.SatietyEmptyTimeCarry,V.FormRemainingSeconds,V.FormTimeCarry,V.SpecialGrowthCarry,
        static_cast<double>(V.BaseForm),P.NextMeleeTime,P.NextThrowTime,W->NextAttackTime,W->ElapsedSeconds,P.FormalDash.ElapsedSeconds(),
        P.FormalDash.CooldownSeconds(),P.CoreInjection.Plan.GetRemainingSeconds(),D.PerfectRemainingSeconds,D.ReopenRemainingSeconds,
        static_cast<double>(D.CounterCharges),static_cast<double>(D.BaseForm),P.LastCombatDamageWorldTime,
        P.CombatKnockback.Direction.X,P.CombatKnockback.Direction.Y,P.CombatKnockback.Direction.Z,
        P.CombatKnockback.RemainingCm,P.CombatKnockback.RemainingSeconds,P.CombatKnockback.SelectedDistanceCm,P.CombatKnockback.CommittedTravelThisFrame};
    Flags={W->bEnabled,W->bActive,W->bPrimaryHeld,W->bSecondaryHeld,W->bCharging,W->bChargeReady,W->bReloading,
        D.bPerfectEligible,W->ShieldDefense.IsHeld(),P.FormalRoot.IsActive(),P.FormalDash.IsActive(),P.FormalDash.IsHeld(),
        P.PaidCombatWeaponSlice.bPending,P.PaidFormalDashSlice.bPending,P.PaidFormalDashSlice.bMoved,P.bDeathHandled,P.bCombatRecoveryPositionPending,
        P.CombatKnockback.bActive,P.bIsRolling,P.bCombatRollRestorePending};
    auto Origin=[&](const TOptional<FDWCombatInputPress>& O)
    {Ids.Add(O.IsSet()?O->Edge.Context.WorldEpoch:0);Ids.Add(O.IsSet()?O->Edge.Context.LifeEpoch:0);Ids.Add(O.IsSet()?O->Edge.Context.BindingGeneration:0);Ids.Add(O.IsSet()?O->Edge.Sequence:0);};
    Origin(W->PrimaryInputOrigin);Origin(W->SecondaryInputOrigin);Error.Reset();return true;
}

bool FDWCombatPlayerPersistence::CaptureBase(ADWPlayerCharacter& P,UDWSaveGame& Save,FString& Error)
{
    TArray<uint64> Ids;TArray<double> Numbers;TArray<bool> Flags;
    if(!Fingerprint(P,Ids,Numbers,Flags,Error))return false;
    const auto* GI=P.GetGameInstance<UDWGameInstance>();const auto Rules=GI?GI->GetCombatRules():nullptr;
    if(!Rules||!P.GetActorTransform().IsValid()){Error=TEXT("Player base has no valid rules/transform.");return false;}
    Save.PlayerTransform=P.GetActorTransform();Save.bHasPlayerTransform=true;Save.Health=P.Health;
    Save.Inventory=P.Inventory->GetSlots();Save.SprintAlcoholElapsed=Save.TransformationDecayElapsed=0.f;
    auto& Block=Save.CombatPlayerResources;Block={};Block.SubblockSchema=DWCombatPlayerSave::SubblockSchema;
    Block.Vitals=P.CombatVitals.Capture(P.Health);
    const auto& W=P.WeaponAnimationComponent->CombatWeaponResources.Snapshot;
    Block.OwnedPermanentWeaponIds=W.OwnedPermanentWeaponIds.Array();Block.OwnedPermanentWeaponIds.Sort(FNameLexicalLess());
    Block.CurrentEquipmentId=W.CurrentEquipmentId;Block.LoadedShotgunShells=W.LoadedShotgunShells;
    Save.Transformation=static_cast<float>(P.CombatVitals.GetSpecial());Save.bYeastForm=P.GetCombatBaseForm()==EDWCombatBaseForm::Yeast;
    if(const auto* Events=P.GetWorld()->GetSubsystem<UDWWorldEventSubsystem>())Save.CompletedWorldEvents=Events->ExportCompleted();
    Save.CombatWorld.Schema=Rules->GetCombatSchema();Save.CombatWorld.bNewAdventure=false;
    // The three old incomplete-block flags are false only in this complete-aggregate candidate.
    // No old CaptureSaveData(), resource availability Blueprint or disk writer is invoked.
    Error.Reset();return true;
}
bool FDWCombatPlayerPersistence::Capture(ADWPlayerCharacter& P,const TArray<FDWCombatObjectBinding>& Bindings,
    FDWCombatPlayerExtendedSave& Out,FString& Error)
{
    TArray<uint64> Ids;TArray<double> Numbers;TArray<bool> Flags;if(!Fingerprint(P,Ids,Numbers,Flags,Error))return false;
    auto* A=UDWCombatAuthoritySubsystem::Get(&P);const double Now=P.GetWorld()->GetTimeSeconds();
    FDWCombatPlayerExtendedSave S;S.Schema=1;S.WeaponLockRemaining=FMath::Max(0.,P.WeaponAnimationComponent->NextAttackTime-Now);
    S.LegacyMeleeLockRemaining=FMath::Max(0.,double(P.NextMeleeTime)-Now);S.LegacyThrowLockRemaining=FMath::Max(0.,double(P.NextThrowTime)-Now);
    auto Dash=P.FormalDash;if(Dash.IsActive())Dash.End(EDWYeastDashEnd::Cancelled);S.DashCooldownRemaining=Dash.CooldownSeconds();
    S.CombatDamageRevision=P.CombatDamageRevision;S.Defense=Shield(P.WeaponAnimationComponent->ShieldDefense.Capture(Now));
    const auto Life=A->FindRegisteredActorLife(&P);S.PlayerEntity=Life.Entity;S.PlayerLife=Life.Life;
    if(!A->CaptureShieldPackets(P,Bindings,S.ShieldPackets,Error))return false;
    if(P.FormalRoot.IsActive())
    {
        FDWMoldRootIdentity Root;if(!P.GetFormalRootIdentity(Root)){Error=TEXT("Active root has no valid identity.");return false;}
        S.bRooted=true;S.RootAnchor=Root.Anchor;S.RootAction=Root.Action.Id;S.RootReceipt=Root.Receipt.Id;
    }
    Out=MoveTemp(S);Error.Reset();return true;
}
bool FDWCombatPlayerPersistence::Validate(const FDWCombatRules& Rules,const UDWSaveGame& Save,const FDWCombatEffectsSave& Effects,FString& Error)
{
    using namespace DWCombatCommit;
    const auto& S=Save.CombatWorld.Player;const auto& V=Save.CombatPlayerResources.Vitals;
    if(Save.CombatWorld.Schema!=Rules.GetCombatSchema()||Save.CombatWorld.bNewAdventure||S.Schema!=1||!S.PlayerEntity||!S.PlayerLife||
        !DWCombatPlayerSave::Validate(Rules,Save,Error)||!DWCombatEffectsSave::Validate(Effects,Error))
    {if(Error.IsEmpty())Error=TEXT("Missing complete D22 player schema.");return false;}
    for(double Value:{S.WeaponLockRemaining,S.LegacyMeleeLockRemaining,S.LegacyThrowLockRemaining,S.DashCooldownRemaining})
        if(!Nonnegative(Value)){Error=TEXT("Saved player cooldown is invalid.");return false;}
    if(S.LegacyMeleeLockRemaining>MAX_flt||S.LegacyThrowLockRemaining>MAX_flt)
    {Error=TEXT("Legacy cooldown cannot be represented.");return false;}
    FDWYeastDashConfig DC;FDWShieldAxeTuning SC;FDWMoldRootConfig RC;FDWShieldAxeDefense Defense;FDWCombatPlayerVitalsConfig VC;
    if(!FDWYeastDashConfig::Build(Rules,DC,Error)||S.DashCooldownRemaining>DC.CooldownSeconds||
        !FDWShieldAxeTuning::Build(Rules,SC,Error)||!Defense.Configure(SC)||!Defense.Restore(Shield(S.Defense),0.)||
        !FDWMoldRootConfig::Build(Rules,RC,Error)||!FDWCombatPlayerVitalsConfig::Build(Rules,VC,Error)||
        !FDWMoldRootPlan::ValidateSnapshot(RC,{S.bRooted,S.RootAnchor},V.BaseForm,V.Special-V.SpecialGrowthCarry,V.FormRemainingSeconds-V.FormTimeCarry))
    {if(Error.IsEmpty())Error=TEXT("Invalid shield, dash or rooted resource snapshot.");return false;}
    const bool Dead=Save.Health<=DWCombatPlayerSave::HealthFloor(VC);
    if((S.bRooted&&Dead)||(S.Defense.CounterCharges>0&&S.Defense.BaseForm!=static_cast<uint8>(V.BaseForm))||
        (S.Defense.CounterCharges>0&&!Save.CombatPlayerResources.OwnedPermanentWeaponIds.Contains(TEXT("DWWeapon_ShieldAxe")))||
        (S.Defense.bPerfectEligible&&Save.CombatPlayerResources.CurrentEquipmentId!=TEXT("DWWeapon_ShieldAxe")))
    {Error=TEXT("Player stance/defense disagrees with health, form or equipment.");return false;}
    const auto* Actor=Effects.Authority.Actors.FindByPredicate([&](const auto& B){return B.Life.Entity==S.PlayerEntity;});
    if(!Actor||Actor->Life.Life!=S.PlayerLife||Actor->bEnded||Actor->Kind!=EDWCombatSavedActorKind::Player||Actor->bDead!=Dead)
    {Error=TEXT("Player resources disagree with the saved authority life.");return false;}
    if(S.bRooted)
    {
        const auto& L=Effects.Authority.Ledger;
        auto Action=std::find_if(L.Actions.begin(),L.Actions.end(),[&](const auto& A){return A.Id==S.RootAction;});
        auto Receipt=std::find_if(L.Receipts.begin(),L.Receipts.end(),[&](const auto& R){return R.Receipt.Handle.Id==S.RootReceipt;});
        if(!S.RootAction||!S.RootReceipt||Action==L.Actions.end()||!Action->Active||!SameLife(Action->Owner,{S.PlayerEntity,S.PlayerLife})||
            Receipt==L.Receipts.end()||!Receipt->Resource||Receipt->ResourceKind!=EResourceKind::RootDeployment||
            Receipt->Receipt.State!=EReceiptState::Committed||Receipt->Action.Id!=S.RootAction||!SameLife(Receipt->Source,Action->Owner))
        {Error=TEXT("Saved rooted episode has no committed owned deployment.");return false;}
    }
    else if(S.RootAction||S.RootReceipt||S.RootAnchor!=FVector::ZeroVector)
    {Error=TEXT("Unrooted player cannot retain a root identity.");return false;}
    Error.Reset();return true;
}

bool FDWCombatPlayerPersistence::Prepare(ADWPlayerCharacter& P,const UDWSaveGame& Save,const FDWCombatEffectsSave& Effects,
    const FDWCombatAuthorityRestorePlan& Identity,FDWCombatPlayerRestorePlan& Out,FString& Error)
{
    const auto* GI=P.GetGameInstance<UDWGameInstance>();const auto Rules=GI?GI->GetCombatRules():nullptr;
    if(!Rules||!Validate(*Rules,Save,Effects,Error)||!Identity.NewEpoch())return false;
    FDWCombatPlayerRestorePlan Plan;Plan.State.Reset(new FDWCombatPlayerRestoreState());auto& C=*Plan.State;
    if(!Fingerprint(P,C.ExpectedIds,C.ExpectedNumbers,C.ExpectedFlags,Error))return false;
    C.Player=&P;C.Inventory=P.Inventory.Get();C.Weapon=P.WeaponAnimationComponent.Get();C.World=P.GetWorld();C.Authority=UDWCombatAuthoritySubsystem::Get(&P);
    C.At=P.GetWorld()->GetTimeSeconds();C.ExpectedTransform=P.GetActorTransform();C.bHadBegunPlay=P.HasActorBegunPlay();C.Controller=P.GetController();
    C.OldEpoch=C.Authority->GetWorldEpoch();C.OldLife=C.Authority->FindRegisteredActorLife(&P);C.NewEpoch=Identity.NewEpoch();
    const auto& S=Save.CombatWorld.Player;C.NewLife=Identity.FindLife(S.PlayerEntity);
    if(Identity.FindActor(S.PlayerEntity)!=&P||!SameLife(C.NewLife,{S.PlayerEntity,S.PlayerLife}))
    {Error=TEXT("Player cannot bind to the prepared authority identity.");return false;}
    FDWCombatPlayerVitalsConfig VC;FDWShieldAxeTuning SC;FDWYeastDashConfig DC;FDWMoldRootConfig RC;
    if(!FDWCombatPlayerVitalsConfig::Build(*Rules,VC,Error)||!C.Vitals.Restore(VC,Save.CombatPlayerResources.Vitals)||
        !DWCombatPlayerSave::BuildWeaponCandidate(*Rules,Save.CombatPlayerResources,C.WeaponState,Error)||
        !DWCombatEquipment::ToPresentation(C.WeaponState.Snapshot.CurrentEquipmentId,C.Selection)||!DWCombatEquipment::BuildSwitchLock(*Rules,C.SwitchLock,Error)||
        !FDWShieldAxeTuning::Build(*Rules,SC,Error)||!C.Defense.Configure(SC)||!C.Defense.Restore(Shield(S.Defense),C.At)||
        !FDWYeastDashConfig::Build(*Rules,DC,Error)||!C.Dash.RestoreCooldown(DC,S.DashCooldownRemaining)||
        !FDWMoldRootConfig::Build(*Rules,RC,Error)||!C.Root.Restore(RC,{S.bRooted,S.RootAnchor},C.Vitals.GetForm(),C.Vitals.GetSpecial(),C.Vitals.GetFormRemaining()))
    {if(Error.IsEmpty())Error=TEXT("Player native restore candidate was rejected.");return false;}
    if(S.bRooted)
    {
        C.RootIdentity={{C.NewEpoch,S.RootAction},{C.NewEpoch,S.RootReceipt},C.NewLife,C.NewEpoch,C.Root.GetSerial(),S.RootAnchor};
        const auto Receipt=Identity.FindReceipt(S.RootReceipt);
        if(!Receipt||Receipt->State!=DWCombatCommit::EReceiptState::Committed||!SameLife(Identity.FindActionOwner(S.RootAction),C.NewLife))
        {Error=TEXT("Root action was not retained by the authority restore plan.");return false;}
    }
    if(!FDWMoldRootPlan::ValidateBinding(C.Root,C.RootIdentity,C.NewEpoch,C.NewLife))return false;
    TArray<FDWItemStack> Slots=Save.Inventory;
    if(!C.Inventory->MakePrepared(MoveTemp(Slots),C.PreparedInventory))
    {Error=TEXT("Saved inventory violates the current registered per-item stack policy.");return false;}
    C.WeaponState.Revision=C.Weapon->CombatWeaponResources.Revision+1;C.WeaponState.RestoreEpoch=C.Weapon->CombatWeaponResources.RestoreEpoch+1;
    C.AdoptedWeaponRestoreEpoch=C.WeaponState.RestoreEpoch;C.bSavedDead=Save.Health<=DWCombatPlayerSave::HealthFloor(VC);
    C.Health=Save.Health;C.SavedDamageRevision=S.CombatDamageRevision;C.bPlace=Save.bHasPlayerTransform;C.Destination=Save.PlayerTransform;
    C.WeaponLock=C.At+S.WeaponLockRemaining;C.MeleeLock=C.At+S.LegacyMeleeLockRemaining;C.ThrowLock=C.At+S.LegacyThrowLockRemaining;
    if(!FMath::IsFinite(C.WeaponLock)||!FMath::IsFinite(C.MeleeLock)||!FMath::IsFinite(C.ThrowLock)||C.MeleeLock>MAX_flt||C.ThrowLock>MAX_flt)
    {Error=TEXT("Saved deadlines overflow this world clock.");return false;}
    if(!Plan.CanCommit(Error))return false;Out=MoveTemp(Plan);return true;
}
bool FDWCombatPlayerPersistence::CanCommit(const FDWCombatPlayerRestorePlan& Plan,FString& Error)
{
    if(!Plan.State||Plan.State->bAdopted){Error=TEXT("Player restore plan is absent or consumed.");return false;}
    const auto& C=*Plan.State;auto* P=C.Player.Get();auto* A=C.Authority.Get();
    if(!P||!A||!C.World.IsValid()||P->GetWorld()!=C.World.Get()||C.World->bIsTearingDown||C.World->GetTimeSeconds()!=C.At||
        UDWCombatAuthoritySubsystem::Get(P)!=A||A->GetWorldEpoch()!=C.OldEpoch||!SameLife(A->FindRegisteredActorLife(P),C.OldLife)||
        P->Inventory.Get()!=C.Inventory.Get()||P->WeaponAnimationComponent.Get()!=C.Weapon.Get()||P->GetController()!=C.Controller.Get()||
        !P->GetActorTransform().Equals(C.ExpectedTransform,0.)||(C.bHadBegunPlay&&!P->HasActorBegunPlay()))
    {Error=TEXT("Player lifetime, components, transform or game time changed during preparation.");return false;}
    TArray<uint64> Ids;TArray<double> Numbers;TArray<bool> Flags;
    if(!Fingerprint(*P,Ids,Numbers,Flags,Error)||Ids!=C.ExpectedIds||Numbers!=C.ExpectedNumbers||Flags!=C.ExpectedFlags||
        !C.Inventory->ValidatePreparedForCommit(C.PreparedInventory))
    {Error=TEXT("Player resources, action clocks, input or inventory changed during preparation.");return false;}
    Error.Reset();return true;
}
void FDWCombatPlayerPersistence::AdoptNativeFacts(FDWCombatPlayerRestorePlan& Plan)
{
    check(Plan.State&&!Plan.State->bAdopted);auto& C=*Plan.State;auto& P=*C.Player.Get();auto& W=*C.Weapon.Get();
    check(C.Authority.IsValid()&&C.Authority->GetWorldEpoch()==C.NewEpoch&&UDWCombatAuthoritySubsystem::Get(&P)==nullptr);
    // Authority is inside the aggregate barrier. Get() deliberately returns null: native EndXXX
    // clears old local actions without ending a numerically equal restored action in the new ledger.
    P.bRestoringSave=true;
    P.CancelCoreInjectionFacts();P.LastCoreInjectionReceipt={};P.EndFormalDashFacts(EDWYeastDashEnd::Cancelled);P.EndFormalRootFacts(EDWMoldRootEnd::Restore);
    // User-approved D22 policy: keep saved damage/location; stop only the remaining player displacement.
    P.ResetCombatKnockbackFacts();P.PaidFormalDashSlice={};P.PaidCombatWeaponSlice={};P.bHasCombatFormFrameSlice=false;
    P.MeleeCommitAction={};P.DashCommitAction={};P.AttackPhase=EDWCombatPhase::None;P.bDashCombatActive=false;
    P.MeleeHitActors.Reset();P.DashHitActors.Reset();P.ClearMeleeComboContinuation();
    W.EndFormalBow();W.EndFormalBottle();W.EndFormalSpore();W.EndFormalSpear();W.EndFormalUnarmed();
    W.EndFormalAxe();W.EndFormalSword();W.EndFormalDagger();W.EndFormalShotgun();W.EndWeaponResourceActions();
    if(W.bActive)P.GetCharacterMovement()->bOrientRotationToMovement=W.bOldOrient;
    W.bActive=W.bPrimaryHeld=W.bSecondaryHeld=W.bCharging=W.bChargeReady=W.bReloading=false;
    W.PrimaryInputOrigin.Reset();W.SecondaryInputOrigin.Reset();W.ActiveSet=nullptr;W.ActiveClip={};W.ElapsedSeconds=0.;W.FiredEvents.Reset();
    C.Inventory->AdoptPreparedUnchecked(MoveTemp(C.PreparedInventory));W.CombatWeaponResources=MoveTemp(C.WeaponState);
    W.SelectedWeapon=C.Selection;W.EquipmentSwitchLockSeconds=C.SwitchLock;W.bEquipmentRulesReady=true;
    W.AmmoInMagazine=C.Selection==EDWAnimatedWeapon::Shotgun?W.CombatWeaponResources.Snapshot.LoadedShotgunShells:0;W.LastWeaponResourceReceipt={};
    W.NextAttackTime=C.WeaponLock;P.NextMeleeTime=LegacyDeadline(C.MeleeLock);P.NextThrowTime=LegacyDeadline(C.ThrowLock);
    P.CombatVitals=MoveTemp(C.Vitals);P.Health=C.Health;P.CombatDamageRevision=C.SavedDamageRevision;P.LastCombatDamageWorldTime=-1.;
    P.FormalDash=MoveTemp(C.Dash);P.DashInputOrigin={};P.FormalDashLife={};P.FormalDashWorld.Reset();P.FormalDashEpoch=0;
    P.LastFormalDashEdge={};P.LastFormalDashRequestOrdinal=P.LastPublishedFormalDashSerial=0;
    P.AdoptFormalRootRestoreUnchecked(MoveTemp(C.Root),C.RootIdentity);
    P.bDeathHandled=P.IsDead();P.bCombatDeathEffectsPending=P.bCombatRecoveryPositionPending=false;
    P.bSprintHeld=P.bHarvestHeld=P.bSprinting=false;P.AttackUntil=P.TransformUntil=0.f;P.bRestartAttackAnimation=false;
    P.bHasMeleeAnimationSnapshot=P.bActiveMeleeCombo=P.bRestartMeleeAnimation=false;P.ActiveMeleeClip=nullptr;P.CurrentMeleeComboStep=INDEX_NONE;
    P.SyncCombatVitalsCompatibility();W.ShieldDefense=MoveTemp(C.Defense);
    // No RefreshDaggerEquipmentFacts: the enemy participant restores each exact saved grace independently.
    C.bAdopted=true;
}
bool FDWCombatPlayerPersistence::IsPublishedLifeCurrent(const FDWCombatPlayerRestoreState& C)
{
    auto* P=C.Player.Get();auto* A=C.Authority.Get();auto* W=C.Weapon.Get();auto* I=C.Inventory.Get();
    return C.bAdopted&&IsValid(P)&&!P->IsActorBeingDestroyed()&&C.World.IsValid()&&P->GetWorld()==C.World.Get()&&!C.World->bIsTearingDown&&
        (!C.bHadBegunPlay||P->HasActorBegunPlay())&&A&&UDWCombatAuthoritySubsystem::Get(P)==A&&A->GetWorldEpoch()==C.NewEpoch&&
        SameLife(A->FindRegisteredActorLife(P),C.NewLife)&&W&&!W->IsBeingDestroyed()&&I&&I->CanWriteInventory()&&
        P->Inventory.Get()==I&&P->WeaponAnimationComponent.Get()==W&&P->GetController()==C.Controller.Get()&&
        P->IsDead()==C.bSavedDead&&W->CombatWeaponResources.RestoreEpoch==C.AdoptedWeaponRestoreEpoch;
}
bool FDWCombatPlayerPersistence::Publish(FDWCombatPlayerRestorePlan& Plan,FString& Error)
{
    if(!Plan.State||Plan.State->bPublished||!IsPublishedLifeCurrent(*Plan.State)){Error=TEXT("Player restore publication is stale.");return false;}
    auto& C=*Plan.State;auto* P=C.Player.Get();
    const auto Current=[&](){if(IsPublishedLifeCurrent(C)&&P->bRestoringSave)return true;Error=TEXT("Player publication interrupted by world/life/restore callback.");return false;};
    if(auto* PC=Cast<ADWPlayerController>(P->GetController()))PC->InvalidateCombatInputLife();if(!Current())return false;
    P->StopGatherFeedback(false);if(!Current())return false;P->StopPlayerVFX(true);if(!Current())return false;P->StopMovementAudio();if(!Current())return false;
    P->bHasVFXLocationSample=false;P->CurrentVFXGroundSpeed=0.f;
    P->RestoreMeleeWeaponPlaceholder();if(!Current())return false;
    P->PublishCombatKnockbackEffects();if(!Current())return false;
    C.Weapon->ResetTrails();if(!Current())return false;
    P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!Current())return false;
    if(C.bPlace){if(!P->SetActorTransform(C.Destination,false,nullptr,ETeleportType::TeleportPhysics)||!Current())return false;}
    P->GetCharacterMovement()->SetMovementMode(P->IsDead()||P->IsFormallyRooted()?MOVE_None:MOVE_Walking);if(!Current())return false;
    P->bRootMovementPending=false; // Placement above applies rooted immobility without retiring the restored mine ring.
    P->UpdateFormAppearance(true);if(!Current())return false;C.Weapon->UpdateVisuals();if(!Current())return false;P->UpdateAnimation();if(!Current())return false;
    C.bPublished=true;Error.Reset();return true;
}
bool FDWCombatPlayerPersistence::FinishPublish(FDWCombatPlayerRestorePlan& Plan,FString& Error)
{
    if(!Plan.State||!Plan.State->bPublished||Plan.State->bFinished||!IsPublishedLifeCurrent(*Plan.State))
    {Error=TEXT("Player restore cannot finish before complete publication.");return false;}
    auto& C=*Plan.State;auto* P=C.Player.Get();
    const auto Current=[&](){if(IsPublishedLifeCurrent(C))return true;Error=TEXT("Final player notice interrupted by lifecycle callback.");return false;};
    if(auto* PC=Cast<APlayerController>(P->GetController()))if(auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD()))
    {
        HUD->SetSessionStarted(true);if(!Current())return false;
        if(P->IsDead())HUD->ShowDefeat();else HUD->ClosePanels();if(!Current())return false;
    }
    // No Die/OnPlayerDied: saved death and its bag have already been adopted by the world participant.
    P->bRestoringSave=false;C.bFinished=true;
    C.Inventory->OnChanged.Broadcast();if(!Current())return false;
    const auto* W=C.Weapon.Get();const int64 Revision=W->CombatWeaponResources.Revision;const int32 Loaded=W->CombatWeaponResources.Snapshot.LoadedShotgunShells;
    C.Weapon->OnWeaponResourceChanged.Broadcast(Loaded,Revision,false);if(!Current())return false;
    Error.Reset();return true;
}
