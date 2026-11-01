#include "DWCreatureDefinition.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "DWGiantAttackTiming.h"
#include "Misc/AutomationTest.h"
namespace DWCreatureFormalAnimation
{
 UAnimSequenceBase* ResolveConfiguredAttack(const UDWCreatureDefinition*,bool,EDWEnemyCombatProfile,UAnimationAsset*,const USkeletalMesh*);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCreatureFormalAnimationSelectionTest,"DoughWorld.Creature.FormalAnimation.Selection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCreatureFormalAnimationSelectionTest::RunTest(const FString&)
{
 // Transient UObject data only: no World, controller, damage, asset writes, or gameplay input.
 auto* Definition=NewObject<UDWCreatureDefinition>();
 Definition->NumericSource=EDWCreatureNumericSource::ExistingFormalProfile;
 Definition->FormalProfile=EDWEnemyCombatProfile::MoldGrunt;
 auto* Skeleton=NewObject<USkeleton>();auto* Mesh=NewObject<USkeletalMesh>();Mesh->SetSkeleton(Skeleton);
 auto* Attack=NewObject<UAnimSequence>();Attack->SetSkeleton(Skeleton);
 using namespace DWCreatureFormalAnimation;
 TestEqual(TEXT("Explicit compatible Mold Thug slot resolves"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::MoldGrunt,Attack,Mesh),static_cast<UAnimSequenceBase*>(Attack));
 Definition->FormalProfile=EDWEnemyCombatProfile::YeastGrunt;
 TestEqual(TEXT("Explicit compatible Yeast Thug slot resolves"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::YeastGrunt,Attack,Mesh),static_cast<UAnimSequenceBase*>(Attack));
 TestNull(TEXT("Missing definition keeps old actor behavior"),ResolveConfiguredAttack(nullptr,true,EDWEnemyCombatProfile::YeastGrunt,Attack,Mesh));
 TestNull(TEXT("Invalid definition cannot opt in"),ResolveConfiguredAttack(Definition,false,EDWEnemyCombatProfile::YeastGrunt,Attack,Mesh));
 TestNull(TEXT("Profile mismatch cannot borrow animation"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::MoldGrunt,Attack,Mesh));
 TestNull(TEXT("Legacy Giant is outside this capability"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::Legacy,Attack,Mesh));
 auto* Wrong=NewObject<UAnimSequence>();Wrong->SetSkeleton(NewObject<USkeleton>());
 TestNull(TEXT("Incompatible skeleton is rejected before snapshot"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::YeastGrunt,Wrong,Mesh));
 auto* Montage=NewObject<UAnimMontage>();Montage->SetSkeleton(Skeleton);
 TestNull(TEXT("Montage cannot enter sequence nodes"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::YeastGrunt,Montage,Mesh));
 auto* OtherConfigured=NewObject<UAnimSequence>();OtherConfigured->SetSkeleton(Skeleton);
 Definition->AttackAnimation=Attack;
 TestNull(TEXT("Loaded Definition soft source must agree with configured slot"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::YeastGrunt,OtherConfigured,Mesh));
 TestEqual(TEXT("Matching Definition source resolves without loads"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::YeastGrunt,Attack,Mesh),static_cast<UAnimSequenceBase*>(Attack));
 Attack->AddMetaData(NewObject<UDWGiantAttackTiming>(Attack));
 TestNull(TEXT("Giant contact metadata is never reused for formal Thug"),ResolveConfiguredAttack(Definition,true,EDWEnemyCombatProfile::YeastGrunt,Attack,Mesh));
 return true;
}
#endif
