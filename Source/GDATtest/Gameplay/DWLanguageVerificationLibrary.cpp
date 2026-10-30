#include "DWLanguageVerificationLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
#include "DWCombatInspectionLibrary.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Composite.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Variable.h"
#include "K2Node_VariableSet.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#endif

namespace DWLanguageBlueprintAudit
{
using FObject=TSharedRef<FJsonObject>;
FString Encode(const FObject& O)
{FString S;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&S));return S;}
#if WITH_EDITOR
TArray<FString> ExactAssets()
{return {TEXT("/Game/DoughWorld/Gameplay/Buildings/BP_DWBuilding_CraftingTable.BP_DWBuilding_CraftingTable"),
         TEXT("/Game/DoughWorld/Gameplay/Buildings/BP_DWBuilding_Furnace.BP_DWBuilding_Furnace"),
         TEXT("/Game/DoughWorld/Gameplay/Buildings/BP_DWBuilding_FermentationVat.BP_DWBuilding_FermentationVat")};}
TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& In)
{TArray<TSharedPtr<FJsonValue>> Out;for(const FString& S:In)Out.Add(MakeShared<FJsonValueString>(S));return Out;}
TArray<FString> DirtyPackages()
{TArray<FString> Out;for(TObjectIterator<UPackage> It;It;++It)if(It->IsDirty()&&!It->HasAnyFlags(RF_Transient))Out.Add(It->GetName());Out.Sort();return Out;}
FString DiskHash(const FString& Asset)
{
    const FString Filename=FPackageName::LongPackageNameToFilename(FPackageName::ObjectPathToPackageName(Asset),FPackageName::GetAssetPackageExtension());
    TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Filename))return FString();
    return FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num()).ToString().ToLower();
}
FString Status(EBlueprintStatus S)
{
    switch(S){case BS_Unknown:return TEXT("Unknown");case BS_Dirty:return TEXT("Dirty");case BS_Error:return TEXT("Error");case BS_UpToDate:return TEXT("UpToDate");
    case BS_BeingCreated:return TEXT("BeingCreated");case BS_UpToDateWithWarnings:return TEXT("UpToDateWithWarnings");default:return TEXT("UnsupportedStatus");}
}
FString NodeId(const UEdGraphNode* N)
{return N?GetPathNameSafe(N->GetGraph())+TEXT("#")+N->NodeGuid.ToString(EGuidFormats::Digits):TEXT("<null>");}
FString PinId(const UEdGraphPin* P)
{return P?NodeId(P->GetOwningNode())+TEXT("/")+P->PinId.ToString(EGuidFormats::Digits):TEXT("<null>");}
FString GraphKind(const UBlueprint* BP,const UEdGraph* G)
{
    for(const auto& X:BP->FunctionGraphs)if(X.Get()==G)return TEXT("Function");for(const auto& X:BP->UbergraphPages)if(X.Get()==G)return TEXT("Ubergraph");
    for(const auto& X:BP->MacroGraphs)if(X.Get()==G)return TEXT("Macro");for(const auto& X:BP->DelegateSignatureGraphs)if(X.Get()==G)return TEXT("DelegateSignature");return TEXT("NestedOrOther");
}
bool PresentationName(FName Name)
{
    // Review hint only. This does not prove an implementation has no side effects.
    static const TSet<FName> Names={TEXT("SetStaticMesh"),TEXT("SetMaterial"),TEXT("CreateDynamicMaterialInstance"),TEXT("K2_SetRelativeTransform"),
        TEXT("K2_SetRelativeLocation"),TEXT("K2_SetRelativeRotation"),TEXT("SetRelativeScale3D"),TEXT("SetVisibility"),TEXT("SetHiddenInGame"),
        TEXT("SetCollisionEnabled"),TEXT("SetCollisionProfileName"),TEXT("SetScalarParameterValue"),TEXT("SetVectorParameterValue"),TEXT("SetTextureParameterValue"),
        TEXT("RebuildBuildingVisuals"),TEXT("RefreshVisualSources")};return Names.Contains(Name);
}
TArray<FString> ReviewTags(const UEdGraphNode* N,const FString& Function)
{
    TArray<FString> Tags;const FString C=N->GetClass()->GetName();
    if(Cast<UK2Node_VariableSet>(N))Tags.Add(TEXT("variable_write"));if(Cast<UK2Node_BaseMCDelegate>(N))Tags.Add(TEXT("delegate_bind_unbind_or_broadcast"));
    if(C.Contains(TEXT("Spawn")))Tags.Add(TEXT("spawn_node"));if(C.Contains(TEXT("Timeline")))Tags.Add(TEXT("timeline"));if(C.Contains(TEXT("Async")))Tags.Add(TEXT("async_node"));
    const TCHAR* Needles[]={TEXT("Inventory"),TEXT("AddItem"),TEXT("RemoveItem"),TEXT("Consume"),TEXT("Craft"),TEXT("Damage"),TEXT("Health"),TEXT("Kill"),
        TEXT("Collect"),TEXT("Broadcast"),TEXT("Dispatch"),TEXT("WorldEvent"),TEXT("SpawnActor"),TEXT("DestroyActor"),TEXT("OpenLevel"),TEXT("SaveGame"),TEXT("ConsoleCommand"),TEXT("ExecuteUbergraph")};
    for(const TCHAR* Needle:Needles)if(Function.Contains(Needle,ESearchCase::IgnoreCase))Tags.Add(FString(TEXT("name_requires_review:"))+Needle);return Tags;
}
void DumpGraphs(UBlueprint* BP,const FObject& Out)
{
    TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);TSet<const UEdGraph*> Seen;
    TArray<TSharedPtr<FJsonValue>> Gs,Calls,Findings,Roots;int32 NodeCount=0,PinCount=0,EdgeCount=0;
    for(const UEdGraph* G:Graphs)
    {
        if(!G||Seen.Contains(G))continue;Seen.Add(G);const auto GR=MakeShared<FJsonObject>();
        GR->SetStringField(TEXT("path"),G->GetPathName());GR->SetStringField(TEXT("name"),G->GetName());GR->SetStringField(TEXT("kind"),GraphKind(BP,G));GR->SetStringField(TEXT("schema"),GetPathNameSafe(G->GetSchema()));
        TArray<TSharedPtr<FJsonValue>> Nodes,Edges;
        for(const auto& NodePtr:G->Nodes)
        {
            const UEdGraphNode* N=NodePtr.Get();if(!N)continue;++NodeCount;const auto R=MakeShared<FJsonObject>();
            R->SetStringField(TEXT("id"),NodeId(N));R->SetStringField(TEXT("class"),N->GetClass()->GetPathName());R->SetStringField(TEXT("title"),N->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            R->SetStringField(TEXT("guid"),N->NodeGuid.ToString(EGuidFormats::Digits));R->SetNumberField(TEXT("x"),N->NodePosX);R->SetNumberField(TEXT("y"),N->NodePosY);
            R->SetBoolField(TEXT("compiler_has_message"),N->bHasCompilerMessage);R->SetStringField(TEXT("compiler_message"),N->ErrorMsg);FString Called;
            R->SetBoolField(TEXT("node_enabled"),N->IsNodeEnabled());R->SetNumberField(TEXT("desired_enabled_state"),static_cast<int32>(N->GetDesiredEnabledState()));
            if(const auto* C=Cast<UK2Node_CallFunction>(N))
            {
                Called=C->FunctionReference.GetMemberName().ToString();const UFunction* F=C->GetTargetFunction();const auto Call=MakeShared<FJsonObject>();
                Call->SetStringField(TEXT("node"),NodeId(N));Call->SetStringField(TEXT("caller_graph"),G->GetPathName());Call->SetStringField(TEXT("member"),Called);
                Call->SetStringField(TEXT("declared_owner"),GetPathNameSafe(C->FunctionReference.GetMemberParentClass()));Call->SetStringField(TEXT("resolved_function"),GetPathNameSafe(F));
                Call->SetBoolField(TEXT("resolved"),F!=nullptr);Call->SetBoolField(TEXT("self_context"),C->FunctionReference.IsSelfContext());
                Call->SetBoolField(TEXT("native"),F&&F->HasAnyFunctionFlags(FUNC_Native));Call->SetBoolField(TEXT("blueprint_pure_flag"),F&&F->HasAnyFunctionFlags(FUNC_BlueprintPure));
                Call->SetBoolField(TEXT("const_flag"),F&&F->HasAnyFunctionFlags(FUNC_Const));Call->SetBoolField(TEXT("network_flag"),F&&F->HasAnyFunctionFlags(FUNC_Net));
                Call->SetBoolField(TEXT("presentation_name_hint_only"),PresentationName(C->FunctionReference.GetMemberName()));TArray<FString> Local;
                for(const UEdGraph* Candidate:Graphs)if(Candidate&&Candidate->GetFName()==C->FunctionReference.GetMemberName())Local.Add(Candidate->GetPathName());
                Call->SetArrayField(TEXT("matching_local_graphs"),Strings(Local));Calls.Add(MakeShared<FJsonValueObject>(Call));R->SetObjectField(TEXT("call"),Call);
            }
            if(const auto* V=Cast<UK2Node_Variable>(N))
            {R->SetStringField(TEXT("variable"),V->VariableReference.GetMemberName().ToString());R->SetStringField(TEXT("variable_owner"),GetPathNameSafe(V->GetVariableSourceClass()));R->SetBoolField(TEXT("variable_is_write"),Cast<UK2Node_VariableSet>(N)!=nullptr);}
            if(const auto* D=Cast<UK2Node_BaseMCDelegate>(N))R->SetStringField(TEXT("delegate"),D->DelegateReference.GetMemberName().ToString());
            if(const auto* E=Cast<UK2Node_Event>(N))
            {R->SetStringField(TEXT("event_member"),E->EventReference.GetMemberName().ToString());R->SetStringField(TEXT("custom_event"),E->CustomFunctionName.ToString());Roots.Add(MakeShared<FJsonValueString>(NodeId(N)));}
            if(Cast<UK2Node_FunctionEntry>(N))Roots.Add(MakeShared<FJsonValueString>(NodeId(N)));
            if(const auto* E=Cast<UK2Node_ComponentBoundEvent>(N))R->SetStringField(TEXT("component_delegate"),E->DelegatePropertyName.ToString());
            if(const auto* M=Cast<UK2Node_MacroInstance>(N))
            {R->SetStringField(TEXT("macro_graph"),GetPathNameSafe(M->GetMacroGraph()));R->SetBoolField(TEXT("macro_in_allowed_blueprint"),M->GetMacroGraph()&&M->GetMacroGraph()->GetTypedOuter<UBlueprint>()==BP);}
            if(const auto* C=Cast<UK2Node_Composite>(N))R->SetStringField(TEXT("collapsed_graph"),GetPathNameSafe(C->BoundGraph.Get()));
            const auto Tags=ReviewTags(N,Called);R->SetArrayField(TEXT("review_tags"),Strings(Tags));if(!Tags.IsEmpty())Findings.Add(MakeShared<FJsonValueObject>(R));
            TArray<TSharedPtr<FJsonValue>> Pins;
            for(const UEdGraphPin* P:N->Pins)
            {
                if(!P)continue;++PinCount;const auto Pin=MakeShared<FJsonObject>();Pin->SetStringField(TEXT("id"),PinId(P));Pin->SetStringField(TEXT("name"),P->PinName.ToString());
                Pin->SetStringField(TEXT("direction"),P->Direction==EGPD_Output?TEXT("out"):TEXT("in"));Pin->SetStringField(TEXT("category"),P->PinType.PinCategory.ToString());
                Pin->SetStringField(TEXT("subcategory"),P->PinType.PinSubCategory.ToString());Pin->SetStringField(TEXT("type_object"),GetPathNameSafe(P->PinType.PinSubCategoryObject.Get()));
                Pin->SetStringField(TEXT("default_value"),P->DefaultValue);Pin->SetStringField(TEXT("default_object"),GetPathNameSafe(P->DefaultObject.Get()));Pin->SetStringField(TEXT("default_text"),P->DefaultTextValue.ToString());
                TArray<FString> Links;
                for(const UEdGraphPin* L:P->LinkedTo)
                {
                    if(!L)continue;Links.Add(PinId(L));if(P->Direction==EGPD_Output)
                    {
                        ++EdgeCount;const auto Edge=MakeShared<FJsonObject>();Edge->SetStringField(TEXT("from_node"),NodeId(N));Edge->SetStringField(TEXT("from_pin"),PinId(P));
                        Edge->SetStringField(TEXT("to_node"),NodeId(L->GetOwningNode()));Edge->SetStringField(TEXT("to_pin"),PinId(L));
                        Edge->SetStringField(TEXT("kind"),P->PinType.PinCategory==UEdGraphSchema_K2::PC_Exec?TEXT("exec"):TEXT("data"));Edges.Add(MakeShared<FJsonValueObject>(Edge));
                    }
                }
                Pin->SetArrayField(TEXT("links"),Strings(Links));Pins.Add(MakeShared<FJsonValueObject>(Pin));
            }
            R->SetArrayField(TEXT("pins"),Pins);Nodes.Add(MakeShared<FJsonValueObject>(R));
        }
        GR->SetArrayField(TEXT("nodes"),Nodes);GR->SetArrayField(TEXT("edges"),Edges);Gs.Add(MakeShared<FJsonValueObject>(GR));
    }
    Out->SetArrayField(TEXT("graphs"),Gs);Out->SetArrayField(TEXT("calls"),Calls);Out->SetArrayField(TEXT("review_tagged_nodes"),Findings);Out->SetArrayField(TEXT("entry_roots"),Roots);
    Out->SetNumberField(TEXT("graph_count"),Gs.Num());Out->SetNumberField(TEXT("node_count"),NodeCount);Out->SetNumberField(TEXT("pin_count"),PinCount);Out->SetNumberField(TEXT("output_edge_count"),EdgeCount);
    Out->SetBoolField(TEXT("manual_side_effect_review_required"),true);Out->SetBoolField(TEXT("side_effects_proven_absent"),false);
}
#endif
}

FString UDWLanguageVerificationLibrary::InspectBuildingBlueprints(bool bCompileInMemory)
{
    using namespace DWLanguageBlueprintAudit;const auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("schema"),TEXT("LANG01_BUILDING_BLUEPRINT_EVIDENCE_v1"));
    Root->SetBoolField(TEXT("saved"),false);Root->SetBoolField(TEXT("graphs_executed"),false);Root->SetBoolField(TEXT("actors_spawned"),false);
    Root->SetBoolField(TEXT("side_effects_proven_absent"),false);Root->SetBoolField(TEXT("manual_review_required"),true);
#if !WITH_EDITOR
    Root->SetBoolField(TEXT("supported"),false);Root->SetStringField(TEXT("error"),TEXT("Editor-only blueprint graph evidence is unavailable in a Game build."));
#else
    Root->SetBoolField(TEXT("supported"),true);Root->SetBoolField(TEXT("compile_requested"),bCompileInMemory);
    if(!IsInGameThread()||!GEditor||GEditor->PlayWorld){Root->SetStringField(TEXT("error"),TEXT("Requires the editor game thread outside PIE/SIE."));return Encode(Root);}
    const TArray<FString> Before=DirtyPackages();Root->SetArrayField(TEXT("dirty_packages_before"),Strings(Before));TArray<TSharedPtr<FJsonValue>> Assets;bool AllLoaded=true,DiskUnchanged=true;
    for(const FString& Path:ExactAssets())
    {
        const auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("asset"),Path);const FString BeforeHash=DiskHash(Path);Row->SetStringField(TEXT("disk_sha1_before"),BeforeHash);
        UBlueprint* BP=LoadObject<UBlueprint>(nullptr,*Path);
        if(!BP||BP->GetPathName()!=Path){AllLoaded=false;Row->SetStringField(TEXT("error"),TEXT("Exact allowlisted blueprint could not be loaded."));Assets.Add(MakeShared<FJsonValueObject>(Row));continue;}
        const bool WasDirty=BP->GetOutermost()->IsDirty();Row->SetBoolField(TEXT("package_dirty_after_load"),WasDirty);Row->SetStringField(TEXT("status_before"),Status(BP->Status));
        Row->SetStringField(TEXT("parent_class"),GetPathNameSafe(BP->ParentClass));Row->SetStringField(TEXT("generated_class"),GetPathNameSafe(BP->GeneratedClass));
        Row->SetStringField(TEXT("parent_blueprint"),BP->ParentClass?GetPathNameSafe(BP->ParentClass->ClassGeneratedBy):TEXT("None"));
        // Reuse the existing public inspector for compiled/native component delegate bindings.
        TSharedPtr<FJsonObject> Existing;const FString Bindings=UDWCombatInspectionLibrary::InspectBlueprintBindings(BP);
        if(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Bindings),Existing)&&Existing)Row->SetObjectField(TEXT("existing_binding_evidence"),Existing);
        DumpGraphs(BP,Row);Row->SetBoolField(TEXT("compile_performed"),false);
        if(bCompileInMemory)
        {
            int32 LiveInstances=0;for(TObjectIterator<AActor> It;It;++It)if(BP->GeneratedClass&&It->IsA(BP->GeneratedClass)&&!It->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject))++LiveInstances;
            Row->SetNumberField(TEXT("live_target_instances"),LiveInstances);
            if(WasDirty||LiveInstances>0)Row->SetStringField(TEXT("compile_skipped_reason"),TEXT("Target package is dirty or has live instances; preserve the executor's current state."));
            else
            {
                FCompilerResultsLog Log;const EBlueprintCompileOptions Flags=EBlueprintCompileOptions::SkipSave|
                    EBlueprintCompileOptions::SkipGarbageCollection|EBlueprintCompileOptions::BatchCompile|EBlueprintCompileOptions::SkipFiBSearchMetaUpdate;
                FKismetEditorUtilities::CompileBlueprint(BP,Flags,&Log);Row->SetBoolField(TEXT("compile_performed"),true);Row->SetBoolField(TEXT("save_on_compile_suppressed"),true);
                Row->SetBoolField(TEXT("reinstancing_suppressed"),false);Row->SetNumberField(TEXT("compile_errors"),Log.NumErrors);Row->SetNumberField(TEXT("compile_warnings"),Log.NumWarnings);
                TArray<FString> Messages;for(const auto& M:Log.Messages)Messages.Add(M->ToText().ToString());Row->SetArrayField(TEXT("compile_messages"),Strings(Messages));
            }
        }
        Row->SetStringField(TEXT("status_after"),Status(BP->Status));Row->SetBoolField(TEXT("package_dirty_after"),BP->GetOutermost()->IsDirty());const FString AfterHash=DiskHash(Path);Row->SetStringField(TEXT("disk_sha1_after"),AfterHash);
        const bool Same=!BeforeHash.IsEmpty()&&BeforeHash==AfterHash;DiskUnchanged&=Same;Row->SetBoolField(TEXT("disk_bytes_unchanged"),Same);Assets.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("assets"),Assets);Root->SetBoolField(TEXT("all_three_loaded"),AllLoaded&&Assets.Num()==3);Root->SetBoolField(TEXT("all_disk_bytes_unchanged"),DiskUnchanged);
    const TArray<FString> After=DirtyPackages();Root->SetArrayField(TEXT("dirty_packages_after"),Strings(After));TArray<FString> NewlyDirty;
    for(const FString& P:After)if(!Before.Contains(P))NewlyDirty.Add(P);Root->SetArrayField(TEXT("newly_dirty_packages_unsaved"),Strings(NewlyDirty));
    Root->SetStringField(TEXT("interpretation"),TEXT("All authored nodes, execution/data links and local call hints are evidence. Compiler success, pure/const flags and presentation-name hints do not prove absence of inventory, event or damage effects. Inspect entry chains and external/native targets separately. No dirty flag is cleared and no package is saved."));
#endif
    return Encode(Root);
}
