#pragma once
#include "CoreMinimal.h"
#include "DWEnemyCharacter.h"
#include "Components/SkinnedMeshComponent.h"
#include "DWSourDoughEnemy.generated.h"
class UInstancedStaticMeshComponent; class UAnimSequence; class UAnimInstance; class UDWSourDeathPresentationComponent;
UCLASS(Blueprintable)
class GDATTEST_API ADWSourDoughEnemy : public ADWEnemyCharacter
{
    GENERATED_BODY()
public:
    ADWSourDoughEnemy();
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Sour Dough|Death") TObjectPtr<UDWSourDeathPresentationComponent> SourDeathPresentation;
    // Native-only identity: callers must be this actor's actual current AnimInstance.
    uint64 GetSourDeathPresentationGeneration()const;
    bool NotifySourDeathAnimationCompleted(const UAnimInstance* Sender,uint64 Generation);
    void FinishSourDeathPresentation(uint64 Generation);
    // Transient presentation controls; no combat phase, damage or persistence is authored here.
    UPROPERTY(Transient,BlueprintReadOnly,Category="Sour Dough|Director") bool bDirectorPresentationControlled=false;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Sour Dough|Director") bool bDirectorAttackPose=false;
    UPROPERTY(Transient,BlueprintReadOnly,Category="Sour Dough|Director") float DirectorAttackNormalized=0.f;
    UFUNCTION(BlueprintCallable,Category="Sour Dough|Director") void SetDirectorPresentationControlled(bool bControlled,AActor* Director=nullptr);
    UFUNCTION(BlueprintCallable,Category="Sour Dough|Director") void SetDirectorAttackPose(float Normalized)
    { bDirectorAttackPose=true; DirectorAttackNormalized=FMath::IsFinite(Normalized) ? FMath::Clamp(Normalized,0.f,1.f) : 0.f; }
    UFUNCTION(BlueprintCallable,Category="Sour Dough|Director") void ClearDirectorAttackPose()
    { bDirectorAttackPose=false; DirectorAttackNormalized=0.f; }
    // Presentation only: formal combat keeps the inherited legacy attack slots empty.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Dough|Animation") TObjectPtr<UAnimSequence> SourAttackAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Dough|Eating") bool bWantsToEat=false;
    UFUNCTION(BlueprintCallable,Category="Sour Dough|Eating") void SetEatingRequested(bool bRequested) { bWantsToEat=bRequested; }
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Sour Dough|Crumbs") TObjectPtr<UInstancedStaticMeshComponent> CrumbInstances;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Dough|Crumbs") FVector CrumbOriginLocal=FVector(0.f,50.f,92.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Dough|Crumbs") FName CrumbAnchorBone=TEXT("bone_003");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Dough|Crumbs",meta=(ClampMin="0",ClampMax="30")) float CrumbRate=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Dough|Crumbs",meta=(ClampMin="0.05",ClampMax="3",Units="s")) float CrumbLifetime=.7f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Dough|Crumbs",meta=(ClampMin="0")) float CrumbGravity=160.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Dough|Crumbs",meta=(ClampMin="0.001",ClampMax="0.15")) float CrumbScale=.03f;
protected:
    virtual void BeginPlay() override;
    virtual bool BeginSpecializedDeathPresentation()override;
    virtual void CancelSpecializedDeathPresentation()override;
    virtual bool HideRestoredDeadWithoutPresentation()const override{return true;}
private:
    TWeakObjectPtr<AActor> PresentationDirector;
    ETickingGroup SavedPresentationMeshTickGroup=TG_PrePhysics;
    ETickingGroup SavedPresentationMeshEndTickGroup=TG_PrePhysics;
    EVisibilityBasedAnimTickOption SavedPresentationVisibilityTick=EVisibilityBasedAnimTickOption::AlwaysTickPose;
    bool bPresentationMeshTickCaptured=false;
    struct FCrumb { float Age=-1.f; FVector Start=FVector::ZeroVector; FVector Velocity=FVector::ZeroVector; };
    TArray<FCrumb> Crumbs;
    FRandomStream CrumbRandom;
    float SpawnAccumulator=0.f;
    int32 NextCrumb=0;
    FVector GetCrumbOriginLocal() const;
};
