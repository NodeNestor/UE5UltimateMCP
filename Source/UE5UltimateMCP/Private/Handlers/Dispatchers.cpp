// Dispatcher Tools — add and list event dispatchers
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

// ============================================================
// add_event_dispatcher
// ============================================================
class FTool_AddEventDispatcher : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_event_dispatcher");
		Info.Description = TEXT("Create a new multicast delegate (event dispatcher) on a Blueprint, optionally with parameters.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("dispatcherName"), TEXT("string"), TEXT("Name for the event dispatcher"), true});
		Info.Parameters.Add({TEXT("parameters"), TEXT("array"), TEXT("Array of {name, type} objects for dispatcher parameters"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString DispatcherName = Params->GetStringField(TEXT("dispatcherName"));

		if (BlueprintName.IsEmpty() || DispatcherName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, dispatcherName"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		FName DispatcherFName(*DispatcherName);

		// Check for name conflicts
		for (const FBPVariableDescription& Var : BP->NewVariables)
		{
			if (Var.VarName == DispatcherFName)
				return FMCPToolResult::Error(FString::Printf(TEXT("A variable or dispatcher named '%s' already exists"), *DispatcherName));
		}

		// Add delegate variable
		FEdGraphPinType DelegateType;
		DelegateType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
		bool bVarAdded = FBlueprintEditorUtils::AddMemberVariable(BP, DispatcherFName, DelegateType);
		if (!bVarAdded)
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to add delegate variable for '%s'"), *DispatcherName));

		// Create signature graph
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		UEdGraph* SigGraph = FBlueprintEditorUtils::CreateNewGraph(BP, DispatcherFName,
			UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		if (!SigGraph)
			return FMCPToolResult::Error(TEXT("Failed to create delegate signature graph"));

		K2Schema->CreateDefaultNodesForGraph(*SigGraph);
		K2Schema->CreateFunctionGraphTerminators(*SigGraph, static_cast<UClass*>(nullptr));
		K2Schema->AddExtraFunctionFlags(SigGraph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
		K2Schema->MarkFunctionEntryAsEditable(SigGraph, true);
		BP->DelegateSignatureGraphs.Add(SigGraph);

		// Add parameters if provided
		TArray<TSharedPtr<FJsonValue>> AddedParamsJson;
		const TArray<TSharedPtr<FJsonValue>>* ParamsArr = nullptr;
		if (Params->TryGetArrayField(TEXT("parameters"), ParamsArr) && ParamsArr)
		{
			UK2Node_EditablePinBase* EntryNode = nullptr;
			for (UEdGraphNode* Node : SigGraph->Nodes)
			{
				if (UK2Node_FunctionEntry* FE = Cast<UK2Node_FunctionEntry>(Node))
				{
					EntryNode = FE;
					break;
				}
			}

			if (EntryNode)
			{
				for (const TSharedPtr<FJsonValue>& ParamVal : *ParamsArr)
				{
					if (!ParamVal.IsValid() || ParamVal->Type != EJson::Object) continue;
					TSharedPtr<FJsonObject> ParamObj = ParamVal->AsObject();

					FString ParamName = ParamObj->GetStringField(TEXT("name"));
					FString ParamType = ParamObj->GetStringField(TEXT("type"));
					if (ParamName.IsEmpty() || ParamType.IsEmpty()) continue;

					FEdGraphPinType PinType;
					FString TypeError;
					if (!MCPHelpers::ResolveTypeFromString(ParamType, PinType, TypeError))
						return FMCPToolResult::Error(FString::Printf(TEXT("Parameter '%s': %s"), *ParamName, *TypeError));

					EntryNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output);

					TSharedRef<FJsonObject> ParamJson = MakeShared<FJsonObject>();
					ParamJson->SetStringField(TEXT("name"), ParamName);
					ParamJson->SetStringField(TEXT("type"), ParamType);
					AddedParamsJson.Add(MakeShared<FJsonValueObject>(ParamJson));
				}
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("dispatcherName"), DispatcherName);
		Result->SetArrayField(TEXT("parameters"), AddedParamsJson);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// list_event_dispatchers
// ============================================================
class FTool_ListEventDispatchers : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_event_dispatchers");
		Info.Description = TEXT("List all event dispatchers on a Blueprint with their parameters.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		if (BlueprintName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: blueprint"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		TSet<FName> DelegateNameSet;
		FBlueprintEditorUtils::GetDelegateNameList(BP, DelegateNameSet);

		TArray<TSharedPtr<FJsonValue>> DispatchersArr;
		for (const FName& DelegateName : DelegateNameSet)
		{
			TSharedRef<FJsonObject> DispObj = MakeShared<FJsonObject>();
			DispObj->SetStringField(TEXT("name"), DelegateName.ToString());

			TArray<TSharedPtr<FJsonValue>> ParamsArr;
			UEdGraph* SigGraph = FBlueprintEditorUtils::GetDelegateSignatureGraphByName(BP, DelegateName);
			if (SigGraph)
			{
				for (UEdGraphNode* Node : SigGraph->Nodes)
				{
					UK2Node_FunctionEntry* FE = Cast<UK2Node_FunctionEntry>(Node);
					if (!FE) continue;
					for (const TSharedPtr<FUserPinInfo>& PinInfo : FE->UserDefinedPins)
					{
						if (!PinInfo.IsValid()) continue;
						TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
						ParamObj->SetStringField(TEXT("name"), PinInfo->PinName.ToString());
						FString TypeStr = PinInfo->PinType.PinCategory.ToString();
						if (PinInfo->PinType.PinSubCategoryObject.IsValid())
							TypeStr = PinInfo->PinType.PinSubCategoryObject->GetName();
						ParamObj->SetStringField(TEXT("type"), TypeStr);
						ParamsArr.Add(MakeShared<FJsonValueObject>(ParamObj));
					}
					break;
				}
			}
			DispObj->SetArrayField(TEXT("parameters"), ParamsArr);
			DispatchersArr.Add(MakeShared<FJsonValueObject>(DispObj));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetNumberField(TEXT("count"), DispatchersArr.Num());
		Result->SetArrayField(TEXT("dispatchers"), DispatchersArr);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterDispatcherTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_AddEventDispatcher>());
		R.Register(MakeShared<FTool_ListEventDispatchers>());
	}
}
