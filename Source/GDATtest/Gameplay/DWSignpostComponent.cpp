#include "DWSignpostComponent.h"
#include "DWInteractionPromptStyle.h"
#include "DWLocalizationLibrary.h"
#include "DWGameplayHUD.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWWorldEvent.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "Engine/Font.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Blueprint/WidgetLayoutLibrary.h"

class SDWSignpostArrow : public SLeafWidget
{
public:
 SLATE_BEGIN_ARGS(SDWSignpostArrow){} SLATE_END_ARGS()
 void Construct(const FArguments&){}
 float Degrees=0;
 FLinearColor Color=FLinearColor(1,.94f,.77f,1);
 virtual FVector2D ComputeDesiredSize(float)const override{return FVector2D(42,42);}
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& WS,bool)const override
 {
  const float A=FMath::DegreesToRadians(Degrees);const FVector2D Center=G.GetLocalSize()*.5;
  const auto P=[&](float X,float Y){return FVector2D(Center.X+X*FMath::Cos(A)-Y*FMath::Sin(A),Center.Y+X*FMath::Sin(A)+Y*FMath::Cos(A));};
  TArray<FVector2D> Stem={P(-13,0),P(13,0)},Head={P(3,-10),P(13,0),P(3,10)};
  auto Draw=[&](int32 L,FLinearColor C,float Width){FSlateDrawElement::MakeLines(Out,L,G.ToPaintGeometry(),Stem,ESlateDrawEffect::None,C*WS.GetColorAndOpacityTint(),true,Width);FSlateDrawElement::MakeLines(Out,L,G.ToPaintGeometry(),Head,ESlateDrawEffect::None,C*WS.GetColorAndOpacityTint(),true,Width);};
  Draw(Layer,FLinearColor(.07f,.035f,.018f,1),8);Draw(Layer+1,Color,4);return Layer+1;
 }
};
TSharedRef<SWidget> UDWSignpostArrow::RebuildWidget(){return SAssignNew(Arrow,SDWSignpostArrow);}
void UDWSignpostArrow::ReleaseSlateResources(bool B){Super::ReleaseSlateResources(B);Arrow.Reset();}
void UDWSignpostArrow::SetDirection(float D,FLinearColor C){if(Arrow){Arrow->Degrees=D;Arrow->Color=C;Arrow->Invalidate(EInvalidateWidgetReason::Paint);}}
void UDWSignpostPromptWidget::SetDirection(float D){if(DirectionArrow)DirectionArrow->SetDirection(D,Style?Style->TextColor:FLinearColor::White);}

UDWSignpostComponent::UDWSignpostComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bTickEvenWhenPaused=true;PrimaryComponentTick.TickGroup=TG_PostUpdateWork;bAutoActivate=true;}
void UDWSignpostComponent::BeginPlay()
{
 Super::BeginPlay();
 if(!Style)Style=LoadObject<UDWInteractionPromptStyle>(nullptr,TEXT("/Game/DoughWorld/UI/Interaction/DA_DWInteractionPromptStyle.DA_DWInteractionPromptStyle"));
 if(!Style)Style=NewObject<UDWInteractionPromptStyle>(this);
 if(!WidgetClass)WidgetClass=LoadClass<UDWSignpostPromptWidget>(nullptr,TEXT("/Game/DoughWorld/UI/Interaction/WBP_DWSignpostPrompt.WBP_DWSignpostPrompt_C"));
 RebuildDirections();
}
TArray<UWidgetComponent*> UDWSignpostComponent::GetDirectionWidgets()const
{TArray<UWidgetComponent*> Result;for(auto W:Widgets)Result.Add(W);return Result;}
void UDWSignpostComponent::RebuildDirections()
{
 for(auto W:Widgets)if(W)W->DestroyComponent();Widgets.Reset();
 if(!WidgetClass||!GetOwner())return;
 for(int32 I=0;I<Directions.Num();++I)
 {
  auto* W=NewObject<UWidgetComponent>(GetOwner(),NAME_None,RF_Transient);
  W->SetupAttachment(GetOwner()->GetRootComponent());W->SetWidgetSpace(EWidgetSpace::Screen);W->SetDrawSize(DrawSize);W->SetPivot(FVector2D(.5,.5));
  W->SetCollisionEnabled(ECollisionEnabled::NoCollision);W->SetGenerateOverlapEvents(false);W->SetCanEverAffectNavigation(false);W->SetWindowFocusable(false);
  W->SetInitialSharedLayerName(TEXT("DWSignpostLayer"));W->SetInitialLayerZOrder(60);
  W->SetUsingAbsoluteScale(true);W->SetTickMode(ETickMode::Enabled);W->SetTickWhenOffscreen(true);W->SetWidgetClass(WidgetClass);
  W->RegisterComponent();W->InitWidget();Widgets.Add(W);
  if(auto* P=Cast<UDWSignpostPromptWidget>(W->GetUserWidgetObject())){P->InitializePrompt(Style);P->SetVisibility(ESlateVisibility::HitTestInvisible);P->SetPromptVisible(false,true);}
 }
}
void UDWSignpostComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* F)
{
 Super::TickComponent(Dt,Type,F);auto* World=GetWorld();auto* Owner=GetOwner();if(!World||!World->IsGameWorld()||!IsValid(Owner)||World->bIsTearingDown)return;
 if(Widgets.Num()!=Directions.Num())RebuildDirections();
 auto* PC=UGameplayStatics::GetPlayerController(this,0);APawn* Pawn=PC?PC->GetPawn():nullptr;
 const auto* Player=Cast<ADWPlayerCharacter>(Pawn);const auto* DWPC=Cast<ADWPlayerController>(PC);const auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 const bool Blocked=!bPromptEnabled||!IsActive()||Owner->IsHidden()||!IsValid(Pawn)||!PC||!PC->IsLocalPlayerController()||World->IsPaused()
  ||(Player&&Player->IsDead())||(DWPC&&DWPC->IsGameplayBlocked())||(HUD&&(!HUD->IsSessionStarted()||HUD->IsBlockingGameplay()))||UDWWorldEventSubsystem::IsPlaying(this);
 bool Show=false;
 if(!Blocked){const FVector Delta=Pawn->GetActorLocation()-Owner->GetActorLocation();Show=FMath::Abs(Delta.Z)<=HeightTolerance&&Delta.SizeSquared2D()<=FMath::Square(FMath::Max(1.f,DetectionRadius)+(bPromptsVisible?FMath::Max(0.f,ExitHysteresis):0.f));}
 bPromptsVisible=Show;
 for(int32 I=0;I<Widgets.Num();++I)
 {
  auto* W=Widgets[I].Get();auto* P=Cast<UDWSignpostPromptWidget>(W->GetUserWidgetObject());if(!P)continue;
  const auto& Entry=Directions[I];FVector D=IsValid(Entry.DirectionTarget)?Entry.DirectionTarget->GetActorLocation()-Owner->GetActorLocation():Entry.WorldDirection;
  D.Z=0;D=D.GetSafeNormal();const FVector At=Owner->GetActorLocation()+WorldOffset+D*FMath::Max(0.f,LabelWorldRadius);
  W->SetWorldLocation(At);W->SetWorldScale3D(FVector::OneVector);
  FVector2D A=FVector2D::ZeroVector,B=FVector2D::ZeroVector;const bool Projected=PC&&PC->ProjectWorldLocationToScreen(At,A,true)&&PC->ProjectWorldLocationToScreen(At+D*100,B,true);
  const bool ValidDirection=!D.IsNearlyZero()&&Projected&&(B-A).SizeSquared()>KINDA_SMALL_NUMBER;
  const FText Text=DWText(this,*Entry.ChineseText.ToString(),*Entry.EnglishText.ToString());
  P->SetPromptText(Text,FText::GetEmpty(),FText::GetEmpty());
  if(ValidDirection)P->SetDirection(FMath::RadiansToDegrees(FMath::Atan2(B.Y-A.Y,B.X-A.X)));
  if(Projected){int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);const float DPI=FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this));
   const float MarginX=FMath::Min(Width*.45f,float(DrawSize.X*.5)*DPI+12),MarginY=FMath::Min(Height*.15f,float(DrawSize.Y*.5)*DPI+12);
   const FVector2D Safe(FMath::Clamp(A.X,double(MarginX),double(Width-MarginX)),FMath::Clamp(A.Y,double(FMath::Max(MarginY,Height*FMath::Clamp(TopScreenMarginFraction,0.f,.4f))),double(Height-MarginY)));
   P->SetRenderTranslation((Safe-A)/DPI);
  }
  P->SetPromptVisible(Show&&ValidDirection&&!Text.IsEmptyOrWhitespace(),Blocked);P->AdvancePresentation(FMath::Max(0.f,Dt));
 }
}
void UDWSignpostComponent::EndPlay(const EEndPlayReason::Type R)
{for(auto W:Widgets)if(W)W->DestroyComponent();Widgets.Reset();bPromptsVisible=false;Super::EndPlay(R);}

#if WITH_EDITOR
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#endif
bool UDWSignpostAuthoringLibrary::CreateSignpostWidget()
{
#if WITH_EDITOR
 const FString Path=TEXT("/Game/DoughWorld/UI/Interaction/WBP_DWSignpostPrompt"),Name=TEXT("WBP_DWSignpostPrompt");
 if(auto* Existing=LoadObject<UWidgetBlueprint>(nullptr,*(Path+TEXT(".")+Name)))return Existing->ParentClass==UDWSignpostPromptWidget::StaticClass();
 auto* Package=CreatePackage(*Path);
 auto* BP=Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(UDWSignpostPromptWidget::StaticClass(),Package,FName(*Name),BPTYPE_Normal,UWidgetBlueprint::StaticClass(),UWidgetBlueprintGeneratedClass::StaticClass()));if(!BP)return false;
 FAssetRegistryModule::AssetCreated(BP);if(!BP->WidgetTree)BP->WidgetTree=NewObject<UWidgetTree>(BP,TEXT("WidgetTree"),RF_Transactional);
 UWidgetTree* T=BP->WidgetTree;auto* Root=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("PromptCanvas"));T->RootWidget=Root;
 auto* Visual=T->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("PromptVisual"));Visual->bIsVariable=true;
 auto* Slot=Root->AddChildToCanvas(Visual);Slot->SetAnchors(FAnchors(.5,.5));Slot->SetAlignment(FVector2D(.5,.5));Slot->SetAutoSize(true);
 auto* Row=T->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("DirectionRow"));Visual->SetContent(Row);
 auto* ArrowWidget=T->ConstructWidget<UDWSignpostArrow>(UDWSignpostArrow::StaticClass(),TEXT("DirectionArrow"));ArrowWidget->bIsVariable=true;
 auto* AS=Row->AddChildToHorizontalBox(ArrowWidget);AS->SetVerticalAlignment(VAlign_Center);AS->SetPadding(FMargin(0,0,8,0));
 auto* Title=T->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("PromptTitle"));Title->bIsVariable=true;Title->SetText(FText::FromString(TEXT("水井")));
 if(auto* S=LoadObject<UDWInteractionPromptStyle>(nullptr,TEXT("/Game/DoughWorld/UI/Interaction/DA_DWInteractionPromptStyle.DA_DWInteractionPromptStyle"))){FSlateFontInfo Font(S->TitleFont,S->TitleFontSize);Font.OutlineSettings.OutlineSize=S->OutlineSize;Font.OutlineSettings.OutlineColor=S->OutlineColor;Title->SetFont(Font);Title->SetColorAndOpacity(S->TextColor);}
 Row->AddChildToHorizontalBox(Title)->SetVerticalAlignment(VAlign_Center);
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);if(BP->Status==BS_Error)return false;
 BP->MarkPackageDirty();FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
 return UPackage::SavePackage(Package,BP,*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args);
#else
 return false;
#endif
}
UDWSignpostComponent* UDWSignpostAuthoringLibrary::AddSignpostComponent(AActor* Actor)
{
#if WITH_EDITOR
 if(!IsValid(Actor)||Actor->GetWorld()->IsGameWorld())return nullptr;
 if(auto* Existing=Actor->FindComponentByClass<UDWSignpostComponent>())return Existing;
 Actor->Modify();auto* C=NewObject<UDWSignpostComponent>(Actor,NAME_None,RF_Transactional);Actor->AddInstanceComponent(C);C->RegisterComponent();Actor->MarkPackageDirty();return C;
#else
 return nullptr;
#endif
}
