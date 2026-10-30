#include "DWPlaceableBuilding.h"
#include "DWBuildingDefinition.h"
#include "DWBuildingEffectsComponent.h"
#include "DWInteractionPromptComponent.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

ADWPlaceableBuilding::ADWPlaceableBuilding()
{
    Mesh->SetStaticMesh(nullptr);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetRelativeScale3D(FVector::OneVector);
    BuildingCollision=CreateDefaultSubobject<UBoxComponent>(TEXT("BuildingCollision"));
    BuildingCollision->SetupAttachment(SceneRoot);
    BuildingCollision->SetCollisionProfileName(TEXT("BlockAllDynamic"));
    BuildingCollision->SetGenerateOverlapEvents(false);
    RandomScaleMin=RandomScaleMax=1.f;
    // Only authored parts participate in bounce/outline; the inherited empty cube is excluded.
    BuildingEffects->SourceComponentTag=TEXT("DWBuildingVisual");
}
void ADWPlaceableBuilding::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildBuildingVisuals();
}
void ADWPlaceableBuilding::BeginPlay()
{
    // Generated rendering components are transient. Rebuild also for saved/cooked level actors.
    RebuildBuildingVisuals();
    Super::BeginPlay();
}
bool ADWPlaceableBuilding::RebuildBuildingVisuals()
{
    if(BuildingEffects)BuildingEffects->Deactivate();
    for(const auto& Part:GeneratedParts)if(IsValid(Part.Get()))Part->DestroyComponent();
    GeneratedParts.Reset();bVisualsReady=false;
    if(!Definition)return false;
    FString Error;if(!Definition->ValidateDefinition(Error))return false;
    BuildingItemId=Definition->BuildingItemId;StationId=Definition->StationId;
    InteractionRadius=Definition->InteractionRadius;
    BuildingCollision->SetBoxExtent(Definition->FootprintHalfExtent);
    BuildingCollision->SetRelativeLocation(FVector(0,0,Definition->FootprintHalfExtent.Z));
    Prompt->GenericDetectionRadius=InteractionRadius;
    Prompt->WorldOffset=FVector(0,0,Definition->FootprintHalfExtent.Z*2.+25.);
    Prompt->GenericPromptTitle=FText::FromName(BuildingItemId);
    Prompt->GenericEnglishPromptTitle=FText::FromName(BuildingItemId);
    for(const auto& Part:Definition->Parts)
    {
        auto* Source=Part.Mesh.LoadSynchronous();if(!Source)return false;
        auto* Component=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient);
        Component->ComponentTags.Add(TEXT("DWBuildingVisual"));
        Component->SetupAttachment(SceneRoot);Component->SetStaticMesh(Source);
        Component->SetRelativeTransform(Part.RelativeTransform);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCanEverAffectNavigation(false);
        for(int32 I=0;I<Part.Materials.Num();++I)
            if(!Part.Materials[I].IsNull())if(auto* Material=Part.Materials[I].LoadSynchronous())Component->SetMaterial(I,Material);
        Component->RegisterComponent();GeneratedParts.Add(Component);
    }
    bVisualsReady=GeneratedParts.Num()==Definition->Parts.Num();
    if(BuildingEffects){BuildingEffects->Activate();BuildingEffects->RefreshVisualSources();}
    return bVisualsReady;
}
bool ADWPlaceableBuilding::IsInteractionAvailable() const
{
    return IsValid(Definition)&&bVisualsReady&&!IsStaged()&&!IsActorBeingDestroyed()&&!bDemolished&&bStationEnabled&&!StationId.IsNone();
}
void ADWPlaceableBuilding::PublishInteractionState()
{
    // Decorative definitions remain visible even when they do not expose a crafting station.
    const bool Visible=IsValid(Definition)&&bVisualsReady&&!IsStaged()&&!IsActorBeingDestroyed()&&!bDemolished;
    if(Visible&&GetWorld()&&GetWorld()->IsGameWorld())
        if(auto* GI=GetWorld()->GetGameInstance<UDWGameInstance>())GI->MarkUnsupportedGatheringPersistence(GetWorld());
    SetActorHiddenInGame(!Visible);SetActorEnableCollision(Visible);
    if(auto* GI=GetWorld()?GetWorld()->GetGameInstance<UDWGameInstance>():nullptr)
        if(auto* Config=GI->GetConfig())if(const auto* Item=Config->GetItemDefinition(BuildingItemId))
        {Prompt->GenericPromptTitle=Item->DisplayName;Prompt->GenericEnglishPromptTitle=Item->EnglishDisplayName;}
    Prompt->SetPromptEnabled(IsInteractionAvailable());
}
