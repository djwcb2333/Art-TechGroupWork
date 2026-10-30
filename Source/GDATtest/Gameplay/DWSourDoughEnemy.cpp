#include "DWSourDoughEnemy.h"
#include "DWSourDeathPresentationComponent.h"
#include "DWSourDoughAnimInstance.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
ADWSourDoughEnemy::ADWSourDoughEnemy()
{
    SourDeathPresentation=CreateDefaultSubobject<UDWSourDeathPresentationComponent>(TEXT("SourDeathPresentation"));
    // Presentation only; retain the base enemy's patrol, combat and collision defaults.
    bAnimatePlaceholder=false;
    GetMesh()->SetRelativeLocation(FVector(0.f,0.f,-GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
    GetMesh()->SetRelativeRotation(FRotator(0.f,-90.f,0.f));
    GetMesh()->SetVisibility(true); GetMesh()->SetHiddenInGame(false);
    if (PlaceholderBody) { PlaceholderBody->SetVisibility(false); PlaceholderBody->SetHiddenInGame(true); }
    CrumbInstances=CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("EatingCrumbs"));
    CrumbInstances->SetupAttachment(GetMesh());
    CrumbInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CrumbInstances->SetCastShadow(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (Sphere.Succeeded()) CrumbInstances->SetStaticMesh(Sphere.Object);
}
bool ADWSourDoughEnemy::BeginSpecializedDeathPresentation()
{return SourDeathPresentation&&SourDeathPresentation->BeginCommittedDeath();}
void ADWSourDoughEnemy::CancelSpecializedDeathPresentation()
{if(SourDeathPresentation)SourDeathPresentation->CancelPresentation();}
uint64 ADWSourDoughEnemy::GetSourDeathPresentationGeneration()const
{return SourDeathPresentation?SourDeathPresentation->GetPresentationGeneration():0;}
bool ADWSourDoughEnemy::NotifySourDeathAnimationCompleted(const UAnimInstance* Sender,uint64 Generation)
{return SourDeathPresentation&&SourDeathPresentation->NotifyAnimationCompleted(Sender,Generation);}
void ADWSourDoughEnemy::FinishSourDeathPresentation(uint64 Generation)
{if(SourDeathPresentation&&SourDeathPresentation->OwnsEpisode(Generation))FinishSpecializedDeathPresentation();}

void ADWSourDoughEnemy::SetDirectorPresentationControlled(bool bControlled,AActor* Director)
{
    USkeletalMeshComponent* Component=GetMesh();
    if (IsValid(Component))
    {
        if (PresentationDirector.IsValid()) Component->RemoveTickPrerequisiteActor(PresentationDirector.Get());
        if (bPresentationMeshTickCaptured)
        {
            Component->PrimaryComponentTick.TickGroup=SavedPresentationMeshTickGroup;
            Component->PrimaryComponentTick.EndTickGroup=SavedPresentationMeshEndTickGroup;
            Component->VisibilityBasedAnimTickOption=SavedPresentationVisibilityTick;
        }
    }
    PresentationDirector.Reset(); bPresentationMeshTickCaptured=false;
    bDirectorPresentationControlled=bControlled;
    if (!bControlled)
    {
        ClearDirectorAttackPose(); SpawnAccumulator=0.f;
        // Keep the allocated instance pool intact, including on aborted openings that disable Actor tick.
        for (int32 I=0;I<Crumbs.Num();++I) Crumbs[I].Age=-1.f;
        if (CrumbInstances)
            for (int32 I=0;I<CrumbInstances->GetInstanceCount();++I)
                CrumbInstances->UpdateInstanceTransform(I,FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector),false,I==CrumbInstances->GetInstanceCount()-1,true);
        return;
    }
    if (IsValid(Component) && IsValid(Director) && Director!=this && Director->GetWorld()==GetWorld())
    {
        SavedPresentationMeshTickGroup=Component->PrimaryComponentTick.TickGroup;
        SavedPresentationMeshEndTickGroup=Component->PrimaryComponentTick.EndTickGroup;
        SavedPresentationVisibilityTick=Component->VisibilityBasedAnimTickOption;
        bPresentationMeshTickCaptured=true; PresentationDirector=Director;
        Component->PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
        Component->PrimaryComponentTick.EndTickGroup=TG_PostUpdateWork;
        Component->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Component->AddTickPrerequisiteActor(Director);
    }
}
void ADWSourDoughEnemy::BeginPlay()
{
    Super::BeginPlay(); CrumbRandom.Initialize(18017); Crumbs.SetNum(24);
    if (CrumbInstances && CrumbInstances->GetStaticMesh())
        for (int32 I=0; I<Crumbs.Num(); ++I)
            CrumbInstances->AddInstance(FTransform(FQuat::Identity,FVector::ZeroVector,FVector::ZeroVector));
}
void ADWSourDoughEnemy::Tick(float DeltaSeconds)
{
    // The opening owns movement/damage admission. Keep crumbs alive without running enemy AI twice.
    if (!bDirectorPresentationControlled) Super::Tick(DeltaSeconds);
    if (GetWorld() && GetWorld()->IsPaused()) return;
    if (!CrumbInstances || Crumbs.IsEmpty() || CrumbInstances->GetInstanceCount()!=Crumbs.Num()) return;
    const UDWSourDoughAnimInstance* Anim=GetMesh() ? Cast<UDWSourDoughAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
    const bool bEmit=Anim && Anim->SourState==EDWSourVisualState::EatLoop && IsAlive();
    const float Dt=FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds,0.f,.1f) : 0.f;
    const float Rate=FMath::IsFinite(CrumbRate) ? FMath::Clamp(CrumbRate,0.f,30.f) : 0.f;
    const float Lifetime=FMath::IsFinite(CrumbLifetime) ? FMath::Clamp(CrumbLifetime,.05f,3.f) : .7f;
    const float Gravity=FMath::IsFinite(CrumbGravity) ? FMath::Max(0.f,CrumbGravity) : 160.f;
    const float Scale=FMath::IsFinite(CrumbScale) ? FMath::Clamp(CrumbScale,.001f,.15f) : .03f;
    if (bEmit && Rate>0.f)
    {
        SpawnAccumulator+=Dt*Rate;
        while (SpawnAccumulator>=1.f)
        {
            SpawnAccumulator-=1.f;
            FCrumb& C=Crumbs[NextCrumb]; NextCrumb=(NextCrumb+1)%Crumbs.Num();
            const FVector SafeOrigin=GetCrumbOriginLocal();
            C.Age=0.f; C.Start=SafeOrigin+FVector(CrumbRandom.FRandRange(-9.f,9.f),CrumbRandom.FRandRange(-3.f,3.f),CrumbRandom.FRandRange(-4.f,4.f));
            C.Velocity=FVector(CrumbRandom.FRandRange(-12.f,12.f),CrumbRandom.FRandRange(1.f,10.f),CrumbRandom.FRandRange(-8.f,8.f));
        }
    }
    else SpawnAccumulator=0.f;
    for (int32 I=0; I<Crumbs.Num(); ++I)
    {
        FCrumb& C=Crumbs[I];
        if (!bEmit) C.Age=-1.f;
        if (C.Age>=0.f) { C.Age+=Dt; if (C.Age>=Lifetime) C.Age=-1.f; }
        const bool bVisible=C.Age>=0.f;
        const FVector Pos=bVisible ? C.Start+C.Velocity*C.Age+FVector(0,0,-.5f*Gravity*C.Age*C.Age) : FVector::ZeroVector;
        const float Size=bVisible ? Scale*(1.f-.45f*C.Age/Lifetime) : 0.f;
        CrumbInstances->UpdateInstanceTransform(I,FTransform(FQuat::Identity,Pos,FVector(Size)),false,I==Crumbs.Num()-1,true);
    }
}

FVector ADWSourDoughEnemy::GetCrumbOriginLocal() const
{
    const FVector Origin=CrumbOriginLocal.ContainsNaN() ? FVector(0.f,50.f,92.f) : CrumbOriginLocal;
    const USkeletalMeshComponent* Component=GetMesh();
    const USkeletalMesh* Asset=Component ? Component->GetSkeletalMeshAsset() : nullptr;
    if (!Asset || CrumbAnchorBone.IsNone()) return Origin;
    const FReferenceSkeleton& Ref=Asset->GetRefSkeleton();
    const int32 Index=Ref.FindBoneIndex(CrumbAnchorBone);
    if (Index==INDEX_NONE) return Origin;
    FTransform Reference=Ref.GetRefBonePose()[Index];
    for (int32 Parent=Ref.GetParentIndex(Index);Parent!=INDEX_NONE;Parent=Ref.GetParentIndex(Parent))
        Reference*=Ref.GetRefBonePose()[Parent];
    const FVector BoneLocal=Reference.InverseTransformPosition(Origin);
    return Component->GetSocketTransform(CrumbAnchorBone,RTS_Component).TransformPosition(BoneLocal);
}
