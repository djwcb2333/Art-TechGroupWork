#include "DepthLandscapeTools.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"

#if WITH_EDITOR
#include "Engine/Level.h"
#include "Engine/Texture2D.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEditLayer.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogDepthLandscapeTools, Log, All);

bool UDepthLandscapeTools::ImportVisibilityMask(ALandscape* Landscape, UTextureRenderTarget2D* RenderTarget, int32 EditLayerIndex)
{
#if WITH_EDITOR
    const FString CandidateRoot(TEXT("/Game/DoughWorld/Maps/DoughWorldDepth/"));
    const FString ExternalActorRoot(TEXT("/Game/__ExternalActors__/DoughWorld/Maps/DoughWorldDepth/"));
    auto IsCandidatePackage = [&CandidateRoot, &ExternalActorRoot](const UObject* Object)
    {
        if (!IsValid(Object)) return false;
        const FString Name = Object->GetOutermost()->GetName();
        return Name.StartsWith(CandidateRoot) || Name.StartsWith(ExternalActorRoot);
    };
    if (!IsValid(Landscape) || !IsValid(RenderTarget) || !RenderTarget->GetResource())
    {
        UE_LOG(LogDepthLandscapeTools, Error, TEXT("Visibility import: invalid landscape or uninitialized render target."));
        return false;
    }
    UWorld* World = Landscape->GetWorld();
    if (!IsValid(World) || World->WorldType != EWorldType::Editor ||
        !World->GetOutermost()->GetName().StartsWith(CandidateRoot) ||
        !IsCandidatePackage(Landscape->GetLevel()))
    {
        UE_LOG(LogDepthLandscapeTools, Error, TEXT("Visibility import is restricted to non-PIE editor maps under /Game/DoughWorld/Maps/DoughWorldDepth/."));
        return false;
    }
    ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
    const ULandscapeEditLayerBase* EditLayer = Landscape->GetEditLayer(EditLayerIndex);
    if (!Info || !EditLayer || !ALandscapeProxy::VisibilityLayer)
    {
        UE_LOG(LogDepthLandscapeTools, Error, TEXT("Visibility import: missing LandscapeInfo, edit layer %d, or engine VisibilityLayer."), EditLayerIndex);
        return false;
    }
    int32 MinX = 0, MinY = 0, MaxX = 0, MaxY = 0;
    if (!Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY) ||
        RenderTarget->SizeX < MaxX - MinX + 1 || RenderTarget->SizeY < MaxY - MinY + 1)
    {
        UE_LOG(LogDepthLandscapeTools, Error, TEXT("Visibility import: render target must cover every landscape vertex."));
        return false;
    }

    // Reject shared/original map data before registration or native weightmap writes.
    bool bCandidateOnly = true;
    Info->ForEachLandscapeProxy([&](ALandscapeProxy* Proxy)
    {
        if (!IsValid(Proxy) || Proxy->GetWorld() != World || !IsCandidatePackage(Proxy->GetLevel()))
        {
            bCandidateOnly = false;
            return false;
        }
        TArray<ULandscapeComponent*> Components;
        Proxy->GetComponents<ULandscapeComponent>(Components);
        for (ULandscapeComponent* Component : Components)
        {
            // Both the selected edit-layer weights and merged weights can be touched by recomposition.
            for (UTexture2D* Texture : Component->GetWeightmapTextures(EditLayer->GetGuid()))
                if (Texture && !IsCandidatePackage(Texture)) bCandidateOnly = false;
            for (UTexture2D* Texture : Component->GetWeightmapTextures(false))
                if (Texture && !IsCandidatePackage(Texture)) bCandidateOnly = false;
        }
        return bCandidateOnly;
    });
    if (!bCandidateOnly)
    {
        UE_LOG(LogDepthLandscapeTools, Error, TEXT("Visibility import refused: a landscape proxy or weightmap still belongs to content outside the candidate root."));
        return false;
    }

    // In 5.8 UpdateLayerInfoMap only rebuilds existing target-layer keys. A new Landscape
    // with no Visibility key therefore cannot resolve the engine's special LayerInfo object.
    // Register that canonical object first, rather than inventing a normal painted layer.
    Info->CreateTargetLayerSettingsFor(ALandscapeProxy::VisibilityLayer);
    Info->UpdateLayerInfoMap(Landscape, false);
    const FName VisibilityName = ALandscapeProxy::VisibilityLayer->GetLayerName();
    const int32 Index = Info->GetLayerInfoIndex(VisibilityName, Info->GetLandscapeProxy());
    if (!Info->Layers.IsValidIndex(Index) || Info->Layers[Index].LayerInfoObj != ALandscapeProxy::VisibilityLayer)
    {
        UE_LOG(LogDepthLandscapeTools, Error, TEXT("Visibility registration did not resolve the canonical LayerInfo."));
        return false;
    }

    // The existing engine function performs RT readback, edit-layer selection, transactional
    // alpha writes and weightmap update requests. The caller must wait for updates then save.
    // This editor UFUNCTION has no LANDSCAPE_API export in 5.8. Dispatch through reflection,
    // using property offsets/types instead of relying on an assumed native parameter layout.
    UFunction* ImportFunction = Landscape->FindFunction(TEXT("LandscapeImportWeightmapFromRenderTarget"));
    if (!ImportFunction)
    {
        UE_LOG(LogDepthLandscapeTools, Error, TEXT("Native reflected weightmap import function was not found."));
        return false;
    }
    FObjectPropertyBase* RTProperty = FindFProperty<FObjectPropertyBase>(ImportFunction, TEXT("InRenderTarget"));
    FNameProperty* LayerProperty = FindFProperty<FNameProperty>(ImportFunction, TEXT("InLayerName"));
    FIntProperty* EditIndexProperty = FindFProperty<FIntProperty>(ImportFunction, TEXT("InEditLayerIndex"));
    FBoolProperty* ReturnProperty = FindFProperty<FBoolProperty>(ImportFunction, TEXT("ReturnValue"));
    if (!RTProperty || !LayerProperty || !EditIndexProperty || !ReturnProperty)
    {
        UE_LOG(LogDepthLandscapeTools, Error, TEXT("Native reflected weightmap import parameters do not match the checked UE 5.8 signature."));
        return false;
    }
    FStructOnScope Parameters(ImportFunction);
    uint8* ParameterMemory = Parameters.GetStructMemory();
    RTProperty->SetObjectPropertyValue_InContainer(ParameterMemory, RenderTarget);
    LayerProperty->SetPropertyValue_InContainer(ParameterMemory, VisibilityName);
    EditIndexProperty->SetPropertyValue_InContainer(ParameterMemory, EditLayerIndex);
    Landscape->ProcessEvent(ImportFunction, ParameterMemory);
    const bool bImported = ReturnProperty->GetPropertyValue_InContainer(ParameterMemory);
    UE_LOG(LogDepthLandscapeTools, Display, TEXT("Visibility import %s: %s, layer %s, extent %d,%d to %d,%d."),
        bImported ? TEXT("accepted") : TEXT("failed"), *Landscape->GetPathName(), *VisibilityName.ToString(), MinX, MinY, MaxX, MaxY);
    return bImported;
#else
    UE_LOG(LogDepthLandscapeTools, Error, TEXT("Visibility import is editor-only."));
    return false;
#endif
}
