#include "DWCinematicDemo.h"
#include "DWWorldEvent.h"
#include "DWGameplayCinematic.h"
#include "DWUIOffscreenComponent.h"
#include "DWTextRevealComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "DWPlayerController.h"
#include "DWGameInstance.h"

void ADWCinematicDemoHUD::BeginPlay(){Super::BeginPlay();SetSessionStarted(true);ClosePanels();}
ADWCinematicDemoGameMode::ADWCinematicDemoGameMode(){HUDClass=ADWCinematicDemoHUD::StaticClass();static ConstructorHelpers::FClassFinder<APawn> Hero(TEXT("/Game/DoughWorld/Characters/Hero/Blueprints/BP_DWTopDownCharacter"));if(Hero.Class)DefaultPawnClass=Hero.Class;static ConstructorHelpers::FClassFinder<APlayerController> Controller(TEXT("/Game/DoughWorld/Core/Controllers/BP_DWPlayerController"));if(Controller.Class)PlayerControllerClass=Controller.Class;}
extern void DWStartCinematicVerification(UDWGameInstance* GI);
void ADWCinematicDemoGameMode::BeginPlay(){Super::BeginPlay();DWStartCinematicVerification(GetGameInstance<UDWGameInstance>());}

ADWBridgeChangeDemo::ADWBridgeChangeDemo()
{
 PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bStartWithTickEnabled=false;
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
 WorldEvent=CreateDefaultSubobject<UDWWorldEventComponent>(TEXT("WorldEvent"));WorldEvent->EventId=TEXT("Bridge_Demo_01");
 LeftHinge=CreateDefaultSubobject<USceneComponent>(TEXT("LeftHinge"));LeftHinge->SetupAttachment(RootComponent);LeftHinge->SetRelativeLocation(FVector(-360,0,0));
 RightHinge=CreateDefaultSubobject<USceneComponent>(TEXT("RightHinge"));RightHinge->SetupAttachment(RootComponent);RightHinge->SetRelativeLocation(FVector(360,0,0));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
 for(int32 I=0;I<6;++I){auto* M=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Plank%d"),I));M->SetupAttachment(I<3?LeftHinge:RightHinge);M->SetStaticMesh(Cube.Object);M->SetRelativeLocation(FVector(I<3?60+I*120:-300+(I-3)*120,0,0));M->SetRelativeScale3D(FVector(1.15,3.2,.22));M->SetCollisionProfileName(TEXT("BlockAll"));M->SetMobility(EComponentMobility::Movable);Planks.Add(M);}
}
void ADWBridgeChangeDemo::PostInitializeComponents(){Super::PostInitializeComponents();WorldEvent->OnRequested.AddUniqueDynamic(this,&ThisClass::StartBreak);WorldEvent->OnApplyState.AddUniqueDynamic(this,&ThisClass::ApplyFinal);}
void ADWBridgeChangeDemo::StartBreak(){bBreaking=true;Progress=0;SetActorTickEnabled(true);}
void ADWBridgeChangeDemo::ApplyPose(float T){LeftHinge->SetRelativeRotation(FRotator(-FallAngle*T,0,0));RightHinge->SetRelativeRotation(FRotator(FallAngle*T,0,0));}
void ADWBridgeChangeDemo::ApplyFinal(bool Complete){bBreaking=false;Progress=Complete?1:0;ApplyPose(Progress);for(auto M:Planks)M->SetCollisionEnabled(Complete?ECollisionEnabled::NoCollision:ECollisionEnabled::QueryAndPhysics);SetActorTickEnabled(false);}
void ADWBridgeChangeDemo::Tick(float Dt){Super::Tick(Dt);if(!bBreaking)return;Progress=FMath::Min(1.f,Progress+Dt/FMath::Max(.05f,BreakSeconds));ApplyPose(Progress*Progress);if(Progress>=1)WorldEvent->CompleteEvent();}

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprintExtension.h"
#include "UIComponentWidgetBlueprintExtension.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
namespace
{
 bool SaveCutAsset(UObject* A){A->MarkPackageDirty();const FString File=FPackageName::LongPackageNameToFilename(A->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;return UPackage::SavePackage(A->GetOutermost(),A,*File,Args);}
 UBlueprint* CutBP(const FString& Name,UClass* Parent){const FString Path=TEXT("/Game/DoughWorld/Cinematics/Blueprints/")+Name;if(auto* B=LoadObject<UBlueprint>(nullptr,*(Path+TEXT(".")+Name)))return B;auto* B=FKismetEditorUtilities::CreateBlueprint(Parent,CreatePackage(*Path),*Name,BPTYPE_Normal,UBlueprint::StaticClass(),UBlueprintGeneratedClass::StaticClass());FAssetRegistryModule::AssetCreated(B);FKismetEditorUtilities::CompileBlueprint(B);return B;}
}
#endif
bool UDWCinematicAuthoringLibrary::CreateCinematicAssets()
{
#if WITH_EDITOR
 const FString P=TEXT("/Game/DoughWorld/Cinematics/UI/WBP_DWCinematicSubtitle");
 auto* Subtitle=LoadObject<UWidgetBlueprint>(nullptr,*(P+TEXT(".WBP_DWCinematicSubtitle")));
 if(!Subtitle){
 Subtitle=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDWCinematicSubtitleWidget::StaticClass(),CreatePackage(*P),TEXT("WBP_DWCinematicSubtitle"),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));
 auto* Tree=Subtitle->WidgetTree.Get();auto* Root=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("Root"));Tree->RootWidget=Root;
 auto* Text=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("SubtitleText"));Text->bIsVariable=true;Text->SetText(FText::GetEmpty());Text->SetJustification(ETextJustify::Center);Text->SetAutoWrapText(true);Text->SetColorAndOpacity(FLinearColor::White);Text->SetShadowColorAndOpacity(FLinearColor::Black);Text->SetShadowOffset(FVector2D(2,2));
 auto* Font=LoadObject<UFont>(nullptr,TEXT("/Game/DoughWorld/UI/Shared/Fonts/F_DWHandDrawn.F_DWHandDrawn"));Text->SetFont(FSlateFontInfo(Font,26));
 auto* Slot=Root->AddChildToCanvas(Text);Slot->SetAnchors(FAnchors(.15f,.81f,.85f,.81f));Slot->SetOffsets(FMargin(0,0,0,90));Slot->SetAlignment(FVector2D(0,.5));
 auto* Ext=UWidgetBlueprintExtension::RequestExtension<UUIComponentWidgetBlueprintExtension>(Subtitle);FText Error;auto* R=Cast<UDWTextRevealComponent>(Ext->AddComponent(UDWTextRevealComponent::StaticClass(),TEXT("SubtitleText"),Error));if(!R)return false;R->bPlayOnConstruct=false;R->VoiceProfile=LoadObject<UDWTextVoiceProfile>(nullptr,TEXT("/Game/DoughWorld/Cinematics/Dialogue/Data/VoicePresets/DA_TextVoice_Squeaky.DA_TextVoice_Squeaky"));
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Subtitle);FKismetEditorUtilities::CompileBlueprint(Subtitle);FAssetRegistryModule::AssetCreated(Subtitle);if(!SaveCutAsset(Subtitle))return false;
 }
 auto* Trigger=CutBP(TEXT("BP_DWCinematicTrigger"),ADWCinematicTrigger::StaticClass());auto* Bridge=CutBP(TEXT("BP_DWBridgeChangeDemo"),ADWBridgeChangeDemo::StaticClass());
 if(!Trigger||!Bridge)return false;
 auto* Default=Cast<ADWCinematicTrigger>(Trigger->GeneratedClass->GetDefaultObject());Default->Cinematic->SubtitleWidgetClass=Subtitle->GeneratedClass;
 if(!SaveCutAsset(Trigger)||!SaveCutAsset(Bridge))return false;
 auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/DoughWorld/UI/WBP_DWGameplay.WBP_DWGameplay"));if(!HUD||!HUD->WidgetTree)return false;
 auto* Ext=UWidgetBlueprintExtension::RequestExtension<UUIComponentWidgetBlueprintExtension>(HUD);FText Error;
 for(FName Name:{FName(TEXT("StatusCard")),FName(TEXT("BrewingCard")),FName(TEXT("ControlsCard")),FName(TEXT("InteractionContainer")),FName(TEXT("HUDPauseButton")),FName(TEXT("ToastContainer"))}){
  auto* W=HUD->WidgetTree->FindWidget(Name);if(!W)continue;
  if(Ext->GetComponent(UDWUIOffscreenComponent::StaticClass(),Name))continue;
  auto* C=Cast<UDWUIOffscreenComponent>(Ext->AddComponent(UDWUIOffscreenComponent::StaticClass(),Name,Error));if(!C)return false;
  if(auto* S=Cast<UCanvasPanelSlot>(W->Slot))C->ExitEdge=S->GetAnchors().Minimum.Y>=.5f?EDWUIExitEdge::Bottom:EDWUIExitEdge::Top;
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(HUD);FKismetEditorUtilities::CompileBlueprint(HUD);
 return HUD->Status!=BS_Error&&SaveCutAsset(HUD);
#else
 return false;
#endif
}
