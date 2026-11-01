#include "DWCombatPresentationComponent.h"
#include "DWAttackVfxProfile.h"
#include "DWAttackVfxResolver.h"
#include "DWWeaponVisualSampler.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "UObject/ObjectKey.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TimerManager.h"
namespace DWAttackVfxRuntime
{
 struct FToken
 {
  TWeakObjectPtr<AActor> Owner;TWeakObjectPtr<UWorld> World;uint64 Epoch=0;
  DWCombatCommit::FEntityLife Life;
 };
 FToken Token(AActor* Owner)
 {
  FToken T;T.Owner=Owner;T.World=Owner?Owner->GetWorld():nullptr;
  if(auto* A=UDWCombatAuthoritySubsystem::Get(Owner)){T.Epoch=A->GetWorldEpoch();T.Life=A->FindRegisteredActorLife(Owner);}return T;
 }
 bool Current(const FToken& T)
 {
  auto* O=T.Owner.Get();auto* W=T.World.Get();if(!O||!W||O->GetWorld()!=W||O->IsActorBeingDestroyed()||W->bIsTearingDown)return false;
  if(auto* A=UDWCombatAuthoritySubsystem::Get(O))
  {const auto L=A->FindRegisteredActorLife(O);return DWAttackVfxResolver::IsVisualLifetimeCurrent(T.Epoch,A->GetWorldEpoch(),T.Life.Entity,T.Life.Life,L.Entity,L.Life);}
  return T.Epoch==0;
 }
 bool UsesWeapon(EDWAttackVfxAnchor A)
 {return A==EDWAttackVfxAnchor::PrimaryWeapon||A==EDWAttackVfxAnchor::SecondaryWeapon||A==EDWAttackVfxAnchor::PrimaryAuthoredTip||A==EDWAttackVfxAnchor::SecondaryAuthoredTip||A==EDWAttackVfxAnchor::PrimaryMeshSocket||A==EDWAttackVfxAnchor::SecondaryMeshSocket;}
 bool Anchor(AActor* Owner,const FDWAttackVfxEntry& Entry,const FTransform& Commit,bool bCommitted,FTransform& Out,FObjectKey& SetKey)
 {
  if(!IsValid(Owner))return false;
  DWAttackVfxResolver::FAnchorContext C;C.OwnerToWorld=Owner->GetActorTransform();C.CommittedToWorld=Commit;C.bHasCommitted=bCommitted;
  DWWeaponVisualSampler::FFrame Frame;DWWeaponVisualSampler::FRequest Request;UDWWeaponAnimationSet* Set=nullptr;
  if(auto* Weapon=Owner->FindComponentByClass<UDWWeaponAnimationComponent>())
  {Set=Weapon->GetSelectedSet();if(UsesWeapon(Entry.Anchor)&&!Weapon->CaptureVisualSnapshot(Request,Frame))return false;}
  if(UsesWeapon(Entry.Anchor)&&!Set)return false;
  if(Set)SetKey=FObjectKey(Set);
  if(Entry.Anchor==EDWAttackVfxAnchor::CharacterBoneOrSocket)
  {
   const auto* Character=Cast<ACharacter>(Owner);const auto* Mesh=Character?Character->GetMesh():nullptr;
   if(!Mesh||!Mesh->DoesSocketExist(Entry.BoneOrSocket))return false;
   C.CharacterSocketsToWorld.Add(Entry.BoneOrSocket,Mesh->GetSocketTransform(Entry.BoneOrSocket));
  }
  if(Entry.Anchor==EDWAttackVfxAnchor::PrimaryMeshSocket||Entry.Anchor==EDWAttackVfxAnchor::SecondaryMeshSocket)
  {
   const bool Primary=Entry.Anchor==EDWAttackVfxAnchor::PrimaryMeshSocket;
   const auto* Mesh=Primary?Set->PrimaryMesh.Get():Set->SecondaryMesh.Get();const auto* Socket=Mesh?Mesh->FindSocket(Entry.BoneOrSocket):nullptr;
   if(!Socket)return false;
   (Primary?C.PrimaryMeshSockets:C.SecondaryMeshSockets).Add(Entry.BoneOrSocket,FTransform(Socket->RelativeRotation,Socket->RelativeLocation,Socket->RelativeScale));
  }
  return DWAttackVfxResolver::ResolveAnchorFrame(Entry,Frame,C,Out);
 }
 bool ScaleBinding(FDWCombatEffectBinding& B,double Radius)
 {
  FTransform Local;if(!DWAttackVfxResolver::ResolveEventLocalTransform(B,Radius,Local))return false;
  B.LocalTransform=Local;return true;
 }
 void Destroy(UFXSystemComponent* FX){if(FX){FX->Deactivate();FX->DestroyComponent();}}
}
struct FDWAttackVfxPlaybackState
{
 struct FFollow
 {
  TWeakObjectPtr<UFXSystemComponent> FX;FDWAttackVfxEntry Entry;DWAttackVfxRuntime::FToken Token;
  FObjectKey SetKey;FTransform Commit;bool bCommitted=true;
 };
 struct FWindow {bool bOpen=false;TWeakObjectPtr<UFXSystemComponent> FX;};
 TArray<FFollow> Following;TMap<FGuid,FWindow> Windows;TArray<FTimerHandle> Deferred;
 FObjectKey SetKey;FName Role;uint32 Serial=0;bool bHasWindowContext=false;
};
void FDWAttackVfxPlaybackStateDeleter::operator()(FDWAttackVfxPlaybackState* P) const {delete P;}
UDWCombatPresentationComponent::UDWCombatPresentationComponent(){PrimaryComponentTick.bCanEverTick=false;}
UFXSystemComponent* UDWCombatPresentationComponent::CreateEffect(AActor* Carrier,const FDWCombatEffectBinding& Binding,const FTransform& T,bool Attach,double HalfAngleDegrees)
{
 if(!IsValid(Carrier)||Carrier->IsActorBeingDestroyed()||!Carrier->GetWorld()||Carrier->GetWorld()->bIsTearingDown||!T.IsValid())return nullptr;
 auto* Asset=Binding.System.LoadSynchronous();UFXSystemComponent* FX=nullptr;
 if(auto* N=Cast<UNiagaraSystem>(Asset)){auto* C=NewObject<UNiagaraComponent>(Carrier);C->SetAsset(N);FX=C;}
 else if(auto* P=Cast<UParticleSystem>(Asset)){auto* C=NewObject<UParticleSystemComponent>(Carrier);C->SetTemplate(P);FX=C;}
 if(!FX)return nullptr;
 Carrier->AddInstanceComponent(FX);FX->SetAutoActivate(false);FX->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 FX->SetCanEverAffectNavigation(false);FX->SetGenerateOverlapEvents(false);
 if(Attach&&Carrier->GetRootComponent()){FX->SetupAttachment(Carrier->GetRootComponent());FX->SetRelativeTransform(T);}
 else FX->SetWorldTransform(T);
 FX->RegisterComponent();
 DWAttackVfxResolver::ApplyParameters(FX,Asset,Binding,HalfAngleDegrees);
 FX->Activate(true);return FX;
}
void UDWCombatPresentationComponent::PublishCommitted(EDWCombatPresentationKind Kind,uint64 Epoch,uint64 Id,FTransform T,uint32 Ordinal,double Radius,double HalfAngle)
{
 if(!Epoch||!Id||!GetWorld()||GetWorld()->bIsTearingDown||!GetOwner()||GetOwner()->IsActorBeingDestroyed())return;
 if(SeenEpoch!=Epoch){SeenEpoch=Epoch;Recent.Reset();}
 const FString Key=FString::Printf(TEXT("%llu/%u"),Id,Ordinal);auto& Keys=Recent.FindOrAdd(Kind);
 if(Keys.Contains(Key))return;Keys.Add(Key);if(Keys.Num()>128)Keys.RemoveAt(0);
 if(AttackVfxProfile)
  if(const auto* Group=AttackVfxProfile->EventOverrides.Find(Kind))
  {
   const auto Entries=DWAttackVfxResolver::ResolveEvents(AttackVfxProfile,Kind,nullptr); // Stable, unique data IDs across callbacks.
   for(const auto& Entry:Entries)
   {
    if(!IsValid(this)||!IsValid(GetOwner()))return;FString Error;
    if(!Entry.bEnabled||!DWAttackVfxResolver::ValidateEntryForEvent(Kind,Entry,Error))continue;
    if(Kind==EDWCombatPresentationKind::GiantSlam&&FMath::IsFinite(HalfAngle)&&HalfAngle>=1.&&HalfAngle<=180.)LastCommittedGiantHalfAngleDegrees=static_cast<float>(HalfAngle);
    SpawnVisualEntry(Entry,Kind,T,Radius,HalfAngle);
   }
   return; // Explicit empty group disables; never also play the legacy binding.
  }
 const auto* Found=Effects.Find(Kind);if(!Found)return;const auto Binding=*Found;
 if(!FMath::IsFinite(Binding.OneShotLifetimeSeconds)||Binding.OneShotLifetimeSeconds<=0.f)return;
 auto Local=Binding.LocalTransform;
 if(Radius>0.)
 {if(!FMath::IsFinite(Radius)||!FMath::IsFinite(Binding.AreaReferenceRadiusCm)||Binding.AreaReferenceRadiusCm<=0.f)return;
  const double Scale=Radius/Binding.AreaReferenceRadiusCm;Local.SetScale3D(Local.GetScale3D()*FVector(Scale,Scale,1.));}
 if(Kind==EDWCombatPresentationKind::GiantSlam&&FMath::IsFinite(HalfAngle)&&HalfAngle>=1.&&HalfAngle<=180.)
  LastCommittedGiantHalfAngleDegrees=static_cast<float>(HalfAngle);
 auto* FX=CreateEffect(GetOwner(),Binding,Local*T,false,Kind==EDWCombatPresentationKind::GiantSlam?HalfAngle:180.);if(!FX)return;
 const TWeakObjectPtr<UFXSystemComponent> Weak(FX);FTimerHandle Timer;
 GetWorld()->GetTimerManager().SetTimer(Timer,[Weak](){if(auto* C=Weak.Get()){C->Deactivate();C->DestroyComponent();}},FMath::Min(Binding.OneShotLifetimeSeconds,30.f),false);
}
bool UDWCombatPresentationComponent::MaterializeBowArea(AActor* Carrier,double Radius,double Remaining)
{
 if(AttackVfxProfile)
  if(const auto* Group=AttackVfxProfile->EventOverrides.Find(EDWCombatPresentationKind::BowSlowArea))
  {
   if(!IsValid(Carrier)||!FMath::IsFinite(Radius)||Radius<=0.||!FMath::IsFinite(Remaining)||Remaining<=0.)return false;
   bool Any=false;const auto Entries=Group->Entries;
   for(const auto& E:Entries)
   {
    FString Error;if(!E.bEnabled||E.VisualDelaySeconds!=0.f||!DWAttackVfxResolver::ValidateEntryForEvent(EDWCombatPresentationKind::BowSlowArea,E,Error))continue;
    auto B=E.Effect;if(!DWAttackVfxRuntime::ScaleBinding(B,Radius))continue;
    Any=CreateEffect(Carrier,B,B.LocalTransform,true)!=nullptr||Any;
   }
   return Any; // Same carrier retirement/restore/world lifetime; no one-shot timer.
  }
 const auto* Found=Effects.Find(EDWCombatPresentationKind::BowSlowArea);if(!Found||!FMath::IsFinite(Radius)||Radius<=0.||!FMath::IsFinite(Remaining)||Remaining<=0.)return false;
 const auto B=*Found;if(!FMath::IsFinite(B.AreaReferenceRadiusCm)||B.AreaReferenceRadiusCm<=0.f)return false;
 auto T=B.LocalTransform;T.SetScale3D(T.GetScale3D()*FVector(Radius/B.AreaReferenceRadiusCm,Radius/B.AreaReferenceRadiusCm,1.));
 return CreateEffect(Carrier,B,T,true)!=nullptr; // Carrier owns the component; retirement/restore/world teardown destroys it.
}

UDWCombatPresentationComponent::~UDWCombatPresentationComponent()=default;
UFXSystemComponent* UDWCombatPresentationComponent::SpawnVisualEntry(const FDWAttackVfxEntry& Entry,EDWCombatPresentationKind Kind,const FTransform& Commit,double Radius,double HalfAngle,bool bCommitted)
{
 using namespace DWAttackVfxRuntime;
 if(!GetWorld()||!GetWorld()->IsGameWorld()||!IsValid(GetOwner())||!Entry.bEnabled||!Entry.Effect.LocalTransform.IsValid()||
  !FMath::IsFinite(Entry.Effect.OneShotLifetimeSeconds)||Entry.Effect.OneShotLifetimeSeconds<=0.f||
  !FMath::IsFinite(Entry.VisualDelaySeconds)||Entry.VisualDelaySeconds<0.f)return nullptr;
 auto Copy=Entry;if(!ScaleBinding(Copy.Effect,Radius))return nullptr;
 FTransform Parent;FObjectKey SetKey;
 if(!Anchor(GetOwner(),Copy,Commit,bCommitted,Parent,SetKey))return nullptr;
 const auto Scope=Token(GetOwner());const auto Weak=TWeakObjectPtr<UDWCombatPresentationComponent>(this);
 if(Copy.VisualDelaySeconds>0.f)
 {
  // Positive delay is an explicitly authored new visual edit. Zero uses the old immediate path.
  if(!bCommitted||Kind==EDWCombatPresentationKind::BowSlowArea)return nullptr;
  if(!VisualState)VisualState.Reset(new FDWAttackVfxPlaybackState());
  VisualState->Deferred.RemoveAll([this](const FTimerHandle& H){return !GetWorld()->GetTimerManager().IsTimerActive(H);});
  FTimerHandle H;
  GetWorld()->GetTimerManager().SetTimer(H,[Weak,Copy,Kind,Commit,HalfAngle,Scope,Parent,SetKey]() mutable
  {
   auto* Self=Weak.Get();if(!Self||!Current(Scope))return;
   Copy.VisualDelaySeconds=0.f;FTransform Birth=Parent;
   if(Copy.Follow==EDWAttackVfxFollow::FollowAnchor)
   {FObjectKey NowKey;if(!Anchor(Self->GetOwner(),Copy,Commit,true,Birth,NowKey)||(UsesWeapon(Copy.Anchor)&&NowKey!=SetKey))return;}
   // Frozen birth keeps the original event sample; do not re-sample a delayed shot/target origin.
   auto* FX=Self->CreateEffect(Self->GetOwner(),Copy.Effect,Copy.Effect.LocalTransform*Birth,false,Kind==EDWCombatPresentationKind::GiantSlam?HalfAngle:180.);
   if(!FX)return;
   if(Copy.Follow==EDWAttackVfxFollow::FollowAnchor)
   {
    if(!Self->VisualState)Self->VisualState.Reset(new FDWAttackVfxPlaybackState());
    Self->VisualState->Following.Add({FX,Copy,Scope,SetKey,Commit,true});
   }
   FTimerHandle Retire;const TWeakObjectPtr<UFXSystemComponent> F(FX);
   Self->GetWorld()->GetTimerManager().SetTimer(Retire,[F](){Destroy(F.Get());},FMath::Min(Copy.Effect.OneShotLifetimeSeconds,30.f),false);
  },Copy.VisualDelaySeconds,false);
  VisualState->Deferred.Add(H);return nullptr;
 }
 auto* FX=CreateEffect(GetOwner(),Copy.Effect,Copy.Effect.LocalTransform*Parent,false,Kind==EDWCombatPresentationKind::GiantSlam?HalfAngle:180.);if(!FX)return nullptr;
 if(Copy.Follow==EDWAttackVfxFollow::FollowAnchor)
 {if(!VisualState)VisualState.Reset(new FDWAttackVfxPlaybackState());VisualState->Following.Add({FX,Copy,Scope,SetKey,Commit,bCommitted});}
 const TWeakObjectPtr<UFXSystemComponent> F(FX);FTimerHandle H;
 GetWorld()->GetTimerManager().SetTimer(H,[F](){Destroy(F.Get());},FMath::Min(Copy.Effect.OneShotLifetimeSeconds,30.f),false);
 return FX;
}
void UDWCombatPresentationComponent::UpdateFollowingVisuals()
{
 if(!VisualState)return;
 for(int32 I=VisualState->Following.Num()-1;I>=0;--I)
 {
  auto& F=VisualState->Following[I];auto* FX=F.FX.Get();FTransform Parent;FObjectKey Key;
  if(!FX||!DWAttackVfxRuntime::Current(F.Token)||!DWAttackVfxRuntime::Anchor(GetOwner(),F.Entry,F.Commit,F.bCommitted,Parent,Key)||
   (DWAttackVfxRuntime::UsesWeapon(F.Entry.Anchor)&&Key!=F.SetKey))
  {DWAttackVfxRuntime::Destroy(FX);VisualState->Following.RemoveAt(I);continue;}
  FX->SetWorldTransform(F.Entry.Effect.LocalTransform*Parent);
 }
}
void UDWCombatPresentationComponent::UpdateWeaponVisuals(const UDWWeaponAnimationSet& Set,const FDWWeaponAnimationClip& Clip,const DWWeaponVisualSampler::FRequest& Request,const DWWeaponVisualSampler::FFrame& Frame,uint32 Serial)
{
 if(!GetWorld()||!GetWorld()->IsGameWorld())return;
 const auto* Profile=Set.AttackVfxProfile.Get();
 if(!VisualState&&(!Profile||Profile->SourceWindows.IsEmpty()))return;
 if(!VisualState)VisualState.Reset(new FDWAttackVfxPlaybackState());
 auto& S=*VisualState;const FObjectKey Key(&Set);
 if(!S.bHasWindowContext||S.SetKey!=Key||S.Role!=Clip.Role||S.Serial!=Serial)
 {for(auto& Pair:S.Windows)DWAttackVfxRuntime::Destroy(Pair.Value.FX.Get());S.Windows.Reset();S.SetKey=Key;S.Role=Clip.Role;S.Serial=Serial;S.bHasWindowContext=true;}
 TSet<FGuid> Seen;
 if(Profile)for(const auto& W:Profile->SourceWindows)
 {
  if(!W.Entry.EntryId.IsValid()||Seen.Contains(W.Entry.EntryId))continue;Seen.Add(W.Entry.EntryId);
  auto& V=S.Windows.FindOrAdd(W.Entry.EntryId);
  const bool SourceAnchor=W.Entry.Anchor!=EDWAttackVfxAnchor::CommittedWorld&&W.Entry.Anchor!=EDWAttackVfxAnchor::AreaCarrier;
  const bool Open=Request.bPresentationActive&&!Request.bDead&&SourceAnchor&&W.Entry.VisualDelaySeconds==0.f&&
   DWAttackVfxResolver::IsSourceWindowOpen(W,Clip.Role,Request.SourceSeconds,Clip.DurationSeconds);
  if(Open&&!V.bOpen)V.FX=SpawnVisualEntry(W.Entry,EDWCombatPresentationKind::SporeShot,FTransform::Identity,0.,180.,false);
  else if(!Open&&V.bOpen){DWAttackVfxRuntime::Destroy(V.FX.Get());V.FX.Reset();}
  V.bOpen=Open; // An expired one-shot does not respawn until the source window is re-entered.
 }
 for(auto It=S.Windows.CreateIterator();It;++It)if(!Seen.Contains(It.Key())){DWAttackVfxRuntime::Destroy(It.Value().FX.Get());It.RemoveCurrent();}
 UpdateFollowingVisuals();
}
void UDWCombatPresentationComponent::ClearWeaponVisuals()
{
 if(!VisualState)return;
 for(auto& P:VisualState->Windows)DWAttackVfxRuntime::Destroy(P.Value.FX.Get());
 VisualState->Windows.Reset();VisualState->bHasWindowContext=false;
 // Committed one-shots retain their existing lifetime; only source-window instances are removed.
 UpdateFollowingVisuals();
}
void UDWCombatPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 if(VisualState)
 {
  for(auto& P:VisualState->Windows)DWAttackVfxRuntime::Destroy(P.Value.FX.Get());
  for(auto& F:VisualState->Following)DWAttackVfxRuntime::Destroy(F.FX.Get());
  if(GetWorld())for(auto& H:VisualState->Deferred)GetWorld()->GetTimerManager().ClearTimer(H);
  VisualState.Reset();
 }
 Super::EndPlay(Reason);
}
