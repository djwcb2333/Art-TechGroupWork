#include "DWWeaponAnimationComponent.h"
#include "DWSimpleSpearPolicy.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWInventoryComponent.h"
#include "DWGatheringComponent.h"
#include "DWGameplayConfig.h"
#include "DWPlayerCharacter.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "DWWeaponVisualSampler.h"

struct FDWSimpleSpearSource
{
 TWeakObjectPtr<UDWInventoryComponent> Inventory;TWeakObjectPtr<UDWGameplayConfig> Config;
 FDWInventorySlotSnapshot Slot;FDWGatheringToolDefinition Tool;
 DWSimpleSpearPolicy::FFacts Facts;FVector2D Fractions;float Damage=0.;
 TWeakObjectPtr<UAnimSequenceBase> Sequence;float SourceLength=0.;
};
bool UDWWeaponAnimationComponent::HasSimpleSpearSelected()const
{
 auto* P=Player();return P&&P->GatheringComponent&&
  P->GatheringComponent->GetActiveTool()==DWSimpleSpearPolicy::Item()&&
  SelectedWeapon==EDWAnimatedWeapon::Unarmed&&CombatWeaponResources.Snapshot.CurrentEquipmentId.IsNone();
}
void UDWWeaponAnimationComponent::ClearSimpleSpearToolSelection()
{
 auto* P=Player();if(P&&P->GatheringComponent)P->GatheringComponent->ClearToolCarryPresentation();
 if(!P||!P->GatheringComponent||P->GatheringComponent->GetActiveTool()!=DWSimpleSpearPolicy::Item())return;
 if(SimpleSpearSource.IsValid())CancelAction();
 P=Player();if(P&&P->GatheringComponent)P->GatheringComponent->ClearActiveToolAfterShortcutForgotten();
}
bool UDWWeaponAnimationComponent::PrepareSimpleSpearSource(FDWSpearTuning& Tuning,FDWWeaponAnimationClip& Clip,
 TSharedPtr<FDWSimpleSpearSource>& Out)const
{
 auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);auto* Bag=P?P->GetInventory():nullptr;
 auto* Config=P?P->GetGameplayConfig():nullptr;
 if(!HasSimpleSpearSelected()||!P||!P->IsCombatActionAllowed()||!P->HasCombatVitalsReady()||P->IsDead()||
    !Bag||!A||!A->IsOperational()||!Config||!bFormalSpearConfigured||!HasCurrentEquipmentIdentity())return false;
 const auto* Tool=Config->Tools.Find(DWSimpleSpearPolicy::Item());
 if(!Tool||Tool->ToolType!=TEXT("Spear"))return false;
 FDWInventorySlotSnapshot Slot;bool Found=false;
 for(int32 Index=0;Index<Bag->GetSlots().Num();++Index)
  if(Bag->GetSlots()[Index].ItemId==DWSimpleSpearPolicy::Item()&&Bag->CaptureSlotSnapshot(Index,Slot)&&Slot.Quantity>0){Found=true;break;}
 if(!Found||!Bag->IsSlotSnapshotCurrent(Slot))return false;
 const UDWWeaponAnimationSet* SourceSet=nullptr;
 for(const UDWWeaponAnimationSet* Set:AnimationSets)if(Set&&Set->Weapon==EDWAnimatedWeapon::Spear){SourceSet=Set;break;}
 const auto* SourceClip=SourceSet?SourceSet->FindClip(TEXT("Attack")):nullptr;
 const auto* Fractions=AttackSourcePhaseEnds.Find(EDWAnimatedWeapon::Spear);
 UAnimSequenceBase* Sequence=SourceClip?SourceClip->Animation.Get():nullptr;
 if(!SourceClip||!Fractions||!IsValid(Sequence)||Sequence!=SpearSourceSequence.Get()||Sequence->IsA<UAnimMontage>()||
    Sequence->GetAdditiveAnimType()!=AAT_None||!FMath::IsFinite(SourceClip->DurationSeconds)||SourceClip->DurationSeconds<=0.||
    !FMath::IsNearlyEqual(Sequence->GetPlayLength(),SourceClip->DurationSeconds,1.e-6f))return false;
 const auto* Mesh=P->GetMesh()?P->GetMesh()->GetSkeletalMeshAsset():nullptr;
 const auto* Skeleton=Mesh?Mesh->GetSkeleton():nullptr;bool Compatible=Skeleton&&Sequence->GetSkeleton()==Skeleton;
 if(Skeleton&&!Compatible)for(const auto& Entry:Skeleton->GetCompatibleSkeletons())if(Entry.ToSoftObjectPath()==FSoftObjectPath(Sequence->GetSkeleton())){Compatible=true;break;}
 if(!Compatible||!DWSimpleSpearPolicy::Build(SimpleSpearDamage,Tool->UseIntervalSeconds,Tool->ReachCm,SpearTuning.FullArcDegrees,
   *Fractions,UnarmedTuning.Attack.RootedDamageMultiplier,Tuning))return false;
 auto Source=MakeShared<FDWSimpleSpearSource>();Source->Inventory=Bag;Source->Config=Config;Source->Slot=Slot;Source->Tool=*Tool;
 Source->Damage=SimpleSpearDamage;Source->Fractions=*Fractions;Source->Sequence=Sequence;Source->SourceLength=SourceClip->DurationSeconds;
 auto& F=Source->Facts;const auto Life=A->CaptureActorLife(P);
 F.ItemId=Slot.ItemId;F.Slot=Slot.SlotIndex;F.Quantity=Slot.Quantity;F.InventoryRevision=Slot.Revision;
 F.EquipmentRevision=CombatWeaponResources.Revision;F.Restore=CombatWeaponResources.RestoreEpoch;F.Epoch=A->GetWorldEpoch();
 F.Entity=Life.Entity;F.Life=Life.Life;F.bSelected=F.bAlive=true;
 if(!IsSimpleSpearSourceCurrent(Source))return false;
 Clip=*SourceClip;Out=MoveTemp(Source);return true;
}
bool UDWWeaponAnimationComponent::IsSimpleSpearSourceCurrent(const TSharedPtr<FDWSimpleSpearSource>& Source,bool PresentationOnly)const
{
 auto* P=Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
 auto* Bag=P?P->GetInventory():nullptr;auto* Config=P?P->GetGameplayConfig():nullptr;
 if(!Source||!IsValid(this)||IsBeingDestroyed()||!P||P->IsActorBeingDestroyed()||!P->GetWorld()||P->GetWorld()->bIsTearingDown||
    !A||!A->IsOperational()||!Bag||Bag!=Source->Inventory.Get()||Bag->GetOwner()!=P||Config!=Source->Config.Get()||
    !Config||!Bag->IsSlotSnapshotCurrent(Source->Slot)||!HasCurrentEquipmentIdentity()||SimpleSpearDamage!=Source->Damage)return false;
 const auto* Tool=Config->Tools.Find(DWSimpleSpearPolicy::Item());const auto* Fractions=AttackSourcePhaseEnds.Find(EDWAnimatedWeapon::Spear);
 if(!Tool||Tool->ToolType!=Source->Tool.ToolType||Tool->Sharpness!=Source->Tool.Sharpness||
    Tool->UseIntervalSeconds!=Source->Tool.UseIntervalSeconds||Tool->ReachCm!=Source->Tool.ReachCm||
    Tool->GatheringSpeedMultiplier!=Source->Tool.GatheringSpeedMultiplier||!Fractions||*Fractions!=Source->Fractions||
    !Source->Sequence.IsValid()||Source->Sequence.Get()!=SpearSourceSequence.Get())return false;
 FDWInventorySlotSnapshot ActualSlot;if(!Bag->CaptureSlotSnapshot(Source->Slot.SlotIndex,ActualSlot))return false;
 auto Current=Source->Facts;const auto Life=A->CaptureActorLife(P);
 Current.ItemId=ActualSlot.ItemId;Current.Slot=ActualSlot.SlotIndex;Current.Quantity=ActualSlot.Quantity;Current.InventoryRevision=ActualSlot.Revision;
 Current.EquipmentRevision=CombatWeaponResources.Revision;Current.Restore=CombatWeaponResources.RestoreEpoch;Current.Epoch=A->GetWorldEpoch();
 Current.Entity=Life.Entity;Current.Life=Life.Life;Current.bSelected=HasSimpleSpearSelected();
 Current.bAlive=P->HasCombatVitalsReady()&&!P->IsDead()&&!P->IsCombatRestoreInProgress()&&(PresentationOnly||P->IsCombatActionAllowed());
 if(Source->Facts.Action)
 {
  Current.Action=UnarmedAction.Id;
  if(!bActive||!bFormalUnarmedActive||UnarmedAction.Epoch!=Source->Facts.Epoch||
     !A->IsActionOwnedBy(UnarmedAction,P)||ActiveClip.Animation!=Source->Sequence.Get()||ActiveClip.DurationSeconds!=Source->SourceLength)return false;
 }
 return DWSimpleSpearPolicy::Current(Source->Facts,Current);
}
void UDWWeaponAnimationComponent::AdoptSimpleSpearSource(TSharedPtr<FDWSimpleSpearSource> Source,DWCombatCommit::FActionId Action)
{
 if(Source)Source->Facts.Action=Action.Id;SimpleSpearSource=MoveTemp(Source);
}
FVector2D UDWWeaponAnimationComponent::GetSimpleSpearSourceFractions()const
{return SimpleSpearSource?SimpleSpearSource->Fractions:FVector2D(.2,.38);}
bool UDWWeaponAnimationComponent::GetCommittedToolAttackSource(DWCombatCommit::FActionId Action,FName& Out)const
{
 Out=NAME_None;
 if(!SimpleSpearSource||UnarmedPlan.GetPhase()!=EDWSpearPhase::Active||!Action.Id||Action.Id!=UnarmedAction.Id||Action.Epoch!=UnarmedAction.Epoch||
    SimpleSpearSource->Facts.Action!=Action.Id||!IsSimpleSpearSourceCurrent(SimpleSpearSource))return false;
 Out=DWSimpleSpearPolicy::Item();return true;
}
bool UDWWeaponAnimationComponent::GetSimpleSpearVisualGrip(FTransform& Out)const
{
 auto* P=Player();
 if(!SimpleSpearSource||!P||!IsSimpleSpearSourceCurrent(SimpleSpearSource,true))return false;
 const UDWWeaponAnimationSet* Set=nullptr;
 for(const UDWWeaponAnimationSet* Entry:AnimationSets)if(Entry&&Entry->Weapon==EDWAnimatedWeapon::Spear){Set=Entry;break;}
 if(!Set)return false;
 DWWeaponVisualSampler::FRequest Request;Request.SourceSeconds=GetPresentationSourceSeconds();
 Request.SelectedWeapon=EDWAnimatedWeapon::Spear;Request.bPresentationActive=Request.bActionActive=true;Request.OwnerToWorld=P->GetActorTransform();
 if(const auto* Mesh=P->GetMesh())
 {
  Request.bHasMesh=true;Request.bHasPrimarySocket=Mesh->DoesSocketExist(Set->PrimaryBone);Request.bHasSecondarySocket=Mesh->DoesSocketExist(Set->SecondaryBone);
  if(Request.bHasPrimarySocket)Request.PrimarySocketToWorld=Mesh->GetSocketTransform(Set->PrimaryBone);
  if(Request.bHasSecondarySocket)Request.SecondarySocketToWorld=Mesh->GetSocketTransform(Set->SecondaryBone);
 }
 const auto Frame=DWWeaponVisualSampler::EvaluateWeaponVisualFrame(*Set,ActiveClip,Request);
 // The sampled prop frame is actor-local. PrimaryToOwner also contains the permanent mesh grip/scale.
 if(!Frame.bPrimaryVisible||!Frame.bHeld)return false;
 const auto* Mesh=P->GetMesh();const FName GripHand(TEXT("bone_005_R"));
 if(!Mesh||!Mesh->DoesSocketExist(GripHand))return false;
 if(Frame.bHasSample)Out=Frame.Sample.PrimaryTransform;
 else if(Request.bHasPrimarySocket)Out=Request.PrimarySocketToWorld*Request.OwnerToWorld.Inverse();
 else return false;
 // The prop bone origin is off the palm. Keep its authored aim and anchor this model at the actual hand.
 FTransform VisibleHand;
 if(!P->GatheringComponent||!P->GatheringComponent->GetToolHandFrame(VisibleHand))return false;
 Out.SetTranslation(VisibleHand.GetTranslation());
 return !Out.ContainsNaN();
}
