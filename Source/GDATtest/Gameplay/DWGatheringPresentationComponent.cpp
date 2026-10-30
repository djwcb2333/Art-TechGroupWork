#include "DWGatheringPresentationComponent.h"
#include "DWGatheringPresentationMath.h"
#include "DWGatheringPhysicsRules.h"
#include "DWGatheringWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace DWGatheringPhysics
{
float Safe(float V,float Fallback){return FMath::IsFinite(V)?V:Fallback;}
bool HasConvexBody(UStaticMesh* Mesh)
{
    const UBodySetup* Body=Mesh?Mesh->GetBodySetup():nullptr;
    return Body&&Body->GetCollisionTraceFlag()!=CTF_UseComplexAsSimple&&Body->AggGeom.ConvexElems.Num()>0;
}
UStaticMeshComponent* MakeMesh(AActor* Owner,UStaticMesh* Mesh)
{
    if(!IsValid(Owner)||!Mesh)return nullptr;
    auto* C=NewObject<UStaticMeshComponent>(Owner,NAME_None,RF_Transient);
    C->ComponentTags.Add(TEXT("DWGatheringPresentationProxy"));
    C->SetupAttachment(Owner->GetRootComponent());
    C->SetMobility(EComponentMobility::Movable);
    C->SetStaticMesh(Mesh);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->SetGenerateOverlapEvents(false);
    C->SetCanEverAffectNavigation(false);
    C->BodyInstance.bAutoWeld=false;
    C->RegisterComponent();
    return C;
}
}

UDWGatheringPresentationComponent::UDWGatheringPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=false;
    PrimaryComponentTick.TickGroup=TG_PostPhysics;
    bAutoActivate=true;
    DustMesh=TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/SM_BuildingCloud.SM_BuildingCloud")));
    DustMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DoughWorld/VFX/Buildings/M_BuildingCloud.M_BuildingCloud")));
}

bool UDWGatheringPresentationComponent::HasPersistentStump() const
{return IsValid(Stump)&&bExhaustionStarted&&!bPhysicsSetupFailed;}
int32 UDWGatheringPresentationComponent::GetActivePhysicsBodyCount() const
{
    int32 Count=0;
    for(const auto& Piece:Pieces)if(IsValid(Piece.Component)&&Piece.Component->IsSimulatingPhysics())++Count;
    return Count;
}
bool UDWGatheringPresentationComponent::CaptureSource()
{
    if(bCaptured)return Source.IsValid();
    const auto* Resource=Cast<ADWGatheringResource>(GetOwner());
    auto* C=Resource?Resource->Mesh.Get():nullptr;
    if(!IsValid(C)||!C->GetStaticMesh())return false;
    Source=C;SourceRest=C->GetComponentTransform();SourceCollision=C->GetCollisionEnabled();
    bOriginalVisible=C->IsVisible();
    const FBox Bounds=C->GetStaticMesh()->GetBoundingBox();const FVector Center=Bounds.GetCenter();
    Pivot=SourceRest.TransformPosition(FVector(Center.X,Center.Y,Bounds.Min.Z));
    ContactPoint=Pivot;
    C->SetMobility(EComponentMobility::Movable);
    bCaptured=true;return true;
}

void UDWGatheringPresentationComponent::ApplyDissolveMaterials(FDWGatheringPieceRuntime& Piece)
{
    auto* Target=Piece.Component.Get();if(!IsValid(Target))return;
    for(int32 I=0;I<Target->GetNumMaterials();++I)
    {
        auto* Original=Target->GetMaterial(I);
        UMaterialInterface* Replacement=nullptr;
        for(const auto& Surface:DissolveSurfaces)
            if(Surface.OriginalMaterial.LoadSynchronous()==Original)
            {Replacement=Surface.DissolveMaterial.LoadSynchronous();break;}
        // The uncut source's slot overrides are also useful on a split upper with
        // identical original slots. Cap slots still need an explicit surface map.
        if(!Replacement&&Source.IsValid()&&SourceDissolveMaterials.IsValidIndex(I)&&
            I<Source->GetNumMaterials()&&Source->GetMaterial(I)==Original)
            Replacement=SourceDissolveMaterials[I].LoadSynchronous();
        if(!Replacement){++MissingDissolveSlots;continue;}
        auto* MID=UMaterialInstanceDynamic::Create(Replacement,this);
        if(!MID){++MissingDissolveSlots;continue;}
        MID->SetScalarParameterValue(TEXT("DissolveAmount"),0.f);
        MID->SetVectorParameterValue(TEXT("EdgeColor"),DissolveEdgeColor);
        MID->SetScalarParameterValue(TEXT("EdgeWidth"),FMath::Clamp(DWGatheringPhysics::Safe(DissolveEdgeWidth,.045f),.001f,.3f));
        MID->SetScalarParameterValue(TEXT("EdgeEmissiveStrength"),FMath::Max(0.f,DWGatheringPhysics::Safe(DissolveEdgeStrength,5.f)));
        Target->SetMaterial(I,MID);Piece.DissolveMIDs.Add(MID);
    }
}

bool UDWGatheringPresentationComponent::BuildFragments()
{
    if(bFragmentsReady)return true;
    if(!Source.IsValid()||Fragments.Num()<2||Fragments.Num()>32)return false;
    TArray<UStaticMesh*> Loaded;
    for(const auto& Fragment:Fragments)
    {
        auto* Mesh=Fragment.Mesh.LoadSynchronous();
        if(!DWGatheringPhysics::HasConvexBody(Mesh)||!Fragment.LocalTransform.IsValid())return false;
        Loaded.Add(Mesh);
    }
    for(int32 I=0;I<Fragments.Num();++I)
    {
        auto* C=DWGatheringPhysics::MakeMesh(GetOwner(),Loaded[I]);if(!C)continue;
        C->ComponentTags.Add(TEXT("DWGatheringPhysicsFragment"));
        FDWGatheringPieceRuntime Piece;Piece.Component=C;
        Piece.Rest=Fragments[I].LocalTransform*SourceRest;
        Piece.LocalCenter=Loaded[I]->GetBounds().Origin;
        C->SetWorldTransform(Piece.Rest);
        ApplyDissolveMaterials(Piece);Pieces.Add(MoveTemp(Piece));
    }
    bFragmentsReady=Pieces.Num()==Fragments.Num();
    if(!bFragmentsReady){ClearTransientVisuals();return false;}
    Source->SetVisibility(false,false);UpdateCracks();return true;
}

bool UDWGatheringPresentationComponent::BuildBrokenTree()
{
    auto* Lower=StumpMesh.LoadSynchronous();auto* Upper=UpperTreeMesh.LoadSynchronous();
    if(!DWGatheringPhysics::HasConvexBody(Lower)||!DWGatheringPhysics::HasConvexBody(Upper)||
        !StumpLocalTransform.IsValid()||!UpperTreeLocalTransform.IsValid())return false;
    Stump=DWGatheringPhysics::MakeMesh(GetOwner(),Lower);
    auto* C=DWGatheringPhysics::MakeMesh(GetOwner(),Upper);
    if(!Stump||!C){if(C)C->DestroyComponent();ClearTransientVisuals();return false;}
    Stump->ComponentTags.Add(TEXT("DWGatheringPersistentStump"));
    Stump->SetWorldTransform(StumpLocalTransform*SourceRest);
    Stump->SetCollisionObjectType(ECC_WorldStatic);
    Stump->SetCollisionResponseToAllChannels(ECR_Block);
    Stump->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    StumpDissolve=FDWGatheringPieceRuntime();StumpDissolve.Component=Stump;
    ApplyDissolveMaterials(StumpDissolve);
    C->ComponentTags.Add(TEXT("DWGatheringPhysicsUpperTree"));
    FDWGatheringPieceRuntime Piece;Piece.Component=C;
    Piece.Rest=UpperTreeLocalTransform*SourceRest;Piece.LocalCenter=Upper->GetBounds().Origin;
    C->SetWorldTransform(Piece.Rest);
    ApplyDissolveMaterials(Piece);Pieces.Add(MoveTemp(Piece));
    Source->SetVisibility(false,false);
    return true;
}

void UDWGatheringPresentationComponent::UpdateCracks()
{
    if(!bFragmentsReady||bExhaustionStarted)return;
    const float Separation=FMath::Max(0.f,DWGatheringPhysics::Safe(CrackSeparation,6.f))*Progress;
    const FVector Center=SourceRest.TransformPosition(Source->GetStaticMesh()->GetBounds().Origin);
    for(auto& Piece:Pieces)if(IsValid(Piece.Component))
    {
        const FVector Direction=(Piece.Rest.TransformPosition(Piece.LocalCenter)-Center).GetSafeNormal();
        FTransform T=Piece.Rest;T.AddToTranslation(Direction*Separation);
        Piece.Component->SetWorldTransform(T); // Before physical release only.
    }
}

bool UDWGatheringPresentationComponent::EnablePhysics(FDWGatheringPieceRuntime& Piece,float MassKg,const FVector& Velocity,const FVector& AngularDegrees)
{
    auto* C=Piece.Component.Get();if(!IsValid(C)||!DWGatheringPhysics::HasConvexBody(C->GetStaticMesh()))return false;
    C->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    C->SetCollisionObjectType(ECC_PhysicsBody);
    C->SetCollisionResponseToAllChannels(ECR_Block);
    C->SetCollisionResponseToChannel(ECC_Pawn,ECR_Ignore);
    C->SetCollisionResponseToChannel(ECC_Camera,ECR_Ignore);
    C->SetCollisionResponseToChannel(ECC_Visibility,ECR_Ignore);
    C->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    C->SetPhysMaterialOverride(PhysicsMaterial);
    C->SetEnableGravity(true);
    C->SetUseCCD(true);
    C->SetLinearDamping(FMath::Max(0.f,DWGatheringPhysics::Safe(LinearDamping,.18f)));
    C->SetAngularDamping(FMath::Max(0.f,DWGatheringPhysics::Safe(AngularDamping,.25f)));
    C->SetNotifyRigidBodyCollision(true);
    C->OnComponentHit.AddDynamic(this,&UDWGatheringPresentationComponent::HandlePhysicsHit);
    C->SetSimulatePhysics(true);
    C->SetMassOverrideInKg(NAME_None,FMath::Max(.1f,DWGatheringPhysics::Safe(MassKg,2.f)),true);
    if(!C->IsSimulatingPhysics())return false;
    Piece.PhysicsStartWorldSeconds=GetWorld()->GetTimeSeconds();
    C->AddImpulse(Velocity,NAME_None,true); // One initial physical velocity change, not a trajectory.
    C->SetPhysicsAngularVelocityInDegrees(AngularDegrees,false);
    C->WakeAllRigidBodies();
    return true;
}

bool UDWGatheringPresentationComponent::BeginExhaustion()
{
    if(bExhaustionStarted||!CaptureSource())return false;
    Source->SetWorldTransform(SourceRest);
    const bool Ready=Style==EDWGatheringDestructionStyle::TreeFall?BuildBrokenTree():BuildFragments();
    if(!Ready)
    {
        bPhysicsSetupFailed=true;
        UE_LOG(LogTemp,Error,TEXT("Gathering physics setup rejected %s: split assets/simple convex collision required."),*GetOwner()->GetName());
        return false;
    }
    PhysicsMaterial=NewObject<UPhysicalMaterial>(this);
    PhysicsMaterial->Friction=FMath::Clamp(DWGatheringPhysics::Safe(SurfaceFriction,.65f),0.f,2.f);
    PhysicsMaterial->Restitution=FMath::Clamp(DWGatheringPhysics::Safe(Restitution,.08f),0.f,1.f);
    Source->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    bExhaustionStarted=true;bContactedGround=bPresentationFinished=false;
    ++PhysicsGeneration;
    PhysicsElapsed=CurrentDissolve=DustElapsed=0.f;
    GetOwner()->SetActorEnableCollision(true);
    const FVector Center=SourceRest.TransformPosition(Source->GetStaticMesh()->GetBounds().Origin);
    for(int32 I=0;I<Pieces.Num();++I)
    {
        auto& Piece=Pieces[I];FVector Velocity,Angular;float Mass;
        if(Style==EDWGatheringDestructionStyle::TreeFall)
        {
            Velocity=FallDirection*FMath::Max(0.f,DWGatheringPhysics::Safe(TreePushSpeed,65.f));
            Angular=FVector::CrossProduct(FVector::UpVector,FallDirection).GetSafeNormal()*FMath::Max(0.f,DWGatheringPhysics::Safe(TreeAngularSpeed,60.f));
            Mass=UpperTreeMassKg;
        }
        else
        {
            const double Angle=I*2.3999632297;
            FVector Direction=(Piece.Component->GetComponentTransform().TransformPosition(Piece.LocalCenter)-Center).GetSafeNormal2D();
            if(Direction.IsNearlyZero())Direction=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0.);
            Velocity=Direction*FMath::Max(0.f,DWGatheringPhysics::Safe(FragmentOutwardSpeed,130.f))+
                FVector::UpVector*FMath::Max(0.f,DWGatheringPhysics::Safe(FragmentUpwardSpeed,95.f));
            Angular=FVector(FMath::Sin(Angle),FMath::Cos(Angle),.35).GetSafeNormal()*(50.f+I*13.f);
            Mass=FragmentMassKg;
        }
        if(!EnablePhysics(Piece,Mass,Velocity,Angular))
        {
            bPhysicsSetupFailed=true;
            UE_LOG(LogTemp,Error,TEXT("Gathering physics body did not activate: %s"),*GetOwner()->GetName());
            ClearTransientVisuals();Source->SetVisibility(bOriginalVisible,false);Source->SetCollisionEnabled(SourceCollision);
            bExhaustionStarted=false;return false;
        }
    }
    return true;
}

void UDWGatheringPresentationComponent::PlayAcceptedHit(const FVector& InstigatorLocation,float GatheredFraction,bool Exhausted)
{
    if(Style==EDWGatheringDestructionStyle::None||bExhaustionStarted||!GetWorld()||GetWorld()->bIsTearingDown||!CaptureSource())return;
    ++AcceptedHitCount;Progress=DWGatheringPresentationMath::Unit(GatheredFraction);HitElapsed=0.f;
    FallDirection=(Pivot-InstigatorLocation).GetSafeNormal2D();
    if(FallDirection.IsNearlyZero())FallDirection=GetOwner()->GetActorForwardVector().GetSafeNormal2D();
    if(FallDirection.IsNearlyZero())FallDirection=FVector::ForwardVector;
    if(Style==EDWGatheringDestructionStyle::RockFracture){BuildFragments();UpdateCracks();}
    if(Exhausted)BeginExhaustion();
    SetComponentTickEnabled(true);
}

bool UDWGatheringPresentationComponent::IsGroundSurface(AActor* OtherActor,UPrimitiveComponent* OtherComponent,const FVector& Normal) const
{
    return DWGatheringPhysicsRules::IsGroundContact(IsValid(OtherActor)&&IsValid(OtherComponent),OtherActor==GetOwner(),
        Cast<APawn>(OtherActor)!=nullptr,IsValid(OtherComponent)&&OtherComponent->IsSimulatingPhysics(),float(Normal.Z),
        DWGatheringPhysics::Safe(GroundNormalMinZ,.45f));
}
void UDWGatheringPresentationComponent::HandlePhysicsHit(UPrimitiveComponent* HitComponent,AActor* OtherActor,UPrimitiveComponent* OtherComponent,FVector NormalImpulse,const FHitResult& Hit)
{
    if(!GetWorld()||GetWorld()->bIsTearingDown||!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed()||
        !bExhaustionStarted||bPhysicsSetupFailed||bPresentationFinished||!Hit.bBlockingHit||!IsGroundSurface(OtherActor,OtherComponent,Hit.ImpactNormal))return;
    for(auto& Piece:Pieces)if(Piece.Component==HitComponent&&!Piece.bFinished)
    {MarkGroundContact(Piece,Hit.ImpactPoint,true);return;}
}
bool UDWGatheringPresentationComponent::HasSupportingGround(UStaticMeshComponent* Body,FVector& Point) const
{
    if(!GetWorld()||!IsValid(Body))return false;
    const FVector Bottom(Body->Bounds.Origin.X,Body->Bounds.Origin.Y,Body->Bounds.Origin.Z-Body->Bounds.BoxExtent.Z);
    FHitResult Hit;FCollisionQueryParams Query(SCENE_QUERY_STAT(GatheringSettledGround),false,GetOwner());
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    if(!GetWorld()->LineTraceSingleByObjectType(Hit,Bottom+FVector(0,0,8),Bottom-FVector(0,0,8),Objects,Query)||
        !IsGroundSurface(Hit.GetActor(),Hit.GetComponent(),Hit.ImpactNormal))return false;
    Point=Hit.ImpactPoint;return true;
}
void UDWGatheringPresentationComponent::MarkGroundContact(FDWGatheringPieceRuntime& Piece,const FVector& Point,bool FromHit)
{
    if(Piece.ContactElapsed>=0.f||Piece.bFinished)return;
    Piece.ContactElapsed=0.f;Piece.ContactWorldSeconds=GetWorld()->GetTimeSeconds();++BodyGroundContactCount;
    if(FromHit)++PhysicsHitEventCount;else ++SettledGroundContactCount;
    if(!bContactedGround)
    {
        bContactedGround=true;StumpContactWorldSeconds=Piece.ContactWorldSeconds;++GroundContactCount;ContactPoint=Point;DustElapsed=0.f;SpawnDust();
        // The resource's listener schedules its existing exact-once drop transaction.
        OnPhysicalGroundContact.Broadcast();
    }
}

void UDWGatheringPresentationComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if(!FMath::IsFinite(DeltaTime)||DeltaTime<=0.f||!GetWorld()||GetWorld()->bIsTearingDown||GetWorld()->IsPaused()||
        !IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed()||!Source.IsValid())return;
    if(!bExhaustionStarted)
    {
        HitElapsed+=DeltaTime;
        if(Style==EDWGatheringDestructionStyle::TreeFall)
        {
            const float T=DWGatheringPresentationMath::Unit(HitElapsed/.32f);
            const FQuat Rotation(FVector::CrossProduct(FVector::UpVector,FallDirection).GetSafeNormal(),FMath::Sin(T*UE_PI*3.f)*(1.f-T)*.025f);
            FTransform Tform=SourceRest;Tform.SetLocation(DWGatheringPresentationMath::RotateAbout(SourceRest.GetLocation(),Pivot,Rotation));
            Tform.SetRotation((Rotation*SourceRest.GetRotation()).GetNormalized());Source->SetWorldTransform(Tform);
        }
        if(HitElapsed>=.32f){Source->SetWorldTransform(SourceRest);SetComponentTickEnabled(false);}return;
    }
    if(bPresentationFinished){SetComponentTickEnabled(false);return;}
    PhysicsElapsed+=DeltaTime;
    const TWeakObjectPtr<UDWGatheringPresentationComponent> WeakThis(this);
    const uint64 Generation=PhysicsGeneration;
    bool AllFinished=Pieces.Num()>0;CurrentDissolve=0.f;
    for(auto& Piece:Pieces)
    {
        auto* C=Piece.Component.Get();if(Piece.bFinished)continue;
        if(!IsValid(C)){AllFinished=false;continue;}
        // A low-speed body only counts as landed when an actual supporting
        // non-Pawn surface is within 8 cm. Airborne/apex/timeout is not contact.
        if(Piece.ContactElapsed<0.f)
        {
            const bool Slow=C->GetPhysicsLinearVelocity().SizeSquared()<25.&&C->GetPhysicsAngularVelocityInDegrees().SizeSquared()<25.;
            FVector Support;
            if(Slow&&HasSupportingGround(C,Support))Piece.StableElapsed+=DeltaTime;else Piece.StableElapsed=0.f;
            if(Piece.StableElapsed>=FMath::Max(.05f,DWGatheringPhysics::Safe(SettledContactSeconds,.25f)))MarkGroundContact(Piece,Support,false);
            if(!WeakThis.IsValid()||PhysicsGeneration!=Generation||!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed())return;
        }
        if(Piece.ContactElapsed>=0.f)
        {
            Piece.ContactElapsed=FMath::Max(0.f,float(GetWorld()->GetTimeSeconds()-Piece.ContactWorldSeconds));
            const float Amount=DWGatheringPhysicsRules::DissolveAfterContact(Piece.ContactElapsed,
                DWGatheringPhysics::Safe(RestBeforeDissolve,2.f),DWGatheringPhysics::Safe(DissolveDuration,1.15f));
            CurrentDissolve=FMath::Max(CurrentDissolve,Amount);
            Piece.DissolveAmount=Amount;
            for(const auto& MID:Piece.DissolveMIDs)if(IsValid(MID))MID->SetScalarParameterValue(TEXT("DissolveAmount"),Amount);
            if(Amount>=1.f)
            {
                C->OnComponentHit.RemoveDynamic(this,&UDWGatheringPresentationComponent::HandlePhysicsHit);
                C->SetNotifyRigidBodyCollision(false);C->SetSimulatePhysics(false);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                C->DestroyComponent();Piece.Component=nullptr;Piece.DissolveMIDs.Reset();Piece.bFinished=true;
            }
        }
        if(!Piece.bFinished)AllFinished=false;
    }
    if(Style==EDWGatheringDestructionStyle::TreeFall&&IsValid(Stump))
    {
        // The stump remains anchored while the upper tree physically falls. It
        // starts the same hold/dissolve curve only after a real ground contact.
        const float Elapsed=StumpContactWorldSeconds<0.?-1.f:FMath::Max(0.f,float(GetWorld()->GetTimeSeconds()-StumpContactWorldSeconds));
        const float Amount=DWGatheringPhysicsRules::DissolveAfterContact(Elapsed,
            DWGatheringPhysics::Safe(RestBeforeDissolve,2.f),DWGatheringPhysics::Safe(DissolveDuration,1.15f));
        for(const auto& MID:StumpDissolve.DissolveMIDs)if(IsValid(MID))MID->SetScalarParameterValue(TEXT("DissolveAmount"),Amount);
        if(Amount>=1.f)
        {
            auto* OldStump=Stump.Get();OldStump->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            if(!WeakThis.IsValid()||PhysicsGeneration!=Generation||!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed()||Stump!=OldStump)return;
            OldStump->DestroyComponent();
            if(!WeakThis.IsValid()||PhysicsGeneration!=Generation||!IsValid(GetOwner())||GetOwner()->IsActorBeingDestroyed())return;
            if(Stump==OldStump)Stump=nullptr;
            StumpDissolve=FDWGatheringPieceRuntime();
        }
        else AllFinished=false;
    }
    if(!IsValid(Stump))StumpDissolve=FDWGatheringPieceRuntime();
    if(bContactedGround){DustElapsed+=DeltaTime;UpdateDust(DustElapsed);}
    if(AllFinished)
    {
        CurrentDissolve=1.f;bPresentationFinished=true;ClearTransientVisuals(true);
        Source->SetVisibility(false,false);SetComponentTickEnabled(false);
        if(auto* Resource=Cast<ADWGatheringResource>(GetOwner()))Resource->PublishInteractionState();
    }
}

void UDWGatheringPresentationComponent::SpawnDust()
{
    if(DustCount<=0)return;auto* M=DustMesh.LoadSynchronous();auto* Material=DustMaterial.LoadSynchronous();if(!M||!Material)return;
    DustMID=UMaterialInstanceDynamic::Create(Material,this);DustMID->SetVectorParameterValue(TEXT("CloudColor"),DustColor);
    DustDiameter=FMath::Max(1.f,float(M->GetBounds().BoxExtent.GetMax()*2.));
    for(int32 I=0;I<FMath::Clamp(DustCount,0,24);++I)
    {
        auto* C=DWGatheringPhysics::MakeMesh(GetOwner(),M);if(!C)continue;
        C->SetMaterial(0,DustMID);C->SetCastShadow(false);C->SetWorldScale3D(FVector(.001));Dust.Add(C);
    }
}
void UDWGatheringPresentationComponent::UpdateDust(float AfterContact)
{
    const float T=DWGatheringPresentationMath::Unit(AfterContact/FMath::Max(.1f,DWGatheringPhysics::Safe(DustDuration,.85f)));
    for(int32 I=0;I<Dust.Num();++I)
    {
        auto* C=Dust[I].Get();if(!IsValid(C))continue;const double Angle=I*2.3999632297;
        const float Radius=FMath::Max(1.f,DWGatheringPhysics::Safe(DustRadius,28.f));
        const float Size=Radius*2.f*(.65f+FMath::Sin(I*1.71f)*.15f)*(1.f-T)/DustDiameter;
        const float Travel=25.f+85.f*T;
        C->SetWorldLocation(ContactPoint+FVector(FMath::Cos(Angle)*Travel,FMath::Sin(Angle)*Travel,Radius*.4+35.f*T));
        C->SetWorldScale3D(FVector(FMath::Max(.001f,Size)));C->SetVisibility(T<1.f,false);
    }
}
void UDWGatheringPresentationComponent::ClearTransientVisuals(bool KeepStump)
{
    for(auto& Piece:Pieces)if(IsValid(Piece.Component))
    {
        Piece.Component->OnComponentHit.RemoveDynamic(this,&UDWGatheringPresentationComponent::HandlePhysicsHit);
        Piece.Component->SetSimulatePhysics(false);Piece.Component->DestroyComponent();
    }
    if(!KeepStump)Pieces.Reset();for(const auto& C:Dust)if(IsValid(C))C->DestroyComponent();
    Dust.Reset();DustMID=nullptr;PhysicsMaterial=nullptr;bFragmentsReady=false;
    if(!KeepStump&&IsValid(Stump)){Stump->DestroyComponent();Stump=nullptr;}
    if(!KeepStump)StumpDissolve=FDWGatheringPieceRuntime();
}
void UDWGatheringPresentationComponent::ResetPresentation()
{
    ++PhysicsGeneration;
    SetComponentTickEnabled(false);ClearTransientVisuals();
    if(Source.IsValid()&&bCaptured)
    {Source->SetWorldTransform(SourceRest);Source->SetVisibility(bOriginalVisible,false);Source->SetCollisionEnabled(SourceCollision);}
    Source.Reset();bCaptured=bExhaustionStarted=bContactedGround=bPresentationFinished=bPhysicsSetupFailed=false;
    PhysicsElapsed=CurrentDissolve=Progress=DustElapsed=0.f;HitElapsed=1.f;StumpContactWorldSeconds=-1.;
    AcceptedHitCount=GroundContactCount=BodyGroundContactCount=PhysicsHitEventCount=SettledGroundContactCount=MissingDissolveSlots=0;
}
void UDWGatheringPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{ResetPresentation();Super::EndPlay(Reason);}
