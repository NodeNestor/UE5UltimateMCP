// Animation Tools — create anim blueprints, state machines, transitions, blend spaces
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "Misc/PackageName.h"

// Animation Blueprint includes
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_Base.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"

// Local helpers
namespace
{
	UAnimationStateMachineGraph* FindStateMachineGraph(UBlueprint* BP, const FString& GraphName)
	{
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(Graph))
			{
				if (SMGraph->GetName() == GraphName) return SMGraph;
			}
		}
		return nullptr;
	}

	UAnimStateNode* FindStateByName(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
	{
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
			{
				if (StateNode->GetStateName() == StateName) return StateNode;
			}
		}
		return nullptr;
	}

	UAnimStateTransitionNode* FindTransition(UAnimationStateMachineGraph* SMGraph, const FString& FromName, const FString& ToName)
	{
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node))
			{
				UAnimStateNode* FromState = Cast<UAnimStateNode>(TransNode->GetPreviousState());
				UAnimStateNode* ToState = Cast<UAnimStateNode>(TransNode->GetNextState());
				if (FromState && ToState && FromState->GetStateName() == FromName && ToState->GetStateName() == ToName)
					return TransNode;
			}
		}
		return nullptr;
	}

	USkeleton* ResolveSkeleton(const FString& SkeletonName, const FString& PackagePath, const FString& AssetName, FString& OutError)
	{
		if (SkeletonName == TEXT("__create_test_skeleton__"))
		{
			FString TestPath = PackagePath / (AssetName + TEXT("_TestSkeleton"));
			UPackage* SkelPackage = CreatePackage(*TestPath);
			return NewObject<USkeleton>(SkelPackage, FName(*(AssetName + TEXT("_TestSkeleton"))), RF_Public | RF_Standalone);
		}

		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> SkeletonAssets;
		ARM.Get().GetAssetsByClass(USkeleton::StaticClass()->GetClassPathName(), SkeletonAssets, false);

		for (const FAssetData& Asset : SkeletonAssets)
		{
			if (Asset.AssetName.ToString() == SkeletonName ||
				Asset.PackageName.ToString() == SkeletonName ||
				Asset.GetObjectPathString() == SkeletonName)
			{
				USkeleton* Skel = Cast<USkeleton>(Asset.GetAsset());
				if (Skel) return Skel;
			}
		}
		for (const FAssetData& Asset : SkeletonAssets)
		{
			if (Asset.AssetName.ToString().Equals(SkeletonName, ESearchCase::IgnoreCase))
			{
				USkeleton* Skel = Cast<USkeleton>(Asset.GetAsset());
				if (Skel) return Skel;
			}
		}
		OutError = FString::Printf(TEXT("Skeleton '%s' not found."), *SkeletonName);
		return nullptr;
	}
}

// ============================================================
// create_anim_blueprint
// ============================================================
class FTool_CreateAnimBlueprint : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_anim_blueprint");
		Info.Description = TEXT("Create a new Animation Blueprint with a target skeleton.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("packagePath"), TEXT("string"), TEXT("Package path"), true});
		Info.Parameters.Add({TEXT("skeleton"), TEXT("string"), TEXT("Skeleton name or path"), true});
		Info.Parameters.Add({TEXT("parentClass"), TEXT("string"), TEXT("Parent class (default AnimInstance)"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		FString PackagePath = Params->GetStringField(TEXT("packagePath"));
		FString SkeletonName = Params->GetStringField(TEXT("skeleton"));
		FString ParentClassName;
		Params->TryGetStringField(TEXT("parentClass"), ParentClassName);

		if (Name.IsEmpty() || PackagePath.IsEmpty() || SkeletonName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: name, packagePath, skeleton"));
		if (!PackagePath.StartsWith(TEXT("/Game")))
			return FMCPToolResult::Error(TEXT("packagePath must start with '/Game'"));

		FString SkelError;
		USkeleton* Skeleton = ResolveSkeleton(SkeletonName, PackagePath, Name, SkelError);
		if (!Skeleton) return FMCPToolResult::Error(SkelError);

		UClass* ParentClass = UAnimInstance::StaticClass();
		if (!ParentClassName.IsEmpty() && ParentClassName != TEXT("AnimInstance"))
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName() == ParentClassName && It->IsChildOf(UAnimInstance::StaticClass()))
				{
					ParentClass = *It;
					break;
				}
			}
		}

		FString FullPackagePath = PackagePath / Name;
		UPackage* Package = CreatePackage(*FullPackagePath);
		if (!Package) return FMCPToolResult::Error(TEXT("Failed to create package"));

		UAnimBlueprint* NewAnimBP = CastChecked<UAnimBlueprint>(
			FKismetEditorUtilities::CreateBlueprint(ParentClass, Package, FName(*Name), BPTYPE_Normal,
				UAnimBlueprint::StaticClass(), UAnimBlueprintGeneratedClass::StaticClass()));
		if (!NewAnimBP) return FMCPToolResult::Error(TEXT("Failed to create AnimBlueprint"));

		NewAnimBP->TargetSkeleton = Skeleton;
		FKismetEditorUtilities::CompileBlueprint(NewAnimBP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(NewAnimBP);

		TArray<TSharedPtr<FJsonValue>> GraphNames;
		TArray<UEdGraph*> AllGraphs;
		NewAnimBP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph) GraphNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprintName"), Name);
		Result->SetStringField(TEXT("assetPath"), PackagePath / Name);
		Result->SetStringField(TEXT("targetSkeleton"), Skeleton->GetName());
		Result->SetStringField(TEXT("parentClass"), ParentClass->GetName());
		Result->SetBoolField(TEXT("saved"), bSaved);
		Result->SetArrayField(TEXT("graphs"), GraphNames);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_anim_state
// ============================================================
class FTool_AddAnimState : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_anim_state");
		Info.Description = TEXT("Add a state to a state machine graph in an Animation Blueprint.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("AnimBlueprint name"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("State machine graph name"), true});
		Info.Parameters.Add({TEXT("stateName"), TEXT("string"), TEXT("New state name"), true});
		Info.Parameters.Add({TEXT("posX"), TEXT("number"), TEXT("X position"), false});
		Info.Parameters.Add({TEXT("posY"), TEXT("number"), TEXT("Y position"), false});
		Info.Parameters.Add({TEXT("animationAsset"), TEXT("string"), TEXT("Animation sequence to assign"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString GraphName = Params->GetStringField(TEXT("graph"));
		FString StateName = Params->GetStringField(TEXT("stateName"));
		if (BlueprintName.IsEmpty() || GraphName.IsEmpty() || StateName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: blueprint, graph, stateName"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
		if (!AnimBP) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not an AnimBlueprint"), *BlueprintName));

		UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(BP, GraphName);
		if (!SMGraph) return FMCPToolResult::Error(FString::Printf(TEXT("State machine graph '%s' not found"), *GraphName));

		if (FindStateByName(SMGraph, StateName))
			return FMCPToolResult::Error(FString::Printf(TEXT("State '%s' already exists"), *StateName));

		double D = 0;
		int32 PosX = Params->TryGetNumberField(TEXT("posX"), D) ? (int32)D : 200;
		int32 PosY = Params->TryGetNumberField(TEXT("posY"), D) ? (int32)D : 0;

		UAnimStateNode* NewState = NewObject<UAnimStateNode>(SMGraph);
		NewState->CreateNewGuid();
		NewState->NodePosX = PosX;
		NewState->NodePosY = PosY;
		NewState->PostPlacedNewNode();
		NewState->AllocateDefaultPins();

		if (NewState->GetBoundGraph())
			NewState->GetBoundGraph()->Rename(*StateName, nullptr);

		SMGraph->AddNode(NewState, false, false);
		NewState->SetFlags(RF_Transactional);

		// Optionally set animation asset
		FString AnimAssetName;
		if (Params->TryGetStringField(TEXT("animationAsset"), AnimAssetName) && !AnimAssetName.IsEmpty() && NewState->GetBoundGraph())
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> AnimAssets;
			ARM.Get().GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), AnimAssets, false);
			for (const FAssetData& Asset : AnimAssets)
			{
				if (Asset.AssetName.ToString() == AnimAssetName || Asset.PackageName.ToString() == AnimAssetName)
				{
					UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset.GetAsset());
					if (AnimSeq)
					{
						UAnimGraphNode_SequencePlayer* SeqNode = NewObject<UAnimGraphNode_SequencePlayer>(NewState->GetBoundGraph());
						SeqNode->CreateNewGuid();
						SeqNode->PostPlacedNewNode();
						SeqNode->AllocateDefaultPins();
						SeqNode->SetAnimationAsset(AnimSeq);
						NewState->GetBoundGraph()->AddNode(SeqNode, false, false);
					}
					break;
				}
			}
		}

		FKismetEditorUtilities::CompileBlueprint(AnimBP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(AnimBP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("stateName"), StateName);
		Result->SetStringField(TEXT("graph"), GraphName);
		Result->SetStringField(TEXT("nodeId"), NewState->NodeGuid.ToString());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// remove_anim_state
// ============================================================
class FTool_RemoveAnimState : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("remove_anim_state");
		Info.Description = TEXT("Remove a state and its transitions from a state machine graph.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("AnimBlueprint name"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("State machine graph name"), true});
		Info.Parameters.Add({TEXT("stateName"), TEXT("string"), TEXT("State to remove"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString GraphName = Params->GetStringField(TEXT("graph"));
		FString StateName = Params->GetStringField(TEXT("stateName"));
		if (BlueprintName.IsEmpty() || GraphName.IsEmpty() || StateName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: blueprint, graph, stateName"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);
		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
		if (!AnimBP) return FMCPToolResult::Error(TEXT("Not an AnimBlueprint"));

		UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(BP, GraphName);
		if (!SMGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *GraphName));

		UAnimStateNode* StateNode = FindStateByName(SMGraph, StateName);
		if (!StateNode) return FMCPToolResult::Error(FString::Printf(TEXT("State '%s' not found"), *StateName));

		TArray<UAnimStateTransitionNode*> TransitionsToRemove;
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node))
			{
				if (TransNode->GetPreviousState() == StateNode || TransNode->GetNextState() == StateNode)
					TransitionsToRemove.Add(TransNode);
			}
		}
		int32 RemovedTransitions = TransitionsToRemove.Num();
		for (UAnimStateTransitionNode* Trans : TransitionsToRemove)
		{
			Trans->BreakAllNodeLinks();
			SMGraph->RemoveNode(Trans);
		}

		StateNode->BreakAllNodeLinks();
		SMGraph->RemoveNode(StateNode);

		FKismetEditorUtilities::CompileBlueprint(AnimBP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(AnimBP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("removedState"), StateName);
		Result->SetNumberField(TEXT("removedTransitions"), RemovedTransitions);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_anim_transition
// ============================================================
class FTool_AddAnimTransition : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_anim_transition");
		Info.Description = TEXT("Add a transition between two states in a state machine.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("AnimBlueprint name"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("State machine graph name"), true});
		Info.Parameters.Add({TEXT("fromState"), TEXT("string"), TEXT("Source state name"), true});
		Info.Parameters.Add({TEXT("toState"), TEXT("string"), TEXT("Target state name"), true});
		Info.Parameters.Add({TEXT("crossfadeDuration"), TEXT("number"), TEXT("Crossfade duration in seconds"), false});
		Info.Parameters.Add({TEXT("priority"), TEXT("number"), TEXT("Transition priority order"), false});
		Info.Parameters.Add({TEXT("bBidirectional"), TEXT("boolean"), TEXT("Bidirectional transition"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString GraphName = Params->GetStringField(TEXT("graph"));
		FString FromStateName = Params->GetStringField(TEXT("fromState"));
		FString ToStateName = Params->GetStringField(TEXT("toState"));

		if (BlueprintName.IsEmpty() || GraphName.IsEmpty() || FromStateName.IsEmpty() || ToStateName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: blueprint, graph, fromState, toState"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);
		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
		if (!AnimBP) return FMCPToolResult::Error(TEXT("Not an AnimBlueprint"));

		UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(BP, GraphName);
		if (!SMGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *GraphName));

		UAnimStateNode* FromState = FindStateByName(SMGraph, FromStateName);
		if (!FromState) return FMCPToolResult::Error(FString::Printf(TEXT("From state '%s' not found"), *FromStateName));
		UAnimStateNode* ToState = FindStateByName(SMGraph, ToStateName);
		if (!ToState) return FMCPToolResult::Error(FString::Printf(TEXT("To state '%s' not found"), *ToStateName));

		UAnimStateTransitionNode* TransNode = NewObject<UAnimStateTransitionNode>(SMGraph);
		TransNode->CreateNewGuid();
		TransNode->PostPlacedNewNode();
		TransNode->AllocateDefaultPins();
		TransNode->NodePosX = (FromState->NodePosX + ToState->NodePosX) / 2;
		TransNode->NodePosY = (FromState->NodePosY + ToState->NodePosY) / 2;
		SMGraph->AddNode(TransNode, false, false);
		TransNode->SetFlags(RF_Transactional);
		TransNode->CreateConnections(FromState, ToState);

		double D = 0;
		if (Params->TryGetNumberField(TEXT("crossfadeDuration"), D)) TransNode->CrossfadeDuration = (float)D;
		if (Params->TryGetNumberField(TEXT("priority"), D)) TransNode->PriorityOrder = (int32)D;
		bool bBiDir = false;
		if (Params->TryGetBoolField(TEXT("bBidirectional"), bBiDir)) TransNode->Bidirectional = bBiDir;

		FKismetEditorUtilities::CompileBlueprint(AnimBP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(AnimBP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("fromState"), FromStateName);
		Result->SetStringField(TEXT("toState"), ToStateName);
		Result->SetStringField(TEXT("nodeId"), TransNode->NodeGuid.ToString());
		Result->SetNumberField(TEXT("crossfadeDuration"), TransNode->CrossfadeDuration);
		Result->SetNumberField(TEXT("priorityOrder"), TransNode->PriorityOrder);
		Result->SetBoolField(TEXT("bBidirectional"), TransNode->Bidirectional);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// set_transition_rule
// ============================================================
class FTool_SetTransitionRule : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_transition_rule");
		Info.Description = TEXT("Update properties on an existing transition between two states.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("AnimBlueprint name"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("State machine graph name"), true});
		Info.Parameters.Add({TEXT("fromState"), TEXT("string"), TEXT("Source state"), true});
		Info.Parameters.Add({TEXT("toState"), TEXT("string"), TEXT("Target state"), true});
		Info.Parameters.Add({TEXT("crossfadeDuration"), TEXT("number"), TEXT("Crossfade duration"), false});
		Info.Parameters.Add({TEXT("priorityOrder"), TEXT("number"), TEXT("Priority order"), false});
		Info.Parameters.Add({TEXT("bBidirectional"), TEXT("boolean"), TEXT("Bidirectional"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString GraphName = Params->GetStringField(TEXT("graph"));
		FString FromStateName = Params->GetStringField(TEXT("fromState"));
		FString ToStateName = Params->GetStringField(TEXT("toState"));

		if (BlueprintName.IsEmpty() || GraphName.IsEmpty() || FromStateName.IsEmpty() || ToStateName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: blueprint, graph, fromState, toState"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);
		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
		if (!AnimBP) return FMCPToolResult::Error(TEXT("Not an AnimBlueprint"));

		UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(BP, GraphName);
		if (!SMGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *GraphName));

		UAnimStateTransitionNode* TransNode = FindTransition(SMGraph, FromStateName, ToStateName);
		if (!TransNode) return FMCPToolResult::Error(FString::Printf(TEXT("Transition from '%s' to '%s' not found"), *FromStateName, *ToStateName));

		int32 ChangedCount = 0;
		double D = 0;
		if (Params->TryGetNumberField(TEXT("crossfadeDuration"), D)) { TransNode->CrossfadeDuration = (float)D; ChangedCount++; }
		if (Params->TryGetNumberField(TEXT("priorityOrder"), D)) { TransNode->PriorityOrder = (int32)D; ChangedCount++; }
		bool bBiDir = false;
		if (Params->TryGetBoolField(TEXT("bBidirectional"), bBiDir)) { TransNode->Bidirectional = bBiDir; ChangedCount++; }

		if (ChangedCount == 0)
			return FMCPToolResult::Error(TEXT("No properties to update"));

		FKismetEditorUtilities::CompileBlueprint(AnimBP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(AnimBP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("fromState"), FromStateName);
		Result->SetStringField(TEXT("toState"), ToStateName);
		Result->SetNumberField(TEXT("propertiesChanged"), ChangedCount);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_anim_node
// ============================================================
class FTool_AddAnimNode : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_anim_node");
		Info.Description = TEXT("Add an animation node (SequencePlayer, BlendSpacePlayer, StateMachine) to an anim graph.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("AnimBlueprint name"), true});
		Info.Parameters.Add({TEXT("nodeType"), TEXT("string"), TEXT("Node type: SequencePlayer, BlendSpacePlayer, StateMachine"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("Target graph name (default AnimGraph)"), false});
		Info.Parameters.Add({TEXT("animationAsset"), TEXT("string"), TEXT("Animation/BlendSpace asset name"), false});
		Info.Parameters.Add({TEXT("posX"), TEXT("number"), TEXT("X position"), false});
		Info.Parameters.Add({TEXT("posY"), TEXT("number"), TEXT("Y position"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString NodeType = Params->GetStringField(TEXT("nodeType"));
		if (BlueprintName.IsEmpty() || NodeType.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: blueprint, nodeType"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);
		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
		if (!AnimBP) return FMCPToolResult::Error(TEXT("Not an AnimBlueprint"));

		FString GraphName;
		if (!Params->TryGetStringField(TEXT("graph"), GraphName) || GraphName.IsEmpty())
			GraphName = TEXT("AnimGraph");

		UEdGraph* TargetGraph = nullptr;
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph->GetName() == GraphName) { TargetGraph = Graph; break; }
		}
		if (!TargetGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *GraphName));

		double D = 0;
		int32 PosX = Params->TryGetNumberField(TEXT("posX"), D) ? (int32)D : 0;
		int32 PosY = Params->TryGetNumberField(TEXT("posY"), D) ? (int32)D : 0;

		UAnimGraphNode_Base* NewNode = nullptr;

		if (NodeType == TEXT("SequencePlayer"))
		{
			UAnimGraphNode_SequencePlayer* SeqNode = NewObject<UAnimGraphNode_SequencePlayer>(TargetGraph);
			SeqNode->CreateNewGuid();
			SeqNode->PostPlacedNewNode();
			SeqNode->AllocateDefaultPins();

			FString AnimAssetName;
			if (Params->TryGetStringField(TEXT("animationAsset"), AnimAssetName) && !AnimAssetName.IsEmpty())
			{
				FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				TArray<FAssetData> AnimAssets;
				ARM.Get().GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), AnimAssets, false);
				for (const FAssetData& Asset : AnimAssets)
				{
					if (Asset.AssetName.ToString() == AnimAssetName || Asset.PackageName.ToString() == AnimAssetName)
					{
						UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset.GetAsset());
						if (AnimSeq) SeqNode->SetAnimationAsset(AnimSeq);
						break;
					}
				}
			}
			NewNode = SeqNode;
		}
		else if (NodeType == TEXT("BlendSpacePlayer"))
		{
			UAnimGraphNode_BlendSpacePlayer* BSNode = NewObject<UAnimGraphNode_BlendSpacePlayer>(TargetGraph);
			BSNode->CreateNewGuid();
			BSNode->PostPlacedNewNode();
			BSNode->AllocateDefaultPins();

			FString BSAssetName;
			if (Params->TryGetStringField(TEXT("animationAsset"), BSAssetName) && !BSAssetName.IsEmpty())
			{
				FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				TArray<FAssetData> BSAssets;
				ARM.Get().GetAssetsByClass(UBlendSpace::StaticClass()->GetClassPathName(), BSAssets, true);
				for (const FAssetData& Asset : BSAssets)
				{
					if (Asset.AssetName.ToString() == BSAssetName || Asset.PackageName.ToString() == BSAssetName)
					{
						UBlendSpace* BS = Cast<UBlendSpace>(Asset.GetAsset());
						if (BS) BSNode->SetAnimationAsset(BS);
						break;
					}
				}
			}
			NewNode = BSNode;
		}
		else if (NodeType == TEXT("StateMachine"))
		{
			UAnimGraphNode_StateMachine* SMNode = NewObject<UAnimGraphNode_StateMachine>(TargetGraph);
			SMNode->CreateNewGuid();
			SMNode->PostPlacedNewNode();
			SMNode->AllocateDefaultPins();
			NewNode = SMNode;
		}
		else
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Unsupported nodeType '%s'. Use: SequencePlayer, BlendSpacePlayer, StateMachine"), *NodeType));
		}

		if (!NewNode) return FMCPToolResult::Error(TEXT("Failed to create anim node"));

		NewNode->NodePosX = PosX;
		NewNode->NodePosY = PosY;
		TargetGraph->AddNode(NewNode, false, false);
		NewNode->SetFlags(RF_Transactional);

		FKismetEditorUtilities::CompileBlueprint(AnimBP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(AnimBP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("nodeType"), NodeType);
		Result->SetStringField(TEXT("nodeId"), NewNode->NodeGuid.ToString());
		Result->SetStringField(TEXT("graph"), GraphName);
		Result->SetBoolField(TEXT("saved"), bSaved);

		if (NodeType == TEXT("StateMachine"))
		{
			if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(NewNode))
			{
				if (SMNode->EditorStateMachineGraph)
					Result->SetStringField(TEXT("stateMachineGraph"), SMNode->EditorStateMachineGraph->GetName());
			}
		}
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_state_machine (convenience wrapper)
// ============================================================
class FTool_AddStateMachine : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_state_machine");
		Info.Description = TEXT("Add a new state machine node to the AnimGraph (convenience wrapper for add_anim_node).");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("AnimBlueprint name"), true});
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("State machine name"), false});
		Info.Parameters.Add({TEXT("posX"), TEXT("number"), TEXT("X position"), false});
		Info.Parameters.Add({TEXT("posY"), TEXT("number"), TEXT("Y position"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		// Forward to add_anim_node with StateMachine type
		TSharedRef<FJsonObject> Forward = MakeShared<FJsonObject>();
		Forward->SetStringField(TEXT("blueprint"), Params->GetStringField(TEXT("blueprint")));
		Forward->SetStringField(TEXT("graph"), TEXT("AnimGraph"));
		Forward->SetStringField(TEXT("nodeType"), TEXT("StateMachine"));
		double D = 0;
		if (Params->TryGetNumberField(TEXT("posX"), D)) Forward->SetNumberField(TEXT("posX"), D);
		if (Params->TryGetNumberField(TEXT("posY"), D)) Forward->SetNumberField(TEXT("posY"), D);

		FTool_AddAnimNode AnimNodeTool;
		return AnimNodeTool.Execute(Forward);
	}
};

// ============================================================
// set_state_animation
// ============================================================
class FTool_SetStateAnimation : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_state_animation");
		Info.Description = TEXT("Set the animation sequence played in a state.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("AnimBlueprint name"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("State machine graph name"), true});
		Info.Parameters.Add({TEXT("stateName"), TEXT("string"), TEXT("Target state name"), true});
		Info.Parameters.Add({TEXT("animationAsset"), TEXT("string"), TEXT("Animation sequence name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString GraphName = Params->GetStringField(TEXT("graph"));
		FString StateName = Params->GetStringField(TEXT("stateName"));
		FString AnimAssetName = Params->GetStringField(TEXT("animationAsset"));

		if (BlueprintName.IsEmpty() || GraphName.IsEmpty() || StateName.IsEmpty() || AnimAssetName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: blueprint, graph, stateName, animationAsset"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);
		UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(BP);
		if (!AnimBP) return FMCPToolResult::Error(TEXT("Not an AnimBlueprint"));

		UAnimationStateMachineGraph* SMGraph = FindStateMachineGraph(BP, GraphName);
		if (!SMGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *GraphName));

		UAnimStateNode* StateNode = FindStateByName(SMGraph, StateName);
		if (!StateNode) return FMCPToolResult::Error(FString::Printf(TEXT("State '%s' not found"), *StateName));

		UEdGraph* InnerGraph = StateNode->GetBoundGraph();
		if (!InnerGraph) return FMCPToolResult::Error(FString::Printf(TEXT("State '%s' has no bound graph"), *StateName));

		// Find animation asset
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetData> AnimAssets;
		ARM.Get().GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), AnimAssets, false);

		UAnimSequence* AnimSeq = nullptr;
		for (const FAssetData& Asset : AnimAssets)
		{
			if (Asset.AssetName.ToString() == AnimAssetName || Asset.PackageName.ToString() == AnimAssetName)
			{
				AnimSeq = Cast<UAnimSequence>(Asset.GetAsset());
				break;
			}
		}
		if (!AnimSeq) return FMCPToolResult::Error(FString::Printf(TEXT("Animation asset '%s' not found"), *AnimAssetName));

		// Find or create SequencePlayer
		UAnimGraphNode_SequencePlayer* SeqNode = nullptr;
		for (UEdGraphNode* Node : InnerGraph->Nodes)
		{
			SeqNode = Cast<UAnimGraphNode_SequencePlayer>(Node);
			if (SeqNode) break;
		}

		bool bCreatedNew = false;
		if (!SeqNode)
		{
			SeqNode = NewObject<UAnimGraphNode_SequencePlayer>(InnerGraph);
			SeqNode->CreateNewGuid();
			SeqNode->PostPlacedNewNode();
			SeqNode->AllocateDefaultPins();
			InnerGraph->AddNode(SeqNode, false, false);
			bCreatedNew = true;
		}

		SeqNode->SetAnimationAsset(AnimSeq);

		FKismetEditorUtilities::CompileBlueprint(AnimBP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(AnimBP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("stateName"), StateName);
		Result->SetStringField(TEXT("animationAsset"), AnimSeq->GetName());
		Result->SetBoolField(TEXT("createdNewNode"), bCreatedNew);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// create_blend_space
// ============================================================
class FTool_CreateBlendSpace : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_blend_space");
		Info.Description = TEXT("Create a new 2D Blend Space asset.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Blend space name"), true});
		Info.Parameters.Add({TEXT("packagePath"), TEXT("string"), TEXT("Package path"), true});
		Info.Parameters.Add({TEXT("skeleton"), TEXT("string"), TEXT("Skeleton name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		FString PackagePath = Params->GetStringField(TEXT("packagePath"));
		FString SkeletonName = Params->GetStringField(TEXT("skeleton"));
		if (Name.IsEmpty() || PackagePath.IsEmpty() || SkeletonName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: name, packagePath, skeleton"));
		if (!PackagePath.StartsWith(TEXT("/Game")))
			return FMCPToolResult::Error(TEXT("packagePath must start with '/Game'"));

		FString SkelError;
		USkeleton* Skeleton = ResolveSkeleton(SkeletonName, PackagePath, Name, SkelError);
		if (!Skeleton) return FMCPToolResult::Error(SkelError);

		FString FullPackagePath = PackagePath / Name;
		UPackage* Package = CreatePackage(*FullPackagePath);
		if (!Package) return FMCPToolResult::Error(TEXT("Failed to create package"));

		UBlendSpace* NewBS = NewObject<UBlendSpace>(Package, FName(*Name), RF_Public | RF_Standalone);
		if (!NewBS) return FMCPToolResult::Error(TEXT("Failed to create BlendSpace"));

		NewBS->SetSkeleton(Skeleton);
		NewBS->MarkPackageDirty();

		FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, NewBS, *PackageFilename, SaveArgs);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("assetPath"), PackagePath / Name);
		Result->SetStringField(TEXT("skeleton"), Skeleton->GetName());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// set_blend_space_samples
// ============================================================
class FTool_SetBlendSpaceSamples : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_blend_space_samples");
		Info.Description = TEXT("Set animation samples and axis parameters on a Blend Space.");
		Info.Annotations.Category = TEXT("Animation");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blendSpace"), TEXT("string"), TEXT("Blend Space name or path"), true});
		Info.Parameters.Add({TEXT("samples"), TEXT("array"), TEXT("Array of {animationAsset, x, y}"), false});
		Info.Parameters.Add({TEXT("axisXName"), TEXT("string"), TEXT("X axis display name"), false});
		Info.Parameters.Add({TEXT("axisYName"), TEXT("string"), TEXT("Y axis display name"), false});
		Info.Parameters.Add({TEXT("axisXMin"), TEXT("number"), TEXT("X axis minimum"), false});
		Info.Parameters.Add({TEXT("axisXMax"), TEXT("number"), TEXT("X axis maximum"), false});
		Info.Parameters.Add({TEXT("axisYMin"), TEXT("number"), TEXT("Y axis minimum"), false});
		Info.Parameters.Add({TEXT("axisYMax"), TEXT("number"), TEXT("Y axis maximum"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlendSpaceName = Params->GetStringField(TEXT("blendSpace"));
		if (BlendSpaceName.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing: blendSpace"));

		// Load blend space
		UBlendSpace* BS = nullptr;
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> BSAssets;
			ARM.Get().GetAssetsByClass(UBlendSpace::StaticClass()->GetClassPathName(), BSAssets, true);
			for (const FAssetData& Asset : BSAssets)
			{
				if (Asset.AssetName.ToString() == BlendSpaceName ||
					Asset.PackageName.ToString() == BlendSpaceName ||
					Asset.GetObjectPathString() == BlendSpaceName)
				{
					BS = Cast<UBlendSpace>(Asset.GetAsset());
					break;
				}
			}
		}
		if (!BS) return FMCPToolResult::Error(FString::Printf(TEXT("Blend Space '%s' not found"), *BlendSpaceName));

		BS->PreEditChange(nullptr);

		// Set axis parameters
		FString AxisXName, AxisYName;
		Params->TryGetStringField(TEXT("axisXName"), AxisXName);
		Params->TryGetStringField(TEXT("axisYName"), AxisYName);

		const FBlendParameter& ParamX = BS->GetBlendParameter(0);
		const FBlendParameter& ParamY = BS->GetBlendParameter(1);
		FBlendParameter& MutableParamX = const_cast<FBlendParameter&>(ParamX);
		FBlendParameter& MutableParamY = const_cast<FBlendParameter&>(ParamY);

		double D = 0;
		if (!AxisXName.IsEmpty()) MutableParamX.DisplayName = AxisXName;
		if (Params->TryGetNumberField(TEXT("axisXMin"), D)) MutableParamX.Min = (float)D;
		if (Params->TryGetNumberField(TEXT("axisXMax"), D)) MutableParamX.Max = (float)D;
		if (!AxisYName.IsEmpty()) MutableParamY.DisplayName = AxisYName;
		if (Params->TryGetNumberField(TEXT("axisYMin"), D)) MutableParamY.Min = (float)D;
		if (Params->TryGetNumberField(TEXT("axisYMax"), D)) MutableParamY.Max = (float)D;

		// Clear existing samples
		int32 NumExisting = BS->GetNumberOfBlendSamples();
		for (int32 i = NumExisting - 1; i >= 0; --i) BS->DeleteSample(i);

		// Add new samples
		const TArray<TSharedPtr<FJsonValue>>* SamplesArray = nullptr;
		int32 SamplesSet = 0;

		if (Params->TryGetArrayField(TEXT("samples"), SamplesArray) && SamplesArray)
		{
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			TArray<FAssetData> AnimAssets;
			ARM.Get().GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), AnimAssets, false);

			for (const TSharedPtr<FJsonValue>& SampleVal : *SamplesArray)
			{
				const TSharedPtr<FJsonObject>& SampleObj = SampleVal->AsObject();
				if (!SampleObj.IsValid()) continue;

				FString AnimAssetName = SampleObj->GetStringField(TEXT("animationAsset"));
				float X = (float)SampleObj->GetNumberField(TEXT("x"));
				float Y = (float)SampleObj->GetNumberField(TEXT("y"));

				UAnimSequence* AnimSeq = nullptr;
				if (!AnimAssetName.IsEmpty())
				{
					for (const FAssetData& Asset : AnimAssets)
					{
						if (Asset.AssetName.ToString() == AnimAssetName || Asset.PackageName.ToString() == AnimAssetName)
						{
							AnimSeq = Cast<UAnimSequence>(Asset.GetAsset());
							break;
						}
					}
				}

				FVector SampleValue(X, Y, 0.0f);
				if (AnimSeq) BS->AddSample(AnimSeq, SampleValue);
				else BS->AddSample(SampleValue);
				SamplesSet++;
			}
		}

		BS->ValidateSampleData();
		BS->PostEditChange();
		BS->MarkPackageDirty();

		UPackage* Package = BS->GetPackage();
		FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, BS, *PackageFilename, SaveArgs);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blendSpace"), BS->GetPathName());
		Result->SetNumberField(TEXT("samplesSet"), SamplesSet);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterAnimationTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_CreateAnimBlueprint>());
		R.Register(MakeShared<FTool_AddAnimState>());
		R.Register(MakeShared<FTool_RemoveAnimState>());
		R.Register(MakeShared<FTool_AddAnimTransition>());
		R.Register(MakeShared<FTool_SetTransitionRule>());
		R.Register(MakeShared<FTool_AddAnimNode>());
		R.Register(MakeShared<FTool_AddStateMachine>());
		R.Register(MakeShared<FTool_SetStateAnimation>());
		R.Register(MakeShared<FTool_CreateBlendSpace>());
		R.Register(MakeShared<FTool_SetBlendSpaceSamples>());
	}
}
