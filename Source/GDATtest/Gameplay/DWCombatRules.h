#pragma once
#include "CoreMinimal.h"

/** Explicit numeric units. A failed read never substitutes legacy tuning. */
enum class EDWCombatRuleUnit : uint8
{
    Points, PointsPerSecond, PointsPerEvent, Seconds, MetersPerSecond, Multiplier,
    CoreCount, Meters, HitCount, ObjectCount, Probability, FullDegrees, Fraction,
    RoundCount, ArrowCount, BottleCount, DegreesPerSecond, Version,
    Centimeters, CentimetersPerSecond
};

/** Immutable numeric configuration after load; the owner supplies lifecycle and verifies UE bindings.
 * The payload's textual rules are provenance, not proof that the 71 gameplay decisions are implemented.
 * No textual rule getter is exposed: numeric-ready must never be presented as gameplay-ready.
 */
class GDATTEST_API FDWCombatRules final
{
public:
    static bool LoadFromJson(const FString& Json, TSharedPtr<const FDWCombatRules>& OutRules, FString& OutError);
    static bool LoadProjectPayload(TSharedPtr<const FDWCombatRules>& OutRules, FString& OutError);
    bool TryGetNumber(FName Id, EDWCombatRuleUnit RequestedUnit, double& OutValue, FString& OutError) const;
    bool TryGetCount(FName Id, int32& OutValue, FString& OutError) const;
    const FString& GetDesignVersion() const { return DesignVersion; }
    const FString& GetSourceHash() const { return SourceHash; }
    int32 GetCombatSchema() const { return CombatSchema; }
    int32 GetBaseParameterCount() const { return BaseParameterCount; }
    int32 GetDerivedParameterCount() const { return DerivedParameterCount; }
    int32 GetUnboundEquipmentCount() const { return UnboundEquipmentCount; }
    struct FValue
    {
        double Value = 0.;
        EDWCombatRuleUnit Unit = EDWCombatRuleUnit::Points;
        bool bCount = false;
        bool bDerived = false;
    };
    const TMap<FName,FValue>& GetValidatedValues() const { return Values; }
private:
    FDWCombatRules() = default;
    TMap<FName,FValue> Values;
    FString DesignVersion;
    FString SourceHash;
    int32 CombatSchema = 0;
    int32 BaseParameterCount = 0;
    int32 DerivedParameterCount = 0;
    int32 UnboundEquipmentCount = 0;
};
