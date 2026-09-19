#include "DWDamageVolumeLibrary.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWPlayerCharacter.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"

namespace DWDamageVolumes
{
    static const FName HitboxTag(TEXT("DWDamageHitbox"));
    static bool IsExplicit(const UPrimitiveComponent* C)
    {
        return IsValid(C) && (C->IsA<UDWDamageHitboxComponent>() || C->ComponentHasTag(HitboxTag));
    }
}

UDWDamageHitboxComponent::UDWDamageHitboxComponent()
{
    InitBoxExtent(FVector(50.f));
    SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    SetCollisionObjectType(ECC_WorldDynamic);
    SetCollisionResponseToAllChannels(ECR_Ignore);
    SetGenerateOverlapEvents(false);
    SetCanEverAffectNavigation(false);
    SetHiddenInGame(true);
    ShapeColor = FColor(255, 90, 40);
    ComponentTags.AddUnique(DWDamageVolumes::HitboxTag);
    PrimaryComponentTick.bCanEverTick = false;
}

bool UDWDamageVolumeLibrary::IsDamageHitComponent(const UPrimitiveComponent* Component)
{
    if (!IsValid(Component) || !Component->IsRegistered() || !Component->IsQueryCollisionEnabled()) return false;
    const AActor* Owner = Component->GetOwner();
    if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || !Owner->GetActorEnableCollision() || !Owner->CanBeDamaged()) return false;
    TInlineComponentArray<UPrimitiveComponent*> Components(Owner);
    bool bHasExplicit = false;
    for (const UPrimitiveComponent* C : Components) bHasExplicit |= DWDamageVolumes::IsExplicit(C);
    // A disabled explicit box must not accidentally fall back to the giant decorative mesh.
    if (bHasExplicit) return DWDamageVolumes::IsExplicit(Component);
    return Owner->IsA<ADWEnemyCharacter>() || Owner->IsA<ADWEnemyNest>() || Owner->IsA<ADWPlayerCharacter>();
}

TArray<AActor*> UDWDamageVolumeLibrary::GetDamageTargetsInVolume(UPrimitiveComponent* AttackVolume, const TArray<AActor*>& ActorsToIgnore)
{
    TArray<AActor*> Targets;
    if (!IsValid(AttackVolume) || !AttackVolume->GetWorld() || !AttackVolume->IsRegistered() || !AttackVolume->IsQueryCollisionEnabled()) return Targets;
    FComponentQueryParams Params(SCENE_QUERY_STAT(DWDamageVolume), AttackVolume->GetOwner());
    Params.AddIgnoredActors(ActorsToIgnore);
    TArray<FOverlapResult> Hits;
    AttackVolume->ComponentOverlapMulti(Hits, AttackVolume->GetWorld(), AttackVolume->GetComponentLocation(),
        AttackVolume->GetComponentQuat(), ECC_WorldDynamic, Params, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects));
    for (const FOverlapResult& Hit : Hits)
    {
        if (IsDamageHitComponent(Hit.GetComponent())) Targets.AddUnique(Hit.GetActor());
    }
    return Targets;
}

bool UDWDamageVolumeLibrary::IsTargetInDamageSphere(AActor* Target, FVector Center, float Radius)
{
    if (!IsValid(Target) || !FMath::IsFinite(Radius) || Radius <= 0.f || Center.ContainsNaN()) return false;
    TInlineComponentArray<UPrimitiveComponent*> Components(Target);
    for (UPrimitiveComponent* C : Components)
        if (IsDamageHitComponent(C) && C->OverlapComponent(Center, FQuat::Identity, FCollisionShape::MakeSphere(Radius))) return true;
    return false;
}


