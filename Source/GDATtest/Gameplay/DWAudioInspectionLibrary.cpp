#include "DWAudioInspectionLibrary.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Components/AudioComponent.h"
#include "Components/Widget.h"
#include "BaseWidgetBlueprint.h"
#include "WidgetBlueprint.h"
#include "UIComponentWidgetBlueprintExtension.h"
#include "Extensions/UIComponent.h"
#include "Extensions/UIComponentContainer.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SlateSound.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifies/AnimNotify_PlaySound.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"
#include "Sections/MovieSceneAudioSection.h"

namespace DWAudioStaticDetails
{
struct FWalk
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    TSet<FString> Visited;
    int32 Remaining=20000;
    bool bLimited=false;
    void Add(const FString& Owner,const FString& Origin,const FString& Property,const FString& Kind,const FString& Sound,const FString& Status)
    {
        auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("owner"),Owner);R->SetStringField(TEXT("origin"),Origin);R->SetStringField(TEXT("property_path"),Property);
        R->SetStringField(TEXT("kind"),Kind);R->SetStringField(TEXT("sound"),Sound);R->SetStringField(TEXT("status"),Status);Rows.Add(MakeShared<FJsonValueObject>(R));
    }
    void Property(const FProperty* P,const void* Value,const FString& Owner,const FString& Origin,const FString& Key,int32 Depth)
    {
        if(!P||!Value)return;
        if(Depth>10||Remaining<=0){bLimited=true;return;}--Remaining;
        const FString VisitKey=FString::Printf(TEXT("%p:%p"),P,Value);
        if(Visited.Contains(VisitKey))return;Visited.Add(VisitKey);
        if(const FStructProperty* SP=CastField<FStructProperty>(P))
        {
            if(SP->Struct==FSlateSound::StaticStruct())
            {
                const FObjectPropertyBase* Resource=FindFProperty<FObjectPropertyBase>(SP->Struct,TEXT("ResourceObject"));
                const UObject* Sound=Resource?Resource->GetObjectPropertyValue_InContainer(Value):nullptr;
                Add(Owner,Origin,Key,TEXT("SlateSound"),GetPathNameSafe(Sound),Resource?TEXT("property read; legacy name not resolved"):TEXT("ResourceObject unavailable"));return;
            }
            for(TFieldIterator<FProperty> It(SP->Struct);It;++It)
                for(int32 I=0;I<It->ArrayDim;++I)
                    Property(*It,It->ContainerPtrToValuePtr<void>(Value,I),Owner,Origin,Key+TEXT(".")+It->GetName()+(It->ArrayDim>1?FString::Printf(TEXT("[%d]"),I):TEXT("")),Depth+1);
            return;
        }
        if(const FArrayProperty* AP=CastField<FArrayProperty>(P))
        {
            FScriptArrayHelper H(AP,Value);for(int32 I=0;I<H.Num();++I)Property(AP->Inner,H.GetRawPtr(I),Owner,Origin,Key+FString::Printf(TEXT("[%d]"),I),Depth+1);return;
        }
        if(const FMapProperty* MP=CastField<FMapProperty>(P))
        {
            FScriptMapHelper H(MP,Value);for(int32 I=0;I<H.GetMaxIndex();++I)if(H.IsValidIndex(I))
            {
                FString Label=FString::Printf(TEXT("{volatile_slot:%d}"),I);
                if(const FNameProperty* NameKey=CastField<FNameProperty>(MP->KeyProp))Label=TEXT("{name:")+NameKey->GetPropertyValue(H.GetKeyPtr(I)).ToString()+TEXT("}");
                else if(const FStrProperty* StringKey=CastField<FStrProperty>(MP->KeyProp))Label=TEXT("{string:")+StringKey->GetPropertyValue(H.GetKeyPtr(I))+TEXT("}");
                else if(const FIntProperty* IntKey=CastField<FIntProperty>(MP->KeyProp))Label=FString::Printf(TEXT("{int:%d}"),IntKey->GetPropertyValue(H.GetKeyPtr(I)));
                Property(MP->KeyProp,H.GetKeyPtr(I),Owner,Origin,Key+Label+TEXT(".key"),Depth+1);Property(MP->ValueProp,H.GetValuePtr(I),Owner,Origin,Key+Label+TEXT(".value"),Depth+1);
            }return;
        }
        if(const FSetProperty* Set=CastField<FSetProperty>(P))
        {
            FScriptSetHelper H(Set,Value);for(int32 I=0;I<H.GetMaxIndex();++I)if(H.IsValidIndex(I))Property(Set->ElementProp,H.GetElementPtr(I),Owner,Origin,Key+FString::Printf(TEXT("{volatile_slot:%d}"),I),Depth+1);return;
        }
        if(const FSoftObjectProperty* Soft=CastField<FSoftObjectProperty>(P))
        {
            if(Soft->PropertyClass&&Soft->PropertyClass->IsChildOf(USoundBase::StaticClass()))
                Add(Owner,Origin,Key,TEXT("SoftSound"),Soft->GetPropertyValue(Value).ToSoftObjectPath().ToString(),TEXT("soft path; not loaded"));return;
        }
        if(const FObjectPropertyBase* OP=CastField<FObjectPropertyBase>(P))
        {
            const UObject* Object=OP->GetObjectPropertyValue(Value);
            if((OP->PropertyClass&&OP->PropertyClass->IsChildOf(USoundBase::StaticClass()))||Cast<USoundBase>(Object))
                Add(Owner,Origin,Key,TEXT("SoundObject"),GetPathNameSafe(Object),TEXT("direct object property; no execution"));
        }
    }
    void Object(const UObject* O,const FString& Origin)
    {
        if(!O)return;Visited.Reset();Remaining=20000;
        for(TFieldIterator<FProperty> It(O->GetClass());It;++It)
            for(int32 I=0;I<It->ArrayDim;++I)
                Property(*It,It->ContainerPtrToValuePtr<void>(O,I),O->GetPathName(),Origin,It->GetName()+(It->ArrayDim>1?FString::Printf(TEXT("[%d]"),I):TEXT("")),0);
        if(Remaining<=0)Add(O->GetPathName(),Origin,TEXT("<scan>"),TEXT("Limit"),TEXT(""),TEXT("20000 property budget exceeded"));
    }
};
}
#endif

FString UDWAudioInspectionLibrary::InspectAsset(UObject* Asset)
{
    auto Root=MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("asset"),GetPathNameSafe(Asset));
    Root->SetBoolField(TEXT("read_only"),true);Root->SetBoolField(TEXT("runtime_execution"),false);
#if WITH_EDITOR
    if(const UBlueprint* BP=Cast<UBlueprint>(Asset))
    {
        TArray<TSharedPtr<FJsonValue>> Nodes,Components,AllNodeIndex;
        TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
        for(const UEdGraph* Graph:Graphs)if(Graph)
        for(const UEdGraphNode* Node:Graph->Nodes)if(Node)
        {
            FString Function;
            if(const UK2Node_CallFunction* Call=Cast<UK2Node_CallFunction>(Node))Function=Call->FunctionReference.GetMemberName().ToString();
            const FString Title=Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
            auto Index=MakeShared<FJsonObject>();Index->SetStringField(TEXT("graph"),Graph->GetPathName());Index->SetStringField(TEXT("guid"),Node->NodeGuid.ToString());Index->SetStringField(TEXT("class"),Node->GetClass()->GetPathName());Index->SetStringField(TEXT("title"),Title);Index->SetStringField(TEXT("function"),Function);
            if(const UK2Node_Variable* Var=Cast<UK2Node_Variable>(Node))
            {Index->SetStringField(TEXT("variable_member_name"),Var->VariableReference.GetMemberName().ToString());Index->SetStringField(TEXT("variable_declared_parent"),GetPathNameSafe(Var->VariableReference.GetMemberParentClass()));Index->SetStringField(TEXT("variable_source_class"),GetPathNameSafe(Var->GetVariableSourceClass()));Index->SetBoolField(TEXT("variable_self_context"),Var->VariableReference.IsSelfContext());}
            AllNodeIndex.Add(MakeShared<FJsonValueObject>(Index));
            bool bRelevant=Function.Contains(TEXT("Sound"),ESearchCase::IgnoreCase)||Function.Contains(TEXT("Audio"),ESearchCase::IgnoreCase)||Title.Contains(TEXT("Sound"),ESearchCase::IgnoreCase)||Title.Contains(TEXT("Audio"),ESearchCase::IgnoreCase);
            for(const UEdGraphPin* Pin:Node->Pins)if(Pin)
            {
                if(Cast<USoundBase>(Pin->DefaultObject.Get()))bRelevant=true;
                if(const UClass* PinClass=Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get()))
                    if(PinClass->IsChildOf(USoundBase::StaticClass())||PinClass->IsChildOf(UAudioComponent::StaticClass()))bRelevant=true;
            }
            if(!bRelevant)continue;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("graph"),Graph->GetPathName());Row->SetStringField(TEXT("node_guid"),Node->NodeGuid.ToString());
            Row->SetStringField(TEXT("node_class"),Node->GetClass()->GetPathName());Row->SetStringField(TEXT("title"),Title);Row->SetStringField(TEXT("function"),Function);Row->SetBoolField(TEXT("enabled"),Node->IsNodeEnabled());
            TArray<TSharedPtr<FJsonValue>> Pins;
            for(const UEdGraphPin* Pin:Node->Pins)if(Pin)
            {
                auto P=MakeShared<FJsonObject>();P->SetStringField(TEXT("name"),Pin->PinName.ToString());P->SetStringField(TEXT("pin_guid"),Pin->PinId.ToString());
                P->SetStringField(TEXT("default_object"),GetPathNameSafe(Pin->DefaultObject.Get()));P->SetStringField(TEXT("default_value"),Pin->DefaultValue);
                P->SetStringField(TEXT("category"),Pin->PinType.PinCategory.ToString());P->SetStringField(TEXT("type_object"),GetPathNameSafe(Pin->PinType.PinSubCategoryObject.Get()));
                TArray<TSharedPtr<FJsonValue>> Links;
                for(const UEdGraphPin* Link:Pin->LinkedTo)if(Link&&Link->GetOwningNode())Links.Add(MakeShared<FJsonValueString>(Link->GetOwningNode()->NodeGuid.ToString()+TEXT("/")+Link->PinName.ToString()));
                P->SetArrayField(TEXT("links"),Links);Pins.Add(MakeShared<FJsonValueObject>(P));
            }
            Row->SetArrayField(TEXT("pins"),Pins);Nodes.Add(MakeShared<FJsonValueObject>(Row));
        }
        if(BP->SimpleConstructionScript)
        for(const USCS_Node* Node:BP->SimpleConstructionScript->GetAllNodes())if(Node)
        if(UAudioComponent* Audio=Cast<UAudioComponent>(Node->ComponentTemplate.Get()))
        {
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("scs_variable"),Node->GetVariableName().ToString());Row->SetStringField(TEXT("template"),Audio->GetPathName());
            Row->SetStringField(TEXT("sound"),GetPathNameSafe(Audio->GetSound()));Row->SetNumberField(TEXT("volume_multiplier"),Audio->VolumeMultiplier);Row->SetNumberField(TEXT("pitch_multiplier"),Audio->PitchMultiplier);
            Row->SetBoolField(TEXT("auto_activate"),Audio->bAutoActivate);Row->SetStringField(TEXT("sound_class_override"),GetPathNameSafe(Audio->SoundClassOverride.Get()));Row->SetStringField(TEXT("attenuation"),GetPathNameSafe(Audio->GetAttenuationSettingsAsset().Get()));
            Components.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetStringField(TEXT("kind"),TEXT("Blueprint"));Root->SetNumberField(TEXT("graphs_scanned"),Graphs.Num());Root->SetArrayField(TEXT("all_node_index"),AllNodeIndex);Root->SetArrayField(TEXT("audio_node_candidates"),Nodes);Root->SetArrayField(TEXT("local_scs_audio_templates"),Components);
        DWAudioStaticDetails::FWalk Walk;
        UObject* CDO=BP->GeneratedClass?BP->GeneratedClass->GetDefaultObject(false):nullptr;
        Root->SetBoolField(TEXT("class_default_available"),CDO!=nullptr);if(CDO)Walk.Object(CDO,TEXT("blueprint_class_default"));
        TArray<TSharedPtr<FJsonValue>> Trees;
        const auto ScanTree=[&Trees,&Walk](const UWidgetTree* Tree,const FString& Origin)
        {
            auto Record=MakeShared<FJsonObject>();Record->SetStringField(TEXT("origin"),Origin);Record->SetStringField(TEXT("tree"),GetPathNameSafe(Tree));
            if(Tree){TArray<UWidget*> Widgets;Tree->GetAllWidgets(Widgets);Record->SetNumberField(TEXT("widget_count"),Widgets.Num());for(const UWidget* Widget:Widgets)Walk.Object(Widget,Origin);}
            Trees.Add(MakeShared<FJsonValueObject>(Record));
        };
        if(const UBaseWidgetBlueprint* WidgetBP=Cast<UBaseWidgetBlueprint>(BP))ScanTree(WidgetBP->WidgetTree.Get(),TEXT("editor_source_tree"));
        if(const UWidgetBlueprintGeneratedClass* WidgetClass=Cast<UWidgetBlueprintGeneratedClass>(BP->GeneratedClass))ScanTree(WidgetClass->GetWidgetTreeArchetype(),TEXT("generated_tree_archetype"));
        if(const UWidgetBlueprint* WidgetBP=Cast<UWidgetBlueprint>(BP))
        if(const UUIComponentWidgetBlueprintExtension* Ext=UWidgetBlueprintExtension::GetExtension<UUIComponentWidgetBlueprintExtension>(WidgetBP))
        {
            const FObjectPropertyBase* Prop=FindFProperty<FObjectPropertyBase>(Ext->GetClass(),TEXT("ComponentContainer"));
            const UUIComponentContainer* Container=Prop?Cast<UUIComponentContainer>(Prop->GetObjectPropertyValue_InContainer(Ext)):nullptr;
            Root->SetBoolField(TEXT("ui_component_container_available"),Container!=nullptr);
            if(Container)Container->ForEachComponentTarget([&Walk](const FUIComponentTarget& Target)
            {if(const UUIComponent* Component=Target.GetComponent())Walk.Object(Component,TEXT("editor_ui_component:")+Target.GetTargetName().ToString());});
        }
        TArray<TSharedPtr<FJsonValue>> Resolved;
        if(UBlueprintGeneratedClass* Actual=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))
        {
            TArray<const UBlueprintGeneratedClass*> Hierarchy;const bool bHierarchyOk=UBlueprintGeneratedClass::GetGeneratedClassesHierarchy(Actual,Hierarchy);Root->SetBoolField(TEXT("generated_class_hierarchy_ok"),bHierarchyOk);
            for(const UBlueprintGeneratedClass* Parent:Hierarchy)if(Parent&&Parent->SimpleConstructionScript)
            for(const USCS_Node* Node:Parent->SimpleConstructionScript->GetAllNodes())if(Node)
            if(UAudioComponent* Audio=Cast<UAudioComponent>(Node->GetActualComponentTemplate(Actual)))
            {
                auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("declared_class"),Parent->GetPathName());R->SetStringField(TEXT("variable"),Node->GetVariableName().ToString());
                R->SetStringField(TEXT("declared_template"),GetPathNameSafe(Node->ComponentTemplate.Get()));R->SetStringField(TEXT("resolved_template"),Audio->GetPathName());R->SetStringField(TEXT("sound"),GetPathNameSafe(Audio->GetSound()));
                R->SetNumberField(TEXT("volume"),Audio->VolumeMultiplier);R->SetNumberField(TEXT("pitch"),Audio->PitchMultiplier);R->SetBoolField(TEXT("auto_activate"),Audio->bAutoActivate);
                R->SetStringField(TEXT("sound_class_override"),GetPathNameSafe(Audio->SoundClassOverride.Get()));R->SetStringField(TEXT("attenuation"),GetPathNameSafe(Audio->GetAttenuationSettingsAsset().Get()));Resolved.Add(MakeShared<FJsonValueObject>(R));
                Walk.Object(Audio,TEXT("resolved_scs_template"));
            }
        }
        Root->SetArrayField(TEXT("widget_trees"),Trees);Root->SetArrayField(TEXT("reflected_sound_properties"),Walk.Rows);Root->SetBoolField(TEXT("property_scan_limited"),Walk.bLimited);Root->SetArrayField(TEXT("resolved_inherited_scs_audio"),Resolved);
        Root->SetStringField(TEXT("limitations"),TEXT("Name matching is a candidate filter; linked/dynamic pins, inherited templates, widget trees, macros and execution reachability require review."));
    }
    else if(const UAnimSequenceBase* Animation=Cast<UAnimSequenceBase>(Asset))
    {
        TArray<TSharedPtr<FJsonValue>> Events;
        for(const FAnimNotifyEvent& Event:Animation->Notifies)
        {
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Event.NotifyName.ToString());Row->SetNumberField(TEXT("track_index"),Event.TrackIndex);
            Row->SetNumberField(TEXT("display_time_seconds"),Event.GetTime());Row->SetNumberField(TEXT("trigger_time_seconds"),Event.GetTriggerTime());Row->SetNumberField(TEXT("end_trigger_time_seconds"),Event.GetEndTriggerTime());Row->SetBoolField(TEXT("is_state_notify"),Event.NotifyStateClass!=nullptr);
#if WITH_EDITORONLY_DATA
            Row->SetStringField(TEXT("guid"),Event.Guid.ToString());
#endif
            Row->SetStringField(TEXT("notify_class"),GetPathNameSafe(Event.Notify?Event.Notify->GetClass():nullptr));Row->SetStringField(TEXT("state_class"),GetPathNameSafe(Event.NotifyStateClass?Event.NotifyStateClass->GetClass():nullptr));
            if(const UAnimNotify_PlaySound* Sound=Cast<UAnimNotify_PlaySound>(Event.Notify.Get()))
            {Row->SetStringField(TEXT("sound"),GetPathNameSafe(Sound->Sound.Get()));Row->SetNumberField(TEXT("volume"),Sound->VolumeMultiplier);Row->SetNumberField(TEXT("pitch"),Sound->PitchMultiplier);Row->SetBoolField(TEXT("follow"),Sound->bFollow);Row->SetStringField(TEXT("attach_name"),Sound->AttachName.ToString());}
            Events.Add(MakeShared<FJsonValueObject>(Row));
        }
        Root->SetStringField(TEXT("kind"),TEXT("AnimSequenceBase"));Root->SetArrayField(TEXT("notifies"),Events);Root->SetStringField(TEXT("limitations"),TEXT("Custom notify classes and Blueprint-generated audio require separate graph inspection."));
    }
    else if(const AActor* Actor=Cast<AActor>(Asset))
    {
        DWAudioStaticDetails::FWalk Walk;Walk.Object(Actor,TEXT("loaded_actor_instance"));
        TInlineComponentArray<UActorComponent*> Components;Actor->GetComponents(Components);
        for(const UActorComponent* Component:Components)if(Component)Walk.Object(Component,TEXT("loaded_actor_component"));
        Root->SetStringField(TEXT("kind"),TEXT("LoadedActor"));Root->SetStringField(TEXT("actor_class"),GetPathNameSafe(Actor->GetClass()));
        Root->SetNumberField(TEXT("component_count"),Components.Num());Root->SetArrayField(TEXT("reflected_sound_properties"),Walk.Rows);Root->SetBoolField(TEXT("property_scan_limited"),Walk.bLimited);
        Root->SetStringField(TEXT("limitations"),TEXT("Loaded components only; dynamic objects and unloaded partition cells are not inspected."));
    }
    else if(const UMovieSceneSequence* Sequence=Cast<UMovieSceneSequence>(Asset))
    {
        TArray<TSharedPtr<FJsonValue>> Sections;
        const UMovieScene* Movie=Sequence->GetMovieScene();
        if(Movie)
        {
            const FFrameRate TickRate=Movie->GetTickResolution();const FFrameRate DisplayRate=Movie->GetDisplayRate();
            Root->SetStringField(TEXT("tick_resolution"),TickRate.ToPrettyText().ToString());
            Root->SetNumberField(TEXT("tick_resolution_numerator"),TickRate.Numerator);Root->SetNumberField(TEXT("tick_resolution_denominator"),TickRate.Denominator);
            Root->SetNumberField(TEXT("display_rate_numerator"),DisplayRate.Numerator);Root->SetNumberField(TEXT("display_rate_denominator"),DisplayRate.Denominator);
            const auto ScanTracks=[&Sections](const TArray<UMovieSceneTrack*>& Tracks,const FString& Binding)
            {
                for(const UMovieSceneTrack* Track:Tracks)if(Track)
                for(const UMovieSceneSection* Section:Track->GetAllSections())
                if(const UMovieSceneAudioSection* Audio=Cast<UMovieSceneAudioSection>(Section))
                {
                    auto R=MakeShared<FJsonObject>();R->SetStringField(TEXT("binding_guid"),Binding);R->SetStringField(TEXT("track"),Track->GetPathName());R->SetStringField(TEXT("section"),Audio->GetPathName());
                    R->SetStringField(TEXT("sound"),GetPathNameSafe(Audio->GetSound()));R->SetBoolField(TEXT("has_start"),Audio->HasStartFrame());R->SetBoolField(TEXT("has_end"),Audio->HasEndFrame());
                    if(Audio->HasStartFrame())R->SetNumberField(TEXT("start_tick"),Audio->GetInclusiveStartFrame().Value);
                    if(Audio->HasEndFrame())R->SetNumberField(TEXT("end_tick_exclusive"),Audio->GetExclusiveEndFrame().Value);
                    R->SetNumberField(TEXT("source_start_offset_ticks"),Audio->GetStartOffset().Value);R->SetBoolField(TEXT("legacy_loop_flag"),Audio->GetLooping());
                    Sections.Add(MakeShared<FJsonValueObject>(R));
                }
            };
            ScanTracks(Movie->GetTracks(),TEXT("master"));
            for(const FMovieSceneBinding& Binding:Movie->GetBindings())ScanTracks(Binding.GetTracks(),Binding.GetObjectGuid().ToString());
        }
        else Root->SetStringField(TEXT("error"),TEXT("Sequence has no MovieScene"));
        Root->SetStringField(TEXT("kind"),TEXT("MovieSceneSequence"));Root->SetArrayField(TEXT("audio_sections"),Sections);
        Root->SetStringField(TEXT("limitations"),TEXT("Subsequences must be inspected separately; volume/pitch channels, custom tracks and runtime spawn/overrides require further review."));
    }
    else Root->SetStringField(TEXT("error"),TEXT("Unsupported asset class"));
#else
    Root->SetStringField(TEXT("error"),TEXT("Editor-only audit is unavailable in a Game build"));
#endif
    FString Out;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Out));return Out;
}
