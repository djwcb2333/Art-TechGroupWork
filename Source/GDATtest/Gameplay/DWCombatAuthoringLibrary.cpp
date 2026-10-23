#include "DWCombatAuthoringLibrary.h"
#include "DWCombatAnimInstance.h"
#include "Animation/AnimBlueprint.h"

#if WITH_EDITOR
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
