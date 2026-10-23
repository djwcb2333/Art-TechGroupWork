#include "DWWeaponVerificationLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/App.h"

FString UDWWeaponVerificationLibrary::SampleDeformedSurface(UObject* Context,USkeletalMesh* Mesh,UAnimSequence* Animation,float TimeSeconds)
{
#if WITH_EDITOR
    if(!FApp::CanEverRender())return TEXT("{\"error\":\"Actual CPU surface sampling requires an enabled rendering environment\"}");
    if(!FMath::IsFinite(TimeSeconds))return TEXT("{\"error\":\"Non-finite sample time\"}");
    UWorld* World=GEngine?GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull):nullptr;
    if(!World||!Mesh||!Animation||Animation->GetSkeleton()!=Mesh->GetSkeleton())return TEXT("{\"error\":\"Invalid world, mesh or exact skeleton\"}");
    USkeletalMeshComponent* Component=NewObject<USkeletalMeshComponent>(GetTransientPackage(),NAME_None,RF_Transient);
    Component->SetSkeletalMesh(Mesh);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCanEverAffectNavigation(false);
    Component->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Component->RegisterComponentWithWorld(World);
    Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Component->PlayAnimation(Animation,false);
    Component->SetPosition(FMath::Clamp(TimeSeconds,0.f,Animation->GetPlayLength()),false);
    Component->TickAnimation(0.f,false);
    Component->RefreshBoneTransforms();
    if(!Component->GetMeshObject())
    {Component->DestroyComponent();return TEXT("{\"error\":\"Skeletal render object is not ready for actual CPU surface sampling\"}");}
    TArray<FFinalSkinVertex> Vertices;
    Component->GetCPUSkinnedVertices(Vertices,0);
    TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("mesh"),Mesh->GetPathName());
    Result->SetStringField(TEXT("animation"),Animation->GetPathName());
    Result->SetNumberField(TEXT("time"),TimeSeconds);
    TArray<TSharedPtr<FJsonValue>> Positions,Indices;
    const FSkeletalMeshModel* Model=Mesh->GetImportedModel();
    const TArray<int32>* Map=Model&&Model->LODModels.Num()?&Model->LODModels[0].MeshToImportVertexMap:nullptr;
    Result->SetBoolField(TEXT("has_import_vertex_map"),Map&&Map->Num()==Vertices.Num());
    for(int32 I=0;I<Vertices.Num();++I)
    {
        const FVector3f P=Vertices[I].Position;
        TArray<TSharedPtr<FJsonValue>> V;V.Add(MakeShared<FJsonValueNumber>(P.X));V.Add(MakeShared<FJsonValueNumber>(P.Y));V.Add(MakeShared<FJsonValueNumber>(P.Z));
        Positions.Add(MakeShared<FJsonValueArray>(V));
        Indices.Add(MakeShared<FJsonValueNumber>(Map&&Map->IsValidIndex(I)?(*Map)[I]:INDEX_NONE));
    }
    Result->SetArrayField(TEXT("component_positions_cm"),Positions);
    Result->SetArrayField(TEXT("import_vertex_indices"),Indices);
    TSharedRef<FJsonObject> Curves=MakeShared<FJsonObject>();
    if(UAnimInstance* Instance=Component->GetAnimInstance())
    {
        TArray<FName> Names;Instance->GetAllCurveNames(Names);
        for(FName Name:Names)if(Name.ToString().StartsWith(TEXT("COR_")))Curves->SetNumberField(Name.ToString(),Instance->GetCurveValue(Name));
    }
    Result->SetObjectField(TEXT("evaluated_corrective_curves"),Curves);
    Component->DestroyComponent();
    FString Output;auto Writer=TJsonWriterFactory<>::Create(&Output);FJsonSerializer::Serialize(Result,Writer);return Output;
#else
    return TEXT("{\"error\":\"Editor-only surface verification\"}");
#endif
}
