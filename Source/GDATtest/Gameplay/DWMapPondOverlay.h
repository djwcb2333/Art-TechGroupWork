#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWMapImage.h"
#include "DWMapPondOverlay.generated.h"
class ADWGatheringPond;
/** Explicit native pond registry, scoped to one World. No actor enumeration or save facts. */
UCLASS()
class GDATTEST_API UDWMapPondOverlay : public UWorldSubsystem
{
 GENERATED_BODY()
public:
 void Register(ADWGatheringPond* Pond);
 void Unregister(ADWGatheringPond* Pond);
 const TArray<uint8>& Prepare(const FDWMapImageSettings& Frame,int32 Resolution);
 uint64 GetRevision()const{return Revision;}
 virtual void Deinitialize()override;
protected:
 virtual bool DoesSupportWorldType(EWorldType::Type Type)const override{return Type==EWorldType::Game||Type==EWorldType::PIE;}
private:
 TArray<TWeakObjectPtr<ADWGatheringPond>> Ponds;
 TArray<uint8> Coverage;
 TArray<FVector2D> PreviousGeometry;
 FDWMapImageSettings Frame;
 int32 Grid=0;
 uint64 Revision=0;
 double NextCheck=-1.;
 bool bDirty=true,bCollecting=false,bRasterizing=false,bFrameDirty=true;
 int32 Probe=0,PolygonIndex=0,Row=0;
 TArray<TArray<FVector2D>> Snapshot;
 TArray<FVector2D> Signature;
 TArray<TArray<FVector2D>> UVPolygons;
 TArray<uint8> Work;
};
