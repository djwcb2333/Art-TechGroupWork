#include "DWInteractionAuthoringLibrary.h"
#include "DWInteractionPromptWidget.h"
static FString DWPromptBuildReport;
FString UDWInteractionAuthoringLibrary::GetLastBuildReport(){return DWPromptBuildReport;}
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
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/ComboBoxString.h"
#include "UObject/UnrealType.h"
#include "Brushes/SlateRoundedBoxBrush.h"
// ComboBoxString exposes no public font setter in 5.8. Write its serialized
// Designer template property before constructing any live Slate widget.
static void DWSetComboTemplateFont(UComboBoxString* Combo,const FSlateFontInfo& Info)
{
    if(FStructProperty* Property=FindFProperty<FStructProperty>(UComboBoxString::StaticClass(),TEXT("Font")))
        *Property->ContainerPtrToValuePtr<FSlateFontInfo>(Combo)=Info;
}
#endif
bool UDWInteractionAuthoringLibrary::CreateInteractionWidget(bool bReplaceExisting)
{
#if WITH_EDITOR
    DWPromptBuildReport.Reset();
    const FString Path=TEXT("/Game/DoughWorld/Maps/Gameplay/UI/Interaction/WBP_DWInteractionPrompt"),Name=TEXT("WBP_DWInteractionPrompt");
    UWidgetBlueprint* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Path+TEXT(".")+Name));
    if(BP&&!bReplaceExisting){DWPromptBuildReport=TEXT("Preserved existing interaction Designer layout.");return BP->ParentClass==UDWInteractionPromptWidget::StaticClass();}
    if(BP&&BP->ParentClass!=UDWInteractionPromptWidget::StaticClass()){DWPromptBuildReport=TEXT("Incompatible parent; preserved asset.");return false;}
    if(!BP)
    {
        UPackage* Package=CreatePackage(*Path);
        BP=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDWInteractionPromptWidget::StaticClass(),Package,FName(*Name),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass(),TEXT("DoughWorldInteraction")));
        if(!BP)return false;FAssetRegistryModule::AssetCreated(BP);
    }
    else BP->WidgetTree=NewObject<UWidgetTree>(BP,MakeUniqueObjectName(BP,UWidgetTree::StaticClass(),TEXT("WidgetTree")),RF_Transactional);
    if(!BP->WidgetTree)BP->WidgetTree=NewObject<UWidgetTree>(BP,TEXT("WidgetTree"),RF_Transactional);
    UWidgetTree* Tree=BP->WidgetTree;
    auto* Root=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("PromptCanvas"));Tree->RootWidget=Root;
    auto* Visual=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("PromptVisual"));Visual->bIsVariable=true;Visual->SetWidthOverride(280);Visual->SetHeightOverride(120);Visual->SetRenderTransformPivot(FVector2D(.5f,1));
    auto* CS=Root->AddChildToCanvas(Visual);CS->SetAnchors(FAnchors(.5f,1.f));CS->SetAlignment(FVector2D(.5f,1.f));CS->SetOffsets(FMargin(0,-18,280,120));
    auto* V=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("PromptLayout"));Visual->SetContent(V);
    UFont* CJK=LoadObject<UFont>(nullptr,TEXT("/Game/DoughWorld/Maps/Gameplay/UI/Interaction/Fonts/F_InteractionChinese.F_InteractionChinese"));
    UFont* Latin=LoadObject<UFont>(nullptr,TEXT("/Game/DoughWorld/Maps/Gameplay/UI/Interaction/Fonts/F_InteractionLatin.F_InteractionLatin"));
    UFont* Fallback=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    auto Text=[&](FName N,const TCHAR* Value,int Size,UFont* Font)
    {
        auto* T=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),N);T->bIsVariable=true;T->SetText(FText::FromString(Value));
        FSlateFontInfo Info(Font?Font:Fallback,Size);Info.OutlineSettings.OutlineSize=2;Info.OutlineSettings.OutlineColor=FLinearColor(.07f,.035f,.018f,1);
        T->SetFont(Info);T->SetColorAndOpacity(FSlateColor(FLinearColor(1,.94f,.77f,1)));T->SetShadowColorAndOpacity(FLinearColor(0,0,0,.7f));T->SetShadowOffset(FVector2D(0,2));T->SetJustification(ETextJustify::Center);T->SetAutoWrapText(false);return T;
    };
    auto* Title=Text(TEXT("PromptTitle"),TEXT("酵母"),32,CJK);auto* TS=V->AddChildToVerticalBox(Title);TS->SetHorizontalAlignment(HAlign_Center);TS->SetPadding(FMargin(0,0,0,6));
    auto* H=Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("ActionRow"));auto* HS=V->AddChildToVerticalBox(H);HS->SetHorizontalAlignment(HAlign_Center);
    auto* Keycap=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("Keycap"));Keycap->bIsVariable=true;Keycap->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,5.f));Keycap->SetBrushColor(FLinearColor(.92f,.83f,.64f,1));Keycap->SetPadding(FMargin(9,1));
    auto* Key=Text(TEXT("KeyText"),TEXT("F"),23,Latin);auto KF=Key->GetFont();KF.OutlineSettings.OutlineSize=0;Key->SetFont(KF);Key->SetColorAndOpacity(FSlateColor(FLinearColor(.07f,.035f,.018f,1)));Key->SetShadowOffset(FVector2D::ZeroVector);Keycap->SetContent(Key);
    auto* KS=H->AddChildToHorizontalBox(Keycap);KS->SetVerticalAlignment(VAlign_Center);KS->SetPadding(FMargin(0,0,9,0));
    auto* Action=Text(TEXT("ActionText"),TEXT("长按采集"),20,CJK);auto* AS=H->AddChildToHorizontalBox(Action);AS->SetVerticalAlignment(VAlign_Center);
    auto* ProgressBox=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ProgressSize"));ProgressBox->SetWidthOverride(134);ProgressBox->SetHeightOverride(6);
    auto* PS=V->AddChildToVerticalBox(ProgressBox);PS->SetHorizontalAlignment(HAlign_Center);PS->SetPadding(FMargin(0,8,0,0));
    auto* Progress=Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("HarvestProgress"));Progress->bIsVariable=true;Progress->SetPercent(.35f);Progress->SetFillColorAndOpacity(FLinearColor(.62f,.83f,.42f,1));ProgressBox->SetContent(Progress);
    FProgressBarStyle BarStyle=Progress->GetWidgetStyle();BarStyle.BackgroundImage=FSlateRoundedBoxBrush(FLinearColor(.07f,.035f,.018f,.75f),3.f);BarStyle.FillImage=FSlateRoundedBoxBrush(FLinearColor::White,3.f);Progress->SetWidgetStyle(BarStyle);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
    if(BP->Status==BS_Error||!BP->GeneratedClass){DWPromptBuildReport=TEXT("Interaction widget compile failed.");return false;}
    BP->MarkPackageDirty();const FString Filename=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    const bool Saved=UPackage::SavePackage(BP->GetOutermost(),BP,*Filename,Args);
    int Count=0;Tree->ForEachWidget([&](UWidget*){++Count;});DWPromptBuildReport=FString::Printf(TEXT("%s interaction WBP, %d editable Designer widgets."),Saved?TEXT("Saved"):TEXT("Failed saving"),Count);return Saved;
#else
    return false;
#endif
}
bool UDWInteractionAuthoringLibrary::CreateHandDrawnCompositeFont(const FString& ChinesePath,const FString& LatinPath)
{
#if WITH_EDITOR
    UFontFace* Chinese=LoadObject<UFontFace>(nullptr,*ChinesePath);UFontFace* Latin=LoadObject<UFontFace>(nullptr,*LatinPath);
    if(!Chinese||!Latin){DWPromptBuildReport=TEXT("Missing imported Chinese or Latin font face.");return false;}
    const FString Path=TEXT("/Game/DoughWorld/Maps/Gameplay/UI/Interaction/Fonts/F_DWHandDrawn");
    UFont* Font=LoadObject<UFont>(nullptr,*(Path+TEXT(".F_DWHandDrawn")));
    if(!Font){Font=NewObject<UFont>(CreatePackage(*Path),TEXT("F_DWHandDrawn"),RF_Public|RF_Standalone);FAssetRegistryModule::AssetCreated(Font);}
    Font->FontCacheType=EFontCacheType::Runtime;
    FCompositeFont& Composite=Font->GetMutableInternalCompositeFont();Composite.DefaultTypeface.Fonts.Reset();Composite.FallbackTypeface.Typeface.Fonts.Reset();Composite.SubTypefaces.Reset();
    auto Add=[&](FTypeface& Typeface,UFontFace* Face)
    {for(FName Name:{FName(TEXT("Default")),FName(TEXT("Regular")),FName(TEXT("Bold"))}){auto& Entry=Typeface.Fonts.AddDefaulted_GetRef();Entry.Name=Name;Entry.Font=FFontData(Face);}};
    Add(Composite.DefaultTypeface,Latin);Add(Composite.FallbackTypeface.Typeface,Chinese);
    FCompositeSubFont& Sub=Composite.SubTypefaces.AddDefaulted_GetRef();Add(Sub.Typeface,Chinese);Sub.ScalingFactor=1.f;
    Sub.CharacterRanges.Add(FInt32Range::Inclusive(0x2E80,0x9FFF));Sub.CharacterRanges.Add(FInt32Range::Inclusive(0xF900,0xFAFF));Sub.CharacterRanges.Add(FInt32Range::Inclusive(0xFF00,0xFFEF));
    Font->MarkPackageDirty();const FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;return UPackage::SavePackage(Font->GetOutermost(),Font,*File,Args);
#else
    return false;
#endif
}
bool UDWInteractionAuthoringLibrary::AddLanguageSelectorToExistingUI()
{
#if WITH_EDITOR
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/Maps/Gameplay/UI/WBP_DWGameplay.WBP_DWGameplay"));
    if(!BP||!BP->WidgetTree)return false;
    auto* Body=Cast<UVerticalBox>(BP->WidgetTree->FindWidget(TEXT("SettingsPage_Body")));if(!Body)return false;
    UFont* Font=LoadObject<UFont>(nullptr,TEXT("/Game/DoughWorld/Maps/Gameplay/UI/Interaction/Fonts/F_DWHandDrawn.F_DWHandDrawn"));if(!Font)return false;
    if(!BP->WidgetTree->FindWidget(TEXT("LanguageCombo")))
    {
        BP->Modify();auto* Label=BP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("LanguageLabel"));Label->bIsVariable=true;Label->SetText(FText::FromString(TEXT("语言 / Language")));Label->SetFont(FSlateFontInfo(Font,20));Label->SetColorAndOpacity(FSlateColor(FLinearColor(.97f,.92f,.83f,1)));
        auto* Combo=BP->WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(),TEXT("LanguageCombo"));Combo->bIsVariable=true;Combo->AddOption(TEXT("简体中文"));Combo->AddOption(TEXT("English"));Combo->SetSelectedIndex(0);DWSetComboTemplateFont(Combo,FSlateFontInfo(Font,20));
        auto* LS=Cast<UVerticalBoxSlot>(Body->InsertChildAt(2,Label));if(LS)LS->SetPadding(FMargin(0,7));
        auto* CS=Cast<UVerticalBoxSlot>(Body->InsertChildAt(3,Combo));if(CS)CS->SetPadding(FMargin(0,7));
    }
    // Preserve every layout and point size; the font asset handles Latin and Chinese glyph ranges.
    const TArray<FString> Names={TEXT("WBP_DWGameplay"),TEXT("WBP_DWInventorySlot"),TEXT("WBP_DWRecipeEntry"),TEXT("WBP_DWSaveSlot")};
    for(const FString& N:Names)
    {
        const FString Path=TEXT("/Game/DoughWorld/Maps/Gameplay/UI/")+N;auto* WidgetBP=LoadObject<UWidgetBlueprint>(nullptr,*(Path+TEXT(".")+N));if(!WidgetBP||!WidgetBP->WidgetTree)continue;
        WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
        {
            if(auto* Text=Cast<UTextBlock>(Widget)){auto Info=Text->GetFont();Info.FontObject=Font;Info.TypefaceFontName=TEXT("Default");Text->SetFont(Info);if(Text->GetText().ToString()==TEXT("垂直同步"))Text->SetAutoWrapText(false);}
            if(auto* Combo=Cast<UComboBoxString>(Widget)){auto Info=Combo->GetFont();Info.FontObject=Font;Info.TypefaceFontName=TEXT("Default");DWSetComboTemplateFont(Combo,Info);}
        });
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);FKismetEditorUtilities::CompileBlueprint(WidgetBP);
        if(WidgetBP->Status==BS_Error)return false;
        WidgetBP->MarkPackageDirty();const FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
        if(!UPackage::SavePackage(WidgetBP->GetOutermost(),WidgetBP,*File,Args))return false;
    }
    return true;
#else
    return false;
#endif
}
