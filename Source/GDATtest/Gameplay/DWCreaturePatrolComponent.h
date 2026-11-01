#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "NavigationData.h"
#include "DWCreaturePatrolComponent.generated.h"
UENUM(BlueprintType)
enum class EDWCreaturePatrolMode : uint8 { Idle, BuildingPatrol };
UCLASS(ClassGroup=(DoughWorld), meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWCreaturePatrolComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UDWCreaturePatrolComponent();
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol") EDWCreaturePatrolMode PatrolMode=EDWCreaturePatrolMode::Idle;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol") TObjectPtr<AActor> AnchorActor;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol") FVector AnchorOffset=FVector::ZeroVector;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol", meta=(ClampMin="1")) float PatrolRadius=400;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol", meta=(ClampMin="0")) float InnerRadius=100;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol", meta=(ClampMin="0.1")) float WaitSeconds=2;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol", meta=(ClampMin="0.25", ClampMax="5")) float DecisionInterval=0.5f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol", meta=(ClampMin="1", ClampMax="12")) int32 NavigationAttempts=6;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol") FVector ProjectionExtent=FVector(100,100,300);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Dough World|Patrol") float MaxVerticalDifference=150;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Dough World|Patrol") FName LastDecision=TEXT("NotStarted");
 UFUNCTION(BlueprintCallable, Category="Dough World|Patrol") void StartPatrol();
 UFUNCTION(BlueprintCallable, Category="Dough World|Patrol") void StopPatrol();
 UFUNCTION(BlueprintCallable, Category="Dough World|Patrol") void RefreshPatrol();
 UFUNCTION(BlueprintCallable, Category="Dough World|Patrol") void SetPatrolAnchor(AActor* Anchor);
 UFUNCTION(BlueprintCallable, Category="Dough World|Patrol") void SetPatrolMode(EDWCreaturePatrolMode Mode);
 // Native locomotion capability: returned path is exactly the route subsequently executed.
 FNavPathSharedPtr AcquireValidatedRoute(const FVector& Goal);
 bool IsRouteCurrent(const FNavPathSharedPtr& Path) const;
 void MaintainPatrolSchedule();
 UFUNCTION(BlueprintCallable, Category="Dough World|Patrol") void SetPatrolRadii(float OuterRadius,float InnerBandRadius);
 virtual void Deactivate() override;
protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 bool bRequested=false;
 FTimerHandle DecisionTimer;
 bool ValidConfiguration() const;
 bool ChooseGoal(FVector& Goal);
 bool ValidateRoute(const FNavPathSharedPtr& Path) const;
 bool SettingsMatch() const;
 void CaptureSettings();
 FNavPathSharedPtr ValidatedRoute;
 TWeakObjectPtr<AActor> RouteAnchor;
 FTransform RouteAnchorTransform;
 FVector RouteOffset,RouteProjectionExtent,RouteStartLocation;
 float RouteRadius=0,RouteInner=0,RouteVertical=0,RouteWait=0,RouteInterval=0;
 int32 RouteAttempts=0;
 uint64 DecisionGeneration=0;
 void Decide();
};
