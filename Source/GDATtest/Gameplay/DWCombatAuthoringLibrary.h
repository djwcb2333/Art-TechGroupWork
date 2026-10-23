#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWCombatAuthoringLibrary.generated.h"

class UAnimBlueprint;
class UAnimSequenceBase;
class USkeleton;

/** Editor authoring plus a narrowly scoped, in-place transition repair for the two combat AnimBPs. */
UCLASS()
class GDATTEST_API UDWCombatAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** PackagePath is a new /Game/.../ABP_Name package (no .ABP_Name suffix). */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Combat", meta=(DevelopmentOnly))
    static UAnimBlueprint* CreateCombatAnimationBlueprint(const FString& PackagePath, USkeleton* Skeleton,
        bool bEnemy, UAnimSequenceBase* PreviewIdleSequence, FString& OutMessage);

    /** Read-only JSON audit of graph nodes, GUIDs, pin links and transition settings. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Combat", meta=(DevelopmentOnly))
    static FString InspectCombatAnimationBlueprintTransitions(UAnimBlueprint* Blueprint);

    /** Disable UE 5.8 implicit self-transition inertialization in the existing two combat graphs.
        Refuses unexpected paths, parents, states, counts or non-standard transitions.
        Does not compile or save: inspect the report, then compile and save from the caller. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Combat", meta=(DevelopmentOnly))
    static bool RepairCombatAnimationBlueprintTransitions(UAnimBlueprint* Blueprint, FString& OutReport);

    /** Configure only this integration's three new combat graphs; preserve states, links and existing assets. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Combat", meta=(DevelopmentOnly))
    static bool ConfigureImportedCombatAnimationBlueprint(UAnimBlueprint* Blueprint,
        bool bLoopAlert, float LocomotionBlendSeconds, FString& OutReport);
};
