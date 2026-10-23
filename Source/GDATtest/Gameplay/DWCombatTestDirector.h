#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCombatTestDirector.generated.h"

/** Place once in the dedicated, unsaveable combat test level. Uses the normal player, controller and HUD. */
UCLASS(Blueprintable)
class GDATTEST_API ADWCombatTestDirector : public AActor
{
    GENERATED_BODY()
public:
    ADWCombatTestDirector();
    virtual void Tick(float DeltaSeconds) override;

    /** Starts this test session once after the pawn and HUD have finished BeginPlay. Never creates a save slot. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Test",meta=(DisplayName="Auto Start Combat Test"))
    bool bAutoStartCombatTest=true;

    /** Adds alcohol to the temporary runtime inventory once. It does not change a Data Asset or save file. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Test",meta=(DisplayName="Grant Starting Alcohol"))
    bool bGrantStartingAlcohol=true;

    /** Sets only this session's pawn debug drawing; the player Blueprint remains unchanged. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Test",meta=(DisplayName="Enable Player Combat Debug"))
    bool bEnablePlayerCombatDebug=true;

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Test",meta=(ClampMin="0",ClampMax="1000",EditCondition="bGrantStartingAlcohol"))
    int32 StartingAlcoholAmount=20;

    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Test")
    bool bShowStartupMessage=true;

    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Test|Status")
    bool bTestSessionStarted=false;

    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Test|Status")
    bool bStartingAlcoholGranted=false;

protected:
    virtual void BeginPlay() override;

private:
    void TryStartTestSession();
};
