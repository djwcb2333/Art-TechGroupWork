#include "DWBuildingPlacementComponent.h"
#include "DWBuildingDefinition.h"
#include "DWBuildingDemolitionCost.h"
#include "DWPlaceableBuilding.h"
#include "DWBuildingEffectsComponent.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameplayConfig.h"
#include "DWGameplayHUD.h"
#include "DWGameInstance.h"
#include "DWWorldEvent.h"
#include "DWGatheringComponent.h"
#include "DWGatheringPond.h"
#include "DWCombatAuthoritySubsystem.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"

namespace DWBuildingPlacement
{
    bool GroundAllowed(const FHitResult& Hit,double MinNormalZ)
    {
        return Hit.bBlockingHit&&!Hit.ImpactPoint.ContainsNaN()&&!Hit.ImpactNormal.ContainsNaN()&&Hit.ImpactNormal.Z>=MinNormalZ&&
            !Cast<APawn>(Hit.GetActor())&&!Cast<ADWGatheringInteractable>(Hit.GetActor())&&!Cast<ADWGatheringPond>(Hit.GetActor());
    }
    bool SampleSupport(UWorld* World,const ADWPlayerCharacter* User,const UDWBuildingDefinition& Definition,
        const FVector& Position,const FQuat& Rotation,const AActor* Ignore,double& MinZ,double& MaxZ)
    {
        if(!World||!User)return false;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(DWBuildingSupport),false,User);
        if(Ignore)Query.AddIgnoredActor(Ignore);
        const double Allowance=Definition.MaxSupportHeightDifference;
        const double Normal=FMath::Cos(FMath::DegreesToRadians(double(Definition.MaxSlopeDegrees)));
        MinZ=TNumericLimits<double>::Max();MaxZ=-TNumericLimits<double>::Max();
        // Centre, four corners and edge midpoints prevent bridges across canyon gaps.
        for(int32 X=-1;X<=1;++X)for(int32 Y=-1;Y<=1;++Y)
        {
            const FVector Point=Position+Rotation.RotateVector(FVector(X*Definition.FootprintHalfExtent.X,Y*Definition.FootprintHalfExtent.Y,0));
            FHitResult Ground;
            if(!World->LineTraceSingleByChannel(Ground,Point+FVector(0,0,Allowance+25.),Point-FVector(0,0,Allowance+100.),ECC_Visibility,Query)||!GroundAllowed(Ground,Normal))return false;
            MinZ=FMath::Min(MinZ,Ground.ImpactPoint.Z);MaxZ=FMath::Max(MaxZ,Ground.ImpactPoint.Z);
        }
        return MaxZ-MinZ<=Allowance+.01;
    }
}

UDWBuildingPlacementComponent::UDWBuildingPlacementComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
    PreviewMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Buildings/Materials/M_DWBuildingPlacementPreview.M_DWBuildingPlacementPreview")));
}
ADWPlayerCharacter* UDWBuildingPlacementComponent::Player() const {return Cast<ADWPlayerCharacter>(GetOwner());}
ADWPlayerController* UDWBuildingPlacementComponent::Controller() const {return Player()?Cast<ADWPlayerController>(Player()->GetController()):nullptr;}
FVector UDWBuildingPlacementComponent::SnapXY(const FVector& P,double Grid)
{
    if(P.ContainsNaN()||!FMath::IsFinite(Grid)||Grid<=0.)return P;
    return FVector(FMath::GridSnap(P.X,Grid),FMath::GridSnap(P.Y,Grid),P.Z);
}
double UDWBuildingPlacementComponent::StepYaw(double Yaw,double Step)
{
    if(!FMath::IsFinite(Yaw)||!FMath::IsFinite(Step))return 0.;
    return FRotator::ClampAxis(Yaw+Step);
}
bool UDWBuildingPlacementComponent::IsAttackInputBlocked() const
{
    return bPlacing||bSuppressCombatUntilReleased;
}
bool UDWBuildingPlacementComponent::IsSessionCurrent() const
{
    auto* User=Player();auto* World=GetWorld();
    if(!IsValid(this)||IsBeingDestroyed()||!bPlacing||!IsValid(User)||User->IsActorBeingDestroyed()||User->IsDead()||!World||World->bIsTearingDown||
       !SessionPlayer.IsValid()||SessionPlayer.Get()!=User||!SessionController.IsValid()||Controller()!=SessionController.Get()||SessionController->GetPawn()!=User||
       !SessionInventory.IsValid()||User->GetInventory()!=SessionInventory.Get()||!SessionConfig.IsValid()||User->GetGameplayConfig()!=SessionConfig.Get()||
       !IsValid(ActiveDefinition)||SessionConfig->GetBuildingDefinition(SourceSlot.ItemId)!=ActiveDefinition||!SessionInventory->IsSlotSnapshotCurrent(SourceSlot))return false;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(User);if(!Authority||Authority->GetWorldEpoch()!=SessionEpoch)return false;
    const auto Life=Authority->CaptureActorLife(User);return Life.Entity==SessionEntity&&Life.Life==SessionLife;
}
bool UDWBuildingPlacementComponent::HasGameplayFocus() const
{
    const auto* PC=Controller();const auto* Local=PC?PC->GetLocalPlayer():nullptr;
    return Local&&Local->ViewportClient&&Local->ViewportClient->Viewport&&Local->ViewportClient->Viewport->HasFocus();
}
bool UDWBuildingPlacementComponent::BeginPlacement(const FDWInventorySlotSnapshot& Slot)
{
    auto* User=Player();auto* PC=Controller();
    if(bCommitting||!IsValid(User)||User->IsDead()||User->IsActorBeingDestroyed()||User->IsFormallyRooted()||User->bIsRolling||!PC||!User->HasCombatVitalsReady()||
        !GetWorld()||GetWorld()->bIsTearingDown||UDWWorldEventSubsystem::IsPlaying(this)||!User->GetInventory()||!User->GetInventory()->IsSlotSnapshotCurrent(Slot))return false;
    auto* Config=User->GetGameplayConfig();auto* Definition=Config?Config->GetBuildingDefinition(Slot.ItemId):nullptr;
    FString Error;const auto* Item=Config?Config->GetItemDefinition(Slot.ItemId):nullptr;
    if(!Definition||!Item||!Item->bUsable||!Definition->ValidateDefinition(Error))return false;
    auto* Class=Definition->ActorClass.LoadSynchronous();
    if(!Class||!Class->IsChildOf(ADWPlaceableBuilding::StaticClass())||Class->HasAnyClassFlags(CLASS_Abstract))return false;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(User);if(!Authority)return false;
    const auto Life=Authority->CaptureActorLife(User);
    if(!Life.Entity||!Life.Life)return false;
    EndPlacement(false);
    ActiveDefinition=Definition;SourceSlot=Slot;PlacementYaw=0.;
    SessionPlayer=User;SessionController=PC;SessionInventory=User->GetInventory();SessionConfig=Config;
    SessionEpoch=Authority->GetWorldEpoch();SessionEntity=Life.Entity;SessionLife=Life.Life;
    if(!CreatePreview()){EndPlacement(false);return false;}
    bPlacing=true;bPlacementValid=false;ResetPlacementInput();
    const TWeakObjectPtr<UDWBuildingPlacementComponent> SafeThis(this);
    User->CancelCombatActions();
    if(!SafeThis.IsValid())return false;
    if(!IsSessionCurrent()){EndPlacement(false);return false;}
    User->SetHarvestHeld(false);
    if(!SafeThis.IsValid())return false;
    if(!IsSessionCurrent()){EndPlacement(false);return false;}
    // Closing an existing panel preserves its layout and restores gameplay input.
    if(auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD()))HUD->ClosePanels();
    if(!SafeThis.IsValid())return false;
    if(!IsSessionCurrent()){EndPlacement(false);return false;}
    // Show the preview immediately; mouse arming protects confirmation, never visibility.
    RestorePlacementInputFocus();
    if(!SafeThis.IsValid())return false;
    if(!IsSessionCurrent()){EndPlacement(false);return false;}
    // The Use button's Slate reply can restore its old focus after ClosePanels returns.
    // Recover once after that event unwinds, without stealing focus on later frames.
    const uint32 InputGeneration=PlacementInputGeneration;
    GetWorld()->GetTimerManager().SetTimerForNextTick([SafeThis,InputGeneration]()
    {
        if(SafeThis.IsValid()&&SafeThis->PlacementInputGeneration==InputGeneration)
            SafeThis->RestorePlacementInputFocus();
    });
    UDWGatheringComponent::Notify(User,TEXT("选择位置：左键放置，Q逆时针、E顺时针旋转，右键或Esc取消"),TEXT("Place: left click. Rotate: Q counterclockwise / E clockwise. Cancel: right click or Esc."));
    return true;
}
bool UDWBuildingPlacementComponent::CreatePreview()
{
    if(!ActiveDefinition||!GetWorld())return false;
    auto* Material=PreviewMaterial.LoadSynchronous();if(!Material)return false;
    FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    PreviewActor=GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,Params);
    if(!PreviewActor)return false;
    PreviewActor->SetActorEnableCollision(false);PreviewActor->SetActorHiddenInGame(true);
    auto* Root=NewObject<USceneComponent>(PreviewActor,TEXT("PlacementPreviewRoot"),RF_Transient);
    PreviewActor->SetRootComponent(Root);Root->RegisterComponent();
    PreviewMID=UMaterialInstanceDynamic::Create(Material,this);if(!PreviewMID)return false;
    PreviewMID->SetScalarParameterValue(TEXT("Opacity"),.35f);
    for(const auto& Part:ActiveDefinition->Parts)
    {
        auto* Source=Part.Mesh.LoadSynchronous();if(!Source)return false;
        auto* Component=NewObject<UStaticMeshComponent>(PreviewActor,NAME_None,RF_Transient);
        Component->SetupAttachment(Root);Component->SetStaticMesh(Source);Component->SetRelativeTransform(Part.RelativeTransform);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);Component->SetCastShadow(false);
        for(int32 I=0;I<FMath::Max(1,Source->GetStaticMaterials().Num());++I)Component->SetMaterial(I,PreviewMID);
        Component->RegisterComponent();
    }
    return true;
}
void UDWBuildingPlacementComponent::SetPreviewVisible(bool Visible)
{
    if(IsValid(PreviewActor))PreviewActor->SetActorHiddenInGame(!Visible);
}
void UDWBuildingPlacementComponent::EndPlacement(bool SuppressInput)
{
    ++PlacementInputGeneration;bPlacing=false;bPlacementValid=false;ResetPlacementInput();
    if(IsValid(PreviewActor))PreviewActor->Destroy();PreviewActor=nullptr;PreviewMID=nullptr;ActiveDefinition=nullptr;SourceSlot=FDWInventorySlotSnapshot();
    SessionPlayer.Reset();SessionController.Reset();SessionInventory.Reset();SessionConfig.Reset();SessionEpoch=SessionEntity=SessionLife=0;
    if(SuppressInput){bSuppressCombatUntilReleased=true;SuppressCombatUntil=FPlatformTime::Seconds()+.25;}
}
void UDWBuildingPlacementComponent::CancelPlacement(){if(!bCommitting&&bPlacing)EndPlacement(true);}
void UDWBuildingPlacementComponent::EndPlay(const EEndPlayReason::Type Reason){EndPlacement(false);Super::EndPlay(Reason);}
bool UDWBuildingPlacementComponent::ResolveGround(const FVector& CursorHit,FTransform& Out) const
{
    if(!ActiveDefinition||!Player()||!GetWorld())return false;
    const FVector Snapped=SnapXY(CursorHit,FMath::IsFinite(GridSize)?FMath::Max(1.f,GridSize):20.f);
    const FQuat Rotation=FRotator(0,PlacementYaw,0).Quaternion();
    double MinZ,MaxZ;
    if(!DWBuildingPlacement::SampleSupport(GetWorld(),Player(),*ActiveDefinition,Snapped,Rotation,PreviewActor,MinZ,MaxZ))return false;
    Out=FTransform(Rotation,FVector(Snapped.X,Snapped.Y,MaxZ),FVector::OneVector);
    return ValidateGroundTransform(GetWorld(),Player(),*ActiveDefinition,Out,PreviewActor);
}
bool UDWBuildingPlacementComponent::ValidateGroundTransform(UWorld* World,const ADWPlayerCharacter* User,const UDWBuildingDefinition& Definition,const FTransform& Candidate,const AActor* Ignore)
{
    FString Error;if(!World||!User||!Definition.ValidateDefinition(Error)||Candidate.ContainsNaN()||!Candidate.GetRotation().IsNormalized()||!Candidate.GetScale3D().Equals(FVector::OneVector,.001))return false;
    const FVector Location=Candidate.GetLocation();
    if(FVector::DistSquared(User->GetActorLocation(),Location)>FMath::Square(double(Definition.MaxPlacementDistance)))return false;
    double MinZ,MaxZ;
    if(!DWBuildingPlacement::SampleSupport(World,User,Definition,Location,Candidate.GetRotation(),Ignore,MinZ,MaxZ)||FMath::Abs(MaxZ-Location.Z)>2.)return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DWBuildingOccupancy),false);
    if(Ignore)Query.AddIgnoredActor(Ignore);
    // Do not ignore the player here: placing a building through the player is invalid.
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);Objects.AddObjectTypesToQuery(ECC_Pawn);Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
    TArray<FOverlapResult> Hits;
    const FVector Extent=Definition.FootprintHalfExtent;
    const FVector QueryExtent(FMath::Max(.1,Extent.X-.5),FMath::Max(.1,Extent.Y-.5),FMath::Max(.1,Extent.Z-1.));
    World->OverlapMultiByObjectType(Hits,Location+FVector(0,0,Extent.Z+1.),Candidate.GetRotation(),Objects,FCollisionShape::MakeBox(QueryExtent),Query);
    for(const auto& Hit:Hits)
        if(auto* Component=Hit.GetComponent();Component&&Component->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block)return false;
    return true;
}
bool UDWBuildingPlacementComponent::UpdatePreviewFromCursor()
{
    auto* PC=Controller();if(!PC||!ActiveDefinition||!PreviewActor)return false;
    FVector Origin,Direction;FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(DWBuildingCursor),false,Player());Query.AddIgnoredActor(PreviewActor);
    if(!PC->DeprojectMousePositionToWorld(Origin,Direction)||!GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*100000.,ECC_Visibility,Query))
    {SetPreviewVisible(false);bPlacementValid=false;return false;}
    const FVector Snap=SnapXY(Hit.ImpactPoint,FMath::IsFinite(GridSize)?FMath::Max(1.f,GridSize):20.f);
    PreviewTransform=FTransform(FRotator(0,PlacementYaw,0),Snap);
    bPlacementValid=DWBuildingPlacement::GroundAllowed(Hit,FMath::Cos(FMath::DegreesToRadians(double(ActiveDefinition->MaxSlopeDegrees))))&&ResolveGround(Hit.ImpactPoint,PreviewTransform);
    PreviewActor->SetActorTransform(PreviewTransform);SetPreviewVisible(true);
    if(PreviewMID)PreviewMID->SetVectorParameterValue(TEXT("PreviewColor"),bPlacementValid?FLinearColor(.05f,1.f,.18f):FLinearColor(1.f,.06f,.025f));
    return bPlacementValid;
}
bool UDWBuildingPlacementComponent::ConfirmPlacement()
{
    auto* PC=Controller();
    if(bCommitting||!IsSessionCurrent()||!bMouseArmed||!PC||PC->IsGameplayBlocked()||!bPlacementValid||!HasGameplayFocus())return false;
    return CommitPlacementTransaction();
}
bool UDWBuildingPlacementComponent::CommitPlacementTransaction()
{
    auto* User=Player();auto* PC=Controller();
    if(bCommitting||!IsSessionCurrent()||!PC||PC->IsGameplayBlocked()||!bPlacementValid)return false;
    TGuardValue<bool> Guard(bCommitting,true);
    auto* Bag=User->GetInventory();auto* Definition=ActiveDefinition.Get();
    FDWPreparedInventoryChange Plan;
    if(!Bag->PrepareRemoveSnapshot(SourceSlot,1,Plan)||!ValidateGroundTransform(GetWorld(),User,*Definition,PreviewTransform,PreviewActor))return false;
    auto* Class=Definition->ActorClass.LoadSynchronous();
    if(!Class||!Class->IsChildOf(ADWPlaceableBuilding::StaticClass())||Class->HasAnyClassFlags(CLASS_Abstract))return false;
    const FTransform Transform=PreviewTransform;
    const TWeakObjectPtr<ADWPlayerCharacter> SafeUser(User);const TWeakObjectPtr<UDWInventoryComponent> SafeBag(Bag);
    auto* Building=GetWorld()->SpawnActorDeferred<ADWPlaceableBuilding>(Class,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if(!Building)return false;
    Building->Definition=Definition;Building->SetStaged(true);Building->bStationEnabled=false;Building->bRuntimeSpawned=true;
    Building->PersistentId=FName(*FGuid::NewGuid().ToString(EGuidFormats::Digits));
    // Only the building item is being spent here. A unique recipe snapshot is not historical payment.
    FDWBuildingDemolitionCost Cost;FString CostError;
    if(FDWBuildingDemolitionCost::ResolveUnique(*User->GetGameplayConfig(),SourceSlot.ItemId,Cost,CostError))
    {Building->DemolitionCostOrigin=1;Building->DemolitionCostRecipe=Cost.RecipeId;Building->DemolitionCostInputs=MoveTemp(Cost.Inputs);}
    else Building->DemolitionCostOrigin=2; // Keep placement working; unsafe refund cannot be inferred later.
    Building->SetActorEnableCollision(false);Building->SetActorHiddenInGame(true);
    Building->FinishSpawning(Transform);
    if(!IsValid(Building)||Building->IsActorBeingDestroyed()||!SafeUser.IsValid()||!SafeBag.IsValid()||!IsSessionCurrent()||
       Building->Definition!=Definition||!Building->HasCompleteBuildingVisuals()||!Building->GetActorTransform().Equals(Transform,.01)||
       !Bag->ValidatePreparedForCommit(Plan)||!ValidateGroundTransform(GetWorld(),User,*Definition,Transform,Building))
    {if(IsValid(Building))Building->Destroy();return false;}
    if(Building->DemolitionCostOrigin==1)
    {
        FDWBuildingDemolitionCost Now;FString Error;FDWBuildingDemolitionCost Frozen;
        Frozen.RecipeId=Building->DemolitionCostRecipe;Frozen.Inputs=Building->DemolitionCostInputs;
        if(!FDWBuildingDemolitionCost::ResolveUnique(*User->GetGameplayConfig(),SourceSlot.ItemId,Now,Error)||!Frozen.Matches(Now))
        {Building->Destroy();return false;}
    }
    // Commit both facts before any notification or visual callback; cancellation never reaches here.
    Bag->AdoptPreparedUnchecked(MoveTemp(Plan));
    Building->bStationEnabled=true;Building->SetStaged(false);EndPlacement(true);
    Building->PublishInteractionState();
    if(IsValid(Building)&&!Building->IsActorBeingDestroyed()&&Building->BuildingEffects)Building->BuildingEffects->PlayConstructionEffect();
    if(SafeBag.IsValid()&&SafeBag->CanWriteInventory()&&SafeUser.IsValid()&&SafeUser->GetWorld()&&!SafeUser->GetWorld()->bIsTearingDown)SafeBag->OnChanged.Broadcast();
    return true;
}
void UDWBuildingPlacementComponent::ResetPlacementInput()
{
    bMouseArmed=false;bRotateArmed=false;LastRotationDirection=0;NextRotation=0.;
}
void UDWBuildingPlacementComponent::RestorePlacementInputFocus()
{
    auto* PC=Controller();
    if(!IsSessionCurrent()||!PC||PC->IsGameplayBlocked()||!FSlateApplication::IsInitialized())return;
    auto& Slate=FSlateApplication::Get();
    auto* Local=PC->GetLocalPlayer();
    const auto Viewport=Local&&Local->ViewportClient?Local->ViewportClient->GetGameViewportWidget():TSharedPtr<SViewport>();
    // Never activate a background application/window or seize a different PIE viewport.
    if(!Slate.IsActive()||!Viewport.IsValid()||Slate.FindWidgetWindow(Viewport.ToSharedRef())!=Slate.GetActiveTopLevelWindow())return;
    const TWeakObjectPtr<UDWBuildingPlacementComponent> SafeThis(this);
    Slate.SetUserFocus(0,Viewport.ToSharedRef(),EFocusCause::SetDirectly);
    if(SafeThis.IsValid()&&IsSessionCurrent()&&HasGameplayFocus())UpdatePreviewFromCursor();
}
bool UDWBuildingPlacementComponent::UpdateConfirmationInput(bool MouseHeld,bool MousePressed)
{
    if(bMouseArmed&&MousePressed)return true;
    // Opening Use and releasing that click cannot commit the building.
    if(!MouseHeld&&!MousePressed)bMouseArmed=true;
    return false;
}
void UDWBuildingPlacementComponent::UpdateRotationInput(bool CounterClockwiseHeld,bool CounterClockwisePressed,bool ClockwiseHeld,bool ClockwisePressed,double Now)
{
    const bool CounterActive=CounterClockwiseHeld||CounterClockwisePressed;
    const bool ClockwiseActive=ClockwiseHeld||ClockwisePressed;
    if(!CounterActive&&!ClockwiseActive)
    {bRotateArmed=true;LastRotationDirection=0;NextRotation=0.;return;}
    if(!bRotateArmed)return; // Require release after focus loss or opening the mode.
    const int32 Direction=int32(ClockwiseActive)-int32(CounterActive);
    if(Direction==0){LastRotationDirection=0;NextRotation=0.;return;} // Both keys cancel each other.
    const double Step=FMath::IsFinite(RotationStep)?FMath::Clamp(double(RotationStep),1.,90.):15.;
    const double First=FMath::IsFinite(RotationFirstDelay)?FMath::Max(.05,double(RotationFirstDelay)):.35;
    const double Repeat=FMath::IsFinite(RotationRepeatInterval)?FMath::Max(.02,double(RotationRepeatInterval)):.10;
    const bool Pressed=Direction>0?ClockwisePressed:CounterClockwisePressed;
    if(Pressed||LastRotationDirection!=Direction)
    {PlacementYaw=StepYaw(PlacementYaw,Direction*Step);NextRotation=Now+First;}
    else if(Now>=NextRotation)
    {PlacementYaw=StepYaw(PlacementYaw,Direction*Step);NextRotation=Now+Repeat;}
    LastRotationDirection=Direction;
}
void UDWBuildingPlacementComponent::TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Dt,TickType,Function);
    auto* PC=Controller();
    if(bSuppressCombatUntilReleased&&PC&&FPlatformTime::Seconds()>=SuppressCombatUntil&&!PC->IsInputKeyDown(EKeys::LeftMouseButton)&&!PC->IsInputKeyDown(EKeys::RightMouseButton))bSuppressCombatUntilReleased=false;
    if(!bPlacing)return;
    if(!IsSessionCurrent()||!PC||UDWWorldEventSubsystem::IsPlaying(this)){EndPlacement(true);return;}
    if(!HasGameplayFocus()||PC->IsGameplayBlocked())
    {ResetPlacementInput();SetPreviewVisible(false);return;}
    if(PC->WasInputKeyJustPressed(EKeys::Escape)||PC->WasInputKeyJustPressed(EKeys::RightMouseButton)){CancelPlacement();return;}
    UpdateRotationInput(PC->IsInputKeyDown(EKeys::Q),PC->WasInputKeyJustPressed(EKeys::Q),
        PC->IsInputKeyDown(EKeys::E),PC->WasInputKeyJustPressed(EKeys::E),GetWorld()->GetTimeSeconds());
    UpdatePreviewFromCursor();
    // A fresh press is required after the button which opened Use has been released.
    if(UpdateConfirmationInput(PC->IsInputKeyDown(EKeys::LeftMouseButton),PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)))
        ConfirmPlacement();
}
