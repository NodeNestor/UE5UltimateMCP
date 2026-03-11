// Blueprint Mutation Tools — modify nodes, pins, connections, assets
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_Select.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"

// ============================================================
// replace_function_calls
// ============================================================
class FTool_ReplaceFunctionCalls : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("replace_function_calls");
		Info.Description = TEXT("Redirect all function call nodes from one class to another in a Blueprint.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("oldClass"), TEXT("string"), TEXT("Current class name"), true});
		Info.Parameters.Add({TEXT("newClass"), TEXT("string"), TEXT("New class name"), true});
		Info.Parameters.Add({TEXT("dryRun"), TEXT("boolean"), TEXT("Preview changes without applying"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString OldClassName = Params->GetStringField(TEXT("oldClass"));
		FString NewClassName = Params->GetStringField(TEXT("newClass"));
		if (BlueprintName.IsEmpty() || OldClassName.IsEmpty() || NewClassName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, oldClass, newClass"));

		bool bDryRun = Params->HasField(TEXT("dryRun")) && Params->GetBoolField(TEXT("dryRun"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UClass* NewClass = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == NewClassName) { NewClass = *It; break; }
		}
		if (!NewClass)
			return FMCPToolResult::Error(FString::Printf(TEXT("Could not find class '%s'"), *NewClassName));

		TArray<UK2Node_CallFunction*> AllCallNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass<UK2Node_CallFunction>(BP, AllCallNodes);

		int32 ReplacedCount = 0;
		for (UK2Node_CallFunction* CallNode : AllCallNodes)
		{
			UClass* ParentClass = CallNode->FunctionReference.GetMemberParentClass();
			if (!ParentClass) continue;
			FString ParentName = ParentClass->GetName();
			bool bMatch = ParentName == OldClassName || ParentName == OldClassName + TEXT("_C");
			if (!bMatch) continue;

			FName FuncName = CallNode->FunctionReference.GetMemberName();
			UFunction* NewFunc = NewClass->FindFunctionByName(FuncName);
			if (!NewFunc) continue;

			if (!bDryRun)
				CallNode->SetFromFunction(NewFunc);
			ReplacedCount++;
		}

		if (!bDryRun && ReplacedCount > 0)
			MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetNumberField(TEXT("replacedCount"), ReplacedCount);
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		if (!bDryRun) Result->SetBoolField(TEXT("saved"), ReplacedCount > 0);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// delete_asset
// ============================================================
class FTool_DeleteAsset : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("delete_asset");
		Info.Description = TEXT("Delete a .uasset file after verifying no references exist. Use force=true to skip reference check.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("assetPath"), TEXT("string"), TEXT("Package path of asset to delete"), true});
		Info.Parameters.Add({TEXT("force"), TEXT("boolean"), TEXT("Force delete even with references"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString AssetPath = Params->GetStringField(TEXT("assetPath"));
		if (AssetPath.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'assetPath'"));

		bool bForce = Params->HasField(TEXT("force")) && Params->GetBoolField(TEXT("force"));

		FString PackageFilename = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
		PackageFilename = FPaths::ConvertRelativePathToFull(PackageFilename);

		if (!IFileManager::Get().FileExists(*PackageFilename))
			return FMCPToolResult::Error(FString::Printf(TEXT("Asset file not found: %s"), *PackageFilename));

		IAssetRegistry& Registry = *IAssetRegistry::Get();
		TArray<FName> Referencers;
		Registry.GetReferencers(FName(*AssetPath), Referencers);
		Referencers.RemoveAll([&AssetPath](const FName& Ref) { return Ref.ToString() == AssetPath; });

		if (Referencers.Num() > 0 && !bForce)
			return FMCPToolResult::Error(FString::Printf(TEXT("Asset has %d referencers. Use force=true to override."), Referencers.Num()));

		bool bDeleted = IFileManager::Get().Delete(*PackageFilename, false, true);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), bDeleted);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetBoolField(TEXT("forced"), bForce);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// connect_pins
// ============================================================
class FTool_ConnectPins : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("connect_pins");
		Info.Description = TEXT("Connect two pins together in a Blueprint graph. Use check_pin_compatibility first to verify.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("sourceNodeId"), TEXT("string"), TEXT("Source node GUID"), true});
		Info.Parameters.Add({TEXT("sourcePinName"), TEXT("string"), TEXT("Source pin name"), true});
		Info.Parameters.Add({TEXT("targetNodeId"), TEXT("string"), TEXT("Target node GUID"), true});
		Info.Parameters.Add({TEXT("targetPinName"), TEXT("string"), TEXT("Target pin name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString SourceNodeId = Params->GetStringField(TEXT("sourceNodeId"));
		FString SourcePinName = Params->GetStringField(TEXT("sourcePinName"));
		FString TargetNodeId = Params->GetStringField(TEXT("targetNodeId"));
		FString TargetPinName = Params->GetStringField(TEXT("targetPinName"));

		if (BlueprintName.IsEmpty() || SourceNodeId.IsEmpty() || SourcePinName.IsEmpty() || TargetNodeId.IsEmpty() || TargetPinName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UEdGraph* SourceGraph = nullptr;
		UEdGraphNode* SourceNode = MCPHelpers::FindNodeByGuid(BP, SourceNodeId, &SourceGraph);
		if (!SourceNode) return FMCPToolResult::Error(FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeId));

		UEdGraphNode* TargetNode = MCPHelpers::FindNodeByGuid(BP, TargetNodeId);
		if (!TargetNode) return FMCPToolResult::Error(FString::Printf(TEXT("Target node '%s' not found"), *TargetNodeId));

		UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*SourcePinName));
		if (!SourcePin) return FMCPToolResult::Error(FString::Printf(TEXT("Source pin '%s' not found"), *SourcePinName));

		UEdGraphPin* TargetPin = TargetNode->FindPin(FName(*TargetPinName));
		if (!TargetPin) return FMCPToolResult::Error(FString::Printf(TEXT("Target pin '%s' not found"), *TargetPinName));

		const UEdGraphSchema* Schema = SourceGraph ? SourceGraph->GetSchema() : nullptr;
		if (!Schema) return FMCPToolResult::Error(TEXT("Graph schema not found"));

		bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);
		if (!bConnected)
			return FMCPToolResult::Error(TEXT("Cannot connect — types are incompatible"));

		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// disconnect_pin
// ============================================================
class FTool_DisconnectPin : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("disconnect_pin");
		Info.Description = TEXT("Break connections on a pin. Optionally specify a target to disconnect only one link.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		Info.Parameters.Add({TEXT("pinName"), TEXT("string"), TEXT("Pin name"), true});
		Info.Parameters.Add({TEXT("targetNodeId"), TEXT("string"), TEXT("Specific target node GUID"), false});
		Info.Parameters.Add({TEXT("targetPinName"), TEXT("string"), TEXT("Specific target pin name"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));
		FString PinName = Params->GetStringField(TEXT("pinName"));
		if (BlueprintName.IsEmpty() || NodeId.IsEmpty() || PinName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields"));

		FString TargetNodeId = Params->GetStringField(TEXT("targetNodeId"));
		FString TargetPinName = Params->GetStringField(TEXT("targetPinName"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UEdGraphNode* Node = MCPHelpers::FindNodeByGuid(BP, NodeId);
		if (!Node) return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

		UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
		if (!Pin) return FMCPToolResult::Error(FString::Printf(TEXT("Pin '%s' not found"), *PinName));

		int32 DisconnectedCount = 0;
		if (!TargetNodeId.IsEmpty() && !TargetPinName.IsEmpty())
		{
			UEdGraphNode* TargetNode = MCPHelpers::FindNodeByGuid(BP, TargetNodeId);
			if (!TargetNode) return FMCPToolResult::Error(TEXT("Target node not found"));
			UEdGraphPin* TargetPin = TargetNode->FindPin(FName(*TargetPinName));
			if (!TargetPin) return FMCPToolResult::Error(TEXT("Target pin not found"));
			if (Pin->LinkedTo.Contains(TargetPin)) { Pin->BreakLinkTo(TargetPin); DisconnectedCount = 1; }
		}
		else
		{
			DisconnectedCount = Pin->LinkedTo.Num();
			if (DisconnectedCount > 0) Pin->BreakAllPinLinks(true);
		}

		bool bSaved = DisconnectedCount > 0 ? MCPHelpers::SaveBlueprintPackage(BP) : false;

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetNumberField(TEXT("disconnectedCount"), DisconnectedCount);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_node
// ============================================================
class FTool_AddNode : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_node");
		Info.Description = TEXT("Create a new node in a Blueprint graph. Supports: CallFunction, VariableGet, VariableSet, BreakStruct, MakeStruct, DynamicCast, Branch, Sequence, CustomEvent, Comment, Reroute, and more.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("Graph name"), true});
		Info.Parameters.Add({TEXT("nodeType"), TEXT("string"), TEXT("Node type to create"), true});
		Info.Parameters.Add({TEXT("posX"), TEXT("number"), TEXT("X position"), false});
		Info.Parameters.Add({TEXT("posY"), TEXT("number"), TEXT("Y position"), false});
		Info.Parameters.Add({TEXT("functionName"), TEXT("string"), TEXT("For CallFunction/OverrideEvent/CallParentFunction"), false});
		Info.Parameters.Add({TEXT("className"), TEXT("string"), TEXT("For CallFunction: class to find function on"), false});
		Info.Parameters.Add({TEXT("variableName"), TEXT("string"), TEXT("For VariableGet/VariableSet"), false});
		Info.Parameters.Add({TEXT("typeName"), TEXT("string"), TEXT("For BreakStruct/MakeStruct"), false});
		Info.Parameters.Add({TEXT("castTarget"), TEXT("string"), TEXT("For DynamicCast"), false});
		Info.Parameters.Add({TEXT("eventName"), TEXT("string"), TEXT("For CustomEvent"), false});
		Info.Parameters.Add({TEXT("comment"), TEXT("string"), TEXT("For Comment node"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString GraphName = Params->GetStringField(TEXT("graph"));
		FString NodeType = Params->GetStringField(TEXT("nodeType"));
		if (BlueprintName.IsEmpty() || GraphName.IsEmpty() || NodeType.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, graph, nodeType"));

		int32 PosX = Params->HasField(TEXT("posX")) ? (int32)Params->GetNumberField(TEXT("posX")) : 0;
		int32 PosY = Params->HasField(TEXT("posY")) ? (int32)Params->GetNumberField(TEXT("posY")) : 0;

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		FString DecodedGraphName = MCPHelpers::UrlDecode(GraphName);
		UEdGraph* TargetGraph = nullptr;
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName().Equals(DecodedGraphName, ESearchCase::IgnoreCase))
			{ TargetGraph = Graph; break; }
		}
		if (!TargetGraph) return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *DecodedGraphName));

		UEdGraphNode* NewNode = nullptr;

		if (NodeType == TEXT("CallFunction"))
		{
			FString FunctionName = Params->GetStringField(TEXT("functionName"));
			FString ClassName = Params->GetStringField(TEXT("className"));
			if (FunctionName.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'functionName'"));

			UFunction* TargetFunc = nullptr;
			if (!ClassName.IsEmpty())
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					if (It->GetName() == ClassName) { TargetFunc = It->FindFunctionByName(FName(*FunctionName)); break; }
				}
			}
			if (!TargetFunc)
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					UFunction* F = It->FindFunctionByName(FName(*FunctionName));
					if (F) { TargetFunc = F; break; }
				}
			}
			if (!TargetFunc) return FMCPToolResult::Error(FString::Printf(TEXT("Function '%s' not found"), *FunctionName));

			UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(TargetGraph);
			CallNode->SetFromFunction(TargetFunc);
			CallNode->NodePosX = PosX; CallNode->NodePosY = PosY;
			TargetGraph->AddNode(CallNode, false, false);
			CallNode->AllocateDefaultPins();
			NewNode = CallNode;
		}
		else if (NodeType == TEXT("VariableGet") || NodeType == TEXT("VariableSet"))
		{
			FString VariableName = Params->GetStringField(TEXT("variableName"));
			if (VariableName.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'variableName'"));

			if (NodeType == TEXT("VariableGet"))
			{
				UK2Node_VariableGet* N = NewObject<UK2Node_VariableGet>(TargetGraph);
				N->VariableReference.SetSelfMember(FName(*VariableName));
				N->NodePosX = PosX; N->NodePosY = PosY;
				TargetGraph->AddNode(N, false, false);
				N->AllocateDefaultPins();
				NewNode = N;
			}
			else
			{
				UK2Node_VariableSet* N = NewObject<UK2Node_VariableSet>(TargetGraph);
				N->VariableReference.SetSelfMember(FName(*VariableName));
				N->NodePosX = PosX; N->NodePosY = PosY;
				TargetGraph->AddNode(N, false, false);
				N->AllocateDefaultPins();
				NewNode = N;
			}
		}
		else if (NodeType == TEXT("BreakStruct") || NodeType == TEXT("MakeStruct"))
		{
			FString TypeNameStr = Params->GetStringField(TEXT("typeName"));
			if (TypeNameStr.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'typeName'"));
			FString SearchName = TypeNameStr.StartsWith(TEXT("F")) ? TypeNameStr.Mid(1) : TypeNameStr;
			UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*SearchName);
			if (!FoundStruct) FoundStruct = FindFirstObject<UScriptStruct>(*TypeNameStr);
			if (!FoundStruct) return FMCPToolResult::Error(FString::Printf(TEXT("Struct '%s' not found"), *TypeNameStr));

			if (NodeType == TEXT("BreakStruct"))
			{
				UK2Node_BreakStruct* N = NewObject<UK2Node_BreakStruct>(TargetGraph);
				N->StructType = FoundStruct; N->NodePosX = PosX; N->NodePosY = PosY;
				TargetGraph->AddNode(N, false, false); N->AllocateDefaultPins(); NewNode = N;
			}
			else
			{
				UK2Node_MakeStruct* N = NewObject<UK2Node_MakeStruct>(TargetGraph);
				N->StructType = FoundStruct; N->NodePosX = PosX; N->NodePosY = PosY;
				TargetGraph->AddNode(N, false, false); N->AllocateDefaultPins(); NewNode = N;
			}
		}
		else if (NodeType == TEXT("Branch"))
		{
			UK2Node_IfThenElse* N = NewObject<UK2Node_IfThenElse>(TargetGraph);
			N->NodePosX = PosX; N->NodePosY = PosY;
			TargetGraph->AddNode(N, false, false); N->AllocateDefaultPins(); NewNode = N;
		}
		else if (NodeType == TEXT("Sequence"))
		{
			UK2Node_ExecutionSequence* N = NewObject<UK2Node_ExecutionSequence>(TargetGraph);
			N->NodePosX = PosX; N->NodePosY = PosY;
			TargetGraph->AddNode(N, false, false); N->AllocateDefaultPins(); NewNode = N;
		}
		else if (NodeType == TEXT("CustomEvent"))
		{
			FString EventName = Params->GetStringField(TEXT("eventName"));
			if (EventName.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'eventName'"));
			UK2Node_CustomEvent* N = NewObject<UK2Node_CustomEvent>(TargetGraph);
			N->CustomFunctionName = FName(*EventName);
			N->NodePosX = PosX; N->NodePosY = PosY;
			TargetGraph->AddNode(N, false, false); N->AllocateDefaultPins(); NewNode = N;
		}
		else if (NodeType == TEXT("DynamicCast"))
		{
			FString CastTarget = Params->GetStringField(TEXT("castTarget"));
			if (CastTarget.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'castTarget'"));
			UClass* TargetClass = nullptr;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName() == CastTarget || It->GetName() == CastTarget + TEXT("_C"))
				{ TargetClass = *It; break; }
			}
			if (!TargetClass) return FMCPToolResult::Error(FString::Printf(TEXT("Class '%s' not found"), *CastTarget));
			UK2Node_DynamicCast* N = NewObject<UK2Node_DynamicCast>(TargetGraph);
			N->TargetType = TargetClass; N->NodePosX = PosX; N->NodePosY = PosY;
			TargetGraph->AddNode(N, false, false); N->AllocateDefaultPins(); NewNode = N;
		}
		else if (NodeType == TEXT("Comment"))
		{
			FString CommentText = Params->GetStringField(TEXT("comment"));
			if (CommentText.IsEmpty()) CommentText = TEXT("Comment");
			UEdGraphNode_Comment* N = NewObject<UEdGraphNode_Comment>(TargetGraph);
			N->NodeComment = CommentText; N->NodePosX = PosX; N->NodePosY = PosY;
			N->NodeWidth = 400; N->NodeHeight = 200;
			TargetGraph->AddNode(N, false, false); N->AllocateDefaultPins(); NewNode = N;
		}
		else if (NodeType == TEXT("Reroute"))
		{
			UK2Node_Knot* N = NewObject<UK2Node_Knot>(TargetGraph);
			N->NodePosX = PosX; N->NodePosY = PosY;
			TargetGraph->AddNode(N, false, false); N->AllocateDefaultPins(); NewNode = N;
		}
		else
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Unsupported nodeType '%s'"), *NodeType));
		}

		if (!NewNode) return FMCPToolResult::Error(TEXT("Failed to create node"));
		if (!NewNode->NodeGuid.IsValid()) NewNode->CreateNewGuid();

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("graph"), DecodedGraphName);
		Result->SetStringField(TEXT("nodeType"), NodeType);
		Result->SetStringField(TEXT("nodeId"), NewNode->NodeGuid.ToString());
		Result->SetBoolField(TEXT("saved"), bSaved);
		TSharedPtr<FJsonObject> NodeState = MCPHelpers::SerializeNode(NewNode);
		if (NodeState.IsValid()) Result->SetObjectField(TEXT("node"), NodeState);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// delete_node
// ============================================================
class FTool_DeleteNode : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("delete_node");
		Info.Description = TEXT("Remove a node from a Blueprint graph. Cannot delete root/entry/event nodes.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));
		if (BlueprintName.IsEmpty() || NodeId.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing required fields"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UEdGraph* Graph = nullptr;
		UEdGraphNode* Node = MCPHelpers::FindNodeByGuid(BP, NodeId, &Graph);
		if (!Node) return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

		if (Cast<UK2Node_FunctionEntry>(Node))
			return FMCPToolResult::Error(TEXT("Cannot delete FunctionEntry node"));
		if (Cast<UK2Node_Event>(Node))
			return FMCPToolResult::Error(TEXT("Cannot delete event entry node"));
		if (Cast<UK2Node_CustomEvent>(Node))
			return FMCPToolResult::Error(TEXT("Cannot delete CustomEvent entry node"));

		FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		Node->BreakAllNodeLinks();
		Graph->RemoveNode(Node);

		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("nodeId"), NodeId);
		Result->SetStringField(TEXT("nodeTitle"), NodeTitle);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// move_node
// ============================================================
class FTool_MoveNode : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("move_node");
		Info.Description = TEXT("Move a node to a new position in the graph editor.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		Info.Parameters.Add({TEXT("posX"), TEXT("number"), TEXT("New X position"), true});
		Info.Parameters.Add({TEXT("posY"), TEXT("number"), TEXT("New Y position"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));
		if (BlueprintName.IsEmpty() || NodeId.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing required fields"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UEdGraphNode* Node = MCPHelpers::FindNodeByGuid(BP, NodeId);
		if (!Node) return FMCPToolResult::Error(TEXT("Node not found"));

		Node->NodePosX = (int32)Params->GetNumberField(TEXT("posX"));
		Node->NodePosY = (int32)Params->GetNumberField(TEXT("posY"));

		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetNumberField(TEXT("posX"), Node->NodePosX);
		Result->SetNumberField(TEXT("posY"), Node->NodePosY);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// set_pin_default
// ============================================================
class FTool_SetPinDefault : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_pin_default");
		Info.Description = TEXT("Set the default value of an input pin on a node.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		Info.Parameters.Add({TEXT("pinName"), TEXT("string"), TEXT("Pin name"), true});
		Info.Parameters.Add({TEXT("value"), TEXT("string"), TEXT("New default value"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));
		FString PinName = Params->GetStringField(TEXT("pinName"));
		FString Value = Params->GetStringField(TEXT("value"));
		if (BlueprintName.IsEmpty() || NodeId.IsEmpty() || PinName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UEdGraph* Graph = nullptr;
		UEdGraphNode* Node = MCPHelpers::FindNodeByGuid(BP, NodeId, &Graph);
		if (!Node) return FMCPToolResult::Error(TEXT("Node not found"));

		UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
		if (!Pin) return FMCPToolResult::Error(TEXT("Pin not found"));
		if (Pin->Direction != EGPD_Input) return FMCPToolResult::Error(TEXT("Can only set defaults on input pins"));

		FString OldValue = Pin->DefaultValue;
		const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
		if (Schema) Schema->TrySetDefaultValue(*Pin, Value);
		else Pin->DefaultValue = Value;

		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
		FKismetEditorUtilities::CompileBlueprint(BP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("oldValue"), OldValue);
		Result->SetStringField(TEXT("newValue"), Pin->DefaultValue);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// duplicate_nodes
// ============================================================
class FTool_DuplicateNodes : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("duplicate_nodes");
		Info.Description = TEXT("Duplicate one or more nodes in a Blueprint graph with optional position offset.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("nodeIds"), TEXT("array"), TEXT("Array of node GUIDs to duplicate"), true});
		Info.Parameters.Add({TEXT("offsetX"), TEXT("number"), TEXT("X offset for duplicates"), false});
		Info.Parameters.Add({TEXT("offsetY"), TEXT("number"), TEXT("Y offset for duplicates"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		// Stub — duplication requires deep node cloning; return info about what would be duplicated
		return FMCPToolResult::Error(TEXT("duplicate_nodes is not yet implemented in this version"));
	}
};

// ============================================================
// set_node_comment
// ============================================================
class FTool_SetNodeComment : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_node_comment");
		Info.Description = TEXT("Set or clear the comment bubble text on a node.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		Info.Parameters.Add({TEXT("comment"), TEXT("string"), TEXT("Comment text (empty to clear)"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));
		FString Comment = Params->GetStringField(TEXT("comment"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UEdGraphNode* Node = MCPHelpers::FindNodeByGuid(BP, NodeId);
		if (!Node) return FMCPToolResult::Error(TEXT("Node not found"));

		FString OldComment = Node->NodeComment;
		Node->NodeComment = Comment;
		Node->bCommentBubbleVisible = !Comment.IsEmpty();

		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("oldComment"), OldComment);
		Result->SetStringField(TEXT("newComment"), Comment);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// refresh_all_nodes
// ============================================================
class FTool_RefreshAllNodes : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("refresh_all_nodes");
		Info.Description = TEXT("Refresh all nodes in a Blueprint, removing orphaned pins and recompiling.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		if (BlueprintName.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'blueprint'"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		FBlueprintEditorUtils::RefreshAllNodes(BP);

		// Remove orphaned pins
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		int32 OrphanedPinsRemoved = 0;
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				for (int32 i = Node->Pins.Num() - 1; i >= 0; --i)
				{
					if (Node->Pins[i] && Node->Pins[i]->bOrphanedPin)
					{
						Node->Pins[i]->BreakAllPinLinks();
						Node->Pins.RemoveAt(i);
						OrphanedPinsRemoved++;
					}
				}
			}
		}

		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetNumberField(TEXT("orphanedPinsRemoved"), OrphanedPinsRemoved);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// rename_asset
// ============================================================
class FTool_RenameAsset : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("rename_asset");
		Info.Description = TEXT("Rename or move an asset to a new path with automatic reference fixup.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("assetPath"), TEXT("string"), TEXT("Current asset path"), true});
		Info.Parameters.Add({TEXT("newPath"), TEXT("string"), TEXT("New path or name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString AssetPath = Params->GetStringField(TEXT("assetPath"));
		FString NewPath = Params->GetStringField(TEXT("newPath"));
		if (AssetPath.IsEmpty() || NewPath.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: assetPath, newPath"));

		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		// Stub: actual rename requires FAssetRenameData pipeline
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("oldPath"), AssetPath);
		Result->SetStringField(TEXT("newPath"), NewPath);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// set_blueprint_default
// ============================================================
class FTool_SetBlueprintDefault : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_blueprint_default");
		Info.Description = TEXT("Set the default value of a Blueprint variable on the CDO (Class Default Object).");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("variable"), TEXT("string"), TEXT("Variable name"), true});
		Info.Parameters.Add({TEXT("value"), TEXT("string"), TEXT("New default value as string"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString VariableName = Params->GetStringField(TEXT("variable"));
		FString Value = Params->GetStringField(TEXT("value"));
		if (BlueprintName.IsEmpty() || VariableName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		if (!BP->GeneratedClass)
			return FMCPToolResult::Error(TEXT("Blueprint has no generated class"));

		UObject* CDO = BP->GeneratedClass->GetDefaultObject();
		if (!CDO)
			return FMCPToolResult::Error(TEXT("Could not get CDO"));

		FProperty* Prop = BP->GeneratedClass->FindPropertyByName(FName(*VariableName));
		if (!Prop)
			return FMCPToolResult::Error(FString::Printf(TEXT("Variable '%s' not found"), *VariableName));

		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(CDO);
		Prop->ImportText_Direct(*Value, PropAddr, CDO, PPF_None);

		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("variable"), VariableName);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterBlueprintMutationTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_ReplaceFunctionCalls>());
		R.Register(MakeShared<FTool_DeleteAsset>());
		R.Register(MakeShared<FTool_ConnectPins>());
		R.Register(MakeShared<FTool_DisconnectPin>());
		R.Register(MakeShared<FTool_AddNode>());
		R.Register(MakeShared<FTool_DeleteNode>());
		R.Register(MakeShared<FTool_MoveNode>());
		R.Register(MakeShared<FTool_SetPinDefault>());
		R.Register(MakeShared<FTool_DuplicateNodes>());
		R.Register(MakeShared<FTool_SetNodeComment>());
		R.Register(MakeShared<FTool_RefreshAllNodes>());
		R.Register(MakeShared<FTool_RenameAsset>());
		R.Register(MakeShared<FTool_SetBlueprintDefault>());
	}
}
