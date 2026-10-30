#pragma once
#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "AdvancedPreviewScene.h"
#include "UObject/GCObject.h"
class UDWBuildingAuthoringDraft;
class AActor;

class SDWBuildingAuthoringViewport : public SEditorViewport, public FGCObject
{
public:
    SLATE_BEGIN_ARGS(SDWBuildingAuthoringViewport){} SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SDWBuildingAuthoringViewport() override;
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override {return TEXT("DWBuildingAuthoringPreview");}
    void SetDraft(UDWBuildingAuthoringDraft* InDraft);
    void RefreshPreview();
    void FocusModels();
    FString GetDimensionsText() const {return DimensionsText;}
    FBox GetFootprint() const;
protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
private:
    TUniquePtr<FAdvancedPreviewScene> PreviewScene;
    TObjectPtr<UDWBuildingAuthoringDraft> Draft;
    TObjectPtr<AActor> BuildingActor;
    TObjectPtr<AActor> PlayerActor;
    FBox VisibleBounds=FBox(ForceInit);
    FString DimensionsText;
};
