#include "DWUIAuthoringLibrary.h"
#include "DWGameplayWidget.h"
#include "DWUIEntryWidgets.h"
#include "DWProgressRing.h"
#include "DWSettingsPanel.h"
#include "DWPlayerController.h"
#include "DWPlayerCharacter.h"

static FString DWUILastBuildReport;
FString UDWUIAuthoringLibrary::GetLastBuildReport(){return DWUILastBuildReport;}

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Font.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/ScaleBox.h"
#include "Components/WidgetSwitcher.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Components/WrapBox.h"
#include "Components/Spacer.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Sound/SoundClass.h"

namespace
{
    const FLinearColor DWUIAuthoringInkColor(.97f,.92f,.83f,1),DWUIAuthoringMutedColor(.7f,.68f,.63f,1),DWUIAuthoringGoldColor(.88f,.64f,.28f,1),DWUIAuthoringPanelColor(.13f,.10f,.085f,.97f);
    struct FUIDesigner
    {
        UWidgetTree* Tree;
        explicit FUIDesigner(UWidgetBlueprint* BP):Tree(BP->WidgetTree){}
        template<class T>T* Make(const FString& Name,UClass* Class=T::StaticClass())
        {
            T* W=Tree->ConstructWidget<T>(Class,FName(*Name));W->bIsVariable=true;return W;
        }
        UTextBlock* Text(const FString& Name,const FString& Value,int32 Size=18,FLinearColor Color=DWUIAuthoringInkColor)
        {
            auto* W=Make<UTextBlock>(Name);W->SetText(FText::FromString(Value));
            // UMG assets must serialize a UFont reference; CoreStyle's transient CompositeFont does not survive saving.
            FSlateFontInfo Font=W->GetFont();Font.Size=Size;
            if(UFont* FontAsset=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")))Font=FSlateFontInfo(FontAsset,Size,TEXT("Regular"));
            W->SetFont(Font);W->SetColorAndOpacity(FSlateColor(Color));W->SetAutoWrapText(true);return W;
        }
        UButton* Button(const FString& Name,const FString& Label,const FString& TextName=TEXT(""))
        {
            auto* B=Make<UButton>(Name);B->SetBackgroundColor(FLinearColor(.35f,.26f,.18f));
            auto* T=Text(TextName.IsEmpty()?Name+TEXT("_Label"):TextName,Label,18);T->SetJustification(ETextJustify::Center);
            auto* Slot=Cast<UButtonSlot>(B->AddChild(T));if(Slot){Slot->SetPadding(FMargin(20,12));Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Center);}return B;
        }
        UBorder* Card(const FString& Name,UWidget* Child,FLinearColor Color=DWUIAuthoringPanelColor,FMargin Padding=FMargin(20))
        {
            auto* B=Make<UBorder>(Name);B->SetBrushColor(Color);B->SetPadding(Padding);B->SetContent(Child);return B;
        }
        UVerticalBoxSlot* V(UVerticalBox* Parent,UWidget* Child,float Padding=7.f)
        {auto* S=Parent->AddChildToVerticalBox(Child);S->SetPadding(FMargin(0,Padding));return S;}
        UHorizontalBoxSlot* H(UHorizontalBox* Parent,UWidget* Child,float Padding=7.f,bool bFill=false)
        {auto* S=Parent->AddChildToHorizontalBox(Child);S->SetPadding(FMargin(Padding,0));if(bFill)S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));return S;}
        UCanvasPanelSlot* Canvas(UCanvasPanel* Parent,UWidget* Child,FAnchors Anchors,FMargin Offset,FVector2D Alignment=FVector2D::ZeroVector)
        {auto* S=Parent->AddChildToCanvas(Child);S->SetAnchors(Anchors);S->SetOffsets(Offset);S->SetAlignment(Alignment);return S;}
        USizeBox* Size(const FString& Name,UWidget* Child,float Width=0,float Height=0)
        {auto* B=Make<USizeBox>(Name);if(Width>0)B->SetWidthOverride(Width);if(Height>0)B->SetHeightOverride(Height);B->SetContent(Child);return B;}
        void Heading(UVerticalBox* Parent,const FString& Prefix,const FString& Title,const FString& Description)
        {V(Parent,Text(Prefix+TEXT("_Heading"),Title,30,DWUIAuthoringGoldColor));V(Parent,Text(Prefix+TEXT("_Description"),Description,16,DWUIAuthoringMutedColor));}
        UBorder* Page(const FString& Name,UVerticalBox*& OutBody,float Width=620.f)
        {
            OutBody=Make<UVerticalBox>(Name+TEXT("_Body"));auto* Scroll=Make<UScrollBox>(Name+TEXT("_Scroll"));Scroll->AddChild(OutBody);
            auto* Box=Make<USizeBox>(Name+TEXT("_Size"));Box->SetWidthOverride(Width);Box->SetMaxDesiredHeight(740);Box->SetContent(Scroll);
            return Card(Name,Box);
        }
    };

    UWidgetBlueprint* MakeBP(const FString& Name,UClass* Parent,bool bReplace,bool& bNeedsBuild)
    {
        const FString Path=TEXT("/Game/DoughWorld/UI/")+Name;
        UWidgetBlueprint* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Path+TEXT(".")+Name));
        if(BP)
        {
            if(BP->ParentClass!=Parent){DWUILastBuildReport+=TEXT("ERROR incompatible existing parent: ")+Path+TEXT("\n");return nullptr;}
            bNeedsBuild=bReplace;
            if(!bReplace){DWUILastBuildReport+=TEXT("PRESERVED existing Designer asset: ")+Path+TEXT("\n");return BP;}
            BP->Modify();
            BP->WidgetTree=NewObject<UWidgetTree>(BP,MakeUniqueObjectName(BP,UWidgetTree::StaticClass(),TEXT("WidgetTree")),RF_Transactional);
            return BP;
        }
        UPackage* Package=CreatePackage(*Path);
        BP=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(Parent,Package,FName(*Name),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass(),TEXT("DoughWorldUIAuthoring")));
        if(!BP){DWUILastBuildReport+=TEXT("ERROR create Blueprint: ")+Path+TEXT("\n");return nullptr;}
        if(!BP->WidgetTree)BP->WidgetTree=NewObject<UWidgetTree>(BP,TEXT("WidgetTree"),RF_Transactional);
        FAssetRegistryModule::AssetCreated(BP);bNeedsBuild=true;return BP;
    }
    bool SaveBP(UWidgetBlueprint* BP)
    {
        if(!BP)return false;
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
        if(!BP->GeneratedClass||BP->Status==BS_Error){DWUILastBuildReport+=TEXT("ERROR compile: ")+BP->GetPathName()+TEXT("\n");return false;}
        BP->MarkPackageDirty();const FString Filename=FPackageName::LongPackageNameToFilename(BP->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        const bool Saved=UPackage::SavePackage(BP->GetOutermost(),BP,*Filename,Args);
        int32 Count=0;BP->WidgetTree->ForEachWidget([&Count](UWidget*){++Count;});
        DWUILastBuildReport+=FString::Printf(TEXT("%s %s | Designer widgets=%d\n"),Saved?TEXT("SAVED"):TEXT("ERROR save"),*BP->GetPathName(),Count);return Saved;
    }

    void BuildInventorySlot(UWidgetBlueprint* BP)
    {
        FUIDesigner D(BP);auto* Content=D.Make<UVerticalBox>(TEXT("SlotContent"));
        auto* Image=D.Make<UImage>(TEXT("ItemIcon"));Image->SetColorAndOpacity(DWUIAuthoringGoldColor);
        D.V(Content,D.Size(TEXT("ItemIconSize"),Image,32,32),3)->SetHorizontalAlignment(HAlign_Center);
        auto* Name=D.Text(TEXT("ItemNameText"),TEXT("物品名称"),14);Name->SetJustification(ETextJustify::Center);D.V(Content,Name,4);
        auto* Quantity=D.Text(TEXT("QuantityText"),TEXT("40"),15);Quantity->SetJustification(ETextJustify::Right);D.V(Content,Quantity,0);
        auto* Button=D.Make<UButton>(TEXT("SlotButton"));Button->SetBackgroundColor(FLinearColor(.34f,.27f,.21f));auto* BSlot=Cast<UButtonSlot>(Button->AddChild(Content));if(BSlot)BSlot->SetPadding(FMargin(8));
        BP->WidgetTree->RootWidget=D.Size(TEXT("SlotDimensions"),Button,90,104);
    }
    void BuildRecipe(UWidgetBlueprint* BP)
    {
        FUIDesigner D(BP);auto* V=D.Make<UVerticalBox>(TEXT("RecipeContent"));
        D.V(V,D.Text(TEXT("RecipeNameText"),TEXT("配方名称"),24,DWUIAuthoringGoldColor));
        D.V(V,D.Text(TEXT("IngredientsText"),TEXT("需要：材料 × 数量（拥有 0）"),16));
        D.V(V,D.Text(TEXT("OutputsText"),TEXT("获得：物品 × 数量"),16,FLinearColor(.35f,.8f,.55f)));
        D.V(V,D.Text(TEXT("RequirementText"),TEXT("仅酵母形态可制作"),14,DWUIAuthoringMutedColor));
        D.V(V,D.Button(TEXT("CraftButton"),TEXT("制作一份"),TEXT("CraftButtonText")));
        BP->WidgetTree->RootWidget=D.Card(TEXT("RecipeCard"),V);
    }
    void BuildSaveSlot(UWidgetBlueprint* BP)
    {
        FUIDesigner D(BP);auto* Row=D.Make<UHorizontalBox>(TEXT("SaveRow"));auto* Labels=D.Make<UVerticalBox>(TEXT("SaveLabels"));
        D.V(Labels,D.Text(TEXT("SlotNameText"),TEXT("存档槽位"),22),3);D.V(Labels,D.Text(TEXT("SlotDateText"),TEXT("保存时间 / 新冒险"),15,DWUIAuthoringMutedColor),3);D.H(Row,Labels,8,true)->SetVerticalAlignment(VAlign_Center);
        D.H(Row,D.Button(TEXT("LoadButton"),TEXT("加载")),5)->SetVerticalAlignment(VAlign_Center);
        D.H(Row,D.Button(TEXT("NewButton"),TEXT("新建")),5)->SetVerticalAlignment(VAlign_Center);
        D.H(Row,D.Button(TEXT("DeleteButton"),TEXT("删除"),TEXT("DeleteButtonText")),5)->SetVerticalAlignment(VAlign_Center);
        D.H(Row,D.Button(TEXT("CancelDeleteButton"),TEXT("取消")),5)->SetVerticalAlignment(VAlign_Center);
        BP->WidgetTree->RootWidget=D.Card(TEXT("SaveSlotCard"),Row);
    }
    void FillInventoryPreview(FUIDesigner& D,UUniformGridPanel* Grid,UClass* EntryClass,const FString& Prefix)
    {
        Grid->SetSlotPadding(FMargin(4));
        for(int32 I=0;I<24;++I){auto* Entry=D.Make<UDWInventorySlotWidget>(Prefix+FString::FromInt(I),EntryClass);Grid->AddChildToUniformGrid(Entry,I/6,I%6);}
    }
    void AddPage(UWidgetSwitcher* Switcher,UWidget* Child)
    {auto* Slot=Cast<UWidgetSwitcherSlot>(Switcher->AddChild(Child));if(Slot){Slot->SetHorizontalAlignment(HAlign_Center);Slot->SetVerticalAlignment(VAlign_Center);}}

    void BuildMain(UWidgetBlueprint* BP,UClass* SlotClass,UClass* RecipeClass,UClass* SaveClass)
    {
        FUIDesigner D(BP);auto* Root=D.Make<UCanvasPanel>(TEXT("RootCanvas"));Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);BP->WidgetTree->RootWidget=Root;
        auto* Hud=D.Make<UCanvasPanel>(TEXT("HUDLayer"));D.Canvas(Root,Hud,FAnchors(0,0,1,1),FMargin(0));Hud->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Status=D.Make<UVerticalBox>(TEXT("StatusContent"));D.V(Status,D.Text(TEXT("HealthValueText"),TEXT("生命 100 / 100"),20));
        auto* HP=D.Make<UProgressBar>(TEXT("HealthBar"));HP->SetPercent(1);HP->SetFillColorAndOpacity(FLinearColor(.86f,.28f,.22f));D.V(Status,D.Size(TEXT("HealthBarSize"),HP,0,14));
        D.V(Status,D.Text(TEXT("TransformationValueText"),TEXT("变身 0%"),18));auto* TP=D.Make<UProgressBar>(TEXT("TransformationBar"));TP->SetFillColorAndOpacity(FLinearColor(.34f,.78f,.57f));D.V(Status,D.Size(TEXT("TransformationBarSize"),TP,0,10));D.V(Status,D.Text(TEXT("FormText"),TEXT("● 面团形态 · [E] 变身"),17,DWUIAuthoringGoldColor));
        D.Canvas(Hud,D.Card(TEXT("StatusCard"),Status),FAnchors(0,0),FMargin(28,28,330,215));
        auto* Brewing=D.Make<UVerticalBox>(TEXT("BrewingContent"));D.V(Brewing,D.Text(TEXT("BrewingTitle"),TEXT("疾跑酿造"),19,DWUIAuthoringGoldColor))->SetHorizontalAlignment(HAlign_Center);
        auto* RingLayer=D.Make<UOverlay>(TEXT("RingLayer"));RingLayer->AddChildToOverlay(D.Make<UDWProgressRing>(TEXT("SprintRing")));
        auto* RingText=D.Text(TEXT("RingValueText"),TEXT("0%"),24);auto* RingSlot=RingLayer->AddChildToOverlay(RingText);RingSlot->SetHorizontalAlignment(HAlign_Center);RingSlot->SetVerticalAlignment(VAlign_Center);
        D.V(Brewing,D.Size(TEXT("RingDimensions"),RingLayer,92,92))->SetHorizontalAlignment(HAlign_Center);
        D.V(Brewing,D.Text(TEXT("BrewingHint"),TEXT("停止后保留进度"),14,DWUIAuthoringMutedColor))->SetHorizontalAlignment(HAlign_Center);D.V(Brewing,D.Text(TEXT("AlcoholCountText"),TEXT("酒精 × 0"),18))->SetHorizontalAlignment(HAlign_Center);
        D.Canvas(Hud,D.Card(TEXT("BrewingCard"),Brewing),FAnchors(1,0),FMargin(-28,28,185,242),FVector2D(1,0));
        auto* Help=D.Text(TEXT("ControlsHint"),TEXT("WASD 移动   Shift 疾跑   空格 冲刺   右键拖拽 视角   左键 投掷   F 采集   B 背包   Tab 制作   Esc 菜单"),15);Help->SetJustification(ETextJustify::Center);
        D.Canvas(Hud,D.Card(TEXT("ControlsCard"),Help),FAnchors(.5f,1),FMargin(0,-22,1120,60),FVector2D(.5f,1));
        auto* Interaction=D.Text(TEXT("InteractionText"),TEXT("按住 F 采集 · 0%"),20,DWUIAuthoringGoldColor);Interaction->SetJustification(ETextJustify::Center);
        D.Canvas(Hud,D.Card(TEXT("InteractionContainer"),Interaction),FAnchors(.5f,1),FMargin(0,-104,570,60),FVector2D(.5f,1));

        auto* Switch=D.Make<UWidgetSwitcher>(TEXT("PageSwitcher"));Switch->SetActiveWidgetIndex(0);
        auto* Menu=D.Card(TEXT("MenuRoot"),Switch,FLinearColor(.025f,.02f,.02f,.86f),FMargin(30));D.Canvas(Root,Menu,FAnchors(0,0,1,1),FMargin(0));

        UVerticalBox* TitleBody=nullptr;auto* TitlePage=D.Page(TEXT("TitlePage"),TitleBody,620);
        auto* Subtitle=D.Text(TEXT("SubtitleText"),TEXT("DOUGH WORLD"),22,DWUIAuthoringGoldColor);Subtitle->SetAutoWrapText(false);
        D.V(TitleBody,Subtitle,16)->SetHorizontalAlignment(HAlign_Center);
        auto* Title=D.Text(TEXT("TitleText"),TEXT("面团世界"),52);Title->SetAutoWrapText(false);
        D.V(TitleBody,Title,24)->SetHorizontalAlignment(HAlign_Center);
        auto* Tagline=D.Text(TEXT("Tagline"),TEXT("探索 · 收集 · 发酵 · 生存"),18,DWUIAuthoringMutedColor);Tagline->SetAutoWrapText(false);
        D.V(TitleBody,Tagline,14)->SetHorizontalAlignment(HAlign_Center);
        D.V(TitleBody,D.Button(TEXT("StartButton"),TEXT("开始游戏")));D.V(TitleBody,D.Button(TEXT("TitleSettingsButton"),TEXT("设置")));D.V(TitleBody,D.Button(TEXT("QuitButton"),TEXT("退出游戏")));
        auto* TitleOverlay=D.Make<UOverlay>(TEXT("TitlePageOverlay"));auto* TitleBG=D.Make<UImage>(TEXT("TitleBackgroundImage"));TitleBG->SetVisibility(ESlateVisibility::Collapsed);TitleOverlay->AddChildToOverlay(TitleBG);TitleOverlay->AddChildToOverlay(TitlePage);AddPage(Switch,TitleOverlay);

        UVerticalBox* Saves=nullptr;auto* SavesPage=D.Page(TEXT("SaveSlotsPage"),Saves,1060);D.Heading(Saves,TEXT("Slots"),TEXT("选择存档"),TEXT("最多 3 个存档。新建不会覆盖已有存档；删除需要再次确认。"));
        auto* SaveList=D.Make<UVerticalBox>(TEXT("SaveSlotList"));for(int32 I=0;I<3;++I)D.V(SaveList,D.Make<UDWSaveSlotWidget>(TEXT("SaveSlotPreview_")+FString::FromInt(I),SaveClass));D.V(Saves,SaveList);D.V(Saves,D.Button(TEXT("SlotsBackButton"),TEXT("返回标题")));AddPage(Switch,SavesPage);

        UVerticalBox* Inventory=nullptr;auto* InventoryPage=D.Page(TEXT("InventoryCard"),Inventory,660);D.Heading(Inventory,TEXT("Inventory"),TEXT("背包"),TEXT("点击物品查看用途与使用。按 B 返回游戏。"));
        auto* Grid=D.Make<UUniformGridPanel>(TEXT("InventoryGrid"));FillInventoryPreview(D,Grid,SlotClass,TEXT("InventoryPreview_"));D.V(Inventory,Grid);D.V(Inventory,D.Text(TEXT("InventoryCapacityText"),TEXT("每槽上限 40 · 共 24 槽"),15,DWUIAuthoringMutedColor));D.V(Inventory,D.Text(TEXT("InventoryDetailsText"),TEXT("选择物品查看用途"),17));D.V(Inventory,D.Button(TEXT("InventoryUseButton"),TEXT("使用一个")));D.V(Inventory,D.Button(TEXT("InventoryCloseButton"),TEXT("返回游戏")));AddPage(Switch,InventoryPage);

        UVerticalBox* Crafting=nullptr;auto* CraftPage=D.Page(TEXT("CraftingCard"),Crafting,1120);D.Heading(Crafting,TEXT("Crafting"),TEXT("制作台"),TEXT("左侧背包，右侧配方。按 Tab 返回游戏。"));
        auto* CraftRow=D.Make<UHorizontalBox>(TEXT("CraftingColumns"));auto* CraftBag=D.Make<UVerticalBox>(TEXT("CraftingBag"));auto* CGrid=D.Make<UUniformGridPanel>(TEXT("CraftingInventoryGrid"));FillInventoryPreview(D,CGrid,SlotClass,TEXT("CraftingPreview_"));D.V(CraftBag,CGrid);D.V(CraftBag,D.Text(TEXT("CraftingDetailsText"),TEXT("选择物品查看用途"),16));D.V(CraftBag,D.Button(TEXT("CraftingUseButton"),TEXT("使用一个")));D.H(CraftRow,CraftBag,10);
        auto* RecipesColumn=D.Make<UVerticalBox>(TEXT("RecipesColumn"));D.V(RecipesColumn,D.Text(TEXT("CraftingStatusText"),TEXT("酵母形态 · 可以制作"),17,DWUIAuthoringGoldColor));auto* Recipes=D.Make<UVerticalBox>(TEXT("RecipeList"));for(int32 I=0;I<2;++I)D.V(Recipes,D.Make<UDWRecipeEntryWidget>(TEXT("RecipePreview_")+FString::FromInt(I),RecipeClass));D.V(RecipesColumn,Recipes);D.H(CraftRow,RecipesColumn,10,true);D.V(Crafting,CraftRow);D.V(Crafting,D.Button(TEXT("CraftingCloseButton"),TEXT("返回游戏")));AddPage(Switch,CraftPage);

        UVerticalBox* Pause=nullptr;auto* PausePage=D.Page(TEXT("PausePage"),Pause,640);D.Heading(Pause,TEXT("Pause"),TEXT("游戏已暂停"),TEXT("保存进度后可以继续探索或返回标题。"));D.V(Pause,D.Button(TEXT("ContinueButton"),TEXT("继续游戏")));D.V(Pause,D.Button(TEXT("SaveButton"),TEXT("保存游戏")));D.V(Pause,D.Button(TEXT("PauseSettingsButton"),TEXT("设置")));D.V(Pause,D.Button(TEXT("SaveAndTitleButton"),TEXT("保存并返回标题")));D.V(Pause,D.Button(TEXT("NoSaveTitleButton"),TEXT("返回标题（不保存）")));AddPage(Switch,PausePage);

        UVerticalBox* Settings=nullptr;auto* SettingsPage=D.Page(TEXT("SettingsPage"),Settings,820);D.Heading(Settings,TEXT("Settings"),TEXT("设置"),TEXT("音量即时生效，图像设置需点击应用。"));D.V(Settings,D.Text(TEXT("VolumeValueText"),TEXT("主音量 100%"),20));auto* Slider=D.Make<USlider>(TEXT("VolumeSlider"));Slider->SetValue(1);D.V(Settings,Slider,14);
        D.V(Settings,D.Text(TEXT("ResolutionLabel"),TEXT("分辨率"),18));D.V(Settings,D.Make<UComboBoxString>(TEXT("ResolutionCombo")));D.V(Settings,D.Text(TEXT("WindowModeLabel"),TEXT("显示模式"),18));D.V(Settings,D.Make<UComboBoxString>(TEXT("WindowModeCombo")));D.V(Settings,D.Text(TEXT("QualityLabel"),TEXT("画质"),18));D.V(Settings,D.Make<UComboBoxString>(TEXT("QualityCombo")));
        auto* VSync=D.Make<UCheckBox>(TEXT("VSyncCheck"));VSync->SetContent(D.Text(TEXT("VSyncLabel"),TEXT("垂直同步"),18));D.V(Settings,VSync);D.V(Settings,D.Button(TEXT("ApplySettingsButton"),TEXT("应用并保存")));D.V(Settings,D.Button(TEXT("SettingsBackButton"),TEXT("返回")));AddPage(Switch,SettingsPage);

        UVerticalBox* Death=nullptr;auto* DeathPage=D.Page(TEXT("DefeatPage"),Death,660);D.Heading(Death,TEXT("Defeat"),TEXT("面团暂时失去了活力"),TEXT("加载上次保存的进度，或返回标题。"));D.V(Death,D.Button(TEXT("DeathLoadButton"),TEXT("加载当前存档")));D.V(Death,D.Button(TEXT("DeathTitleButton"),TEXT("返回标题")));AddPage(Switch,DeathPage);

        auto* Toast=D.Text(TEXT("ToastText"),TEXT("操作反馈提示"),19);Toast->SetJustification(ETextJustify::Center);D.Canvas(Root,D.Card(TEXT("ToastContainer"),Toast),FAnchors(.5f,1),FMargin(0,-170,670,60),FVector2D(.5f,1));
    }
}
#endif

bool UDWUIAuthoringLibrary::CreatePrototypeUIAssets(bool bReplaceExisting)
{
    DWUILastBuildReport.Reset();
#if WITH_EDITOR
    bool bSlot=false,bRecipe=false,bSave=false,bMain=false;
    auto* Slot=MakeBP(TEXT("WBP_DWInventorySlot"),UDWInventorySlotWidget::StaticClass(),bReplaceExisting,bSlot);
    auto* Recipe=MakeBP(TEXT("WBP_DWRecipeEntry"),UDWRecipeEntryWidget::StaticClass(),bReplaceExisting,bRecipe);
    auto* Save=MakeBP(TEXT("WBP_DWSaveSlot"),UDWSaveSlotWidget::StaticClass(),bReplaceExisting,bSave);
    if(!Slot||!Recipe||!Save)return false;
    if(bSlot){BuildInventorySlot(Slot);if(!SaveBP(Slot))return false;}
    if(bRecipe){BuildRecipe(Recipe);if(!SaveBP(Recipe))return false;}
    if(bSave){BuildSaveSlot(Save);if(!SaveBP(Save))return false;}
    auto* Main=MakeBP(TEXT("WBP_DWGameplay"),UDWGameplayWidget::StaticClass(),bReplaceExisting,bMain);if(!Main)return false;
    if(bMain)
    {
        BuildMain(Main,Slot->GeneratedClass,Recipe->GeneratedClass,Save->GeneratedClass);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Main);FKismetEditorUtilities::CompileBlueprint(Main);
        if(Main->Status==BS_Error||!Main->GeneratedClass)return false;
        auto* CDO=Cast<UDWGameplayWidget>(Main->GeneratedClass->GetDefaultObject());if(!CDO)return false;
        CDO->InventorySlotClass=Slot->GeneratedClass;CDO->RecipeEntryClass=Recipe->GeneratedClass;CDO->SaveSlotClass=Save->GeneratedClass;
        // Set defaults before the final compile; compiler preserves authored CDO defaults.
        if(!SaveBP(Main))return false;
    }
    return true;
#else
    DWUILastBuildReport=TEXT("UI asset authoring is only available in the Unreal Editor.");return false;
#endif
}

bool UDWUIAuthoringLibrary::RepairPrototypeFonts()
{
    DWUILastBuildReport.Reset();
#if WITH_EDITOR
    UFont* FontAsset=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    if(!FontAsset){DWUILastBuildReport=TEXT("ERROR: Engine Roboto UFont asset could not be loaded.");return false;}
    bool bSuccess=true;
    for(const TCHAR* Name:{TEXT("WBP_DWInventorySlot"),TEXT("WBP_DWRecipeEntry"),TEXT("WBP_DWSaveSlot"),TEXT("WBP_DWGameplay")})
    {
        const FString Path=FString(TEXT("/Game/DoughWorld/UI/"))+Name+TEXT(".")+Name;
        auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*Path);
        if(!BP||!BP->WidgetTree){DWUILastBuildReport+=TEXT("ERROR missing Widget Blueprint: ")+Path+TEXT("\n");bSuccess=false;continue;}
        int32 Repaired=0;
        BP->Modify();
        BP->WidgetTree->ForEachWidget([FontAsset,&Repaired](UWidget* Widget)
        {
            if(auto* Text=Cast<UTextBlock>(Widget))
            {
                FSlateFontInfo Font=Text->GetFont();
                if(!Font.FontObject)
                {
                    Text->Modify();Font.FontObject=FontAsset;Font.CompositeFont.Reset();
                    if(Font.TypefaceFontName.IsNone())Font.TypefaceFontName=TEXT("Regular");
                    Text->SetFont(Font);++Repaired;
                }
            }
        });
        DWUILastBuildReport+=FString::Printf(TEXT("FONT REPAIR %s | repaired=%d | layout preserved\n"),*Path,Repaired);
        if(Repaired>0&&!SaveBP(BP))bSuccess=false;
    }
    return bSuccess;
#else
    DWUILastBuildReport=TEXT("Font asset repair is only available in the Unreal Editor.");return false;
#endif
}

bool UDWUIAuthoringLibrary::UpgradeMenuSettingsAssets()
{
 DWUILastBuildReport.Reset();
#if WITH_EDITOR
 bool New=false,OK=true;
 auto* Row=MakeBP(TEXT("WBP_DWKeyBindingRow"),UDWKeyBindingRow::StaticClass(),false,New);
 if(!Row)return false;
 if(New){FUIDesigner D(Row);auto* H=D.Make<UHorizontalBox>(TEXT("BindingRow"));D.H(H,D.Text(TEXT("ActionLabel"),TEXT("Action")),4,true)->SetVerticalAlignment(VAlign_Center);D.H(H,D.Size(TEXT("KeyButtonWidth"),D.Button(TEXT("BindingButton"),TEXT("Key"),TEXT("BindingLabel")),220),4);Row->WidgetTree->RootWidget=H;OK&=SaveBP(Row);}
 auto* PanelBP=MakeBP(TEXT("WBP_DWSettingsPanel"),UDWSettingsPanel::StaticClass(),false,New);
 if(!PanelBP)return false;
 if(New){FUIDesigner D(PanelBP);auto* V=D.Make<UVerticalBox>(TEXT("SettingsContent"));auto* Tabs=D.Make<UHorizontalBox>(TEXT("SettingsTabs"));D.H(Tabs,D.Button(TEXT("AudioTabButton"),TEXT("Audio")),4,true);D.H(Tabs,D.Button(TEXT("ControlsTabButton"),TEXT("Controls")),4,true);D.V(V,Tabs);
 auto* Switch=D.Make<UWidgetSwitcher>(TEXT("OptionsSwitcher"));D.V(V,Switch);
 auto* Audio=D.Make<UVerticalBox>(TEXT("AudioOptions"));for(const TCHAR* N:{TEXT("Music"),TEXT("Voice"),TEXT("Effects")}){D.V(Audio,D.Text(FString(N)+TEXT("Label"),FString(N)+TEXT(" 100%")));auto* Sl=D.Make<USlider>(FString(N)+TEXT("Slider"));Sl->SetValue(1);D.V(Audio,D.Size(FString(N)+TEXT("SliderHeight"),Sl,0,28),10);}Switch->AddChild(Audio);
 auto* Keys=D.Make<UVerticalBox>(TEXT("ControlsOptions"));D.V(Keys,D.Text(TEXT("BindingStatus"),TEXT("Click a key to change it."),16));D.V(Keys,D.Button(TEXT("CancelBindingButton"),TEXT("Cancel rebinding")));
 auto* Scroll=D.Make<UScrollBox>(TEXT("KeyRowsScroll"));Scroll->AddChild(D.Make<UVerticalBox>(TEXT("KeyRows")));D.V(Keys,D.Size(TEXT("KeyRowsHeight"),Scroll,0,280));D.V(Keys,D.Button(TEXT("ResetKeysButton"),TEXT("Restore default keys")));Switch->AddChild(Keys);
 PanelBP->WidgetTree->RootWidget=V;OK&=SaveBP(PanelBP);}
 for(const TCHAR* P:{TEXT("/Game/DoughWorld/UI/WBP_DWGameplay.WBP_DWGameplay"),TEXT("/Game/DoughWorld/UI/Frontend/WBP_DWFrontend.WBP_DWFrontend")})
 {
  auto* BP=LoadObject<UWidgetBlueprint>(nullptr,P);if(!BP||!BP->WidgetTree){OK=false;DWUILastBuildReport+=FString(TEXT("ERROR missing "))+P+TEXT("\n");continue;}BP->Modify();FUIDesigner D(BP);
  if(!BP->WidgetTree->FindWidget(TEXT("ExtendedSettings"))){auto* Body=Cast<UVerticalBox>(BP->WidgetTree->FindWidget(TEXT("SettingsPage_Body")));if(!Body){OK=false;continue;}auto* W=D.Make<UDWSettingsPanel>(TEXT("ExtendedSettings"),PanelBP->GeneratedClass);D.V(Body,W);auto* Slot=Body->GetChildAt(Body->GetChildrenCount()-1);Body->RemoveChild(Slot);Body->InsertChildAt(2,Slot);}
  if(!BP->WidgetTree->FindWidget(TEXT("PauseQuitButton")))if(auto* Body=Cast<UVerticalBox>(BP->WidgetTree->FindWidget(TEXT("PausePage_Body"))))D.V(Body,D.Button(TEXT("PauseQuitButton"),TEXT("Save & quit")));
  if(!BP->WidgetTree->FindWidget(TEXT("HUDPauseButton")))if(auto* HUD=Cast<UCanvasPanel>(BP->WidgetTree->FindWidget(TEXT("HUDLayer")))){auto* B=D.Button(TEXT("HUDPauseButton"),TEXT("Pause [P]"));auto* CS=D.Canvas(HUD,B,FAnchors(.5f,0),FMargin(0,18,160,46),FVector2D(.5f,0));CS->SetZOrder(20);HUD->SetVisibility(ESlateVisibility::SelfHitTestInvisible);}
  OK&=SaveBP(BP);
 }
 auto* Save=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/UI/WBP_DWSaveSlot.WBP_DWSaveSlot"));
 if(Save&&Save->WidgetTree){Save->Modify();FUIDesigner D(Save);
 if(!Save->WidgetTree->FindWidget(TEXT("SaveActionsWrap"))){auto* Card=Cast<UBorder>(Save->WidgetTree->FindWidget(TEXT("SaveSlotCard")));auto* Labels=Save->WidgetTree->FindWidget(TEXT("SaveLabels"));if(Card&&Labels){Labels->RemoveFromParent();auto* V=D.Make<UVerticalBox>(TEXT("SaveSlotBody"));D.V(V,Labels,3);auto* Wrap=D.Make<UWrapBox>(TEXT("SaveActionsWrap"));Wrap->SetInnerSlotPadding(FVector2D(10,8));for(const TCHAR* N:{TEXT("LoadButton"),TEXT("NewButton"),TEXT("DeleteButton"),TEXT("CancelDeleteButton")})if(auto* B=Cast<UButton>(Save->WidgetTree->FindWidget(N))){B->RemoveFromParent();if(auto* T=Cast<UTextBlock>(B->GetContent()))T->SetAutoWrapText(false);auto* Box=D.Make<USizeBox>(FString(N)+TEXT("MinimumWidth"));Box->SetMinDesiredWidth(140);Box->SetContent(B);Wrap->AddChild(Box);}D.V(V,Wrap,6);Card->SetContent(V);}else OK=false;}
 OK&=SaveBP(Save);}else OK=false;
 for(const TCHAR* N:{TEXT("Music"),TEXT("Voice"),TEXT("SFX")}){FString Name=TEXT("SC_DW_")+FString(N),Path=TEXT("/Game/DoughWorld/Audio/Mixing/")+Name;if(!LoadObject<USoundClass>(nullptr,*(Path+TEXT(".")+Name))){auto* Package=CreatePackage(*Path);auto* C=NewObject<USoundClass>(Package,FName(*Name),RF_Public|RF_Standalone);C->Properties.Volume=1;FAssetRegistryModule::AssetCreated(C);C->MarkPackageDirty();FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs A;A.TopLevelFlags=RF_Public|RF_Standalone;OK&=UPackage::SavePackage(Package,C,*File,A);}}
 return OK;
#else
 return false;
#endif
}

bool UDWUIAuthoringLibrary::UpgradeGameplayUXAssets(bool bInspectOnly)
{
    DWUILastBuildReport.Reset();
#if WITH_EDITOR
    auto* Settings=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/UI/WBP_DWSettingsPanel.WBP_DWSettingsPanel"));
    TArray<UWidgetBlueprint*> Screens;
    for(const TCHAR* Path:{TEXT("/Game/DoughWorld/UI/WBP_DWGameplay.WBP_DWGameplay"),TEXT("/Game/DoughWorld/UI/Frontend/WBP_DWFrontend.WBP_DWFrontend")})
        Screens.Add(LoadObject<UWidgetBlueprint>(nullptr,Path));
    if(!Settings||!Settings->WidgetTree)return false;
    auto* Tabs=Cast<UHorizontalBox>(Settings->WidgetTree->FindWidget(TEXT("SettingsTabs")));
    auto* Switch=Cast<UWidgetSwitcher>(Settings->WidgetTree->FindWidget(TEXT("OptionsSwitcher")));
    if(!Tabs||!Switch||Switch->GetChildrenCount()<2)return false;
    UWidget* ExistingGuide=Settings->WidgetTree->FindWidget(TEXT("ControllerOptions"));
    if((!ExistingGuide&&Switch->GetChildrenCount()!=2)||(ExistingGuide&&Switch->GetChildIndex(ExistingGuide)!=2))return false;
    // Validate every exact insertion point before changing any authored package.
    for(auto* BP:Screens)
    {
        if(!BP||!BP->WidgetTree||!Cast<UCanvasPanel>(BP->WidgetTree->RootWidget))return false;
        for(const TCHAR* Body:{TEXT("InventoryCard_Body"),TEXT("CraftingCard_Body")})
            if(!Cast<UVerticalBox>(BP->WidgetTree->FindWidget(Body)))return false;
    }
    TArray<UWidgetBlueprint*> All=Screens;All.Add(Settings);
    for(auto* BP:All)
    {
        DWUILastBuildReport+=BP->GetPathName()+TEXT("\n");
        BP->WidgetTree->ForEachWidget([](UWidget* W)
        {
            DWUILastBuildReport+=FString::Printf(TEXT("  %s %s parent=%s\n"),*W->GetName(),*W->GetClass()->GetName(),W->GetParent()?*W->GetParent()->GetName():TEXT("ROOT"));
        });
    }
    if(bInspectOnly){DWUILastBuildReport+=TEXT("INSPECT ONLY: no mutation\n");return true;}
    Settings->Modify();FUIDesigner D(Settings);
    if(!Settings->WidgetTree->FindWidget(TEXT("ControllerTabButton")))
        D.H(Tabs,D.Button(TEXT("ControllerTabButton"),TEXT("控制器")),4,true);
    if(!Settings->WidgetTree->FindWidget(TEXT("ControllerOptions")))
    {
        if(Switch->GetChildrenCount()!=2)return false;
        auto* Guide=D.Make<UVerticalBox>(TEXT("ControllerOptions"));
        D.V(Guide,D.Text(TEXT("CameraSensitivityLabel"),TEXT("视角拖拽灵敏度 100%"),18));
        auto* Sens=D.Make<USlider>(TEXT("CameraSensitivitySlider"));Sens->SetValue((1.f-.25f)/2.75f);
        D.V(Guide,D.Size(TEXT("CameraSensitivityHeight"),Sens,0,36),8);
        D.V(Guide,D.Button(TEXT("CameraSensitivityResetButton"),TEXT("恢复默认灵敏度")),5);
        auto* Scroll=D.Make<UScrollBox>(TEXT("ControlsGuideScroll"));
        auto* Texts=D.Make<UVerticalBox>(TEXT("ControlsGuideColumn"));Scroll->AddChild(Texts);
        D.V(Texts,D.Text(TEXT("ControlsGuideText"),TEXT("当前键盘与鼠标操作"),16),6);
        D.V(Texts,D.Text(TEXT("InventoryShortcutsText"),TEXT("背包操作说明"),15),10);
        D.V(Guide,D.Size(TEXT("ControlsGuideHeight"),Scroll,0,230),6);
        Switch->AddChild(Guide);
    }
    // Only slider dimensions change. Authored brushes, tints and surrounding layout stay in place.
    for(auto* BP:All)BP->WidgetTree->ForEachWidget([](UWidget* W)
    {
        if(auto* Slider=Cast<USlider>(W))
        {
            FSliderStyle Style=Slider->GetWidgetStyle();Style.BarThickness=8;
            Style.NormalThumbImage.ImageSize=FVector2D(22);Style.HoveredThumbImage.ImageSize=FVector2D(22);Style.DisabledThumbImage.ImageSize=FVector2D(22);
            Slider->SetWidgetStyle(Style);
        }
    });
    Settings->WidgetTree->ForEachWidget([Settings](UWidget* W){if(!Settings->WidgetVariableNameToGuidMap.Contains(W->GetFName()))Settings->OnVariableAdded(W->GetFName());});
    if(!SaveBP(Settings))return false;
    for(auto* BP:Screens)
    {
        BP->Modify();FUIDesigner UI(BP);
        for(const TCHAR* N:{TEXT("ControlsCard"),TEXT("ControlsHint")})if(auto* W=BP->WidgetTree->FindWidget(N))W->SetVisibility(ESlateVisibility::Collapsed);
        for(const TPair<FName,FName>& Entry:{TPair<FName,FName>(TEXT("InventoryControlsFooter"),TEXT("InventoryControlsHelpText")),TPair<FName,FName>(TEXT("CraftingControlsFooter"),TEXT("CraftingControlsHelpText"))})
            if(!BP->WidgetTree->FindWidget(Entry.Key))
            {
                auto* Help=Cast<UTextBlock>(BP->WidgetTree->FindWidget(Entry.Value));
                if(!Help)Help=UI.Text(Entry.Value.ToString(),TEXT("左键选择 / 拖动 · 右键拆1 · Shift+右键拆半\nShift+左键合并同类 · Delete 或 Ctrl+左键请求丢弃\n拖到面板外的游戏区域可请求丢弃，均需确认。"),16);
                else Help->RemoveFromParent(); // Only the new task-owned help text moves out of the scrolling area.
                FSlateFontInfo Font=Help->GetFont();Font.Size=16;Help->SetFont(Font);Help->SetJustification(ETextJustify::Center);
                Help->SetVisibility(ESlateVisibility::HitTestInvisible);
                auto* Footer=UI.Card(Entry.Key.ToString(),Help,DWUIAuthoringPanelColor,FMargin(12,8));Footer->SetVisibility(ESlateVisibility::Collapsed);
                UI.Canvas(CastChecked<UCanvasPanel>(BP->WidgetTree->RootWidget),Footer,FAnchors(.5f,1.f),FMargin(0,-14,1050,96),FVector2D(.5f,1.f))->SetZOrder(100);
            }
        if(!BP->WidgetTree->FindWidget(TEXT("InventoryDiscardModal")))
        {
            auto* Column=UI.Make<UVerticalBox>(TEXT("InventoryDiscardBody"));
            UI.V(Column,UI.Text(TEXT("InventoryDiscardTitle"),TEXT("是否丢弃？"),24),8);
            UI.V(Column,UI.Text(TEXT("InventoryDiscardMessage"),TEXT("确认后永久删除，不会留在地面。"),18),12);
            auto* Buttons=UI.Make<UHorizontalBox>(TEXT("InventoryDiscardActions"));
            UI.H(Buttons,UI.Button(TEXT("InventoryDiscardConfirmButton"),TEXT("确认"),TEXT("InventoryDiscardConfirmLabel")),5,true);
            UI.H(Buttons,UI.Button(TEXT("InventoryDiscardCancelButton"),TEXT("取消"),TEXT("InventoryDiscardCancelLabel")),5,true);UI.V(Column,Buttons,6);
            auto* Modal=UI.Card(TEXT("InventoryDiscardModal"),UI.Size(TEXT("InventoryDiscardWidth"),UI.Card(TEXT("InventoryDiscardCard"),Column),460),FLinearColor(0,0,0,.74f),FMargin(20));
            Modal->SetHorizontalAlignment(HAlign_Center);Modal->SetVerticalAlignment(VAlign_Center);Modal->SetVisibility(ESlateVisibility::Collapsed);
            UI.Canvas(CastChecked<UCanvasPanel>(BP->WidgetTree->RootWidget),Modal,FAnchors(0,0,1,1),FMargin(0))->SetZOrder(10000);
        }
        if(auto* Modal=Cast<UBorder>(BP->WidgetTree->FindWidget(TEXT("InventoryDiscardModal"))))
            if(auto* Slot=Cast<UBorderSlot>(Modal->GetContentSlot())){Slot->SetHorizontalAlignment(HAlign_Center);Slot->SetVerticalAlignment(VAlign_Center);}
        BP->WidgetTree->ForEachWidget([BP](UWidget* W){if(!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))BP->OnVariableAdded(W->GetFName());});
        if(!SaveBP(BP))return false;
    }
    DWUILastBuildReport+=TEXT("Local append complete; existing WidgetTrees retained.\n");return true;
#else
    return false;
#endif
}

#if WITH_EDITOR
namespace
{
const FLinearColor BookInk(.18f,.085f,.04f,1.f),BookCream(.985f,.94f,.83f,1.f);
const FLinearColor BookPaper(.97f,.89f,.72f,1.f),BookEdge(.54f,.31f,.14f,1.f),BookAccent(.85f,.55f,.25f,1.f);

void BookButton(UButton* Button,bool Primary=false)
{
    if(!Button)return;
    FButtonStyle Style=Button->GetStyle();
    Style.Normal=FSlateRoundedBoxBrush(Primary?BookAccent:BookPaper,12.f,BookEdge,1.5f);
    Style.Hovered=FSlateRoundedBoxBrush(FLinearColor(1.f,.83f,.55f,1.f),12.f,BookEdge,2.f);
    Style.Pressed=FSlateRoundedBoxBrush(FLinearColor(.78f,.47f,.22f,1.f),12.f,BookEdge,2.f);
    Style.NormalForeground=Style.HoveredForeground=Style.PressedForeground=FSlateColor(BookInk);
    Button->SetStyle(Style);Button->SetBackgroundColor(FLinearColor::White);
}

void BookWidgetStyle(UWidget* Widget)
{
    if(!Widget)return;
    if(auto* Text=Cast<UTextBlock>(Widget))Text->SetColorAndOpacity(FSlateColor(BookInk));
    if(auto* Button=Cast<UButton>(Widget))BookButton(Button,Button->GetFName()==TEXT("ApplySettingsButton"));
    if(auto* Slider=Cast<USlider>(Widget))
    {
        FSliderStyle Style=Slider->GetWidgetStyle();Style.BarThickness=8.f;
        Style.NormalBarImage=FSlateRoundedBoxBrush(FLinearColor(.84f,.72f,.53f,1.f),4.f);
        Style.HoveredBarImage=FSlateRoundedBoxBrush(FLinearColor(.76f,.59f,.35f,1.f),4.f);
        Style.DisabledBarImage=FSlateRoundedBoxBrush(FLinearColor(.88f,.82f,.72f,1.f),4.f);
        Style.NormalThumbImage=FSlateRoundedBoxBrush(BookAccent,11.f,BookEdge,1.f,FVector2f(22.f));
        Style.HoveredThumbImage=FSlateRoundedBoxBrush(FLinearColor(.95f,.68f,.33f,1.f),11.f,BookEdge,1.5f,FVector2f(22.f));
        Style.DisabledThumbImage=FSlateRoundedBoxBrush(FLinearColor(.79f,.73f,.62f,1.f),11.f,BookEdge,1.f,FVector2f(22.f));
        Slider->SetWidgetStyle(Style);Slider->SetSliderBarColor(FLinearColor::White);Slider->SetSliderHandleColor(FLinearColor::White);
    }
    if(auto* Combo=Cast<UComboBoxString>(Widget))
    {
        FComboBoxStyle Style=Combo->GetWidgetStyle();auto& Button=Style.ComboButtonStyle.ButtonStyle;
        Button.Normal=FSlateRoundedBoxBrush(BookCream,10.f,BookEdge,1.f);
        Button.Hovered=FSlateRoundedBoxBrush(FLinearColor(1.f,.96f,.87f,1.f),10.f,BookEdge,2.f);
        Button.Pressed=FSlateRoundedBoxBrush(BookPaper,10.f,BookEdge,2.f);
        Button.NormalForeground=Button.HoveredForeground=Button.PressedForeground=FSlateColor(BookInk);
        Style.ComboButtonStyle.DownArrowImage.TintColor=FSlateColor(BookInk);
        Style.ComboButtonStyle.MenuBorderBrush=FSlateRoundedBoxBrush(BookCream,10.f,BookEdge,1.f);
        Style.ContentPadding=FMargin(14,9);Combo->SetWidgetStyle(Style);
        FTableRowStyle Items=Combo->GetItemStyle();Items.TextColor=Items.SelectedTextColor=FSlateColor(BookInk);
        Items.EvenRowBackgroundBrush=FSlateRoundedBoxBrush(BookCream,0.f);
        Items.OddRowBackgroundBrush=Items.EvenRowBackgroundBrush;
        Items.ActiveBrush=Items.ActiveHoveredBrush=FSlateRoundedBoxBrush(BookAccent,4.f);
        Items.InactiveBrush=Items.InactiveHoveredBrush=FSlateRoundedBoxBrush(BookPaper,4.f);
        Items.EvenRowBackgroundHoveredBrush=Items.OddRowBackgroundHoveredBrush=FSlateRoundedBoxBrush(BookPaper,4.f);
        Combo->SetItemStyle(Items);
    }
    if(auto* Scroll=Cast<UScrollBox>(Widget))
    {
        FScrollBoxStyle Style=Scroll->GetWidgetStyle();
        Style.TopShadowBrush=Style.BottomShadowBrush=FSlateNoResource();Scroll->SetWidgetStyle(Style);
        FScrollBarStyle Bar=Scroll->GetWidgetBarStyle();
        Bar.NormalThumbImage=FSlateRoundedBoxBrush(BookEdge,4.f);
        Bar.HoveredThumbImage=Bar.DraggedThumbImage=FSlateRoundedBoxBrush(BookAccent,4.f);
        Scroll->SetWidgetBarStyle(Bar);
    }
    if(auto* ChildPanel=Cast<UPanelWidget>(Widget))for(UWidget* Child:ChildPanel->GetAllChildren())BookWidgetStyle(Child);
}

struct FBookScreen
{
    UWidgetBlueprint* BP=nullptr;
    UBorder* Page=nullptr;
    USizeBox* Size=nullptr;
    UScrollBox* Scroll=nullptr;
    UVerticalBox* Body=nullptr;
    bool Existing=false;
};

bool InspectBookScreen(UWidgetBlueprint* BP,FBookScreen& Out)
{
    auto Fail=[BP](const FString& Why){DWUILastBuildReport+=TEXT("ERROR ")+GetPathNameSafe(BP)+TEXT(": ")+Why+TEXT("\n");return false;};
    if(!BP||!BP->WidgetTree||!BP->ParentClass||!BP->ParentClass->IsChildOf(UDWGameplayWidget::StaticClass()))return Fail(TEXT("Missing compatible existing screen/tree"));
    UWidgetTree* Tree=BP->WidgetTree;Out.BP=BP;
    Out.Page=Cast<UBorder>(Tree->FindWidget(TEXT("SettingsPage")));
    Out.Size=Cast<USizeBox>(Tree->FindWidget(TEXT("SettingsPage_Size")));
    Out.Scroll=Cast<UScrollBox>(Tree->FindWidget(TEXT("SettingsPage_Scroll")));
    Out.Body=Cast<UVerticalBox>(Tree->FindWidget(TEXT("SettingsPage_Body")));
    auto* Switch=Cast<UWidgetSwitcher>(Tree->FindWidget(TEXT("PageSwitcher")));
    if(!Out.Page||!Out.Size||!Out.Scroll||!Out.Body||!Switch||Switch->GetChildIndex(Out.Page)!=5)return Fail(TEXT("Original settings page is not intact at PageSwitcher index 5"));
    for(const TCHAR* Name:{TEXT("Settings_Heading"),TEXT("Settings_Description"),TEXT("VolumeValueText"),TEXT("VolumeSlider"),
        TEXT("ResolutionLabel"),TEXT("ResolutionCombo"),TEXT("WindowModeLabel"),TEXT("WindowModeCombo"),
        TEXT("QualityLabel"),TEXT("QualityCombo"),TEXT("VSyncCheck"),TEXT("LanguageLabel"),TEXT("LanguageCombo"),
        TEXT("ExtendedSettings"),TEXT("SettingsBackButton"),TEXT("ApplySettingsButton")})
        if(!Tree->FindWidget(Name))return Fail(FString(TEXT("Missing original control "))+Name);
    if(!Cast<UCanvasPanel>(Tree->RootWidget))return Fail(TEXT("Original root is not a CanvasPanel"));
    auto* Modal=Cast<UBorder>(Tree->FindWidget(TEXT("InventoryDiscardModal")));
    if(!Modal||Modal->GetParent()!=Tree->RootWidget||!Cast<UCanvasPanelSlot>(Modal->Slot))return Fail(TEXT("Existing discard popup must be a root Canvas child"));
    for(const TCHAR* Name:{TEXT("InventoryDiscardWidth"),TEXT("InventoryDiscardCard"),TEXT("InventoryDiscardTitle"),
        TEXT("InventoryDiscardMessage"),TEXT("InventoryDiscardActions"),TEXT("InventoryDiscardConfirmButton"),TEXT("InventoryDiscardCancelButton")})
        if(!Tree->FindWidget(Name))return Fail(FString(TEXT("Missing existing discard widget "))+Name);
    auto* Pages=Cast<UWidgetSwitcher>(Tree->FindWidget(TEXT("SettingsBookPages")));
    Out.Existing=Tree->FindWidget(TEXT("SettingsBookRoot"))!=nullptr;
    if(Out.Existing)
    {
        if(!Pages||Pages->GetChildrenCount()!=3||Out.Scroll->GetChildrenCount()!=1||Out.Scroll->GetChildAt(0)!=Pages)return Fail(TEXT("Existing book content topology is incomplete"));
        for(const TCHAR* Name:{TEXT("SettingsBookScale"),TEXT("SettingsBookCover"),TEXT("SettingsBookPaper"),TEXT("SettingsBookContentPaper"),TEXT("SettingsBookTabs"),
            TEXT("SettingsBookActions"),TEXT("SettingsBookGraphicsPage"),TEXT("SettingsBookSharedOptionsPage"),TEXT("SettingsBookGeneralPage"),
            TEXT("SettingsBookMasterVolume"),TEXT("SettingsBookGraphicsTabButton"),TEXT("SettingsBookAudioTabButton"),
            TEXT("SettingsBookKeysTabButton"),TEXT("SettingsBookControllerTabButton"),TEXT("SettingsBookGeneralTabButton")})
            if(!Tree->FindWidget(Name))return Fail(FString(TEXT("Incomplete previous book append: "))+Name);
        if(Pages->GetChildAt(0)!=Tree->FindWidget(TEXT("SettingsBookGraphicsPage"))
            ||Pages->GetChildAt(1)!=Tree->FindWidget(TEXT("SettingsBookSharedOptionsPage"))
            ||Pages->GetChildAt(2)!=Tree->FindWidget(TEXT("SettingsBookGeneralPage")))return Fail(TEXT("Existing book page indices changed"));
        if(!Cast<UScaleBox>(Tree->FindWidget(TEXT("SettingsBookScale")))
            ||!Cast<UVerticalBox>(Tree->FindWidget(TEXT("SettingsBookRoot")))
            ||!Cast<UBorder>(Tree->FindWidget(TEXT("SettingsBookCover")))
            ||!Cast<UBorder>(Tree->FindWidget(TEXT("SettingsBookPaper")))
            ||!Cast<UBorder>(Tree->FindWidget(TEXT("SettingsBookContentPaper"))))return Fail(TEXT("Existing book container classes changed"));
    }
    else
    {
        if(Out.Size->GetContent()!=Out.Scroll||Out.Scroll->GetChildrenCount()!=1||Out.Scroll->GetChildAt(0)!=Out.Body)return Fail(TEXT("Unexpected original settings container topology"));
        bool Collision=false;Tree->ForEachWidget([&](UWidget* W){Collision|=W->GetName().StartsWith(TEXT("SettingsBook"));});
        if(Collision)return Fail(TEXT("Partial SettingsBook names already exist; refusing an ambiguous append"));
        const int32 ExpectedChildren=16;
        TSet<FName> Expected={TEXT("Settings_Heading"),TEXT("Settings_Description"),TEXT("VolumeValueText"),TEXT("VolumeSlider"),
            TEXT("ResolutionLabel"),TEXT("ResolutionCombo"),TEXT("WindowModeLabel"),TEXT("WindowModeCombo"),TEXT("QualityLabel"),
            TEXT("QualityCombo"),TEXT("VSyncCheck"),TEXT("LanguageLabel"),TEXT("LanguageCombo"),TEXT("ExtendedSettings"),
            TEXT("SettingsBackButton"),TEXT("ApplySettingsButton")};
        for(const FName Name:Expected)if(Tree->FindWidget(Name)->GetParent()!=Out.Body)return Fail(TEXT("Original setting control was already reparented: ")+Name.ToString());
        if(Expected.Num()!=ExpectedChildren)return Fail(TEXT("Internal preflight schema mismatch"));
        // Unknown user children remain in the original Graphics body; they are never removed.
    }
    return true;
}

void StyleDiscardPopup(UWidgetBlueprint* BP)
{
    UWidgetTree* Tree=BP->WidgetTree;
    auto* Modal=CastChecked<UBorder>(Tree->FindWidget(TEXT("InventoryDiscardModal")));
    Modal->SetBrush(FSlateRoundedBoxBrush(BookCream,12.f,BookEdge,1.5f));Modal->SetBrushColor(FLinearColor::White);
    Modal->SetPadding(FMargin(16));Modal->SetHorizontalAlignment(HAlign_Fill);Modal->SetVerticalAlignment(VAlign_Fill);
    Modal->SetVisibility(ESlateVisibility::Collapsed);
    auto* CanvasSlot=CastChecked<UCanvasPanelSlot>(Modal->Slot);
    CanvasSlot->SetAnchors(FAnchors(0));CanvasSlot->SetAlignment(FVector2D::ZeroVector);
    CanvasSlot->SetAutoSize(false);CanvasSlot->SetOffsets(FMargin(0,0,260,128));CanvasSlot->SetZOrder(10000);
    if(auto* Width=Cast<USizeBox>(Tree->FindWidget(TEXT("InventoryDiscardWidth"))))
    {Width->ClearWidthOverride();Width->ClearHeightOverride();Width->ClearMinDesiredWidth();Width->ClearMaxDesiredWidth();Width->ClearMinDesiredHeight();Width->ClearMaxDesiredHeight();}
    if(auto* Card=Cast<UBorder>(Tree->FindWidget(TEXT("InventoryDiscardCard"))))
    {Card->SetBrush(FSlateNoResource());Card->SetBrushColor(FLinearColor::White);Card->SetPadding(FMargin(0));Card->SetHorizontalAlignment(HAlign_Fill);Card->SetVerticalAlignment(VAlign_Fill);}
    if(auto* Title=Cast<UTextBlock>(Tree->FindWidget(TEXT("InventoryDiscardTitle"))))
    {
        Title->SetText(FText::FromString(TEXT("是否丢弃？")));Title->SetColorAndOpacity(FSlateColor(BookInk));
        FSlateFontInfo Font=Title->GetFont();Font.Size=22;Title->SetFont(Font);Title->SetJustification(ETextJustify::Center);Title->SetAutoWrapText(false);
        if(auto* Slot=Cast<UVerticalBoxSlot>(Title->Slot))Slot->SetPadding(FMargin(0,0,0,10));
    }
    if(auto* Message=Cast<UTextBlock>(Tree->FindWidget(TEXT("InventoryDiscardMessage")))){Message->SetText(FText::GetEmpty());Message->SetVisibility(ESlateVisibility::Collapsed);}
    if(auto* Actions=Tree->FindWidget(TEXT("InventoryDiscardActions")))if(auto* Slot=Cast<UVerticalBoxSlot>(Actions->Slot))Slot->SetPadding(FMargin(0));
    for(const TCHAR* Name:{TEXT("InventoryDiscardConfirmButton"),TEXT("InventoryDiscardCancelButton")})
        if(auto* Button=Cast<UButton>(Tree->FindWidget(Name)))
        {
            const bool Confirm=Button->GetFName()==TEXT("InventoryDiscardConfirmButton");BookButton(Button,Confirm);
            if(auto* Label=Cast<UTextBlock>(Button->GetContent()))
            {Label->SetText(FText::FromString(Confirm?TEXT("是"):TEXT("否")));Label->SetColorAndOpacity(FSlateColor(BookInk));FSlateFontInfo Font=Label->GetFont();Font.Size=18;Label->SetFont(Font);Label->SetAutoWrapText(false);Label->SetJustification(ETextJustify::Center);}
            if(auto* Slot=Cast<UButtonSlot>(Button->GetContentSlot())){Slot->SetPadding(FMargin(8,5));Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Center);}
        }
    if(auto* Slot=Cast<UBorderSlot>(Modal->GetContentSlot())){Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);}
}

void AppendSettingsBook(FBookScreen& Screen)
{
    UWidgetTree* Tree=Screen.BP->WidgetTree;FUIDesigner D(Screen.BP);
    // FindWidget walks the connected root, so retain original controls before any reparent.
    // New widgets below are kept in local references until their complete subtree is attached.
    TMap<FName,UWidget*> OriginalWidgets;
    Tree->ForEachWidget([&OriginalWidgets](UWidget* Widget){OriginalWidgets.Add(Widget->GetFName(),Widget);});
    auto Original=[&OriginalWidgets](const TCHAR* Name)->UWidget*{return OriginalWidgets.FindChecked(FName(Name));};
    if(!Screen.Existing)
    {
        auto* Root=D.Make<UVerticalBox>(TEXT("SettingsBookRoot"));
        auto* Cover=D.Card(TEXT("SettingsBookCover"),nullptr,BookEdge,FMargin(7,7,7,11));
        auto* Paper=D.Card(TEXT("SettingsBookPaper"),Root,BookCream,FMargin(30,24));Cover->SetContent(Paper);
        auto* Scale=D.Make<UScaleBox>(TEXT("SettingsBookScale"));Scale->SetStretch(EStretch::ScaleToFit);Scale->SetStretchDirection(EStretchDirection::DownOnly);
        Screen.Body->RemoveFromParent();Screen.Scroll->RemoveFromParent();Screen.Size->RemoveFromParent();
        Screen.Page->SetContent(Scale);Scale->SetContent(Screen.Size);Screen.Size->SetContent(Cover);
        for(const TCHAR* Name:{TEXT("Settings_Heading"),TEXT("Settings_Description")})
        {UWidget* Widget=Original(Name);Widget->RemoveFromParent();D.V(Root,Widget,2);}
        auto* Tabs=D.Make<UHorizontalBox>(TEXT("SettingsBookTabs"));
        const TCHAR* Names[]={TEXT("Graphics"),TEXT("Audio"),TEXT("Keys"),TEXT("Controller"),TEXT("General")};
        const TCHAR* Labels[]={TEXT("图像"),TEXT("声音"),TEXT("按键"),TEXT("控制器"),TEXT("常规")};
        for(int32 I=0;I<5;++I)
        {
            const FString Name=TEXT("SettingsBook")+FString(Names[I])+TEXT("TabButton");
            auto* Button=D.Make<UButton>(Name);auto* Column=D.Make<UVerticalBox>(Name+TEXT("_Body"));
            auto* Label=D.Text(Name+TEXT("_Label"),Labels[I],21,BookInk);Label->SetAutoWrapText(false);Label->SetJustification(ETextJustify::Center);
            auto* TextSlot=D.V(Column,Label,0);TextSlot->SetVerticalAlignment(VAlign_Center);TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
            auto* Line=D.Card(Name+TEXT("_Underline"),nullptr,BookInk,FMargin(0));
            Line->SetBrush(FSlateRoundedBoxBrush(BookInk,2.f));Line->SetBrushColor(FLinearColor::White);
            Line->SetVisibility(I==0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
            D.V(Column,D.Size(Name+TEXT("_UnderlineHeight"),Line,0,4),0)->SetPadding(FMargin(20,3,20,0));
            Button->SetContent(Column);if(auto* Slot=Cast<UButtonSlot>(Button->GetContentSlot()))Slot->SetPadding(FMargin(12,10,12,7));
            D.H(Tabs,D.Size(Name+TEXT("_Height"),Button,0,64),4,true);
        }
        D.V(Root,Tabs,0)->SetPadding(FMargin(0,14,0,12));
        auto* Pages=D.Make<UWidgetSwitcher>(TEXT("SettingsBookPages"));Pages->SetActiveWidgetIndex(0);
        auto* Graphic=D.Card(TEXT("SettingsBookGraphicsPage"),Screen.Body,FLinearColor::Transparent,FMargin(0));
        Graphic->SetBrush(FSlateNoResource());Pages->AddChild(Graphic);
        auto* Shared=D.Make<UVerticalBox>(TEXT("SettingsBookSharedOptionsPage"));Pages->AddChild(Shared);
        auto* General=D.Make<UVerticalBox>(TEXT("SettingsBookGeneralPage"));Pages->AddChild(General);
        auto* Master=D.Make<UVerticalBox>(TEXT("SettingsBookMasterVolume"));
        for(const TCHAR* Name:{TEXT("VolumeValueText"),TEXT("VolumeSlider")})
        {UWidget* Widget=Original(Name);Widget->RemoveFromParent();auto* Slot=D.V(Master,Widget,8);if(FString(Name)==TEXT("VolumeSlider"))Slot->SetPadding(FMargin(0,10,0,16));}
        D.V(Shared,Master,0);Master->SetVisibility(ESlateVisibility::Collapsed);
        UWidget* Extended=Original(TEXT("ExtendedSettings"));Extended->RemoveFromParent();D.V(Shared,Extended,0);
        auto Pair=[&](UVerticalBox* Parent,const TCHAR* RowName,const TCHAR* LabelName,const TCHAR* ComboName)
        {
            UWidget* Label=Original(LabelName);UWidget* Combo=Original(ComboName);
            const int32 Index=Parent->GetChildIndex(Label);Label->RemoveFromParent();Combo->RemoveFromParent();
            auto* Row=D.Make<UHorizontalBox>(RowName);D.H(Row,D.Size(FString(RowName)+TEXT("_LabelWidth"),Label,310),0)->SetVerticalAlignment(VAlign_Center);
            D.H(Row,Combo,0,true)->SetVerticalAlignment(VAlign_Center);
            auto* Height=D.Size(FString(RowName)+TEXT("_Height"),Row,0,70);
            auto* Slot=Index!=INDEX_NONE?Cast<UVerticalBoxSlot>(Parent->InsertChildAt(Index,Height)):Parent->AddChildToVerticalBox(Height);
            if(Slot)Slot->SetPadding(FMargin(0,6));
        };
        Pair(Screen.Body,TEXT("SettingsBookResolutionRow"),TEXT("ResolutionLabel"),TEXT("ResolutionCombo"));
        Pair(Screen.Body,TEXT("SettingsBookWindowModeRow"),TEXT("WindowModeLabel"),TEXT("WindowModeCombo"));
        Pair(Screen.Body,TEXT("SettingsBookQualityRow"),TEXT("QualityLabel"),TEXT("QualityCombo"));
        UWidget* LanguageLabel=Original(TEXT("LanguageLabel"));UWidget* LanguageCombo=Original(TEXT("LanguageCombo"));
        LanguageLabel->RemoveFromParent();LanguageCombo->RemoveFromParent();D.V(General,LanguageLabel,0);D.V(General,LanguageCombo,0);
        Pair(General,TEXT("SettingsBookLanguageRow"),TEXT("LanguageLabel"),TEXT("LanguageCombo"));
        Screen.Scroll->AddChild(Pages);
        if(auto* Slot=Cast<UScrollBoxSlot>(Pages->Slot))Slot->SetPadding(FMargin(0,4,12,4));
        auto* Content=D.Card(TEXT("SettingsBookContentPaper"),Screen.Scroll,BookCream,FMargin(24,16));
        D.V(Root,Content,0)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Actions=D.Make<UHorizontalBox>(TEXT("SettingsBookActions"));
        UWidget* Back=Original(TEXT("SettingsBackButton"));Back->RemoveFromParent();
        UWidget* Apply=Original(TEXT("ApplySettingsButton"));Apply->RemoveFromParent();
        D.H(Actions,D.Size(TEXT("SettingsBookBackWidth"),Back,220,56),0);
        D.H(Actions,D.Make<USpacer>(TEXT("SettingsBookActionSpacer")),0,true);
        D.H(Actions,D.Size(TEXT("SettingsBookApplyWidth"),Apply,280,56),0);
        D.V(Root,Actions,0)->SetPadding(FMargin(0,16,0,0));
    }
    Screen.Size->SetWidthOverride(1240);Screen.Size->SetHeightOverride(780);Screen.Size->SetMaxDesiredHeight(780);
    Screen.Page->SetBrush(FSlateNoResource());Screen.Page->SetBrushColor(FLinearColor::White);Screen.Page->SetPadding(FMargin(0));
    Screen.Page->SetHorizontalAlignment(HAlign_Fill);Screen.Page->SetVerticalAlignment(VAlign_Fill);
    if(auto* Slot=Cast<UWidgetSwitcherSlot>(Screen.Page->Slot)){Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);}
    auto* Cover=CastChecked<UBorder>(Tree->FindWidget(TEXT("SettingsBookCover")));
    auto* Paper=CastChecked<UBorder>(Tree->FindWidget(TEXT("SettingsBookPaper")));
    auto* Content=CastChecked<UBorder>(Tree->FindWidget(TEXT("SettingsBookContentPaper")));
    Cover->SetBrush(FSlateRoundedBoxBrush(BookEdge,24.f,FLinearColor(.30f,.14f,.05f,1.f),2.f));Cover->SetBrushColor(FLinearColor::White);
    Paper->SetBrush(FSlateRoundedBoxBrush(BookCream,18.f,FLinearColor(.84f,.70f,.49f,1.f),1.f));Paper->SetBrushColor(FLinearColor::White);
    Content->SetBrush(FSlateRoundedBoxBrush(FLinearColor(1.f,.97f,.89f,1.f),12.f,FLinearColor(.86f,.74f,.54f,1.f),1.f));Content->SetBrushColor(FLinearColor::White);
    BookWidgetStyle(Paper);
    for(const TCHAR* Name:{TEXT("Graphics"),TEXT("Audio"),TEXT("Keys"),TEXT("Controller"),TEXT("General")})
        if(auto* Button=Cast<UButton>(Tree->FindWidget(FName(*(TEXT("SettingsBook")+FString(Name)+TEXT("TabButton"))))))
        {
            FButtonStyle Style=Button->GetStyle();Style.Normal=FSlateRoundedBoxBrush(FString(Name)==TEXT("Graphics")?BookAccent:BookPaper,FVector4(16,16,5,5),BookEdge,1.5f);
            Button->SetStyle(Style);
        }
    StyleDiscardPopup(Screen.BP);
    Tree->ForEachWidget([BP=Screen.BP](UWidget* Widget){if(!BP->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))BP->OnVariableAdded(Widget->GetFName());});
}

struct FBookScrollContent
{
    UVerticalBox* Parent=nullptr;
    USizeBox* Height=nullptr;
    UScrollBox* LegacyScroll=nullptr;
    UVerticalBox* Content=nullptr;
    bool bNeedsReparent=false;
};

bool CacheBookScrollContent(UWidgetTree* Tree,const TCHAR* ParentName,const TCHAR* HeightName,
    const TCHAR* ScrollName,const TCHAR* ContentName,FBookScrollContent& Out)
{
    // Both original and already-flattened layouts are valid. Cache everything while connected.
    Out.Parent=Cast<UVerticalBox>(Tree->FindWidget(ParentName));
    Out.Height=Cast<USizeBox>(Tree->FindWidget(HeightName));
    Out.LegacyScroll=Cast<UScrollBox>(Tree->FindWidget(ScrollName));
    Out.Content=Cast<UVerticalBox>(Tree->FindWidget(ContentName));
    if(!Out.Parent||!Out.Height||!Out.LegacyScroll||!Out.Content||Out.Height->GetParent()!=Out.Parent)
    {DWUILastBuildReport+=FString(TEXT("ERROR single-scroll containers missing or changed: "))+ScrollName+TEXT("\n");return false;}
    const bool Original=Out.Height->GetContent()==Out.LegacyScroll
        &&Out.LegacyScroll->GetChildrenCount()==1&&Out.LegacyScroll->GetChildAt(0)==Out.Content;
    const bool Flattened=Out.Height->GetContent()==Out.Content&&Out.LegacyScroll->GetParent()==Out.Parent
        &&Out.LegacyScroll->GetChildrenCount()==0;
    if(!Original&&!Flattened)
    {DWUILastBuildReport+=FString(TEXT("ERROR refusing ambiguous single-scroll topology: "))+ScrollName+TEXT("\n");return false;}
    Out.bNeedsReparent=Original;
    DWUILastBuildReport+=FString::Printf(TEXT("Validated %s: %s; all original widget references cached\n"),ScrollName,Original?TEXT("flatten ready"):TEXT("already flattened"));
    return true;
}

void FlattenBookScrollContent(const FBookScrollContent& Widgets)
{
    if(Widgets.bNeedsReparent)
    {
        Widgets.Content->RemoveFromParent();Widgets.LegacyScroll->RemoveFromParent();
        Widgets.Height->SetContent(Widgets.Content);
        auto* LegacySlot=Widgets.Parent->AddChildToVerticalBox(Widgets.LegacyScroll);LegacySlot->SetPadding(FMargin(0));
    }
    Widgets.Height->ClearHeightOverride();Widgets.Height->ClearMinDesiredHeight();Widgets.Height->ClearMaxDesiredHeight();
    Widgets.LegacyScroll->SetVisibility(ESlateVisibility::Collapsed);Widgets.LegacyScroll->SetIsEnabled(false);
}
}
#endif

bool UDWUIAuthoringLibrary::UpgradeSettingsBookAssets(bool bInspectOnly)
{
    DWUILastBuildReport.Reset();
#if WITH_EDITOR
    auto* PanelBP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/UI/WBP_DWSettingsPanel.WBP_DWSettingsPanel"));
    if(!PanelBP||!PanelBP->WidgetTree||PanelBP->ParentClass!=UDWSettingsPanel::StaticClass())
    {DWUILastBuildReport=TEXT("ERROR compatible existing settings panel required\n");return false;}
    auto* Tabs=Cast<UHorizontalBox>(PanelBP->WidgetTree->FindWidget(TEXT("SettingsTabs")));
    auto* Options=Cast<UWidgetSwitcher>(PanelBP->WidgetTree->FindWidget(TEXT("OptionsSwitcher")));
    if(!Tabs||!Options||Options->GetChildrenCount()!=3
        ||Options->GetChildAt(0)!=PanelBP->WidgetTree->FindWidget(TEXT("AudioOptions"))
        ||Options->GetChildAt(1)!=PanelBP->WidgetTree->FindWidget(TEXT("ControlsOptions"))
        ||Options->GetChildAt(2)!=PanelBP->WidgetTree->FindWidget(TEXT("ControllerOptions")))
    {DWUILastBuildReport=TEXT("ERROR existing audio, keys and guide pages must retain indices 0/1/2\n");return false;}
    FBookScrollContent KeyRowsContent,GuideContent;
    if(!CacheBookScrollContent(PanelBP->WidgetTree,TEXT("ControlsOptions"),TEXT("KeyRowsHeight"),TEXT("KeyRowsScroll"),TEXT("KeyRows"),KeyRowsContent)
        ||!CacheBookScrollContent(PanelBP->WidgetTree,TEXT("ControllerOptions"),TEXT("ControlsGuideHeight"),TEXT("ControlsGuideScroll"),TEXT("ControlsGuideColumn"),GuideContent))return false;
    TArray<FBookScreen> Screens;
    for(const TCHAR* Path:{TEXT("/Game/DoughWorld/UI/WBP_DWGameplay.WBP_DWGameplay"),TEXT("/Game/DoughWorld/UI/Frontend/WBP_DWFrontend.WBP_DWFrontend")})
    {
        FBookScreen Screen;if(!InspectBookScreen(LoadObject<UWidgetBlueprint>(nullptr,Path),Screen))return false;Screens.Add(Screen);
        DWUILastBuildReport+=FString::Printf(TEXT("Validated %s: %s; existing tree, page index 5, controls and popup retained\n"),Path,Screen.Existing?TEXT("book already present"):TEXT("local reparent ready"));
    }
    if(bInspectOnly){DWUILastBuildReport+=TEXT("INSPECT ONLY: no packages modified\n");return true;}
    // All insertion points were checked before the first mutation. No WidgetTree or resource is regenerated.
    PanelBP->Modify();PanelBP->WidgetTree->Modify();Tabs->SetVisibility(ESlateVisibility::Collapsed);Tabs->SetIsEnabled(false);
    FlattenBookScrollContent(KeyRowsContent);FlattenBookScrollContent(GuideContent);
    BookWidgetStyle(PanelBP->WidgetTree->RootWidget);
    if(!SaveBP(PanelBP))return false;
    for(FBookScreen& Screen:Screens)
    {
        Screen.BP->Modify();Screen.BP->WidgetTree->Modify();AppendSettingsBook(Screen);
        if(!SaveBP(Screen.BP))return false;
    }
    DWUILastBuildReport+=TEXT("Saved three existing UI assets. Five bookmarks, fixed actions, one outer content scroll, 1240x780 ScaleToFit book and 260x128 release-point popup. Empty legacy inner scrolls retained collapsed; existing footer untouched.\n");
    return true;
#else
    DWUILastBuildReport=TEXT("Settings book authoring is only available in the Unreal Editor.");return false;
#endif
}
