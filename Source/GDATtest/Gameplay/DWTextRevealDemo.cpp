#include "DWTextRevealDemo.h"
#include "DWTextRevealComponent.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Blueprint/WidgetTree.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

UDWTextRevealComponent* UDWTextRevealDemoWidget::GetReveal() const{return UDWTextRevealLibrary::GetTextRevealComponent(DialogueText);}
void UDWTextRevealDemoWidget::NativeConstruct()
{
 Super::NativeConstruct();
 if(EnglishButton)EnglishButton->OnClicked.AddUniqueDynamic(this,&ThisClass::PlayEnglish);
 if(ChineseButton)ChineseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::PlayChinese);
 if(MixedButton)MixedButton->OnClicked.AddUniqueDynamic(this,&ThisClass::PlayMixed);
 if(ReplayButton)ReplayButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReplayLine);
 if(PauseButton)PauseButton->OnClicked.AddUniqueDynamic(this,&ThisClass::TogglePause);
 if(SkipButton)SkipButton->OnClicked.AddUniqueDynamic(this,&ThisClass::SkipLine);
 if(ClearButton)ClearButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ClearLine);
}
void UDWTextRevealDemoWidget::NativeTick(const FGeometry& G,float Dt)
{
 Super::NativeTick(G,Dt);if(auto* C=GetReveal())if(StatusText)
  StatusText->SetText(FText::FromString(FString::Printf(TEXT("%s   %d / %d characters   |   Lines: %d   |   %.2fs / %.2fs   |   Blips: %d"),C->IsPaused()?TEXT("PAUSED"):C->IsPlaying()?TEXT("PLAYING"):TEXT("READY"),C->GetVisibleCharacterCount(),C->GetCharacterCount(),C->GetVisualLineCount(),C->GetPlaybackTime(),C->GetDuration(),C->GetBlipsPlayed())));
}
void UDWTextRevealDemoWidget::PlayEnglish(){if(auto* C=GetReveal())C->PlayText(EnglishLine);}
void UDWTextRevealDemoWidget::PlayChinese(){if(auto* C=GetReveal())C->PlayText(ChineseLine);}
void UDWTextRevealDemoWidget::PlayMixed(){if(auto* C=GetReveal())C->PlayText(MixedLine);}
void UDWTextRevealDemoWidget::ReplayLine(){if(auto* C=GetReveal())C->Replay();}
void UDWTextRevealDemoWidget::TogglePause(){if(auto* C=GetReveal()){if(C->IsPaused())C->Resume();else C->Pause();}}
void UDWTextRevealDemoWidget::SkipLine(){if(auto* C=GetReveal())C->SkipToEnd();}
void UDWTextRevealDemoWidget::ClearLine(){if(auto* C=GetReveal())C->Stop(false);}
ADWTextRevealDemoGameMode::ADWTextRevealDemoGameMode(){HUDClass=ADWTextRevealDemoHUD::StaticClass();DefaultPawnClass=nullptr;}
void ADWTextRevealDemoHUD::BeginPlay()
{
 Super::BeginPlay();
 auto* Class=LoadClass<UDWTextRevealDemoWidget>(nullptr,TEXT("/Game/DoughWorld/Maps/Gameplay/Dialogue/UI/WBP_DWTextRevealDemo.WBP_DWTextRevealDemo_C"));
 auto* PC=GetOwningPlayerController();if(!Class||!PC)return;
 DemoWidget=CreateWidget<UDWTextRevealDemoWidget>(PC,Class);if(DemoWidget){DemoWidget->AddToViewport();PC->bShowMouseCursor=true;PC->SetInputMode(FInputModeUIOnly());}
}
void ADWTextRevealDemoHUD::EndPlay(const EEndPlayReason::Type Reason){if(DemoWidget)DemoWidget->RemoveFromParent();DemoWidget=nullptr;Super::EndPlay(Reason);}

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprintExtension.h"
#include "UIComponentWidgetBlueprintExtension.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ButtonSlot.h"
#include "DWUIBounceComponent.h"
#include "Engine/Font.h"
namespace
{
 bool SaveDemoAsset(UObject* A)
 {
  if(!A)return false;A->MarkPackageDirty();const FString Filename=FPackageName::LongPackageNameToFilename(A->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
  FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;return UPackage::SavePackage(A->GetOutermost(),A,*Filename,Args);
 }

}
#endif
bool UDWTextRevealAuthoringLibrary::CreateDemoAssets()
{
#if WITH_EDITOR
 auto* Profile=LoadObject<UDWTextVoiceProfile>(nullptr,TEXT("/Game/DoughWorld/Maps/Gameplay/Dialogue/Data/VoicePresets/DA_TextVoice_Squeaky.DA_TextVoice_Squeaky"));if(!Profile)return false;
 const FString P=TEXT("/Game/DoughWorld/Maps/Gameplay/Dialogue/UI/WBP_DWTextRevealDemo");
 if(LoadObject<UWidgetBlueprint>(nullptr,*(P+TEXT(".WBP_DWTextRevealDemo"))))return true;
 auto* BP=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDWTextRevealDemoWidget::StaticClass(),CreatePackage(*P),TEXT("WBP_DWTextRevealDemo"),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));if(!BP)return false;
 UWidgetTree* Tree=BP->WidgetTree;
 auto Text=[&](FName Name,const FString& Value,int32 Size){auto* T=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);T->bIsVariable=true;T->SetText(FText::FromString(Value));T->SetFont(FSlateFontInfo(LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto")),Size,TEXT("Regular")));T->SetColorAndOpacity(FSlateColor(FLinearColor(.94,.85,.65,1)));T->SetAutoWrapText(true);return T;};
 auto* Root=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("RootCanvas"));Tree->RootWidget=Root;
 auto* Background=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("Background"));Background->SetBrushColor(FLinearColor(.035,.05,.055,1));auto* BG=Root->AddChildToCanvas(Background);BG->SetAnchors(FAnchors(0,0,1,1));BG->SetOffsets(FMargin(0));
 auto* Card=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("DialogueCard"));Card->SetBrushColor(FLinearColor(.09,.115,.12,1));Card->SetPadding(FMargin(30));
 auto* Slot=Root->AddChildToCanvas(Card);Slot->SetAnchors(FAnchors(.5,.5));Slot->SetAlignment(FVector2D(.5,.5));Slot->SetAutoSize(true);
 auto* Size=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CardSize"));Size->SetWidthOverride(930);Card->SetContent(Size);
 auto* Body=Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("CardBody"));Size->SetContent(Body);
 auto Add=[&](UWidget* W,float Gap){auto* S=Body->AddChildToVerticalBox(W);S->SetPadding(FMargin(0,Gap));return S;};
 Add(Text(TEXT("TitleText"),TEXT("DW TEXT REVEAL / 逐字弹出与拟声"),26),10);
 Add(Text(TEXT("HelpText"),TEXT("A separate TextBlock component. Edit DialogueText > Components > DW Text Reveal.\nChoose a voice in Dialogue/Data/VoicePresets; edit its English and Chinese sound slots."),16),8);
 auto* Area=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("TextAreaSize"));Area->SetHeightOverride(250);Add(Area,22);
 auto* Dialogue=Text(TEXT("DialogueText"),TEXT("Welcome to Dough World!\n文字会逐字弹出，每一行从左到右。"),32);Area->SetContent(Dialogue);
 Add(Text(TEXT("StatusText"),TEXT("READY"),15),8);
 auto* Buttons=Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("Controls"));Add(Buttons,14);
 auto* Ext=UWidgetBlueprintExtension::RequestExtension<UUIComponentWidgetBlueprintExtension>(BP);FText Error;
 auto* Effect=Cast<UDWTextRevealComponent>(Ext->AddComponent(UDWTextRevealComponent::StaticClass(),TEXT("DialogueText"),Error));if(!Effect)return false;Effect->VoiceProfile=Profile;Effect->bPlayOnConstruct=true;
 Effect->Cues.Add({});Effect->Cues.Last().AfterCharacter=8;Effect->Cues.Last().CueName=TEXT("CameraBeat");
 const TPair<FName,FString> Labels[]={{TEXT("EnglishButton"),TEXT("English")},{TEXT("ChineseButton"),TEXT("中文")},{TEXT("MixedButton"),TEXT("Mixed")},{TEXT("ReplayButton"),TEXT("Replay")},{TEXT("PauseButton"),TEXT("Pause / Resume")},{TEXT("SkipButton"),TEXT("Show all")},{TEXT("ClearButton"),TEXT("Clear")}};
 for(const auto& Pair:Labels)
 {
  auto* B=Tree->ConstructWidget<UButton>(UButton::StaticClass(),Pair.Key);B->bIsVariable=true;B->SetBackgroundColor(FLinearColor(.28,.36,.36));
  auto* L=Text(FName(*(Pair.Key.ToString()+TEXT("_Label"))),Pair.Value,15);auto* BS=Cast<UButtonSlot>(B->AddChild(L));BS->SetPadding(FMargin(10,13));Buttons->AddChildToHorizontalBox(B)->SetPadding(FMargin(4,0));
  if(auto* Bounce=Cast<UDWUIBounceComponent>(Ext->AddComponent(UDWUIBounceComponent::StaticClass(),Pair.Key,Error)))Bounce->bPlayOnConstruct=false;
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);FAssetRegistryModule::AssetCreated(BP);return BP->Status!=BS_Error&&SaveDemoAsset(BP);
#else
 return false;
#endif
}

bool UDWTextRevealAuthoringLibrary::EnsureFrontendRevealComponents()
{
#if WITH_EDITOR
 auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/Maps/Gameplay/Frontend/UI/WBP_DWFrontend.WBP_DWFrontend"));
 if(!BP||!BP->WidgetTree)return false;
 auto* Ext=UWidgetBlueprintExtension::RequestExtension<UUIComponentWidgetBlueprintExtension>(BP);FText Error;
 for(FName Name:{FName(TEXT("LogoText")),FName(TEXT("TitleText"))})
 {
  if(!Cast<UTextBlock>(BP->WidgetTree->FindWidget(Name)))return false;
  if(!Ext->GetComponent(UDWTextRevealComponent::StaticClass(),Name)&&!Ext->AddComponent(UDWTextRevealComponent::StaticClass(),Name,Error))return false;
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
 return BP->Status!=BS_Error&&SaveDemoAsset(BP);
#else
 return false;
#endif
}
