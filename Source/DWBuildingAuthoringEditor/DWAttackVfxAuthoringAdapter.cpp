#include "DWAttackVfxAuthoringAdapter.h"
#include "DWAttackVfxProfile.h"
#include "DWAttackVfxResolver.h"
#include "DWWeaponVisualSampler.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatPresentationComponent.h"
#include "PreviewScene.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "KismetProceduralMeshLibrary.h"
#include "DWCombatFeedbackComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
class FDWAttackVfxGizmoWriteback;
class FDWAttackVfxBodyPoseProbe;
#endif
namespace DWAttackVfxGizmoDiagnostics
{void Log(const TCHAR* Stage,const FGuid& Id,const FString& Detail);}
namespace SV=DWWeaponVisualSampler;
namespace RV=DWAttackVfxResolver;
namespace
{
 enum class ERow:uint8 {Event,Primary,Secondary,Window,EventGroup};
 struct FRow {ERow Kind=ERow::Event;FGuid Id;EDWCombatPresentationKind Event=EDWCombatPresentationKind::ShotgunShot;int32 Index=INDEX_NONE;};
 // Runtime destroys a source-window effect at close or its capped one-shot lifetime.
 double SourceWindowPreviewEnd(const FDWAttackVfxSourceWindow& W,double Duration)
 {return FMath::Min(W.EndNormalized*Duration,W.StartNormalized*Duration+FMath::Min(double(W.Entry.Effect.OneShotLifetimeSeconds),30.));}
 bool IsEventRow(const FRow& R){return R.Kind==ERow::Event||R.Kind==ERow::EventGroup;}
 struct FFx
 {
  TObjectPtr<UFXSystemComponent> Component;
  FTransform FrozenWorld=FTransform::Identity;
  bool bBorn=false,bEmitting=false,bCaptured=false,bEnded=false;
  double Birth=0;
 };
 FText Label(const FString& S){return FText::FromString(S);}
 bool ValidTrs(const FTransform& T)
 {return T.IsValid()&&T.GetScale3D().GetAbsMin()>KINDA_SMALL_NUMBER;}

 class FBridge final:public IDWAttackVfxAuthoringBridge
 {
#if WITH_DEV_AUTOMATION_TESTS
  friend class ::FDWAttackVfxGizmoWriteback;
  friend class ::FDWAttackVfxBodyPoseProbe;
#endif

  TObjectPtr<UDWAttackVfxProfile> Profile;
  TObjectPtr<UDWWeaponAnimationSet> Set;
  TObjectPtr<UClass> SourceClass;
  TObjectPtr<UAnimSequenceBase> Sequence;
  const ACharacter* SourceCDO=nullptr;
  const UDWWeaponAnimationComponent* WeaponCDO=nullptr;
  const UDWCombatPresentationComponent* PresentationCDO=nullptr;
  FDWWeaponAnimationClip Clip;
  SV::FFrame Frame;
  RV::FAnchorContext Anchors;
  FGuid Selected;
  FRow SelectedRow;
  FPreviewScene* Scene=nullptr;
  TObjectPtr<AActor> Owner;
  TObjectPtr<USkeletalMeshComponent> Body;
  TObjectPtr<UStaticMeshComponent> PrimarySM,SecondarySM;
  TObjectPtr<UProceduralMeshComponent> PrimaryProc,SecondaryProc,ReleasedArrow,PrimaryTrailProc,SecondaryTrailProc;
  TMap<FGuid,FFx> Fx;
  double Time=0,EventAt=0,ClipDuration=1,ContextRadius=0;
  bool bContextValid=false,bRebuilding=false;
  FString ContextError;
  FTransform OwnerWorld=FTransform::Identity;
  bool IsWeaponOwner() const{return !Profile->PreviewWeaponSet.IsNull();}

  TArray<FRow> Rows() const
  {
   TArray<FRow> Result;
   if(!Profile)return Result;
   for(const auto& Pair:Profile->EventOverrides)
   {
    if(Pair.Value.Entries.IsEmpty())Result.Add({ERow::EventGroup,FGuid(0x44575647,0x45444954,uint32(Pair.Key)+1,0),Pair.Key,INDEX_NONE});
    for(int32 I=0;I<Pair.Value.Entries.Num();++I)Result.Add({ERow::Event,Pair.Value.Entries[I].EntryId,Pair.Key,I});
   }
   if(Set&&!Profile->PreviewWeaponSet.IsNull())
   {
    const auto T=RV::ResolveTrails(Profile, *Set);
    Result.Add({ERow::Primary,Profile->Trails.Primary.EntryId});Result.Add({ERow::Secondary,Profile->Trails.Secondary.EntryId});
   }
   for(int32 I=0;I<Profile->SourceWindows.Num();++I)Result.Add({ERow::Window,Profile->SourceWindows[I].Entry.EntryId,EDWCombatPresentationKind::ShotgunShot,I});
   return Result;
  }
  bool Locate(FGuid Id,FRow& Out) const
  {for(const auto& R:Rows())if(R.Id==Id){Out=R;return true;}return false;}
  FDWAttackVfxEntry* MutableEntry(const FRow& R)
  {
   if(R.Kind==ERow::Window)return Profile->SourceWindows.IsValidIndex(R.Index)?&Profile->SourceWindows[R.Index].Entry:nullptr;
   if(R.Kind==ERow::Event)if(auto* G=Profile->EventOverrides.Find(R.Event))return G->Entries.IsValidIndex(R.Index)?&G->Entries[R.Index]:nullptr;
   return nullptr;
  }
  FDWAttackVfxEntry TrailEntry(bool Primary) const
  {
   const auto T=RV::ResolveTrails(Profile,*Set);const auto& C=Primary?T.Primary:T.Secondary;
   FDWAttackVfxEntry E;E.EntryId=Primary?Profile->Trails.Primary.EntryId:Profile->Trails.Secondary.EntryId;E.Effect.System=C.System;E.Effect.LocalTransform=C.LocalTransform;
   E.Anchor=T.bAttachAtAuthoredTip?(Primary?EDWAttackVfxAnchor::PrimaryAuthoredTip:EDWAttackVfxAnchor::SecondaryAuthoredTip)
       :(Primary?EDWAttackVfxAnchor::PrimaryWeapon:EDWAttackVfxAnchor::SecondaryWeapon);
   E.Follow=EDWAttackVfxFollow::FollowAnchor;return E;
  }
  void EnsureTrailOverride()
  {
   if(!Profile->bOverrideLegacyTrails&&Set)
   {
    const auto P=Profile->Trails.Primary.EntryId,S=Profile->Trails.Secondary.EntryId;
    Profile->Trails=RV::ResolveTrails(nullptr,*Set);
    Profile->Trails.Primary.EntryId=P.IsValid()?P:FGuid::NewGuid();Profile->Trails.Secondary.EntryId=S.IsValid()?S:FGuid::NewGuid();
    Profile->bOverrideLegacyTrails=true;
   }
  }
  bool ResolveContext(FString& Error)
  {
   bContextValid=false;Set=nullptr;Sequence=nullptr;SourceCDO=nullptr;WeaponCDO=nullptr;PresentationCDO=nullptr;
   SourceClass=Profile->PreviewSourceClass.LoadSynchronous();
   if(!SourceClass)
   {Error=TEXT("Profile PreviewSourceClass is missing; initialize it from the audited current Hero/Giant class.");return false;}
   SourceCDO=Cast<ACharacter>(SourceClass->GetDefaultObject());
   if(!SourceCDO||!SourceCDO->GetMesh()||!SourceCDO->GetMesh()->GetSkeletalMeshAsset())
   {Error=TEXT("PreviewSourceClass has no actual Character mesh; no Cube fallback is used.");return false;}
   if(IsEventRow(SelectedRow)&&SelectedRow.Event==EDWCombatPresentationKind::GiantSlam&&!SourceCDO->IsA<ADWEnemyCharacter>()){Error=TEXT("GiantSlam requires the actual Giant source class, not Hero weapon animation.");return false;}
   WeaponCDO=SourceCDO->FindComponentByClass<UDWWeaponAnimationComponent>();
   PresentationCDO=SourceCDO->FindComponentByClass<UDWCombatPresentationComponent>();
   Set=Profile->PreviewWeaponSet.LoadSynchronous();
   // Resolve selection before refreshing the selected action; Event row must select the correct weapon.
   for(const auto& Pair:Profile->EventOverrides)for(int32 I=0;I<Pair.Value.Entries.Num();++I)
    if(Pair.Value.Entries[I].EntryId==Selected)SelectedRow={ERow::Event,Selected,Pair.Key,I};
   FName Role=Profile->PreviewClipRole.IsNone()?FName(TEXT("Attack")):Profile->PreviewClipRole;
   if(IsEventRow(SelectedRow)&&WeaponCDO&&!IsWeaponOwner())
   {
    EDWAnimatedWeapon Desired=EDWAnimatedWeapon::Shotgun;
    switch(SelectedRow.Event)
    {
     case EDWCombatPresentationKind::GreatswordSlam:Desired=EDWAnimatedWeapon::Greatsword;break;
     case EDWCombatPresentationKind::BowSlowArea:Desired=EDWAnimatedWeapon::MoldBow;break;
     case EDWCombatPresentationKind::SporeShot:Desired=EDWAnimatedWeapon::SporeGun;break;
     case EDWCombatPresentationKind::DaggerAssassination:Desired=EDWAnimatedWeapon::DualDaggers;Role=TEXT("Attack");break;
     default:break;
    }
    Set=nullptr;for(const auto& Candidate:WeaponCDO->AnimationSets)if(Candidate&&Candidate->Weapon==Desired){Set=Candidate;break;}
   }
   if(SelectedRow.Kind==ERow::Window&&Profile->SourceWindows.IsValidIndex(SelectedRow.Index)&&!Profile->SourceWindows[SelectedRow.Index].ClipRole.IsNone())Role=Profile->SourceWindows[SelectedRow.Index].ClipRole;
   if(Set)
   {
    const auto* Found=Set->FindClip(Role);if(!Found||!Found->Animation){Error=FString::Printf(TEXT("Missing actual Role %s on %s"),*Role.ToString(),*Set->GetPathName());return false;}
    Clip=*Found;Sequence=Found->Animation;ClipDuration=FMath::Max(.001f,Clip.DurationSeconds);
   }
   else if(const auto* Enemy=Cast<ADWEnemyCharacter>(SourceCDO);Enemy&&IsEventRow(SelectedRow)&&SelectedRow.Event==EDWCombatPresentationKind::GiantSlam)
   {
    Sequence=Cast<UAnimSequenceBase>(Enemy->AttackAnimation);if(!Sequence){Error=TEXT("Actual Giant AttackAnimation is not a sequence.");return false;}
    Clip=FDWWeaponAnimationClip();Clip.Role=TEXT("Attack");Clip.Animation=Sequence;Clip.DurationSeconds=Sequence->GetPlayLength();ClipDuration=FMath::Max(.001f,Clip.DurationSeconds);
   }
   else {Error=TEXT("No real PreviewWeaponSet/action context. Initialize the Profile metadata; no proxy weapon is substituted.");return false;}
   const FVector ActorScale=SourceCDO->GetActorScale3D();
   const float Half=SourceCDO->GetCapsuleComponent()?SourceCDO->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight():0;
   OwnerWorld=FTransform(FQuat::Identity,FVector(0,0,Half*FMath::Abs(ActorScale.Z)),ActorScale);
   // Reference event cue uses audited source markers. It is Editor-only, never a combat clock write.
   EventAt=0;
   if(IsEventRow(SelectedRow)&&Set&&WeaponCDO)
   {
    if(SelectedRow.Event==EDWCombatPresentationKind::GreatswordSlam&&WeaponCDO->SwordSourcePhaseEnds.Num()>=2)
     EventAt=ClipDuration*WeaponCDO->SwordSourcePhaseEnds[0];
    else if(SelectedRow.Event==EDWCombatPresentationKind::BowSlowArea)EventAt=WeaponCDO->BowSourceReleaseSeconds;
    else if(SelectedRow.Event==EDWCombatPresentationKind::DaggerAssassination&&WeaponCDO->DaggerSourceSegments.Num()>0)
    {
     // Explicit non-hit-simulation reference: midpoint of actual first absolute source segment.
     // No Dagger plan/rules/action is initialized; UI states this is a reference, not lethal timing.
     EventAt=(WeaponCDO->DaggerSourceSegments[0].X+WeaponCDO->DaggerSourceSegments[0].Y)*.5;
    }
    else if(const auto* Ends=WeaponCDO->AttackSourcePhaseEnds.Find(Set->Weapon))EventAt=ClipDuration*Ends->X;
   }
   ContextRadius=0;
   if(const auto* Enemy=Cast<ADWEnemyCharacter>(SourceCDO))
   {
    if(IsEventRow(SelectedRow))ContextRadius=Enemy->AttackRange;
    EventAt=ClipDuration*Enemy->AttackWindup/FMath::Max(.001f,Enemy->AttackWindup+Enemy->AttackActiveDuration+Enemy->AttackRecovery);
   }
   bContextValid=true;Error.Reset();return true;
  }
  template<typename T> T* AddComponent()
  {
   T* C=NewObject<T>(Owner,NAME_None,RF_Transient);Owner->AddInstanceComponent(C);C->SetupAttachment(Owner->GetRootComponent());
   C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);C->SetComponentTickEnabled(false);return C;
  }
  void Upload(UProceduralMeshComponent* C,const TArray<FVector>& V,const TArray<int32>& T,const TArray<FVector2D>& SourceUV)
  {
   if(V.IsEmpty()||T.IsEmpty()||T.Num()%3){C->ClearAllMeshSections();return;}
   for(int32 I:T)if(I<0||I>=V.Num()){C->ClearAllMeshSections();return;}
   for(const auto& P:V)if(P.ContainsNaN()){C->ClearAllMeshSections();return;}
   auto UV=SourceUV;if(UV.Num()!=V.Num())UV.Init(FVector2D::ZeroVector,V.Num());
   TArray<FVector> N;TArray<FProcMeshTangent> Tangents;
   // Engine reference helper has the same welded smoothing contract as current optimized Runtime helper.
   UKismetProceduralMeshLibrary::CalculateTangentsForMesh(V,T,UV,N,Tangents);
   C->CreateMeshSection_LinearColor(0,V,T,N,UV,{},Tangents,false);
  }
  void MeshPart(UStaticMeshComponent* SM,UProceduralMeshComponent* Proc,bool Primary)
  {
   const auto& Slice=Primary?Frame.PrimaryGeometry:Frame.SecondaryGeometry;
   const auto& Geometry=Primary?Set->PrimaryGeometry:Set->SecondaryGeometry;
   const auto T=Primary?Frame.PrimaryToOwner:Frame.SecondaryToOwner;
   const bool Deformed=Primary?Frame.bPrimaryDeformedVisible:Frame.bSecondaryDeformedVisible;
   const bool Static=Primary?Frame.bPrimaryStaticVisible:Frame.bSecondaryStaticVisible;
   SM->SetStaticMesh(Primary?Set->PrimaryMesh:Set->SecondaryMesh);SM->SetRelativeTransform(T);SM->SetVisibility(Static);
   Proc->SetRelativeTransform(T);Proc->SetVisibility(Deformed);
   if(Set->WeaponMaterial){SM->SetMaterial(0,Set->WeaponMaterial);Proc->SetMaterial(0,Set->WeaponMaterial);}
   if(Deformed&&Slice.A)
   {
    const auto Vertices=Slice.B?SV::InterpolateVertices(*Slice.A,*Slice.B,Slice.Alpha):*Slice.A;
    Upload(Proc,Vertices,Geometry.Triangles,Geometry.UVs);
   }
   else Proc->ClearAllMeshSections();
  }
  void FillAnchors()
  {
   Anchors=RV::FAnchorContext();Anchors.OwnerToWorld=OwnerWorld;
   Anchors.bHasCommitted=true;Anchors.bHasCarrier=true;
   const bool Muzzle=SelectedRow.Event==EDWCombatPresentationKind::ShotgunShot||SelectedRow.Event==EDWCombatPresentationKind::SporeShot;
   // Preview-only frozen reference; never calls FindLegalGround or modifies a Notice/Muzzle/shot origin.
   Anchors.CommittedToWorld=FTransform(FQuat::Identity,OwnerWorld.GetLocation()+(Muzzle?FVector(80,0,30):FVector(100,0,-OwnerWorld.GetLocation().Z)));
   Anchors.CarrierToWorld=FTransform(FVector(100,0,0));
   if(Body)for(FName Name:Body->GetAllSocketNames())Anchors.CharacterSocketsToWorld.Add(Name,Body->GetSocketTransform(Name));
   auto Sockets=[](UStaticMeshComponent* C,TMap<FName,FTransform>& Out)
   {if(C&&C->GetStaticMesh())for(FName N:C->GetAllSocketNames())Out.Add(N,C->GetSocketTransform(N,RTS_Component));};
   Sockets(PrimarySM,Anchors.PrimaryMeshSockets);Sockets(SecondarySM,Anchors.SecondaryMeshSockets);
  }
  UFXSystemComponent* NewFx(const FDWAttackVfxEntry& Entry,const FTransform& World)
  {
   UFXSystemAsset* Asset=Entry.Effect.System.LoadSynchronous();UFXSystemComponent* C=nullptr;
   if(auto* N=Cast<UNiagaraSystem>(Asset))
   {auto* NC=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient);NC->SetAsset(N);NC->SetAutoDestroy(false);NC->SetForceSolo(true);NC->SetAllowScalability(false);C=NC;}
   else if(auto* P=Cast<UParticleSystem>(Asset)){auto* PC=NewObject<UParticleSystemComponent>(Owner,NAME_None,RF_Transient);PC->SetTemplate(P);PC->bAutoDestroy=false;C=PC;}
   if(!C)return nullptr;
   Owner->AddInstanceComponent(C);C->SetAutoActivate(false);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCanEverAffectNavigation(false);C->SetGenerateOverlapEvents(false);
   C->SetWorldTransform(World);C->RegisterComponent();C->SetComponentTickEnabled(false);
   const auto* Enemy=Cast<ADWEnemyCharacter>(SourceCDO);
   RV::ApplyParameters(C,Asset,Entry.Effect,Enemy?Enemy->AttackHalfAngle:180.);
   C->Activate(true);return C;
  }
  void UpdateFx(const FDWAttackVfxEntry& Entry,bool Open,double Birth,float Dt,const FDWAttackVfxTrailGroup* Trail=nullptr,bool Primary=true,double CaptureTime=-1,double HardEnd=-1)
  {
   auto& S=Fx.FindOrAdd(Entry.EntryId);
   if(HardEnd>=0&&Time>=HardEnd)
   {
    if(S.Component){S.Component->Deactivate();S.Component->DestroyComponent();S.Component=nullptr;}
    S.bEnded=true;S.bEmitting=false;return;
   }
   if(S.bEnded)return;
   FTransform Parent;
   if(!RV::ResolveAnchorFrame(Entry,Frame,Anchors,Parent))Open=false;
   FTransform EffectiveLocal=Entry.Effect.LocalTransform;
   if(!Trail&&IsEventRow(SelectedRow)&&!RV::ResolveEventLocalTransform(Entry.Effect,ContextRadius,EffectiveLocal))Open=false;
   FTransform World=EffectiveLocal*Parent;
   if(Trail)
   {
    const auto EP=SV::MapTrailEndpoints(Primary?Frame.PrimaryToOwner:Frame.SecondaryToOwner,Primary?Frame.Sample.PrimaryTransform:Frame.Sample.SecondaryTransform,
     Primary?Frame.Sample.PrimaryTrailRoot:Frame.Sample.SecondaryTrailRoot,Primary?Frame.Sample.PrimaryTrailTip:Frame.Sample.SecondaryTrailTip);
    const auto& L=Entry.Effect.LocalTransform;
    World=DWAttackVfxAuthoringMath::TrailWorld(Primary?Frame.PrimaryToOwner:Frame.SecondaryToOwner,EP.Tip,L,Trail->bAttachAtAuthoredTip,OwnerWorld);
   }
   if(!S.bCaptured&&Time>=(CaptureTime>=0?CaptureTime:Birth)) {S.FrozenWorld=World;S.bCaptured=true;}
   if(Open&&!S.bBorn&&!Entry.Effect.System.IsNull())
   {S.Component=NewFx(Entry,Entry.Follow==EDWAttackVfxFollow::SampleAtBirth&&S.bCaptured?S.FrozenWorld:World);S.bBorn=S.Component!=nullptr;S.Birth=Birth;S.bEmitting=S.bBorn;}
   if(!S.Component)return;
   if(Entry.Follow==EDWAttackVfxFollow::FollowAnchor&&Open)S.Component->SetWorldTransform(World);
   if(Open&&!S.bEmitting){S.Component->Activate(true);S.bEmitting=true;}
   if(!Open&&S.bEmitting)
   {
    if(Trail&&Trail->bParticleReplacesGeometry){if(auto* N=Cast<UNiagaraComponent>(S.Component))N->DeactivateImmediate();else S.Component->Deactivate();}
    else S.Component->Deactivate();
    S.bEmitting=false;
   }
   if(auto* N=Cast<UNiagaraComponent>(S.Component))
   {
    if(Trail&&Frame.bHasSample)
    {
     const auto Endpoints=SV::MapTrailEndpoints(Primary?Frame.PrimaryToOwner:Frame.SecondaryToOwner,Primary?Frame.Sample.PrimaryTransform:Frame.Sample.SecondaryTransform,
       Primary?Frame.Sample.PrimaryTrailRoot:Frame.Sample.SecondaryTrailRoot,Primary?Frame.Sample.PrimaryTrailTip:Frame.Sample.SecondaryTrailTip);
     if(!Trail->StartParameter.IsNone())N->SetVariableVec3(Trail->StartParameter,OwnerWorld.TransformPosition(Endpoints.Root));
     if(!Trail->EndParameter.IsNone())N->SetVariableVec3(Trail->EndParameter,OwnerWorld.TransformPosition(Endpoints.Tip));
    }
    if(Dt>0)N->AdvanceSimulation(1,Dt);
   }
   else if(auto* Cascade=Cast<UParticleSystemComponent>(S.Component);Cascade&&Dt>0)Cascade->TickComponent(Dt,LEVELTICK_All,nullptr);
  }
 public:
  explicit FBridge(UDWAttackVfxProfile* In):Profile(In){}
  bool Initialize(FString& Error)
  {
   if(!Profile){Error=TEXT("No cosmetic Profile selected.");return false;}
   // First event or explicit Set, not a guessed old character/weapon catalog.
   if(!Profile->EventOverrides.IsEmpty())for(const auto& P:Profile->EventOverrides)
   {
    if(P.Value.Entries.IsEmpty())
    {Selected=FGuid(0x44575647,0x45444954,uint32(P.Key)+1,0);SelectedRow={ERow::EventGroup,Selected,P.Key,INDEX_NONE};}
    else {Selected=P.Value.Entries[0].EntryId;SelectedRow={ERow::Event,Selected,P.Key,0};}
    break;
   }
   if(Profile->EventOverrides.IsEmpty()&&!Profile->PreviewWeaponSet.IsNull())SelectedRow.Kind=ERow::Primary;
   if(!ResolveContext(Error))return false;
   const auto All=Rows();if(All.IsEmpty()){Error=TEXT("Profile has no configured entry or real PreviewWeaponSet.");return false;}
   if(!Selected.IsValid())Selected=All[0].Id;
   return Locate(Selected,SelectedRow);
  }
  virtual UObject* GetProfile() const override{return Profile;}
  virtual bool IsVisualOnlyProfile() const override{return Profile&&Profile->IsA<UDWAttackVfxProfile>();}
  virtual TArray<FDWAttackVfxEntryOption> Entries() const override
  {
   TArray<FDWAttackVfxEntryOption> Out;
   for(const auto& R:Rows())
   {
    FString Text;
    if(R.Kind==ERow::Primary||R.Kind==ERow::Secondary)Text=R.Kind==ERow::Primary?TEXT("Primary Trail"):TEXT("Secondary Trail");
    else if(R.Kind==ERow::Window)Text=TEXT("SourceWindow | ")+Profile->SourceWindows[R.Index].ClipRole.ToString()+TEXT(" | ")+Profile->SourceWindows[R.Index].Entry.Label.ToString();
    else if(R.Kind==ERow::EventGroup)Text=StaticEnum<EDWCombatPresentationKind>()->GetNameStringByValue(int64(R.Event))+TEXT(" | 空组，运行时已关闭 / Empty disabled group");
    else {const auto& E=Profile->EventOverrides.FindChecked(R.Event).Entries[R.Index];Text=StaticEnum<EDWCombatPresentationKind>()->GetNameStringByValue(int64(R.Event))+TEXT(" | ")+E.Label.ToString();}
    Out.Add({R.Id,Label(Text)});
   }return Out;
  }
  virtual FGuid SelectedEntry() const override{return Selected;}
  virtual void SelectEntry(FGuid Id) override{Selected=Id;Locate(Id,SelectedRow);RebindAfterPropertyChangeOrUndo();}
  virtual FGuid AddVisualEntry() override
  {
   FDWAttackVfxEntry E;E.EntryId=FGuid::NewGuid();E.Label=Label(TEXT("New visual effect"));
   if(IsEventRow(SelectedRow))
   {
    if(SelectedRow.Event==EDWCombatPresentationKind::BowSlowArea){E.Anchor=EDWAttackVfxAnchor::AreaCarrier;E.Follow=EDWAttackVfxFollow::FollowAnchor;}
    Profile->EventOverrides.FindOrAdd(SelectedRow.Event).Entries.Add(E);
   }
   else
   {if(!Set||Clip.Role!=TEXT("Attack"))return FGuid();FDWAttackVfxSourceWindow W;W.Entry=E;W.Entry.Anchor=EDWAttackVfxAnchor::PrimaryWeapon;W.Entry.Follow=EDWAttackVfxFollow::FollowAnchor;W.ClipRole=Clip.Role;Profile->SourceWindows.Add(W);}
   return E.EntryId;
  }
  virtual bool RemoveVisualEntry(FGuid Id) override
  {
   FRow R;if(!Locate(Id,R))return false;
   if(R.Kind==ERow::Event){Profile->EventOverrides.FindChecked(R.Event).Entries.RemoveAt(R.Index);return true;}
   if(R.Kind==ERow::Window){Profile->SourceWindows.RemoveAt(R.Index);return true;}
   if(R.Kind==ERow::EventGroup)return false;
   EnsureTrailOverride();(R.Kind==ERow::Primary?Profile->Trails.Primary:Profile->Trails.Secondary).System=nullptr;return true;
  }
  virtual bool ValidateForSave(FString& Error) const override
  {
   if(!bContextValid){Error=ContextError.IsEmpty()?TEXT("Invalid preview/owner context"):ContextError;return false;}
   if(!RV::ValidateProfile(*Profile,IsWeaponOwner(),Error))return false;
   TSet<FGuid> IDs;
   auto Unique=[&](FGuid Id){if(!Id.IsValid()||IDs.Contains(Id))return false;IDs.Add(Id);return true;};
   for(const auto& P:Profile->EventOverrides)for(const auto& E:P.Value.Entries)
    if(P.Key==EDWCombatPresentationKind::BowSlowArea&&E.VisualDelaySeconds!=0.f){Error=TEXT("Bow carrier VisualDelaySeconds currently must be zero; runtime does not consume a delayed area entry.");return false;}
   for(const auto& P:Profile->EventOverrides)for(const auto& E:P.Value.Entries)
    if(!Unique(E.EntryId)||!RV::ValidateEntryForEvent(P.Key,E,Error)){if(Error.IsEmpty())Error=TEXT("Invalid/duplicate EntryId: use Repair IDs or the tool Add button, not raw Details array addition");return false;}
   if(Profile->bOverrideLegacyTrails)
    for(const auto* C:{&Profile->Trails.Primary,&Profile->Trails.Secondary})
     if(!Unique(C->EntryId)||!ValidTrs(C->LocalTransform)||!FMath::IsFinite(C->StartNormalized)||!FMath::IsFinite(C->EndNormalized)||C->StartNormalized<0||C->EndNormalized>1||C->EndNormalized<=C->StartNormalized){Error=TEXT("Invalid Trail ID/TRS/window");return false;}
   if(!Profile->SourceWindows.IsEmpty()&&!IsWeaponOwner()){Error=TEXT("SourceWindows require a real supported WeaponSet owner");return false;}
   for(const auto& W:Profile->SourceWindows)
    if(W.ClipRole!=TEXT("Attack")||W.Entry.VisualDelaySeconds!=0.f||W.Entry.Anchor==EDWAttackVfxAnchor::CommittedWorld||W.Entry.Anchor==EDWAttackVfxAnchor::AreaCarrier||!Set||(!W.ClipRole.IsNone()&&!Set->FindClip(W.ClipRole))){Error=TEXT("SourceWindow needs zero VisualDelaySeconds, supported Attack Role and non-event anchor");return false;}
   for(const auto& W:Profile->SourceWindows)
    if(!Unique(W.Entry.EntryId)||!ValidTrs(W.Entry.Effect.LocalTransform)||!FMath::IsFinite(W.StartNormalized)||!FMath::IsFinite(W.EndNormalized)||W.StartNormalized<0||W.EndNormalized>1||W.EndNormalized<=W.StartNormalized){Error=TEXT("Invalid source window ID/TRS/window");return false;}
   Error.Reset();return true;
  }
  virtual int32 RepairEditorEntryIds() override
  {
   int32 Changed=0;TSet<FGuid> Seen;
   auto Repair=[&](FGuid& Id){if(!Id.IsValid()||Seen.Contains(Id)){Id=FGuid::NewGuid();++Changed;}Seen.Add(Id);};
   for(auto& P:Profile->EventOverrides)for(auto& E:P.Value.Entries)Repair(E.EntryId);
   // These editor metadata IDs do not enable a legacy override or assign any System.
   Repair(Profile->Trails.Primary.EntryId);Repair(Profile->Trails.Secondary.EntryId);
   for(auto& W:Profile->SourceWindows)Repair(W.Entry.EntryId);
   return Changed;
  }
  virtual void RebindAfterPropertyChangeOrUndo() override
  {
   if(bRebuilding)return;bRebuilding=true;
   FRow R;if(Locate(Selected,R))SelectedRow=R;else {const auto All=Rows();if(!All.IsEmpty()){Selected=All[0].Id;SelectedRow=All[0];}}
   FPreviewScene* Keep=Scene;ClearTransientPreview();
   if(ResolveContext(ContextError)&&Keep)PopulateTransientPreview(*Keep,ContextError);
   bRebuilding=false;
  }
  virtual double SourceDurationSeconds() const override
  {
   double End=ClipDuration;
   if(IsEventRow(SelectedRow))if(const auto* G=Profile->EventOverrides.Find(SelectedRow.Event))
    for(const auto& E:G->Entries)End=FMath::Max(End,EventAt+E.VisualDelaySeconds+FMath::Min(double(E.Effect.OneShotLifetimeSeconds),30.));
   return End;
  }
  virtual bool PopulateTransientPreview(FPreviewScene& InScene,FString& Error) override
  {
   Scene=&InScene;if(!bContextValid&&!ResolveContext(Error))return false;
   FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;
   Owner=Scene->GetWorld()->SpawnActor<AActor>(FVector::ZeroVector,FRotator::ZeroRotator,Params);if(!Owner){Error=TEXT("Transient preview owner creation failed");return false;}
   auto* Root=NewObject<USceneComponent>(Owner,NAME_None,RF_Transient);Owner->SetRootComponent(Root);Owner->AddInstanceComponent(Root);Root->RegisterComponent();Owner->SetActorTransform(OwnerWorld);Owner->SetActorEnableCollision(false);
   Body=AddComponent<USkeletalMeshComponent>();Body->SetSkeletalMeshAsset(SourceCDO->GetMesh()->GetSkeletalMeshAsset());Body->SetRelativeTransform(SourceCDO->GetMesh()->GetRelativeTransform());
   for(int32 I=0;I<SourceCDO->GetMesh()->GetNumMaterials();++I)Body->SetMaterial(I,SourceCDO->GetMesh()->GetMaterial(I));
   // Registration creates the SingleNode instance; pre-registration SetAnimation is a silent no-op.
   Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);Body->RegisterComponent();Body->SetComponentTickEnabled(false);
   Body->SetAnimation(Sequence);
   auto* SingleNode=Body->GetSingleNodeInstance();
   if(!SingleNode||SingleNode->GetCurrentAsset()!=Sequence.Get())
   {Error=TEXT("Preview body failed to bind the selected animation after registration");return false;}
   SingleNode->SetPlaying(false);
   PrimarySM=AddComponent<UStaticMeshComponent>();SecondarySM=AddComponent<UStaticMeshComponent>();PrimaryProc=AddComponent<UProceduralMeshComponent>();SecondaryProc=AddComponent<UProceduralMeshComponent>();ReleasedArrow=AddComponent<UProceduralMeshComponent>();PrimaryTrailProc=AddComponent<UProceduralMeshComponent>();SecondaryTrailProc=AddComponent<UProceduralMeshComponent>();
   for(USceneComponent* C:{static_cast<USceneComponent*>(PrimarySM),static_cast<USceneComponent*>(SecondarySM),static_cast<USceneComponent*>(PrimaryProc),static_cast<USceneComponent*>(SecondaryProc),static_cast<USceneComponent*>(ReleasedArrow.Get()),static_cast<USceneComponent*>(PrimaryTrailProc.Get()),static_cast<USceneComponent*>(SecondaryTrailProc.Get())})C->RegisterComponent();
   ResetPreviewSimulation();return StepPreview(0,0,Error);
  }
  virtual void ClearTransientPreview() override
  {ResetPreviewSimulation();if(Owner)Owner->Destroy();Owner=nullptr;Body=nullptr;PrimarySM=nullptr;SecondarySM=nullptr;PrimaryProc=nullptr;SecondaryProc=nullptr;ReleasedArrow=nullptr;PrimaryTrailProc=nullptr;SecondaryTrailProc=nullptr;Frame=SV::FFrame();}
  virtual void ResetPreviewSimulation() override
  {for(auto& P:Fx)if(P.Value.Component){P.Value.Component->Deactivate();P.Value.Component->DestroyComponent();}Fx.Reset();Time=0;}
  virtual bool StepPreview(double Source,float Dt,FString& Error) override
  {
   if(!bContextValid||!Body||!Sequence){Error=ContextError.IsEmpty()?TEXT("Missing configured preview context"):ContextError;return false;}
   if(!ValidateForSave(Error))return false;
   auto* SingleNode=Body->GetSingleNodeInstance();
   if(!SingleNode||SingleNode->GetCurrentAsset()!=Sequence.Get())
   {Error=TEXT("Preview body animation binding is missing; reopen or reselect the configured entry");return false;}
   Time=Source;Body->SetPosition(SV::AnimationSeconds(Source,float(ClipDuration),Sequence->GetPlayLength()),false);Body->TickAnimation(0,false);Body->RefreshBoneTransforms();
   if(Set)
   {
    SV::FRequest Q;Q.SourceSeconds=Source;Q.SelectedWeapon=Set->Weapon;Q.bPresentationActive=true;Q.bActionActive=Source<ClipDuration;Q.OwnerToWorld=OwnerWorld;Q.bHasMesh=true;
    Q.bFormalBowActive=Set->Weapon==EDWAnimatedWeapon::MoldBow;Q.bBowShotCommitted=Q.bFormalBowActive&&WeaponCDO&&Source>=WeaponCDO->BowSourceReleaseSeconds;
    Q.bHasPrimarySocket=Body->DoesSocketExist(Set->PrimaryBone);Q.bHasSecondarySocket=Body->DoesSocketExist(Set->SecondaryBone);
    if(Q.bHasPrimarySocket)Q.PrimarySocketToWorld=Body->GetSocketTransform(Set->PrimaryBone);
    if(Q.bHasSecondarySocket)Q.SecondarySocketToWorld=Body->GetSocketTransform(Set->SecondaryBone);
    Frame=SV::EvaluateWeaponVisualFrame(*Set,Clip,Q);MeshPart(PrimarySM,PrimaryProc,true);MeshPart(SecondarySM,SecondaryProc,false);
    ReleasedArrow->SetVisibility(false);
    if(Q.bBowShotCommitted&&WeaponCDO)
    {
     FDWWeaponGeometry G;FTransform T;UMaterialInterface* M=nullptr;
     if(WeaponCDO->EvaluateBowReleaseVisual(G,T,M))
     {Upload(ReleasedArrow,G.Vertices,G.Triangles,G.UVs);ReleasedArrow->SetRelativeTransform(T);ReleasedArrow->SetMaterial(0,M);ReleasedArrow->SetVisibility(true);}
    }
   }
   FillAnchors();
   if(Set&&IsWeaponOwner())
   {
    const auto T=RV::ResolveTrails(Profile,*Set);
    const auto* Feedback=SourceCDO->FindComponentByClass<UDWCombatFeedbackComponent>();
    const bool Highlight=Feedback&&Feedback->bHighlightTrails;
    const bool UseGeometry=T.bUseGeometryTrail||(Highlight&&Frame.Geometry);
    UMaterialInterface* TrailMaterial=Highlight&&Feedback->TrailHighlightMaterial?Feedback->TrailHighlightMaterial.Get():T.GeometryMaterial.Get();
    for(bool Primary:{true,false})
    {
     auto* Proc=Primary?PrimaryTrailProc.Get():SecondaryTrailProc.Get();const auto* System=Primary?T.Primary.System.Get():T.Secondary.System.Get();
     const bool Allowed=!T.bParticleReplacesGeometry||!System;
     const bool Visible=UseGeometry&&Frame.Geometry&&(!Frame.bHasSample||Frame.Sample.bTrailActive)&&Allowed&&(Primary||Set->Weapon!=EDWAnimatedWeapon::MoldBow||Frame.bSecondaryVisible);
     Proc->SetVisibility(Visible);Proc->SetRelativeTransform(FTransform::Identity);if(TrailMaterial)Proc->SetMaterial(0,TrailMaterial);
     if(Visible)
     {
      const auto& V=Primary?Frame.Geometry->TrailVertices:Frame.Geometry->SecondaryTrailVertices;
      const auto& Tris=Primary?Frame.Geometry->TrailTriangles:Frame.Geometry->SecondaryTrailTriangles;
      const auto Pivot=(Primary?Frame.PrimaryToOwner:Frame.SecondaryToOwner).GetTranslation();
      Upload(Proc,SV::ScaleVerticesAboutPivot(V,Pivot,FMath::Max(.01f,Set->VisualScale)),Tris,{});
     }
    }
    for(bool Primary:{true,false})
    {
     const auto& C=Primary?T.Primary:T.Secondary;const auto E=TrailEntry(Primary);
     bool Open=Primary?Frame.bPrimaryParticleWindow:Frame.bSecondaryParticleWindow;
     if(!C.bUseBakedWindow)
     {FDWAttackVfxSourceWindow W;W.Entry=E;W.ClipRole=Clip.Role;W.StartNormalized=C.StartNormalized;W.EndNormalized=C.EndNormalized;Open=RV::IsSourceWindowOpen(W,Clip.Role,Source,ClipDuration)&&QActionActive(Source)&&(Primary?Frame.bPrimaryVisible:Frame.bSecondaryVisible);}
     UpdateFx(E,Open,0,Dt,&T,Primary);
    }
   }
   // Source-time preview reference; never advances or imitates gameplay phase clocks.
   for(const auto& W:Profile->SourceWindows)
   {
    const double Birth=W.StartNormalized*ClipDuration;
    const double HardEnd=SourceWindowPreviewEnd(W,ClipDuration);
    UpdateFx(W.Entry,RV::IsSourceWindowOpen(W,Clip.Role,Source,ClipDuration),Birth,Dt,nullptr,true,-1,HardEnd);
   }
   if(IsEventRow(SelectedRow))if(const auto* Group=Profile->EventOverrides.Find(SelectedRow.Event))for(const auto& E:Group->Entries)
   {
    const double Birth=EventAt+E.VisualDelaySeconds;const double End=Birth+FMath::Clamp(double(E.Effect.OneShotLifetimeSeconds),.001,30.);
    UpdateFx(E,E.bEnabled&&Source>=Birth&&(SelectedRow.Event==EDWCombatPresentationKind::BowSlowArea||Source<End),Birth,Dt,nullptr,true,EventAt,SelectedRow.Event==EDWCombatPresentationKind::BowSlowArea?-1:End);
   }
   Error.Reset();return true;
  }
  bool QActionActive(double Source) const{return Source<ClipDuration;}
  virtual FBox PreviewBounds() const override
  {FBox B(ForceInit);if(Body)B+=Body->Bounds.GetBox();if(PrimarySM)B+=PrimarySM->Bounds.GetBox();if(PrimaryProc)B+=PrimaryProc->Bounds.GetBox();return B.IsValid?B:FBox(FVector(-100,-100,0),FVector(100,100,250));}
  virtual bool ReadSelectedWorldTransform(FTransform& Out) const override
  {
   FRow R;if(!Locate(Selected,R)||R.Kind==ERow::EventGroup)return false;FDWAttackVfxEntry E;
   if(R.Kind==ERow::Primary||R.Kind==ERow::Secondary){if(!Set)return false;E=TrailEntry(R.Kind==ERow::Primary);}
   else {auto* Self=const_cast<FBridge*>(this);const auto* P=Self->MutableEntry(R);if(!P)return false;E=*P;}
   if(R.Kind==ERow::Primary||R.Kind==ERow::Secondary)
   {
    const bool P=R.Kind==ERow::Primary;const auto T=RV::ResolveTrails(Profile,*Set);
    const auto EP=SV::MapTrailEndpoints(P?Frame.PrimaryToOwner:Frame.SecondaryToOwner,P?Frame.Sample.PrimaryTransform:Frame.Sample.SecondaryTransform,P?Frame.Sample.PrimaryTrailRoot:Frame.Sample.SecondaryTrailRoot,P?Frame.Sample.PrimaryTrailTip:Frame.Sample.SecondaryTrailTip);
    const auto& L=E.Effect.LocalTransform;
    Out=DWAttackVfxAuthoringMath::TrailWorld(P?Frame.PrimaryToOwner:Frame.SecondaryToOwner,EP.Tip,L,T.bAttachAtAuthoredTip,OwnerWorld);return ValidTrs(Out);
   }
   FTransform Parent;if(!RV::ResolveAnchorFrame(E,Frame,Anchors,Parent))return false;
   FTransform Effective=E.Effect.LocalTransform;
   if(R.Kind==ERow::Event&&!RV::ResolveEventLocalTransform(E.Effect,ContextRadius,Effective))return false;
   Out=Effective*Parent;return ValidTrs(Out);
  }
  virtual bool WriteSelectedWorldTransform(const FTransform& World,FString& Error) override
  {
   FRow R;if(!Locate(Selected,R)||R.Kind==ERow::EventGroup||!ValidTrs(World)){Error=TEXT("Invalid selection/world transform");return false;}
   FDWAttackVfxEntry E=(R.Kind==ERow::Primary||R.Kind==ERow::Secondary)?TrailEntry(R.Kind==ERow::Primary):*MutableEntry(R);
   FTransform Parent;if(!RV::ResolveAnchorFrame(E,Frame,Anchors,Parent)||!ValidTrs(Parent)){Error=TEXT("Selected anchor is missing or singular");return false;}
   DWAttackVfxGizmoDiagnostics::Log(TEXT("WriteBegin"),Selected,FString::Printf(TEXT("Profile=%s Row=%d LocalBefore=%s Parent=%s RequestedWorld=%s"),*Profile->GetPathName(),int32(R.Kind),*E.Effect.LocalTransform.ToString(),*Parent.ToString(),*World.ToString()));
   FTransform Local=World.GetRelativeTransform(Parent);
   if(R.Kind==ERow::Primary||R.Kind==ERow::Secondary)
   {
    const bool P=R.Kind==ERow::Primary;const auto T=RV::ResolveTrails(Profile,*Set);const auto& Weapon=P?Frame.PrimaryToOwner:Frame.SecondaryToOwner;
    const auto EP=SV::MapTrailEndpoints(Weapon,P?Frame.Sample.PrimaryTransform:Frame.Sample.SecondaryTransform,P?Frame.Sample.PrimaryTrailRoot:Frame.Sample.SecondaryTrailRoot,P?Frame.Sample.PrimaryTrailTip:Frame.Sample.SecondaryTrailTip);
    if(!DWAttackVfxAuthoringMath::InverseTrailWorld(World,Weapon,EP.Tip,T.bAttachAtAuthoredTip,OwnerWorld,Local)){Error=TEXT("Trail frame inversion failed");return false;}
   }
   if(R.Kind==ERow::Event)
   {
    FTransform Base;
    if(!DWAttackVfxAuthoringMath::InverseEventWorld(World,E.Effect,ContextRadius,Parent,Base)){Error=TEXT("Invalid event radius/reference scale");return false;}
    Local=Base;
   }
   if(!ValidTrs(Local)){Error=TEXT("Local transform is singular");return false;}
   if(R.Kind==ERow::Primary||R.Kind==ERow::Secondary){EnsureTrailOverride();(R.Kind==ERow::Primary?Profile->Trails.Primary:Profile->Trails.Secondary).LocalTransform=Local;}
   else MutableEntry(R)->Effect.LocalTransform=Local;
   if(auto* S=Fx.Find(Selected);S&&S->Component)S->Component->SetWorldTransform(World);
   FTransform Readback;const bool ReadOK=ReadSelectedWorldTransform(Readback);
   DWAttackVfxGizmoDiagnostics::Log(TEXT("WriteEnd"),Selected,FString::Printf(TEXT("LocalAfter=%s ReadOK=%d WorldAfter=%s"),*Local.ToString(),ReadOK,*Readback.ToString()));
   Error.Reset();return true;
  }
  virtual void CollectReferences(FReferenceCollector& C) override
  {
   C.AddReferencedObject(Profile);C.AddReferencedObject(Set);C.AddReferencedObject(SourceClass);C.AddReferencedObject(Sequence);C.AddReferencedObject(Owner);C.AddReferencedObject(Body);
   C.AddReferencedObject(PrimarySM);C.AddReferencedObject(SecondarySM);C.AddReferencedObject(PrimaryProc);C.AddReferencedObject(SecondaryProc);C.AddReferencedObject(ReleasedArrow);C.AddReferencedObject(PrimaryTrailProc);C.AddReferencedObject(SecondaryTrailProc);
   for(auto& P:Fx)C.AddReferencedObject(P.Value.Component);
  }
  virtual FText SimulationLimitations() const override
  {return Label(FString::Printf(TEXT("EditorPreview only: real Attack source markers; reference ground/target, not gameplay. Giant radius uses actual CDO; Bow area has no live carrier/radius and shows asset reference size, not a claimed gameplay radius. GPU/Depth/random/world-time effects may differ; seek replays in bounded steps. Bow released arrow is a frozen appearance reference, not a projectile simulation. W/E/R edits only Profile local TRS; no damage/clock/aim changes. Reference event source: %.4fs%s"),EventAt,SelectedRow.Event==EDWCombatPresentationKind::DaggerAssassination?TEXT(" (Dagger first-segment midpoint, NOT hit/kill simulation)"):TEXT("")));}
 };
}
TSharedPtr<IDWAttackVfxAuthoringBridge> MakeDWAttackVfxAuthoringBridge(UDWAttackVfxProfile* Profile,FString& Error)
{auto B=MakeShared<FBridge>(Profile);if(!B->Initialize(Error))return nullptr;return B;}
TArray<FAssetData> GetDWAttackVfxProfileCatalog()
{
 TArray<FAssetData> Results;FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(UDWAttackVfxProfile::StaticClass()->GetClassPathName(),Results,true);
 Results.Sort([](const auto& A,const auto& B){return A.AssetName.LexicalLess(B.AssetName);});return Results;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWVfxEditorTransformRoundTrip,"DoughWorld.Editor.AttackVfx.TransformRoundTrip",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWVfxEditorTransformRoundTrip::RunTest(const FString&)
{
 FDWAttackVfxSourceWindow Window;Window.StartNormalized=.2f;Window.EndNormalized=.8f;
 Window.Entry.Effect.OneShotLifetimeSeconds=1.f;
 TestTrue(TEXT("Source window short lifetime caps preview"),FMath::IsNearlyEqual(SourceWindowPreviewEnd(Window,10.),3.,.00001));
 Window.Entry.Effect.OneShotLifetimeSeconds=20.f;
 TestTrue(TEXT("Source window close destroys preview"),FMath::IsNearlyEqual(SourceWindowPreviewEnd(Window,10.),8.,.00001));
 Window.Entry.Effect.OneShotLifetimeSeconds=99.f;
 TestTrue(TEXT("Source window hard maximum is thirty seconds"),FMath::IsNearlyEqual(SourceWindowPreviewEnd(Window,100.),50.,.00001));
 namespace M=DWAttackVfxAuthoringMath;
 const FTransform Owner(FRotator(13,47,-9),FVector(10,-20,90),FVector(1.7,.8,2.3));
 const FTransform Weapon(FRotator(-21,32,17),FVector(45,12,33),FVector(.7,1.4,.9));
 const FTransform Local(FRotator(12,-37,19),FVector(8,-11,7),FVector(.9,1.2,.8));
 const FVector Tip(55,-14,123);
 auto Deltas=[](FTransform T,int32 Mode)
 {
  if(Mode==0)T.AddToTranslation(FVector(7,-11,3));
  if(Mode==1)T.SetRotation((FRotator(3,9,-4).Quaternion()*T.GetRotation()).GetNormalized());
  if(Mode==2)T.SetScale3D(T.GetScale3D()+FVector(.1,.2,.3));
  return T;
 };
 for(bool AtTip:{false,true})for(int32 Mode=-1;Mode<3;++Mode)
 {
  const auto World=Deltas(M::TrailWorld(Weapon,Tip,Local,AtTip,Owner),Mode);FTransform Recovered;
  TestTrue(TEXT("Trail inverse supports nonuniform Owner"),M::InverseTrailWorld(World,Weapon,Tip,AtTip,Owner,Recovered));
  TestTrue(TEXT("Trail W/E/R world roundtrip"),M::TrailWorld(Weapon,Tip,Recovered,AtTip,Owner).Equals(World,.0001));
  if(Mode==-1)TestTrue(TEXT("Trail no-edit preserves original Local"),Recovered.Equals(Local,.0001));
 }
 FDWCombatEffectBinding Binding;Binding.LocalTransform=Local;Binding.AreaReferenceRadiusCm=100;
 for(double Radius:{0.,100.,230.})for(int32 Mode=-1;Mode<3;++Mode)
 {
  FTransform World;TestTrue(TEXT("Event resolve"),M::EventWorld(Binding,Radius,Owner,World));World=Deltas(World,Mode);
  FTransform Recovered;TestTrue(TEXT("Event inverse radius scale"),M::InverseEventWorld(World,Binding,Radius,Owner,Recovered));
  auto Copy=Binding;Copy.LocalTransform=Recovered;FTransform Again;
  TestTrue(TEXT("Event repeated resolve"),M::EventWorld(Copy,Radius,Owner,Again));
  TestTrue(TEXT("Event W/E/R world roundtrip"),Again.Equals(World,.0001));
  if(Mode==-1)TestTrue(TEXT("Radius 2.3 not stored a second time"),Recovered.Equals(Local,.0001));
 }
 // Numeric regression from actual Giant: radius230/ref100 * local scale .9 = 2.07.
 auto Giant=Binding;Giant.LocalTransform=FTransform(FQuat::Identity,FVector::ZeroVector,FVector(.9,.9,.9));
 FTransform World;TestTrue(TEXT("Giant event scale"),M::EventWorld(Giant,230,FTransform::Identity,World));
 TestTrue(TEXT("Giant XY 2.07"),World.GetScale3D().Equals(FVector(2.07,2.07,.9),.0001));
 Giant.AreaReferenceRadiusCm=0;TestFalse(TEXT("Invalid area reference rejected"),M::EventWorld(Giant,230,Owner,World));
 TestFalse(TEXT("Singular owner rejected"),M::InverseTrailWorld(FTransform::Identity,Weapon,Tip,true,FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),World));
 return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAttackVfxGizmoWriteback,"DoughWorld.Editor.AttackVfx.GizmoEntryWriteback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAttackVfxGizmoWriteback::RunTest(const FString&)
{
 auto* Profile=NewObject<UDWAttackVfxProfile>();FDWAttackVfxEntry A;A.EntryId=FGuid::NewGuid();A.Effect.LocalTransform.SetScale3D(FVector(.5));
 FDWAttackVfxEntry B=A;B.EntryId=FGuid::NewGuid();Profile->EventOverrides.FindOrAdd(EDWCombatPresentationKind::ShotgunShot).Entries={A,B};
 FBridge Bridge(Profile);Bridge.Selected=A.EntryId;Bridge.SelectedRow={ERow::Event,A.EntryId,EDWCombatPresentationKind::ShotgunShot,0};
 Bridge.Anchors.bHasCommitted=true;Bridge.Anchors.CommittedToWorld=FTransform(FRotator(8,35,-7),FVector(80,0,100),FVector(1.7,.8,1.4));
 FString Error;FTransform World;TestTrue(TEXT("Actual adapter reads selected entry"),Bridge.ReadSelectedWorldTransform(World));
 for(int32 Mode=0;Mode<3;++Mode)
 {
  if(Mode==0)World.AddToTranslation(FVector(0,0,40));
  if(Mode==1)World.SetRotation((FRotator(0,30,0).Quaternion()*World.GetRotation()).GetNormalized());
  if(Mode==2)World.SetScale3D(World.GetScale3D()+FVector(.1,.2,.3));
  TestTrue(TEXT("Actual adapter accepts W/E/R world delta"),Bridge.WriteSelectedWorldTransform(World,Error));
  FTransform Actual;TestTrue(TEXT("Actual selected profile readback"),Bridge.ReadSelectedWorldTransform(Actual));
  TestTrue(TEXT("Actual selected world equals requested world"),Actual.Equals(World,.0001));
  const auto& Entries=Profile->EventOverrides.FindChecked(EDWCombatPresentationKind::ShotgunShot).Entries;
  TestFalse(TEXT("Selected saved data LocalTRS changes"),Entries[0].Effect.LocalTransform.Equals(A.Effect.LocalTransform,.0001));
  TestTrue(TEXT("Unselected entry LocalTRS unchanged"),Entries[1].Effect.LocalTransform.Equals(B.Effect.LocalTransform,0.));
 }
 return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAttackVfxBodyPoseProbe,"DoughWorld.Editor.AttackVfx.BodyPoseBinding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAttackVfxBodyPoseProbe::RunTest(const FString&)
{
 auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Cases;
 auto Dirty=[](){TArray<TSharedPtr<FJsonValue>> Out;for(TObjectIterator<UPackage> I;I;++I)if(I->IsDirty()&&!I->HasAnyFlags(RF_Transient))Out.Add(MakeShared<FJsonValueString>(I->GetName()));return Out;};
 Report->SetArrayField(TEXT("dirty_before"),Dirty());
 auto Vec=[](const FVector& V){return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};};
 auto Transform=[&](const FTransform& T){auto O=MakeShared<FJsonObject>();O->SetArrayField(TEXT("t"),Vec(T.GetTranslation()));O->SetArrayField(TEXT("s"),Vec(T.GetScale3D()));auto Q=T.GetRotation();O->SetArrayField(TEXT("q"),{MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W)});return O;};
 struct FCase{const TCHAR* Profile;EDWCombatPresentationKind Kind;TArray<double> Times;TArray<FName> Bones;};
 const TArray<FCase> Spec={
  {TEXT("/Game/DoughWorld/Core/Data/DA_DWAttackVfx_HeroEvents"),EDWCombatPresentationKind::ShotgunShot,{0.,1./6.,.25,.5},{TEXT("bone_005_L"),TEXT("bone_005_R"),TEXT("bone_003")}},
  {TEXT("/Game/DoughWorld/Core/Data/DA_DWAttackVfx_HeroEvents"),EDWCombatPresentationKind::GreatswordSlam,{0.,1.6,1.8,1.8916666507720947},{TEXT("bone_005_R"),TEXT("bone_005_L"),TEXT("Spear")}},
  {TEXT("/Game/DoughWorld/Core/Data/DA_DWAttackVfx_GiantEvents"),EDWCombatPresentationKind::GiantSlam,{0.,.8,1.,1.1,1.25},{TEXT("hand_R"),TEXT("forearm_R"),TEXT("hand_L"),TEXT("body")}}
 };
 for(const auto& C:Spec)
 {
  const auto* Original=LoadObject<UDWAttackVfxProfile>(nullptr,C.Profile);if(!Original){AddError(TEXT("Missing actual profile"));continue;}
  // Read original metadata/AN refs; disable only cosmetic FX in a transient copy, never edit saved data.
  auto* Profile=DuplicateObject<UDWAttackVfxProfile>(Original,GetTransientPackage());Profile->ClearFlags(RF_Public|RF_Standalone);Profile->SetFlags(RF_Transient);
  auto* Group=Profile->EventOverrides.Find(C.Kind);if(!Group||Group->Entries.IsEmpty()){AddError(TEXT("Missing actual event row"));continue;}
  const FGuid Id=Group->Entries[0].EntryId;for(auto& G:Profile->EventOverrides)for(auto& E:G.Value.Entries)E.Effect.System=nullptr;
  FBridge Bridge(Profile);FString Error;if(!Bridge.Initialize(Error)){AddError(Error);continue;}Bridge.SelectEntry(Id);
  FPreviewScene Scene(FPreviewScene::ConstructionValues().SetEditor(true).SetCreatePhysicsScene(false).SetTransactional(false));
  TestEqual(TEXT("Probe world is EditorPreview"),Scene.GetWorld()->WorldType,EWorldType::EditorPreview);
  // Reproduce exact old order on a bare transient display component before corrected real Bridge path.
  FActorSpawnParameters Params;Params.ObjectFlags=RF_Transient;auto* OldOwner=Scene.GetWorld()->SpawnActor<AActor>(Params);
  auto* Old=NewObject<USkeletalMeshComponent>(OldOwner,NAME_None,RF_Transient);OldOwner->AddInstanceComponent(Old);Old->SetCanEverAffectNavigation(false);Old->SetCollisionEnabled(ECollisionEnabled::NoCollision);Old->SetComponentTickEnabled(false);
  Old->SetSkeletalMeshAsset(Bridge.SourceCDO->GetMesh()->GetSkeletalMeshAsset());Old->SetAnimationMode(EAnimationMode::AnimationSingleNode);Old->SetAnimation(Bridge.Sequence);
  const bool OldBeforeNull=Old->GetSingleNodeInstance()==nullptr;Old->RegisterComponent();auto* OldInstance=Old->GetSingleNodeInstance();
  TestTrue(TEXT("Old pre-registration assignment has no instance"),OldBeforeNull);TestTrue(TEXT("Old registered asset is empty"),OldInstance&&OldInstance->GetCurrentAsset()==nullptr);
  if(OldInstance)OldInstance->SetPlaying(false);
  const float EndTime=SV::AnimationSeconds(C.Times.Last(),float(Bridge.ClipDuration),Bridge.Sequence->GetPlayLength());
  Old->SetPosition(EndTime,false);Old->TickAnimation(0,false);Old->RefreshBoneTransforms();
  auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("profile"),C.Profile);O->SetStringField(TEXT("sequence"),Bridge.Sequence->GetPathName());O->SetNumberField(TEXT("source_duration"),Bridge.ClipDuration);O->SetNumberField(TEXT("animation_length"),Bridge.Sequence->GetPlayLength());
  O->SetBoolField(TEXT("old_pre_register_instance_null"),OldBeforeNull);O->SetStringField(TEXT("old_post_register_asset"),OldInstance&&OldInstance->GetCurrentAsset()?OldInstance->GetCurrentAsset()->GetPathName():TEXT("None"));O->SetNumberField(TEXT("old_seek_current_time"),OldInstance?OldInstance->GetCurrentTime():-1.);
  TestTrue(TEXT("Old empty asset seek clamps to zero"),OldInstance&&OldInstance->GetCurrentTime()==0.f);OldOwner->Destroy();
  if(!Bridge.PopulateTransientPreview(Scene,Error)){AddError(Error);Bridge.ClearTransientPreview();continue;}
  auto* Body=Bridge.Body.Get();auto* Single=Body->GetSingleNodeInstance();TestTrue(TEXT("Real bridge has registered original selected Sequence"),Body->IsRegistered()&&Single&&Single->GetCurrentAsset()==Bridge.Sequence.Get());
  O->SetStringField(TEXT("bound_asset"),Single&&Single->GetCurrentAsset()?Single->GetCurrentAsset()->GetPathName():TEXT("None"));O->SetObjectField(TEXT("original_mesh_relative_transform"),Transform(Body->GetRelativeTransform()));
  TArray<TSharedPtr<FJsonValue>> Samples;TMap<FName,FTransform> FirstBones;TMap<FName,float> FirstCurves;TArray<float> FirstMorph;double MaxBoneDelta=0,MaxCurveDelta=0,MaxMorphDelta=0;
  for(double Source:C.Times)
  {
   if(!Bridge.StepPreview(Source,0,Error)){AddError(Error);break;}
   const float ExpectedTime=SV::AnimationSeconds(Source,float(Bridge.ClipDuration),Bridge.Sequence->GetPlayLength());
   TestTrue(TEXT("Actual SingleNode time follows mapped source"),Single&&FMath::IsNearlyEqual(Single->GetCurrentTime(),ExpectedTime,.00001f));
   auto Row=MakeShared<FJsonObject>();Row->SetNumberField(TEXT("source_seconds"),Source);Row->SetNumberField(TEXT("mapped_anim_seconds"),ExpectedTime);Row->SetNumberField(TEXT("actual_anim_seconds"),Single?Single->GetCurrentTime():-1.);auto Bones=MakeShared<FJsonObject>();
   for(FName Name:C.Bones)
   {
    const int32 I=Body->GetBoneIndex(Name);TestTrue(*FString::Printf(TEXT("Actual sample bone %s exists"),*Name.ToString()),I!=INDEX_NONE);if(I==INDEX_NONE)continue;
    const FTransform T=Body->GetComponentSpaceTransforms()[I];Bones->SetObjectField(Name.ToString(),Transform(T));
    if(!FirstBones.Contains(Name))FirstBones.Add(Name,T);else MaxBoneDelta=FMath::Max(MaxBoneDelta,FVector::Distance(T.GetTranslation(),FirstBones[Name].GetTranslation()));
   }
   Row->SetObjectField(TEXT("component_space_bones"),Bones);auto Curves=MakeShared<FJsonObject>();TArray<FName> CurveNames;if(Single)Single->GetAllCurveNames(CurveNames);
   for(FName Name:CurveNames)if(Name.ToString().StartsWith(TEXT("COR_")))
   {
    const float V=Single->GetCurveValue(Name);Curves->SetNumberField(Name.ToString(),V);if(Source==C.Times[0])FirstCurves.Add(Name,V);else MaxCurveDelta=FMath::Max(MaxCurveDelta,double(FMath::Abs(V-FirstCurves.FindRef(Name))));
   }
   Row->SetObjectField(TEXT("COR_curves"),Curves);TArray<TSharedPtr<FJsonValue>> Weights;
   if(Source==C.Times[0])FirstMorph=Body->MorphTargetWeights;
   for(int32 I=0;I<Body->MorphTargetWeights.Num();++I){const float V=Body->MorphTargetWeights[I];Weights.Add(MakeShared<FJsonValueNumber>(V));if(FirstMorph.IsValidIndex(I))MaxMorphDelta=FMath::Max(MaxMorphDelta,double(FMath::Abs(V-FirstMorph[I])));}
   Row->SetArrayField(TEXT("morph_weights_by_mesh_index"),Weights);Samples.Add(MakeShared<FJsonValueObject>(Row));
  }
  O->SetArrayField(TEXT("samples"),Samples);O->SetNumberField(TEXT("max_body_bone_translation_delta_cm"),MaxBoneDelta);O->SetNumberField(TEXT("max_COR_curve_delta"),MaxCurveDelta);O->SetNumberField(TEXT("max_morph_weight_delta"),MaxMorphDelta);
  TestTrue(TEXT("Real original body motion is nonstatic at known peaks"),MaxBoneDelta>1.);
  if(C.Kind==EDWCombatPresentationKind::GreatswordSlam){TestTrue(TEXT("Original Hero COR curves actually evaluate"),MaxCurveDelta>.001);TestTrue(TEXT("Original Hero morph weights actually change"),MaxMorphDelta>.001);}
  Cases.Add(MakeShared<FJsonValueObject>(O));Bridge.ClearTransientPreview();
 }
 Report->SetArrayField(TEXT("cases"),Cases);Report->SetArrayField(TEXT("dirty_after"),Dirty());Report->SetStringField(TEXT("boundary"),TEXT("Actual transient EditorPreview component pose probe; no gameplay, no saved assets. NullRHI CPU bones/curves/weights are not visible GUI/GPU proof. Original animation refs and data unchanged."));
 TestEqual(TEXT("Three real original attack contexts sampled"),Cases.Num(),3);
 FString Output;if(FParse::Value(FCommandLine::Get(),TEXT("DWAttackVfxPoseReport="),Output))
 {FString Text;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Text));TestTrue(TEXT("Write external pose evidence only"),FFileHelper::SaveStringToFile(Text,*Output,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));}
 return true;
}
#endif
