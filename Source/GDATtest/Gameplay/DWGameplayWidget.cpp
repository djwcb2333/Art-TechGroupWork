#include "DWGameplayWidget.h"
#include "DWC01UIStyle.h"
#include "DWHandbookButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Framework/Application/SlateApplication.h"
#include "DWSettingsPanel.h"
#include "DWUserSettings.h"
#include "DWLoadingTransition.h"
#include "DWUIEntryWidgets.h"
#include "DWProgressRing.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWAudioLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/UniformGridPanel.h"
#include "Components/VerticalBox.h"
#include "Components/CanvasPanel.h"
#include "Components/WidgetSwitcher.h"
#include "Components/Slider.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/BorderSlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Components/ComboBoxString.h"
#include "Components/CheckBox.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "AudioDevice.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformTime.h"

namespace
{
    void DWSetText(UTextBlock* Widget,const FString& Text){if(Widget)Widget->SetText(FText::FromString(Text));}

    FText DWActionLabel(const UDWGameplayWidget* Widget,EDWInputAction Action)
    {
        const auto* Controller=Widget?Cast<ADWPlayerController>(Widget->GetOwningPlayer()):nullptr;
        return Controller?Controller->GetActionKeyLabel(Action):FText::FromString(TEXT("--"));
    }
    void DWSetNamedText(UDWGameplayWidget* Widget,const FName Name,const FText& Text)
    {
        if(auto* Label=Widget?Cast<UTextBlock>(Widget->GetWidgetFromName(Name)):nullptr)
            if(!Label->GetText().EqualTo(Text))Label->SetText(Text);
    }
    // These labels are data-driven at runtime. Keeping the Designer source labels
    // intact preserves normal editing and translation of all other text.
    void DWRefreshInputLabels(UDWGameplayWidget* Widget)
    {
        if(!Widget)return;
        const auto* Player=Widget->GetPlayer();
        const FText Transform=DWActionLabel(Widget,EDWInputAction::Transform);
        DWSetNamedText(Widget,TEXT("FormText"),Player&&Player->IsYeastForm()
            ?DWText(Widget,TEXT("酵母形态 · 耗尽后还原"),TEXT("Yeast form - reverts at zero"))
            :DWText(Widget,TEXT("面团形态"),TEXT("Dough form")));
        const auto* Config=Widget->GetGameplayConfig();
        DWSetNamedText(Widget,TEXT("CraftingStatusText"),Config&&Config->bRequireTransformationForCrafting
            ?(Player&&Player->IsYeastForm()?DWText(Widget,TEXT("酵母形态 · 可以制作"),TEXT("Yeast form - Ready to craft"))
                :FText::Format(DWText(Widget,TEXT("部分配方需要变身：变身值满后按 {0}"),TEXT("Some recipes need Yeast form: at full meter press {0}")),Transform))
            :DWText(Widget,TEXT("无需变身 · 材料齐全即可制作"),TEXT("No transformation needed - craft when materials are ready")));
        DWSetNamedText(Widget,TEXT("Inventory_Description"),FText::Format(
            DWText(Widget,TEXT("点击物品查看用途与使用。按 {0} 返回游戏。"),TEXT("Select an item to inspect or use it. Press {0} to return to the game.")),
            DWActionLabel(Widget,EDWInputAction::Inventory)));
        DWSetNamedText(Widget,TEXT("Crafting_Description"),FText::Format(
            DWText(Widget,TEXT("左侧背包，右侧配方。按 {0} 返回游戏。"),TEXT("Inventory on the left, recipes on the right. Press {0} to return to the game.")),
            DWActionLabel(Widget,EDWInputAction::Crafting)));

        // Retain the authored widgets and offscreen animation bindings, but hide permanent instructions.
        if(UWidget* Hint=Widget->GetWidgetFromName(TEXT("ControlsCard")))Hint->SetVisibility(ESlateVisibility::Collapsed);
        if(UWidget* Hint=Widget->GetWidgetFromName(TEXT("ControlsHint")))Hint->SetVisibility(ESlateVisibility::Collapsed);

    }
    bool DWHandleMenuKey(ADWGameplayHUD* HUD,ADWPlayerController* Controller,const FKey& Key,bool bRepeat)
    {
        if(!HUD||!Controller||!Key.IsValid())return false;
        if(ADWGameplayHUD::IsHandbookPage(HUD->GetMenuPage()))
        {
            const auto Focus=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetKeyboardFocusedWidget():TSharedPtr<SWidget>();
            if(Focus.IsValid()&&(Focus->GetTypeAsString().Contains(TEXT("EditableText"))||Focus->GetTypeAsString().Contains(TEXT("SpinBox"))))return false;
            if(Key==Controller->GetAppliedActionKey(EDWInputAction::PreviousBookTab)){if(!bRepeat)HUD->CycleHandbook(-1);return true;}
            if(Key==Controller->GetAppliedActionKey(EDWInputAction::NextBookTab)){if(!bRepeat)HUD->CycleHandbook(1);return true;}
        }
        const EDWInputAction BookActions[]={EDWInputAction::Character,EDWInputAction::Building,EDWInputAction::LanguageJournal};
        const EDWMenuPage BookPages[]={EDWMenuPage::Character,EDWMenuPage::Building,EDWMenuPage::LanguageJournal};
        for(int32 I=0;I<3;++I)if(HUD->IsSessionStarted()&&Key==Controller->GetAppliedActionKey(BookActions[I])){if(!bRepeat)HUD->ToggleHandbook(BookPages[I]);return true;}
        if(Key==Controller->GetAppliedActionKey(EDWInputAction::PauseMenu)||Key==Controller->GetAppliedActionKey(EDWInputAction::PauseAlternate))
        {if(!bRepeat)HUD->TogglePause();return true;}
        if(HUD->IsSessionStarted()&&Key==Controller->GetAppliedActionKey(EDWInputAction::Inventory))
        {if(!bRepeat)HUD->ToggleInventory();return true;}
        if(HUD->IsSessionStarted()&&Key==Controller->GetAppliedActionKey(EDWInputAction::Crafting))
        {if(!bRepeat)HUD->ToggleCrafting();return true;}
        return false;
    }
}

bool UDWGameplayWidget::FindTextOverride(const UTextBlock* Text,EDWGameLanguage Language,FText& OutText) const
{
    if(!Text)return false;
    for(const FDWWidgetTextOverride& Entry:TextOverrides)
        if(!Entry.WidgetName.IsNone()&&Entry.WidgetName==Text->GetFName())
        {
            OutText=Language==EDWGameLanguage::English?Entry.English:Entry.Chinese;
            return true;
        }
    return false;
}

void UDWGameplayWidget::ApplyDesignerTextPreview()
{
    if(!IsDesignTime()||!bPreviewLanguageInDesigner||!WidgetTree)return;
    WidgetTree->ForEachWidget([this](UWidget* W)
    {
        if(auto* Text=Cast<UTextBlock>(W))
        {
            FText Copy;
            if(FindTextOverride(Text,DesignerPreviewLanguage,Copy))Text->SetText(Copy);
        }
    });
}

void UDWGameplayWidget::NativePreConstruct()
{
    Super::NativePreConstruct();
    ApplyDesignerTextPreview();
    ApplySliderPresentation(this);
}

void UDWGameplayWidget::RefreshTextPresentation()
{
    if(IsDesignTime()){ApplyDesignerTextPreview();return;}
    ApplyWidgetPresentation(this);
    if(HUD)ApplyArtwork();
}

void UDWGameplayWidget::NativeConstruct()
{
    Super::NativeConstruct();SetIsFocusable(true);
    if(!HUD&&GetOwningPlayer())HUD=Cast<ADWGameplayHUD>(GetOwningPlayer()->GetHUD());
#define DW_BIND(Button,Handler) if(Button){Button->OnClicked.RemoveDynamic(this,&UDWGameplayWidget::Handler);if(bBindDefaultButtonActions)Button->OnClicked.AddDynamic(this,&UDWGameplayWidget::Handler);}
    DW_BIND(HUDPauseButton,ClickPause) DW_BIND(PauseQuitButton,ClickPauseQuit)
    DW_BIND(StartButton,ClickStart) DW_BIND(TitleSettingsButton,ClickSettings) DW_BIND(QuitButton,ClickQuit)
    DW_BIND(SlotsBackButton,ClickSlotsBack) DW_BIND(InventoryUseButton,ClickUse) DW_BIND(CraftingUseButton,ClickUse)
    DW_BIND(InventoryCloseButton,ClickClose) DW_BIND(CraftingCloseButton,ClickClose) DW_BIND(ContinueButton,ClickClose)
    DW_BIND(SaveButton,ClickSave) DW_BIND(PauseSettingsButton,ClickSettings) DW_BIND(SaveAndTitleButton,ClickSaveTitle)
    DW_BIND(NoSaveTitleButton,ClickNoSaveTitle) DW_BIND(DeathTitleButton,ClickNoSaveTitle) DW_BIND(DeathLoadButton,ClickDeathLoad)
    DW_BIND(ApplySettingsButton,ClickApplySettings) DW_BIND(SettingsBackButton,ClickSettingsBack)
#undef DW_BIND
    if(VolumeSlider){VolumeSlider->OnValueChanged.RemoveDynamic(this,&UDWGameplayWidget::ChangeVolume);VolumeSlider->OnValueChanged.AddDynamic(this,&UDWGameplayWidget::ChangeVolume);}
    if(VolumeSlider)
    {
        VolumeSlider->OnMouseCaptureBegin.RemoveDynamic(this,&UDWGameplayWidget::BeginSettingsInteraction);VolumeSlider->OnMouseCaptureBegin.AddDynamic(this,&UDWGameplayWidget::BeginSettingsInteraction);
        VolumeSlider->OnControllerCaptureBegin.RemoveDynamic(this,&UDWGameplayWidget::BeginSettingsInteraction);VolumeSlider->OnControllerCaptureBegin.AddDynamic(this,&UDWGameplayWidget::BeginSettingsInteraction);
    }
    if(VSyncCheck){VSyncCheck->OnCheckStateChanged.RemoveDynamic(this,&UDWGameplayWidget::ChangeVSync);VSyncCheck->OnCheckStateChanged.AddDynamic(this,&UDWGameplayWidget::ChangeVSync);}
    if(LanguageCombo){LanguageCombo->OnSelectionChanged.RemoveDynamic(this,&UDWGameplayWidget::ChangeLanguage);LanguageCombo->OnSelectionChanged.AddDynamic(this,&UDWGameplayWidget::ChangeLanguage);}
    for(UComboBoxString* Combo:{ResolutionCombo.Get(),WindowModeCombo.Get(),QualityCombo.Get(),LanguageCombo.Get()})
        if(Combo)
        {
            Combo->OnGenerateWidgetEvent.BindDynamic(this,&UDWGameplayWidget::GenerateComboOption);
            Combo->OnOpening.RemoveDynamic(this,&UDWGameplayWidget::BeginSettingsInteraction);Combo->OnOpening.AddDynamic(this,&UDWGameplayWidget::BeginSettingsInteraction);
        }
    for(UComboBoxString* Combo:{ResolutionCombo.Get(),WindowModeCombo.Get(),QualityCombo.Get()})
        if(Combo){Combo->OnSelectionChanged.RemoveDynamic(this,&UDWGameplayWidget::ChangeSettingsOption);Combo->OnSelectionChanged.AddDynamic(this,&UDWGameplayWidget::ChangeSettingsOption);}
    if(ExtendedSettings)ExtendedSettings->InitializePanel(this);
    InitializeSettingsBook();
    InitializeCombatSettings();
    InitializeHandbook();
    InitializeC01Display();
    if(auto* GI=GetGameInstance())if(auto* Settings=GI->GetSubsystem<UDWUserSettings>())MasterVolume=Settings->GetMasterVolume();
    RefreshSettingsLabels();
    ApplyArtwork();
    if(HUD)ApplyMenuPage(HUD->GetMenuPage());
    RefreshFromHUD(true);
    RefreshInventoryInteractionUI();
}
void UDWGameplayWidget::InitializeScreen(ADWGameplayHUD* InHUD){HUD=InHUD;ApplyArtwork();if(HUD)ApplyMenuPage(HUD->GetMenuPage());RefreshFromHUD(true);}
void UDWGameplayWidget::NativeDestruct()
{
    CancelInventoryDiscard();
    bInventoryUIInitialized=false;
    FinishPageTransition();
    CurrentPage=EDWMenuPage::None;
    Super::NativeDestruct();
}
void UDWGameplayWidget::NativeTick(const FGeometry& Geometry,float Dt)
{
    Super::NativeTick(Geometry,Dt);
    RefreshFromHUD(false);
    InitializeC01Display();RefreshC01Presentation(Dt);TickC01QuickDock();TickC01Feedback();
    RefreshInventoryInteractionUI();
    if(ADWGameplayHUD::IsHandbookPage(CurrentPage))RefreshHandbook();
    SliderStyleRefreshSeconds+=Dt;
    if(SliderStyleRefreshSeconds>=.5f){ApplySliderPresentation(this);SliderStyleRefreshSeconds=0.f;}
    TickPageTransition(FPlatformTime::Seconds()); // Menu pages may pause world time.
    TickC01Buttons(); // Last: text, translation and book refresh have all settled for this frame.
}

bool UDWGameplayWidget::ShouldAnimatePageEntrance(EDWMenuPage Page) const
{
    return Page!=EDWMenuPage::None;
}

void UDWGameplayWidget::FinishPageTransition()
{
    UWidget* Previous=PageTransitionWidget.Get();
    PageTransitionWidget=nullptr;
    if(IsValid(Previous))
    {
        Previous->SetRenderTransform(PageTransitionBaseTransform);
        Previous->SetRenderOpacity(PageTransitionBaseOpacity);
    }
}

void UDWGameplayWidget::PlayCurrentPageEntrance()
{
    // Restore first even when replaying the same page halfway through an entrance.
    // Never capture the partially animated scale/opacity as the next baseline.
    FinishPageTransition();
    if(IsDesignTime()||!bAnimatePageTransitions||!PageSwitcher||
        CurrentPage==EDWMenuPage::None||!ShouldAnimatePageEntrance(CurrentPage)||
        PageEnterSeconds<=KINDA_SMALL_NUMBER)return;
    UWidget* Page=PageSwitcher->GetActiveWidget();
    if(!IsValid(Page))return;
    PageTransitionWidget=Page;
    PageTransitionBaseTransform=Page->GetRenderTransform();
    PageTransitionBaseOpacity=Page->GetRenderOpacity();
    PageTransitionDuration=FMath::Max(KINDA_SMALL_NUMBER,PageEnterSeconds);
    PageTransitionStartScale=FMath::Clamp(PageEnterStartScale,.01f,2.f);
    PageTransitionOffsetY=PageEnterOffsetY;
    PageTransitionOvershoot=FMath::Clamp(PageEnterOvershoot,1.f,1.25f);
    PageTransitionStartedAt=FPlatformTime::Seconds();
    TickPageTransition(PageTransitionStartedAt);
}

void UDWGameplayWidget::TickPageTransition(double Now)
{
    if(!PageTransitionWidget)return;
    if(IsDesignTime()||!bAnimatePageTransitions||!PageSwitcher||
        PageSwitcher->GetActiveWidget()!=PageTransitionWidget.Get()||
        !ShouldAnimatePageEntrance(CurrentPage))
    {
        FinishPageTransition();
        return;
    }
    const float Alpha=FMath::Clamp(static_cast<float>((Now-PageTransitionStartedAt)/PageTransitionDuration),0.f,1.f);
    if(Alpha>=1.f)
    {
        FinishPageTransition();
        return;
    }
    const auto Smooth=[](float Value){return Value*Value*(3.f-2.f*Value);};
    constexpr float PeakAt=.66f;
    float Scale=1.f;
    float Offset=0.f;
    // Two smooth segments give a small configurable overshoot and an exact settled end.
    const float ReboundOffset=PageTransitionOvershoot>1.f+KINDA_SMALL_NUMBER ? -.12f*PageTransitionOffsetY : 0.f;
    if(Alpha<PeakAt)
    {
        const float Progress=Smooth(Alpha/PeakAt);
        Scale=FMath::Lerp(PageTransitionStartScale,PageTransitionOvershoot,Progress);
        Offset=FMath::Lerp(PageTransitionOffsetY,ReboundOffset,Progress);
    }
    else
    {
        const float Progress=Smooth((Alpha-PeakAt)/(1.f-PeakAt));
        Scale=FMath::Lerp(PageTransitionOvershoot,1.f,Progress);
        Offset=FMath::Lerp(ReboundOffset,0.f,Progress);
    }
    FWidgetTransform Transform=PageTransitionBaseTransform;
    Transform.Scale*=Scale;
    Transform.Translation.Y+=Offset;
    PageTransitionWidget->SetRenderTransform(Transform);
    PageTransitionWidget->SetRenderOpacity(PageTransitionBaseOpacity*Smooth(FMath::Min(Alpha/PeakAt,1.f)));
}
ADWPlayerCharacter* UDWGameplayWidget::GetPlayer()const{return GetOwningPlayer()?Cast<ADWPlayerCharacter>(GetOwningPlayer()->GetPawn()):nullptr;}
UDWGameInstance* UDWGameplayWidget::GetDWGameInstance()const{return Cast<UDWGameInstance>(GetGameInstance());}
UDWGameplayConfig* UDWGameplayWidget::GetGameplayConfig()const{if(GetPlayer())return GetPlayer()->GetGameplayConfig();return GetDWGameInstance()?GetDWGameInstance()->GetConfig():nullptr;}
UDWInventoryComponent* UDWGameplayWidget::GetInventory()const{return GetPlayer()?GetPlayer()->GetInventory():nullptr;}
void UDWGameplayWidget::PlayClick(){if(HUD)HUD->PlayClickSound();}
void UDWGameplayWidget::SetToast(const FString& Message){if(HUD)HUD->Notify(FText::FromString(Message));}
void UDWGameplayWidget::SaveError(const FString& Fallback){UDWAudioLibrary::PlayEvent(this,GetGameplayConfig(),EDWAudioEvent::UIFailed);if(HUD&&GetDWGameInstance()&&!GetDWGameInstance()->LastSaveError.IsEmpty())HUD->Notify(GetDWGameInstance()->LastSaveError);else SetToast(Fallback);}

void UDWGameplayWidget::ApplyArtwork()
{
    if(!HUD)return;
    if(TitleText)TitleText->SetText(UDWLocalizationLibrary::TranslateLabel(this,HUD->GameTitle));
    if(SubtitleText)SubtitleText->SetText(UDWLocalizationLibrary::TranslateLabel(this,HUD->GameSubtitle));
    if(TitleBackgroundImage&&HUD->TitleBackgroundTexture){TitleBackgroundImage->SetBrushFromTexture(HUD->TitleBackgroundTexture);TitleBackgroundImage->SetVisibility(ESlateVisibility::HitTestInvisible);}
    if(InventoryCard&&HUD->InventoryPanelTexture){InventoryCard->SetBrushFromTexture(HUD->InventoryPanelTexture);InventoryCard->SetBrushColor(FLinearColor::White);}
    if(CraftingCard&&HUD->CraftingPanelTexture){CraftingCard->SetBrushFromTexture(HUD->CraftingPanelTexture);CraftingCard->SetBrushColor(FLinearColor::White);}
    if(HealthBar&&HUD->HealthFrameTexture){FProgressBarStyle Style=HealthBar->GetWidgetStyle();Style.BackgroundImage.SetResourceObject(HUD->HealthFrameTexture);Style.BackgroundImage.DrawAs=ESlateBrushDrawType::Image;HealthBar->SetWidgetStyle(Style);}
    if(TransformationBar&&HUD->TransformationFrameTexture){FProgressBarStyle Style=TransformationBar->GetWidgetStyle();Style.BackgroundImage.SetResourceObject(HUD->TransformationFrameTexture);Style.BackgroundImage.DrawAs=ESlateBrushDrawType::Image;TransformationBar->SetWidgetStyle(Style);}
    ApplyWidgetPresentation(this);
    // Explicit HUD title overrides are also authored source text, never reverse translated.
    for(UTextBlock* Text:{TitleText.Get(),SubtitleText.Get()})
    {
        if(!Text)continue;
        FText Copy;
        if(FindTextOverride(Text,UDWLocalizationLibrary::GetLanguage(this),Copy))Text->SetText(Copy);
        else
        {
            const FText Source=Text==TitleText.Get()?HUD->GameTitle:HUD->GameSubtitle;
            AuthoredLabels.FindOrAdd(TWeakObjectPtr<UTextBlock>(Text))=Source;
            Text->SetText(UDWLocalizationLibrary::TranslateLabel(this,Source));
        }
    }
}

// Preserve authored WBP nodes and callbacks; the only wrapper is created before Slate/Bounce.
void UDWGameplayWidget::PrepareSavePageLayout()
{
    if(!WidgetTree)return;
    if(auto* Page=Cast<UBorder>(GetWidgetFromName(TEXT("SaveSlotsPage"))))
    {
        Page->SetPadding(FMargin(44,32));
        if(Page->GetContent())if(auto* LayoutSlot=Cast<UBorderSlot>(Page->GetContent()->Slot))LayoutSlot->SetPadding(FMargin(44,32));
    }
    if(auto* Box=Cast<USizeBox>(GetWidgetFromName(TEXT("SaveSlotsPage_Size"))))
    {
        Box->SetWidthOverride(1020);Box->ClearMaxDesiredHeight();
        // This page always shows all three slots; keep the authored ScrollBox dormant.
        if(auto* Body=GetWidgetFromName(TEXT("SaveSlotsPage_Body"));Body&&Body->GetParent()!=Box)
        {Body->RemoveFromParent();Box->SetContent(Body);}
        // Fit the complete page in a smaller viewport, never scroll or fold its cards.
        if(auto* Page=Cast<UBorder>(Box->GetParent()))
        {
            Box->RemoveFromParent();
            auto* Fit=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("C05SaveFit"));
            Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
            Fit->SetContent(Box);Page->SetContent(Fit);
            if(auto* LayoutSlot=Cast<UBorderSlot>(Fit->Slot))LayoutSlot->SetPadding(FMargin(44,32));
        }
    }
    if(auto* Description=GetWidgetFromName(TEXT("Slots_Description")))Description->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* Heading=Cast<UTextBlock>(GetWidgetFromName(TEXT("Slots_Heading"))))
    {
        Heading->SetJustification(ETextJustify::Center);Heading->SetAutoWrapText(false);
        if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Heading->Slot)){LayoutSlot->SetHorizontalAlignment(HAlign_Fill);LayoutSlot->SetPadding(FMargin(0,0,0,20));}
    }
    if(auto* List=GetWidgetFromName(TEXT("SaveSlotList")))
        if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(List->Slot))LayoutSlot->SetPadding(FMargin(0));
    if(auto* Back=Cast<UButton>(GetWidgetFromName(TEXT("SlotsBackButton"))))
    {
        auto* Box=Cast<USizeBox>(GetWidgetFromName(TEXT("C05SlotsBackSize")));
        if(!Box)if(auto* Body=Cast<UVerticalBox>(Back->GetParent()))
        {
            Back->RemoveFromParent();Box=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("C05SlotsBackSize"));
            Box->SetContent(Back);auto* LayoutSlot=Body->AddChildToVerticalBox(Box);LayoutSlot->SetPadding(FMargin(0,20,0,0));
        }
        if(Box)Box->SetHeightOverride(56);
    }
}

void UDWGameplayWidget::ApplyWidgetPresentation(UUserWidget* Target)
{
    ApplySliderPresentation(Target);
    if(!Target||!Target->WidgetTree)return;
    Target->WidgetTree->ForEachWidget([this,Target](UWidget* W)
    {
        // Style only the known legacy pages/cards. Runtime book/settings/frontend retain
        // their explicit state colors, backgrounds and authored hierarchy.
        bool bLegacyC01Text=false;
        if(bUseC01HUD)
        {
            for(UWidget* Parent=W;Parent;Parent=Parent->GetParent())
            {
                const FName Name=Parent->GetFName();
                if(Name==TEXT("PausePage")||Name==TEXT("DefeatPage")||Name==TEXT("SaveSlotsPage")||Name==TEXT("SaveSlotCard"))
                {bLegacyC01Text=true;break;}
            }
            if(auto* Border=Cast<UBorder>(W))
            {
                const FName Name=Border->GetFName();
                if(Name==TEXT("PausePage")||Name==TEXT("DefeatPage")||Name==TEXT("SaveSlotsPage"))
                {
                    Border->SetBrush(DWC01UI::Texture(TEXT("T_DW_C01_Menu_Paper_1160x650"),FVector2D(1160,650)));
                    Border->SetBrushColor(FLinearColor::White);
                }
                // SaveSlotCard owns its regular rounded brush and hover tint in UDWSaveSlotWidget.
            }
            // A default only: never overwrite explicit tab selection, danger or
            // transparent-rest registration already supplied by the new UI.
            if(auto* Button=Cast<UButton>(W);Button&&!C01Buttons.Contains(Button))
            {
                const FName Name=Button->GetFName();
                const bool Danger=Name==TEXT("DeleteButton")||Name==TEXT("QuitButton")||
                    Name==TEXT("NoSaveTitleButton")||Name==TEXT("DeathTitleButton")||
                    Name==TEXT("PauseQuitButton")||Name==TEXT("InventoryDiscardConfirmButton");
                // A bag cell is already constructed here. Register its color-only role
                // before the generic presentation pass can attempt a button spring.
                const bool ItemSlot=Cast<UDWInventorySlotWidget>(Target)&&Name==TEXT("SlotButton");
                StyleC01Button(Button,Danger?DWC01UI::Danger():DWC01UI::Paper(),false,false,ItemSlot);
                if(Name==TEXT("SlotsBackButton"))
                {
                    StyleC01Button(Button,FMath::Lerp(DWC01UI::Paper(),FLinearColor::White,.50f));
                    auto Design=Button->GetStyle();Design.Normal=Design.Hovered=Design.Pressed=Design.Disabled=FSlateRoundedBoxBrush(FLinearColor::White,12.f);Button->SetStyle(Design);
                }
            }
        }
        if(auto* T=Cast<UTextBlock>(W))
        {
            const TWeakObjectPtr<UTextBlock> Key(T);
            if(!AuthoredLabels.Contains(Key))AuthoredLabels.Add(Key,T->GetText());
            FText Copy;
            if(Target==this&&FindTextOverride(T,UDWLocalizationLibrary::GetLanguage(this),Copy))T->SetText(Copy);
            else if(const auto* BookLabel=HandbookLabels.Find(Key))T->SetText(DWText(this,*BookLabel->Key,*BookLabel->Value));
            else T->SetText(UDWLocalizationLibrary::TranslateLabel(this,AuthoredLabels.FindChecked(Key)));
            if(HUD&&HUD->GameUIFont){FSlateFontInfo Font=T->GetFont();Font.FontObject=HUD->GameUIFont;T->SetFont(Font);}
            if(bLegacyC01Text)
            {
                FLinearColor TextColor=DWC01UI::Ink();
                for(UWidget* Parent=T->GetParent();Parent;Parent=Parent->GetParent())
                {
                    if(auto* Button=Cast<UButton>(Parent))
                    {
                        if(const auto* Visual=C01Buttons.Find(Button);Visual&&!Visual->bSelected&&Visual->Base.Equals(DWC01UI::Danger(),.0001f))TextColor=FLinearColor::White;
                        break;
                    }
                }
                T->SetColorAndOpacity(FSlateColor(TextColor));
                FSlateFontInfo Font=T->GetFont();Font.OutlineSettings.OutlineSize=0;T->SetFont(Font);
                T->SetShadowColorAndOpacity(FLinearColor::Transparent);T->SetShadowOffset(FVector2D::ZeroVector);
            }
        }
    });
}

UWidget* UDWGameplayWidget::GenerateComboOption(FString Option)
{
    auto* Text=NewObject<UTextBlock>(this);
    Text->SetText(FText::FromString(Option));
    FSlateFontInfo Font=Text->GetFont();Font.Size=18;if(HUD&&HUD->GameUIFont)Font.FontObject=HUD->GameUIFont;
    Text->SetFont(Font);Text->SetColorAndOpacity(FSlateColor(bUseC01HUD?DWC01UI::Ink():FLinearColor(.12f,.07f,.035f,1.f)));
    if(!bUseC01HUD)return Text;
    Text->SetAutoWrapText(false);Text->SetJustification(ETextJustify::Center);
    bool Latin=false,CJK=false;for(TCHAR C:Option){Latin|=(C>=TEXT('A')&&C<=TEXT('Z'))||(C>=TEXT('a')&&C<=TEXT('z'));CJK|=C>=0x2e80;}
    Text->SetRenderTranslation(FVector2D(0,Latin&&!CJK?-Font.Size*C01ButtonLatinOpticalOffsetEm:0));
    auto* Center=NewObject<UBorder>(this);Center->SetBrush(FSlateNoResource());Center->SetPadding(FMargin(0,2));
    Center->SetHorizontalAlignment(HAlign_Fill);Center->SetVerticalAlignment(VAlign_Center);Center->SetContent(Text);return Center;
}

void UDWGameplayWidget::ApplyMenuPage(EDWMenuPage Page)
{
    const bool bPageChanged=CurrentPage!=Page;
    const bool bBookSwitch=bPageChanged&&ADWGameplayHUD::IsHandbookPage(CurrentPage)&&ADWGameplayHUD::IsHandbookPage(Page);
    if(bPageChanged||Page==EDWMenuPage::None)FinishPageTransition();
    if(Page!=CurrentPage)CancelInventoryDiscard();
    CurrentPage=Page;
    if(ExtendedSettings){if(Page!=EDWMenuPage::Settings)ExtendedSettings->CancelCapture();else ExtendedSettings->RefreshPanel();}
    if(MenuRoot)MenuRoot->SetVisibility(Page==EDWMenuPage::None?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    // Designer PageSwitcher order: title, slots, inventory, crafting, pause, settings, defeat.
    int32 Index=0;
    switch(Page){case EDWMenuPage::SaveSlots:Index=1;break;case EDWMenuPage::Inventory:Index=2;break;case EDWMenuPage::Crafting:Index=3;break;case EDWMenuPage::Pause:Index=4;break;case EDWMenuPage::Settings:Index=5;break;case EDWMenuPage::Defeat:Index=6;break;default:break;}
    if(ADWGameplayHUD::IsHandbookPage(Page)&&HandbookPageIndex!=INDEX_NONE)Index=HandbookPageIndex;
    if(PageSwitcher&&PageSwitcher->GetChildrenCount()>Index)PageSwitcher->SetActiveWidgetIndex(Index);
    if(Page==EDWMenuPage::Settings){LoadSettings();RefreshSettingsBook();}
    if(Page==EDWMenuPage::SaveSlots)RefreshSaves();
    if(Page==EDWMenuPage::Inventory||Page==EDWMenuPage::Crafting){RefreshInventory();RefreshRecipes();}
    const bool bSlot=GetDWGameInstance()&&GetDWGameInstance()->HasActiveSlot();
    if(SaveButton)SaveButton->SetIsEnabled(bSlot);
    if(SaveAndTitleButton)SaveAndTitleButton->SetIsEnabled(bSlot);
    if(DeathLoadButton)
    {
        DeathLoadButton->SetIsEnabled(GetPlayer()&&GetPlayer()->CanCombatRespawn());
        if(auto* Label=Cast<UTextBlock>(DeathLoadButton->GetChildAt(0)))
            Label->SetText(DWText(this,TEXT("返回安全点"),TEXT("Return to safe point")));
    }
    if(ADWGameplayHUD::IsHandbookPage(Page))RefreshHandbook();
    OnMenuPageChanged(Page);
    // Blueprint notifications may redirect to another page. Animate only the page
    // that actually remains current, and never replay merely because a list refreshed.
    if(bPageChanged&&CurrentPage==Page&&Page!=EDWMenuPage::None)
    {
        if(bBookSwitch){if(HandbookFrame&&bAnimatePageTransitions)HandbookFrame->Pulse();}
        else PlayCurrentPageEntrance();
    }
}

void UDWGameplayWidget::RefreshFromHUD(bool bForceLists)
{
    if(!HUD)return;
    RefreshCombatHUD();
    const int32 NewRevision=UDWLocalizationLibrary::GetRevision(this);
    if(LanguageRevision!=NewRevision)
    {
        LanguageRevision=NewRevision;
        for(auto It=AuthoredLabels.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
        ApplyArtwork();RefreshSettingsLabels();RefreshInventory();RefreshRecipes();RefreshSaves();
        if(ExtendedSettings)ExtendedSettings->RefreshPanel();
        OnLanguageChanged(UDWLocalizationLibrary::GetLanguage(this));
    }
    const auto* P=GetPlayer();const auto* C=GetGameplayConfig();
    const float HP=P?P->GetHealth():0.f,MaxHP=CombatHUD.bReady?static_cast<float>(CombatHUD.Health.Maximum):(C?C->MaxHealth:100.f);
    const float H=CombatHUD.bReady?static_cast<float>(CombatHUD.Health.Fraction):FMath::Clamp(HP/FMath::Max(1.f,MaxHP),0.f,1.f);
    const float T=CombatHUD.bReady?static_cast<float>(CombatHUD.Special.Fraction):(P&&C?FMath::Clamp(P->GetTransformation()/FMath::Max(1.f,C->MaxTransformation),0.f,1.f):0.f);
    const float Sprint=CombatHUD.bReady
        ?(CombatHUD.Form==EDWCombatBaseForm::Normal?0.f:static_cast<float>(FMath::Clamp(CombatHUD.FormRemaining/CombatHUDConfig.Player.FormDurationSeconds,0.,1.)))
        :(P?P->GetSprintAlcoholProgress():0.f);
    if(HUDLayer)HUDLayer->SetVisibility(HUD->IsSessionStarted()?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    if(HUDPauseButton)HUDPauseButton->SetVisibility(HUD->IsSessionStarted()&&HUD->GetMenuPage()==EDWMenuPage::None?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    DWSetNamedText(this,TEXT("HUDPauseButton_Label"),DWText(this,TEXT("暂停"),TEXT("Pause")));
    if(auto* L=Cast<UTextBlock>(GetWidgetFromName(TEXT("HUDPauseButton_Label"))))L->SetAutoWrapText(false);
    DWSetNamedText(this,TEXT("PauseQuitButton_Label"),DWText(this,TEXT("保存并退出游戏"),TEXT("Save & quit game")));
    if(HealthBar)HealthBar->SetPercent(H);if(TransformationBar)TransformationBar->SetPercent(T);if(SprintRing)SprintRing->SetFraction(Sprint);
    if(HealthValueText)HealthValueText->SetText(FText::Format(DWText(this,TEXT("生命  {0} / {1}"),TEXT("Health  {0} / {1}")),FMath::RoundToInt(HP),FMath::RoundToInt(MaxHP)));
    if(TransformationValueText)TransformationValueText->SetText(CombatHUD.bReady
        ?FText::Format(DWText(this,TEXT("特殊  {0} / {1}"),TEXT("Special  {0} / {1}")),FText::FromString(FString::Printf(TEXT("%.1f"),CombatHUD.Special.Value)),FMath::RoundToInt(CombatHUD.Special.Maximum))
        :FText::Format(DWText(this,TEXT("变身  {0}%"),TEXT("Transform  {0}%")),FMath::RoundToInt(T*100.f)));
    DWRefreshInputLabels(this);
    if(CombatHUD.bReady&&FormText)FormText->SetText(DWCombatHUD::FormLabel(this,CombatHUD.Form));
    // The existing Designer label predates safe-point recovery; update only its runtime copy.
    DWSetNamedText(this,TEXT("Defeat_Description"),DWText(this,
        TEXT("返回已激活的安全点复活，再找回死亡袋中的掉落物品。"),
        TEXT("Respawn at your activated safe point, then recover dropped items from your death bag.")));
    DWSetText(RingValueText,CombatHUD.bReady?(CombatHUD.Form==EDWCombatBaseForm::Normal?TEXT("—"):FString::Printf(TEXT("%.1fs"),CombatHUD.FormRemaining)):FString::Printf(TEXT("%.0f%%"),Sprint*100.f));
    if(AlcoholCountText)AlcoholCountText->SetText(FText::Format(DWText(this,TEXT("酒精  × {0}"),TEXT("Alcohol  x {0}")),GetInventory()?GetInventory()->CountItem(TEXT("Alcohol")):0));
    const FText Prompt=HUD->bUseLegacyInteractionPrompt&&P?P->GetInteractionPrompt():FText();
    if(InteractionText)InteractionText->SetText(Prompt);
    if(InteractionContainer)InteractionContainer->SetVisibility(Prompt.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    const FText Toast=HUD->GetToast();if(ToastText)ToastText->SetText(Toast);
    if(ToastContainer)ToastContainer->SetVisibility(Toast.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    uint32 Hash=P?GetTypeHash(P->IsYeastForm()):0;
    if(GetInventory())for(const auto& Item:GetInventory()->GetSlots())Hash=HashCombine(Hash,HashCombine(GetTypeHash(Item.ItemId),GetTypeHash(Item.Quantity)));
    if(bForceLists||InventoryHash!=Hash)
    {
        InventoryHash=Hash;
        if(CurrentPage==EDWMenuPage::Inventory||CurrentPage==EDWMenuPage::Crafting){RefreshInventory();RefreshRecipes();}
    }
    OnHUDValuesChanged(H,T,P&&P->IsYeastForm(),Sprint);
}

void UDWGameplayWidget::RefreshInventory()
{
    const auto* C=GetGameplayConfig();auto* Bag=GetInventory();
    const int32 Count=C?C->MaxInventorySlots:24;
    for(UUniformGridPanel* Grid:{InventoryGrid.Get(),CraftingInventoryGrid.Get()})
    {
        if(!Grid)continue;
        if(!InventorySlotClass)continue;
        if(Grid->GetChildrenCount()!=Count){DWC01UI::ReleaseMotionTree(Grid);Grid->ClearChildren();}
        for(int32 I=0;I<Count;++I)
        {
            auto* Entry=Cast<UDWInventorySlotWidget>(Grid->GetChildAt(I));
            if(!Entry){Entry=CreateWidget<UDWInventorySlotWidget>(GetOwningPlayer(),InventorySlotClass);if(!Entry)continue;Grid->AddChildToUniformGrid(Entry,I/FMath::Max(1,InventoryColumns),I%FMath::Max(1,InventoryColumns));}
            const FDWItemStack Item=Bag&&Bag->GetSlots().IsValidIndex(I)?Bag->GetSlots()[I]:FDWItemStack();
            Entry->ApplyItemData(this,I,Item,SelectedSlot==I);
        }
    }
    if(InventoryCapacityText)InventoryCapacityText->SetText(FText::Format(DWText(this,TEXT("每槽上限 {0} · 共 {1} 槽"),TEXT("Stack limit {0} - {1} slots")),C?C->MaxStackSize:40,Count));RefreshSelection();
}
void UDWGameplayWidget::RefreshSelection()
{
    const auto* Bag=GetInventory();const auto* C=GetGameplayConfig();
    const FDWItemStack Item=Bag&&Bag->GetSlots().IsValidIndex(SelectedSlot)?Bag->GetSlots()[SelectedSlot]:FDWItemStack();
    const auto* D=C?C->GetItemDefinition(Item.ItemId):nullptr;
    FString Description=DWText(this,TEXT("选择物品查看用途，再点击使用。"),TEXT("Select an item to see its use, then choose Use.")).ToString();
    const auto ItemRole=FDWCombatItemRegistry::GetItemRole(Item.ItemId);
    const bool bCore=ItemRole==EDWCombatItemRole::MoldCore||ItemRole==EDWCombatItemRole::YeastCore;
    FDWInventorySlotSnapshot CoreSlot;
    const bool bCanUse=bCore?(GetPlayer()&&Bag&&Bag->CaptureSlotSnapshot(SelectedSlot,CoreSlot)&&GetPlayer()->CanRequestCoreInjection(CoreSlot,true)):(D&&Item.Quantity>0&&D->bUsable);
    if(D&&Item.Quantity>0)
    {
        Description=UDWLocalizationLibrary::GetItemDisplayName(this,*D).ToString();
        if(D->HealAmount>0)Description+=FText::Format(DWText(this,TEXT(" · 恢复 {0} 生命"),TEXT(" - Restores {0} health")),FMath::RoundToInt(D->HealAmount)).ToString();
        if(D->TransformationGainPercent>0)Description+=FText::Format(DWText(this,TEXT(" · +{0}% 变身值"),TEXT(" - +{0}% transformation")),FMath::RoundToInt(D->TransformationGainPercent)).ToString();
        if(ItemRole==EDWCombatItemRole::PermanentWeapon||ItemRole==EDWCombatItemRole::ThrowableStock)Description+=DWText(this,TEXT(" · 可从背包装备"),TEXT(" - Equip from inventory")).ToString();
        else if(!D->bUsable)Description+=DWText(this,TEXT(" · 制作 / 战斗材料"),TEXT(" - Crafting / combat material")).ToString();
    }
    DWSetText(InventoryDetailsText,Description);DWSetText(CraftingDetailsText,Description);
    if(InventoryUseButton)InventoryUseButton->SetIsEnabled(bCanUse);if(CraftingUseButton)CraftingUseButton->SetIsEnabled(bCanUse);
    if(auto* Equip=Cast<UButton>(GetWidgetFromName(TEXT("HandbookInventoryEquip"))))Equip->SetIsEnabled(CanEquipInventorySlot(SelectedSlot));
}
void UDWGameplayWidget::RefreshRecipes()
{
    DWRefreshInputLabels(this);
    if(HandbookRecipeList){RefreshHandbookRecipes();return;}
    if(!RecipeList||!RecipeEntryClass||!GetGameplayConfig())return;
    const auto& Recipes=GetGameplayConfig()->Recipes;
    if(RecipeList->GetChildrenCount()!=Recipes.Num()){DWC01UI::ReleaseMotionTree(RecipeList);RecipeList->ClearChildren();}
    for(int32 I=0;I<Recipes.Num();++I){auto* Entry=Cast<UDWRecipeEntryWidget>(RecipeList->GetChildAt(I));if(!Entry){Entry=CreateWidget<UDWRecipeEntryWidget>(GetOwningPlayer(),RecipeEntryClass);if(Entry)RecipeList->AddChildToVerticalBox(Entry);}if(Entry)Entry->ApplyRecipeData(this,Recipes[I]);}
}
void UDWGameplayWidget::RefreshSaves()
{
    if(!SaveSlotList||!SaveSlotClass||!GetDWGameInstance())return;
    const auto Summaries=GetDWGameInstance()->GetSlotSummaries();
    if(SaveSlotList->GetChildrenCount()!=Summaries.Num()){DWC01UI::ReleaseMotionTree(SaveSlotList);SaveSlotList->ClearChildren();}
    for(int32 I=0;I<Summaries.Num();++I){auto* Entry=Cast<UDWSaveSlotWidget>(SaveSlotList->GetChildAt(I));if(!Entry){Entry=CreateWidget<UDWSaveSlotWidget>(GetOwningPlayer(),SaveSlotClass);if(Entry)SaveSlotList->AddChildToVerticalBox(Entry);}if(Entry){Entry->ApplySaveData(this,Summaries[I]);if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Entry->Slot)){LayoutSlot->SetHorizontalAlignment(HAlign_Fill);LayoutSlot->SetPadding(FMargin(0,0,0,I+1<Summaries.Num()?16.f:0.f));}}}
}
void UDWGameplayWidget::SelectInventorySlot(int32 Index)
{
    if(IsInventoryDiscardPending())return;
    UDWAudioLibrary::PlayEvent(this,GetGameplayConfig(),EDWAudioEvent::UISelection);
    SelectedSlot=Index;RefreshInventory();const auto* Bag=GetInventory();const FName Id=Bag&&Bag->GetSlots().IsValidIndex(Index)?Bag->GetSlots()[Index].ItemId:NAME_None;OnInventorySelectionChanged(Index,Id);
}
void UDWGameplayWidget::ClickUse()
{
    if(IsInventoryDiscardPending())return;
    PlayClick();const auto* Bag=GetInventory();const FName Id=Bag&&Bag->GetSlots().IsValidIndex(SelectedSlot)?Bag->GetSlots()[SelectedSlot].ItemId:NAME_None;
    bool Used=false;
    const auto Role=FDWCombatItemRegistry::GetItemRole(Id);
    if(Role==EDWCombatItemRole::MoldCore||Role==EDWCombatItemRole::YeastCore)
    {
        const TWeakObjectPtr<ADWPlayerCharacter> P(GetPlayer());
        auto* A=UDWCombatAuthoritySubsystem::Get(P.Get());FDWInventorySlotSnapshot CoreSlot;
        if(A&&P.IsValid()&&Bag&&Bag->CaptureSlotSnapshot(SelectedSlot,CoreSlot)&&P->CanRequestCoreInjection(CoreSlot,true))
        {
            const auto Life=A->CaptureActorLife(P.Get());const uint64 Epoch=A->GetWorldEpoch();
            const TWeakObjectPtr<UDWInventoryComponent> OriginalInventory(P->GetInventory());
            // Closing a panel can run user callbacks. The original slot/life must survive them.
            if(HUD)HUD->ClosePanels();
            A=UDWCombatAuthoritySubsystem::Get(P.Get());
            if(A&&P.IsValid()&&A->GetWorldEpoch()==Epoch&&GetPlayer()==P.Get()&&P->GetInventory()==OriginalInventory.Get())
            {
                const auto Current=A->CaptureActorLife(P.Get());
                if(Current.Entity==Life.Entity&&Current.Life==Life.Life)Used=P->RequestCoreInjection(CoreSlot);
            }
        }
    }
    else Used=GetPlayer()&&GetPlayer()->UseInventoryItem(SelectedSlot);
    UDWAudioLibrary::PlayEvent(this,GetGameplayConfig(),Used?EDWAudioEvent::ItemUseSuccess:EDWAudioEvent::ItemUseFailed);
    if(!Used)SetToast(TEXT("当前无法使用该物品。"));RefreshFromHUD(true);OnItemUseResult(Id,Used);
}
void UDWGameplayWidget::CraftRecipe(FName Id){if(IsInventoryDiscardPending())return;const bool Done=GetPlayer()&&GetPlayer()->CraftRecipe(Id);UDWAudioLibrary::PlayEvent(this,GetGameplayConfig(),Done?EDWAudioEvent::CraftSuccess:EDWAudioEvent::CraftFailed);SetToast(Done?TEXT("制作完成，物品已加入背包。"):(GetGameplayConfig()&&GetGameplayConfig()->bRequireTransformationForCrafting?TEXT("制作失败：请检查材料、形态和空间。"):TEXT("制作失败：请检查材料和背包空间。")));RefreshFromHUD(true);OnRecipeCraftResult(Id,Done);}
void UDWGameplayWidget::LoadSlot(int32 Index){if(!GetDWGameInstance()||!GetDWGameInstance()->LoadGameSlot(Index)){SaveError(TEXT("加载失败。"));return;}OnUIAction(TEXT("LoadSlot"));if(HUD&&!GetDWGameInstance()->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive()){HUD->SetSessionStarted(true);HUD->ClosePanels();}}
void UDWGameplayWidget::NewSlot(int32 Index){if(!GetDWGameInstance()||!GetDWGameInstance()->NewGame(Index,FString::Printf(TEXT("存档 %d"),Index+1))){SaveError(TEXT("新建失败。"));return;}OnUIAction(TEXT("NewSlot"));if(HUD&&!GetDWGameInstance()->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive()){HUD->SetSessionStarted(true);HUD->ClosePanels();}}
void UDWGameplayWidget::DeleteSlot(int32 Index){const bool Done=GetDWGameInstance()&&GetDWGameInstance()->DeleteGameSlot(Index);if(Done)SetToast(TEXT("存档已删除。"));else SaveError(TEXT("删除失败。"));RefreshSaves();OnUIAction(TEXT("DeleteSlot"));}

void UDWGameplayWidget::ClickStart(){PlayClick();if(HUD)HUD->ShowMenu(EDWMenuPage::SaveSlots);OnUIAction(TEXT("Start"));}
void UDWGameplayWidget::ClickSettings(){PlayClick();if(HUD)HUD->OpenSettings();OnUIAction(TEXT("Settings"));}
void UDWGameplayWidget::ClickQuit(){PlayClick();OnUIAction(TEXT("Quit"));UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false);}
void UDWGameplayWidget::ClickSlotsBack(){PlayClick();if(HUD)HUD->ShowTitle();}
void UDWGameplayWidget::ClickClose(){PlayClick();if(HUD)HUD->ClosePanels();}
void UDWGameplayWidget::ClickSave(){PlayClick();if(GetDWGameInstance()&&GetDWGameInstance()->SaveCurrentGame())SetToast(TEXT("保存成功。"));else SaveError(TEXT("保存失败。"));OnUIAction(TEXT("Save"));}
void UDWGameplayWidget::ClickSaveTitle(){PlayClick();if(!GetDWGameInstance()||!GetDWGameInstance()->SaveCurrentGame()){SaveError(TEXT("保存失败，未返回标题。"));return;}OnUIAction(TEXT("SaveAndTitle"));GetDWGameInstance()->ReturnToTitle();}
void UDWGameplayWidget::ClickNoSaveTitle(){PlayClick();OnUIAction(TEXT("ReturnToTitle"));if(GetDWGameInstance())GetDWGameInstance()->ReturnToTitle();}
void UDWGameplayWidget::ClickDeathLoad()
{
    PlayClick();auto* P=GetPlayer();
    if(!P||!P->RequestCombatRespawn())
        if(HUD)HUD->Notify(DWText(this,TEXT("尚无可用的已激活安全点。"),TEXT("No activated safe point is available.")));
}
void UDWGameplayWidget::ClickSettingsBack(){PlayClick();if(HUD)HUD->ReturnFromSettings();}
void UDWGameplayWidget::ChangeVolume(float Value){MasterVolume=FMath::Clamp(Value,0.f,1.f);if(!bUpdatingSettingsControls)if(auto* GI=GetGameInstance())if(auto* Settings=GI->GetSubsystem<UDWUserSettings>())Settings->SetMasterVolume(MasterVolume);if(VolumeValueText)VolumeValueText->SetText(FText::Format(DWText(this,TEXT("主音量  {0}%"),TEXT("Master volume  {0}%")),FMath::RoundToInt(MasterVolume*100.f)));}
void UDWGameplayWidget::RefreshSettingsLabels()
{
    TGuardValue<bool> Guard(bUpdatingLanguageOptions,true);
    TGuardValue<bool> SettingsGuard(bUpdatingSettingsControls,true);
    const auto* S=UGameUserSettings::GetGameUserSettings();
    if(LanguageCombo)
    {
        LanguageCombo->ClearOptions();LanguageCombo->AddOption(TEXT("简体中文"));LanguageCombo->AddOption(TEXT("English"));
        LanguageCombo->SetSelectedIndex(UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English?1:0);
    }
    if(WindowModeCombo)
    {
        int32 Index=WindowModeCombo->GetSelectedIndex();
        if(Index==INDEX_NONE)Index=S?(S->GetFullscreenMode()==EWindowMode::Windowed?0:S->GetFullscreenMode()==EWindowMode::WindowedFullscreen?1:2):0;
        WindowModeCombo->ClearOptions();
        WindowModeCombo->AddOption(DWText(this,TEXT("窗口"),TEXT("Windowed")).ToString());
        WindowModeCombo->AddOption(DWText(this,TEXT("无边框"),TEXT("Borderless")).ToString());
        WindowModeCombo->AddOption(DWText(this,TEXT("全屏"),TEXT("Fullscreen")).ToString());
        WindowModeCombo->SetSelectedIndex(FMath::Clamp(Index,0,2));
    }
    if(QualityCombo)
    {
        int32 Index=QualityCombo->GetSelectedIndex();if(Index==INDEX_NONE)Index=S?FMath::Clamp(S->GetOverallScalabilityLevel(),0,3):2;
        QualityCombo->ClearOptions();
        QualityCombo->AddOption(DWText(this,TEXT("低"),TEXT("Low")).ToString());
        QualityCombo->AddOption(DWText(this,TEXT("中"),TEXT("Medium")).ToString());
        QualityCombo->AddOption(DWText(this,TEXT("高"),TEXT("High")).ToString());
        QualityCombo->AddOption(DWText(this,TEXT("极高"),TEXT("Epic")).ToString());
        QualityCombo->SetSelectedIndex(FMath::Clamp(Index,0,3));
    }
    ChangeVolume(MasterVolume);
    RefreshSettingsBook();
}
void UDWGameplayWidget::ChangeLanguage(FString SelectedItem,ESelectInfo::Type SelectionType)
{
    if(bUpdatingLanguageOptions)return;
    if(SelectedItem!=TEXT("简体中文")&&SelectedItem!=TEXT("English"))return;
    const EDWGameLanguage Selected=SelectedItem==TEXT("English")?EDWGameLanguage::English:EDWGameLanguage::SimplifiedChinese;
    if(Selected==UDWLocalizationLibrary::GetLanguage(this))return;
    PlayClick();UDWLocalizationLibrary::SetLanguage(this,Selected);RefreshFromHUD(true);OnUIAction(TEXT("ChangeLanguage"));
}
void UDWGameplayWidget::LoadSettings()
{
    if(auto* GI=GetGameInstance())if(auto* Settings=GI->GetSubsystem<UDWUserSettings>())MasterVolume=Settings->GetMasterVolume();
    TGuardValue<bool> SettingsGuard(bUpdatingSettingsControls,true);
    auto* S=UGameUserSettings::GetGameUserSettings();
    RefreshSettingsLabels();
    if(VolumeSlider)VolumeSlider->SetValue(MasterVolume);ChangeVolume(MasterVolume);
    if(ResolutionCombo)
    {
        if(ResolutionCombo->GetOptionCount()==0)for(const auto* R:{TEXT("1280 x 720"),TEXT("1600 x 900"),TEXT("1920 x 1080"),TEXT("2560 x 1440")})ResolutionCombo->AddOption(R);
        if(S){const auto R=S->GetScreenResolution();const FString Option=FString::Printf(TEXT("%d x %d"),R.X,R.Y);if(ResolutionCombo->FindOptionIndex(Option)==INDEX_NONE)ResolutionCombo->AddOption(Option);ResolutionCombo->SetSelectedOption(Option);}
    }
    if(WindowModeCombo&&S)WindowModeCombo->SetSelectedIndex(S->GetFullscreenMode()==EWindowMode::Windowed?0:S->GetFullscreenMode()==EWindowMode::WindowedFullscreen?1:2);
    if(QualityCombo&&S)QualityCombo->SetSelectedIndex(FMath::Clamp(S->GetOverallScalabilityLevel(),0,3));
    if(VSyncCheck&&S)VSyncCheck->SetIsChecked(S->IsVSyncEnabled());
}
void UDWGameplayWidget::BeginSettingsInteraction(){if(!bUpdatingSettingsControls&&!bUpdatingLanguageOptions)PlayClick();}
void UDWGameplayWidget::ChangeSettingsOption(FString SelectedItem,ESelectInfo::Type SelectionType)
{
    if(!bUpdatingSettingsControls&&!bUpdatingLanguageOptions&&SelectionType!=ESelectInfo::Direct&&!SelectedItem.IsEmpty())PlayClick();
}
void UDWGameplayWidget::ChangeVSync(bool bChecked){if(!bUpdatingSettingsControls)PlayClick();}
void UDWGameplayWidget::ClickApplySettings()
{
    PlayClick();auto* S=UGameUserSettings::GetGameUserSettings();
    if(S)
    {
        if(ResolutionCombo){FString A,B;if(ResolutionCombo->GetSelectedOption().Split(TEXT("x"),&A,&B)){const FIntPoint R(FCString::Atoi(*A),FCString::Atoi(*B));if(R.X>=640&&R.Y>=480)S->SetScreenResolution(R);}}
        if(WindowModeCombo){const int32 I=WindowModeCombo->GetSelectedIndex();S->SetFullscreenMode(I==0?EWindowMode::Windowed:I==1?EWindowMode::WindowedFullscreen:EWindowMode::Fullscreen);}
        if(QualityCombo)S->SetOverallScalabilityLevel(FMath::Clamp(QualityCombo->GetSelectedIndex(),0,3));
        if(VSyncCheck)S->SetVSyncEnabled(VSyncCheck->IsChecked());S->ApplySettings(false);S->SaveSettings();
    }
    if(auto* GI=GetGameInstance())if(auto* Settings=GI->GetSubsystem<UDWUserSettings>())Settings->SetMasterVolume(MasterVolume);
    SetToast(DWText(this,TEXT("设置已保存。"),TEXT("Settings saved.")).ToString());OnUIAction(TEXT("ApplySettings"));
}

FReply UDWGameplayWidget::NativeOnPreviewKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(ExtendedSettings&&ExtendedSettings->IsCapturing()){if(!E.IsRepeat())ExtendedSettings->CaptureKey(E.GetKey());return FReply::Handled();}
    if(HandleInventoryKey(E))return FReply::Handled();
    // Remapped menu keys (including Enter/Space) take priority over focused buttons.
    if(HUD&&HUD->IsBlockingGameplay()&&DWHandleMenuKey(HUD,Cast<ADWPlayerController>(GetOwningPlayer()),E.GetKey(),E.IsRepeat()))return FReply::Handled();
    return Super::NativeOnPreviewKeyDown(G,E);
}
FReply UDWGameplayWidget::NativeOnPreviewMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    if(ExtendedSettings&&ExtendedSettings->IsCapturing()){if(IsSettingsBookNavigationHit(E.GetScreenSpacePosition())){ExtendedSettings->CancelCapture();return Super::NativeOnPreviewMouseButtonDown(G,E);}if(ExtendedSettings->IsCancelHit(E.GetScreenSpacePosition()))return Super::NativeOnPreviewMouseButtonDown(G,E);ExtendedSettings->CaptureKey(E.GetEffectingButton());return FReply::Handled();}
    if(IsInventoryDiscardPending()){if(HandleInventoryDiscardPointer(E))return FReply::Handled();return Super::NativeOnPreviewMouseButtonDown(G,E);}
    if(HUD&&HUD->IsBlockingGameplay()&&DWHandleMenuKey(HUD,Cast<ADWPlayerController>(GetOwningPlayer()),E.GetEffectingButton(),false))return FReply::Handled();
    return Super::NativeOnPreviewMouseButtonDown(G,E);
}
FReply UDWGameplayWidget::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{
    if(HandleInventoryKey(E))return FReply::Handled();
    if(DWHandleMenuKey(HUD,Cast<ADWPlayerController>(GetOwningPlayer()),E.GetKey(),E.IsRepeat()))return FReply::Handled();
    return Super::NativeOnKeyDown(G,E);
}
FReply UDWGameplayWidget::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
    // Bubble-phase fallback only: child buttons handle their own clicks first; gaps/disabled cells cannot attack the world.
    if(IsC01QuickDockPointer(E.GetScreenSpacePosition()))return FReply::Handled();
    if(IsInventoryDiscardPending())return FReply::Handled();
    if(DWHandleMenuKey(HUD,Cast<ADWPlayerController>(GetOwningPlayer()),E.GetEffectingButton(),false))return FReply::Handled();
    return HUD&&HUD->IsBlockingGameplay()?FReply::Handled():Super::NativeOnMouseButtonDown(G,E);
}

void UDWGameplayWidget::ClickPause(){PlayClick();if(HUD)HUD->TogglePause();}
void UDWGameplayWidget::ClickPauseQuit(){PlayClick();if(!GetDWGameInstance()||!GetDWGameInstance()->SaveCurrentGame()){SaveError(TEXT("保存失败，未退出游戏。"));return;}UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false);}

void UDWGameplayWidget::ApplySliderPresentation(UUserWidget* Target)
{
    if(!Target||!Target->WidgetTree)return;
    Target->WidgetTree->ForEachWidget([this](UWidget* W)
    {
        if(auto* Nested=Cast<UUserWidget>(W))ApplySliderPresentation(Nested);
        if(auto* Slider=Cast<USlider>(W))
        {
            FSliderStyle Style=Slider->GetWidgetStyle();
            const float Thickness=FMath::Clamp(SliderTrackThickness,1.f,24.f);
            const FVector2D Thumb(FMath::Clamp(SliderThumbSize,12.f,48.f));
            if(auto* Box=Cast<USizeBox>(Slider->GetParent()))
                if(Box->GetHeightOverride()>0&&Box->GetHeightOverride()<Thumb.Y)Box->SetHeightOverride(Thumb.Y);
            if(Style.BarThickness!=Thickness||Style.NormalThumbImage.ImageSize!=Thumb||Style.HoveredThumbImage.ImageSize!=Thumb||Style.DisabledThumbImage.ImageSize!=Thumb)
            {
                Style.BarThickness=Thickness;Style.NormalThumbImage.ImageSize=Thumb;
                Style.HoveredThumbImage.ImageSize=Thumb;Style.DisabledThumbImage.ImageSize=Thumb;
                Slider->SetWidgetStyle(Style);
            }
        }
    });
}
