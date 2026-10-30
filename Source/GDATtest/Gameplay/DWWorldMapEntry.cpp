#include "DWWorldMapEntry.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Styling/CoreStyle.h"

UDWWorldMapEntryButton::UDWWorldMapEntryButton(){InitIsFocusable(false);}

void UDWWorldMapEntry::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    if(!WidgetTree)return;
    auto* Button=WidgetTree->ConstructWidget<UDWWorldMapEntryButton>(UDWWorldMapEntryButton::StaticClass(),TEXT("WorldMapEntryButton"));
    Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("WorldMapEntryLabel"));
    if(!Button||!Label)return;
    FButtonStyle Style=Button->GetStyle();
    Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.23f,.21f,.18f,.72f),8.f));
    Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.37f,.34f,.28f,.82f),8.f));
    Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.27f,.25f,.21f,.82f),8.f));
    Style.SetNormalPadding(FMargin(6,3));Style.SetPressedPadding(FMargin(6,3));
    Button->SetStyle(Style);Button->OnClicked.AddDynamic(this,&UDWWorldMapEntry::OpenMap);
    Label->SetJustification(ETextJustify::Center);Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    if(auto* ContentSlot=Cast<UButtonSlot>(Button->AddChild(Label)))
    {ContentSlot->SetHorizontalAlignment(HAlign_Center);ContentSlot->SetVerticalAlignment(VAlign_Center);}
    WidgetTree->RootWidget=Button;SetVisibility(ESlateVisibility::Collapsed);
}

void UDWWorldMapEntry::InitializeEntry(ADWGameplayHUD* InHUD)
{
    HUD=InHUD;
    if(Label)
    {
        FSlateFontInfo Font=InHUD&&InHUD->GameUIFont?FSlateFontInfo(InHUD->GameUIFont,16):FCoreStyle::GetDefaultFontStyle("Regular",16);
        Font.OutlineSettings.OutlineSize=0;Label->SetFont(Font);
    }
    SetDesiredSizeInViewport(FVector2D(84.f,32.f));
}

void UDWWorldMapEntry::UpdateEntry(bool bShow,float Diameter,FVector2D Margin)
{
    if(Label)Label->SetText(DWText(this,TEXT("地图"),TEXT("Map")));
    const ESlateVisibility NewVisibility=bShow?ESlateVisibility::Visible:ESlateVisibility::Collapsed;
    if(GetVisibility()!=NewVisibility)SetVisibility(NewVisibility);
    if(!bShow||!FMath::IsFinite(Diameter)||Margin.ContainsNaN())return;
    const FVector2D Position(-Margin.X,-Margin.Y-FMath::Clamp(Diameter,160.f,400.f)-10.f);
    if(!bPositioned||!Position.Equals(LastPosition,.1))
    {
        SetPositionInViewport(Position,false);SetAnchorsInViewport(FAnchors(1.f,1.f));SetAlignmentInViewport(FVector2D(1.f,1.f));
        LastPosition=Position;bPositioned=true;
    }
}
void UDWWorldMapEntry::OpenMap(){if(auto* Owner=HUD.Get())Owner->ToggleWorldMap();}
