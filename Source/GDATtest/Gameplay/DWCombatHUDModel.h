#pragma once
#include "CoreMinimal.h"
#include "DWCombatPlayerVitals.h"
#include "DWYeastDashPlan.h"
#include "DWCombatMoldRoot.h"
#include "DWCombatHUDModel.generated.h"

class ADWPlayerCharacter;
class ADWEnemyCharacter;
class AActor;
class FDWCombatRules;

UENUM(BlueprintType)
enum class EDWCombatHUDReason:uint8
{
    None,NotReady,WrongDashForm,WrongRootForm,SpecialForDash,SpecialForRoot,
    FormTooShort,Cooldown,NotGrounded,Controlled,ActionLocked,NoAmmunition,
    EmptyMagazine,MagazineFull,WrongReloadWeapon,Unavailable
};
enum class EDWCombatHUDRequest:uint8 {Dash,Root,Primary,Reload};

USTRUCT(BlueprintType)
struct GDATTEST_API FDWCombatHUDResource
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) double Value=0.;
    UPROPERTY(BlueprintReadOnly) double Floor=0.;
    UPROPERTY(BlueprintReadOnly) double Maximum=0.;
    UPROPERTY(BlueprintReadOnly) double Fraction=0.;
};

/** Read-only projection. It never spends, advances a clock, captures a life, or grants an action. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWCombatHUDSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) bool bReady=false;
    UPROPERTY(BlueprintReadOnly) FDWCombatHUDResource Health;
    UPROPERTY(BlueprintReadOnly) FDWCombatHUDResource Water;
    UPROPERTY(BlueprintReadOnly) FDWCombatHUDResource Satiety;
    UPROPERTY(BlueprintReadOnly) FDWCombatHUDResource Special;
    UPROPERTY(BlueprintReadOnly) EDWCombatBaseForm Form=EDWCombatBaseForm::Normal;
    UPROPERTY(BlueprintReadOnly) double FormRemaining=0.;
    UPROPERTY(BlueprintReadOnly) bool bFormWarning=false;
    UPROPERTY(BlueprintReadOnly) FName EquipmentId;
    UPROPERTY(BlueprintReadOnly) FText EquipmentName;
    UPROPERTY(BlueprintReadOnly) bool bUsesAmmo=false;
    UPROPERTY(BlueprintReadOnly) bool bUsesMagazine=false;
    UPROPERTY(BlueprintReadOnly) int32 Reserve=0;
    UPROPERTY(BlueprintReadOnly) int32 Loaded=0;
    UPROPERTY(BlueprintReadOnly) int32 MagazineCapacity=0;
    /** Current native shell cycle, never inferred from animation playback or reserve stock. */
    UPROPERTY(BlueprintReadOnly) bool bShotgunReloading=false;
    UPROPERTY(BlueprintReadOnly) double ShotgunReloadProgress=0.;
    UPROPERTY(BlueprintReadOnly) double ShotgunReloadRemainingSeconds=0.;
    UPROPERTY(BlueprintReadOnly) double ShotgunReloadDurationSeconds=0.;
    UPROPERTY(BlueprintReadOnly) double DashCooldown=0.;
    UPROPERTY(BlueprintReadOnly) bool bDashing=false;
    UPROPERTY(BlueprintReadOnly) bool bRooted=false;
    UPROPERTY(BlueprintReadOnly) double MineCooldown=0.;
    UPROPERTY(BlueprintReadOnly) int32 LiveMines=0;
    UPROPERTY(BlueprintReadOnly) EDWCombatHUDReason DashReason=EDWCombatHUDReason::NotReady;
    UPROPERTY(BlueprintReadOnly) EDWCombatHUDReason RootReason=EDWCombatHUDReason::NotReady;
    UPROPERTY(BlueprintReadOnly) EDWCombatHUDReason AmmoReason=EDWCombatHUDReason::None;
};

struct FDWCombatHUDConfig
{
    FDWCombatPlayerVitalsConfig Player;
    FDWYeastDashConfig Dash;
    FDWMoldRootConfig Root;
    double FormWarningSeconds=0.,MessageSeconds=0.;
    int32 ShotgunCapacity=0;
    static bool Build(const FDWCombatRules&,FDWCombatHUDConfig&,FString& Error);
};

namespace DWCombatHUD
{
    GDATTEST_API FDWCombatHUDResource Resource(double Value,double Floor,double Maximum);
    GDATTEST_API EDWCombatHUDReason DashResourceReason(const FDWYeastDashConfig&,EDWCombatBaseForm,
        double Special,double FormRemaining,double Cooldown);
    GDATTEST_API EDWCombatHUDReason RootResourceReason(const FDWMoldRootConfig&,EDWCombatBaseForm,double Special,bool Grounded);
    GDATTEST_API bool Build(const ADWPlayerCharacter*,const FDWCombatHUDConfig&,FDWCombatHUDSnapshot&);
    GDATTEST_API bool ShouldShowEnemyHealth(const AActor*,const ADWPlayerCharacter*,bool Enabled);
    GDATTEST_API bool ShouldShowDaggerCircle(const ADWEnemyCharacter*,const ADWPlayerCharacter*);
    GDATTEST_API bool ShouldShowShotgunOverhead(const ADWPlayerCharacter*);
    GDATTEST_API FText FormLabel(const UObject*,EDWCombatBaseForm);
    GDATTEST_API FText ReasonText(const UObject*,EDWCombatHUDReason);
    // Only call after a fresh input was accepted and the actual gameplay request returned false.
    GDATTEST_API void NotifyRejected(ADWPlayerCharacter*,EDWCombatHUDRequest);
}
