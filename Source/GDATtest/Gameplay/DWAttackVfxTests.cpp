#include "DWAttackVfxResolver.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"
#include <limits>
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAttackVfxBasic,"DoughWorld.Visual.AttackVfx.PureContracts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAttackVfxBasic::RunTest(const FString&)
{
 auto* S=NewObject<UDWWeaponAnimationSet>();S->PrimaryGrip=FTransform(FVector(2,0,0));S->VisualScale=3.f;S->PrimaryBack=FTransform(FVector(-40,0,0));
 S->PrimaryGeometry.Vertices={FVector(0,0,0),FVector(1,0,0),FVector(0,1,0)};S->PrimaryGeometry.Triangles={0,1,2};
 FDWWeaponAnimationClip C;C.DurationSeconds=1.f;C.HeldStartSeconds=0.f;C.HeldEndSeconds=1.f;
 FDWWeaponVisualSample A,B;A.TimeSeconds=0;B.TimeSeconds=1;
 A.PrimaryTransform=FTransform(FVector(10,0,0));B.PrimaryTransform=FTransform(FVector(30,0,0));
 A.PrimaryTrailRoot=FVector(12,0,0);B.PrimaryTrailRoot=FVector(32,0,0);A.PrimaryTrailTip=FVector(14,0,0);B.PrimaryTrailTip=FVector(34,0,0);
 A.bTrailActive=true;B.bTrailActive=false;A.bPrimaryVisible=true;B.bPrimaryVisible=false;C.VisualSamples={A,B};
 DWWeaponVisualSampler::FRequest R;R.bPresentationActive=R.bActionActive=true;R.SourceSeconds=.5;
 const auto F=DWWeaponVisualSampler::EvaluateWeaponVisualFrame(*S,C,R);
 TestTrue(TEXT("Scale affects scale, not held translation"),F.PrimaryToOwner.GetTranslation().Equals(FVector(22,0,0))&&F.PrimaryToOwner.GetScale3D().Equals(FVector(3)));
 TestTrue(TEXT("Visibility and emission remain left-sample values"),F.bPrimaryVisible&&F.bPrimaryParticleWindow);
 TestTrue(TEXT("Geometry shows even with no primary static mesh"),F.bPrimaryDeformedVisible&&!F.bPrimaryStaticVisible);
 auto E=DWWeaponVisualSampler::MapTrailEndpoints(F.PrimaryToOwner,F.Sample.PrimaryTransform,F.Sample.PrimaryTrailRoot,F.Sample.PrimaryTrailTip);
 TestTrue(TEXT("Authored inverse then edited grip/scale mapping"),E.Root.Equals(FVector(28,0,0))&&E.Tip.Equals(FVector(34,0,0)));
 const auto T=DWWeaponVisualSampler::ResolveTrailAttachment(F.PrimaryToOwner,E.Tip,FTransform(FVector(1,0,0)),true);
 TestTrue(TEXT("At-tip offset remains weapon-local"),T.GetTranslation().Equals(FVector(37,0,0)));
 R.bActionActive=false;auto Tail=DWWeaponVisualSampler::EvaluateWeaponVisualFrame(*S,C,R);
 TestTrue(TEXT("Visual tail retains pose but emits no legacy particles"),Tail.bHeld&&!Tail.bPrimaryParticleWindow);
 R.bActionActive=true;R.SourceSeconds=1;auto End=DWWeaponVisualSampler::EvaluateWeaponVisualFrame(*S,C,R);
 TestTrue(TEXT("Held end is exclusive and scale never translates back pose"),!End.bHeld&&End.PrimaryToOwner.GetTranslation().Equals(FVector(-40,0,0)));
 R.SelectedWeapon=EDWAnimatedWeapon::MoldBow;R.SourceSeconds=.5;R.bFormalBowActive=true;R.bBowShotCommitted=true;
 TestFalse(TEXT("Released nocked arrow is hidden"),DWWeaponVisualSampler::EvaluateWeaponVisualFrame(*S,C,R).bSecondaryVisible);
 S->bUseBakedVisualSamples=false;S->VisualScale=1.f;
 S->PrimaryGrip=FTransform(FRotator(17,29,-13),FVector(2,3,4),FVector(.8,1.2,2));
 R.bHasMesh=R.bHasPrimarySocket=true;R.OwnerToWorld=FTransform(FRotator(11,47,5),FVector(80,-30,15),FVector(2,3,4));
 R.PrimarySocketToWorld=FTransform(FRotator(-15,30,20),FVector(200,100,80),FVector(1.4,.7,1.2));
 const auto Fallback=DWWeaponVisualSampler::EvaluateWeaponVisualFrame(*S,C,R);
 const FTransform OldLeft=(S->PrimaryGrip*R.PrimarySocketToWorld)*R.OwnerToWorld.Inverse();
 const FTransform WrongRegroup=S->PrimaryGrip*(R.PrimarySocketToWorld*R.OwnerToWorld.Inverse());
 TestTrue(TEXT("Non-baked non-uniform fallback preserves exact old left association"),Fallback.PrimaryToOwner.Equals(OldLeft,.0000001));
 TestFalse(TEXT("Fixture detects re-parenthesized non-uniform transform drift"),OldLeft.Equals(WrongRegroup,.00001));
 auto* P=NewObject<UDWAttackVfxProfile>();FDWCombatEffectBinding Legacy;Legacy.DurationSeconds=2.25f;Legacy.LocalTransform=FTransform(FVector(4,5,6));
 auto Rows=DWAttackVfxResolver::ResolveEvents(P,EDWCombatPresentationKind::ShotgunShot,&Legacy);
 TestTrue(TEXT("Absent override uses exact legacy values, no runtime EntryGuid allocation"),Rows.Num()==1&&Rows[0].Effect.LocalTransform.Equals(Legacy.LocalTransform)&&Rows[0].Effect.DurationSeconds==Legacy.DurationSeconds&&!Rows[0].EntryId.IsValid());
 P->EventOverrides.Add(EDWCombatPresentationKind::ShotgunShot,FDWAttackVfxEventGroup{});
 TestTrue(TEXT("Explicit empty group suppresses legacy instead of double/fallback spawn"),DWAttackVfxResolver::ResolveEvents(P,EDWCombatPresentationKind::ShotgunShot,&Legacy).IsEmpty());
 FDWAttackVfxSourceWindow W;W.Entry.EntryId=FGuid(1,2,3,4);W.StartNormalized=.25f;W.EndNormalized=.75f;
 TestFalse(TEXT("Window before start"),DWAttackVfxResolver::IsSourceWindowOpen(W,TEXT("Attack"),.249,1));
 TestTrue(TEXT("Window start included"),DWAttackVfxResolver::IsSourceWindowOpen(W,TEXT("Attack"),.25,1));
 TestFalse(TEXT("Window end excluded"),DWAttackVfxResolver::IsSourceWindowOpen(W,TEXT("Attack"),.75,1));
 TestFalse(TEXT("Other role cannot create preview-only cue"),DWAttackVfxResolver::IsSourceWindowOpen(W,TEXT("ReloadOne"),.5,1));
 auto* WeaponProfile=NewObject<UDWAttackVfxProfile>();WeaponProfile->SourceWindows={W,W};FString Error;
 TestFalse(TEXT("Duplicate data IDs rejected for save"),DWAttackVfxResolver::ValidateProfile(*WeaponProfile,true,Error));
 WeaponProfile->SourceWindows.SetNum(1);TestTrue(TEXT("Actual weapon source window validates"),DWAttackVfxResolver::ValidateProfile(*WeaponProfile,true,Error));
 TestFalse(TEXT("Actor event owner cannot pretend to consume weapon source windows"),DWAttackVfxResolver::ValidateProfile(*WeaponProfile,false,Error));
 FDWAttackVfxEntry World;World.EntryId=FGuid(4,3,2,1);World.Anchor=EDWAttackVfxAnchor::PrimaryWeapon;
 TestFalse(TEXT("World slam cannot be redirected into following a weapon"),DWAttackVfxResolver::ValidateEntryForEvent(EDWCombatPresentationKind::GreatswordSlam,World,Error));
 FDWAttackVfxEntry Area;Area.EntryId=FGuid(7,6,5,4);Area.Anchor=EDWAttackVfxAnchor::AreaCarrier;Area.Follow=EDWAttackVfxFollow::FollowAnchor;Area.VisualDelaySeconds=.1f;
 TestFalse(TEXT("Bow separate event delay explicitly unsupported, not silently dropped"),DWAttackVfxResolver::ValidateEntryForEvent(EDWCombatPresentationKind::BowSlowArea,Area,Error));
 TestTrue(TEXT("Visual lifetime accepts only the same existing epoch/life facts"),DWAttackVfxResolver::IsVisualLifetimeCurrent(1,1,2,3,2,3));
 TestFalse(TEXT("Old deferred cosmetic is stale after restore epoch"),DWAttackVfxResolver::IsVisualLifetimeCurrent(1,2,2,3,2,3));
 TestFalse(TEXT("Replaced owner life cannot receive stale cosmetic"),DWAttackVfxResolver::IsVisualLifetimeCurrent(1,1,2,3,2,4));
 FDWAttackVfxEntry Duplicate;Duplicate.EntryId=FGuid(9,8,7,6);P->EventOverrides[EDWCombatPresentationKind::ShotgunShot].Entries={Duplicate,Duplicate};
 TestEqual(TEXT("Same saved data ID cannot double spawn in one committed group"),DWAttackVfxResolver::ResolveEvents(P,EDWCombatPresentationKind::ShotgunShot,&Legacy).Num(),1);
 FDWCombatEffectBinding RadiusBinding;RadiusBinding.LocalTransform=FTransform(FRotator(10,20,30),FVector(7,8,9),FVector(2,3,4));RadiusBinding.AreaReferenceRadiusCm=100;
 FTransform Effective,Original;
 TestTrue(TEXT("Shared radius affects XY scale only"),DWAttackVfxResolver::ResolveEventLocalTransform(RadiusBinding,250,Effective)&&Effective.GetTranslation().Equals(FVector(7,8,9))&&Effective.GetScale3D().Equals(FVector(5,7.5,4)));
 TestTrue(TEXT("Gizmo radius inverse restores base data, no double scaling"),DWAttackVfxResolver::RemoveEventRadiusScale(RadiusBinding,250,Effective,Original)&&Original.Equals(RadiusBinding.LocalTransform,.000001));
 auto* FX=NewObject<UNiagaraSystem>();auto& Params=FX->GetExposedParameters();
 Params.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(),TEXT("User.Color_01")));
 Params.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Delay")));
 Params.AddParameter(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(),TEXT("User.Color"))); // Explicit wrong type.
 FDWCombatEffectBinding Param;Param.ColorParameter=TEXT("User.Color_01");Param.Color=FLinearColor(.2f,.3f,.4f,.5f);Param.DelayParameter=TEXT("User.Delay");Param.DelaySeconds=.4f;
 Param.GiantHalfAngleDegreesParameter=TEXT("User.Delay");
 auto Writes=DWAttackVfxResolver::ResolveNiagaraParameters(FX,Param,60.);
 TestTrue(TEXT("Pure parameter resolver returns original color/float values, no angle doubling"),Writes.Colors.Contains(TEXT("User.Color_01"))&&Writes.Colors[TEXT("User.Color_01")]==Param.Color&&Writes.Floats[TEXT("User.Delay")]==.4f);
 Param.ColorParameter=TEXT("User.Color");Param.DelaySeconds=std::numeric_limits<float>::quiet_NaN();Writes=DWAttackVfxResolver::ResolveNiagaraParameters(FX,Param,60.);
 TestTrue(TEXT("Wrong parameter type, NaN delay and non-Giant asset produce no writes"),Writes.Colors.IsEmpty()&&Writes.Floats.IsEmpty());
 TestTrue(TEXT("Null FX asset produces no parameter writes"),DWAttackVfxResolver::ResolveNiagaraParameters(nullptr,Param,180.).Floats.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAttackVfxNativeGolden,"DoughWorld.Visual.AttackVfx.NativeGolden",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAttackVfxNativeGolden::RunTest(const FString&)
{
 FString File,Text;FParse::Value(FCommandLine::Get(),TEXT("DWAttackVfxGolden="),File);
 if(File.IsEmpty()||!FFileHelper::LoadFileToString(Text,*File)){AddError(TEXT("Pass the actual current Golden_HighPrecision_UE.json via -DWAttackVfxGolden; do not silently skip equivalence."));return false;}
 TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root){AddError(TEXT("Invalid golden JSON"));return false;}
 const auto Vector=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Key){const auto& A=O->GetArrayField(Key);return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());};
 const auto Transform=[&](const TSharedPtr<FJsonObject>& O){const auto& Q=O->GetArrayField(TEXT("q"));return FTransform(FQuat(Q[0]->AsNumber(),Q[1]->AsNumber(),Q[2]->AsNumber(),Q[3]->AsNumber()),Vector(O,TEXT("t")),Vector(O,TEXT("s")));};
 int32 Count=0;
 for(const auto& Value:Root->GetArrayField(TEXT("cases")))
 {
  const auto O=Value->AsObject();const FString SetPath=O->GetStringField(TEXT("set"));
  if(!SetPath.StartsWith(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_"))){AddError(TEXT("Unexpected asset in explicit visual golden"));return false;}
  const auto* S=LoadObject<UDWWeaponAnimationSet>(nullptr,*SetPath);const auto* C=S?S->FindClip(FName(O->GetStringField(TEXT("role")))):nullptr;
  if(!C){AddError(TEXT("Missing exact current Set/Role: ")+SetPath);return false;}
  const auto Flags=O->GetObjectField(TEXT("host_flags"));DWWeaponVisualSampler::FRequest R;
  R.SourceSeconds=O->GetNumberField(TEXT("source_seconds"));R.SelectedWeapon=S->Weapon;
  R.bPresentationActive=Flags->GetBoolField(TEXT("presentation_active"));R.bActionActive=Flags->GetBoolField(TEXT("bActive"));R.bDead=Flags->GetBoolField(TEXT("dead"));
  R.bFormalBowActive=S->Weapon==EDWAnimatedWeapon::MoldBow;R.bBowShotCommitted=Flags->GetBoolField(TEXT("bow_shot_committed"));
  const auto F=DWWeaponVisualSampler::EvaluateWeaponVisualFrame(*S,*C,R);const FString Label=SetPath+TEXT("/")+C->Role.ToString()+FString::Printf(TEXT("@%.9g"),R.SourceSeconds);
  // Independent transcription of the pre-integration UpdateVisuals arithmetic.
  // This reference preserves its conditional Blend and float32 denominator exactly.
  int32 LegacyIndex=0;
  for(int32 I=1;I<C->VisualSamples.Num()&&C->VisualSamples[I].TimeSeconds<=R.SourceSeconds;++I)LegacyIndex=I;
  auto LegacySample=C->VisualSamples[LegacyIndex];
  if(C->VisualSamples.IsValidIndex(LegacyIndex+1))
  {
   const auto& B=C->VisualSamples[LegacyIndex+1];
   const float Span=B.TimeSeconds-LegacySample.TimeSeconds;
   const float Den=FMath::IsFinite(Span)&&Span>0.f?Span:.001f;
   const float Alpha=static_cast<float>(FMath::Clamp((R.SourceSeconds-LegacySample.TimeSeconds)/Den,0.,1.));
   LegacySample.PrimaryTransform.Blend(C->VisualSamples[LegacyIndex].PrimaryTransform,B.PrimaryTransform,Alpha);
   LegacySample.SecondaryTransform.Blend(C->VisualSamples[LegacyIndex].SecondaryTransform,B.SecondaryTransform,Alpha);
  }
  const bool LegacyHeld=R.bPresentationActive&&R.SourceSeconds>=C->HeldStartSeconds&&R.SourceSeconds<C->HeldEndSeconds;
  FTransform LegacyPrimary=LegacyHeld?S->PrimaryGrip*LegacySample.PrimaryTransform:S->PrimaryBack;
  FTransform LegacySecondary=LegacyHeld?S->SecondaryGrip*LegacySample.SecondaryTransform:S->SecondaryBack;
  LegacyPrimary.SetScale3D(LegacyPrimary.GetScale3D()*FMath::Max(.01f,S->VisualScale));
  LegacySecondary.SetScale3D(LegacySecondary.GetScale3D()*FMath::Max(.01f,S->VisualScale));
  TestTrue(*(Label+TEXT(" old UpdateVisuals primary raw exact")),F.PrimaryToOwner.Equals(LegacyPrimary,0.));
  TestTrue(*(Label+TEXT(" old UpdateVisuals secondary raw exact")),F.SecondaryToOwner.Equals(LegacySecondary,0.));
  TestEqual(*Label,F.VisualIndex,static_cast<int32>(O->GetNumberField(TEXT("sample_index"))));TestEqual(*(Label+TEXT(" held")),F.bHeld,O->GetBoolField(TEXT("held")));
  for(int32 Side=0;Side<2;++Side)
  {
   const auto D=O->GetObjectField(Side?TEXT("secondary"):TEXT("primary"));const auto Expected=Transform(D->GetObjectField(TEXT("resolved_actor_local")));
   const auto& Actual=Side?F.SecondaryToOwner:F.PrimaryToOwner;
   // Use the separately documented NativeBlend-derived reference for one alpha-zero
   // Python TLerp representation correction; raw tolerance is unchanged. The original
   // golden is preserved, and every case also checks the old native arithmetic exactly.
   if(F.VisualIndex==C->VisualSamples.Num()-1&&F.bHeld)
   {
    const auto& Terminal=C->VisualSamples.Last();
    auto Legacy=(Side?S->SecondaryGrip:S->PrimaryGrip)*(Side?Terminal.SecondaryTransform:Terminal.PrimaryTransform);
    Legacy.SetScale3D(Legacy.GetScale3D()*FMath::Max(.01f,S->VisualScale));
    TestTrue(*(Label+TEXT(" terminal raw legacy transform exact")),Actual.Equals(Legacy,0.));
   }
   const bool SameTransform=Actual.GetTranslation().Equals(Expected.GetTranslation(),.0001)&&Actual.GetScale3D().Equals(Expected.GetScale3D(),.000001)&&Actual.GetRotation().Equals(Expected.GetRotation(),.0000001);
   TestTrue(*(Label+FString::Printf(TEXT(" side%d transform"),Side)),SameTransform);
   if(!SameTransform)AddInfo(FString::Printf(TEXT("side%d Actual=%s Expected=%s QuaternionActual=(%.17g,%.17g,%.17g,%.17g) Expected=(%.17g,%.17g,%.17g,%.17g)"),Side,*Actual.ToString(),*Expected.ToString(),Actual.GetRotation().X,Actual.GetRotation().Y,Actual.GetRotation().Z,Actual.GetRotation().W,Expected.GetRotation().X,Expected.GetRotation().Y,Expected.GetRotation().Z,Expected.GetRotation().W));
   const auto Points=DWWeaponVisualSampler::MapTrailEndpoints(Actual,Side?F.Sample.SecondaryTransform:F.Sample.PrimaryTransform,Side?F.Sample.SecondaryTrailRoot:F.Sample.PrimaryTrailRoot,Side?F.Sample.SecondaryTrailTip:F.Sample.PrimaryTrailTip);
   TestTrue(*(Label+TEXT(" endpoints")),Points.Root.Equals(Vector(D,TEXT("trail_root_actor_local_cm")),.0001)&&Points.Tip.Equals(Vector(D,TEXT("trail_tip_actor_local_cm")),.0001));
   const auto G=D->GetObjectField(TEXT("geometry"));TestEqual(*(Label+TEXT(" geometry index")),F.GeometryIndex,static_cast<int32>(G->GetNumberField(TEXT("frame_index"))));
   const auto& Slice=Side?F.SecondaryGeometry:F.PrimaryGeometry;
   for(const auto& Probe:G->GetArrayField(TEXT("probes")))
   {
    const auto P=Probe->AsObject();const int32 I=static_cast<int32>(P->GetNumberField(TEXT("index")));
    if(!Slice.A||!Slice.A->IsValidIndex(I)){AddError(Label+TEXT(" missing real geometry probe"));continue;}
    FVector Local=(*Slice.A)[I];if(Slice.B&&Slice.A->Num()==Slice.B->Num()&&Slice.Alpha>0.f)Local=FMath::Lerp(Local,(*Slice.B)[I],Slice.Alpha);
    TestTrue(*(Label+TEXT(" geometry probe")),Local.Equals(Vector(P,TEXT("local_cm")),.0001)&&Actual.TransformPosition(Local).Equals(Vector(P,TEXT("actor_local_cm")),.0001));
   }
  }
  const auto B=O->GetObjectField(TEXT("sample_flags"));TestEqual(*(Label+TEXT(" left primary visibility")),F.Sample.bPrimaryVisible,B->GetBoolField(TEXT("bPrimaryVisible")));
  TestEqual(*(Label+TEXT(" left secondary visibility")),F.Sample.bSecondaryVisible,B->GetBoolField(TEXT("bSecondaryVisible")));
  TestEqual(*(Label+TEXT(" left trail flag")),F.Sample.bTrailActive,B->GetBoolField(TEXT("bTrailActive")));
  const bool ExpectedHas=R.bPresentationActive&&S->bUseBakedVisualSamples&&!C->VisualSamples.IsEmpty();
  bool ExpectedPrimary=ExpectedHas&&F.bHeld?B->GetBoolField(TEXT("bPrimaryVisible")):!R.bDead;
  bool ExpectedSecondary=ExpectedHas&&F.bHeld?B->GetBoolField(TEXT("bSecondaryVisible")):!R.bDead;
  if(S->Weapon==EDWAnimatedWeapon::MoldBow)ExpectedSecondary=ExpectedSecondary&&F.bHeld&&!R.bDead&&!R.bBowShotCommitted;
  TestEqual(*(Label+TEXT(" resolved primary visibility")),F.bPrimaryVisible,ExpectedPrimary);
  TestEqual(*(Label+TEXT(" resolved secondary visibility")),F.bSecondaryVisible,ExpectedSecondary);
  const bool ExpectedWindow=R.bActionActive&&ExpectedHas&&B->GetBoolField(TEXT("bTrailActive"))&&!R.bDead;
  TestEqual(*(Label+TEXT(" primary particle window")),F.bPrimaryParticleWindow,ExpectedWindow&&ExpectedPrimary);
  TestEqual(*(Label+TEXT(" secondary particle window")),F.bSecondaryParticleWindow,ExpectedWindow&&ExpectedSecondary);++Count;
 }
 TestEqual(TEXT("Current high-precision native golden cases"),Count,209);return true;
}
#endif
