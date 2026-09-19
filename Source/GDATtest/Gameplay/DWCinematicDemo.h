#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWGameplayGameMode.h"
#include "DWGameplayHUD.h"
#include "DWCinematicDemo.generated.h"
class UDWWorldEventComponent;
class UStaticMeshComponent;
UCLASS()
class GDATTEST_API ADWCinematicDemoHUD:public ADWGameplayHUD
{
 GENERATED_BODY()
protected:
 virtual void BeginPlay()override;
};
UCLASS()
class GDATTEST_API ADWCinematicDemoGameMode:public ADWGameplayGameMode
{
 GENERATED_BODY()
public:
 ADWCinematicDemoGameMode();
protected:
 virtual void BeginPlay()override;
};
UCLASS(Blueprintable)
class GDATTEST_API ADWBridgeChangeDemo:public AActor
{
 GENERATED_BODY()
public:
 ADWBridgeChangeDemo();
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge") TObjectPtr<UDWWorldEventComponent> WorldEvent;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge") TObjectPtr<USceneComponent> LeftHinge;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Bridge") TObjectPtr<USceneComponent> RightHinge;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Bridge",meta=(ClampMin=".05",Units="s")) float BreakSeconds=1.4f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Bridge",meta=(ClampMin="5",ClampMax="160")) float FallAngle=78;
 UFUNCTION(BlueprintPure,Category="Bridge") float GetBreakProgress()const{return Progress;}
 virtual void Tick(float Dt)override;
protected:
 virtual void PostInitializeComponents()override;
private:
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Planks;
 bool bBreaking=false;float Progress=0;
 void ApplyPose(float T);
 UFUNCTION() void StartBreak();
 UFUNCTION() void ApplyFinal(bool Completed);
};
UCLASS()
class GDATTEST_API UDWCinematicAuthoringLibrary:public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Creates new reusable assets only; installs components on six existing HUD groups without replacing them. */
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Authoring") static bool CreateCinematicAssets();
};
