#pragma once

// Runtime-only layout helpers. The authored WBP controls, bindings and assets stay intact.
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DWC01UIStyle.h"

namespace DWC04Settings
{
inline FLinearColor RowColor()
{
    FLinearColor Color=DWC01UI::Ink();Color.A=.86f;return Color;
}
inline void Label(UTextBlock* Text,int32 Size=20,bool bLight=true)
{
    if(!Text)return;
    FSlateFontInfo Font=Text->GetFont();Font.Size=Size;Font.OutlineSettings.OutlineSize=0;Text->SetFont(Font);
    Text->SetColorAndOpacity(bLight?FLinearColor::White:DWC01UI::Ink());
    Text->SetShadowColorAndOpacity(FLinearColor::Transparent);Text->SetShadowOffset(FVector2D::ZeroVector);
    Text->SetAutoWrapText(false);Text->SetJustification(ETextJustify::Left);
}
inline UBorder* Card(UWidgetTree* Tree,const FString& Name,UWidget* Content)
{
    auto* Border=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*Name));
    Border->SetBrush(DWC01UI::Surface(RowColor(),FVector2D(920,68)));
    Border->SetBrushColor(FLinearColor::White);Border->SetPadding(FMargin(18,8));
    Border->SetHorizontalAlignment(HAlign_Fill);Border->SetVerticalAlignment(VAlign_Center);
    Border->SetContent(Content);return Border;
}
inline void RowChildren(UWidget* LabelWidget,UWidget* Control)
{
    if(auto* Cell=Cast<UHorizontalBoxSlot>(LabelWidget?LabelWidget->Slot:nullptr))
    {Cell->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Cell->SetPadding(FMargin(0,0,24,0));Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Center);}
    if(auto* Cell=Cast<UHorizontalBoxSlot>(Control?Control->Slot:nullptr))
    {Cell->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));Cell->SetPadding(FMargin(0));Cell->SetHorizontalAlignment(HAlign_Right);Cell->SetVerticalAlignment(VAlign_Center);}
}
inline UBorder* Pair(UWidgetTree* Tree,UVerticalBox* Column,UTextBlock* Text,UWidget* Control,const FString& Name,float Width=310.f)
{
    if(!Tree||!Column||!Text||!Control)return nullptr;
    if(auto* Existing=Cast<UBorder>(Tree->FindWidget(FName(*Name))))return Existing;
    // Reuse a known authored size wrapper when present, instead of leaving an empty gap.
    USizeBox* ControlSize=Cast<USizeBox>(Control->GetParent());
    if(ControlSize&&ControlSize->GetContent()!=Control)ControlSize=nullptr;
    Text->RemoveFromParent();
    if(ControlSize)ControlSize->RemoveFromParent();
    else
    {
        Control->RemoveFromParent();ControlSize=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(Name+TEXT("_Control"))));
        ControlSize->SetContent(Control);
    }
    ControlSize->SetWidthOverride(Width);ControlSize->SetHeightOverride(44);
    auto* Row=Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),FName(*(Name+TEXT("_Body"))));
    Row->AddChildToHorizontalBox(Text);Row->AddChildToHorizontalBox(ControlSize);RowChildren(Text,ControlSize);
    auto* Background=Card(Tree,Name,Row);
    auto* Height=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(Name+TEXT("_Height"))));
    Height->SetHeightOverride(68);Height->SetContent(Background);
    auto* Cell=Column->AddChildToVerticalBox(Height);Cell->SetPadding(FMargin(0,0,0,12));Cell->SetHorizontalAlignment(HAlign_Fill);
    Label(Text);return Background;
}
inline void ExistingPair(UWidgetTree* Tree,const TCHAR* Name,const TCHAR* LabelName,const TCHAR* ControlName)
{
    auto* Row=Cast<UHorizontalBox>(Tree->FindWidget(Name));
    const FString Prefix(Name);
    auto* Height=Cast<USizeBox>(Tree->FindWidget(FName(*(Prefix+TEXT("_Height")))));
    auto* LabelSize=Cast<USizeBox>(Tree->FindWidget(FName(*(Prefix+TEXT("_LabelWidth")))));
    UWidget* Control=Tree->FindWidget(ControlName);
    if(!Row||!Height||!LabelSize||!Control)return;
    LabelSize->ClearWidthOverride();
    USizeBox* ControlSize=Cast<USizeBox>(Control->GetParent());
    if(!ControlSize)
    {
        Control->RemoveFromParent();ControlSize=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*(Prefix+TEXT("_ControlWidth"))));
        ControlSize->SetContent(Control);Row->AddChildToHorizontalBox(ControlSize);
    }
    ControlSize->SetWidthOverride(310);ControlSize->SetHeightOverride(44);RowChildren(LabelSize,ControlSize);
    if(Height->GetContent()==Row)
    {Row->RemoveFromParent();Height->SetContent(Card(Tree,Prefix+TEXT("_C04Background"),Row));}
    Height->SetHeightOverride(68);
    if(auto* Cell=Cast<UVerticalBoxSlot>(Height->Slot)){Cell->SetPadding(FMargin(0,0,0,12));Cell->SetHorizontalAlignment(HAlign_Fill);}
    Label(Cast<UTextBlock>(Tree->FindWidget(LabelName)));
}
}
