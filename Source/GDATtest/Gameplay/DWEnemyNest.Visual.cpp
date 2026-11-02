#include "DWEnemyNest.h"
#include "DWEnemyCharacter.h"
#include "DWCreatureDefinition.h"
#include "DWCombatAuthoritySubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace DWNestVisual
{
 struct FSample { double Lift=0.; FVector Scale=FVector::OneVector; };
 double Ease(double T){T=FMath::Clamp(T,0.,1.);return T*T*(3.-2.*T);}
 FSample Sample(double T)
 {
  T=FMath::Clamp(T,0.,1.);FSample R;
  if(T<.18){const double A=Ease(T/.18);R.Scale=FMath::Lerp(FVector::OneVector,FVector(1.04,1.04,.92),A);}
  else if(T<.45){const double A=Ease((T-.18)/.27);R.Lift=A;R.Scale=FMath::Lerp(FVector(1.04,1.04,.92),FVector(.98,.98,1.06),A);}
  else{const double A=Ease((T-.45)/.55);R.Lift=1.-A;R.Scale=FMath::Lerp(FVector(.98,.98,1.06),FVector::OneVector,A);}
  return R;
 }
 EDWEnemyCombatProfile Profile(const UClass* Class)
 {
  const auto* E=Class&&Class->IsChildOf(ADWEnemyCharacter::StaticClass())?Class->GetDefaultObject<ADWEnemyCharacter>():nullptr;
  const auto* D=E?E->CreatureDefinition.Get():nullptr;
  if(!D||D->NumericSource!=EDWCreatureNumericSource::ExistingFormalProfile||D->Disposition!=EDWCreatureDisposition::AlwaysHostile)return EDWEnemyCombatProfile::Legacy;
  return D->FormalProfile==EDWEnemyCombatProfile::MoldGrunt||D->FormalProfile==EDWEnemyCombatProfile::YeastGrunt?D->FormalProfile:EDWEnemyCombatProfile::Legacy;
 }
 EDWEnemyCombatProfile Profile(const ADWEnemyNest& N)
 {
  if(N.WeightedEnemies.IsEmpty())return Profile(N.EnemyClass.Get());
  EDWEnemyCombatProfile Result=EDWEnemyCombatProfile::Legacy;bool Found=false;
  for(const auto& Row:N.WeightedEnemies)
  {
   if(!FMath::IsFinite(Row.Weight)||Row.Weight<=0.f)continue;
   const auto P=Profile(Row.EnemyClass.Get());
   if(P==EDWEnemyCombatProfile::Legacy||(Found&&P!=Result))return EDWEnemyCombatProfile::Legacy;
   Result=P;Found=true;
  }
  return Found?Result:EDWEnemyCombatProfile::Legacy;
 }
}

void ADWEnemyNest::InitializeNestVisual()
{
 if(NestVisualProxy||!bEnableThugNestVisuals||bAmbientSpawner||bAmbientPresentationApplied||!NestMesh||!NestMesh->GetStaticMesh()||!GetWorld()||!GetWorld()->IsGameWorld())return;
 const auto Profile=DWNestVisual::Profile(*this);if(Profile==EDWEnemyCombatProfile::Legacy)return;
 bNestVisualOriginalVisible=NestMesh->IsVisible();
 // An independent primitive keeps the existing root, hitboxes, navigation and authoring transform fixed.
 NestVisualProxy=NewObject<UStaticMeshComponent>(this,TEXT("ThugNestVisualProxy"),RF_Transient);
 NestVisualProxy->SetupAttachment(NestMesh);NestVisualProxy->SetMobility(EComponentMobility::Movable);
 NestVisualProxy->SetStaticMesh(NestMesh->GetStaticMesh());NestVisualProxy->SetRelativeTransform(FTransform::Identity);
 NestVisualProxy->SetCollisionEnabled(ECollisionEnabled::NoCollision);NestVisualProxy->SetGenerateOverlapEvents(false);NestVisualProxy->SetCanEverAffectNavigation(false);
 NestVisualProxy->SetCastShadow(NestMesh->CastShadow);NestVisualProxy->SetRenderCustomDepth(NestMesh->bRenderCustomDepth);NestVisualProxy->SetCustomDepthStencilValue(NestMesh->CustomDepthStencilValue);
 NestVisualProxy->SetVisibility(bNestVisualOriginalVisible);NestVisualProxy->SetHiddenInGame(NestMesh->bHiddenInGame);
 const FLinearColor Tint=Profile==EDWEnemyCombatProfile::MoldGrunt?MoldNestColor:YeastNestColor;
 for(int32 I=0;I<NestMesh->GetNumMaterials();++I)
 {
  auto* Original=NestMesh->GetMaterial(I);NestVisualProxy->SetMaterial(I,Original);if(!Original)continue;
  TArray<FMaterialParameterInfo> Parameters;TArray<FGuid> Ids;Original->GetAllVectorParameterInfo(Parameters,Ids);
  if(Parameters.ContainsByPredicate([this](const FMaterialParameterInfo& Info){return Info.Name==NestColorParameter;}))
   if(auto* MID=NestVisualProxy->CreateDynamicMaterialInstance(I,Original))MID->SetVectorParameterValue(NestColorParameter,Tint);
 }
 NestVisualProxy->RegisterComponent();
 // Do not propagate visibility to AggroRange, authored VFX, hitboxes or the proxy child.
 NestMesh->SetVisibility(false,false);
}

void ADWEnemyNest::ResetNestVisual()
{
 bNestBounceActive=false;NestBounceAge=0.;NestBounceStartOffset=FVector::ZeroVector;NestBounceStartScale=FVector::OneVector;
 if(NestVisualProxy)NestVisualProxy->SetRelativeTransform(FTransform::Identity);
}

void ADWEnemyNest::StartNestSpawnVisual()
{
 if(!IsAlive()||bAmbientSpawner||!bEnableSpawnBounce||!FMath::IsFinite(SpawnBounceSeconds)||SpawnBounceSeconds<=0.f||!FMath::IsFinite(SpawnBounceHeightCm)||SpawnBounceHeightCm<0.f)return;
 InitializeNestVisual();if(!NestVisualProxy||!NestVisualProxy->IsVisible()||NestVisualProxy->bHiddenInGame||IsHidden())return;
 NestBounceStartOffset=NestVisualProxy->GetRelativeLocation();NestBounceStartScale=NestVisualProxy->GetRelativeScale3D();NestBounceAge=0.;bNestBounceActive=true;
 if(auto* A=UDWCombatAuthoritySubsystem::Get(this))NestBounceEpoch=A->GetWorldEpoch();else NestBounceEpoch=0;
}

void ADWEnemyNest::UpdateNestVisual(float DeltaSeconds)
{
 // This runs before range/death early returns, and restores visibility before ambient mode records it.
 if(NestVisualProxy&&(bAmbientSpawner||!bEnableThugNestVisuals))
 {
  ResetNestVisual();NestMesh->SetVisibility(bNestVisualOriginalVisible,false);NestVisualProxy->DestroyComponent();NestVisualProxy=nullptr;return;
 }
 InitializeNestVisual();if(!NestVisualProxy)return;
 auto* A=UDWCombatAuthoritySubsystem::Get(this);
 if(!IsAlive()||IsHidden()||!bEnableSpawnBounce||(bNestBounceActive&&(!A||A->GetWorldEpoch()!=NestBounceEpoch)))
 {ResetNestVisual();return;}
 if(!bNestBounceActive)return;
 if(!FMath::IsFinite(SpawnBounceSeconds)||SpawnBounceSeconds<=0.f||!FMath::IsFinite(SpawnBounceHeightCm)||SpawnBounceHeightCm<0.f){ResetNestVisual();return;}
 NestBounceAge+=FMath::IsFinite(DeltaSeconds)?FMath::Max(0.f,DeltaSeconds):0.f;
 const double T=FMath::Clamp(NestBounceAge/FMath::Clamp(double(SpawnBounceSeconds),.05,3.),0.,1.);
 if(T>=1.){ResetNestVisual();return;}
 const auto Sample=DWNestVisual::Sample(T);const double Residual=1.-DWNestVisual::Ease(T/.35);
 const FVector Scale=Sample.Scale+(NestBounceStartScale-FVector::OneVector)*Residual;
 const double RootZ=FMath::Max(.001,FMath::Abs(NestMesh->GetComponentScale().Z));
 const auto Bounds=NestMesh->GetStaticMesh()->GetBounds();const double Bottom=Bounds.Origin.Z-Bounds.BoxExtent.Z;
 const FVector Offset=FVector(0.,0.,FMath::Clamp(double(SpawnBounceHeightCm),0.,100.)*Sample.Lift/RootZ+Bottom*(1.-Sample.Scale.Z))+NestBounceStartOffset*Residual;
 NestVisualProxy->SetRelativeLocation(Offset);NestVisualProxy->SetRelativeScale3D(Scale);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWNestVisualCurveTest,"DoughWorld.Nest.Visual.PureCurveBoundaries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWNestVisualCurveTest::RunTest(const FString&)
{
 const auto Begin=DWNestVisual::Sample(0.),End=DWNestVisual::Sample(1.);
 TestTrue(TEXT("Start and finish at authored baseline"),Begin.Lift==0.&&End.Lift==0.&&Begin.Scale==FVector::OneVector&&End.Scale==FVector::OneVector);
 for(int32 I=0;I<=1000;++I){const auto S=DWNestVisual::Sample(I/1000.);TestTrue(TEXT("Finite bounded lift and mild positive scale"),FMath::IsFinite(S.Lift)&&S.Lift>=0.&&S.Lift<=1.&&!S.Scale.ContainsNaN()&&S.Scale.GetMin()>=.91&&S.Scale.GetMax()<=1.05+0.011);}
 for(double T:{.18,.45}){const auto L=DWNestVisual::Sample(T-1.e-7),R=DWNestVisual::Sample(T+1.e-7);TestTrue(TEXT("Continuous at phase edges"),FMath::Abs(L.Lift-R.Lift)<1.e-5&&L.Scale.Equals(R.Scale,1.e-5));}
 TestTrue(TEXT("Restart retains current visual at age zero"),DWNestVisual::Ease(0.)==0.&&DWNestVisual::Ease(1.)==1.);
 return true;
}
#endif
