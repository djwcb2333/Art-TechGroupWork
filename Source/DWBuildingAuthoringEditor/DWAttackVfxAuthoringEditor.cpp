#include "DWAttackVfxAuthoringEditor.h"
#include "DWAttackVfxAuthoringViewport.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Misc/MessageDialog.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Commands/GenericCommands.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"

static const FName VfxEditorTab(TEXT("DWAttackVfxAuthoring.Main"));
void FDWAttackVfxAuthoringEditor::Init(TSharedRef<IDWAttackVfxAuthoringBridge> InBridge)
{
    Bridge=InBridge;check(Bridge->GetProfile()&&Bridge->IsVisualOnlyProfile());
    Bridge->GetProfile()->SetFlags(RF_Transactional);
    FDetailsViewArgs Args;Args.bHideSelectionTip=true;Args.NameAreaSettings=FDetailsViewArgs::HideNameArea;
    Details=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(Args);
    Details->SetObject(Bridge->GetProfile());
    Details->OnFinishedChangingProperties().AddRaw(this,&FDWAttackVfxAuthoringEditor::OnDetailsChanged);
    RefreshEntries();
    const auto Layout=FTabManager::NewLayout(TEXT("DWAttackVfxAuthoring_v1"))
        ->AddArea(FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
        ->Split(FTabManager::NewStack()->AddTab(VfxEditorTab,ETabState::OpenedTab)->SetHideTabWell(true)));
    InitAssetEditor(EToolkitMode::Standalone,TSharedPtr<IToolkitHost>(),TEXT("DWAttackVfxAuthoring"),Layout,true,true,Bridge->GetProfile());
    if(GEditor)GEditor->RegisterForUndo(this);
    GetToolkitCommands()->MapAction(FGenericCommands::Get().Undo,FExecuteAction::CreateLambda([](){if(GEditor)GEditor->UndoTransaction();}));
    GetToolkitCommands()->MapAction(FGenericCommands::Get().Redo,FExecuteAction::CreateLambda([](){if(GEditor)GEditor->RedoTransaction();}));
}
FDWAttackVfxAuthoringEditor::~FDWAttackVfxAuthoringEditor()
{
    if(Details)Details->OnFinishedChangingProperties().RemoveAll(this);
    if(GEditor)GEditor->UnregisterForUndo(this);
}
void FDWAttackVfxAuthoringEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{
    FAssetEditorToolkit::RegisterTabSpawners(Manager);
    Manager->RegisterTabSpawner(VfxEditorTab,FOnSpawnTab::CreateSP(this,&FDWAttackVfxAuthoringEditor::SpawnEditorTab))
        .SetDisplayName(FText::FromString(TEXT("攻击特效 / Attack VFX")));
}
void FDWAttackVfxAuthoringEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& Manager)
{Manager->UnregisterTabSpawner(VfxEditorTab);FAssetEditorToolkit::UnregisterTabSpawners(Manager);}
void FDWAttackVfxAuthoringEditor::RefreshEntries()
{
    Options.Reset();for(const auto& Value:Bridge->Entries())Options.Add(MakeShared<FDWAttackVfxEntryOption>(Value));
    if(!Options.ContainsByPredicate([this](const auto& O){return O->Id==Bridge->SelectedEntry();})&&!Options.IsEmpty())Bridge->SelectEntry(Options[0]->Id);
    if(EntryCombo)EntryCombo->RefreshOptions();
}
void FDWAttackVfxAuthoringEditor::OnDetailsChanged(const FPropertyChangedEvent&)
{Bridge->RebindAfterPropertyChangeOrUndo();RefreshEntries();if(Preview)Preview->RefreshAfterEdit();}
void FDWAttackVfxAuthoringEditor::PostUndo(bool Success)
{
    if(!Success||!Bridge)return;
    Bridge->RebindAfterPropertyChangeOrUndo();RefreshEntries();
    if(Details)Details->ForceRefresh();if(Preview)Preview->RefreshAfterEdit();
}
FReply FDWAttackVfxAuthoringEditor::AddEntry()
{
    const FScopedTransaction Transaction(FText::FromString(TEXT("Add Visual VFX Entry")));
    Bridge->GetProfile()->Modify();const FGuid Id=Bridge->AddVisualEntry();
    if(Id.IsValid())
    {Bridge->SelectEntry(Id);Bridge->GetProfile()->PostEditChange();Bridge->GetProfile()->MarkPackageDirty();PostUndo(true);}
    return FReply::Handled();
}
FReply FDWAttackVfxAuthoringEditor::RemoveEntry()
{
    const FScopedTransaction Transaction(FText::FromString(TEXT("Remove Visual VFX Entry")));
    Bridge->GetProfile()->Modify();
    if(Bridge->RemoveVisualEntry(Bridge->SelectedEntry()))
    {Bridge->GetProfile()->PostEditChange();Bridge->GetProfile()->MarkPackageDirty();PostUndo(true);}
    return FReply::Handled();
}
void FDWAttackVfxAuthoringEditor::SaveAsset_Execute()
{
    if(Preview)Preview->EndGizmoDrag();
    FString Error;
    if(!Bridge->ValidateForSave(Error))
    {FMessageDialog::Open(EAppMsgType::Ok,FText::FromString(Error));return;}
    // Toolkit owns this exact runtime profile. No Save All / map / DA timing / Skeleton write.
    FAssetEditorToolkit::SaveAsset_Execute();
}
TSharedRef<SDockTab> FDWAttackVfxAuthoringEditor::SpawnEditorTab(const FSpawnTabArgs&)
{
    SAssignNew(Preview,SDWAttackVfxAuthoringViewport,Bridge.ToSharedRef());
    return SNew(SDockTab)
    [SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight().Padding(6)
      [SNew(STextBlock).Text(FText::FromString(Bridge->GetProfile()->GetPathName()))]
      +SVerticalBox::Slot().AutoHeight().Padding(6)
      [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1)
        [SAssignNew(EntryCombo,SComboBox<TSharedPtr<FDWAttackVfxEntryOption>>).OptionsSource(&Options)
          .OnGenerateWidget_Lambda([](TSharedPtr<FDWAttackVfxEntryOption> O){return SNew(STextBlock).Text(O->Label);})
          .OnSelectionChanged_Lambda([this](TSharedPtr<FDWAttackVfxEntryOption> O,ESelectInfo::Type)
          {if(O){Preview->EndGizmoDrag();Preview->SetPlaying(false);Bridge->SelectEntry(O->Id);Preview->RefreshAfterEdit();Preview->Replay();}})
          [SNew(STextBlock).Text_Lambda([this](){for(const auto& O:Options)if(O->Id==Bridge->SelectedEntry())return O->Label;return FText::FromString(TEXT("没有条目 / No entries"));})]]
        +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("新增 / Add"))).OnClicked_Raw(this,&FDWAttackVfxAuthoringEditor::AddEntry)]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(FText::FromString(TEXT("删除 / Delete"))).OnClicked_Raw(this,&FDWAttackVfxAuthoringEditor::RemoveEntry)]
        +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("修复条目 ID / Repair IDs"))).ToolTipText(FText::FromString(TEXT("为Details手工新增/复制的无效或重复条目生成编辑ID；不修改游戏身份，不激活空System。"))).OnClicked_Lambda([this]()
         {const FScopedTransaction Tx(FText::FromString(TEXT("Repair cosmetic entry IDs")));Bridge->GetProfile()->Modify();if(Bridge->RepairEditorEntryIds()>0){Bridge->GetProfile()->PostEditChange();Bridge->GetProfile()->MarkPackageDirty();PostUndo(true);}return FReply::Handled();})]]
      +SVerticalBox::Slot().FillHeight(1)
      [SNew(SSplitter)
        +SSplitter::Slot().Value(.72f)[Preview.ToSharedRef()]
        +SSplitter::Slot().Value(.28f)[Details.ToSharedRef()]]
      +SVerticalBox::Slot().AutoHeight().Padding(6)
      [SNew(SHorizontalBox)
        +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text_Lambda([this](){return FText::FromString(Preview->IsPlaying()?TEXT("暂停 / Pause"):TEXT("播放 / Play"));}).OnClicked_Lambda([this](){Preview->SetPlaying(!Preview->IsPlaying());return FReply::Handled();})]
        +SHorizontalBox::Slot().AutoWidth().Padding(4,0)[SNew(SButton).Text(FText::FromString(TEXT("重播 / Replay"))).OnClicked_Lambda([this](){Preview->Replay();return FReply::Handled();})]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SCheckBox).IsChecked_Lambda([this](){return Preview->IsLooping()?ECheckBoxState::Checked:ECheckBoxState::Unchecked;}).OnCheckStateChanged_Lambda([this](ECheckBoxState S){Preview->SetLoop(S==ECheckBoxState::Checked);})[SNew(STextBlock).Text(FText::FromString(TEXT("循环 / Loop")))]]
        +SHorizontalBox::Slot().AutoWidth().Padding(6,0)[SNew(SSpinBox<double>).MinValue(.1).MaxValue(4).Value_Lambda([this](){return Preview->GetRate();}).OnValueChanged_Lambda([this](double V){Preview->SetRate(V);})]
        +SHorizontalBox::Slot().FillWidth(1).Padding(6,0)[SNew(SSlider).Value_Lambda([this](){return Preview->Duration()>0?float(Preview->GetTime()/Preview->Duration()):0.f;}).OnValueChanged_Lambda([this](float V){Preview->Seek(V*Preview->Duration());})]
        +SHorizontalBox::Slot().AutoWidth()[SNew(SSpinBox<double>).MinValue(0).MaxValue_Lambda([this](){return Preview->Duration();}).Value_Lambda([this](){return Preview->GetTime();}).OnValueChanged_Lambda([this](double V){Preview->Seek(V);})]]
      +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text_Lambda([this](){return Preview->Status();})]
      +SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return Bridge->SimulationLimitations();})]
    ];
}
