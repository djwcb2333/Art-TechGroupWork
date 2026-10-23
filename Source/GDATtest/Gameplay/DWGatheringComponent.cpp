#include "DWGatheringComponent.h"
#include "DWGatheringWorld.h"
#include "DWGatheringPond.h"
#include "DWGatheringProfile.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWGameplayHUD.h"
#include "DWLocalizationLibrary.h"
#include "DWInteractionPromptComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

UDWGatheringComponent::UDWGatheringComponent(){PrimaryComponentTick.bCanEverTick=false;}
ADWPlayerCharacter* UDWGatheringComponent::Player() const {return Cast<ADWPlayerCharacter>(GetOwner());}
const UDWGatheringProfile* UDWGatheringComponent::Profile() const {return Player()?Cast<UDWGatheringProfile>(Player()->GetGameplayConfig()):nullptr;}
bool UDWGatheringComponent::IsGatheringEnabled() const {return Profile()!=nullptr;}
UDWGatheringComponent* UDWGatheringComponent::Find(const UDWInventoryComponent& Inventory)
{return Inventory.GetOwner()?Inventory.GetOwner()->FindComponentByClass<UDWGatheringComponent>():nullptr;}
void UDWGatheringComponent::InitializeGathering()
{
    if(!Profile()||!Player()||!Player()->GetInventory())return;
    const TWeakObjectPtr<UDWGatheringComponent> Weak(this);
    Player()->GetInventory()->SetCraftingContextValidator([Weak](FName Recipe,FName Station,FName Building)
    {return Weak.IsValid()&&Weak->HasCraftingContext(Recipe,Station,Building);});
}
void UDWGatheringComponent::EndPlay(const EEndPlayReason::Type Reason)
{
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
    return Profile()&&IsValid(User)&&!User->IsActorBeingDestroyed()&&User->CanPerformGatheringAction()&&
        PC&&!PC->IsGameplayBlocked()&&GetWorld()&&!GetWorld()->bIsTearingDown&&!GetWorld()->IsPaused();
}
bool UDWGatheringComponent::TryUseTool(FName Id)
{
    const auto* Config=Profile();auto* User=Player();
    if(!Config||!IsValid(User)||User->IsDead()||!User->GetInventory()||User->GetInventory()->CountItem(Id)<1||!Config->Tools.Contains(Id))return false;
    ActiveTool=Id; // Selecting again must not bypass the shared tool cooldown.
    const auto* Definition=Config->GetItemDefinition(Id);
    const FString CN=Definition?Definition->DisplayName.ToString():Id.ToString();
    const FString EN=Definition?Definition->EnglishDisplayName.ToString():Id.ToString();
    Notify(User,TEXT("已选择采集工具：")+CN,TEXT("Gathering tool selected: ")+EN);return true;
}
void UDWGatheringComponent::RestoreActiveTool(FName Id)
{
    ActiveTool=Profile()&&Player()->GetInventory()&&Profile()->Tools.Contains(Id)&&Player()->GetInventory()->CountItem(Id)>0?Id:NAME_None;
    bWasHeld=false;LastTarget.Reset();NextToolTime=0.;
}
AActor* UDWGatheringComponent::FindTarget() const
{
    auto* User=Player();if(!User||!GetWorld())return nullptr;
    AActor* Best=nullptr;double Distance=TNumericLimits<double>::Max();
    for(TActorIterator<ADWGatheringInteractable> It(GetWorld());It;++It)
    {
        if(!It->IsInteractionAvailable()||!It->IsWithinReach(User))continue;
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
    if(!CanAct()){bWasHeld=false;LastTarget.Reset();return true;}
    AActor* Target=FindTarget();const bool Pressed=Held&&!bWasHeld;bWasHeld=Held;
    if(LastTarget.Get()!=Target){LastTarget=Target;NextFailureNotice=0.;}
    if(Held&&Target)InteractWith(Target,Pressed);
    return true;
}
bool UDWGatheringComponent::InteractWith(AActor* Target,bool Pressed)
{
    auto* User=Player();if(!CanAct()||!IsValid(Target))return false;
    if(auto* Pickup=Cast<ADWGatheringPickup>(Target))return Pressed&&Pickup->TryCollect(User);
    if(auto* Resource=Cast<ADWGatheringResource>(Target))
    {
        const double Now=GetWorld()->GetTimeSeconds();if(Now<NextToolTime)return false;
        const auto* Tool=Profile()->Tools.Find(ActiveTool);
        if(!Tool||User->GetInventory()->CountItem(ActiveTool)<1||Tool->ToolType!=Resource->RequiredTool)
        {
            if(Now>=NextFailureNotice){NextFailureNotice=Now+1.5;Notify(User,TEXT("请先在背包中使用对应的斧或镐"),TEXT("Use the matching axe or pickaxe from your inventory first."));}
            return false;
        }
        NextToolTime=Now+FMath::Max(.05f,Tool->UseIntervalSeconds);
        const FVector Facing=(Resource->GetActorLocation()-User->GetActorLocation()).GetSafeNormal2D();
        if(!Facing.IsNearlyZero())User->SetActorRotation(Facing.Rotation());
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
    auto* PC=Cast<APlayerController>(Player()->GetController());FHitResult Hit;
    if(!PC||!PC->GetHitResultUnderCursor(ECC_Visibility,false,Hit))return false;
    AActor* Target=Hit.GetActor();
    if(auto* Object=Cast<ADWGatheringInteractable>(Target))
    {
        if(!Object->IsInteractionAvailable()||!Object->IsWithinReach(Player()))return false;
        // A recognized in-range target consumes the click even when full or wrong tool.
        InteractWith(Object,true);return true;
    }
    if(auto* Pond=Cast<ADWGatheringPond>(Target))if(Pond->IsUsableWaterStation(Player())){InteractWith(Pond,true);return true;}
    return false;
}
bool UDWGatheringComponent::HasCraftingContext(FName Recipe,FName Station,FName Building) const
{
    const auto* Config=Profile();auto* User=Player();
    if(!Config||!User||User->IsDead()||User->IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown)return false;
    bool Found=Station.IsNone();
    if(Station==TEXT("GatherStation_WaterPond"))
    {for(TActorIterator<ADWGatheringPond> It(GetWorld());It;++It)if(It->IsUsableWaterStation(User)){Found=true;break;}}
    else if(!Station.IsNone())
    {for(TActorIterator<ADWGatheringStation> It(GetWorld());It;++It)if(It->StationId==Station&&It->IsInteractionAvailable()&&It->IsWithinReach(User)){Found=true;break;}}
    if(!Found)return false;
    if(!Building.IsNone()){FTransform Placement;return FindBuildingPlacement(Building,Placement);}
    return true;
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
