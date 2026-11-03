#include "DWGatheringComponent.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWWeaponAnimationComponent.h"
#include "DWLootVisualCatalog.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

namespace DWToolGripVisual
{
 FTransform Facing(FName Type){return Type==TEXT("Axe")?FTransform(FQuat(FVector(0.,0.,-1.),PI)):FTransform::Identity;}
 // Reference tool-to-hand rotations measured from the current authored first poses, not guessed world Euler angles.
 FQuat CarryBasis(FName Type)
 {
  if(Type==TEXT("Axe"))return FQuat(-.8371142464503184,.1935765840401345,-.4082108141130865,-.30843439455157956);
  if(Type==TEXT("Pickaxe"))return FQuat(-.5206145959171139,.3102355023482939,-.7585185526395466,-.23950778881357862);
  if(Type==TEXT("Spear"))return FQuat(.3987293901709514,.7364786739460776,-.48191066065964816,-.2576356950795247);
  return FQuat(-.34278326886502225,-.6184654472946267,.6184657188657088,.34278313213574146);
 }
 bool Visible(const UStaticMeshComponent* C){return IsValid(C)&&C->IsVisible();}
}

bool UDWGatheringComponent::GetToolHandFrame(FTransform& Out)const
{
 const auto* User=Player();const auto* Mesh=User?User->GetMesh():nullptr;const FName Hand(TEXT("bone_005_R"));
 if(!Mesh||!Mesh->DoesSocketExist(Hand))return false;
 FTransform World=Mesh->GetSocketTransform(Hand);
 // Center of the 222 rigid right-hand vertices in the actual Hero support mesh's hand-local coordinates.
 // The bone pivot is ~38cm from the visible hand; its origin is not a palm attachment point.
 const FVector VisibleCenter(-3.34331161699944,-38.39667965264974,-3.7946008237988407);
 World.SetTranslation(World.TransformPosition(VisibleCenter));Out=World.GetRelativeTransform(User->GetActorTransform());
 // Keep the established model size; mesh/form/root compensation affects the anchor position, not a second weapon scale.
 Out.SetScale3D(FVector::OneVector);return !Out.ContainsNaN();
}

FTransform UDWGatheringComponent::ApplyToolHandAdjustment(FName Id,const FTransform& ModelToOwner,bool Idle)const
{
 const auto* Binding=ItemModelBindings.Find(Id);FTransform Hand;
 if(!Binding||!GetToolHandFrame(Hand))return ModelToOwner;
 const FTransform ModelToHand=ModelToOwner.GetRelativeTransform(Hand);
 const FTransform IdleAdjust=Idle?Binding->IdleHandAdjustment:FTransform::Identity;
 return ModelToHand*IdleAdjust*Binding->HandAdjustment*Hand;
}

bool UDWGatheringComponent::UpdateIdleSelectedToolVisual()
{
 auto* User=Player();const auto* Config=Profile();const auto* Tool=Config?Config->Tools.Find(ActiveTool):nullptr;
 const auto* Binding=ItemModelBindings.Find(ActiveTool);FTransform Hand;
 // A selected model may fill a tool motion's non-held entry/exit gap. This is display only, never an attack permission.
 const auto* W=User?User->WeaponAnimationComponent.Get():nullptr;
 const bool OwnToolMotion=SwingClock.bActive||(W&&W->IsSimpleSpearAttack());
 if(!bShowSelectedToolInHand||!bToolCarrySelected||(!CanAct()&&!OwnToolMotion)||!Tool||!Binding||!Binding->Mesh||!GetToolHandFrame(Hand))return false;
 const FName Type=Tool->ToolType;
 if(Type!=TEXT("Axe")&&Type!=TEXT("Pickaxe")&&Type!=TEXT("Hammer")&&Type!=TEXT("Spear"))return false;
 if(!GatheringAuthoredTool)
 {
  GatheringAuthoredTool=NewObject<UStaticMeshComponent>(User,TEXT("GatheringAuthoredTool"),RF_Transient);User->AddInstanceComponent(GatheringAuthoredTool);
  GatheringAuthoredTool->SetupAttachment(User->GetRootComponent());GatheringAuthoredTool->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  GatheringAuthoredTool->SetGenerateOverlapEvents(false);GatheringAuthoredTool->SetCanEverAffectNavigation(false);GatheringAuthoredTool->RegisterComponent();
 }
 auto* Mesh=Binding->Mesh.Get();const bool MeshChanged=GatheringAuthoredTool->GetStaticMesh()!=Mesh;
 if(MeshChanged)GatheringAuthoredTool->SetStaticMesh(Mesh);
 TArray<UMaterialInterface*,TInlineAllocator<8>> Desired;Desired.SetNumZeroed(FMath::Max(Mesh->GetStaticMaterials().Num(),Binding->MaterialOverrides.Num()));
 for(int32 I=0;I<Desired.Num();++I)Desired[I]=Binding->MaterialOverrides.IsValidIndex(I)&&Binding->MaterialOverrides[I]?Binding->MaterialOverrides[I].Get():Mesh->GetMaterial(I);
 while(Desired.Num()>Mesh->GetStaticMaterials().Num()&&!Desired.Last())Desired.Pop(EAllowShrinking::No);
 bool Changed=MeshChanged||GatheringAuthoredTool->GetNumMaterials()!=Desired.Num();
 for(int32 I=0;I<Desired.Num()&&!Changed;++I)Changed=GatheringAuthoredTool->GetMaterial(I)!=Desired[I];
 if(Changed){GatheringAuthoredTool->EmptyOverrideMaterials();for(int32 I=0;I<Desired.Num();++I)if(Desired[I]!=Mesh->GetMaterial(I))GatheringAuthoredTool->SetMaterial(I,Desired[I]);}
 // Axe carry is horizontal and points out from the character, rather than inheriting an upright strike-ready wrist pose.
 // The calibrated handle axis is -Z. Keep the actual palm position and the established shaft-facing flip.
 const FTransform CarryFrame=Type==TEXT("Axe")
  ?FTransform(FQuat::FindBetweenNormals(FVector(0.,0.,-1.),FVector::ForwardVector),Hand.GetTranslation(),FVector::OneVector)
  :FTransform(DWToolGripVisual::CarryBasis(Type))*Hand;
 const FTransform Carry=Binding->GripAdjustment*DWToolGripVisual::Facing(Type)*CarryFrame;
 const FTransform Tuned=ApplyToolHandAdjustment(ActiveTool,Carry,true);if(Tuned.ContainsNaN())return false;
 GatheringAuthoredTool->SetRelativeTransform(Tuned);
 if(GatheringToolHandle)GatheringToolHandle->SetVisibility(false);if(GatheringToolHead)GatheringToolHead->SetVisibility(false);
 GatheringAuthoredTool->SetVisibility(true);if(User->SwordPlaceholder)User->SwordPlaceholder->SetVisibility(false);return true;
}

bool UDWGatheringComponent::UpdateSelectedToolVisual(const FTransform* SimpleSpearGrip)
{
 auto* User=Player();const auto* Bag=User?User->GetInventory():nullptr;auto* W=User?User->WeaponAnimationComponent.Get():nullptr;
 if(!IsActive()||!IsValid(User)||User->IsActorBeingDestroyed()||User->IsDead()||User->IsCombatRestoreInProgress()||!Bag||Bag->CountItem(ActiveTool)<1)
 {HideGatheringToolVisual();return false;}
 if(SwingClock.bActive)
 {
  UpdateGatheringToolVisual();
  if(!DWToolGripVisual::Visible(GatheringAuthoredTool)&&!DWToolGripVisual::Visible(GatheringToolHandle)&&!DWToolGripVisual::Visible(GatheringToolHead))UpdateIdleSelectedToolVisual();
 }
 else if(W&&W->IsSimpleSpearAttack())
 {
  if(!SimpleSpearGrip){if(!UpdateIdleSelectedToolVisual())HideGatheringToolVisual();return DWToolGripVisual::Visible(GatheringAuthoredTool);}
  UpdateSimpleSpearVisual(*SimpleSpearGrip,true);
 }
 else if(W&&W->HasWeaponPresentation()){HideGatheringToolVisual();return false;}
 else if(!UpdateIdleSelectedToolVisual()){HideGatheringToolVisual();return false;}
 return DWToolGripVisual::Visible(GatheringAuthoredTool)||DWToolGripVisual::Visible(GatheringToolHandle)||DWToolGripVisual::Visible(GatheringToolHead);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWToolGripTransformTest,"DoughWorld.Tools.Visual.PureHandAdjustment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWToolGripTransformTest::RunTest(const FString&)
{
 const FTransform Hand(FRotator(15.,47.,-23.),FVector(90.,12.,5.));const FTransform Model(FRotator(20.,-10.,32.),FVector(12.,-30.,7.),FVector(.6));
 const FTransform Base=Model*Hand;TestTrue(TEXT("Identity adjustment preserves final placement"),(Base.GetRelativeTransform(Hand)*FTransform::Identity*Hand).Equals(Base,1.e-7));
 const FTransform Adjust(FQuat::Identity,FVector(0.,2.,0.));const auto Changed=Base.GetRelativeTransform(Hand)*Adjust*Hand;
 TestTrue(TEXT("Hand-space translation is independent of model pivot"),(Changed.GetTranslation()-Base.GetTranslation()).Equals(Hand.TransformVectorNoScale(FVector(0.,2.,0.)),1.e-7));
 const auto Facing=DWToolGripVisual::Facing(TEXT("Axe"));TestTrue(TEXT("Axe flip retains shaft direction"),Facing.TransformVectorNoScale(FVector(0,0,-1)).Equals(FVector(0,0,-1),1.e-7));TestTrue(TEXT("Axe flip reverses blade side"),Facing.TransformVectorNoScale(FVector(1,0,0)).Equals(FVector(-1,0,0),1.e-7));
 return true;
}
#endif

void UDWGatheringComponent::HideGatheringToolVisual()
{
    bSimpleSpearVisualShown=false;
    for(auto* Visual:{GatheringToolHandle.Get(),GatheringToolHead.Get(),GatheringAuthoredTool.Get()})
        if(IsValid(Visual))Visual->SetVisibility(false);
}
void UDWGatheringComponent::UpdateGatheringToolVisual()
{
    auto* User=Player();if(!SwingClock.bActive||!User||ActiveVisualSamples.IsEmpty()){HideGatheringToolVisual();return;}
    auto Create=[User](TObjectPtr<UStaticMeshComponent>& C,FName Name)
    {
        if(IsValid(C.Get()))return;
        C=NewObject<UStaticMeshComponent>(User,Name,RF_Transient);User->AddInstanceComponent(C);
        C->SetupAttachment(User->GetRootComponent());C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);C->RegisterComponent();
    };
    int32 Index=0;
    while(Index+1<ActiveVisualSamples.Num()&&ActiveVisualSamples[Index+1].TimeSeconds<=ActiveSourceSeconds)++Index;
    // Entry/exit hand convergence is part of the clean clip; show the tool only while actually held.
    if(!ActiveVisualSamples[Index].bPrimaryVisible){HideGatheringToolVisual();return;}
    FTransform Grip=ActiveVisualSamples[Index].PrimaryTransform;
    if(ActiveVisualSamples.IsValidIndex(Index+1))
    {
        const auto& A=ActiveVisualSamples[Index];const auto& B=ActiveVisualSamples[Index+1];
        Grip.Blend(A.PrimaryTransform,B.PrimaryTransform,FMath::Clamp((ActiveSourceSeconds-A.TimeSeconds)/
            FMath::Max(.001f,B.TimeSeconds-A.TimeSeconds),0.f,1.f));
    }
    const FTransform SampledGrip=Grip;
    Grip=ToolGripAdjustment*Grip;
    const bool bPickaxe=SwingToolType==TEXT("Pickaxe"),bHammer=SwingToolType==TEXT("Hammer");
    const auto* Binding=ItemModelBindings.Find(SwingToolId);
    auto* Mesh=Binding&&Binding->Mesh?Binding->Mesh.Get():(bHammer?HammerMesh.Get():(bPickaxe?PickaxeMesh.Get():AxeMesh.Get()));
    if(Mesh)
    {
        Create(GatheringAuthoredTool,TEXT("GatheringAuthoredTool"));GatheringAuthoredTool->SetStaticMesh(Mesh);
        FTransform AuthoredGrip=Grip;
        if(SwingToolType==TEXT("Axe")&&Binding&&Binding->Mesh)
        {
            // V07 Z scale fits the old procedural handle to the hand gap; it is not a scale for the authored axe.
            // Strip only that proxy scale before the model's rotated grip calibration, preserving its source pose/time.
            FTransform RigidSource=SampledGrip;RigidSource.SetScale3D(FVector::OneVector);
            AuthoredGrip=ToolGripAdjustment*RigidSource;
        }
        const FTransform Calibrated=(Binding&&Binding->Mesh?Binding->GripAdjustment:(bHammer?HammerMeshAdjustment:(bPickaxe?PickaxeMeshAdjustment:AxeMeshAdjustment)))*DWToolGripVisual::Facing(SwingToolType)*AuthoredGrip;
        GatheringAuthoredTool->SetRelativeTransform(ApplyToolHandAdjustment(SwingToolId,Calibrated,false));
        GatheringAuthoredTool->EmptyOverrideMaterials();
        if(Binding&&Binding->Mesh)
        {for(int32 I=0;I<Binding->MaterialOverrides.Num();++I)if(Binding->MaterialOverrides[I])GatheringAuthoredTool->SetMaterial(I,Binding->MaterialOverrides[I]);}
        else if(ToolHeadMaterial)GatheringAuthoredTool->SetMaterial(0,ToolHeadMaterial);
        HideGatheringToolVisual();GatheringAuthoredTool->SetVisibility(true);
    }
    else
    {
        Create(GatheringToolHandle,TEXT("GatheringToolHandle"));Create(GatheringToolHead,TEXT("GatheringToolHead"));
        GatheringToolHandle->SetStaticMesh(ProxyCube);GatheringToolHead->SetStaticMesh(ProxyCube);
        if(ToolHandleMaterial)GatheringToolHandle->SetMaterial(0,ToolHandleMaterial);
        if(ToolHeadMaterial)GatheringToolHead->SetMaterial(0,ToolHeadMaterial);
        const FTransform Handle=bHammer?FTransform(FQuat::Identity,FVector(0,0,-30),FVector(.07,.07,1.6)):
            (bPickaxe?PickaxeProxyHandleTransform:AxeProxyHandleTransform);
        const FTransform Head=bHammer?FTransform(FQuat::Identity,FVector(0,0,-110),FVector(.48,.16,.45)):
            (bPickaxe?PickaxeProxyHeadTransform:AxeProxyHeadTransform);
        GatheringToolHandle->SetRelativeTransform(ApplyToolHandAdjustment(SwingToolId,Handle*DWToolGripVisual::Facing(SwingToolType)*Grip,false));
        GatheringToolHead->SetRelativeTransform(ApplyToolHandAdjustment(SwingToolId,Head*DWToolGripVisual::Facing(SwingToolType)*Grip,false));
        HideGatheringToolVisual();GatheringToolHandle->SetVisibility(true);GatheringToolHead->SetVisibility(true);
    }
    if(User->SwordPlaceholder)User->SwordPlaceholder->SetVisibility(false);
}


void UDWGatheringComponent::UpdateSimpleSpearVisual(const FTransform& GripToOwner,bool Show)
{
 if(!Show)
 {
  if(bSimpleSpearVisualShown&&GatheringAuthoredTool)GatheringAuthoredTool->SetVisibility(false);
  bSimpleSpearVisualShown=false;return;
 }
 auto* User=Player();if(!IsValid(User)||User->IsActorBeingDestroyed()||ActiveTool!=TEXT("SimpleSugarSpear")||GripToOwner.ContainsNaN())return;
 const TWeakObjectPtr<ADWPlayerCharacter> VisualOwner(User);
 const auto* Binding=ItemModelBindings.Find(FName(TEXT("SimpleSugarSpear")));
 UStaticMesh* Mesh=Binding?Binding->Mesh.Get():nullptr;
 if(!Mesh&&!bSimpleSpearVisualLookupAttempted)
 {
  bSimpleSpearVisualLookupAttempted=true;
  const TSoftObjectPtr<UDWLootVisualCatalog> CatalogPath(FSoftObjectPath(TEXT("/Game/DoughWorld/Core/Data/DA_DWLootVisualCatalog.DA_DWLootVisualCatalog")));
  TStrongObjectPtr<UDWLootVisualCatalog> Catalog(CatalogPath.LoadSynchronous());
  if(Catalog.IsValid())if(const auto* Entry=Catalog->FindUnique(FName(TEXT("SimpleSugarSpear"))))
  {
   const FDWLootVisualEntry EntryCopy=*Entry;
   SimpleSpearFallbackMesh=EntryCopy.Mesh.LoadSynchronous();
   for(const auto& Pair:EntryCopy.MaterialOverrides)if(Pair.Key>=0)if(auto* Material=Pair.Value.LoadSynchronous())SimpleSpearFallbackMaterials.Add(Pair.Key,Material);
  }
 }
 // A synchronous asset load can publish editor callbacks. Recheck the same live source before component edits.
 if(!IsValid(this)||IsBeingDestroyed()||!VisualOwner.IsValid()||Player()!=VisualOwner.Get()||User->IsActorBeingDestroyed()||
    User->GatheringComponent.Get()!=this||ActiveTool!=TEXT("SimpleSugarSpear")||!User->WeaponAnimationComponent||!User->WeaponAnimationComponent->IsSimpleSpearAttack())return;
 Binding=ItemModelBindings.Find(FName(TEXT("SimpleSugarSpear")));Mesh=Binding&&Binding->Mesh?Binding->Mesh.Get():SimpleSpearFallbackMesh.Get();
 if(!Mesh){HideGatheringToolVisual();return;} // No arbitrary permanent mesh fallback.
 if(!GatheringAuthoredTool)
 {
  GatheringAuthoredTool=NewObject<UStaticMeshComponent>(User,TEXT("GatheringAuthoredTool"),RF_Transient);User->AddInstanceComponent(GatheringAuthoredTool);
  GatheringAuthoredTool->SetupAttachment(User->GetRootComponent());GatheringAuthoredTool->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  GatheringAuthoredTool->SetGenerateOverlapEvents(false);GatheringAuthoredTool->SetCanEverAffectNavigation(false);GatheringAuthoredTool->RegisterComponent();
 }
 const bool MeshChanged=GatheringAuthoredTool->GetStaticMesh()!=Mesh;
 if(MeshChanged)GatheringAuthoredTool->SetStaticMesh(Mesh);
 TArray<UMaterialInterface*,TInlineAllocator<8>> Desired;Desired.SetNumZeroed(Mesh->GetStaticMaterials().Num());
 for(int32 Index=0;Index<Desired.Num();++Index)Desired[Index]=Mesh->GetMaterial(Index);
 if(Binding&&Binding->Mesh)
 {
  if(Binding->MaterialOverrides.Num()>Desired.Num())Desired.SetNumZeroed(Binding->MaterialOverrides.Num());
  for(int32 Index=0;Index<Binding->MaterialOverrides.Num();++Index)if(Binding->MaterialOverrides[Index])Desired[Index]=Binding->MaterialOverrides[Index];
  // Existing SimpleSugarSpear binding maps its shaft to gathering -Z; the source prop uses this measured axis.
  // PCA of the unchanged Attack first geometry frame, signed toward the authored spear tip.
  const FQuat GatheringToSpear=FQuat::FindBetweenNormals(FVector(0.,0.,-1.),
   FVector(0.022905686225751041,-0.40277157654356532,-0.91501387239054355).GetSafeNormal());
  const FTransform GripBasis(GatheringToSpear);
  GatheringAuthoredTool->SetRelativeTransform(ApplyToolHandAdjustment(ActiveTool,Binding->GripAdjustment*GripBasis*GripToOwner,false));
 }
 else
 {
  for(const auto& Pair:SimpleSpearFallbackMaterials)if(Pair.Value){if(Pair.Key>=Desired.Num())Desired.SetNumZeroed(Pair.Key+1);Desired[Pair.Key]=Pair.Value;}
  // Catalog drop geometry alone has no calibrated held pivot. Do not display an uncalibrated fallback.
  HideGatheringToolVisual();bSimpleSpearVisualShown=false;return;
 }
 while(Desired.Num()>Mesh->GetStaticMaterials().Num()&&!Desired.Last())Desired.Pop(EAllowShrinking::No);
 bool MaterialsChanged=MeshChanged||GatheringAuthoredTool->GetNumMaterials()!=Desired.Num();
 for(int32 Index=0;Index<Desired.Num()&&!MaterialsChanged;++Index)MaterialsChanged=GatheringAuthoredTool->GetMaterial(Index)!=Desired[Index];
 if(MaterialsChanged)
 {
  GatheringAuthoredTool->EmptyOverrideMaterials();
  for(int32 Index=0;Index<Desired.Num();++Index)if(Desired[Index]!=Mesh->GetMaterial(Index))GatheringAuthoredTool->SetMaterial(Index,Desired[Index]);
 }
 if(GatheringToolHandle)GatheringToolHandle->SetVisibility(false);if(GatheringToolHead)GatheringToolHead->SetVisibility(false);
 GatheringAuthoredTool->SetVisibility(true);bSimpleSpearVisualShown=true;
 if(User->SwordPlaceholder)User->SwordPlaceholder->SetVisibility(false);
}
