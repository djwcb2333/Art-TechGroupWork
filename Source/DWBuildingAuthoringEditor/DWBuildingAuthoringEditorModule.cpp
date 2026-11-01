#include "Modules/ModuleManager.h"
#include "DWBuildingAuthoringLibrary.h"
#include "DWBuildingAuthoringViewport.h"
#include "DWResourcePlacementTool.h"
#include "DWAttackVfxAuthoringAdapter.h"
#include "DWGameplayConfig.h"
#include "DWBuildingDefinition.h"
#include "Engine/StaticMesh.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "ToolMenus.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"

struct FDWAuthoringItemOption {FName Id;FString Label;};

class SDWBuildingAuthoringPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWBuildingAuthoringPanel){}SLATE_END_ARGS()
    void Construct(const FArguments& Args)
    {
        Draft.Reset(UDWBuildingAuthoringLibrary::CreateDraft(nullptr));
        FDetailsViewArgs DetailArgs;DetailArgs.bAllowSearch=true;DetailArgs.bHideSelectionTip=true;DetailArgs.NameAreaSettings=FDetailsViewArgs::HideNameArea;
        Details=FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor")).CreateDetailView(DetailArgs);
        Details->SetObject(Draft.Get());
        Details->OnFinishedChangingProperties().AddSP(this,&SDWBuildingAuthoringPanel::OnChanged);
        ChildSlot
        [SNew(SVerticalBox)
            +SVerticalBox::Slot().AutoHeight().Padding(10)
            [SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(TEXT("建筑制作工具：自动读取当前游戏物品与合成配方。设置模型、属性和原料后，发布为每种建筑独立 Actor。新建筑固定在 Gameplay/Buildings；现有模型和用户资源不会自动搬动或覆盖。")))]
            +SVerticalBox::Slot().AutoHeight().Padding(8,0)
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("新建草稿"))).OnClicked(this,&SDWBuildingAuthoringPanel::NewDraft)]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("导入 FBX"))).ToolTipText(FText::FromString(TEXT("每个FBX合并为一个静态网格；保留共同场景原点，转换厘米。默认不导入材质/贴图。同名目录已有资产时拒绝覆盖，请使用已有模型。"))).OnClicked(this,&SDWBuildingAuthoringPanel::Import)]
                +SHorizontalBox::Slot().FillWidth(1).Padding(2)[SNew(SObjectPropertyEntryBox).AllowedClass(UStaticMesh::StaticClass()).OnObjectChanged(this,&SDWBuildingAuthoringPanel::AddMesh).ToolTipText(FText::FromString(TEXT("选择已有模型，立即追加一个分件；不会复制原资源。局部位置/旋转/缩放在左侧模型分件中调整。")))]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("刷新尺度预览"))).OnClicked_Lambda([this](){Preview->RefreshPreview();return FReply::Handled();})]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("校验与发布…"))).OnClicked(this,&SDWBuildingAuthoringPanel::Publish)]
            ]
            +SVerticalBox::Slot().AutoHeight().Padding(8,4)
            [SNew(SHorizontalBox)
                +SHorizontalBox::Slot().FillWidth(1).Padding(2)
                [SAssignNew(ItemCombo,SComboBox<TSharedPtr<FDWAuthoringItemOption>>).OptionsSource(&Options)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FDWAuthoringItemOption> O){return SNew(STextBlock).Text(FText::FromString(O?O->Label:TEXT("")));})
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FDWAuthoringItemOption> O,ESelectInfo::Type){Selected=O;})
                    [SNew(STextBlock).Text_Lambda([this](){return FText::FromString(Selected?Selected->Label:TEXT("当前游戏物品：选择建筑或配方原料"));})]]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("设为建筑物品"))).OnClicked(this,&SDWBuildingAuthoringPanel::UseItem)]
                +SHorizontalBox::Slot().AutoWidth().Padding(2)[SNew(SButton).Text(FText::FromString(TEXT("加入配方原料"))).OnClicked(this,&SDWBuildingAuthoringPanel::AddIngredient)]
                +SHorizontalBox::Slot().FillWidth(.65).Padding(2)[SNew(SObjectPropertyEntryBox).AllowedClass(UDWBuildingDefinition::StaticClass()).OnObjectChanged(this,&SDWBuildingAuthoringPanel::LoadDefinition).ToolTipText(FText::FromString(TEXT("选择已发布建筑定义，载入视觉与规则作为新 ID 草稿。默认另存，不覆盖旧 Actor。")))]
            ]
            +SVerticalBox::Slot().AutoHeight().Padding(10,2)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(SourceText);})]
            +SVerticalBox::Slot().FillHeight(1).Padding(8)
            [SNew(SSplitter)
                +SSplitter::Slot().Value(.43f)[Details.ToSharedRef()]
                +SSplitter::Slot().Value(.57f)[SAssignNew(Preview,SDWBuildingAuthoringViewport)]
            ]
            +SVerticalBox::Slot().AutoHeight().Padding(8,0,8,8)
            [SNew(SBox).HeightOverride(130)
                [SAssignNew(Status,SMultiLineEditableTextBox).IsReadOnly(true).AutoWrapText(true).Text(FText::FromString(TEXT("尚未发布。模型原点约定：地面中心 Z=0；绿色框底部 Z=0。工具只发布到正式统一配置 DA_DoughWorldGameplay；独立测试配置不作为发布目标。")))]]
        ];
        Preview->SetDraft(Draft.Get());RefreshItems();
    }
    virtual void Tick(const FGeometry& G,double Time,float Delta) override
    {
        SCompoundWidget::Tick(G,Time,Delta);if(Time-LastRefresh<.75)return;LastRefresh=Time;
        RefreshItems();
    }
    bool CanClose()
    {
        for(const FString& Path:Draft->ImportedAssets)if(UObject* O=FindObject<UObject>(nullptr,*Path);O&&O->GetOutermost()->IsDirty())
            return FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(TEXT("此草稿导入的模型仍未保存。关闭工具只会关闭窗口，资产保留在内容浏览器且保持未保存，不会自动保存或删除。是否关闭？")))==EAppReturnType::Yes;
        return true;
    }
private:
    void SetStatus(const FString& S){if(Status)Status->SetText(FText::FromString(S));}
    void OnChanged(const FPropertyChangedEvent&){if(Preview)Preview->RefreshPreview();}
    void RefreshItems()
    {
        TArray<FDWItemDefinition> Items;TArray<FDWRecipeDefinition> Recipes;FString E;
        bool Ok=UDWBuildingAuthoringLibrary::ReadEffectiveDefinitions(Draft->Config,Items,Recipes,E);
        FString Fingerprint=GetPathNameSafe(Draft->Config)+TEXT("|")+E;
        for(const auto& I:Items)Fingerprint+=TEXT("|")+I.ItemId.ToString()+TEXT(":")+I.DisplayName.ToString();
        SourceText=Ok?FString::Printf(TEXT("当前游戏物品／配方：%d 物品 / %d 配方｜物品 %s｜配方 %s｜每 0.75 秒重新读取内容；发布前再验证。"),Items.Num(),Recipes.Num(),
            Draft->Config->ItemTable?*Draft->Config->ItemTable->GetName():TEXT("配置 Items 数组"),Draft->Config->RecipeTable?*Draft->Config->RecipeTable->GetName():TEXT("配置 Recipes 数组")):E;
        if(Fingerprint==LastFingerprint)return;LastFingerprint=Fingerprint;
        FName SelectedId=Selected?Selected->Id:NAME_None;Selected.Reset();Options.Reset();
        Items.Sort([](const auto& A,const auto& B){return A.ItemId.LexicalLess(B.ItemId);});
        for(const auto& I:Items){auto O=MakeShared<FDWAuthoringItemOption>();O->Id=I.ItemId;O->Label=I.ItemId.ToString()+TEXT(" — ")+I.DisplayName.ToString();Options.Add(O);if(I.ItemId==SelectedId)Selected=O;}
        if(ItemCombo){ItemCombo->RefreshOptions();ItemCombo->SetSelectedItem(Selected);}
    }
    FReply NewDraft()
    {
        if(!CanClose())return FReply::Handled();
        Draft.Reset(UDWBuildingAuthoringLibrary::CreateDraft(nullptr));Details->SetObject(Draft.Get());Preview->SetDraft(Draft.Get());SetStatus(TEXT("已建立新草稿，自动读取当前游戏配置。先前导入资产保留在内容浏览器，不会自动删除。"));return FReply::Handled();
    }
    FReply UseItem()
    {if(Selected&&UDWBuildingAuthoringLibrary::SelectExistingItem(Draft.Get(),Selected->Id)){Details->ForceRefresh();SetStatus(TEXT("已读取现有物品和匹配配方，发布清单会明确显示更新内容。没有现成配方的建筑不会自动编造配方。"));}return FReply::Handled();}
    FReply AddIngredient()
    {
        if(Selected){auto* Existing=Draft->Ingredients.FindByPredicate([this](const auto& I){return I.ItemId==Selected->Id;});if(Existing)++Existing->Quantity;else Draft->Ingredients.Add(FDWItemStack(Selected->Id,1));Draft->bCreateRecipe=true;Details->ForceRefresh();}return FReply::Handled();
    }
    void AddMesh(const FAssetData& A)
    {if(auto* M=Cast<UStaticMesh>(A.GetAsset())){FDWBuildingMeshPart P;P.PartName=M->GetFName();P.Mesh=M;Draft->Parts.Add(P);Details->ForceRefresh();Preview->RefreshPreview();}}
    void LoadDefinition(const FAssetData& A)
    {if(auto* Def=Cast<UDWBuildingDefinition>(A.GetAsset())){if(!CanClose())return;UDWBuildingAuthoringLibrary::LoadDefinitionAsNewDraft(Draft.Get(),Def);Details->ForceRefresh();Preview->RefreshPreview();SetStatus(TEXT("已作为新 ID 草稿载入；模型复用原引用，没有复制。请修改 ID/名称/配方，旧建筑不会覆盖。"));}}
    FReply Import()
    {
        auto* Platform=FDesktopPlatformModule::Get();if(!Platform)return FReply::Handled();TArray<FString> Files;
        if(!Platform->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared()),TEXT("选择建筑 FBX（多文件为多分件）"),FPaths::ProjectDir(),TEXT(""),TEXT("FBX 模型 (*.fbx)|*.fbx"),EFileDialogFlags::Multiple,Files))return FReply::Handled();
        const auto R=UDWBuildingAuthoringLibrary::ImportModels(Draft.Get(),Files,true);SetStatus(R.Message);Details->ForceRefresh();Preview->RefreshPreview();return FReply::Handled();
    }
    FReply Publish()
    {
        FString Report;if(!UDWBuildingAuthoringLibrary::ValidateDraft(Draft.Get(),Report)){SetStatus(Report);return FReply::Handled();}
        SetStatus(Report);if(FMessageDialog::Open(EAppMsgType::YesNo,FText::FromString(Report+TEXT("\n确认创建资产并保存列出的配置？")))!=EAppReturnType::Yes)return FReply::Handled();
        const auto R=UDWBuildingAuthoringLibrary::PublishDraft(Draft.Get(),true);SetStatus(R.Message);RefreshItems();return FReply::Handled();
    }
    TStrongObjectPtr<UDWBuildingAuthoringDraft> Draft;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<SDWBuildingAuthoringViewport> Preview;
    TSharedPtr<SMultiLineEditableTextBox> Status;
    TArray<TSharedPtr<FDWAuthoringItemOption>> Options;
    TSharedPtr<SComboBox<TSharedPtr<FDWAuthoringItemOption>>> ItemCombo;
    TSharedPtr<FDWAuthoringItemOption> Selected;
    FString SourceText,LastFingerprint;
    double LastRefresh=0;
};

class FDWBuildingAuthoringEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        RegisterDWResourcePlacementTool();
        RegisterDWAttackVfxProfileAssetActions();
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TEXT("DWBuildingAuthoring"),FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
        {
            TSharedRef<SDWBuildingAuthoringPanel> Panel=SNew(SDWBuildingAuthoringPanel);
            return SNew(SDockTab).TabRole(ETabRole::NomadTab).OnCanCloseTab(SDockTab::FCanCloseTab::CreateLambda([Panel](){return Panel->CanClose();}))[Panel];
        })).SetDisplayName(FText::FromString(TEXT("建筑制作工具"))).SetTooltipText(FText::FromString(TEXT("制作可进入背包并放置的建筑 Actor、物品和配方")));
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this,&FDWBuildingAuthoringEditorModule::RegisterMenus));
    }
    virtual void ShutdownModule() override
    {
        UnregisterDWAttackVfxProfileAssetActions();
        UnregisterDWResourcePlacementTool();
        UToolMenus::UnRegisterStartupCallback(this);UToolMenus::UnregisterOwner(this);FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TEXT("DWBuildingAuthoring"));
    }
private:
    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);RegisterDWResourcePlacementMenu();RegisterDWAttackVfxProfileCatalogMenu();auto* Menu=UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
        Menu->FindOrAddSection(TEXT("DoughWorldTools")).AddMenuEntry(TEXT("DWBuildingAuthoring"),FText::FromString(TEXT("建筑制作工具")),FText::FromString(TEXT("导入模型、比较玩家尺寸、登记背包建筑与合成配方")),FSlateIcon(),FUIAction(FExecuteAction::CreateLambda([](){UDWBuildingAuthoringLibrary::OpenBuildingAuthoringTool();})));
    }
};
IMPLEMENT_MODULE(FDWBuildingAuthoringEditorModule,DWBuildingAuthoringEditor)
