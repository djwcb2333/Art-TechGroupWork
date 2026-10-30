#include "DWBuildingDefinition.h"
#include "DWPlaceableBuilding.h"

bool UDWBuildingDefinition::ValidateDefinition(FString& Error) const
{
    Error.Reset();
    const auto Fail=[&Error](const TCHAR* Message){Error=Message;return false;};
    if(BuildingItemId.IsNone()||ActorClass.IsNull())return Fail(TEXT("Building identity and actor class are required."));
    if(Parts.IsEmpty())return Fail(TEXT("At least one building mesh part is required."));
    if(FootprintHalfExtent.ContainsNaN()||FootprintHalfExtent.GetMin()<=0.)return Fail(TEXT("Footprint half extent must be finite and positive."));
    if(!FMath::IsFinite(MaxPlacementDistance)||MaxPlacementDistance<20.f||!FMath::IsFinite(MaxSlopeDegrees)||MaxSlopeDegrees<0.f||MaxSlopeDegrees>60.f||
       !FMath::IsFinite(MaxSupportHeightDifference)||MaxSupportHeightDifference<0.f||!FMath::IsFinite(InteractionRadius)||InteractionRadius<=0.f||StackLimit<1||StackLimit>999)
        return Fail(TEXT("Invalid building placement, interaction or stack limit."));
    for(const auto& Part:Parts)
        if(Part.Mesh.IsNull()||Part.RelativeTransform.ContainsNaN()||!Part.RelativeTransform.GetRotation().IsNormalized()||Part.RelativeTransform.GetScale3D().GetMin()<=0.)
            return Fail(TEXT("Each building part needs a mesh and a finite positive transform."));
    return true;
}
