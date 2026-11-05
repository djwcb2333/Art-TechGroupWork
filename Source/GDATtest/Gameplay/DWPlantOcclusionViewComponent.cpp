#include "DWPlantOcclusionViewComponent.h"
#include "DWGameplayHUD.h"
#include "DWWorldEvent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
 TMap<TWeakObjectPtr<UWorld>,TWeakObjectPtr<UDWPlantOcclusionViewComponent>> PlantViewOwners;
}
UDWPlantOcclusionViewComponent::UDWPlantOcclusionViewComponent()
{
 bAutoActivate=true;
 PrimaryComponentTick.bCanEverTick=true;
 PrimaryComponentTick.bTickEvenWhenPaused=true;
 PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
 Parameters=FSoftObjectPath(TEXT("/Game/DoughWorld/Fab/SoStylized/Environment/MPC_GlobalEnvironment.MPC_GlobalEnvironment"));
}
void UDWPlantOcclusionViewComponent::BeginPlay()
{
 Super::BeginPlay();
 if(GetWorld()&&GetWorld()->IsGameWorld()&&GetNetMode()!=NM_DedicatedServer)
  LoadedParameters=Parameters.LoadSynchronous(); // One existing-protocol collection; never loads in Tick.
}
void UDWPlantOcclusionViewComponent::ClearOwnedParameters()
{
 UWorld* World=GetWorld();auto* Owner=PlantViewOwners.Find(World);
 if(!Owner||Owner->Get()!=this)return;
 if(World&&!World->bIsTearingDown&&LoadedParameters)
  if(auto* Instance=World->GetParameterCollectionInstance(LoadedParameters))
   Instance->SetScalarParameterValue(TEXT("DWPlantOcclusionEnabled"),0.f);
 PlantViewOwners.Remove(World);LastStrength=-1.f;
}
void UDWPlantOcclusionViewComponent::SetPlantOcclusionEnabled(bool bEnable)
{
 bEnabled=bEnable;
 if(!bEnable)ClearOwnedParameters();
}
void UDWPlantOcclusionViewComponent::Deactivate()
{
 ClearOwnedParameters();Super::Deactivate();
}
void UDWPlantOcclusionViewComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
 UWorld* World=GetWorld();auto* Pawn=Cast<APawn>(GetOwner());auto* PC=Pawn?Cast<APlayerController>(Pawn->GetController()):nullptr;
 const auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 if(!World||!World->IsGameWorld()||World->bIsTearingDown||GetNetMode()==NM_DedicatedServer||!IsActive()||!bEnabled||
    !IsValid(Pawn)||Pawn->IsActorBeingDestroyed()||!PC||!PC->IsLocalController()||
    PC!=UGameplayStatics::GetPlayerController(World,0)||PC->GetPawn()!=Pawn||PC->GetViewTarget()!=Pawn||
    !HUD||!HUD->IsSessionStarted()||HUD->GetMenuPage()!=EDWMenuPage::None||UDWWorldEventSubsystem::IsPlaying(this)||!LoadedParameters)
 {ClearOwnedParameters();return;}
 auto* Other=PlantViewOwners.Find(World);
 if(Other&&Other->IsValid()&&Other->Get()!=this&&Other->Get()->GetOwner()==Pawn)return;
 const FVector Position=Pawn->GetActorLocation();
 if(Position.ContainsNaN()){ClearOwnedParameters();return;}
 auto* Instance=World->GetParameterCollectionInstance(LoadedParameters);if(!Instance){ClearOwnedParameters();return;}
 const bool bNewOwner=!Other||Other->Get()!=this;
 const float AppliedStrength=FMath::IsFinite(Strength)?FMath::Clamp(Strength,0.f,1.f):0.f;
 if(bNewOwner||!Position.Equals(LastPosition,.001))
 {Instance->SetVectorParameterValue(TEXT("DWPlantOcclusionPosition"),FLinearColor(Position.X,Position.Y,Position.Z,1.f));LastPosition=Position;}
 if(bNewOwner||AppliedStrength!=LastStrength)
 {Instance->SetScalarParameterValue(TEXT("DWPlantOcclusionEnabled"),AppliedStrength);LastStrength=AppliedStrength;}
 PlantViewOwners.Add(World,this);
}
void UDWPlantOcclusionViewComponent::EndPlay(const EEndPlayReason::Type Reason)
{ClearOwnedParameters();Super::EndPlay(Reason);}
void UDWPlantOcclusionViewComponent::OnUnregister()
{ClearOwnedParameters();Super::OnUnregister();}
