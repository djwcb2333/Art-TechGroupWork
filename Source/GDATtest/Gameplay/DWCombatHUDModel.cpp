#include "DWCombatHUDModel.h"
#include "DWCombatRules.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatMineSubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWInventoryComponent.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

bool FDWCombatHUDConfig::Build(const FDWCombatRules& Rules,FDWCombatHUDConfig& Out,FString& Error)
{
    FDWCombatHUDConfig C;
    if(!FDWCombatPlayerVitalsConfig::Build(Rules,C.Player,Error)||!FDWYeastDashConfig::Build(Rules,C.Dash,Error)||
        !FDWMoldRootConfig::Build(Rules,C.Root,Error)||
        !Rules.TryGetNumber(TEXT("hud.form_warning_s"),EDWCombatRuleUnit::Seconds,C.FormWarningSeconds,Error)||
        !Rules.TryGetNumber(TEXT("hud.message_duration_s"),EDWCombatRuleUnit::Seconds,C.MessageSeconds,Error)||
        !Rules.TryGetCount(TEXT("weapon.shotgun.magazine"),C.ShotgunCapacity,Error))return false;
    if(C.FormWarningSeconds<0.||C.MessageSeconds<=0.||C.ShotgunCapacity<=0)return false;
    Out=C;return true;
}
FDWCombatHUDResource DWCombatHUD::Resource(double Value,double Floor,double Maximum)
{
    FDWCombatHUDResource R;
    if(!FMath::IsFinite(Value)||!FMath::IsFinite(Floor)||!FMath::IsFinite(Maximum)||Maximum<=Floor)return R;
    R.Value=Value;R.Floor=Floor;R.Maximum=Maximum;R.Fraction=FMath::Clamp((Value-Floor)/(Maximum-Floor),0.,1.);return R;
}
EDWCombatHUDReason DWCombatHUD::DashResourceReason(const FDWYeastDashConfig& C,EDWCombatBaseForm Form,double Special,double Remaining,double Cooldown)
{
    if(!C.IsValid()||!FMath::IsFinite(Special)||!FMath::IsFinite(Remaining)||!FMath::IsFinite(Cooldown))return EDWCombatHUDReason::NotReady;
    if(Form!=EDWCombatBaseForm::Yeast)return EDWCombatHUDReason::WrongDashForm;
    if(Cooldown>0.)return EDWCombatHUDReason::Cooldown;
    if(Remaining<C.MinimumSeconds)return EDWCombatHUDReason::FormTooShort;
    if(Special-C.SpecialFloor<C.MinimumBudget())return EDWCombatHUDReason::SpecialForDash;
    return EDWCombatHUDReason::None;
}
EDWCombatHUDReason DWCombatHUD::RootResourceReason(const FDWMoldRootConfig& C,EDWCombatBaseForm Form,double Special,bool Grounded)
{
    if(!C.IsValid()||!FMath::IsFinite(Special))return EDWCombatHUDReason::NotReady;
    if(Form!=EDWCombatBaseForm::Mold)return EDWCombatHUDReason::WrongRootForm;
    if(Special<=C.StartCost||Special-C.StartCost<=C.SpecialFloor)return EDWCombatHUDReason::SpecialForRoot;
    if(!Grounded)return EDWCombatHUDReason::NotGrounded;
    return EDWCombatHUDReason::None;
}
FText DWCombatHUD::FormLabel(const UObject* C,EDWCombatBaseForm Form)
{
    if(Form==EDWCombatBaseForm::Mold)return DWText(C,TEXT("霉菌"),TEXT("Mold"));
    if(Form==EDWCombatBaseForm::Yeast)return DWText(C,TEXT("酵母"),TEXT("Yeast"));
    return DWText(C,TEXT("普通"),TEXT("Normal"));
}
bool DWCombatHUD::Build(const ADWPlayerCharacter* P,const FDWCombatHUDConfig& C,FDWCombatHUDSnapshot& Out)
{
    Out={};if(!IsValid(P)||P->IsActorBeingDestroyed()||!P->GetWorld()||P->GetWorld()->bIsTearingDown||!P->HasCombatVitalsReady())return false;
    auto& S=Out;const auto& V=C.Player.Vitals;S.bReady=true;
    S.Health=Resource(P->GetHealth(),V.HealthFloor,V.HealthMax);S.Water=Resource(P->GetCombatWater(),V.WaterFloor,V.WaterMax);
    S.Satiety=Resource(P->GetCombatSatiety(),V.SatietyFloor,V.SatietyMax);S.Special=Resource(P->GetCombatSpecial(),V.SpecialFloor,V.SpecialMax);
    S.Form=P->GetCombatBaseForm();S.FormRemaining=P->GetCombatFormRemaining();
    S.bFormWarning=S.Form!=EDWCombatBaseForm::Normal&&S.FormRemaining<=C.FormWarningSeconds;
    S.bRooted=P->IsFormallyRooted();S.bDashing=P->IsFormalDashing();S.DashCooldown=P->GetFormalDashCooldown();
    S.DashReason=DashResourceReason(C.Dash,S.Form,S.Special.Value,S.FormRemaining,S.DashCooldown);
    const auto* Move=P->GetCharacterMovement();
    S.RootReason=RootResourceReason(C.Root,S.Form,S.Special.Value,Move&&Move->IsMovingOnGround()&&Move->CurrentFloor.IsWalkableFloor());
    if(P->IsCombatKnockedBack())S.DashReason=EDWCombatHUDReason::Controlled;
    if(const auto* W=P->WeaponAnimationComponent.Get())
    {
        if(!W->AllowsFormalSwordDashInterruption())S.DashReason=EDWCombatHUDReason::ActionLocked;
        S.EquipmentId=W->GetCurrentEquipmentId();S.EquipmentName=DWText(P,TEXT("空手"),TEXT("Unarmed"));
        const auto* Binding=FDWCombatItemRegistry::GetWeaponBindings().FindByPredicate([&](const auto& B){return B.ItemId==S.EquipmentId;});
        if(Binding)
        {
            const TCHAR* CN[]={TEXT("长矛"),TEXT("盾斧"),TEXT("大剑"),TEXT("双匕首"),TEXT("孢子枪"),TEXT("霉菌弓"),TEXT("霰弹枪"),TEXT("酒精瓶")};
            const TCHAR* EN[]={TEXT("Spear"),TEXT("Shield axe"),TEXT("Greatsword"),TEXT("Daggers"),TEXT("Spore gun"),TEXT("Mold bow"),TEXT("Shotgun"),TEXT("Alcohol")};
            const auto& Bindings=FDWCombatItemRegistry::GetWeaponBindings();const int32 Index=int32(Binding-Bindings.GetData());
            S.EquipmentName=Index>=0&&Index<8?DWText(P,CN[Index],EN[Index]):FText::FromName(S.EquipmentId);
            if(auto* GI=P->GetGameInstance<UDWGameInstance>())if(const auto* Items=GI->GetConfig())
                if(const auto* Item=Items->Items.FindByPredicate([&](const auto& I){return I.ItemId==S.EquipmentId;}))
                    S.EquipmentName=UDWLocalizationLibrary::GetItemDisplayName(P,*Item);
            S.bUsesAmmo=!Binding->AmmoItemId.IsNone();S.Reserve=P->Inventory?P->Inventory->CountItem(Binding->AmmoItemId):0;
            S.bUsesMagazine=Binding->Presentation==EDWAnimatedWeapon::Shotgun;
            if(S.bUsesMagazine)
            {
                S.Loaded=W->GetCombatWeaponStateSnapshot().LoadedShotgunShells;S.MagazineCapacity=C.ShotgunCapacity;
                S.bShotgunReloading=W->IsFormalShotgunReloading();
                S.ShotgunReloadProgress=W->GetFormalShotgunReloadProgress();
                S.ShotgunReloadRemainingSeconds=W->GetFormalShotgunReloadRemainingSeconds();
                S.ShotgunReloadDurationSeconds=W->GetFormalShotgunReloadDurationSeconds();
            }
            if(S.bUsesMagazine&&S.Loaded<=0)S.AmmoReason=S.Reserve>0?EDWCombatHUDReason::EmptyMagazine:EDWCombatHUDReason::NoAmmunition;
            else if(S.bUsesAmmo&&!S.bUsesMagazine&&S.Reserve<=0)S.AmmoReason=EDWCombatHUDReason::NoAmmunition;
        }
    }
    if(const auto* Mines=UDWCombatMineSubsystem::Get(P)){S.MineCooldown=Mines->GetCooldownRemaining(P);S.LiveMines=Mines->GetLiveMineCount(P);}
    return true;
}
bool DWCombatHUD::ShouldShowEnemyHealth(const AActor* Actor,const ADWPlayerCharacter* P,bool Enabled)
{
    // Combat characters only. Generic actors, resources, nests and future townsfolk are not inferred from a Health property.
    const auto* E=Cast<ADWEnemyCharacter>(Actor);
    return Enabled&&IsValid(E)&&!E->IsActorBeingDestroyed()&&!E->IsHidden()&&E->IsAlive()&&IsValid(P)&&
        !P->IsActorBeingDestroyed()&&!P->IsDead()&&E->GetWorld()==P->GetWorld()&&FMath::IsFinite(E->MaxHealth)&&E->MaxHealth>0.f;
}
bool DWCombatHUD::ShouldShowDaggerCircle(const ADWEnemyCharacter* E,const ADWPlayerCharacter* P)
{
    const auto* W=IsValid(P)?P->WeaponAnimationComponent.Get():nullptr;
    return ShouldShowEnemyHealth(E,P,true)&&P->HasCombatVitalsReady()&&W&&W->IsEnabled()&&
        W->GetCurrentEquipmentId()==TEXT("DWWeapon_DualDaggers")&&FMath::IsFinite(E->GetDaggerAlertRadius())&&E->GetDaggerAlertRadius()>0.;
}
bool DWCombatHUD::ShouldShowShotgunOverhead(const ADWPlayerCharacter* P)
{
    const auto* W=IsValid(P)?P->WeaponAnimationComponent.Get():nullptr;
    return IsValid(P)&&!P->IsActorBeingDestroyed()&&!P->IsHidden()&&!P->IsDead()&&P->HasCombatVitalsReady()&&
        P->IsCombatActionAllowed()&&IsValid(W)&&!W->IsBeingDestroyed()&&W->IsEnabled()&&
        W->SelectedWeapon==EDWAnimatedWeapon::Shotgun&&W->GetCurrentEquipmentId()==TEXT("DWWeapon_Shotgun")&&
        W->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(TEXT("DWWeapon_Shotgun"));
}
FText DWCombatHUD::ReasonText(const UObject* C,EDWCombatHUDReason R)
{
    switch(R)
    {
    case EDWCombatHUDReason::None:return DWText(C,TEXT("资源条件满足"),TEXT("Resource requirements met"));
    case EDWCombatHUDReason::WrongDashForm:return DWText(C,TEXT("冲刺仅限酵母形态"),TEXT("Dash requires Yeast form"));
    case EDWCombatHUDReason::WrongRootForm:return DWText(C,TEXT("扎根仅限霉菌形态"),TEXT("Root requires Mold form"));
    case EDWCombatHUDReason::SpecialForDash:return DWText(C,TEXT("特殊值不足以支付冲刺最短时长"),TEXT("Not enough special for the minimum dash"));
    case EDWCombatHUDReason::SpecialForRoot:return DWText(C,TEXT("特殊值须高于扎根起手费用"),TEXT("Special must exceed the root entry cost"));
    case EDWCombatHUDReason::FormTooShort:return DWText(C,TEXT("形态剩余时间不足以完成最短冲刺"),TEXT("Form time is shorter than the minimum dash"));
    case EDWCombatHUDReason::Cooldown:return DWText(C,TEXT("冲刺冷却中"),TEXT("Dash is cooling down"));
    case EDWCombatHUDReason::NotGrounded:return DWText(C,TEXT("扎根需要可站立地面"),TEXT("Root requires walkable ground"));
    case EDWCombatHUDReason::Controlled:return DWText(C,TEXT("受击退控制中，暂时无法发动"),TEXT("Cannot start while knocked back"));
    case EDWCombatHUDReason::ActionLocked:return DWText(C,TEXT("当前动作或攻击间隔尚未结束"),TEXT("Current action or attack lock has not ended"));
    case EDWCombatHUDReason::NoAmmunition:return DWText(C,TEXT("缺少对应弹药或酒精瓶"),TEXT("No matching ammunition or alcohol"));
    case EDWCombatHUDReason::EmptyMagazine:return DWText(C,TEXT("匣内为空，请先装填"),TEXT("Magazine empty; reload first"));
    case EDWCombatHUDReason::MagazineFull:return DWText(C,TEXT("弹匣已满"),TEXT("Magazine is full"));
    case EDWCombatHUDReason::WrongReloadWeapon:return DWText(C,TEXT("装填仅用于霰弹枪"),TEXT("Reload is only for the shotgun"));
    case EDWCombatHUDReason::NotReady:return DWText(C,TEXT("战斗状态尚未就绪"),TEXT("Combat state is not ready"));
    default:return DWText(C,TEXT("当前无法执行此动作"),TEXT("This action is currently unavailable"));
    }
}
void DWCombatHUD::NotifyRejected(ADWPlayerCharacter* P,EDWCombatHUDRequest Request)
{
    if(!IsValid(P)||P->IsActorBeingDestroyed()||!P->GetWorld()||P->GetWorld()->bIsTearingDown||P->IsDead()||!P->IsCombatActionAllowed())return;
    auto* PC=Cast<APlayerController>(P->GetController());auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
    auto* GI=P->GetGameInstance<UDWGameInstance>();FDWCombatHUDConfig C;FDWCombatHUDSnapshot S;FString Error;
    if(!HUD||!GI||!GI->GetCombatRules()||!FDWCombatHUDConfig::Build(*GI->GetCombatRules(),C,Error)||!Build(P,C,S))return;
    EDWCombatHUDReason Reason=EDWCombatHUDReason::Unavailable;
    if(Request==EDWCombatHUDRequest::Dash)Reason=S.DashReason;
    else if(Request==EDWCombatHUDRequest::Root)Reason=S.RootReason;
    else if(Request==EDWCombatHUDRequest::Primary)Reason=S.AmmoReason;
    else if(!S.bUsesMagazine)Reason=EDWCombatHUDReason::WrongReloadWeapon;
    else if(S.Loaded>=S.MagazineCapacity)Reason=EDWCombatHUDReason::MagazineFull;
    else if(S.Reserve<=0)Reason=EDWCombatHUDReason::NoAmmunition;
    if(Reason==EDWCombatHUDReason::None)
        Reason=P->WeaponAnimationComponent&&(P->WeaponAnimationComponent->IsBusy()||P->WeaponAnimationComponent->GetRemainingAttackLockSeconds()>0.)
            ?EDWCombatHUDReason::ActionLocked:EDWCombatHUDReason::Unavailable;
    FText Message=ReasonText(P,Reason);
    if(Reason==EDWCombatHUDReason::SpecialForDash)Message=FText::Format(
        DWText(P,TEXT("冲刺需要至少 {0} 特殊值（当前 {1}）"),TEXT("Dash needs at least {0} special (current {1})")),
        FText::AsNumber(C.Dash.SpecialFloor+C.Dash.MinimumBudget()),FText::FromString(FString::Printf(TEXT("%.1f"),S.Special.Value)));
    if(Reason==EDWCombatHUDReason::SpecialForRoot)Message=FText::Format(
        DWText(P,TEXT("扎根特殊值须严格高于 {0}（当前 {1}）"),TEXT("Root special must be strictly above {0} (current {1})")),
        FText::AsNumber(C.Root.StartCost+C.Root.SpecialFloor),FText::FromString(FString::Printf(TEXT("%.1f"),S.Special.Value)));
    if(Reason==EDWCombatHUDReason::FormTooShort)Message=FText::Format(
        DWText(P,TEXT("形态至少还需 {0} 秒，当前剩余 {1} 秒"),TEXT("Form needs at least {0}s; {1}s remain")),
        FText::AsNumber(C.Dash.MinimumSeconds),FText::FromString(FString::Printf(TEXT("%.2f"),S.FormRemaining)));
    HUD->ShowCombatToast(Message); // Final operation: no old request resumes after UI reentry.
}
