#include "DWOpeningExitVolume.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

ADWOpeningExitVolume::ADWOpeningExitVolume()
{
 PrimaryActorTick.bCanEverTick=false;
 if(auto* Box=Cast<UBoxComponent>(GetCollisionComponent()))
 {
  Box->SetBoxExtent(FVector(140.f,140.f,180.f));
  Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Box->SetCollisionResponseToAllChannels(ECR_Ignore);
  Box->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
  Box->SetGenerateOverlapEvents(false); // Director polls containment, including already-inside entry.
  Box->SetCanEverAffectNavigation(false);
  Box->ShapeColor=FColor(255,190,40);
 }
}

bool ADWOpeningExitVolume::IsValidBoxGeometry(const FTransform& Transform,const FVector& Extent)
{
 if(Transform.ContainsNaN()||Extent.ContainsNaN())return false;
 return Transform.GetScale3D().GetMin()>SMALL_NUMBER&&Extent.GetMin()>0.;
}

bool ADWOpeningExitVolume::IsAvailable(FString& Reason) const
{
 if(!bExitVolumeEnabled||!GetActorEnableCollision()){Reason=TEXT("Exit Volume disabled / 出口体积已关闭");return false;}
 const auto* Box=Cast<UBoxComponent>(GetCollisionComponent());
 if(!Box||!Box->IsRegistered()||!Box->IsQueryCollisionEnabled()){Reason=TEXT("Box not registered or query disabled / 盒未注册或查询碰撞已关闭");return false;}
 if(!IsValidBoxGeometry(Box->GetComponentTransform(),Box->GetUnscaledBoxExtent()))
 {Reason=TEXT("Invalid Box extent or scale / 盒尺寸或缩放无效");return false;}
 Reason=TEXT("Box configured / 出口盒配置有效");return true;
}

bool ADWOpeningExitVolume::ContainsExitPlayer(const APawn* Candidate,const APlayerController* Controller,FString& Reason) const
{
 if(!IsAvailable(Reason))return false;
 if(!IsValid(Candidate)||!IsValid(Controller)||Candidate->GetWorld()!=GetWorld()||Controller->GetWorld()!=GetWorld()||Controller!=UGameplayStatics::GetPlayerController(this,0)||Controller->GetPawn()!=Candidate)
 {Reason=TEXT("Not this level's player / 不是本关玩家（忽略敌人与其他重叠物）");return false;}
 const auto* Box=CastChecked<UBoxComponent>(GetCollisionComponent());
 const auto* Character=Cast<ACharacter>(Candidate);
 const auto* Capsule=Character?Character->GetCapsuleComponent():nullptr;
 if(!Capsule||!Capsule->IsRegistered()||!Capsule->IsQueryCollisionEnabled()||Capsule->GetScaledCapsuleRadius()<=0.f||Capsule->GetScaledCapsuleHalfHeight()<=0.f||Capsule->GetComponentTransform().ContainsNaN())
 {Reason=TEXT("Player capsule invalid / 玩家胶囊未注册、禁用或尺寸无效");return false;}
 if(!Box->OverlapComponent(Capsule->GetComponentLocation(),Capsule->GetComponentQuat(),Capsule->GetCollisionShape()))
 {Reason=TEXT("Player capsule outside Box / 玩家胶囊未与出口盒相交（含高度）");return false;}
 Reason=TEXT("Player capsule intersects Box / 玩家胶囊与出口盒相交");return true;
}
