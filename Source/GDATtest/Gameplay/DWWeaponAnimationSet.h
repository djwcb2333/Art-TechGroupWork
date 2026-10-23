#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DWWeaponAnimationSet.generated.h"

class UAnimSequenceBase;
class UStaticMesh;
class UMaterialInterface;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class EDWAnimatedWeapon : uint8 { Spear, Greatsword, ShieldAxe, DualDaggers, SporeGun, Shotgun, MoldBow, Alcohol, Unarmed };

/** Explicit gameplay event. Visual accents never implicitly add damage. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWWeaponDamageEvent
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float TimeSeconds=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float DamageMultiplier=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float RangeMultiplier=1.f;
};

USTRUCT(BlueprintType)
struct GDATTEST_API FDWWeaponGeometry
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FVector> Vertices;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<int32> Triangles;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FVector2D> UVs;
};

/** Exact authored proxy shape keys and trail mesh, separate from the character FBX. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWWeaponGeometryFrame
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float TimeSeconds=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FVector> PrimaryVertices;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FVector> SecondaryVertices;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FVector> TrailVertices;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<int32> TrailTriangles;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FVector> SecondaryTrailVertices;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<int32> SecondaryTrailTriangles;
};

/** Baked in actor-local centimetres. Character skeleton and socket hierarchy remain unchanged. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWWeaponVisualSample
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float TimeSeconds=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform PrimaryTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform SecondaryTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bPrimaryVisible=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bSecondaryVisible=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bTrailActive=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector PrimaryTrailRoot=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector PrimaryTrailTip=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector SecondaryTrailRoot=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector SecondaryTrailTip=FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct GDATTEST_API FDWWeaponAnimationClip
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName Role=TEXT("Attack");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UAnimSequenceBase> Animation;
    /** Source clock, including fractional 24 fps endpoints. Never rounded to exported frames. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.001",Units="s")) float DurationSeconds=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bLoop=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="s")) float HeldStartSeconds=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(Units="s")) float HeldEndSeconds=1000.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FDWWeaponDamageEvent> DamageEvents;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FDWWeaponVisualSample> VisualSamples;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FDWWeaponGeometryFrame> GeometryFrames;
};

/** One asset per weapon; animation, visuals and damage parameters can be tuned independently. */
UCLASS(BlueprintType)
class GDATTEST_API UDWWeaponAnimationSet : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Identity") EDWAnimatedWeapon Weapon=EDWAnimatedWeapon::Spear;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Identity") FText DisplayName;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Animation",meta=(TitleProperty="Role")) TArray<FDWWeaponAnimationClip> Clips;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0.01",Units="s")) float AttackIntervalSeconds=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing",meta=(ClampMin="0",Units="s")) float ChargeSeconds=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing") bool bHoldAtFullCharge=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing") bool bRepeatWhileHeld=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Timing") bool bLockMovementDuringAction=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage",meta=(ClampMin="0")) float Damage=25.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage",meta=(ClampMin="1",Units="cm")) float DamageRange=180.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage",meta=(ClampMin="1",ClampMax="360")) float ArcDegrees=80.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage",meta=(ClampMin="0",Units="cm")) float KnockbackDistance=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage",meta=(ClampMin="0.01",Units="s")) float KnockbackSeconds=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage") bool bSingleTarget=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage") bool bApplyBasicDamage=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Defense",meta=(ClampMin="0",ClampMax="1")) float BlockDamageReduction=.75f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Defense",meta=(ClampMin="0",Units="s")) float PerfectBlockSeconds=.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Reload",meta=(ClampMin="0")) int32 MagazineCapacity=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") TObjectPtr<UStaticMesh> PrimaryMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") TObjectPtr<UStaticMesh> SecondaryMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") FDWWeaponGeometry PrimaryGeometry;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") FDWWeaponGeometry SecondaryGeometry;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") TObjectPtr<UMaterialInterface> WeaponMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") FName PrimaryBone=TEXT("Spear");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") FName SecondaryBone=TEXT("bone_005_L");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") FTransform PrimaryGrip=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") FTransform SecondaryGrip=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") FTransform PrimaryBack=FTransform(FRotator(0,0,30),FVector(-35,20,20));
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") FTransform SecondaryBack=FTransform(FRotator(0,0,-30),FVector(-35,-20,20));
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual",meta=(ClampMin="0.01")) float VisualScale=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual") bool bUseBakedVisualSamples=true;
    /** Optional user-authored effects. Empty slots intentionally produce no trail. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Primary") TObjectPtr<UNiagaraSystem> PrimaryTrailSystem;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Primary",meta=(Units="cm")) FVector PrimaryTrailOffset=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Primary") FRotator PrimaryTrailRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Primary") FVector PrimaryTrailScale=FVector::OneVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Secondary") TObjectPtr<UNiagaraSystem> SecondaryTrailSystem;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Secondary",meta=(Units="cm")) FVector SecondaryTrailOffset=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Secondary") FRotator SecondaryTrailRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Secondary") FVector SecondaryTrailScale=FVector::OneVector;
    /** Follow the animated tip for ribbons; false attaches at the weapon's origin. Offset remains weapon-local. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Attachment") bool bAttachTrailAtAuthoredTip=true;
    /** Optional Niagara Vector3 user parameters containing world-space centimetres. They are not required for playback. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Parameters") FName TrailStartParameterName=TEXT("User.TrailStart");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Parameters") FName TrailEndParameterName=TEXT("User.TrailEnd");
    /** Legacy authored mesh trail is debug-only. Disabled means no trail mesh creation or tangent/geometry work. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Debug") bool bUseGeometryTrail=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Trail|Debug",meta=(EditCondition="bUseGeometryTrail")) TObjectPtr<UMaterialInterface> TrailMaterial;

    const FDWWeaponAnimationClip* FindClip(FName Role) const;
};
