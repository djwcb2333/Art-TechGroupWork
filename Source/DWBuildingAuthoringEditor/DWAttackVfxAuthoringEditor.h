#pragma once
#include "CoreMinimal.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"
#include "Widgets/Input/SComboBox.h"
#include "Input/Reply.h"
#include "DWAttackVfxAuthoringBridge.h"
class IDetailsView;
class SDWAttackVfxAuthoringViewport;
class SDockTab;
class FSpawnTabArgs;

class FDWAttackVfxAuthoringEditor : public FAssetEditorToolkit, public FEditorUndoClient
{
public:
    void Init(TSharedRef<IDWAttackVfxAuthoringBridge> InBridge);
    virtual ~FDWAttackVfxAuthoringEditor() override;
    virtual void RegisterTabSpawners(const TSharedRef<FTabManager>&) override;
    virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>&) override;
    virtual FName GetToolkitFName() const override{return TEXT("DWAttackVfxAuthoring");}
    virtual FText GetBaseToolkitName() const override{return FText::FromString(TEXT("Attack VFX Authoring"));}
    virtual FString GetWorldCentricTabPrefix() const override{return TEXT("Attack VFX");}
    virtual FLinearColor GetWorldCentricTabColorScale() const override{return FLinearColor(.8f,.35f,.1f);}
    virtual void PostUndo(bool Success) override;
    virtual void PostRedo(bool Success) override{PostUndo(Success);}
    virtual void SaveAsset_Execute() override;
private:
    TSharedPtr<IDWAttackVfxAuthoringBridge> Bridge;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SDWAttackVfxAuthoringViewport> Preview;
    TArray<TSharedPtr<FDWAttackVfxEntryOption>> Options;
    TSharedPtr<SComboBox<TSharedPtr<FDWAttackVfxEntryOption>>> EntryCombo;
    void RefreshEntries();
    void OnDetailsChanged(const FPropertyChangedEvent&);
    FReply AddEntry();
    FReply RemoveEntry();
    TSharedRef<SDockTab> SpawnEditorTab(const FSpawnTabArgs&);
};
