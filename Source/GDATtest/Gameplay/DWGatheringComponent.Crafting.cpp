#include "DWGatheringComponent.h"
#include "DWGatheringWorld.h"
#include "DWGatheringPond.h"
#include "DWGameplayConfig.h"
#include "DWBuildingDefinition.h"
#include "DWLanguageCarrier.h"
#include "DWLanguageSubsystem.h"
#include "DWPlayerCharacter.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
TArray<FName> UDWGatheringComponent::GetNearbyCraftingStations() const
{
 TArray<FName> Result;auto* U=Player();
 if(!IsValid(U)||U->IsDead()||U->IsActorBeingDestroyed()||!U->GetGameplayConfig()||!GetWorld()||GetWorld()->bIsTearingDown||
  !FMath::IsFinite(CraftingStationRadiusCm)||CraftingStationRadiusCm<=0.f)return Result;
 const double Radius=FMath::Min(double(CraftingStationRadiusCm),100000.);
 for(TActorIterator<ADWGatheringStation> It(GetWorld());It;++It)
  if(It->IsInteractionAvailable()&&FMath::IsFinite(It->HeightTolerance)&&
   FMath::Abs(U->GetActorLocation().Z-It->GetActorLocation().Z)<=FMath::Max(0.f,It->HeightTolerance)&&
   FVector::DistSquared2D(U->GetActorLocation(),It->GetActorLocation())<=FMath::Square(Radius))Result.AddUnique(It->StationId);
 // The pond remains a shaped, stocked station; its visual mesh origin is not its usable boundary.
 for(TActorIterator<ADWGatheringPond> It(GetWorld());It;++It)
  if(It->IsUsableWaterStation()&&It->ContainsActor(U,Radius))Result.AddUnique(ADWGatheringPond::WaterStationId());
 if(auto* L=U->GetGameInstance()->GetSubsystem<UDWLanguageSubsystem>();L&&L->IsReady())
  for(TActorIterator<ADWLanguageCarrier> It(GetWorld());It;++It)
   if(It->Kind==EDWLanguageCarrierKind::WaterSource&&!It->IsActorBeingDestroyed()&&!It->StableId.IsNone()&&
    FMath::IsFinite(It->HeightTolerance)&&It->HeightTolerance>0.f&&FMath::Abs(U->GetActorLocation().Z-It->GetActorLocation().Z)<=It->HeightTolerance&&
    FVector::DistSquared2D(U->GetActorLocation(),It->GetActorLocation())<=FMath::Square(Radius))Result.AddUnique(ADWGatheringPond::WaterStationId());
 Result.Remove(NAME_None);return Result;
}
TArray<FName> UDWGatheringComponent::GetRecipeStationRequirements(FName Id) const
{
 TArray<FName> Result;const auto* U=Player();const auto* C=U?U->GetGameplayConfig():nullptr;
 const auto* R=C?C->GetRecipeDefinition(Id):nullptr;if(!R)return Result;
 const FName Station=C->GetRequiredStation(Id);if(!Station.IsNone())Result.AddUnique(Station);
 for(const auto& O:R->Outputs)if(const auto* B=C->GetBuildingDefinition(O.ItemId))
  if(!B->RequiredCraftingStation.IsNone())Result.AddUnique(B->RequiredCraftingStation);
 return Result;
}
bool UDWGatheringComponent::EvaluateRecipeStationRequirements(FName Id,TArray<FName>& Missing) const
{
 Missing.Reset();const auto* U=Player();const auto* C=U?U->GetGameplayConfig():nullptr;
 if(!U||U->IsDead()||!C||!C->GetRecipeDefinition(Id)||!GetWorld()||GetWorld()->bIsTearingDown)return false;
 const auto Available=GetNearbyCraftingStations();for(FName Required:GetRecipeStationRequirements(Id))
  if(!Available.Contains(Required))Missing.Add(Required);return Missing.IsEmpty();
}
bool UDWGatheringComponent::HasCraftingContext(FName Recipe,FName Station,FName Building) const
{
 auto* U=Player();const auto* C=U?U->GetGameplayConfig():nullptr;
 if(!C||!U||U->IsDead()||U->IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)return false;
 const auto Available=GetNearbyCraftingStations();
 for(FName Required:GetRecipeStationRequirements(Recipe))if(!Available.Contains(Required))return false;
 if(!Station.IsNone()&&!Available.Contains(Station))return false;
 if(!Building.IsNone()){FTransform Placement;return FindBuildingPlacement(Building,Placement);}return true;
}
