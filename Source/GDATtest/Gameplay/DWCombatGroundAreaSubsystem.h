#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatGroundAreas.h"
#include "DWCombatGroundAreaSubsystem.generated.h"
class ADWPlayerCharacter;
class ADWEnemyCharacter;
class UStaticMeshComponent;
class UDWCombatAuthoritySubsystem;
struct FDWGroundAreaTick
{
    TWeakObjectPtr<ADWPlayerCharacter> Source;TWeakObjectPtr<AController> Instigator;
    TWeakObjectPtr<ADWEnemyCharacter> Target;
    DWCombatCommit::FEntityLife SourceLife,TargetLife;DWCombatCommit::FEmitterId Emitter;
    uint64 Ordinal=0;float Damage=0.f;
};

UCLASS(NotBlueprintable,NotPlaceable)
class GDATTEST_API ADWCombatGroundAreaMarker:public AActor
{
    GENERATED_BODY()
public:
    ADWCombatGroundAreaMarker();
    void ConfigureRadius(double RadiusCm);
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Disc;
};

/** Fixed world areas. Owns released emitter lifetime, never health or inventory. */
UCLASS()
class GDATTEST_API UDWCombatGroundAreaSubsystem:public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    static UDWCombatGroundAreaSubsystem* Get(const UObject* Context);
    virtual void Tick(float DeltaSeconds)override;
    virtual void Deinitialize()override;
    virtual TStatId GetStatId()const override;
    EDWAreaAdoption AdoptReleasedAlcohol(const FDWAlcoholAreaSnapshot&,TWeakObjectPtr<ADWPlayerCharacter>,TWeakObjectPtr<AController>,
        DWCombatCommit::FEntityLife,DWCombatCommit::FEmitterId,FVector Ground,double ImpactGameSeconds);
    EDWAreaAdoption AdoptReleasedBowSlow(const FDWBowSlowAreaSnapshot&,TWeakObjectPtr<ADWPlayerCharacter>,TWeakObjectPtr<AController>,
        DWCombatCommit::FEntityLife,DWCombatCommit::FEmitterId,FVector Ground,double ImpactGameSeconds);
    int32 GetLiveAreaCount()const;
    bool HasUnserializedReleasedAreas()const{return GetLiveAreaCount()>0||!Targets.IsEmpty();}
    // Shared native ground query for arrow/bottle impact. Does not create, charge, or damage anything.
    static bool FindLegalGround(UWorld*,FVector Impact,AActor* Ignore,FVector& Out);
    double GetStrongestSlowAt(const ADWEnemyCharacter&,double WorldSeconds)const;
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type)const override;
private:
    friend class UDWCombatAuthoritySubsystem;
    friend class FDWCombatEffectsPersistence;
    friend struct FDWCombatEffectsRestoreState;
    struct FArea
    {
        DWCombatCommit::FEmitterId Emitter;
        DWCombatCommit::FEntityLife SourceLife;
        TWeakObjectPtr<ADWPlayerCharacter> Source;
        TWeakObjectPtr<AController> Instigator;
        TWeakObjectPtr<ADWCombatGroundAreaMarker> Marker;
        FVector Ground=FVector::ZeroVector;
        double Started=0.,Ends=0.,RadiusCm=0.,SlowFraction=0.,Damage=0.,Period=0.;
        bool bActive=true;
    };
    struct FTarget
    {
        TWeakObjectPtr<ADWEnemyCharacter> Actor;
        DWCombatCommit::FEntityLife Life;
        FDWCombatAreaTargetClock Clock;
    };
    TMap<uint64,FArea> Areas; // Emitter IDs are unique within the retained world epoch.
    TMap<uint64,FTarget> Targets;
    uint64 Epoch=0;
    bool bStopping=false,bAdvancing=false;
    bool Operational()const;
    EDWAreaAdoption Adopt(const FArea&);
    void DiscoverTargets(double At);
    bool Covers(const FArea&,const ADWEnemyCharacter&,double At,bool IncludeEnd)const;
    bool FindStrongestAlcohol(const ADWEnemyCharacter&,double At,FArea& Out)const;
    void Materialize(uint64 Id);
    void RetireExpired(double Through);
};
