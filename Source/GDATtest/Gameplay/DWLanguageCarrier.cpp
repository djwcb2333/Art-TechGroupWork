#include "DWLanguageCarrier.h"
#include "DWLanguageSubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWInteractionPromptComponent.h"
#include "DWLocalizationLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "UObject/ConstructorHelpers.h"

ADWLanguageCarrier::ADWLanguageCarrier()
{
 SceneRoot=CreateDefaultSubobject<USceneComponent>(TEXT("LanguageRoot"));SetRootComponent(SceneRoot);
 MovingRoot=CreateDefaultSubobject<USceneComponent>(TEXT("MovingRoot"));MovingRoot->SetupAttachment(SceneRoot);
 PrimaryActorTick.bCanEverTick=true;
 CarrierMesh=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CarrierMesh"));CarrierMesh->SetupAttachment(SceneRoot);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
 if(Cube.Succeeded())CarrierMesh->SetStaticMesh(Cube.Object);
 CarrierMesh->SetRelativeScale3D(FVector(2.4,.9,.45));CarrierMesh->SetCollisionProfileName(TEXT("BlockAll"));
 Prompt=CreateDefaultSubobject<UDWInteractionPromptComponent>(TEXT("InteractionPrompt"));Prompt->SetupAttachment(SceneRoot);
 Prompt->WorldOffset=FVector(0,0,150);Prompt->GenericPromptTitle=FText::FromString(TEXT("文字载体"));
 Prompt->GenericEnglishPromptTitle=FText::FromString(TEXT("Inscription"));
}
void ADWLanguageCarrier::OnConstruction(const FTransform& Transform){Super::OnConstruction(Transform);RebuildVisualParts();RebuildGlyphDisplay();}
void ADWLanguageCarrier::BeginPlay()
{
 Super::BeginPlay();
 if(BuiltVisualParts.IsEmpty()&&!VisualParts.IsEmpty())RebuildVisualParts();
 if(GlyphPlanes.IsEmpty()&&!GlyphTextures.IsEmpty())RebuildGlyphDisplay();
 if(LinkedGateActor){ClosedGateTransform=LinkedGateActor->GetActorTransform();bGateCaptured=true;}
 if(auto* GI=GetGameInstance())if(auto* S=GI->GetSubsystem<UDWLanguageSubsystem>())S->OnProgressChanged.AddUniqueDynamic(this,&ADWLanguageCarrier::HandleProgressChanged);
 RefreshCommittedPresentation();
}
void ADWLanguageCarrier::EndPlay(const EEndPlayReason::Type Reason)
{
 if(auto* GI=GetGameInstance())if(auto* S=GI->GetSubsystem<UDWLanguageSubsystem>())S->OnProgressChanged.RemoveDynamic(this,&ADWLanguageCarrier::HandleProgressChanged);
 Super::EndPlay(Reason);
}
bool ADWLanguageCarrier::IsInReach(const ADWPlayerCharacter* P)const
{
 return IsValid(P)&&!P->IsDead()&&!P->IsActorBeingDestroyed()&&P->GetWorld()==GetWorld()&&!StableId.IsNone()&&
  FMath::IsFinite(InteractionRadius)&&InteractionRadius>0.f&&FMath::IsFinite(HeightTolerance)&&HeightTolerance>0.f&&
  FVector::DistSquared2D(GetActorLocation(),P->GetActorLocation())<=FMath::Square(InteractionRadius)&&
  FMath::Abs(GetActorLocation().Z-P->GetActorLocation().Z)<=HeightTolerance;
}
void ADWLanguageCarrier::RebuildGlyphDisplay()
{
 for(const auto& Plane:GlyphPlanes)if(IsValid(Plane.Get()))Plane->DestroyComponent();GlyphPlanes.Reset();
 if(!GlyphMaterial||GlyphTextures.IsEmpty()||!FMath::IsFinite(GlyphSize)||GlyphSize<=0.f)return;
 auto* PlaneMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));if(!PlaneMesh)return;
 for(int32 I=0;I<GlyphTextures.Num();++I)
 {
  if(!GlyphTextures[I])continue;auto* P=NewObject<UStaticMeshComponent>(this);P->SetStaticMesh(PlaneMesh);
  P->SetCollisionEnabled(ECollisionEnabled::NoCollision);P->SetGenerateOverlapEvents(false);P->SetupAttachment(bGlyphAttachToMovingRoot?MovingRoot.Get():SceneRoot.Get());
  const float Step=(I-(GlyphTextures.Num()-1)*.5)*GlyphSize;
  P->SetRelativeLocation(GlyphOffset+(bGlyphVertical?FVector(0,0,-Step):FVector(Step,0,0)));P->SetRelativeRotation(GlyphRotation);P->SetRelativeScale3D(FVector(GlyphSize/100.f));
  auto* M=UMaterialInstanceDynamic::Create(GlyphMaterial,this);M->SetTextureParameterValue(TEXT("GlyphTexture"),GlyphTextures[I]);P->SetMaterial(0,M);
  P->RegisterComponent();GlyphPlanes.Add(P);
 }
}
void ADWLanguageCarrier::RebuildVisualParts()
{
 for(const auto& C:BuiltVisualParts)if(IsValid(C.Get()))C->DestroyComponent();BuiltVisualParts.Reset();
 CarrierMesh->SetVisibility(VisualParts.IsEmpty());CarrierMesh->SetCollisionEnabled(VisualParts.IsEmpty()?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
 for(const auto& Part:VisualParts)
 {
  if(!Part.Mesh)continue;auto* C=NewObject<UStaticMeshComponent>(this);C->SetupAttachment(Part.bAttachToMovingRoot?MovingRoot.Get():SceneRoot.Get());
  C->SetStaticMesh(Part.Mesh);C->SetRelativeTransform(Part.RelativeTransform);
  C->SetCollisionEnabled(Part.bEnableCollision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
  C->SetCollisionResponseToAllChannels(ECR_Block);C->SetGenerateOverlapEvents(false);
  for(int32 I=0;I<Part.Materials.Num();++I)if(Part.Materials[I])C->SetMaterial(I,Part.Materials[I]);
  C->RegisterComponent();BuiltVisualParts.Add(C);
 }
 if(MovingRoot)MovingRoot->SetRelativeTransform(bDecoded?MovingRootDecoded:MovingRootLocked);
}
void ADWLanguageCarrier::Tick(float Dt)
{
 Super::Tick(Dt);if(Kind!=EDWLanguageCarrierKind::Relic||!MovingRoot)return;
 const float Target=bDecoded?1.f:0.f;DecodeAlpha=FMath::FInterpConstantTo(DecodeAlpha,Target,Dt,1.f/FMath::Max(.01f,DecodeRiseSeconds));
 FTransform Pose;Pose.Blend(MovingRootLocked,MovingRootDecoded,DecodeAlpha);
 if(bDecoded&&DecodeAlpha>=1.f&&GetWorld())Pose.AddToTranslation(FVector(0,0,FMath::Sin(GetWorld()->GetTimeSeconds()*PI)*DecodedBobAmplitude));
 MovingRoot->SetRelativeTransform(Pose);
}
void ADWLanguageCarrier::HandleProgressChanged(){RefreshCommittedPresentation();}
void ADWLanguageCarrier::RefreshCommittedPresentation()
{
 auto* GI=GetGameInstance();auto* S=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;if(!S||!S->IsReady())return;
 FText CN=ChineseTitle.IsEmpty()?FText::FromString(TEXT("查看文字")):ChineseTitle;
 FText EN=EnglishTitle.IsEmpty()?FText::FromString(TEXT("Inspect inscription")):EnglishTitle;
 FDWLanguageEntryView E;
 if(Kind==EDWLanguageCarrierKind::Relic)
 {
  FDWRelicPuzzleView R;if(S->GetRelic(GetContextId(),R))
  {bDecoded=R.bSolved;for(const auto& C:BuiltVisualParts)if(C)for(int32 I=0;I<C->GetNumMaterials();++I)
    {auto* MID=Cast<UMaterialInstanceDynamic>(C->GetMaterial(I));if(!MID)MID=C->CreateDynamicMaterialInstance(I);if(MID)MID->SetScalarParameterValue(GlyphGlowParameter,bDecoded?GlyphGlow:0.f);}}
 }
 if((Kind==EDWLanguageCarrierKind::Inscription||Kind==EDWLanguageCarrierKind::WaterSource)&&S->GetEntry(EntryId,E))
 {
  if(E.Status==EDWLanguageEntryStatus::Undiscovered){CN=FText::FromString(TEXT("未发现的团字"));EN=FText::FromString(TEXT("Undiscovered glyph"));}
  else if(E.Guess.IsEmpty()){CN=FText::FromString(TEXT("已发现，尚未记录含义"));EN=FText::FromString(TEXT("Discovered; no guess recorded"));}
  else{CN=FText::Format(FText::FromString(TEXT("我的猜测：{0}")),FText::FromString(E.Guess));EN=FText::Format(FText::FromString(TEXT("My guess: {0}")),FText::FromString(E.Guess));}
 }
 if(Prompt){Prompt->GenericPromptTitle=CN;Prompt->GenericEnglishPromptTitle=EN;Prompt->GenericPromptAction=FText::FromString(Kind==EDWLanguageCarrierKind::WaterSource?TEXT("取水"):TEXT("交互"));Prompt->GenericEnglishAction=FText::FromString(Kind==EDWLanguageCarrierKind::WaterSource?TEXT("Take water"):TEXT("Interact"));}
 // The prompt component renders the separate ReadGlyph row from the bridge's APPLIED key.
 if(Kind==EDWLanguageCarrierKind::Offering&&LinkedGateActor&&bGateCaptured)
 {
  FDWLanguageMechanismView V;if(S->GetMechanism(GetContextId(),V))
  {FTransform T=ClosedGateTransform;if(V.bComplete)T.AddToTranslation(GateOpenOffset);LinkedGateActor->SetActorTransform(T,false,nullptr,ETeleportType::TeleportPhysics);LinkedGateActor->SetActorEnableCollision(!V.bComplete);}
 }
}
