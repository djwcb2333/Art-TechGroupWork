#include "DesertBuildingCutoutComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialFunctionInterface.h"
#include "DesertBuilding.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "ShaderCore.h"
#endif
UDesertBuildingCutoutComponent::UDesertBuildingCutoutComponent()
{PrimaryComponentTick.bCanEverTick=false;bAutoActivate=true;}
TArray<float> UDesertBuildingCutoutComponent::ReadRuntimePrimitiveData(const UPrimitiveComponent* Mesh)
{return Mesh?Mesh->GetCustomPrimitiveData().Data:TArray<float>();}
TArray<FString> UDesertBuildingCutoutComponent::FinishMaterialCompilationAndReadErrors(UMaterialInterface* Material)
{
#if WITH_EDITOR
    UMaterial* Base=Material?Material->GetMaterial():nullptr;
    if(!Base)return {TEXT("Missing base material.")};
    FMaterialResource* Resource=Base->GetMaterialResource(SP_PCD3D_SM6,EMaterialQualityLevel::High);
    if(!Resource)return {TEXT("No SM6 High material resource; shader validation unavailable.")};
    Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
    Resource->FinishCompilation();
    if(!Resource->IsGameThreadShaderMapComplete())
    {TArray<FString> Errors=Resource->GetCompileErrors();if(Errors.IsEmpty())Errors.Add(TEXT("SM6 High shader map incomplete; validation not passed."));return Errors;}
    return Resource->GetCompileErrors();
#else
    return {TEXT("Shader compilation diagnostics require the Editor target.")};
#endif
}
FGuid UDesertBuildingCutoutComponent::ReadDependencyAssetStateId(const UObject* Asset)
{
    if(const auto* C=Cast<UMaterialParameterCollection>(Asset))return C->StateId;
    if(const auto* F=Cast<UMaterialFunctionInterface>(Asset))return F->StateId;
    return FGuid();
}
TArray<FGuid> UDesertBuildingCutoutComponent::ReadCompiledDependencyStateIds(UMaterialInterface* Material,bool Functions)
{
#if WITH_EDITOR
    UMaterial* Base=Material?Material->GetMaterial():nullptr;
    FMaterialResource* Resource=Base?Base->GetMaterialResource(SP_PCD3D_SM6,EMaterialQualityLevel::High):nullptr;
    if(Resource&&Resource->GetGameThreadShaderMap())
    {const auto& Id=Resource->GetGameThreadShaderMap()->GetShaderMapId();return Functions?Id.ReferencedFunctions:Id.ReferencedParameterCollections;}
#endif
    return {};
}
void UDesertBuildingCutoutComponent::RefreshEligibility()
{
    ADesertBuilding* Building=Cast<ADesertBuilding>(GetOwner());UWorld* W=GetWorld();
    if(!bCutoutEnabled||!IsActive()||!HasBegunPlay()||!Building||!W||!W->IsGameWorld()||W->bIsTearingDown)
    {ReleaseEligibility();return;}
    TSet<UPrimitiveComponent*> Current;
    for(int32 Floor=1;Floor<=Building->GetFloorCount();++Floor)
        for(UInstancedStaticMeshComponent* Mesh:Building->GetFloorComponents(Floor))
            if(IsValid(Mesh)&&Mesh->GetOwner()==Building&&Mesh->GetStaticMesh()&&Mesh->GetInstanceCount()>0&&
               !Mesh->ComponentHasTag(TEXT("DesertFoundation")))
            {
                bool Supported=Mesh->GetNumMaterials()>0;
                for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
                {
                    UMaterialInterface* Material=Mesh->GetMaterial(Slot);TArray<FMaterialParameterInfo> Parameters;TArray<FGuid> Ids;
                    if(Material)Material->GetAllScalarParameterInfo(Parameters,Ids);
                    if(!Parameters.ContainsByPredicate([](const FMaterialParameterInfo& Info){return Info.Name==TEXT("DBL_PlayerCutoutEligible");})){Supported=false;break;}
                }
                // Unknown custom shaders may use CPD16 for something else: leave that entire component untouched.
                if(Supported)Current.Add(Mesh);
            }
    for(auto It=PreviousValues.CreateIterator();It;++It)
        if(!It.Key().IsValid()||!Current.Contains(It.Key().Get()))
        {
            if(UPrimitiveComponent* Mesh=It.Key().Get())
            {const auto& Data=Mesh->GetCustomPrimitiveData().Data;if(Data.IsValidIndex(EligibilityIndex)&&Data[EligibilityIndex]==1.f)Mesh->SetCustomPrimitiveDataFloat(EligibilityIndex,It.Value());}
            It.RemoveCurrent();
        }
    for(UPrimitiveComponent* Mesh:Current)
    {
        if(PreviousValues.Contains(Mesh))continue;
        const auto& Data=Mesh->GetCustomPrimitiveData().Data;const float Original=Data.IsValidIndex(EligibilityIndex)?Data[EligibilityIndex]:0.f;
        if(Original!=0.f)
        {UE_LOG(LogTemp,Warning,TEXT("Designer cutout left occupied CPD16 unchanged: %s (%g)."),*Mesh->GetPathName(),Original);continue;}
        PreviousValues.Add(Mesh,Original);Mesh->SetCustomPrimitiveDataFloat(EligibilityIndex,1.f);
    }
}
void UDesertBuildingCutoutComponent::ReleaseEligibility()
{
    for(const auto& Pair:PreviousValues)if(UPrimitiveComponent* Mesh=Pair.Key.Get())
    {const auto& Data=Mesh->GetCustomPrimitiveData().Data;if(Data.IsValidIndex(EligibilityIndex)&&Data[EligibilityIndex]==1.f)Mesh->SetCustomPrimitiveDataFloat(EligibilityIndex,Pair.Value);}
    PreviousValues.Reset();
}
void UDesertBuildingCutoutComponent::SetCutoutEnabled(bool Enabled)
{bCutoutEnabled=Enabled;RefreshEligibility();}
void UDesertBuildingCutoutComponent::BeginPlay(){Super::BeginPlay();RefreshEligibility();}
void UDesertBuildingCutoutComponent::EndPlay(const EEndPlayReason::Type Reason){ReleaseEligibility();Super::EndPlay(Reason);}
void UDesertBuildingCutoutComponent::OnUnregister(){ReleaseEligibility();Super::OnUnregister();}
void UDesertBuildingCutoutComponent::Deactivate(){ReleaseEligibility();Super::Deactivate();}
void UDesertBuildingCutoutComponent::Activate(bool bReset){Super::Activate(bReset);if(IsActive())RefreshEligibility();}
