#include "DWCombatAuthoringLibrary.h"
#include "DWCombatAnimInstance.h"
#include "Animation/AnimBlueprint.h"

#if WITH_EDITOR
#include <initializer_list>
#include "DWEnemyCharacter.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateAliasNode.h"
#include "AnimStateTransitionNode.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/PackageName.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DWCombatAuthoring
{
    template<typename T>
    T* AddNode(UEdGraph* Graph, int32 X, int32 Y)
    {
        FGraphNodeCreator<T> Creator(*Graph);
        T* NewNode=Creator.CreateNode();
        NewNode->NodePosX=X;
        NewNode->NodePosY=Y;
        Creator.Finalize();
        return NewNode;
    }

    UEdGraphPin* FirstOutput(UEdGraphNode* GraphNode)
    {
        for (UEdGraphPin* Pin : GraphNode->Pins)
            if (Pin && Pin->Direction==EGPD_Output) return Pin;
        return nullptr;
    }

    bool Connect(UEdGraph* Graph, UEdGraphPin* Output, UEdGraphPin* Input, FString& Error)
    {
        if (Output && Input && Graph->GetSchema()->TryCreateConnection(Output,Input)) return true;
        Error=FString::Printf(TEXT("Cannot connect %s -> %s in graph %s"),
            Output ? *Output->PinName.ToString() : TEXT("missing output"),
            Input ? *Input->PinName.ToString() : TEXT("missing input"), *Graph->GetName());
        return false;
    }

    UK2Node_VariableGet* ReadVariable(UEdGraph* Graph, const FName Name, int32 X, int32 Y)
    {
        FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
        UK2Node_VariableGet* Variable=Creator.CreateNode();
        Variable->VariableReference.SetSelfMember(Name);
        Variable->NodePosX=X;
        Variable->NodePosY=Y;
        Creator.Finalize();
        return Variable;
    }

    bool ExposePin(UAnimGraphNode_Base* GraphNode, FName PropertyName)
    {
        for (int32 Index=0;Index<GraphNode->ShowPinForProperties.Num();++Index)
        {
            if (GraphNode->ShowPinForProperties[Index].PropertyName==PropertyName)
            {
                GraphNode->SetPinVisibility(true,Index);
                return GraphNode->FindPin(PropertyName)!=nullptr;
            }
        }
        return false;
    }

    struct FStateSpec
    {
        EDWCombatAnimationState Value;
        const TCHAR* Name;
        const TCHAR* SequenceProperty;
        const TCHAR* RateProperty;
        bool bLoop;
    };

    bool FillState(UAnimStateNode* StateNode, const FStateSpec& Spec, UAnimSequenceBase* Preview, FString& Error)
    {
        UEdGraph* StateGraph=StateNode->BoundGraph;
        StateNode->bAlwaysResetOnEntry=true;
        FBlueprintEditorUtils::RenameGraph(StateGraph,Spec.Name);
        UAnimGraphNode_Base* AssetNode=nullptr;
        const TCHAR* TimeProperty=Spec.Value==EDWCombatAnimationState::Throw ? TEXT("ThrowPositionSeconds")
            : Spec.Value==EDWCombatAnimationState::Melee ? TEXT("MeleePositionSeconds")
            : Spec.Value==EDWCombatAnimationState::Attack ? TEXT("AttackPositionSeconds") : nullptr;
        if (TimeProperty)
        {
            UAnimGraphNode_SequenceEvaluator* Evaluator=AddNode<UAnimGraphNode_SequenceEvaluator>(StateGraph,0,0);
            Evaluator->Node.SetSequence(Preview);
            Evaluator->Node.SetShouldLoop(false);
            // Teleport=true preserves exact clip selection and consecutive attack restarts.
            // Gameplay owns hit timing; this node intentionally does not fire damage notifies.
            Evaluator->NodeComment=Spec.Value==EDWCombatAnimationState::Throw
                ? TEXT("Exact throw clip clock: accepted throw start/end/rate are sampled by the character.")
                : TEXT("Combat action clock restarts each accepted attack, even if animation tick did not observe the intervening idle state.");
            Evaluator->bCommentBubbleVisible=true;
            AssetNode=Evaluator;
            if (!ExposePin(Evaluator,TEXT("Sequence")) || !ExposePin(Evaluator,TEXT("ExplicitTime")))
            { Error=TEXT("Could not expose Sequence Evaluator inputs."); return false; }
            UK2Node_VariableGet* Time=ReadVariable(StateGraph,TimeProperty,-340,160);
            if (!Connect(StateGraph,FirstOutput(Time),Evaluator->FindPin(TEXT("ExplicitTime")),Error)) return false;
        }
        else
        {
            UAnimGraphNode_SequencePlayer* Player=AddNode<UAnimGraphNode_SequencePlayer>(StateGraph,0,0);
            Player->Node.SetSequence(Preview);
            Player->Node.SetLoopAnimation(Spec.bLoop);
            AssetNode=Player;
            if (!ExposePin(Player,TEXT("Sequence")))
            { Error=TEXT("Could not expose Sequence Player Sequence input."); return false; }
            if (Spec.RateProperty)
            {
                if (!ExposePin(Player,TEXT("PlayRate")))
                { Error=TEXT("Could not expose Sequence Player Play Rate input."); return false; }
                UK2Node_VariableGet* Rate=ReadVariable(StateGraph,Spec.RateProperty,-340,160);
                if (!Connect(StateGraph,FirstOutput(Rate),Player->FindPin(TEXT("PlayRate")),Error)) return false;
            }
        }
        UK2Node_VariableGet* Sequence=ReadVariable(StateGraph,Spec.SequenceProperty,-340,0);
        if (!Connect(StateGraph,FirstOutput(Sequence),AssetNode->FindPin(TEXT("Sequence")),Error)) return false;
        UEdGraphPin* Sink=StateNode->GetPoseSinkPinInsideState();
        if (Sink) { Sink->GetOwningNode()->NodePosX=380; Sink->GetOwningNode()->NodePosY=0; }
        return Connect(StateGraph,FirstOutput(AssetNode),Sink,Error);
    }

    bool AddStateTransition(UEdGraph* MachineGraph, UAnimStateAliasNode* Sources,
        UAnimStateNode* Destination, const FStateSpec& Spec, int32 Y, FString& Error)
    {
        UAnimStateTransitionNode* Transition=AddNode<UAnimStateTransitionNode>(MachineGraph,260,Y);
        Transition->CreateConnections(Sources,Destination);
        // UE 5.8 PostPlacedNewNode enables an implicit inertialization fallback even
        // for standard blends. This graph has no inertialization node by design.
        Transition->LogicType=ETransitionLogicType::TLT_StandardBlend;
        Transition->bAllowInertializationForSelfTransitions=false;
        // Immediate action changes preserve the pre-existing throw/transform timing.
        // Move/Idle can use a small pose blend without delaying the gameplay state.
        Transition->CrossfadeDuration=(Spec.Value==EDWCombatAnimationState::Idle || Spec.Value==EDWCombatAnimationState::Move) ? .06f : 0.f;
        Transition->PriorityOrder=Spec.Value==EDWCombatAnimationState::Death ? 0 : 1;
        Transition->bAutomaticRuleBasedOnSequencePlayerInState=false;
        UAnimationTransitionGraph* RuleGraph=Cast<UAnimationTransitionGraph>(Transition->BoundGraph);
        if (!RuleGraph || !RuleGraph->MyResultNode) { Error=TEXT("Transition has no result graph."); return false; }
        UK2Node_VariableGet* Current=ReadVariable(RuleGraph,TEXT("AnimationState"),-500,0);
        UK2Node_EnumEquality* Equal=AddNode<UK2Node_EnumEquality>(RuleGraph,-220,0);
        if (!Connect(RuleGraph,FirstOutput(Current),Equal->GetInput1Pin(),Error)) return false;
        const FString EnumValue=StaticEnum<EDWCombatAnimationState>()->GetNameStringByValue(static_cast<int64>(Spec.Value));
        RuleGraph->GetSchema()->TrySetDefaultValue(*Equal->GetInput2Pin(),EnumValue);
        UAnimGraphNode_TransitionResult* Result=RuleGraph->MyResultNode;
        Result->NodePosX=80;
        Result->NodePosY=0;
        return Connect(RuleGraph,Equal->GetReturnValuePin(),Result->FindPin(TEXT("bCanEnterTransition")),Error);
    }

    FString AuditJson(const TSharedRef<FJsonObject>& AuditObject)
    {
        FString AuditText;
        const TSharedRef<TJsonWriter<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>> AuditWriter=
            TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&AuditText);
        FJsonSerializer::Serialize(AuditObject,AuditWriter);
        return AuditText;
    }

    bool ValidateRepairTarget(UAnimBlueprint* Blueprint, TArray<UAnimStateTransitionNode*>& OutTransitions, FString& Error)
    {
        OutTransitions.Reset();
        if (!Blueprint) { Error=TEXT("Null animation blueprint."); return false; }
        const FString AuditPath=Blueprint->GetPathName();
        const bool bAuditPlayer=AuditPath==TEXT("/Game/DoughWorld/Gameplay/Combat/ABP_DWPlayerCombat.ABP_DWPlayerCombat");
        const bool bAuditEnemy=AuditPath==TEXT("/Game/DoughWorld/Gameplay/Combat/ABP_DWEnemyCombat.ABP_DWEnemyCombat");
        if (!bAuditPlayer && !bAuditEnemy)
        { Error=TEXT("Only the two exact existing combat animation blueprint paths may be repaired."); return false; }
        if (Blueprint->ParentClass!=UDWCombatAnimInstance::StaticClass())
        { Error=TEXT("ParentClass must be exactly DWCombatAnimInstance."); return false; }
        const FName AuditMachineName=bAuditPlayer ? FName(TEXT("PlayerCombatStates")) : FName(TEXT("EnemyCombatStates"));
        TSet<FString> AuditExpectedStates;
        AuditExpectedStates.Add(TEXT("Idle")); AuditExpectedStates.Add(TEXT("Move")); AuditExpectedStates.Add(TEXT("Death"));
        if (bAuditPlayer)
        {
            AuditExpectedStates.Add(TEXT("Throw")); AuditExpectedStates.Add(TEXT("Transform"));
            AuditExpectedStates.Add(TEXT("Dash")); AuditExpectedStates.Add(TEXT("Melee"));
        }
        else { AuditExpectedStates.Add(TEXT("Alert")); AuditExpectedStates.Add(TEXT("Attack")); }
        TArray<UEdGraph*> AuditGraphs;
        Blueprint->GetAllGraphs(AuditGraphs);
        TSet<FString> AuditFoundStates;
        int32 AuditStateCount=0;
        int32 AuditMachineCount=0;
        for (UEdGraph* AuditGraph : AuditGraphs)
        {
            if (!AuditGraph) continue;
            const bool bAuditExpectedMachine=AuditGraph->GetFName()==AuditMachineName
                && AuditGraph->IsA<UAnimationStateMachineGraph>();
            if (AuditGraph->IsA<UAnimationStateMachineGraph>())
            {
                if (!bAuditExpectedMachine)
                { Error=TEXT("Unexpected additional or renamed state machine; refusing repair."); return false; }
                ++AuditMachineCount;
            }
            for (UEdGraphNode* AuditNode : AuditGraph->Nodes)
            {
                if (UAnimStateNode* AuditState=Cast<UAnimStateNode>(AuditNode))
                {
                    if (!bAuditExpectedMachine || !AuditState->BoundGraph)
                    { Error=TEXT("State is outside the expected combat machine or lacks its graph."); return false; }
                    const FString AuditStateName=AuditState->BoundGraph->GetName();
                    if (!AuditExpectedStates.Contains(AuditStateName))
                    { Error=TEXT("Unexpected state: ")+AuditStateName; return false; }
                    AuditFoundStates.Add(AuditStateName);
                    ++AuditStateCount;
                }
                if (UAnimStateTransitionNode* AuditTransition=Cast<UAnimStateTransitionNode>(AuditNode))
                {
                    if (!bAuditExpectedMachine || AuditTransition->LogicType!=ETransitionLogicType::TLT_StandardBlend)
                    { Error=TEXT("Unexpected transition location or non-standard blend; refusing to change authored blending."); return false; }
                    OutTransitions.Add(AuditTransition);
                }
            }
        }
        if (AuditMachineCount!=1 || AuditStateCount!=AuditExpectedStates.Num()
            || AuditFoundStates.Num()!=AuditExpectedStates.Num() || OutTransitions.Num()!=AuditExpectedStates.Num())
        {
            Error=FString::Printf(TEXT("Expected one machine with %d states and transitions; found %d machines, %d states, %d unique state names, %d transitions."),
                AuditExpectedStates.Num(),AuditMachineCount,AuditStateCount,AuditFoundStates.Num(),OutTransitions.Num());
            return false;
        }
        return true;
    }

    TSharedRef<FJsonObject> CaptureAudit(UAnimBlueprint* Blueprint)
    {
        TSharedRef<FJsonObject> AuditRoot=MakeShared<FJsonObject>();
        AuditRoot->SetStringField(TEXT("asset"),GetPathNameSafe(Blueprint));
        AuditRoot->SetStringField(TEXT("parent"),Blueprint ? GetPathNameSafe(Blueprint->ParentClass) : TEXT("None"));
        TArray<UAnimStateTransitionNode*> AuditTransitions;
        FString AuditError;
        AuditRoot->SetBoolField(TEXT("valid_repair_target"),ValidateRepairTarget(Blueprint,AuditTransitions,AuditError));
        AuditRoot->SetStringField(TEXT("validation_error"),AuditError);
        TArray<TSharedPtr<FJsonValue>> AuditTopology;
        TArray<TSharedPtr<FJsonValue>> AuditTransitionSettings;
        int32 AuditStateCount=0;
        int32 AuditEnabledCount=0;
        TArray<UEdGraph*> AuditGraphs;
        if (Blueprint) Blueprint->GetAllGraphs(AuditGraphs);
        for (UEdGraph* AuditGraph : AuditGraphs)
        {
            if (!AuditGraph) continue;
            for (UEdGraphNode* AuditNode : AuditGraph->Nodes)
            {
                if (!AuditNode) continue;
                TSharedRef<FJsonObject> AuditNodeJson=MakeShared<FJsonObject>();
                AuditNodeJson->SetStringField(TEXT("graph"),AuditGraph->GetPathName());
                AuditNodeJson->SetStringField(TEXT("graph_guid"),AuditGraph->GraphGuid.ToString());
                AuditNodeJson->SetStringField(TEXT("node"),AuditNode->GetName());
                AuditNodeJson->SetStringField(TEXT("class"),AuditNode->GetClass()->GetPathName());
                AuditNodeJson->SetStringField(TEXT("guid"),AuditNode->NodeGuid.ToString());
                AuditNodeJson->SetNumberField(TEXT("x"),AuditNode->NodePosX);
                AuditNodeJson->SetNumberField(TEXT("y"),AuditNode->NodePosY);
                if (UAnimStateNode* AuditState=Cast<UAnimStateNode>(AuditNode))
                {
                    ++AuditStateCount;
                    AuditNodeJson->SetStringField(TEXT("state"),GetNameSafe(AuditState->BoundGraph));
                    AuditNodeJson->SetBoolField(TEXT("always_reset_on_entry"),AuditState->bAlwaysResetOnEntry);
                }
                TArray<TSharedPtr<FJsonValue>> AuditPins;
                for (UEdGraphPin* AuditPin : AuditNode->Pins)
                {
                    if (!AuditPin) continue;
                    TSharedRef<FJsonObject> AuditPinJson=MakeShared<FJsonObject>();
                    AuditPinJson->SetStringField(TEXT("guid"),AuditPin->PinId.ToString());
                    AuditPinJson->SetStringField(TEXT("name"),AuditPin->PinName.ToString());
                    AuditPinJson->SetStringField(TEXT("direction"),AuditPin->Direction==EGPD_Input ? TEXT("input") : TEXT("output"));
                    AuditPinJson->SetStringField(TEXT("default_value"),AuditPin->DefaultValue);
                    AuditPinJson->SetStringField(TEXT("default_object"),GetPathNameSafe(AuditPin->DefaultObject));
                    AuditPinJson->SetStringField(TEXT("default_text"),AuditPin->DefaultTextValue.ToString());
                    TArray<FString> AuditLinkKeys;
                    for (UEdGraphPin* AuditLinkedPin : AuditPin->LinkedTo)
                        if (AuditLinkedPin && AuditLinkedPin->GetOwningNode())
                            AuditLinkKeys.Add(AuditLinkedPin->GetOwningNode()->NodeGuid.ToString()+TEXT(":")+AuditLinkedPin->PinId.ToString());
                    AuditLinkKeys.Sort();
                    TArray<TSharedPtr<FJsonValue>> AuditLinks;
                    for (const FString& AuditLinkKey : AuditLinkKeys) AuditLinks.Add(MakeShared<FJsonValueString>(AuditLinkKey));
                    AuditPinJson->SetArrayField(TEXT("links"),AuditLinks);
                    AuditPins.Add(MakeShared<FJsonValueObject>(AuditPinJson));
                }
                AuditNodeJson->SetArrayField(TEXT("pins"),AuditPins);
                AuditTopology.Add(MakeShared<FJsonValueObject>(AuditNodeJson));
                if (UAnimStateTransitionNode* AuditTransition=Cast<UAnimStateTransitionNode>(AuditNode))
                {
                    TSharedRef<FJsonObject> AuditTransitionJson=MakeShared<FJsonObject>();
                    AuditTransitionJson->SetStringField(TEXT("guid"),AuditTransition->NodeGuid.ToString());
                    AuditTransitionJson->SetNumberField(TEXT("logic_type"),static_cast<int32>(AuditTransition->LogicType.GetValue()));
                    AuditTransitionJson->SetBoolField(TEXT("allow_self_transition_inertialization"),AuditTransition->bAllowInertializationForSelfTransitions);
                    AuditTransitionJson->SetNumberField(TEXT("crossfade_seconds"),AuditTransition->CrossfadeDuration);
                    AuditTransitionJson->SetNumberField(TEXT("priority"),AuditTransition->PriorityOrder);
                    if (AuditTransition->bAllowInertializationForSelfTransitions) ++AuditEnabledCount;
                    AuditTransitionSettings.Add(MakeShared<FJsonValueObject>(AuditTransitionJson));
                }
            }
        }
        AuditRoot->SetNumberField(TEXT("state_count"),AuditStateCount);
        AuditRoot->SetNumberField(TEXT("transition_count"),AuditTransitionSettings.Num());
        AuditRoot->SetNumberField(TEXT("implicit_inertialization_enabled_count"),AuditEnabledCount);
        AuditRoot->SetArrayField(TEXT("topology"),AuditTopology);
        AuditRoot->SetArrayField(TEXT("transitions"),AuditTransitionSettings);
        return AuditRoot;
    }
}
#endif

UAnimBlueprint* UDWCombatAuthoringLibrary::CreateCombatAnimationBlueprint(const FString& PackagePath,
    USkeleton* Skeleton, bool bEnemy, UAnimSequenceBase* PreviewIdleSequence, FString& OutMessage)
{
#if WITH_EDITOR
    using namespace DWCombatAuthoring;
    if (!Skeleton) { OutMessage=TEXT("A real skeleton is required."); return nullptr; }
    FText PackageReason;
    if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath,false,&PackageReason))
    { OutMessage=TEXT("Use a new /Game/.../ABP_Name package path: ")+PackageReason.ToString(); return nullptr; }
    if (FPackageName::DoesPackageExist(PackagePath) || FindPackage(nullptr,*PackagePath))
    { OutMessage=TEXT("Refusing to overwrite an existing package: ")+PackagePath; return nullptr; }
    if (PreviewIdleSequence && (PreviewIdleSequence->IsA<UAnimMontage>() || !Skeleton->IsCompatibleForEditor(PreviewIdleSequence->GetSkeleton())))
    { OutMessage=TEXT("Preview idle must be a sequence/composite compatible with the supplied skeleton."); return nullptr; }

    UPackage* NewPackage=CreatePackage(*PackagePath);
    UAnimBlueprintFactory* Factory=NewObject<UAnimBlueprintFactory>();
    Factory->ParentClass=UDWCombatAnimInstance::StaticClass();
    Factory->TargetSkeleton=Skeleton;
    // The const overload reads the existing preview without assigning a mesh to the source skeleton.
    Factory->PreviewSkeletalMesh=static_cast<const USkeleton*>(Skeleton)->GetPreviewMesh();
    UAnimBlueprint* Blueprint=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),NewPackage,
        FName(*FPackageName::GetLongPackageAssetName(PackagePath)),RF_Public|RF_Standalone,nullptr,GWarn));
    if (!Blueprint) { OutMessage=TEXT("Anim Blueprint factory failed; no asset was saved."); return nullptr; }

    UEdGraph* AnimGraph=nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        if (Graph && Graph->GetFName()==UEdGraphSchema_K2::GN_AnimGraph) { AnimGraph=Graph; break; }
    if (!AnimGraph) { OutMessage=TEXT("Factory did not create AnimGraph; no asset was saved."); return nullptr; }
    UAnimGraphNode_Root* Root=nullptr;
    for (UEdGraphNode* GraphNode : AnimGraph->Nodes)
        if (UAnimGraphNode_Root* Candidate=Cast<UAnimGraphNode_Root>(GraphNode)) { Root=Candidate; break; }
    if (!Root) { OutMessage=TEXT("AnimGraph is missing Output Pose; no asset was saved."); return nullptr; }

    UAnimGraphNode_StateMachine* Machine=AddNode<UAnimGraphNode_StateMachine>(AnimGraph,0,0);
    Machine->GetNode().bSkipFirstUpdateTransition=false;
    Machine->GetNode().MaxTransitionsPerFrame=3;
    Machine->GetNode().bReinitializeOnBecomingRelevant=true;
    Machine->NodeComment=TEXT("AnimationState is copied from the character. Replace animations on the character's Animation slots.");
    Machine->bCommentBubbleVisible=true;
    UAnimationStateMachineGraph* MachineGraph=Machine->EditorStateMachineGraph;
    FBlueprintEditorUtils::RenameGraph(MachineGraph,bEnemy ? TEXT("EnemyCombatStates") : TEXT("PlayerCombatStates"));
    Root->NodePosX=420;
    Root->NodePosY=0;
    if (!Connect(AnimGraph,FirstOutput(Machine),Root->FindPin(TEXT("Result")),OutMessage)) return nullptr;

    TArray<FStateSpec> Specs;
    Specs.Add({EDWCombatAnimationState::Idle,TEXT("Idle"),TEXT("IdleSequence"),nullptr,true});
    Specs.Add({EDWCombatAnimationState::Move,TEXT("Move"),TEXT("MoveSequence"),TEXT("MovePlayRate"),true});
    if (bEnemy)
    {
        Specs.Add({EDWCombatAnimationState::Alert,TEXT("Alert"),TEXT("AlertSequence"),nullptr,true});
        Specs.Add({EDWCombatAnimationState::Attack,TEXT("Attack"),TEXT("AttackSequence"),TEXT("AttackPlayRate"),false});
    }
    else
    {
        Specs.Add({EDWCombatAnimationState::Throw,TEXT("Throw"),TEXT("ThrowSequence"),nullptr,false});
        Specs.Add({EDWCombatAnimationState::Transform,TEXT("Transform"),TEXT("TransformSequence"),TEXT("TransformPlayRate"),false});
        Specs.Add({EDWCombatAnimationState::Dash,TEXT("Dash"),TEXT("DashSequence"),nullptr,false});
        Specs.Add({EDWCombatAnimationState::Melee,TEXT("Melee"),TEXT("MeleeSequence"),TEXT("MeleePlayRate"),false});
    }
    Specs.Add({EDWCombatAnimationState::Death,TEXT("Death"),TEXT("DeathSequence"),nullptr,false});

    TArray<UAnimStateNode*> States;
    for (int32 Index=0;Index<Specs.Num();++Index)
    {
        UAnimStateNode* StateNode=AddNode<UAnimStateNode>(MachineGraph,420,Index*180);
        if (!FillState(StateNode,Specs[Index],PreviewIdleSequence,OutMessage)) return nullptr;
        States.Add(StateNode);
    }
    MachineGraph->EntryNode->NodePosX=180;
    MachineGraph->EntryNode->NodePosY=-160;
    if (!Connect(MachineGraph,MachineGraph->EntryNode->GetOutputPin(),States[0]->GetInputPin(),OutMessage)) return nullptr;
    for (int32 Index=0;Index<States.Num();++Index)
    {
        UAnimStateAliasNode* Sources=AddNode<UAnimStateAliasNode>(MachineGraph,-140,Index*180);
        Sources->StateAliasName=FString::Printf(TEXT("Other states to %s"),Specs[Index].Name);
        Sources->bGlobalAlias=false;
        for (int32 Other=0;Other<States.Num();++Other)
            if (Other!=Index) Sources->GetAliasedStates().Add(States[Other]);
        Sources->NodeComment=TEXT("Select this alias to inspect its source states. The destination is excluded so playback is not restarted every frame.");
        if (!AddStateTransition(MachineGraph,Sources,States[Index],Specs[Index],Index*180,OutMessage)) return nullptr;
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FCompilerResultsLog CompileLog;
    FKismetEditorUtilities::CompileBlueprint(Blueprint,EBlueprintCompileOptions::None,&CompileLog);
    if (CompileLog.NumErrors>0 || Blueprint->Status==BS_Error || !Blueprint->GeneratedClass)
    { OutMessage=FString::Printf(TEXT("Compile failed (%d errors); no asset was saved. Inspect the editor compiler log."),CompileLog.NumErrors); return nullptr; }
    if (UDWCombatAnimInstance* Defaults=Cast<UDWCombatAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject()))
        Defaults->PreviewIdleSequence=PreviewIdleSequence;
    Blueprint->MarkPackageDirty();
    const FString Filename=FPackageName::LongPackageNameToFilename(PackagePath,FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags=RF_Public|RF_Standalone;
    SaveArgs.SaveFlags=SAVE_NoError;
    if (!UPackage::SavePackage(NewPackage,Blueprint,*Filename,SaveArgs))
    { OutMessage=TEXT("New animation blueprint compiled but saving failed: ")+Filename; return nullptr; }
    FAssetRegistryModule::AssetCreated(Blueprint);
    OutMessage=FString::Printf(TEXT("Created and compiled %s: %d states, %d enum rules. Existing assets were not modified. Compiler warnings: %d."),
        *Blueprint->GetPathName(),Specs.Num(),Specs.Num(),CompileLog.NumWarnings);
    return Blueprint;
#else
    OutMessage=TEXT("Combat animation authoring is available only in the Unreal Editor.");
    return nullptr;
#endif
}

FString UDWCombatAuthoringLibrary::InspectSharedClubR05(UBlueprint* Blueprint)
{
#if WITH_EDITOR
    const FString Mold=TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWCreature_MoldThug.BP_DWCreature_MoldThug");
    const FString Yeast=TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWCreature_YeastThug.BP_DWCreature_YeastThug");
    const TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>();Result->SetBoolField(TEXT("read_only"),true);
    const auto Finish=[&](){FString Text;auto Writer=TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);FJsonSerializer::Serialize(Result,Writer);return Text;};
    if(!IsValid(Blueprint)||(Blueprint->GetPathName()!=Mold&&Blueprint->GetPathName()!=Yeast)||
        !Blueprint->ParentClass||!Blueprint->ParentClass->IsChildOf(ADWEnemyCharacter::StaticClass())||
        !Blueprint->GeneratedClass||!Blueprint->SimpleConstructionScript)
    {Result->SetStringField(TEXT("error"),TEXT("Exact compiled Thug BP/Enemy inheritance/SCS required."));return Finish();}
    Result->SetStringField(TEXT("blueprint"),Blueprint->GetPathName());
    USCS_Node* Found=nullptr;int32 Count=0;
    for(USCS_Node* Node:Blueprint->SimpleConstructionScript->GetAllNodes())
        if(Node&&Node->GetVariableName()==TEXT("SharedClubMesh")){Found=Node;++Count;}
    Result->SetNumberField(TEXT("matching_nodes"),Count);
    const auto* Component=Found?Cast<UStaticMeshComponent>(Found->ComponentTemplate):nullptr;
    if(Count!=1||!Component)
    {Result->SetStringField(TEXT("error"),TEXT("Expected one StaticMeshComponent SharedClubMesh."));return Finish();}
    Result->SetStringField(TEXT("node"),Found->GetPathName());Result->SetStringField(TEXT("guid"),Found->VariableGuid.ToString());
    Result->SetBoolField(TEXT("native_parent"),Found->bIsParentComponentNative);
    Result->SetStringField(TEXT("parent_component"),Found->ParentComponentOrVariableName.ToString());
    Result->SetStringField(TEXT("parent_owner_class"),Found->ParentComponentOwnerClassName.ToString());
    Result->SetStringField(TEXT("attach_to"),Found->AttachToName.ToString());
    Result->SetStringField(TEXT("template_socket"),Component->GetAttachSocketName().ToString());
    Result->SetStringField(TEXT("template_parent"),Component->GetAttachParent()?Component->GetAttachParent()->GetPathName():TEXT(""));
    Result->SetStringField(TEXT("mesh"),Component->GetStaticMesh()?Component->GetStaticMesh()->GetPathName():TEXT(""));
    const FTransform X=Component->GetRelativeTransform();const FVector L=X.GetTranslation(),S=X.GetScale3D();const FQuat Q=X.GetRotation();
    const auto Values=[](std::initializer_list<double> Items){TArray<TSharedPtr<FJsonValue>> A;for(double V:Items)A.Add(MakeShared<FJsonValueNumber>(V));return A;};
    Result->SetArrayField(TEXT("location_cm"),Values({L.X,L.Y,L.Z}));
    Result->SetArrayField(TEXT("rotation_xyzw"),Values({Q.X,Q.Y,Q.Z,Q.W}));Result->SetArrayField(TEXT("scale"),Values({S.X,S.Y,S.Z}));
    Result->SetNumberField(TEXT("collision_enabled"),static_cast<int32>(Component->GetCollisionEnabled()));
    Result->SetStringField(TEXT("collision_profile"),Component->GetCollisionProfileName().ToString());
    Result->SetBoolField(TEXT("generate_overlap_events"),Component->GetGenerateOverlapEvents());
    Result->SetBoolField(TEXT("can_affect_navigation"),Component->CanEverAffectNavigation());
    Result->SetBoolField(TEXT("can_ever_tick"),Component->PrimaryComponentTick.bCanEverTick);
    Result->SetBoolField(TEXT("start_with_tick_enabled"),Component->PrimaryComponentTick.bStartWithTickEnabled);
    Result->SetBoolField(TEXT("tick_enabled"),Component->IsComponentTickEnabled());
    Result->SetNumberField(TEXT("mobility"),static_cast<int32>(Component->Mobility));
    Result->SetBoolField(TEXT("visible"),Component->IsVisible());Result->SetBoolField(TEXT("hidden_in_game"),Component->bHiddenInGame);
    Result->SetNumberField(TEXT("child_nodes"),Found->GetChildNodes().Num());
    Result->SetBoolField(TEXT("is_local_root"),Blueprint->SimpleConstructionScript->FindParentNode(Found)==nullptr);
    return Finish();
#else
    return TEXT("{\"read_only\":true,\"error\":\"Editor only\"}");
#endif
}

bool UDWCombatAuthoringLibrary::ConfigureR05ThugClub(UBlueprint* Blueprint,const FTransform& RelativeTransform,FString& OutReport)
{
#if WITH_EDITOR
    const FString Mold=TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWCreature_MoldThug.BP_DWCreature_MoldThug");
    const FString Yeast=TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWCreature_YeastThug.BP_DWCreature_YeastThug");
    const FString MeshPath=TEXT("/Game/DoughWorld/Characters/Enemies/Weapons/SM_DWThug_SharedClub_R01.SM_DWThug_SharedClub_R01");
    if(!IsValid(Blueprint)||(Blueprint->GetPathName()!=Mold&&Blueprint->GetPathName()!=Yeast)||
        !Blueprint->ParentClass||!Blueprint->ParentClass->IsChildOf(ADWEnemyCharacter::StaticClass())||
        !Blueprint->GeneratedClass||!Blueprint->SimpleConstructionScript||RelativeTransform.ContainsNaN()||
        !RelativeTransform.GetRotation().IsNormalized()||RelativeTransform.GetScale3D().GetMin()<=0.)
    {OutReport=TEXT("Refused: exact Thug BP, Enemy inheritance/SCS and finite normalized positive-scale transform required.");return false;}
    auto* Default=Cast<ADWEnemyCharacter>(Blueprint->GeneratedClass->GetDefaultObject());
    auto* NativeMesh=Default?Default->GetMesh():nullptr;
    if(!NativeMesh||NativeMesh->GetFName()!=TEXT("CharacterMesh0")||NativeMesh->CreationMethod!=EComponentCreationMethod::Native||
        !NativeMesh->GetSkeletalMeshAsset()||NativeMesh->GetBoneIndex(TEXT("Spear"))==INDEX_NONE||
        !NativeMesh->GetSkeletalMeshAsset()->GetSkeleton()||NativeMesh->GetSkeletalMeshAsset()->GetSkeleton()->GetPathName()!=
            TEXT("/Game/DoughWorld/Characters/Hero/Meshes/SK_DWHeroSpear_Skeleton.SK_DWHeroSpear_Skeleton"))
    {OutReport=TEXT("Refused: existing native CharacterMesh0 with the original Hero Skeleton/Spear bone required.");return false;}
    auto* ClubMesh=LoadObject<UStaticMesh>(nullptr,*MeshPath);
    if(!ClubMesh){OutReport=TEXT("Fixed imported shared club mesh is missing; no changes.");return false;}
    USimpleConstructionScript* SCS=Blueprint->SimpleConstructionScript.Get();USCS_Node* Node=nullptr;int32 Matches=0;
    for(USCS_Node* Candidate:SCS->GetAllNodes())
    {
        if(Candidate&&Candidate->GetVariableName()==TEXT("SharedClubMesh")){Node=Candidate;++Matches;}
        else if(Candidate)
        {
            const auto* Other=Cast<UStaticMeshComponent>(Candidate->ComponentTemplate);
            if(Other&&Other->GetStaticMesh()==ClubMesh)
            {OutReport=TEXT("Shared club already exists under another SCS name; refusing to duplicate/rename it.");return false;}
        }
    }
    if(Matches>1||(Node&&(Node->ComponentClass!=UStaticMeshComponent::StaticClass()||
        !Cast<UStaticMeshComponent>(Node->ComponentTemplate)||Node->GetChildNodes().Num()!=0||SCS->FindParentNode(Node))))
    {OutReport=TEXT("Unexpected/duplicate SharedClubMesh SCS structure; no changes.");return false;}
    if(!Node&&Blueprint->ParentClass->FindPropertyByName(TEXT("SharedClubMesh")))
    {OutReport=TEXT("Inherited SharedClubMesh property would be shadowed; no changes.");return false;}
    auto* Component=Node?Cast<UStaticMeshComponent>(Node->ComponentTemplate):nullptr;
    if(Component&&Component->GetStaticMesh()&&Component->GetStaticMesh()!=ClubMesh)
    {OutReport=TEXT("Existing SharedClubMesh uses another mesh; preserving it, no changes.");return false;}
    const bool Already=Node&&Node->bIsParentComponentNative&&Node->ParentComponentOrVariableName==TEXT("CharacterMesh0")&&
        Node->ParentComponentOwnerClassName.IsNone()&&Node->AttachToName==TEXT("Spear")&&Component->GetAttachParent()==NativeMesh&&
        Component->GetAttachSocketName()==TEXT("Spear")&&Component->GetStaticMesh()==ClubMesh&&
        Component->GetRelativeTransform().Equals(RelativeTransform,0.000001)&&Component->GetCollisionEnabled()==ECollisionEnabled::NoCollision&&
        !Component->GetGenerateOverlapEvents()&&!Component->CanEverAffectNavigation()&&!Component->PrimaryComponentTick.bCanEverTick&&
        !Component->PrimaryComponentTick.bStartWithTickEnabled&&Component->Mobility==EComponentMobility::Movable&&
        Component->IsVisible()&&!Component->bHiddenInGame;
    if(!Already)
    {
        const FScopedTransaction Transaction(NSLOCTEXT("DWCombatAuthoring","ConfigureR05ThugClub","Configure R05 Shared Thug Club"));
        Blueprint->Modify();SCS->Modify();
        if(!Node)
        {
            Node=SCS->CreateNode(UStaticMeshComponent::StaticClass(),TEXT("SharedClubMesh"));
            if(!Node||Node->GetVariableName()!=TEXT("SharedClubMesh"))
            {OutReport=TEXT("Could not create exact SharedClubMesh node; no node added.");return false;}
            SCS->AddNode(Node);
        }
        Node->Modify();Component=Cast<UStaticMeshComponent>(Node->ComponentTemplate);Component->Modify();
        // Official SCS native-parent API + public native socket field; no property-access flag hacks.
        Node->SetParent(NativeMesh);Node->AttachToName=TEXT("Spear");
        Component->SetupAttachment(NativeMesh,TEXT("Spear"));
        Component->SetStaticMesh(ClubMesh);Component->SetRelativeTransform(RelativeTransform);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetGenerateOverlapEvents(false);Component->SetCanEverAffectNavigation(false);
        Component->PrimaryComponentTick.bCanEverTick=false;Component->PrimaryComponentTick.bStartWithTickEnabled=false;
        Component->SetComponentTickEnabled(false);Component->SetMobility(EComponentMobility::Movable);
        Component->SetVisibility(true,false);Component->SetHiddenInGame(false,false);
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    }
    const FTransform Actual=Component->GetRelativeTransform();const FVector T=Actual.GetTranslation(),Scale=Actual.GetScale3D();const FQuat Q=Actual.GetRotation();
    OutReport=FString::Printf(TEXT("{\"already_configured\":%s,\"blueprint\":\"%s\",\"node\":\"%s\",\"guid\":\"%s\",\"native_parent\":%s,\"parent\":\"%s\",\"socket\":\"%s\",\"template_socket\":\"%s\",\"mesh\":\"%s\",\"location_cm\":[%.12g,%.12g,%.12g],\"rotation_xyzw\":[%.12g,%.12g,%.12g,%.12g],\"scale\":[%.12g,%.12g,%.12g],\"collision\":\"NoCollision\",\"overlap\":false,\"tick\":false,\"compile_save\":\"caller only\"}"),
        Already?TEXT("true"):TEXT("false"),*Blueprint->GetPathName(),*Node->GetVariableName().ToString(),*Node->VariableGuid.ToString(),
        Node->bIsParentComponentNative?TEXT("true"):TEXT("false"),*Node->ParentComponentOrVariableName.ToString(),*Node->AttachToName.ToString(),
        *Component->GetAttachSocketName().ToString(),*Component->GetStaticMesh()->GetPathName(),T.X,T.Y,T.Z,Q.X,Q.Y,Q.Z,Q.W,Scale.X,Scale.Y,Scale.Z);
    return Node->bIsParentComponentNative&&Node->ParentComponentOrVariableName==TEXT("CharacterMesh0")&&Node->AttachToName==TEXT("Spear")&&
        Component->GetAttachParent()==NativeMesh&&Component->GetAttachSocketName()==TEXT("Spear")&&Actual.Equals(RelativeTransform,0.000001);
#else
    OutReport=TEXT("Editor only.");return false;
#endif
}

bool UDWCombatAuthoringLibrary::ConfigureR05CreatureAlertClock(UAnimBlueprint* Blueprint,FString& OutReport)
{
#if WITH_EDITOR
    using namespace DWCombatAuthoring;
    const FString Thug=TEXT("/Game/DoughWorld/Characters/Enemies/Animations/ABP_DWThug_HeroRig_R05.ABP_DWThug_HeroRig_R05");
    const FString Neutral=TEXT("/Game/DoughWorld/Characters/Enemies/Animations/ABP_DWNeutral_HeroRig_R05.ABP_DWNeutral_HeroRig_R05");
    if(!IsValid(Blueprint)||Blueprint->ParentClass!=UDWCombatAnimInstance::StaticClass()||
        (Blueprint->GetPathName()!=Thug&&Blueprint->GetPathName()!=Neutral)||!Blueprint->TargetSkeleton||
        Blueprint->TargetSkeleton->GetPathName()!=TEXT("/Game/DoughWorld/Characters/Hero/Meshes/SK_DWHeroSpear_Skeleton.SK_DWHeroSpear_Skeleton"))
    {OutReport=TEXT("Refused: exact R05 package, original Hero Skeleton and native parent are required.");return false;}
    TArray<UEdGraph*> Graphs;Blueprint->GetAllGraphs(Graphs);
    UEdGraph* AlertGraph=nullptr;int32 AlertGraphs=0,StateCount=0,TransitionCount=0,MachineCount=0;
    TSet<FString> States;
    for(UEdGraph* Graph:Graphs)
    {
        if(!Graph)continue;
        if(Graph->GetName()==TEXT("Alert")){AlertGraph=Graph;++AlertGraphs;}
        for(UEdGraphNode* Node:Graph->Nodes)
        {
            if(const auto* State=Cast<UAnimStateNode>(Node)){++StateCount;States.Add(State->GetStateName());}
            if(Cast<UAnimStateTransitionNode>(Node))++TransitionCount;
            if(const auto* Machine=Cast<UAnimGraphNode_StateMachine>(Node))
            {if(!Machine->EditorStateMachineGraph||Machine->EditorStateMachineGraph->GetName()!=TEXT("EnemyCombatStates"))
             {OutReport=TEXT("Unexpected R05 state machine; no changes.");return false;}++MachineCount;}
        }
    }
    if(AlertGraphs!=1||StateCount!=5||States.Num()!=5||TransitionCount!=5||MachineCount!=1||
        !States.Contains(TEXT("Idle"))||!States.Contains(TEXT("Move"))||!States.Contains(TEXT("Alert"))||
        !States.Contains(TEXT("Attack"))||!States.Contains(TEXT("Death")))
    {OutReport=TEXT("Unexpected R05 state/transition structure; no changes.");return false;}
    UAnimGraphNode_SequencePlayer* Player=nullptr;UAnimGraphNode_SequenceEvaluator* Existing=nullptr;
    int32 Players=0,Evaluators=0;
    for(UEdGraphNode* Node:AlertGraph->Nodes)
    {
        if(auto* P=Cast<UAnimGraphNode_SequencePlayer>(Node)){Player=P;++Players;}
        if(auto* E=Cast<UAnimGraphNode_SequenceEvaluator>(Node)){Existing=E;++Evaluators;}
    }
    const auto Reads=[](UEdGraphPin* Pin,FName Name)
    {
        if(!Pin||Pin->LinkedTo.Num()!=1)return false;
        const auto* Read=Cast<UK2Node_VariableGet>(Pin->LinkedTo[0]->GetOwningNode());
        return Read&&Read->VariableReference.IsSelfContext()&&Read->VariableReference.GetMemberName()==Name;
    };
    if(Blueprint->GetPathName()==Neutral)
    {
        if(Players!=1||Evaluators!=0||AlertGraph->Nodes.Num()!=3||!Reads(Player->FindPin(TEXT("Sequence")),TEXT("AlertSequence")))
        {OutReport=TEXT("Unexpected Neutral R05 Alert structure; no changes.");return false;}
        OutReport=TEXT("Exact Neutral R05 validated only; no graph/default changes applied.");return true;
    }
    if(Evaluators==1&&Players==0)
    {
        if(AlertGraph->Nodes.Num()!=4||!Reads(Existing->FindPin(TEXT("Sequence")),TEXT("AlertSequence"))||
            !Reads(Existing->FindPin(TEXT("ExplicitTime")),TEXT("AlertPositionSeconds"))||
            !Existing->Node.GetTeleportToExplicitTime()||Existing->Node.IsLooping()||
            Existing->Node.GetGroupMethod()!=EAnimSyncMethod::DoNotSync)
        {OutReport=TEXT("Existing R05 evaluator is not the exact expected clock binding; no changes.");return false;}
        OutReport=TEXT("Exact Thug R05 alert evaluator already configured; no changes.");return true;
    }
    UEdGraphPin* OldPose=Player?FirstOutput(Player):nullptr;
    if(Players!=1||Evaluators!=0||AlertGraph->Nodes.Num()!=3||
        !Reads(Player->FindPin(TEXT("Sequence")),TEXT("AlertSequence"))||!OldPose||OldPose->LinkedTo.Num()!=1||
        OldPose->LinkedTo[0]->GetOwningNode()->GetClass()->GetFName()!=FName(TEXT("AnimGraphNode_StateResult")))
    {OutReport=TEXT("Unexpected Thug R05 Alert player/links; no changes.");return false;}
    UEdGraphPin* Sink=OldPose->LinkedTo[0];
    UEdGraphPin* SequenceOutput=Player->FindPin(TEXT("Sequence"))->LinkedTo[0];
    const FScopedTransaction Transaction(NSLOCTEXT("DWCombatAuthoring","ConfigureR05Alert","Configure R05 Thug Alert Clock"));
    Blueprint->Modify();AlertGraph->Modify();Player->Modify();Sink->GetOwningNode()->Modify();SequenceOutput->GetOwningNode()->Modify();
    auto* Evaluator=AddNode<UAnimGraphNode_SequenceEvaluator>(AlertGraph,Player->NodePosX,Player->NodePosY);
    Evaluator->Node.SetSequence(Player->Node.GetSequence());Evaluator->Node.SetShouldLoop(false);
    Evaluator->Node.SetTeleportToExplicitTime(true);
    Evaluator->Node.SetGroupMethod(EAnimSyncMethod::DoNotSync);
    Evaluator->Node.SetReinitializationBehavior(ESequenceEvalReinit::ExplicitTime);
    Evaluator->NodeComment=TEXT("Formal Thug alert game clock; explicit presentation only, no gameplay notify dispatch.");
    Evaluator->bCommentBubbleVisible=true;
    auto* Time=ReadVariable(AlertGraph,TEXT("AlertPositionSeconds"),Player->NodePosX-340,Player->NodePosY+160);
    // Finish input validation/connections before replacing the old pose link. Never rebuild the graph.
    if(!ExposePin(Evaluator,TEXT("Sequence"))||!ExposePin(Evaluator,TEXT("ExplicitTime"))||
        !Connect(AlertGraph,SequenceOutput,Evaluator->FindPin(TEXT("Sequence")),OutReport)||
        !Connect(AlertGraph,FirstOutput(Time),Evaluator->FindPin(TEXT("ExplicitTime")),OutReport)||
        !Connect(AlertGraph,FirstOutput(Evaluator),Sink,OutReport))
    {
        FBlueprintEditorUtils::RemoveNode(Blueprint,Evaluator,true);FBlueprintEditorUtils::RemoveNode(Blueprint,Time,true);
        if(OldPose->LinkedTo.Num()==0)Connect(AlertGraph,OldPose,Sink,OutReport);
        OutReport=TEXT("R05 clock connection failed; new nodes removed and original player retained.");return false;
    }
    FBlueprintEditorUtils::RemoveNode(Blueprint,Player,true);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    OutReport=TEXT("Only Thug R05 Alert player replaced by AlertSequence/AlertPositionSeconds evaluator. Existing states/transitions/GUIDs/layout retained; caller must compile, independently inspect, then save exact package.");
    return true;
#else
    OutReport=TEXT("Editor only.");return false;
#endif
}

bool UDWCombatAuthoringLibrary::ConfigureImportedCombatAnimationBlueprint(UAnimBlueprint* Blueprint,
    bool bLoopAlert, float LocomotionBlendSeconds, FString& OutReport)
{
#if WITH_EDITOR
    using namespace DWCombatAuthoring;
    if (!Blueprint || Blueprint->ParentClass!=UDWCombatAnimInstance::StaticClass())
    { OutReport=TEXT("Invalid combat animation blueprint."); return false; }
    const FString Path=Blueprint->GetPathName();
    const bool bAllowed=Path==TEXT("/Game/DoughWorld/Characters/Enemies/Small/Animations/ABP_DWSmallCombat.ABP_DWSmallCombat")
        || Path==TEXT("/Game/DoughWorld/Characters/Enemies/Giant/Animations/ABP_DWGiantCombat.ABP_DWGiantCombat")
        || Path==TEXT("/Game/DoughWorld/Characters/Hero/Animations/ABP_DWHeroSpearCombat.ABP_DWHeroSpearCombat");
    if (!bAllowed || !FMath::IsFinite(LocomotionBlendSeconds))
    { OutReport=TEXT("Refused: only the three newly imported character graphs may be configured."); return false; }
    TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
    TArray<UAnimGraphNode_SequencePlayer*> AlertPlayers;
    TArray<UAnimStateTransitionNode*> BlendTransitions;
    for (UEdGraph* Graph : Graphs)
    {
        if (!Graph) continue;
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            if (Graph->GetName()==TEXT("Alert"))
            {
                if (UAnimGraphNode_SequencePlayer* Player=Cast<UAnimGraphNode_SequencePlayer>(GraphNode))
                { AlertPlayers.Add(Player); }
            }
            if (UAnimStateTransitionNode* Transition=Cast<UAnimStateTransitionNode>(GraphNode))
            {
                UAnimStateNodeBase* Destination=Transition->GetNextState();
                if (Destination && (Destination->GetStateName()==TEXT("Idle") || Destination->GetStateName()==TEXT("Move")))
                { BlendTransitions.Add(Transition); }
            }
        }
    }
    const int32 ExpectedAlerts=Path.Contains(TEXT("/Enemies/")) ? 1 : 0;
    if (BlendTransitions.Num()!=2 || AlertPlayers.Num()!=ExpectedAlerts)
    { OutReport=TEXT("Unexpected imported graph structure; no changes applied."); return false; }
    const FScopedTransaction Transaction(NSLOCTEXT("DWCombatAuthoring","ConfigureImportedCombat","Configure Imported Character Animation"));
    Blueprint->Modify();
    for (UAnimGraphNode_SequencePlayer* Player : AlertPlayers)
    { Player->Modify(); Player->Node.SetLoopAnimation(bLoopAlert); }
    for (UAnimStateTransitionNode* Transition : BlendTransitions)
    { Transition->Modify(); Transition->CrossfadeDuration=FMath::Clamp(LocomotionBlendSeconds,0.f,.5f); }
    FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    OutReport=FString::Printf(TEXT("Configured alert players=%d, locomotion transitions=%d; caller must compile and save."),AlertPlayers.Num(),BlendTransitions.Num());
    return true;
#else
    OutReport=TEXT("Editor only."); return false;
#endif
}

FString UDWCombatAuthoringLibrary::InspectCombatAnimationBlueprintTransitions(UAnimBlueprint* Blueprint)
{
#if WITH_EDITOR
    return DWCombatAuthoring::AuditJson(DWCombatAuthoring::CaptureAudit(Blueprint));
#else
    return TEXT("{\"valid_repair_target\":false,\"validation_error\":\"Editor only.\"}");
#endif
}

bool UDWCombatAuthoringLibrary::RepairCombatAnimationBlueprintTransitions(UAnimBlueprint* Blueprint, FString& OutReport)
{
#if WITH_EDITOR
    using namespace DWCombatAuthoring;
    const TSharedRef<FJsonObject> BeforeAudit=CaptureAudit(Blueprint);
    TSharedRef<FJsonObject> RepairReport=MakeShared<FJsonObject>();
    RepairReport->SetObjectField(TEXT("before"),BeforeAudit);
    TArray<UAnimStateTransitionNode*> RepairTransitions;
    FString RepairError;
    if (!ValidateRepairTarget(Blueprint,RepairTransitions,RepairError))
    {
        RepairReport->SetBoolField(TEXT("success"),false);
        RepairReport->SetStringField(TEXT("error"),RepairError);
        OutReport=AuditJson(RepairReport);
        return false;
    }
    int32 RepairChangedCount=0;
    for (const UAnimStateTransitionNode* RepairTransition : RepairTransitions)
        if (RepairTransition->bAllowInertializationForSelfTransitions) ++RepairChangedCount;
    if (RepairChangedCount>0)
    {
        const FScopedTransaction RepairTransaction(NSLOCTEXT("DWCombatAuthoring","DisableImplicitInertialization","Repair Combat Transition Blending"));
        Blueprint->Modify();
        for (UAnimStateTransitionNode* RepairTransition : RepairTransitions)
        {
            RepairTransition->Modify();
            RepairTransition->LogicType=ETransitionLogicType::TLT_StandardBlend;
            RepairTransition->bAllowInertializationForSelfTransitions=false;
        }
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
    }
    const TSharedRef<FJsonObject> AfterAudit=CaptureAudit(Blueprint);
    TSharedRef<FJsonObject> BeforeTopology=MakeShared<FJsonObject>();
    TSharedRef<FJsonObject> AfterTopology=MakeShared<FJsonObject>();
    BeforeTopology->SetArrayField(TEXT("nodes"),BeforeAudit->GetArrayField(TEXT("topology")));
    AfterTopology->SetArrayField(TEXT("nodes"),AfterAudit->GetArrayField(TEXT("topology")));
    const bool bTopologyUnchanged=AuditJson(BeforeTopology)==AuditJson(AfterTopology);
    RepairReport->SetObjectField(TEXT("after"),AfterAudit);
    RepairReport->SetNumberField(TEXT("changed_transition_count"),RepairChangedCount);
    RepairReport->SetBoolField(TEXT("topology_unchanged"),bTopologyUnchanged);
    RepairReport->SetBoolField(TEXT("success"),bTopologyUnchanged);
    RepairReport->SetStringField(TEXT("next_step"),TEXT("Compile the existing blueprint, inspect again, then save only this asset. This function did not compile or save."));
    OutReport=AuditJson(RepairReport);
    return bTopologyUnchanged;
#else
    OutReport=TEXT("{\"success\":false,\"error\":\"Editor only.\"}");
    return false;
#endif
}
