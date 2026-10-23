#include "DWGameplayWidget.h"
#include "DWCombatHUDModel.h"
#include "DWC01UIStyle.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWWorldEvent.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerController.h"
#include "DWUserSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CheckBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Brushes/SlateRoundedBoxBrush.h"

void UDWGameplayWidget::InitializeCombatSettings()
{
    if(!WidgetTree)return;
    CombatEnemyHealthCheck=Cast<UCheckBox>(GetWidgetFromName(TEXT("CombatEnemyHealthCheck")));
    CombatEnemyHealthLabel=Cast<UTextBlock>(GetWidgetFromName(TEXT("CombatEnemyHealthLabel")));
    if(!CombatEnemyHealthCheck)
    {
        // Existing book page only. No saved WidgetTree replacement, reparenting, or asset creation.
        auto* General=Cast<UVerticalBox>(GetWidgetFromName(TEXT("SettingsBookGeneralPage")));if(!General)return;
        CombatEnemyHealthCheck=WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(),TEXT("CombatEnemyHealthCheck"));
        CombatEnemyHealthLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CombatEnemyHealthLabel"));
        CombatEnemyHealthLabel->SetFont(FCoreStyle::GetDefaultFontStyle("Regular",18));
        CombatEnemyHealthLabel->SetColorAndOpacity(FSlateColor(FLinearColor(.18f,.085f,.04f,1.f)));
        CombatEnemyHealthCheck->SetContent(CombatEnemyHealthLabel);
        General->AddChildToVerticalBox(CombatEnemyHealthCheck)->SetPadding(FMargin(4.f,18.f,4.f,8.f));
    }
    CombatEnemyHealthCheck->OnCheckStateChanged.RemoveDynamic(this,&UDWGameplayWidget::ChangeCombatEnemyHealth);
    CombatEnemyHealthCheck->OnCheckStateChanged.AddUniqueDynamic(this,&UDWGameplayWidget::ChangeCombatEnemyHealth);
    RefreshCombatSettings();
}
void UDWGameplayWidget::RefreshCombatSettings()
{
    if(CombatEnemyHealthLabel)CombatEnemyHealthLabel->SetText(DWText(this,TEXT("显示敌人生命"),TEXT("Show enemy health")));
    if(CombatEnemyHealthCheck)if(const auto* Settings=UDWUserSettings::Resolve(this))
        if(CombatEnemyHealthCheck->IsChecked()!=Settings->GetShowEnemyHealth())CombatEnemyHealthCheck->SetIsChecked(Settings->GetShowEnemyHealth());
}
void UDWGameplayWidget::ChangeCombatEnemyHealth(bool Checked)
{if(auto* Settings=UDWUserSettings::Resolve(this))Settings->SetShowEnemyHealth(Checked,true);}

void UDWGameplayWidget::RefreshCombatHUD()
{
    auto* GI=GetDWGameInstance();const auto* Rules=GI?GI->GetCombatRules().Get():nullptr;
    if(!Rules){CombatHUD={};CombatHUDRules=nullptr;return;}
    if(CombatHUDRules!=Rules)
    {
        FString Error;if(!FDWCombatHUDConfig::Build(*Rules,CombatHUDConfig,Error)){CombatHUD={};return;}
        CombatHUDRules=Rules;
    }
    DWCombatHUD::Build(GetPlayer(),CombatHUDConfig,CombatHUD);
    RefreshCombatSettings();
}


void UDWGameplayWidget::RefreshC01Presentation(float DeltaSeconds)
{
    auto* Player=GetPlayer();
    if(!bUseC01HUD||!CombatHUD.bReady||!IsValid(Player))
    {bC01VisualReady=false;C01VisualPlayer.Reset();return;}
    const FVector4f Target(float(CombatHUD.Special.Fraction),float(CombatHUD.Health.Fraction),
        float(CombatHUD.Water.Fraction),float(CombatHUD.Satiety.Fraction));
    if(!bC01VisualReady||C01VisualPlayer.Get()!=Player)
    {C01VisualResources=Target;C01VisualPlayer=Player;bC01VisualReady=true;return;}
    const float Seconds=FMath::IsFinite(C01ResourceSeconds)?FMath::Max(0.f,C01ResourceSeconds):.24f;
    const float Dt=FMath::IsFinite(DeltaSeconds)?FMath::Clamp(DeltaSeconds,0.f,.1f):0.f;
    // Presentation only. Resource eligibility and labels always use current native facts.
    // 95% settling at the exposed duration; no world-time or resource mutation.
    const float Alpha=Seconds>SMALL_NUMBER?1.f-FMath::Exp(-3.f*Dt/Seconds):1.f;
    for(int32 I=0;I<4;++I)C01VisualResources[I]=FMath::Lerp(C01VisualResources[I],Target[I],Alpha);
}

namespace DWCombatHUDPaint
{
void Box(FSlateWindowElementList& E,int32 Layer,const FGeometry& G,FVector2f P,FVector2f Size,FLinearColor C)
{FSlateDrawElement::MakeBox(E,Layer,G.ToPaintGeometry(Size,FSlateLayoutTransform(P)),FCoreStyle::Get().GetBrush("WhiteBrush"),ESlateDrawEffect::None,C);}
void Text(FSlateWindowElementList& E,int32 Layer,const FGeometry& G,FVector2f P,float Width,const FText& T,FSlateFontInfo Font,FLinearColor C)
{
    if(!FSlateApplication::IsInitialized()||Width<=0.f)return;
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    FVector2f Size(Measure->Measure(T,Font));
    if(Size.X>Width){Font.Size=FMath::Max(10,FMath::FloorToInt(Font.Size*Width/Size.X));Size=FVector2f(Measure->Measure(T,Font));}
    const FGeometry Clip=G.MakeChild(FVector2f(Width,Size.Y+3.f),FSlateLayoutTransform(P));E.PushClip(FSlateClippingZone(Clip));
    FSlateDrawElement::MakeText(E,Layer,G.ToPaintGeometry(Size,FSlateLayoutTransform(P)),T,Font,ESlateDrawEffect::None,C);E.PopClip();
}
FText Amount(const FDWCombatHUDResource& R)
{return FText::FromString(FString::Printf(TEXT("%.1f / %.0f"),R.Value,R.Maximum));}
}

int32 UDWGameplayWidget::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Cull,
    FSlateWindowElementList& E,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled)const
{
    const int32 Base=Super::NativePaint(Args,G,Cull,E,Layer,Style,ParentEnabled);
    auto* P=GetPlayer();auto* PC=GetOwningPlayer();
    if(!HUD||!HUD->IsSessionStarted()||
        !IsValid(P)||!PC||!GetWorld()||GetWorld()->bIsTearingDown||P->IsDead())return Base;
    const bool bMenuOpen=HUD->GetMenuPage()!=EDWMenuPage::None;
    const auto* GameInstance=GetDWGameInstance();
    const auto* Loading=GameInstance?GameInstance->GetSubsystem<UDWLoadingTransitionSubsystem>():nullptr;
    if((!bUseC01HUD&&HUD->IsBlockingGameplay())||(Loading&&Loading->IsTransitionActive())||
        (GameInstance&&GameInstance->IsPlayerRestorePending()))return Base;
    // The C01 screen-edge HUD continues painting while its existing offscreen
    // component animates through a paused menu. World-space combat overlays do not.
    using namespace DWCombatHUDPaint;
    FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle("Regular",16);
    if(HUD->GameUIFont)Font.FontObject=HUD->GameUIFont;
    Font.OutlineSettings.OutlineSize=1;Font.OutlineSettings.OutlineColor=FLinearColor(0,0,0,.8f);
    if(bUseC01HUD)Font.OutlineSettings.OutlineSize=0;
    const FLinearColor White(.97f,.95f,.87f,1.f),Gold(1.f,.72f,.26f,1.f),Red(1.f,.30f,.22f,1.f);
    const auto* Settings=UDWUserSettings::Resolve(this);const bool HealthEnabled=!Settings||Settings->GetShowEnemyHealth();
    const FVector2f ViewSize(G.GetLocalSize());
    const auto Project=[&](FVector Position,FVector2f& Out)
    {
        FVector2D Point;
        if(!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,Position,Point,true))return false;
        Out=FVector2f(Point);return !Point.ContainsNaN();
    };
    const bool bC01Cinematic=bMenuOpen||UDWWorldEventSubsystem::IsPlaying(this)||(bUseC01HUD&&!GetC01CinematicOffset().IsNearlyZero(.5));
    if(!bC01Cinematic&&bShowShotgunOverhead&&DWCombatHUD::ShouldShowShotgunOverhead(P))
    {
        // Fresh facts: shell commit, cancel and switch cannot leave a cached progress ring.
        const auto* GI=GetDWGameInstance();FDWCombatHUDSnapshot Shells;
        if(GI&&CombatHUDRules==GI->GetCombatRules().Get()&&DWCombatHUD::Build(P,CombatHUDConfig,Shells)&&
            Shells.bUsesMagazine&&Shells.MagazineCapacity>0&&Shells.MagazineCapacity<=128&&
            !ShotgunHeadWorldOffset.ContainsNaN()&&!ShotgunHeadScreenOffset.ContainsNaN()&&!ShotgunAmmoSlotSize.ContainsNaN()&&
            FMath::IsFinite(ViewSize.X)&&FMath::IsFinite(ViewSize.Y)&&ViewSize.X>16.f&&ViewSize.Y>16.f)
        {
            const auto Number=[](float V,float Default,float Min,float Max){return FMath::IsFinite(V)?FMath::Clamp(V,Min,Max):Default;};
            const auto Color=[](FLinearColor C,FLinearColor Default)
            {return FMath::IsFinite(C.R)&&FMath::IsFinite(C.G)&&FMath::IsFinite(C.B)&&FMath::IsFinite(C.A)?C.GetClamped():Default;};
            const auto* Capsule=P->GetCapsuleComponent();FVector2f At;
            if(Project(P->GetActorLocation()+FVector(0,0,Capsule?Capsule->GetScaledCapsuleHalfHeight():90.)+ShotgunHeadWorldOffset,At))
            {
                At+=FVector2f(ShotgunHeadScreenOffset);
                if(At.X>=0.f&&At.Y>=0.f&&At.X<=ViewSize.X&&At.Y<=ViewSize.Y)
                {
                    FVector2f Size(Number(ShotgunAmmoSlotSize.X,11.f,3.f,64.f),Number(ShotgunAmmoSlotSize.Y,17.f,3.f,64.f));
                    float Gap=Number(ShotgunAmmoSlotGap,4.f,0.f,32.f);
                    float Width=Shells.MagazineCapacity*Size.X+(Shells.MagazineCapacity-1)*Gap;
                    const float Scale=FMath::Min(1.f,(ViewSize.X-16.f)/Width);Size*=Scale;Gap*=Scale;Width*=Scale;
                    const FLinearColor Loaded=Color(ShotgunLoadedColor,Gold),Empty=Color(ShotgunEmptyColor,FLinearColor(.42f,.44f,.47f,.9f));
                    const int32 Count=FMath::Clamp(Shells.Loaded,0,Shells.MagazineCapacity);
                    if(!bUseC01HUD||!C01ShellDock)for(int32 I=0;I<Shells.MagazineCapacity;++I)
                    {
                        const FVector2f Top=At+FVector2f(-Width*.5f+I*(Size.X+Gap),-Size.Y*.5f);
                        const FSlateRoundedBoxBrush Fill(FLinearColor::White,Number(ShotgunShellCornerRadius,3.f,0.f,16.f)*Scale);
                        FSlateDrawElement::MakeBox(E,Base+2,G.ToPaintGeometry(Size,FSlateLayoutTransform(Top)),&Fill,ESlateDrawEffect::None,I<Count?Loaded:Empty);
                    }
                    if(Shells.bShotgunReloading&&Shells.ShotgunReloadDurationSeconds>0.)
                    {
                        const float Radius=Number(ShotgunReloadRingRadius,8.f,2.f,48.f);
                        const float RingGap=Number(ShotgunReloadRingGap,7.f,0.f,64.f);
                        const float Stroke=Number(ShotgunReloadRingThickness,2.f,.5f,8.f);
                        const FVector2f Center=At-FVector2f(0,Size.Y*.5f+RingGap+Radius);
                        const double Progress=FMath::IsFinite(Shells.ShotgunReloadProgress)?FMath::Clamp(Shells.ShotgunReloadProgress,0.,1.):0.;
                        TArray<FVector2f> Track,Fill;constexpr int32 Steps=48;
                        const auto Point=[&](double F){const double A=2.*PI*F;return Center+FVector2f(FMath::Sin(A)*Radius,-FMath::Cos(A)*Radius);};
                        for(int32 I=0;I<=Steps;++I)Track.Add(Point(static_cast<double>(I)/Steps));
                        FSlateDrawElement::MakeLines(E,Base+2,G.ToPaintGeometry(),Track,ESlateDrawEffect::None,Empty,true,Stroke);
                        if(Progress>0.)
                        {
                            const int32 Parts=FMath::CeilToInt(Progress*Steps);
                            for(int32 I=0;I<=Parts;++I)Fill.Add(Point(FMath::Min(Progress,static_cast<double>(I)/Steps)));
                            FSlateDrawElement::MakeLines(E,Base+3,G.ToPaintGeometry(),Fill,ESlateDrawEffect::None,Color(ShotgunReloadRingColor,Gold),true,Stroke);
                        }
                    }
                }
            }
        }
    }
    if(!bC01Cinematic)for(TActorIterator<ADWEnemyCharacter> It(GetWorld());It;++It)
    {
        auto* Enemy=*It;const auto* Capsule=Enemy->GetCapsuleComponent();
        if(DWCombatHUD::ShouldShowDaggerCircle(Enemy,P))
        {
            const FVector Center=Enemy->GetActorLocation()-FVector(0,0,Capsule?Capsule->GetScaledCapsuleHalfHeight()-2.:0.);
            const double Radius=Enemy->GetDaggerAlertRadius();
            const FLinearColor Color=Enemy->HasDaggerDiscoveryGrace(P)?FLinearColor(.2f,1.f,.35f,1.f):Enemy->IsEngagedForDagger()?Red:Gold;
            // Project each ground-plane segment. This Slate path is present in Shipping; no DrawDebug dependency.
            for(int32 I=0;I<48;++I)
            {
                const double A=2.*PI*I/48.,B=2.*PI*(I+1)/48.;FVector2f From,To;
                if(!Project(Center+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0),From)||
                    !Project(Center+FVector(FMath::Cos(B)*Radius,FMath::Sin(B)*Radius,0),To))continue;
                TArray<FVector2f> Line{From,To};FSlateDrawElement::MakeLines(E,Base+1,G.ToPaintGeometry(),Line,ESlateDrawEffect::None,Color,true,1.5f);
            }
        }
        if(!DWCombatHUD::ShouldShowEnemyHealth(Enemy,P,true))continue;
        FVector2f At;if(!Project(Enemy->GetActorLocation()+FVector(0,0,(Capsule?Capsule->GetScaledCapsuleHalfHeight():90.)+18.),At))continue;
        if(At.X<0||At.Y<0||At.X>ViewSize.X||At.Y>ViewSize.Y)continue;
        const bool Hostile=Enemy->IsHostileToCombatPlayer(P);const FLinearColor Color=Hostile?Red:Gold;
        if(HealthEnabled)
        {
            if(bUseC01HUD)
            {
                const FVector2f Position=At-FVector2f(53.f,0.f),Size(106.f,8.f);
                const FSlateBrush Track=DWC01UI::Texture(TEXT("T_DW_C01_Meter_Value_track_57b7b89f"),FVector2D(Size));
                FSlateDrawElement::MakeBox(E,Base+2,G.ToPaintGeometry(Size,FSlateLayoutTransform(Position)),&Track);
                const float Fraction=FMath::Clamp(Enemy->Health/Enemy->MaxHealth,0.f,1.f);
                if(Fraction>0.f)
                {
                    // Normalize the exported 80%-example strip to the live bar width.
                    const FSlateBrush Fill=DWC01UI::Texture(TEXT("T_DW_C01_Meter_Current_value_fb334a7f"),FVector2D(Size));
                    FSlateDrawElement::MakeBox(E,Base+3,G.ToPaintGeometry(FVector2f(Size.X*Fraction,Size.Y),FSlateLayoutTransform(Position)),&Fill);
                }
            }
            else
            {
                Box(E,Base+2,G,At-FVector2f(53,0),FVector2f(106,8),FLinearColor(0,0,0,.8f));
                Box(E,Base+3,G,At-FVector2f(51,-2),FVector2f(102*FMath::Clamp(Enemy->Health/Enemy->MaxHealth,0.f,1.f),4),Color);
            }
        }
        if(bUseC01HUD)continue; // C01 enemy bars carry no names, values or frame outlines.
        const FText State=Enemy->IsEngagedForDagger()?DWText(this,TEXT("交战"),TEXT("Engaged")):
            Hostile?DWText(this,TEXT("敌对"),TEXT("Hostile")):DWText(this,TEXT("中立"),TEXT("Neutral"));
        auto Small=Font;Small.Size=12;
        const FText Label=HealthEnabled?FText::FromString(FString::Printf(TEXT("%s  %.0f / %.0f"),*State.ToString(),Enemy->Health,Enemy->MaxHealth)):State;
        Text(E,Base+4,G,At-FVector2f(68,19),136,Label,Small,White);
    }

    if(bUseC01HUD)
    {
        if(!CombatHUD.bReady)return Base+4;
        const float Diameter=FMath::IsFinite(C01HUDDiameter)?FMath::Clamp(C01HUDDiameter,160.f,400.f):248.f;
        const FVector2f Margin(C01HUDMargin.ContainsNaN()?FVector2D(72,30):C01HUDMargin);
        const FVector2f CinematicOffset(GetC01CinematicOffset());
        const FVector2f Position=FVector2f(Margin.X,FMath::Max(0.f,ViewSize.Y-Margin.Y-Diameter))+CinematicOffset;
        const float Scale=Diameter/180.f;
        const auto Image=[&](const TCHAR* Asset,FVector2f Pos,FVector2f Size,int32 DrawLayer,FLinearColor Tint=FLinearColor::White)
        {
            const FSlateBrush Brush=DWC01UI::Texture(Asset,FVector2D(Size));
            FSlateDrawElement::MakeBox(E,DrawLayer,G.ToPaintGeometry(Size,FSlateLayoutTransform(Pos)),&Brush,ESlateDrawEffect::None,Tint);
        };
        const TCHAR* Tracks[]={TEXT("T_DW_C01_ResourceDial_Special_energy_Track"),TEXT("T_DW_C01_ResourceDial_Health_Track"),
            TEXT("T_DW_C01_ResourceDial_Water_Track"),TEXT("T_DW_C01_ResourceDial_Food_Track")};
        const TCHAR* Masks[]={TEXT("T_DW_C01_ResourceDial_Special_energy_Mask"),TEXT("T_DW_C01_ResourceDial_Health_Mask"),
            TEXT("T_DW_C01_ResourceDial_Water_Mask"),TEXT("T_DW_C01_ResourceDial_Food_Mask")};
        const FLinearColor Colors[]={FLinearColor::FromSRGBColor(FColor(196,161,96)),FLinearColor::FromSRGBColor(FColor(185,99,69)),
            FLinearColor::FromSRGBColor(FColor(143,156,141)),FLinearColor::FromSRGBColor(FColor(146,150,104))};
        const FVector4f Fractions=bC01VisualReady?C01VisualResources:FVector4f(float(CombatHUD.Special.Fraction),float(CombatHUD.Health.Fraction),
            float(CombatHUD.Water.Fraction),float(CombatHUD.Satiety.Fraction));
        for(int32 I=0;I<4;++I)
        {
            Image(Tracks[I],Position,FVector2f(Diameter,Diameter),Base+5);
            const float Fraction=FMath::Clamp(Fractions[I],0.f,1.f);
            if(Fraction<=0.f)continue;
            const float MinY=I<2?2.f:30.f,MaxY=I<2?178.f:150.f;
            const float FillY=(MaxY-Fraction*(MaxY-MinY))*Scale;
            const auto Slice=[&](float Left,float Top,float Right,float Bottom)
            {
                if(Right<=Left||Bottom<=Top)return;
                const FGeometry Clip=G.MakeChild(FVector2f(Right-Left,Bottom-Top),FSlateLayoutTransform(Position+FVector2f(Left,Top)));
                E.PushClip(FSlateClippingZone(Clip));
                Image(Masks[I],Position,FVector2f(Diameter,Diameter),Base+6,Colors[I]);
                E.PopClip();
            };
            if(Fraction>=1.f-UE_SMALL_NUMBER)
            {
                Image(Masks[I],Position,FVector2f(Diameter,Diameter),Base+6,Colors[I]);
                continue;
            }
            // Round the *visible* new liquid intersections, not points hidden below the white rims.
            // White-rim inner radii: outer band R82..60, inner band R52..16. The original mask
            // remains the final alpha authority, so neither a strip nor the broad body can fill the hole.
            const float Radius=FMath::Min(4.f*Scale,(MaxY*Scale-FillY)*.5f);
            const float PixelScale=FMath::Max(.25f,G.GetAccumulatedLayoutTransform().GetScale());
            const int32 Steps=FMath::Clamp(FMath::CeilToInt(Radius*PixelScale*2.f),4,64);
            for(int32 Step=0;Step<Steps;++Step)
            {
                const float Top=FillY+Radius*Step/Steps,Bottom=FillY+Radius*(Step+1)/Steps;
                const float T=(Top+Bottom)*.5f-FillY;
                const float Inset=Radius-FMath::Sqrt(FMath::Max(0.f,Radius*Radius-FMath::Square(Radius-T)));
                const float Y=(Top+Bottom)*.5f/Scale-90.f;
                const float OuterRadius=I<2?82.f:52.f,InnerRadius=I<2?60.f:16.f;
                if(FMath::Abs(Y)>=OuterRadius)continue; // Only white pixels exist on this row.
                const float OuterX=FMath::Sqrt(FMath::Max(0.f,OuterRadius*OuterRadius-Y*Y));
                const float InnerX=FMath::Abs(Y)<InnerRadius?FMath::Sqrt(FMath::Max(0.f,InnerRadius*InnerRadius-Y*Y)):1.5f;
                const bool LeftSide=(I==0||I==2);
                const float Left=LeftSide?90.f-OuterX:FMath::Max(91.5f,90.f+InnerX);
                const float Right=LeftSide?FMath::Min(88.5f,90.f-InnerX):90.f+OuterX;
                Slice(Left*Scale+Inset,Top,Right*Scale-Inset,Bottom);
            }
            Slice(0.f,FillY+Radius,Diameter,MaxY*Scale);
        }
        // The supplied alpha has a real central hole. Never place an opaque backing behind this.
        Image(TEXT("T_DW_C01_ResourceDial_WhiteRims"),Position,FVector2f(Diameter,Diameter),Base+7);
        const FText Acquired=HUD->GetPickupToast();
        if(!Acquired.IsEmpty()&&!C01PickupPanel)
        {
            const double Age=HUD->GetPickupToastAge(),Remaining=HUD->GetPickupToastRemaining();
            const double Enter=FMath::IsFinite(HUD->C01PickupEnterSeconds)?FMath::Max(0.,double(HUD->C01PickupEnterSeconds)):.18;
            const double Exit=FMath::IsFinite(HUD->C01PickupExitSeconds)?FMath::Max(0.,double(HUD->C01PickupExitSeconds)):.18;
            const float In=Enter>UE_SMALL_NUMBER?DWC01UI::Ease(float(FMath::Clamp(Age/Enter,0.,1.))):1.f;
            const float Out=Exit>UE_SMALL_NUMBER?DWC01UI::Ease(float(FMath::Clamp(Remaining/Exit,0.,1.))):1.f;
            const float Opacity=In*Out;
            const FVector2f ToastPosition=Position+FVector2f(-12.f*(1.f-In),-72.f-8.f*(1.f-Out));
            // Export canvas includes 2 units of transparent bleed on every side.
            Image(TEXT("T_DW_C01_PickupToast_Paper_fine_edge_53098739"),ToastPosition-FVector2f(2,2),FVector2f(234,52),Base+8,FLinearColor(1,1,1,Opacity));
            auto ToastFont=Font;ToastFont.Size=18;ToastFont.OutlineSettings.OutlineSize=0;
            FLinearColor ToastInk=DWC01UI::Ink();ToastInk.A=Opacity;
            Text(E,Base+9,G,ToastPosition+FVector2f(14,10),202,Acquired,ToastFont,ToastInk);
        }
        const auto& S=CombatHUD;
        if(S.Form!=EDWCombatBaseForm::Normal)
        {
            const FText Time=FText::Format(FText::FromString(TEXT("{0}  {1}s")),DWCombatHUD::FormLabel(this,S.Form),
                FText::FromString(FString::Printf(TEXT("%.1f"),S.FormRemaining)));
            auto FormFont=Font;FormFont.Size=14;Text(E,Base+7,G,Position-FVector2f(0,22),Diameter+30,Time,FormFont,S.bFormWarning?DWC01UI::Gold():White);
        }
        // Real hit-testable quick buttons are runtime UMG children. Keep this equipment/ammo label only.
        const float CellSize=FMath::IsFinite(C01QuickSlotSize)?FMath::Clamp(C01QuickSlotSize,64.f,128.f):92.f;
        const float Gap=FMath::IsFinite(C01QuickSlotGap)?FMath::Clamp(C01QuickSlotGap,0.f,32.f):16.f;
        const float Width=3.f*CellSize+2.f*Gap;
        const FVector2f Dock=FVector2f(FMath::Max(0.f,(ViewSize.X-Width)*.5f),FMath::Max(0.f,ViewSize.Y-Margin.Y-CellSize))+CinematicOffset;
        FText Equipment=S.EquipmentName;
        if(S.bUsesMagazine)Equipment=FText::Format(FText::FromString(TEXT("{0}   {1}/{2}  ·  {3}")),S.EquipmentName,S.Loaded,S.MagazineCapacity,S.Reserve);
        else if(S.bUsesAmmo)Equipment=FText::Format(FText::FromString(TEXT("{0}   {1}")),S.EquipmentName,S.Reserve);
        Text(E,Base+7,G,Dock-FVector2f(0,28),Width,Equipment,Font,White);
        return Base+9; // Single C01 HUD; includes the transient item acquisition overlay.
    }
    if(!bShowCombatInfoPanel||!CombatHUD.bReady)return Base+4;
    const auto& S=CombatHUD;const float Width=FMath::Clamp(CombatInfoWidth,260.f,FMath::Max(260.f,ViewSize.X-24.f));
    const float X=FMath::Clamp(CombatInfoMargin.X,8.f,FMath::Max(8.f,ViewSize.X-Width-8.f));
    const float Y=FMath::Max(8.f,ViewSize.Y-CombatInfoMargin.Y-172.f);
    Box(E,Base+5,G,FVector2f(X,Y),FVector2f(Width,172),FLinearColor(.04f,.035f,.025f,.76f));
    const auto Row=[&](int32 Index,FText T,FLinearColor C){Text(E,Base+6,G,FVector2f(X+10,Y+8+Index*23),Width-20,T,Font,C);};
    Row(0,FText::Format(DWText(this,TEXT("水分 {0}      饱腹 {1}"),TEXT("Water {0}    Satiety {1}")),Amount(S.Water),Amount(S.Satiety)),White);
    Row(1,S.Form==EDWCombatBaseForm::Normal?DWText(this,TEXT("普通形态 · 无倒计时"),TEXT("Normal form · no countdown")):
        FText::Format(DWText(this,TEXT("{0}形态剩余 {1} 秒"),TEXT("{0} form: {1} seconds left")),DWCombatHUD::FormLabel(this,S.Form),FText::FromString(FString::Printf(TEXT("%.1f"),S.FormRemaining))),S.bFormWarning?Gold:White);
    FText Equipment=FText::Format(DWText(this,TEXT("装备：{0}"),TEXT("Equipment: {0}")),S.EquipmentName);
    if(S.bUsesMagazine)Equipment=FText::Format(DWText(this,TEXT("{0} · 匣内 {1}/{2} · 备用 {3}"),TEXT("{0} · loaded {1}/{2} · reserve {3}")),S.EquipmentName,S.Loaded,S.MagazineCapacity,S.Reserve);
    else if(S.bUsesAmmo)Equipment=FText::Format(DWText(this,TEXT("{0} · 备用 {1}"),TEXT("{0} · reserve {1}")),S.EquipmentName,S.Reserve);
    Row(2,Equipment,S.AmmoReason==EDWCombatHUDReason::None?White:Gold);
    const auto* Input=Cast<ADWPlayerController>(PC);
    const FText DashKey=Input?Input->GetActionKeyLabel(EDWInputAction::Dash):FText::GetEmpty();
    const FText RootKey=Input?Input->GetActionKeyLabel(EDWInputAction::Transform):FText::GetEmpty();
    FText Dash=S.bDashing?DWText(this,TEXT("冲刺中"),TEXT("Dashing")):DWCombatHUD::ReasonText(this,S.DashReason);
    if(!S.bDashing&&S.DashCooldown>0.)Dash=FText::Format(DWText(this,TEXT("冲刺冷却 {0} 秒"),TEXT("Dash cooldown {0}s")),FText::FromString(FString::Printf(TEXT("%.1f"),S.DashCooldown)));
    Row(3,FText::Format(FText::FromString(TEXT("[{0}] {1}")),DashKey,Dash),White);
    Row(4,FText::Format(FText::FromString(TEXT("[{0}] {1}")),RootKey,S.bRooted?DWText(this,TEXT("扎根中 · 再按解除"),TEXT("Rooted · press to release")):DWCombatHUD::ReasonText(this,S.RootReason)),White);
    Row(5,FText::Format(DWText(this,TEXT("圈雷 {0} · 下圈冷却 {1} 秒"),TEXT("Mines {0} · next ring cooldown {1}s")),S.LiveMines,FText::FromString(FString::Printf(TEXT("%.1f"),S.MineCooldown))),S.MineCooldown>0.?Gold:White);
    Row(6,S.AmmoReason!=EDWCombatHUDReason::None?DWCombatHUD::ReasonText(this,S.AmmoReason):S.EquipmentId==TEXT("DWWeapon_DualDaggers")?
        DWText(this,TEXT("警戒圈：绿＝宽限；黄＝未交战；红＝交战"),TEXT("Alert: green = grace; gold = unaware; red = engaged")):
        DWText(this,TEXT("特殊值与形态时间分别结算"),TEXT("Special and form time are separate")),S.AmmoReason!=EDWCombatHUDReason::None?Gold:White);
    return Base+6;
}
