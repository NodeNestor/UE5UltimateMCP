// Function Parameter Tools — add, remove, change type of function/event parameters
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_EditablePinBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

// Helper: find entry node for a function or custom event
static UK2Node_EditablePinBase* FindEntryNode(UBlueprint* BP, const FString& FunctionName, FString& OutNodeType, bool bIncludeDelegates = false)
{
	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);

	// Strategy 1: K2Node_FunctionEntry in function graphs
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph || !Graph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase)) continue;
		if (bIncludeDelegates || !BP->DelegateSignatureGraphs.Contains(Graph))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_FunctionEntry* FE = Cast<UK2Node_FunctionEntry>(Node))
				{
					OutNodeType = BP->DelegateSignatureGraphs.Contains(Graph) ? TEXT("EventDispatcher") : TEXT("FunctionEntry");
					return FE;
				}
			}
		}
	}

	// Strategy 2: K2Node_CustomEvent
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_CustomEvent* CE = Cast<UK2Node_CustomEvent>(Node))
			{
				if (CE->CustomFunctionName.ToString().Equals(FunctionName, ESearchCase::IgnoreCase))
				{
					OutNodeType = TEXT("CustomEvent");
					return CE;
				}
			}
		}
	}

	// Strategy 3: DelegateSignatureGraphs
	if (bIncludeDelegates)
	{
		for (UEdGraph* SigGraph : BP->DelegateSignatureGraphs)
		{
			if (!SigGraph || !SigGraph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase)) continue;
			for (UEdGraphNode* Node : SigGraph->Nodes)
			{
				if (UK2Node_FunctionEntry* FE = Cast<UK2Node_FunctionEntry>(Node))
				{
					OutNodeType = TEXT("EventDispatcher");
					return FE;
				}
			}
		}
	}

	return nullptr;
}

// ============================================================
// add_function_parameter
// ============================================================
class FTool_AddFunctionParameter : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_function_parameter");
		Info.Description = TEXT("Add a parameter to a function, custom event, or event dispatcher.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("functionName"), TEXT("string"), TEXT("Function, event, or dispatcher name"), true});
		Info.Parameters.Add({TEXT("paramName"), TEXT("string"), TEXT("Parameter name"), true});
		Info.Parameters.Add({TEXT("paramType"), TEXT("string"), TEXT("Parameter type"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString FunctionName = Params->GetStringField(TEXT("functionName"));
		FString ParamName = Params->GetStringField(TEXT("paramName"));
		FString ParamType = Params->GetStringField(TEXT("paramType"));

		if (BlueprintName.IsEmpty() || FunctionName.IsEmpty() || ParamName.IsEmpty() || ParamType.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, functionName, paramName, paramType"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		FEdGraphPinType PinType;
		FString TypeError;
		if (!MCPHelpers::ResolveTypeFromString(ParamType, PinType, TypeError))
			return FMCPToolResult::Error(TypeError);

		FString NodeType;
		UK2Node_EditablePinBase* EntryNode = FindEntryNode(BP, FunctionName, NodeType, true);
		if (!EntryNode)
			return FMCPToolResult::Error(FString::Printf(TEXT("Function/event/dispatcher '%s' not found in Blueprint '%s'"), *FunctionName, *BlueprintName));

		// Check for duplicate
		for (const TSharedPtr<FUserPinInfo>& Existing : EntryNode->UserDefinedPins)
		{
			if (Existing.IsValid() && Existing->PinName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
				return FMCPToolResult::Error(FString::Printf(TEXT("Parameter '%s' already exists on '%s'"), *ParamName, *FunctionName));
		}

		EntryNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("paramName"), ParamName);
		Result->SetStringField(TEXT("paramType"), ParamType);
		Result->SetStringField(TEXT("nodeType"), NodeType);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// remove_function_parameter
// ============================================================
class FTool_RemoveFunctionParameter : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("remove_function_parameter");
		Info.Description = TEXT("Remove a parameter from a function or custom event.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("functionName"), TEXT("string"), TEXT("Function or event name"), true});
		Info.Parameters.Add({TEXT("paramName"), TEXT("string"), TEXT("Parameter to remove"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString FunctionName = Params->GetStringField(TEXT("functionName"));
		FString ParamName = Params->GetStringField(TEXT("paramName"));

		if (BlueprintName.IsEmpty() || FunctionName.IsEmpty() || ParamName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, functionName, paramName"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		FString NodeType;
		UK2Node_EditablePinBase* EntryNode = FindEntryNode(BP, FunctionName, NodeType);
		if (!EntryNode)
			return FMCPToolResult::Error(FString::Printf(TEXT("Function/event '%s' not found"), *FunctionName));

		int32 RemovedIndex = INDEX_NONE;
		for (int32 i = 0; i < EntryNode->UserDefinedPins.Num(); ++i)
		{
			if (EntryNode->UserDefinedPins[i].IsValid() &&
				EntryNode->UserDefinedPins[i]->PinName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
			{
				RemovedIndex = i;
				break;
			}
		}

		if (RemovedIndex == INDEX_NONE)
			return FMCPToolResult::Error(FString::Printf(TEXT("Parameter '%s' not found on '%s'"), *ParamName, *FunctionName));

		EntryNode->UserDefinedPins.RemoveAt(RemovedIndex);

		if (UEdGraph* OwningGraph = EntryNode->GetGraph())
		{
			if (const UEdGraphSchema* Schema = OwningGraph->GetSchema())
				Schema->ReconstructNode(*EntryNode);
		}

		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("paramName"), ParamName);
		Result->SetStringField(TEXT("nodeType"), NodeType);
		Result->SetStringField(TEXT("nodeId"), EntryNode->NodeGuid.ToString());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// change_function_parameter_type
// ============================================================
class FTool_ChangeFunctionParameterType : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("change_function_parameter_type");
		Info.Description = TEXT("Change the type of a function or custom event parameter.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("functionName"), TEXT("string"), TEXT("Function or event name"), true});
		Info.Parameters.Add({TEXT("paramName"), TEXT("string"), TEXT("Parameter name"), true});
		Info.Parameters.Add({TEXT("newType"), TEXT("string"), TEXT("New parameter type"), true});
		Info.Parameters.Add({TEXT("dryRun"), TEXT("boolean"), TEXT("If true, only analyze impact"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString FunctionName = Params->GetStringField(TEXT("functionName"));
		FString ParamName = Params->GetStringField(TEXT("paramName"));
		FString NewTypeName = Params->GetStringField(TEXT("newType"));

		if (BlueprintName.IsEmpty() || FunctionName.IsEmpty() || ParamName.IsEmpty() || NewTypeName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, functionName, paramName, newType"));

		bool bDryRun = false;
		Params->TryGetBoolField(TEXT("dryRun"), bDryRun);

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		FEdGraphPinType NewPinType;
		FString TypeError;
		if (!MCPHelpers::ResolveTypeFromString(NewTypeName, NewPinType, TypeError))
			return FMCPToolResult::Error(TypeError);

		FString NodeType;
		UK2Node_EditablePinBase* EntryNode = FindEntryNode(BP, FunctionName, NodeType);
		if (!EntryNode)
			return FMCPToolResult::Error(FString::Printf(TEXT("Function/event '%s' not found"), *FunctionName));

		bool bPinFound = false;
		for (TSharedPtr<FUserPinInfo>& PinInfo : EntryNode->UserDefinedPins)
		{
			if (PinInfo.IsValid() && PinInfo->PinName.ToString().Equals(ParamName, ESearchCase::IgnoreCase))
			{
				if (!bDryRun)
					PinInfo->PinType = NewPinType;
				bPinFound = true;
				break;
			}
		}
		if (!bPinFound)
			return FMCPToolResult::Error(FString::Printf(TEXT("Parameter '%s' not found on '%s'"), *ParamName, *FunctionName));

		if (bDryRun)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("dryRun"), true);
			Result->SetStringField(TEXT("blueprint"), BlueprintName);
			Result->SetStringField(TEXT("functionName"), FunctionName);
			Result->SetStringField(TEXT("paramName"), ParamName);
			Result->SetStringField(TEXT("newType"), NewTypeName);
			Result->SetStringField(TEXT("nodeType"), NodeType);
			return FMCPToolResult::Ok(Result);
		}

		if (UEdGraph* OwningGraph = EntryNode->GetGraph())
		{
			if (const UEdGraphSchema* Schema = OwningGraph->GetSchema())
				Schema->ReconstructNode(*EntryNode);
		}

		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("functionName"), FunctionName);
		Result->SetStringField(TEXT("paramName"), ParamName);
		Result->SetStringField(TEXT("newType"), NewTypeName);
		Result->SetStringField(TEXT("nodeType"), NodeType);
		Result->SetStringField(TEXT("nodeId"), EntryNode->NodeGuid.ToString());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterParamTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_AddFunctionParameter>());
		R.Register(MakeShared<FTool_RemoveFunctionParameter>());
		R.Register(MakeShared<FTool_ChangeFunctionParameterType>());
	}
}
