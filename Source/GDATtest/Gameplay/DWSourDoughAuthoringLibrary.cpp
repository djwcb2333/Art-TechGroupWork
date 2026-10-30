#include "DWSourDoughAuthoringLibrary.h"
#include "DWSourDoughAnimInstance.h"
#include "Animation/AnimBlueprint.h"
#if WITH_EDITOR
#include "Animation/Skeleton.h"
#include "Animation/AnimSequenceBase.h"
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
#include "Factories/AnimBlueprintFactory.h"
#include "K2Node_EnumEquality.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "UObject/Package.h"
namespace DWSourAuthoring
{
    template<typename T> T* Add(UEdGraph* Graph,int32 X,int32 Y)
    { FGraphNodeCreator<T> C(*Graph); T* N=C.CreateNode(); N->NodePosX=X; N->NodePosY=Y; C.Finalize(); return N; }
    UEdGraphPin* Out(UEdGraphNode* N)
    { for (UEdGraphPin* P:N->Pins) if (P && P->Direction==EGPD_Output) return P; return nullptr; }
    bool Link(UEdGraph* G,UEdGraphPin* A,UEdGraphPin* B,FString& Error)
    { if (A && B && G->GetSchema()->TryCreateConnection(A,B)) return true; Error=TEXT("Failed to link animation graph pins"); return false; }
    UK2Node_VariableGet* Get(UEdGraph* G,FName Name,int32 X,int32 Y)
    { FGraphNodeCreator<UK2Node_VariableGet> C(*G); auto* N=C.CreateNode(); N->VariableReference.SetSelfMember(Name); N->NodePosX=X; N->NodePosY=Y; C.Finalize(); return N; }
    bool Expose(UAnimGraphNode_Base* Node,FName Property)
    { for (int32 I=0;I<Node->ShowPinForProperties.Num();++I) if (Node->ShowPinForProperties[I].PropertyName==Property) { Node->SetPinVisibility(true,I); return Node->FindPin(Property)!=nullptr; } return false; }
    struct FSpec { EDWSourVisualState Value; const TCHAR* Name; const TCHAR* Sequence; bool bLoop; };
    bool State(UAnimStateNode* S,const FSpec& Spec,UAnimSequenceBase* Preview,FString& Error)
    {
        UEdGraph* G=S->BoundGraph; S->bAlwaysResetOnEntry=true; FBlueprintEditorUtils::RenameGraph(G,Spec.Name);
        UAnimGraphNode_Base* N=nullptr;
        if (Spec.Value==EDWSourVisualState::Attack)
        {
            auto* E=Add<UAnimGraphNode_SequenceEvaluator>(G,0,0); E->Node.SetSequence(Preview); E->Node.SetShouldLoop(false); N=E;
            if (!Expose(E,TEXT("Sequence")) || !Expose(E,TEXT("ExplicitTime"))) { Error=TEXT("Failed to expose attack evaluator"); return false; }
            if (!Link(G,Out(Get(G,TEXT("AttackPositionSeconds"),-340,140)),E->FindPin(TEXT("ExplicitTime")),Error)) return false;
        }
        else
        {
            auto* P=Add<UAnimGraphNode_SequencePlayer>(G,0,0); P->Node.SetSequence(Preview); P->Node.SetLoopAnimation(Spec.bLoop); N=P;
            if (!Expose(P,TEXT("Sequence"))) { Error=TEXT("Failed to expose sequence"); return false; }
            if (Spec.Value==EDWSourVisualState::Move)
            {
                if (!Expose(P,TEXT("PlayRate")) || !Link(G,Out(Get(G,TEXT("MovePlayRate"),-340,140)),P->FindPin(TEXT("PlayRate")),Error)) return false;
            }
        }
        if (!Link(G,Out(Get(G,Spec.Sequence,-340,0)),N->FindPin(TEXT("Sequence")),Error)) return false;
        return Link(G,Out(N),S->GetPoseSinkPinInsideState(),Error);
    }
    bool Transition(UEdGraph* G,UAnimStateAliasNode* Sources,UAnimStateNode* Dest,const FSpec& Spec,int32 Y,FString& Error)
    {
        auto* T=Add<UAnimStateTransitionNode>(G,260,Y); T->CreateConnections(Sources,Dest);
        T->LogicType=ETransitionLogicType::TLT_StandardBlend; T->bAllowInertializationForSelfTransitions=false;
        T->CrossfadeDuration=(Spec.Value==EDWSourVisualState::Attack || Spec.Value==EDWSourVisualState::Death) ? 0.f : .08f;
        T->PriorityOrder=Spec.Value==EDWSourVisualState::Death ? 0 : 1; T->bAutomaticRuleBasedOnSequencePlayerInState=false;
        auto* Rule=Cast<UAnimationTransitionGraph>(T->BoundGraph); if (!Rule || !Rule->MyResultNode) { Error=TEXT("Missing transition graph"); return false; }
        auto* Equal=Add<UK2Node_EnumEquality>(Rule,-220,0);
        if (!Link(Rule,Out(Get(Rule,TEXT("SourState"),-500,0)),Equal->GetInput1Pin(),Error)) return false;
        const FString Value=StaticEnum<EDWSourVisualState>()->GetNameStringByValue(static_cast<int64>(Spec.Value));
        Rule->GetSchema()->TrySetDefaultValue(*Equal->GetInput2Pin(),Value);
        return Link(Rule,Equal->GetReturnValuePin(),Rule->MyResultNode->FindPin(TEXT("bCanEnterTransition")),Error);
    }
}
#endif
UAnimBlueprint* UDWSourDoughAuthoringLibrary::CreateSourAnimationBlueprint(const FString& PackagePath,USkeleton* Skeleton,UAnimSequenceBase* Idle,UAnimSequenceBase* Enter,UAnimSequenceBase* Loop,UAnimSequenceBase* Exit,FString& OutMessage)
{
#if WITH_EDITOR
    using namespace DWSourAuthoring;
    if (!Skeleton || !Idle || !Enter || !Loop || !Exit || Idle->GetSkeleton()!=Skeleton || Enter->GetSkeleton()!=Skeleton || Loop->GetSkeleton()!=Skeleton || Exit->GetSkeleton()!=Skeleton)
    { OutMessage=TEXT("All animations must use the exact target skeleton"); return nullptr; }
    FText Reason;
    if (!PackagePath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(PackagePath,false,&Reason) || FPackageName::DoesPackageExist(PackagePath) || FindPackage(nullptr,*PackagePath))
    { OutMessage=TEXT("Invalid or existing package: ")+PackagePath; return nullptr; }
    UPackage* Package=CreatePackage(*PackagePath); auto* Factory=NewObject<UAnimBlueprintFactory>();
    Factory->ParentClass=UDWSourDoughAnimInstance::StaticClass(); Factory->TargetSkeleton=Skeleton;
    Factory->PreviewSkeletalMesh=static_cast<const USkeleton*>(Skeleton)->GetPreviewMesh();
    auto* BP=Cast<UAnimBlueprint>(Factory->FactoryCreateNew(UAnimBlueprint::StaticClass(),Package,FName(*FPackageName::GetLongPackageAssetName(PackagePath)),RF_Public|RF_Standalone,nullptr,GWarn));
    if (!BP) { OutMessage=TEXT("Factory failed"); return nullptr; }
    UEdGraph* AnimGraph=nullptr; for (UEdGraph* G:BP->FunctionGraphs) if (G && G->GetFName()==UEdGraphSchema_K2::GN_AnimGraph) { AnimGraph=G; break; }
    if (!AnimGraph) { OutMessage=TEXT("No AnimGraph"); return nullptr; }
    UAnimGraphNode_Root* Root=nullptr; for (UEdGraphNode* N:AnimGraph->Nodes) if ((Root=Cast<UAnimGraphNode_Root>(N))) break;
    if (!Root) { OutMessage=TEXT("No output node"); return nullptr; }
    auto* Machine=Add<UAnimGraphNode_StateMachine>(AnimGraph,0,0); Machine->GetNode().bSkipFirstUpdateTransition=false; Machine->GetNode().MaxTransitionsPerFrame=3; Machine->GetNode().bReinitializeOnBecomingRelevant=true;
    UAnimationStateMachineGraph* Graph=Machine->EditorStateMachineGraph; FBlueprintEditorUtils::RenameGraph(Graph,TEXT("SourDoughEatingAndCombat"));
    if (!Link(AnimGraph,Out(Machine),Root->FindPin(TEXT("Result")),OutMessage)) return nullptr;
    TArray<FSpec> Specs={
        {EDWSourVisualState::Idle,TEXT("Idle"),TEXT("IdleSequence"),true},
        {EDWSourVisualState::Move,TEXT("Move"),TEXT("MoveSequence"),true},
        {EDWSourVisualState::Alert,TEXT("Alert"),TEXT("AlertSequence"),true},
        {EDWSourVisualState::Attack,TEXT("Attack"),TEXT("AttackSequence"),false},
        {EDWSourVisualState::Death,TEXT("Death"),TEXT("DeathSequence"),false},
        {EDWSourVisualState::EatEnter,TEXT("EatEnter"),TEXT("EatEnterSequence"),false},
        {EDWSourVisualState::EatLoop,TEXT("EatLoop"),TEXT("EatLoopSequence"),true},
        {EDWSourVisualState::EatExit,TEXT("EatExit"),TEXT("EatExitSequence"),false}};
    TArray<UAnimStateNode*> States;
    for (int32 I=0;I<Specs.Num();++I) { auto* S=Add<UAnimStateNode>(Graph,420,I*180); if (!State(S,Specs[I],Idle,OutMessage)) return nullptr; States.Add(S); }
    if (!Link(Graph,Graph->EntryNode->GetOutputPin(),States[0]->GetInputPin(),OutMessage)) return nullptr;
    for (int32 I=0;I<States.Num();++I)
    {
        auto* Alias=Add<UAnimStateAliasNode>(Graph,-140,I*180); Alias->StateAliasName=FString::Printf(TEXT("Other states to %s"),Specs[I].Name); Alias->bGlobalAlias=false;
        for (int32 J=0;J<States.Num();++J) if (J!=I) Alias->GetAliasedStates().Add(States[J]);
        if (!Transition(Graph,Alias,States[I],Specs[I],I*180,OutMessage)) return nullptr;
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); FCompilerResultsLog Log; FKismetEditorUtilities::CompileBlueprint(BP,EBlueprintCompileOptions::None,&Log);
    if (Log.NumErrors || BP->Status==BS_Error || !BP->GeneratedClass) { OutMessage=FString::Printf(TEXT("Compile failed: %d errors"),Log.NumErrors); return nullptr; }
    auto* CDO=Cast<UDWSourDoughAnimInstance>(BP->GeneratedClass->GetDefaultObject()); if (!CDO) { OutMessage=TEXT("Incorrect parent"); return nullptr; }
    CDO->PreviewIdleSequence=Idle; CDO->EatEnterSequence=Enter; CDO->EatLoopSequence=Loop; CDO->EatExitSequence=Exit;
    BP->MarkPackageDirty(); const FString File=FPackageName::LongPackageNameToFilename(PackagePath,FPackageName::GetAssetPackageExtension());
    FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
    if (!UPackage::SavePackage(Package,BP,*File,Args)) { OutMessage=TEXT("Save failed"); return nullptr; }
    FAssetRegistryModule::AssetCreated(BP); OutMessage=FString::Printf(TEXT("Created 8-state SourDough graph; compiler warnings: %d"),Log.NumWarnings); return BP;
#else
    OutMessage=TEXT("Editor only"); return nullptr;
#endif
}

bool UDWSourDoughAuthoringLibrary::MountZombieSourcePack(const FString& Directory,FString& OutMessage)
{
#if WITH_EDITOR
    const FString Root=TEXT("/Game/Zombie_Anims/");
    FString Full=FPaths::ConvertRelativePathToFull(Directory); FPaths::NormalizeDirectoryName(Full);
    if (!Full.EndsWith(TEXT("/Zombie_Anims")) || !IFileManager::Get().DirectoryExists(*Full))
    { OutMessage=TEXT("Expected an existing Zombie_Anims staging directory"); return false; }
    if (FPackageName::MountPointExists(Root)) { OutMessage=TEXT("Source mount already exists; refusing to replace"); return false; }
    FPackageName::RegisterMountPoint(Root,Full+TEXT("/"));
    OutMessage=TEXT("Mounted the new source pack for native relocation; existing /Game mount retained"); return true;
#else
    OutMessage=TEXT("Editor only"); return false;
#endif
}

void UDWSourDoughAuthoringLibrary::UnmountZombieSourcePack(const FString& Directory)
{
#if WITH_EDITOR
    FString Full=FPaths::ConvertRelativePathToFull(Directory); FPaths::NormalizeDirectoryName(Full);
    if (Full.EndsWith(TEXT("/Zombie_Anims"))) FPackageName::UnRegisterMountPoint(TEXT("/Game/Zombie_Anims/"),Full+TEXT("/"));
#endif
}
