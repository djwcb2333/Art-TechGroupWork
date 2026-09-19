#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DepthLandscapeTools.generated.h"

class ALandscape;
class UTextureRenderTarget2D;

/** Editor-only operations restricted to this candidate map's content root. */
UCLASS()
class GDATTEST_API UDepthLandscapeTools : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Red 0 = solid, 1 = hole. No saving; caller owns the outer editor transaction/save. */
    UFUNCTION(BlueprintCallable, Category="Dough World|Editor", meta=(DevelopmentOnly))
    static bool ImportVisibilityMask(ALandscape* Landscape, UTextureRenderTarget2D* RenderTarget, int32 EditLayerIndex = 0);
};
