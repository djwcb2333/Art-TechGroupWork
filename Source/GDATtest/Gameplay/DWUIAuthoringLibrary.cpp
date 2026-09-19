#include "DWUIAuthoringLibrary.h"
#include "DWGameplayWidget.h"
#include "DWUIEntryWidgets.h"
#include "DWProgressRing.h"
#include "DWSettingsPanel.h"

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
#include "Sound/SoundClass.h"

namespace
{
    const FLinearColor Ink(.97f,.92f,.83f,1),Muted(.7f,.68f,.63f,1),Gold(.88f,.64f,.28f,1),Panel(.13f,.10f,.085f,.97f);
    struct FUIDesigner
    {
        UWidgetTree* Tree;
        explicit FUIDesigner(UWidgetBlueprint* BP):Tree(BP->WidgetTree){}
        template<class T>T* Make(const FString& Name,UClass* Class=T::StaticClass())
        {
            T* W=Tree->ConstructWidget<T>(Class,FName(*Name));W->bIsVariable=true;return W;
        }
        UTextBlock* Text(const FString& Name,const FString& Value,int32 Size=18,FLinearColor Color=Ink)
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
        UBorder* Card(const FString& Name,UWidget* Child,FLinearColor Color=Panel,FMargin Padding=FMargin(20))
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
        {V(Parent,Text(Prefix+TEXT("_Heading"),Title,30,Gold));V(Parent,Text(Prefix+TEXT("_Description"),Description,16,Muted));}
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
        auto* Image=D.Make<UImage>(TEXT("ItemIcon"));Image->SetColorAndOpacity(Gold);
        D.V(Content,D.Size(TEXT("ItemIconSize"),Image,32,32),3)->SetHorizontalAlignment(HAlign_Center);
        auto* Name=D.Text(TEXT("ItemNameText"),TEXT("物品名称"),14);Name->SetJustification(ETextJustify::Center);D.V(Content,Name,4);
        auto* Quantity=D.Text(TEXT("QuantityText"),TEXT("40"),15);Quantity->SetJustification(ETextJustify::Right);D.V(Content,Quantity,0);
        auto* Button=D.Make<UButton>(TEXT("SlotButton"));Button->SetBackgroundColor(FLinearColor(.34f,.27f,.21f));auto* BSlot=Cast<UButtonSlot>(Button->AddChild(Content));if(BSlot)BSlot->SetPadding(FMargin(8));
        BP->WidgetTree->RootWidget=D.Size(TEXT("SlotDimensions"),Button,90,104);
    }
    void BuildRecipe(UWidgetBlueprint* BP)
    {
        FUIDesigner D(BP);auto* V=D.Make<UVerticalBox>(TEXT("RecipeContent"));
        D.V(V,D.Text(TEXT("RecipeNameText"),TEXT("配方名称"),24,Gold));
        D.V(V,D.Text(TEXT("IngredientsText"),TEXT("需要：材料 × 数量（拥有 0）"),16));
        D.V(V,D.Text(TEXT("OutputsText"),TEXT("获得：物品 × 数量"),16,FLinearColor(.35f,.8f,.55f)));
        D.V(V,D.Text(TEXT("RequirementText"),TEXT("仅酵母形态可制作"),14,Muted));
        D.V(V,D.Button(TEXT("CraftButton"),TEXT("制作一份"),TEXT("CraftButtonText")));
        BP->WidgetTree->RootWidget=D.Card(TEXT("RecipeCard"),V);
    }
    void BuildSaveSlot(UWidgetBlueprint* BP)
    {
        FUIDesigner D(BP);auto* Row=D.Make<UHorizontalBox>(TEXT("SaveRow"));auto* Labels=D.Make<UVerticalBox>(TEXT("SaveLabels"));
        D.V(Labels,D.Text(TEXT("SlotNameText"),TEXT("存档槽位"),22),3);D.V(Labels,D.Text(TEXT("SlotDateText"),TEXT("保存时间 / 新冒险"),15,Muted),3);D.H(Row,Labels,8,true)->SetVerticalAlignment(VAlign_Center);
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
        D.V(Status,D.Text(TEXT("TransformationValueText"),TEXT("变身 0%"),18));auto* TP=D.Make<UProgressBar>(TEXT("TransformationBar"));TP->SetFillColorAndOpacity(FLinearColor(.34f,.78f,.57f));D.V(Status,D.Size(TEXT("TransformationBarSize"),TP,0,10));D.V(Status,D.Text(TEXT("FormText"),TEXT("● 面团形态 · [E] 变身"),17,Gold));
        D.Canvas(Hud,D.Card(TEXT("StatusCard"),Status),FAnchors(0,0),FMargin(28,28,330,215));
        auto* Brewing=D.Make<UVerticalBox>(TEXT("BrewingContent"));D.V(Brewing,D.Text(TEXT("BrewingTitle"),TEXT("疾跑酿造"),19,Gold))->SetHorizontalAlignment(HAlign_Center);
        auto* RingLayer=D.Make<UOverlay>(TEXT("RingLayer"));RingLayer->AddChildToOverlay(D.Make<UDWProgressRing>(TEXT("SprintRing")));
        auto* RingText=D.Text(TEXT("RingValueText"),TEXT("0%"),24);auto* RingSlot=RingLayer->AddChildToOverlay(RingText);RingSlot->SetHorizontalAlignment(HAlign_Center);RingSlot->SetVerticalAlignment(VAlign_Center);
        D.V(Brewing,D.Size(TEXT("RingDimensions"),RingLayer,92,92))->SetHorizontalAlignment(HAlign_Center);
        D.V(Brewing,D.Text(TEXT("BrewingHint"),TEXT("停止后保留进度"),14,Muted))->SetHorizontalAlignment(HAlign_Center);D.V(Brewing,D.Text(TEXT("AlcoholCountText"),TEXT("酒精 × 0"),18))->SetHorizontalAlignment(HAlign_Center);
        D.Canvas(Hud,D.Card(TEXT("BrewingCard"),Brewing),FAnchors(1,0),FMargin(-28,28,185,242),FVector2D(1,0));
        auto* Help=D.Text(TEXT("ControlsHint"),TEXT("WASD 移动   Shift 疾跑   空格 冲刺   右键拖拽 视角   左键 投掷   F 采集   B 背包   Tab 制作   Esc 菜单"),15);Help->SetJustification(ETextJustify::Center);
        D.Canvas(Hud,D.Card(TEXT("ControlsCard"),Help),FAnchors(.5f,1),FMargin(0,-22,1120,60),FVector2D(.5f,1));
        auto* Interaction=D.Text(TEXT("InteractionText"),TEXT("按住 F 采集 · 0%"),20,Gold);Interaction->SetJustification(ETextJustify::Center);
        D.Canvas(Hud,D.Card(TEXT("InteractionContainer"),Interaction),FAnchors(.5f,1),FMargin(0,-104,570,60),FVector2D(.5f,1));

        auto* Switch=D.Make<UWidgetSwitcher>(TEXT("PageSwitcher"));Switch->SetActiveWidgetIndex(0);
        auto* Menu=D.Card(TEXT("MenuRoot"),Switch,FLinearColor(.025f,.02f,.02f,.86f),FMargin(30));D.Canvas(Root,Menu,FAnchors(0,0,1,1),FMargin(0));

        UVerticalBox* TitleBody=nullptr;auto* TitlePage=D.Page(TEXT("TitlePage"),TitleBody,620);
        auto* Subtitle=D.Text(TEXT("SubtitleText"),TEXT("DOUGH WORLD"),22,Gold);Subtitle->SetAutoWrapText(false);
        D.V(TitleBody,Subtitle,16)->SetHorizontalAlignment(HAlign_Center);
        auto* Title=D.Text(TEXT("TitleText"),TEXT("面团世界"),52);Title->SetAutoWrapText(false);
        D.V(TitleBody,Title,24)->SetHorizontalAlignment(HAlign_Center);
        auto* Tagline=D.Text(TEXT("Tagline"),TEXT("探索 · 收集 · 发酵 · 生存"),18,Muted);Tagline->SetAutoWrapText(false);
        D.V(TitleBody,Tagline,14)->SetHorizontalAlignment(HAlign_Center);
        D.V(TitleBody,D.Button(TEXT("StartButton"),TEXT("开始游戏")));D.V(TitleBody,D.Button(TEXT("TitleSettingsButton"),TEXT("设置")));D.V(TitleBody,D.Button(TEXT("QuitButton"),TEXT("退出游戏")));
        auto* TitleOverlay=D.Make<UOverlay>(TEXT("TitlePageOverlay"));auto* TitleBG=D.Make<UImage>(TEXT("TitleBackgroundImage"));TitleBG->SetVisibility(ESlateVisibility::Collapsed);TitleOverlay->AddChildToOverlay(TitleBG);TitleOverlay->AddChildToOverlay(TitlePage);AddPage(Switch,TitleOverlay);

        UVerticalBox* Saves=nullptr;auto* SavesPage=D.Page(TEXT("SaveSlotsPage"),Saves,1060);D.Heading(Saves,TEXT("Slots"),TEXT("选择存档"),TEXT("最多 3 个存档。新建不会覆盖已有存档；删除需要再次确认。"));
        auto* SaveList=D.Make<UVerticalBox>(TEXT("SaveSlotList"));for(int32 I=0;I<3;++I)D.V(SaveList,D.Make<UDWSaveSlotWidget>(TEXT("SaveSlotPreview_")+FString::FromInt(I),SaveClass));D.V(Saves,SaveList);D.V(Saves,D.Button(TEXT("SlotsBackButton"),TEXT("返回标题")));AddPage(Switch,SavesPage);

        UVerticalBox* Inventory=nullptr;auto* InventoryPage=D.Page(TEXT("InventoryCard"),Inventory,660);D.Heading(Inventory,TEXT("Inventory"),TEXT("背包"),TEXT("点击物品查看用途与使用。按 B 返回游戏。"));
        auto* Grid=D.Make<UUniformGridPanel>(TEXT("InventoryGrid"));FillInventoryPreview(D,Grid,SlotClass,TEXT("InventoryPreview_"));D.V(Inventory,Grid);D.V(Inventory,D.Text(TEXT("InventoryCapacityText"),TEXT("每槽上限 40 · 共 24 槽"),15,Muted));D.V(Inventory,D.Text(TEXT("InventoryDetailsText"),TEXT("选择物品查看用途"),17));D.V(Inventory,D.Button(TEXT("InventoryUseButton"),TEXT("使用一个")));D.V(Inventory,D.Button(TEXT("InventoryCloseButton"),TEXT("返回游戏")));AddPage(Switch,InventoryPage);

        UVerticalBox* Crafting=nullptr;auto* CraftPage=D.Page(TEXT("CraftingCard"),Crafting,1120);D.Heading(Crafting,TEXT("Crafting"),TEXT("制作台"),TEXT("左侧背包，右侧配方。按 Tab 返回游戏。"));
        auto* CraftRow=D.Make<UHorizontalBox>(TEXT("CraftingColumns"));auto* CraftBag=D.Make<UVerticalBox>(TEXT("CraftingBag"));auto* CGrid=D.Make<UUniformGridPanel>(TEXT("CraftingInventoryGrid"));FillInventoryPreview(D,CGrid,SlotClass,TEXT("CraftingPreview_"));D.V(CraftBag,CGrid);D.V(CraftBag,D.Text(TEXT("CraftingDetailsText"),TEXT("选择物品查看用途"),16));D.V(CraftBag,D.Button(TEXT("CraftingUseButton"),TEXT("使用一个")));D.H(CraftRow,CraftBag,10);
        auto* RecipesColumn=D.Make<UVerticalBox>(TEXT("RecipesColumn"));D.V(RecipesColumn,D.Text(TEXT("CraftingStatusText"),TEXT("酵母形态 · 可以制作"),17,Gold));auto* Recipes=D.Make<UVerticalBox>(TEXT("RecipeList"));for(int32 I=0;I<2;++I)D.V(Recipes,D.Make<UDWRecipeEntryWidget>(TEXT("RecipePreview_")+FString::FromInt(I),RecipeClass));D.V(RecipesColumn,Recipes);D.H(CraftRow,RecipesColumn,10,true);D.V(Crafting,CraftRow);D.V(Crafting,D.Button(TEXT("CraftingCloseButton"),TEXT("返回游戏")));AddPage(Switch,CraftPage);

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
