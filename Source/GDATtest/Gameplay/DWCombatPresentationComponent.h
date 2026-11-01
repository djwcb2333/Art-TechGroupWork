#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWCombatPresentationComponent.generated.h"
class UDWAttackVfxProfile;
class UDWWeaponAnimationSet;
struct FDWWeaponAnimationClip;
struct FDWAttackVfxEntry;
struct FDWAttackVfxPlaybackState;
struct GDATTEST_API FDWAttackVfxPlaybackStateDeleter { void operator()(FDWAttackVfxPlaybackState*) const; };
namespace DWWeaponVisualSampler { struct FRequest;struct FFrame; }
class UFXSystemAsset;
class UFXSystemComponent;
UENUM(BlueprintType)
enum class EDWCombatPresentationKind : uint8 { BowSlowArea, GreatswordSlam, SporeShot, ShotgunShot, DaggerAssassination, GiantSlam };
USTRUCT(BlueprintType)
struct FDWCombatEffectBinding
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UFXSystemAsset> System;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform LocalTransform=FTransform::Identity;
 /** Optional, exact exposed Niagara parameters. None preserves the asset default. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName ColorParameter;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FLinearColor Color=FLinearColor::White;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName SecondaryColorParameter;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FLinearColor SecondaryColor=FLinearColor::White;
 /** Set only after the approved R02 clone defines and verifies a Float input in degrees. None sends no angle. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName GiantHalfAngleDegreesParameter;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName DelayParameter;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0")) float DelaySeconds=0.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName DurationParameter;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01")) float DurationSeconds=.65f;
 /** Hard visual lifetime for repeating muzzle systems; never a gameplay interval. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01",Units="s")) float OneShotLifetimeSeconds=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="1",Units="cm")) float AreaReferenceRadiusCm=100.f;
};
/** Presentation only. Native committed events own trigger identity; this component never submits gameplay. */
UCLASS(ClassGroup=(DoughWorld),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWCombatPresentationComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UDWCombatPresentationComponent();
 virtual ~UDWCombatPresentationComponent() override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat|Presentation") TObjectPtr<UDWAttackVfxProfile> AttackVfxProfile;
 // Cosmetic-only updates from the existing visual tick, never a gameplay/preview trigger.
 void UpdateWeaponVisuals(const UDWWeaponAnimationSet&,const FDWWeaponAnimationClip&,const DWWeaponVisualSampler::FRequest&,const DWWeaponVisualSampler::FFrame&,uint32 VisualSerial);
 void ClearWeaponVisuals();
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat|Presentation") TMap<EDWCombatPresentationKind,FDWCombatEffectBinding> Effects;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Combat|Presentation") float LastCommittedGiantHalfAngleDegrees=180.f;
 void PublishCommitted(EDWCombatPresentationKind Kind,uint64 Epoch,uint64 Identity,FTransform Transform,uint32 Ordinal=1,double AuthoritativeRadiusCm=0.,double AuthoritativeHalfAngleDegrees=180.);
 bool MaterializeBowArea(AActor* Carrier,double Radius,double RemainingSeconds);
private:
 TUniquePtr<FDWAttackVfxPlaybackState,FDWAttackVfxPlaybackStateDeleter> VisualState;
 UFXSystemComponent* SpawnVisualEntry(const FDWAttackVfxEntry&,EDWCombatPresentationKind,const FTransform&,double Radius,double HalfAngle,bool bCommitted=true);
 void UpdateFollowingVisuals();
 uint64 SeenEpoch=0;
 TMap<EDWCombatPresentationKind,TArray<FString>> Recent;
 UFXSystemComponent* CreateEffect(AActor* Carrier,const FDWCombatEffectBinding&,const FTransform&,bool Attach,double HalfAngleDegrees=180.);
};
