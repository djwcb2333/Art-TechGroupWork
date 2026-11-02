#include "DWGatheringComponent.h"
#include "DWLanguageCarrier.h"
#include "DWLanguageSubsystem.h"
#include "DWGatheringWorld.h"
#include "DWGatheringPond.h"
#include "DWGatheringProfile.h"
#include "DWLootVisualCatalog.h"
#include "UObject/StrongObjectPtr.h"
#include "DWPlaceableBuilding.h"
#include "DWBuildingDemolitionTransaction.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "DWInteractionPromptComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UDWGatheringComponent::UDWGatheringComponent()
{
    bAutoActivate=true;
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.bTickEvenWhenPaused=true;
    static ConstructorHelpers::FObjectFinder<UDWWeaponAnimationSet> Motion(TEXT("/Game/DoughWorld/Gameplay/Resources/DA_DWGatheringMotion_Clean.DA_DWGatheringMotion_Clean"));
    SharedTwoHandMotion=Motion.Object;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    ProxyCube=Cube.Object;
}
ADWPlayerCharacter* UDWGatheringComponent::Player() const {return Cast<ADWPlayerCharacter>(GetOwner());}
const UDWGameplayConfig* UDWGatheringComponent::Profile() const {return Player()?UDWGameplayConfig::AsGatheringConfig(Player()->GetGameplayConfig()):nullptr;}
bool UDWGatheringComponent::IsGatheringEnabled() const {return Profile()!=nullptr||(Player()&&Player()->GetGameplayConfig()&&!Player()->GetGameplayConfig()->BuildingDefinitions.IsEmpty());}
UDWGatheringComponent* UDWGatheringComponent::Find(const UDWInventoryComponent& Inventory)
{return Inventory.GetOwner()?Inventory.GetOwner()->FindComponentByClass<UDWGatheringComponent>():nullptr;}
void UDWGatheringComponent::InitializeGathering()
{
    if(!IsGatheringEnabled()||!Player()||!Player()->GetGameplayConfig()||!Player()->GetInventory())return;
    // Pay owner resource time before a gathering contact at the same game timestamp.
    PrimaryComponentTick.AddPrerequisite(Player(),Player()->PrimaryActorTick);
    const TWeakObjectPtr<UDWGatheringComponent> Weak(this);
    Player()->GetInventory()->SetCraftingContextValidator([Weak](FName Recipe,FName Station,FName Building)
    {return Weak.IsValid()&&Weak->HasCraftingContext(Recipe,Station,Building);});
}
void UDWGatheringComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    CancelGatheringSwing();
    if(Player()&&Player()->GetInventory())Player()->GetInventory()->SetCraftingContextValidator(nullptr);
    Super::EndPlay(Reason);
}
void UDWGatheringComponent::Notify(ADWPlayerCharacter* User,const FString& CN,const FString& EN)
{
    if(IsValid(User))if(auto* PC=Cast<APlayerController>(User->GetController()))if(auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD()))
        HUD->Notify(DWText(User,*CN,*EN));
}
bool UDWGatheringComponent::CanAct() const
{
    const auto* User=Player();const auto* PC=User?Cast<ADWPlayerController>(User->GetController()):nullptr;
    return IsActive()&&IsGatheringEnabled()&&IsValid(User)&&!User->IsActorBeingDestroyed()&&User->CanPerformGatheringAction()&&
        PC&&!PC->IsGameplayBlocked()&&GetWorld()&&!GetWorld()->bIsTearingDown&&!GetWorld()->IsPaused();
}
bool UDWGatheringComponent::TryUseTool(FName Id)
{
    const auto* Config=Profile();auto* User=Player();
    if(!Config||!IsValid(User)||User->IsDead()||!User->GetInventory()||User->GetInventory()->CountItem(Id)<1||!Config->Tools.Contains(Id))return false;
    if(Id==TEXT("SimpleSugarSpear"))
    {
        FDWInventorySlotSnapshot Slot;bool Found=false;
        for(int32 Index=0;Index<User->GetInventory()->GetSlots().Num();++Index)
            if(User->GetInventory()->GetSlots()[Index].ItemId==Id&&User->GetInventory()->CaptureSlotSnapshot(Index,Slot)&&Slot.Quantity>0){Found=true;break;}
        if(!Found||!User->WeaponAnimationComponent||!User->WeaponAnimationComponent->EquipSimpleSpearInventory(Slot))return false;
    }
    else if(ActiveTool==TEXT("SimpleSugarSpear")&&User->WeaponAnimationComponent)User->WeaponAnimationComponent->CancelAction();
    CancelGatheringSwing();
    bToolCarrySelected=true;
    ActiveTool=Id; // Selecting again must not bypass the shared tool cooldown.
    const auto* Definition=Config->GetItemDefinition(Id);
    const FString CN=Definition?Definition->DisplayName.ToString():Id.ToString();
    const FString EN=Definition?Definition->EnglishDisplayName.ToString():Id.ToString();
    Notify(User,TEXT("已选择采集工具：")+CN,TEXT("Gathering tool selected: ")+EN);return true;
}
void UDWGatheringComponent::ClearActiveToolAfterShortcutForgotten()
{
    CancelGatheringSwing();ActiveTool=NAME_None;bWasHeld=false;LastTarget.Reset();
    // Keep NextToolTime: forgetting does not grant a free gathering cooldown reset.
}
void UDWGatheringComponent::RestoreActiveTool(FName Id)
{
    CancelGatheringSwing();
    ActiveTool=Profile()&&Player()->GetInventory()&&Profile()->Tools.Contains(Id)&&Player()->GetInventory()->CountItem(Id)>0?Id:NAME_None;
    bWasHeld=false;LastTarget.Reset();NextToolTime=0.;bToolCarrySelected=true;
}
AActor* UDWGatheringComponent::FindTarget() const
{
    auto* User=Player();if(!User||!GetWorld())return nullptr;
    AActor* Best=nullptr;double Distance=TNumericLimits<double>::Max();
    for(TActorIterator<ADWGatheringInteractable> It(GetWorld());It;++It)
    {
        if(!Profile()&&!Cast<ADWPlaceableBuilding>(*It))continue;
        if((!It->IsInteractionAvailable()&&!IsDemolitionTarget(Cast<ADWGatheringStation>(*It)))||!It->IsWithinReach(User))continue;
        const double D=FVector::DistSquared2D(It->GetActorLocation(),User->GetActorLocation());
        if(D<Distance){Best=*It;Distance=D;}
    }
    for(TActorIterator<ADWGatheringPond> It(GetWorld());It;++It)
    {
        if(!It->IsUsableWaterStation(User))continue;
        const double D=FVector::DistSquared2D(It->GetActorLocation(),User->GetActorLocation());
        if(D<Distance){Best=*It;Distance=D;}
    }
    return Best;
}
bool UDWGatheringComponent::HandleInteraction(float Dt,bool Held)
{
    if(!IsGatheringEnabled())return false;
    if(!CanAct()){CancelGatheringSwing();bWasHeld=false;LastTarget.Reset();return true;}
    if(Demolition.Active())
    {
        bWasHeld=Held;auto* User=Player();auto* Focus=Cast<ADWGatheringStation>(FindTarget());
        if(!User||!Demolition.Current(*User,Focus,ActiveTool,Held)){CancelDemolition();return true;}
        if(Demolition.Advance(*User,Focus,ActiveTool,Held,Dt))
        {
            const TWeakObjectPtr<UDWGatheringComponent> SafeThis(this);
            UpdateDemolitionFeedback();if(!SafeThis.IsValid())return true;
            EndDemolitionFeedback();if(!SafeThis.IsValid())return true;
            TryCommitDemolition();if(SafeThis.IsValid())CancelDemolition();return true;
        }
        if(!Demolition.Active()){CancelDemolition();return true;}
        const TWeakObjectPtr<UDWGatheringComponent> SafeThis(this);UpdateDemolitionFeedback();if(!SafeThis.IsValid())return true;
        if(!SwingClock.bActive&&!StartDemolitionSwing()){CancelDemolition();return true;}
        return true;
    }
    if(SwingClock.bActive){bWasHeld=Held;return true;}
    AActor* Target=FindTarget();const bool Pressed=Held&&!bWasHeld;bWasHeld=Held;
    if(!Target)return false; // Unified recipes do not disable authored legacy resource nodes in existing maps.
    if(LastTarget.Get()!=Target){LastTarget=Target;NextFailureNotice=0.;}
    if(Held&&Pressed)if(auto* Station=Cast<ADWGatheringStation>(Target);IsDemolitionTarget(Station))
    {
        auto* User=Player();
        if(User&&GetWorld()->GetTimeSeconds()>=NextToolTime&&Demolition.Begin(*User,*Station,ActiveTool,DemolitionWorkSeconds))
        {
            if(!StartDemolitionSwing())
            {CancelDemolition();Notify(User,TEXT("锤子动作或采集速度未配置。"),TEXT("Hammer motion or gathering speed is not configured."));}
            else BeginDemolitionFeedback();
        }
        return true;
    }
    if(Held&&Target)InteractWith(Target,Pressed);
    return true;
}
bool UDWGatheringComponent::InteractWith(AActor* Target,bool Pressed)
{
    auto* User=Player();if(!CanAct()||!IsValid(Target)||SwingClock.bActive)return false;
    if(auto* Pickup=Cast<ADWGatheringPickup>(Target))return Pressed&&Pickup->TryCollect(User);
    if(auto* Resource=Cast<ADWGatheringResource>(Target))
    {
        const double Now=GetWorld()->GetTimeSeconds();if(Now<NextToolTime)return false;
        const auto* Tool=Profile()?Profile()->Tools.Find(ActiveTool):nullptr;
        if(!Tool||User->GetInventory()->CountItem(ActiveTool)<1||Tool->ToolType!=Resource->RequiredTool)
        {
            if(Now>=NextFailureNotice){NextFailureNotice=Now+1.5;Notify(User,TEXT("请先在背包中使用对应的斧或镐"),TEXT("Use the matching axe or pickaxe from your inventory first."));}
            return false;
        }
        if(Tool->ToolType==TEXT("Axe")||Tool->ToolType==TEXT("Pickaxe"))return StartGatheringSwing(Resource);
        // Other authored tool families keep their existing gathering behavior.
        double Duration=0.;
        if(!DWGatheringActionTiming::TryScaleDuration(FMath::Max(.05f,Tool->UseIntervalSeconds),Tool->GatheringSpeedMultiplier,Duration))
        {Notify(User,TEXT("采集速度参数无效，请检查工具配置。"),TEXT("Invalid gathering speed; check the tool configuration."));return false;}
        NextToolTime=Now+Duration;
        return Resource->ApplyToolHit(User,Tool->ToolType,Tool->Sharpness,Tool->ReachCm);
    }
    if(!Pressed)return false;
    if(Cast<ADWGatheringStation>(Target)||Cast<ADWGatheringPond>(Target))
    {
        if(auto* PC=Cast<APlayerController>(User->GetController()))if(auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD()))
        {HUD->ShowMenu(EDWMenuPage::Crafting);return true;}
    }
    if(Cast<ADWGatheringRemains>(Target))
    {Notify(User,TEXT("残骸文字玩法等待策划补充，本测试不发放技能点。"),TEXT("Deciphering rules are pending. This test does not grant a skill point."));return true;}
    return false;
}
bool UDWGatheringComponent::HandlePrimaryClick()
{
    if(!CanAct())return false;
    if(SwingClock.bActive)return true; // Do not turn repeated gathering clicks into combat attacks.
    auto* PC=Cast<APlayerController>(Player()->GetController());FHitResult Hit;
    if(!PC||!PC->GetHitResultUnderCursor(ECC_Visibility,false,Hit))return false;
    AActor* Target=Hit.GetActor();
    if(auto* Object=Cast<ADWGatheringInteractable>(Target))
    {
        if(!Object->IsInteractionAvailable()||!Object->IsWithinReach(Player()))return false;
        // A recognized in-range target consumes the click even when full or wrong tool.
        InteractWith(Object,true);return true;
    }
    if(auto* Pond=Cast<ADWGatheringPond>(Target))if(Pond->IsWaterSurfaceHit(Hit)&&Pond->IsUsableWaterStation(Player())){InteractWith(Pond,true);return true;}
    return false;
}
bool UDWGatheringComponent::FindBuildingPlacement(FName Building,FTransform& Out) const
{
    auto* User=Player();if(!User||!GetWorld()||(Building!=TEXT("Furnace")&&Building!=TEXT("FermentationVat")))return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(GatherBuilding),false,User);
    // Select the first nearby grounded free patch; no new placement UI is introduced.
    for(int32 I=0;I<8;++I)
    {
        const double Angle=FMath::DegreesToRadians(User->GetActorRotation().Yaw+I*45.);
        const FVector XY=User->GetActorLocation()+FVector(FMath::Cos(Angle)*280.,FMath::Sin(Angle)*280.,0);
        FHitResult Floor;
        if(!GetWorld()->LineTraceSingleByChannel(Floor,XY+FVector(0,0,150),XY-FVector(0,0,500),ECC_Visibility,Query)||Floor.ImpactNormal.Z<.8||Cast<ADWGatheringInteractable>(Floor.GetActor())||Cast<ADWGatheringPond>(Floor.GetActor()))continue;
        const FVector Center=Floor.ImpactPoint+FVector(0,0,52);
        if(GetWorld()->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeBox(FVector(82,62,48)),Query))continue;
        bool NearExisting=false;
        for(TActorIterator<ADWGatheringStation> It(GetWorld());It;++It)if(!It->IsStaged()&&It->bStationEnabled&&FVector::DistSquared2D(It->GetActorLocation(),Center)<FMath::Square(190.)){NearExisting=true;break;}
        if(!NearExisting){Out=FTransform(FRotator(0,User->GetActorRotation().Yaw,0),Center);return true;}
    }
    return false;
}


namespace DWGatheringAction
{
float Finite(float Value,float Fallback){return FMath::IsFinite(Value)?Value:Fallback;}
}
bool UDWGatheringComponent::StartGatheringSwing(ADWGatheringResource* Resource)
{
    auto* User=Player();const auto* Tool=Profile()?Profile()->Tools.Find(ActiveTool):nullptr;
    auto* Inventory=User?User->GetInventory():nullptr;
    UDWWeaponAnimationSet* Motion=Tool&&Tool->ToolType==TEXT("Axe")&&AxeMotion?AxeMotion.Get():
        (Tool&&Tool->ToolType==TEXT("Pickaxe")&&PickaxeMotion?PickaxeMotion.Get():SharedTwoHandMotion.Get());
    const FName Role=Tool&&Tool->ToolType==TEXT("Axe")&&AxeMotion?AxeMotionRole:
        (Tool&&Tool->ToolType==TEXT("Pickaxe")&&PickaxeMotion?PickaxeMotionRole:SharedMotionRole);
    const auto* Clip=Motion?Motion->FindClip(Role):nullptr;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
    if(!CanAct()||!Tool||!Inventory||!IsValid(Resource)||!Resource->CanPlayerInteract(User,Tool->ReachCm)||
        Resource->RequiredTool!=Tool->ToolType||Inventory->CountItem(ActiveTool)<1||!Authority)return false;
    if(!Clip||!Clip->Animation||Clip->VisualSamples.IsEmpty()||!FMath::IsFinite(Clip->DurationSeconds)||Clip->DurationSeconds<=.01f)
    {
        if(GetWorld()->GetTimeSeconds()>=NextFailureNotice)
        {NextFailureNotice=GetWorld()->GetTimeSeconds()+1.5;Notify(User,TEXT("采集双手动作或握持轨迹未配置。"),TEXT("Gathering two-hand motion or grip samples are not configured."));}
        return false;
    }
    const float SourceLength=Clip->DurationSeconds;
    SwingTiming.SourceStart=FMath::Clamp(DWGatheringAction::Finite(SourceStartSeconds,.75f),0.f,SourceLength-.003f);
    SwingTiming.SourceRaised=FMath::Clamp(DWGatheringAction::Finite(SourceRaisedSeconds,1.6f),float(SwingTiming.SourceStart)+.001f,SourceLength-.002f);
    SwingTiming.SourceEnd=FMath::Clamp(DWGatheringAction::Finite(SourceEndSeconds,1.95f),float(SwingTiming.SourceRaised)+.001f,SourceLength);
    SwingTiming.SourceContact=FMath::Clamp(DWGatheringAction::Finite(SourceContactSeconds,1.8f),float(SwingTiming.SourceRaised),float(SwingTiming.SourceEnd));
    SwingTiming.Windup=FMath::Clamp(DWGatheringAction::Finite(WindupFraction,.4f),.05f,.65f);
    SwingTiming.Downstroke=FMath::Clamp(DWGatheringAction::Finite(DownstrokeFraction,.15f),.05f,float(.85-SwingTiming.Windup));
    SwingTiming.Hold=FMath::Clamp(DWGatheringAction::Finite(ImpactHoldFraction,.09f),0.f,float(.9-SwingTiming.Windup-SwingTiming.Downstroke));
    const double Baseline=FMath::Max(FMath::Clamp(DWGatheringAction::Finite(MinimumSwingSeconds,.8f),.1f,10.f),
        FMath::Max(.05f,DWGatheringAction::Finite(Tool->UseIntervalSeconds,1.f)));
    double Duration=0.;
    if(!DWGatheringActionTiming::TryScaleDuration(Baseline,Tool->GatheringSpeedMultiplier,Duration))
    {Notify(User,TEXT("采集速度参数无效，请检查工具配置。"),TEXT("Invalid gathering speed; check the tool configuration."));return false;}
    const auto Life=Authority->CaptureActorLife(User);const auto ResourceLife=Authority->CaptureActorLife(Resource);
    SwingEpoch=Authority->GetWorldEpoch();SwingPlayerEntity=Life.Entity;SwingPlayerLife=Life.Life;
    SwingResourceEntity=ResourceLife.Entity;SwingResourceLife=ResourceLife.Life;
    SwingTarget=Resource;SwingInventory=Inventory;SwingInventoryRevision=Inventory->GetInventoryRevision();
    SwingToolId=ActiveTool;SwingToolType=Tool->ToolType;SwingSharpness=Tool->Sharpness;SwingReach=Tool->ReachCm;
    SwingUseInterval=Tool->UseIntervalSeconds;SwingSpeedMultiplier=Tool->GatheringSpeedMultiplier;
    ActiveGatheringSequence=Clip->Animation;ActiveVisualSamples=Clip->VisualSamples;SwingSourceDuration=SourceLength;
    bActiveDirectGatheringSource=bUseDirectGatheringSource;
    ActiveSourceSeconds=bActiveDirectGatheringSource?0.f:static_cast<float>(SwingTiming.SourceStart);
    GatheringPoseHoldUntil=0.;
    const float ToolContact=Tool->ToolType==TEXT("Axe")&&AxeMotion?AxeGatheringContactFraction:
        (Tool->ToolType==TEXT("Pickaxe")&&PickaxeMotion?PickaxeGatheringContactFraction:-1.f);
    const float ContactFraction=FMath::IsFinite(ToolContact)&&ToolContact>=0.f?ToolContact:GatheringContactFraction;
    const double Contact=bActiveDirectGatheringSource?
        FMath::Clamp(DWGatheringAction::Finite(ContactFraction,0.4857142857f),.01f,.99f):SwingTiming.ContactProgress();
    if(User->CharacterA01&&User->CharacterA01->IsEnabled()){SwingA01Action=Authority->BeginAction(User);if(!SwingA01Action.Id)return false;}
    ++SwingGeneration;SwingClock.Start(Duration,Contact);
    NextToolTime=GetWorld()->GetTimeSeconds()+Duration;
    const FVector Facing=(Resource->GetActorLocation()-User->GetActorLocation()).GetSafeNormal2D();
    if(!Facing.IsNearlyZero())User->SetActorRotation(Facing.Rotation());
    SetComponentTickEnabled(true);
    return true;
}
bool UDWGatheringComponent::IsPendingHitCurrent() const
{
    auto* User=Player();auto* Resource=SwingTarget.Get();auto* Inventory=SwingInventory.Get();
    const auto* Tool=Profile()?Profile()->Tools.Find(SwingToolId):nullptr;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(this);
    if(!CanAct()||!IsValid(Resource)||!Resource->CanPlayerInteract(User,SwingReach)||!Inventory||User->GetInventory()!=Inventory||
        Inventory->GetInventoryRevision()!=SwingInventoryRevision||Inventory->CountItem(SwingToolId)<1||ActiveTool!=SwingToolId||
        !Tool||Tool->ToolType!=SwingToolType||Tool->Sharpness!=SwingSharpness||Tool->ReachCm!=SwingReach||
        Tool->UseIntervalSeconds!=SwingUseInterval||Tool->GatheringSpeedMultiplier!=SwingSpeedMultiplier||
        Resource->RequiredTool!=SwingToolType||!Authority||Authority->GetWorldEpoch()!=SwingEpoch||
        (User->CharacterA01&&User->CharacterA01->IsEnabled()&&!Authority->IsActionOwnedBy(SwingA01Action,User)))return false;
    const auto Life=Authority->CaptureActorLife(User);const auto ResourceLife=Authority->CaptureActorLife(Resource);
    return Life.Entity==SwingPlayerEntity&&Life.Life==SwingPlayerLife&&
        ResourceLife.Entity==SwingResourceEntity&&ResourceLife.Life==SwingResourceLife;
}
void UDWGatheringComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if(!SwingClock.bActive)return;
    if(bDemolitionSwing)
    {
        auto* User=Player();
        if(!User||!CanAct()||!Demolition.Current(*User,Cast<ADWGatheringStation>(FindTarget()),ActiveTool,bWasHeld))
        {CancelDemolition();return;}
        const auto Step=SwingClock.Advance(DeltaTime);
        ActiveSourceSeconds=bActiveDirectGatheringSource?static_cast<float>(SwingClock.Progress())*SwingSourceDuration:
            static_cast<float>(SwingTiming.SourceSeconds(SwingClock.Progress()));
        if(Step.bFinished){StopGatheringSwingOnly();return;}
        return;
    }
    if(!CanAct()||(!SwingClock.bImpactConsumed&&!IsPendingHitCurrent())){CancelGatheringSwing();return;}
    const uint64 Generation=SwingGeneration;
    const auto Step=SwingClock.Advance(DeltaTime);
    ActiveSourceSeconds=bActiveDirectGatheringSource?static_cast<float>(SwingClock.Progress())*SwingSourceDuration:
        static_cast<float>(SwingTiming.SourceSeconds(SwingClock.Progress()));
    if(Step.bImpact)
    {
        // The clock consumes this contact BEFORE any resource/Blueprint callback executes.
        auto* Resource=SwingTarget.Get();
        if(!IsPendingHitCurrent()){CancelGatheringSwing();return;}
        Resource->ApplyToolHit(Player(),SwingToolType,SwingSharpness,SwingReach,SwingToolId,SwingA01Action);
        if(!IsValid(this)||SwingGeneration!=Generation||!SwingClock.bActive)return;
    }
    if(Step.bFinished){CancelGatheringSwing();return;}
    
}
float UDWGatheringComponent::GetGatheringSourceSeconds() const
{
    return ActiveGatheringSequence?ActiveGatheringSequence->GetPlayLength()*
        FMath::Clamp(ActiveSourceSeconds/FMath::Max(.001f,SwingSourceDuration),0.f,1.f):0.f;
}
float UDWGatheringComponent::GetGatheringSourceDuration() const
{return ActiveGatheringSequence?ActiveGatheringSequence->GetPlayLength():1.f;}
bool UDWGatheringComponent::HasGatheringAnimationSnapshot() const
{
    return ActiveGatheringSequence&&(SwingClock.bActive||(GetWorld()&&GetWorld()->GetTimeSeconds()<GatheringPoseHoldUntil));
}
void UDWGatheringComponent::CancelGatheringSwing()
{Demolition.Cancel();const TWeakObjectPtr<UDWGatheringComponent> SafeThis(this);const uint64 Generation=SwingGeneration;EndDemolitionFeedback();if(SafeThis.IsValid()&&SwingGeneration==Generation)StopGatheringSwingOnly();}
void UDWGatheringComponent::StopGatheringSwingOnly()
{
    bDemolitionSwing=false;
    if(SwingA01Action.Id){if(auto* A=UDWCombatAuthoritySubsystem::Get(this))A->EndAction(SwingA01Action);SwingA01Action={};}
    if(SwingClock.bActive&&GetWorld())GatheringPoseHoldUntil=GetWorld()->GetTimeSeconds()+
        FMath::Clamp(DWGatheringAction::Finite(GatheringPoseBlendHoldSeconds,.12f),0.f,.5f);
    ++SwingGeneration;SwingClock.Cancel();SwingTarget.Reset();SwingInventory.Reset();
    HideGatheringToolVisual();SetComponentTickEnabled(false);
    // Keep the outgoing sequence for the existing AnimBP's short transition blend.
}
void UDWGatheringComponent::Deactivate()
{
    CancelGatheringSwing();Super::Deactivate();
}
// Kept separate from attack/corruption code so the aggregate restores one tool owner.
bool UDWGatheringComponent::CaptureGatheringPersistence(FName& OutTool,double& OutCooldown,FString& Error)
{
    // Saving intentionally cancels an incomplete demolition; progress is never serialized.
    if(Demolition.Active())CancelDemolition();
    if(!GetWorld()||SwingClock.bActive||!FMath::IsFinite(NextToolTime))
    {Error=TEXT("Wait for the current gathering swing to finish before saving.");return false;}
    OutTool=ActiveTool;OutCooldown=FMath::Max(0.,NextToolTime-GetWorld()->GetTimeSeconds());Error.Reset();return true;
}
void UDWGatheringComponent::AdoptGatheringPersistenceFacts(FName ItemId,double CooldownRemaining)
{
    Demolition.Cancel();bDemolitionSwing=false;
    ++SwingGeneration;SwingClock.Cancel();SwingTarget.Reset();SwingInventory.Reset();
    ActiveTool=ItemId;NextToolTime=GetWorld()->GetTimeSeconds()+CooldownRemaining;
    bWasHeld=false;LastTarget.Reset();GatheringPoseHoldUntil=0.;ActiveGatheringSequence=nullptr;ActiveVisualSamples.Reset();
}
void UDWGatheringComponent::PublishGatheringPersistencePresentation()
{
    const TWeakObjectPtr<UDWGatheringComponent> SafeThis(this);EndDemolitionFeedback();if(!SafeThis.IsValid())return;HideGatheringToolVisual();SetComponentTickEnabled(false);bToolCarrySelected=true;
}

bool UDWGatheringComponent::IsDemolitionTarget(const ADWGatheringStation* Target) const
{
    const auto* User=Player();const auto* Config=Profile();
    const auto* Tool=Config?Config->Tools.Find(ActiveTool):nullptr;
    if(!User||!Tool||Tool->ToolType!=TEXT("Hammer")||!User->GetInventory()||User->GetInventory()->CountItem(ActiveTool)<1||
       !IsValid(Target)||Target->bDemolished||
       (Target->bRuntimeSpawned?(Target->DemolitionCostOrigin!=1&&Target->DemolitionCostOrigin!=3&&Target->DemolitionCostOrigin!=4):!Target->bAllowAuthoredDemolition)||
       Target->IsStaged()||Target->IsActorBeingDestroyed()||
       !Target->IsWithinReach(User,Tool->ReachCm)||Target->BuildingItemId.IsNone()||
       !Config->GetBuildingDefinition(Target->BuildingItemId))return false;
    if(const auto* Building=Cast<ADWPlaceableBuilding>(Target))return Building->HasCompleteBuildingVisuals();
    return Target->IsInteractionAvailable();
}

EDWGatheringWorkKind UDWGatheringComponent::GetWorkKind()const
{return Demolition.Active()?EDWGatheringWorkKind::Demolition:EDWGatheringWorkKind::None;}
float UDWGatheringComponent::GetDemolitionProgress()const{return static_cast<float>(Demolition.Progress());}
AActor* UDWGatheringComponent::GetDemolitionTarget()const{return Demolition.GetTarget();}
float UDWGatheringComponent::GetEffectiveToolSwingSeconds(FName ToolId)const
{
    const auto* Config=Profile();const auto* Tool=Config?Config->Tools.Find(ToolId):nullptr;
    if(!Tool)return 0.f;
    const bool bAnimated=Tool->ToolType==TEXT("Axe")||Tool->ToolType==TEXT("Pickaxe")||Tool->ToolType==TEXT("Hammer");
    const double Interval=FMath::Max(.05f,DWGatheringAction::Finite(Tool->UseIntervalSeconds,1.f));
    const double Baseline=bAnimated?FMath::Max(FMath::Clamp(DWGatheringAction::Finite(MinimumSwingSeconds,.8f),.1f,10.f),Interval):Interval;
    double Duration=0.;return DWGatheringActionTiming::TryScaleDuration(Baseline,Tool->GatheringSpeedMultiplier,Duration)?float(Duration):0.f;
}
void UDWGatheringComponent::CancelDemolition()
{
    Demolition.Cancel();const TWeakObjectPtr<UDWGatheringComponent> SafeThis(this);const uint64 Generation=SwingGeneration;
    EndDemolitionFeedback();if(SafeThis.IsValid()&&SwingGeneration==Generation&&bDemolitionSwing)StopGatheringSwingOnly();
}
void UDWGatheringComponent::BeginDemolitionFeedback()
{
    auto* User=Player();auto* Target=Demolition.GetTarget();auto* PC=User?Cast<ADWPlayerController>(User->GetController()):nullptr;
    if(!User||!Target||!PC||!Target->Prompt)return;
    DemolitionPrompt=Target->Prompt;DemolitionPromptId=FGuid::NewGuid();
    const TWeakObjectPtr<UDWGatheringComponent> SafeThis(this);
    if(!DemolitionPrompt->BeginDemolitionSession(User,DemolitionPromptId,PC->GetAppliedActionKey(EDWInputAction::Harvest)))
        if(SafeThis.IsValid()){DemolitionPrompt.Reset();DemolitionPromptId.Invalidate();}
}
void UDWGatheringComponent::UpdateDemolitionFeedback()
{
    if(!DemolitionPrompt.IsValid()||!DemolitionPromptId.IsValid())return;
    const TWeakObjectPtr<UDWGatheringComponent> SafeThis(this);
    if(DemolitionPrompt->UpdateDemolitionSession(DemolitionPromptId,GetDemolitionProgress()))return;
    if(!SafeThis.IsValid())return;
    auto* User=Player();
    if(!User||!Demolition.Current(*User,Cast<ADWGatheringStation>(FindTarget()),ActiveTool,bWasHeld))return;
    EndDemolitionFeedback();if(!SafeThis.IsValid())return;
    User=Player();if(!User||!Demolition.Current(*User,Cast<ADWGatheringStation>(FindTarget()),ActiveTool,bWasHeld))return;
    BeginDemolitionFeedback();if(!SafeThis.IsValid())return;
    if(DemolitionPrompt.IsValid()&&DemolitionPromptId.IsValid())
        DemolitionPrompt->UpdateDemolitionSession(DemolitionPromptId,GetDemolitionProgress());
}
void UDWGatheringComponent::EndDemolitionFeedback()
{
    const auto Prompt=DemolitionPrompt;const FGuid Id=DemolitionPromptId;
    DemolitionPrompt.Reset();DemolitionPromptId.Invalidate();
    if(Prompt.IsValid()&&Id.IsValid())Prompt->EndDemolitionSession(Id);
}

bool UDWGatheringComponent::StartDemolitionSwing()
{
    auto* User=Player();auto* Target=Demolition.GetTarget();const auto* Tool=Profile()?Profile()->Tools.Find(ActiveTool):nullptr;
    auto* Motion=HammerMotion?HammerMotion.Get():SharedTwoHandMotion.Get();
    const auto* Clip=Motion?Motion->FindClip(HammerMotion?HammerMotionRole:SharedMotionRole):nullptr;
    if(!User||!Target||!Tool||!Clip||!Clip->Animation||Clip->VisualSamples.IsEmpty()||
       !FMath::IsFinite(Clip->DurationSeconds)||Clip->DurationSeconds<=.01f)return false;
    double Duration=0.;const double Baseline=FMath::Max(FMath::Clamp(DWGatheringAction::Finite(MinimumSwingSeconds,.8f),.1f,10.f),
        FMath::Max(.05f,DWGatheringAction::Finite(Tool->UseIntervalSeconds,1.f)));
    if(!DWGatheringActionTiming::TryScaleDuration(Baseline,Tool->GatheringSpeedMultiplier,Duration))return false;
    const float SourceLength=Clip->DurationSeconds;
    SwingTiming.SourceStart=FMath::Clamp(DWGatheringAction::Finite(SourceStartSeconds,.75f),0.f,SourceLength-.003f);
    SwingTiming.SourceRaised=FMath::Clamp(DWGatheringAction::Finite(SourceRaisedSeconds,1.6f),float(SwingTiming.SourceStart)+.001f,SourceLength-.002f);
    SwingTiming.SourceEnd=FMath::Clamp(DWGatheringAction::Finite(SourceEndSeconds,1.95f),float(SwingTiming.SourceRaised)+.001f,SourceLength);
    SwingTiming.SourceContact=FMath::Clamp(DWGatheringAction::Finite(SourceContactSeconds,1.8f),float(SwingTiming.SourceRaised),float(SwingTiming.SourceEnd));
    SwingTiming.Windup=FMath::Clamp(DWGatheringAction::Finite(WindupFraction,.4f),.05f,.65f);
    SwingTiming.Downstroke=FMath::Clamp(DWGatheringAction::Finite(DownstrokeFraction,.15f),.05f,float(.85-SwingTiming.Windup));
    SwingTiming.Hold=FMath::Clamp(DWGatheringAction::Finite(ImpactHoldFraction,.09f),0.f,float(.9-SwingTiming.Windup-SwingTiming.Downstroke));
    SwingToolId=ActiveTool;SwingToolType=Tool->ToolType;SwingSharpness=Tool->Sharpness;SwingReach=Tool->ReachCm;
    ActiveGatheringSequence=Clip->Animation;ActiveVisualSamples=Clip->VisualSamples;SwingSourceDuration=SourceLength;
    bActiveDirectGatheringSource=bUseDirectGatheringSource;ActiveSourceSeconds=bActiveDirectGatheringSource?0.f:float(SwingTiming.SourceStart);
    GatheringPoseHoldUntil=0.;const double Contact=bActiveDirectGatheringSource?
        FMath::Clamp(DWGatheringAction::Finite(GatheringContactFraction,.4857142857f),.01f,.99f):SwingTiming.ContactProgress();
    bDemolitionSwing=true;++SwingGeneration;SwingClock.Start(Duration,Contact);
    NextToolTime=GetWorld()->GetTimeSeconds()+Duration;
    const FVector Facing=(Target->GetActorLocation()-User->GetActorLocation()).GetSafeNormal2D();
    if(!Facing.IsNearlyZero())User->SetActorRotation(Facing.Rotation());
    SetComponentTickEnabled(true);return true;
}

bool UDWGatheringComponent::TryCommitDemolition()
{
    auto* User=Player();auto* Target=Demolition.GetTarget();if(!User||!Target||!Demolition.Ready())return false;
    const TWeakObjectPtr<UDWGatheringComponent> SafeThis(this);FString Error;
    // User confirmed materials are returned as world drops at the building location.
    const bool Committed=FDWBuildingDemolitionTransaction::Commit(*User,*Target,EDWBuildingDemolitionReturn::MaterialsToWorld,
        [this,User,Target](){return IsValid(this)&&Demolition.Current(*User,Cast<ADWGatheringStation>(FindTarget()),ActiveTool,bWasHeld)&&Demolition.GetTarget()==Target;},
        [this](){return IsValid(this)&&Demolition.ConsumeCompletion();},Error);
    if(!SafeThis.IsValid())return Committed;
    if(!Committed&&!Error.IsEmpty())Notify(User,TEXT("拆除未完成，请检查返还配置或背包空间。"),Error);
    return Committed;
}


