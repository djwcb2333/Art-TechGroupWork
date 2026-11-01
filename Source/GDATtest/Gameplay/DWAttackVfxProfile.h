#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DWCombatPresentationComponent.h"
#include "DWAttackVfxProfile.generated.h"
class UDWWeaponAnimationSet;
class UMaterialInterface;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class EDWAttackVfxAnchor : uint8 { CommittedWorld, AreaCarrier, Owner, CharacterBoneOrSocket, PrimaryWeapon, SecondaryWeapon, PrimaryAuthoredTip, SecondaryAuthoredTip, PrimaryMeshSocket, SecondaryMeshSocket };
UENUM(BlueprintType)
enum class EDWAttackVfxFollow : uint8 { SampleAtBirth, FollowAnchor };
USTRUCT(BlueprintType)
struct GDATTEST_API FDWAttackVfxEntry
{
 GENERATED_BODY()
 // Editor assigns once; unrelated to gameplay action/entity/save identities.
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid EntryId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FText Label;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=true;
 // Single authoritative effect TRS is Effect.LocalTransform. No second offset field.
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FDWCombatEffectBinding Effect;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) EDWAttackVfxAnchor Anchor=EDWAttackVfxAnchor::CommittedWorld;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) EDWAttackVfxFollow Follow=EDWAttackVfxFollow::SampleAtBirth;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName BoneOrSocket;
 // Presentation activation only, relative to a committed event; zero exactly preserves old spawning.
 // This is NOT the existing Niagara User.Delay parameter inside Effect (which remains unchanged).
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",Units="s")) float VisualDelaySeconds=0.f;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWAttackVfxEventGroup
{
 GENERATED_BODY()
 // A present empty group explicitly suppresses its old effect. Absent key preserves old Effects fallback.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(TitleProperty="Label")) TArray<FDWAttackVfxEntry> Entries;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWAttackVfxTrailChannel
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FGuid EntryId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UNiagaraSystem> System;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform LocalTransform=FTransform::Identity;
 // Default copies left-sample bTrailActive exactly. New window is a future explicit visual-only edit.
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bUseBakedWindow=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) float StartNormalized=0.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) float EndNormalized=1.f;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWAttackVfxTrailGroup
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FDWAttackVfxTrailChannel Primary;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FDWAttackVfxTrailChannel Secondary;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bAttachAtAuthoredTip=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName StartParameter=TEXT("User.TrailStart");
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName EndParameter=TEXT("User.TrailEnd");
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bParticleReplacesGeometry=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bUseGeometryTrail=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UMaterialInterface> GeometryMaterial;
};
USTRUCT(BlueprintType)
struct GDATTEST_API FDWAttackVfxSourceWindow
{
 GENERATED_BODY()
 FDWAttackVfxSourceWindow(){Entry.Anchor=EDWAttackVfxAnchor::PrimaryWeapon;Entry.Follow=EDWAttackVfxFollow::FollowAnchor;}
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FName ClipRole=TEXT("Attack");
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FDWAttackVfxEntry Entry;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) float StartNormalized=0.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) float EndNormalized=1.f;
};
/** Pure cosmetic data; no damage, aim, resources, clock ownership or gameplay calls. */
UCLASS(BlueprintType)
class GDATTEST_API UDWAttackVfxProfile : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual|Committed") TMap<EDWCombatPresentationKind,FDWAttackVfxEventGroup> EventOverrides;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual|Trails") bool bOverrideLegacyTrails=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual|Trails",meta=(EditCondition="bOverrideLegacyTrails")) FDWAttackVfxTrailGroup Trails;
 // New entries must be consumed by Runtime source-window dispatch, not preview-only decorations.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Visual|Source Windows") TArray<FDWAttackVfxSourceWindow> SourceWindows;
#if WITH_EDITORONLY_DATA
 // Read CLASS DEFAULTS only to resolve current real mesh/materials. Never instantiate a gameplay Actor.
 UPROPERTY(EditAnywhere,Category="Preview") TSoftClassPtr<AActor> PreviewSourceClass;
 UPROPERTY(EditAnywhere,Category="Preview") TSoftObjectPtr<UDWWeaponAnimationSet> PreviewWeaponSet;
 UPROPERTY(EditAnywhere,Category="Preview") FName PreviewClipRole=TEXT("Attack");
#endif
};
