#include "DWCombatInspectionLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "UObject/UnrealType.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/ComponentDelegateBinding.h"
#if WITH_EDITOR
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_BaseMCDelegate.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CallFunction.h"
#endif

FString UDWCombatInspectionLibrary::InspectBlueprintBindings(UBlueprint* Blueprint)
{
    TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("read_only"),true);
    Root->SetStringField(TEXT("blueprint"),GetPathNameSafe(Blueprint));
    if(!Blueprint){Root->SetStringField(TEXT("error"),TEXT("Null blueprint"));}
    else
    {
#if WITH_EDITORONLY_DATA
        Root->SetNumberField(TEXT("status"),static_cast<int32>(Blueprint->Status));
#else
        Root->SetStringField(TEXT("status_unavailable"),TEXT("Editor-only blueprint compilation metadata"));
#endif
        Root->SetStringField(TEXT("parent"),GetPathNameSafe(Blueprint->ParentClass));
        TArray<TSharedPtr<FJsonValue>> Bindings;
        for(UClass* Class=Blueprint->GeneratedClass;Class;Class=Class->GetSuperClass())
        {
            if(const auto* Generated=Cast<UBlueprintGeneratedClass>(Class))
            for(const auto& Dynamic:Generated->DynamicBindingObjects)
            if(const auto* Component=Cast<UComponentDelegateBinding>(Dynamic.Get()))
            for(const auto& Binding:Component->ComponentDelegateBindings)
            {
                auto Record=MakeShared<FJsonObject>();
                Record->SetStringField(TEXT("class"),Class->GetPathName());
                Record->SetStringField(TEXT("component"),Binding.ComponentPropertyName.ToString());
                Record->SetStringField(TEXT("delegate"),Binding.DelegatePropertyName.ToString());
                Record->SetStringField(TEXT("function"),Binding.FunctionNameToBind.ToString());
                Bindings.Add(MakeShared<FJsonValueObject>(Record));
            }
        }
        Root->SetArrayField(TEXT("compiled_component_bindings"),Bindings);
#if WITH_EDITOR
        TArray<UEdGraph*> Graphs;Blueprint->GetAllGraphs(Graphs);
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for(const UEdGraph* Graph:Graphs)if(Graph)
        for(const UEdGraphNode* Node:Graph->Nodes)if(Node)
        {
            auto Record=MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("graph"),Graph->GetName());
            Record->SetStringField(TEXT("node"),Node->GetClass()->GetName());
            Record->SetStringField(TEXT("guid"),Node->NodeGuid.ToString());
            Record->SetStringField(TEXT("title"),Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
            if(const auto* Event=Cast<UK2Node_ComponentBoundEvent>(Node))
                Record->SetStringField(TEXT("delegate"),Event->DelegatePropertyName.ToString());
            if(const auto* Delegate=Cast<UK2Node_BaseMCDelegate>(Node))
                Record->SetStringField(TEXT("delegate"),Delegate->DelegateReference.GetMemberName().ToString());
            if(const auto* Function=Cast<UK2Node_CallFunction>(Node))
                Record->SetStringField(TEXT("function"),Function->FunctionReference.GetMemberName().ToString());
            int32 LinkedPins=0;for(const auto* Pin:Node->Pins)if(Pin)LinkedPins+=Pin->LinkedTo.Num();
            Record->SetNumberField(TEXT("linked_pin_connections"),LinkedPins);
            Nodes.Add(MakeShared<FJsonValueObject>(Record));
        }
        Root->SetArrayField(TEXT("graph_nodes"),Nodes);
#endif
    }
    FString Json;const auto Writer=TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(Root,Writer);return Json;
}

UBlueprint* UDWCombatInspectionLibrary::GetExistingLevelScriptBlueprint(UWorld* World)
{
#if WITH_EDITOR
    return World&&World->PersistentLevel?World->PersistentLevel->GetLevelScriptBlueprint(true):nullptr;
#else
    return nullptr;
#endif
}
FString UDWCombatInspectionLibrary::InspectObjectDelegates(UObject* Object)
{
    auto Root=MakeShared<FJsonObject>();Root->SetStringField(TEXT("object"),GetPathNameSafe(Object));
    TArray<TSharedPtr<FJsonValue>> Delegates;
    if(IsValid(Object))for(TFieldIterator<FMulticastDelegateProperty> It(Object->GetClass());It;++It)
    {
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("delegate"),It->GetName());
        FString Bound;It->ExportTextItem_InContainer(Bound,Object,nullptr,Object,PPF_None);
        Row->SetStringField(TEXT("bindings"),Bound);Delegates.Add(MakeShared<FJsonValueObject>(Row));
    }
    Root->SetArrayField(TEXT("multicast_delegates"),Delegates);
    FString Json;const auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Root,Writer);return Json;
}
