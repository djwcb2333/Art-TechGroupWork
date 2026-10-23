#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

void UDWWeaponAnimationComponent::InitializeShotgunRules()
{
    const auto* P=Player();const auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;FString Error;
    bShotgunConfigured=GI&&GI->GetCombatRules()&&FDWShotgunConfig::Build(*GI->GetCombatRules(),ShotgunConfig,Error);
}
bool UDWWeaponAnimationComponent::StartFormalShotgun(FVector Target,bool Reload)
{
    auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    if(!A||!P||!bShotgunConfigured||!bEnabled||!CanUseShotgunResources()||P->IsFormalDashing()||
        Target.ContainsNaN()||ActionSerial==MAX_int32)return false;
    if(Reload){if(bActive||!HasShotgunReloadCapacityAndStock())return false;}
    else
    {
        if(bActive&&!bFormalShotgunReload)return false;
        // A deliberate fire press first stops an ongoing reload, even with no completed shell.
        // Idle empty-fire keeps the existing auto-reload rule. Reload cancellation never re-enters it.
        if(!bFormalShotgunReload)
        {
            if(GetCombatActionWorldSeconds()<NextAttackTime)return false;
            if(CombatWeaponResources.Snapshot.LoadedShotgunShells<CombatWeaponResources.ShotgunRoundsPerShot)return StartFormalShotgun(Target,true);
        }
    }
    const auto Life=A->CaptureActorLife(P);const uint64 Epoch=A->GetWorldEpoch(),Restore=CombatWeaponResources.RestoreEpoch;
    const int64 Revision=CombatWeaponResources.Revision;const TWeakObjectPtr<UWorld> World(GetWorld());
    P->CancelCombatActions();A=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||!World.IsValid()||GetWorld()!=World.Get()||
        World->bIsTearingDown||!A||A->GetWorldEpoch()!=Epoch||!CanUseShotgunResources()||
        Restore!=CombatWeaponResources.RestoreEpoch||Revision!=CombatWeaponResources.Revision)return false;
    const auto Current=A->CaptureActorLife(P);if(Current.Entity!=Life.Entity||Current.Life!=Life.Life)return false;
    if(!Reload&&(GetCombatActionWorldSeconds()<NextAttackTime||
        CombatWeaponResources.Snapshot.LoadedShotgunShells<CombatWeaponResources.ShotgunRoundsPerShot))return false;
    const auto Action=A->BeginAction(P);if(!Action.Id)return false;
    if(!Reload&&!ShotgunPlan.Start(ShotgunConfig,ReadFormalSpearForm())){A->EndAction(Action);return false;}
    bActive=true;bFormalShotgunActive=!Reload;bFormalShotgunReload=bReloading=Reload;
    bPrimaryHeld=!Reload;bSecondaryHeld=bCharging=bChargeReady=false;bShotgunStartPublished=false;
    if(Reload){ReloadResourceAction=Action;ReloadShellOrdinal=1;bReloadShellCommitted=false;bReloadShellCompletionDue=false;ShotgunReloadRemaining=CombatWeaponResources.ReloadOneSeconds;}
    else MagazineResourceAction=Action;
    TargetLocation=Target;ShotgunForward=(Target-P->GetActorLocation()).GetSafeNormal2D();
    if(ShotgunForward.IsNearlyZero())ShotgunForward=P->GetActorForwardVector().GetSafeNormal2D();
    ActiveSet=GetSelectedSet();const FName Role=Reload?FName(TEXT("ReloadOne")):FName(TEXT("Attack"));
    const auto* Clip=ActiveSet?ActiveSet->FindClip(Role):nullptr;
    ActiveClip=Clip?*Clip:FDWWeaponAnimationClip();ActiveClip.Role=Role;
    if(!FMath::IsFinite(ActiveClip.DurationSeconds)||ActiveClip.DurationSeconds<=0.f)
        ActiveClip.DurationSeconds=static_cast<float>(Reload?CombatWeaponResources.ReloadOneSeconds:ShotgunConfig.Windup+ShotgunConfig.Recovery);
    // Source timing is presentation only. Gameplay never reads DamageEvents or mesh geometry.
    ElapsedSeconds=0.;FiredEvents.Reset();++ActionSerial;bOldOrient=P->GetCharacterMovement()->bOrientRotationToMovement;
    if(!Reload)NextAttackTime=FMath::Max(NextAttackTime,GetCombatActionWorldSeconds()+ShotgunConfig.Interval/ShotgunConfig.Rate(ReadFormalSpearForm()));
    P->RefreshCombatMovementSpeed();
    return true;
}
bool UDWWeaponAnimationComponent::IsCurrentFormalShotgun()const
{
    const auto* P=Player();
    return IsValid(this)&&!IsBeingDestroyed()&&bActive&&(bFormalShotgunActive||bFormalShotgunReload)&&
        SelectedWeapon==EDWAnimatedWeapon::Shotgun&&HasCurrentEquipmentIdentity()&&P&&!P->IsActorBeingDestroyed()&&P->IsCombatActionAllowed();
}
void UDWWeaponAnimationComponent::PublishFormalShotgunStart()
{
    if(!IsCurrentFormalShotgun()||bShotgunStartPublished)return;bShotgunStartPublished=true;
    auto* P=Player();const int32 Serial=ActionSerial;
    if(!bFormalShotgunReload)
    {
        P->ConsumeMovementInputVector();P->GetCharacterMovement()->StopMovementImmediately();if(!IsCurrentFormalShotgun()||Serial!=ActionSerial)return;
        P->GetCharacterMovement()->bOrientRotationToMovement=false;P->SetActorRotation(ShotgunForward.Rotation());if(!IsCurrentFormalShotgun()||Serial!=ActionSerial)return;
    }
    P->SetHarvestHeld(false);if(!IsCurrentFormalShotgun()||Serial!=ActionSerial)return;
    ResetTrails();UpdateVisuals();
}
void UDWWeaponAnimationComponent::EndFormalShotgun()
{
    const bool WasReloading=bFormalShotgunReload;
    bFormalShotgunActive=bFormalShotgunReload=bShotgunStartPublished=false;ShotgunPlan.Cancel();ShotgunReloadRemaining=0.;
    if(WasReloading)if(auto* P=Player();IsValid(P)&&!P->IsActorBeingDestroyed())P->RefreshCombatMovementSpeed();
}
void UDWWeaponAnimationComponent::AdvanceFormalShotgun(double Dt)
{
    if(!FMath::IsFinite(Dt)||Dt<0.||!IsCurrentFormalShotgun())return;
    const int32 Serial=ActionSerial;const uint64 Restore=CombatWeaponResources.RestoreEpoch;
    const auto Action=bFormalShotgunReload?ReloadResourceAction:MagazineResourceAction;
    auto Current=[&](){return IsCurrentFormalShotgun()&&Serial==ActionSerial&&Restore==CombatWeaponResources.RestoreEpoch&&
        (bFormalShotgunReload?ReloadResourceAction.Id:MagazineResourceAction.Id)==Action.Id;};
    double Left=Dt,Offset=0.;
    for(int32 Budget=64;Budget>0&&Current();--Budget)
    {
        if(bFormalShotgunReload)
        {
            const double Used=FMath::Min(Left,ShotgunReloadRemaining);ShotgunReloadRemaining=FMath::Max(0.,ShotgunReloadRemaining-Used);Left-=Used;Offset+=Used;
            ElapsedSeconds=FMath::Min(static_cast<double>(ActiveClip.DurationSeconds),ElapsedSeconds+Used);
            if(ShotgunReloadRemaining>0.)return;
            if(!bReloadShellCommitted)
            {
                bReloadShellCompletionDue=true;
                const auto Result=FDWCombatInventoryTransaction::CommitReloadShell(*this);
                if(!Result.bCommittedThisCall){if(Current())FinishAction();return;}
                if(!Current()){bCombatAdvanceInterrupted=true;return;}
            }
            if(!HasShotgunReloadCapacityAndStock()||ReloadShellOrdinal==MAX_uint64){FinishAction();return;}
            ++ReloadShellOrdinal;bReloadShellCommitted=bReloadShellCompletionDue=false;
            ShotgunReloadRemaining=CombatWeaponResources.ReloadOneSeconds;ElapsedSeconds=0.;
        }
        else
        {
            const double Used=ShotgunPlan.Advance(Left);Left-=Used;Offset+=Used;
            ElapsedSeconds=FMath::Min(static_cast<double>(ActiveClip.DurationSeconds),ElapsedSeconds+Used);
            if(ShotgunPlan.Remaining>0.)return;
            if(ShotgunPlan.Phase==EDWShotgunPhase::Windup)
            {
                auto* A=UDWCombatAuthoritySubsystem::Get(this);
                if(!A||!A->CommitFormalShotgun(*this,ReadFormalSpearForm(Offset))){if(Current())FinishAction();return;}
                if(!Current()){bCombatAdvanceInterrupted=true;return;}
            }
            else
            {
                const bool AutoReload=CombatWeaponResources.Snapshot.LoadedShotgunShells<CombatWeaponResources.ShotgunRoundsPerShot;
                const FVector Aim=TargetLocation;FinishAction();
                if(AutoReload&&StartFormalShotgun(Aim,true))PublishFormalShotgunStart();
                return;
            }
        }
        if(Left<=0.)return;
    }
}
