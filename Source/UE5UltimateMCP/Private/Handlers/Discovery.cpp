// Discovery Tools — list classes, functions, properties, get pin info, check pin compatibility
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "UObject/UObjectIterator.h"

// ============================================================
// list_classes
// ============================================================
class FTool_ListClasses : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_classes");
		Info.Description = TEXT("Discover available UClasses, optionally filtered by name or parent class.");
		Info.Annotations.Category = TEXT("Discovery");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Name substring filter"), false});
		Info.Parameters.Add({TEXT("parentClass"), TEXT("string"), TEXT("Only list subclasses of this class"), false});
		Info.Parameters.Add({TEXT("limit"), TEXT("number"), TEXT("Max results (default 100, max 500)"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Filter = Params->GetStringField(TEXT("filter"));
		FString ParentClassName = Params->GetStringField(TEXT("parentClass"));
		int32 Limit = 100;
		double LimitD = 0;
		if (Params->TryGetNumberField(TEXT("limit"), LimitD))
			Limit = FMath::Clamp((int32)LimitD, 1, 500);

		UClass* ParentClass = nullptr;
		if (!ParentClassName.IsEmpty())
		{
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName() == ParentClassName || It->GetName() == ParentClassName + TEXT("_C"))
				{
					ParentClass = *It;
					break;
				}
			}
			if (!ParentClass)
				return FMCPToolResult::Error(FString::Printf(TEXT("Parent class '%s' not found"), *ParentClassName));
		}

		TArray<TSharedPtr<FJsonValue>> ClassList;
		int32 TotalMatched = 0;

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class || Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)) continue;
			if (ParentClass && !Class->IsChildOf(ParentClass)) continue;
			if (!Filter.IsEmpty() && !Class->GetName().Contains(Filter, ESearchCase::IgnoreCase)) continue;

			TotalMatched++;
			if (ClassList.Num() >= Limit) continue;

			TSharedRef<FJsonObject> ClassObj = MakeShared<FJsonObject>();
			ClassObj->SetStringField(TEXT("name"), Class->GetName());
			ClassObj->SetStringField(TEXT("fullPath"), Class->GetPathName());
			ClassObj->SetBoolField(TEXT("isBlueprint"), Class->ClassGeneratedBy != nullptr);
			if (Class->GetSuperClass())
				ClassObj->SetStringField(TEXT("parentClass"), Class->GetSuperClass()->GetName());

			TArray<TSharedPtr<FJsonValue>> Flags;
			if (Class->HasAnyClassFlags(CLASS_Abstract)) Flags.Add(MakeShared<FJsonValueString>(TEXT("Abstract")));
			if (Class->HasAnyClassFlags(CLASS_Interface)) Flags.Add(MakeShared<FJsonValueString>(TEXT("Interface")));
			if (Flags.Num() > 0) ClassObj->SetArrayField(TEXT("flags"), Flags);

			ClassList.Add(MakeShared<FJsonValueObject>(ClassObj));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), ClassList.Num());
		Result->SetNumberField(TEXT("totalMatched"), TotalMatched);
		if (TotalMatched > Limit) Result->SetBoolField(TEXT("truncated"), true);
		Result->SetArrayField(TEXT("classes"), ClassList);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// list_functions
// ============================================================
class FTool_ListFunctions : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_functions");
		Info.Description = TEXT("List Blueprint-callable functions on a class with parameter and return type info.");
		Info.Annotations.Category = TEXT("Discovery");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("className"), TEXT("string"), TEXT("Class to inspect"), true});
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Name filter"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString ClassName = Params->GetStringField(TEXT("className"));
		FString Filter = Params->GetStringField(TEXT("filter"));

		if (ClassName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: className"));

		UClass* FoundClass = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == ClassName || It->GetName() == ClassName + TEXT("_C"))
			{
				FoundClass = *It;
				break;
			}
		}
		if (!FoundClass)
			return FMCPToolResult::Error(FString::Printf(TEXT("Class '%s' not found"), *ClassName));

		TArray<TSharedPtr<FJsonValue>> FuncList;
		for (TFieldIterator<UFunction> FuncIt(FoundClass); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			if (!Func) continue;
			if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_BlueprintEvent)) continue;

			FString FuncName = Func->GetName();
			if (!Filter.IsEmpty() && !FuncName.Contains(Filter, ESearchCase::IgnoreCase)) continue;

			TSharedRef<FJsonObject> FuncObj = MakeShared<FJsonObject>();
			FuncObj->SetStringField(TEXT("name"), FuncName);
			if (Func->GetOwnerClass()) FuncObj->SetStringField(TEXT("definedIn"), Func->GetOwnerClass()->GetName());
			FuncObj->SetBoolField(TEXT("isPure"), Func->HasAnyFunctionFlags(FUNC_BlueprintPure));
			FuncObj->SetBoolField(TEXT("isStatic"), Func->HasAnyFunctionFlags(FUNC_Static));
			FuncObj->SetBoolField(TEXT("isEvent"), Func->HasAnyFunctionFlags(FUNC_BlueprintEvent));

			TArray<TSharedPtr<FJsonValue>> FuncParams;
			FString ReturnType;
			for (TFieldIterator<FProperty> PropIt(Func); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop) continue;
				FString PropType = Prop->GetCPPType();

				if (Prop->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					ReturnType = PropType;
					continue;
				}
				if (Prop->HasAnyPropertyFlags(CPF_Parm))
				{
					TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
					ParamObj->SetStringField(TEXT("name"), Prop->GetName());
					ParamObj->SetStringField(TEXT("type"), PropType);
					ParamObj->SetBoolField(TEXT("isOutput"), Prop->HasAnyPropertyFlags(CPF_OutParm) && !Prop->HasAnyPropertyFlags(CPF_ReferenceParm));
					FuncParams.Add(MakeShared<FJsonValueObject>(ParamObj));
				}
			}
			FuncObj->SetArrayField(TEXT("parameters"), FuncParams);
			if (!ReturnType.IsEmpty()) FuncObj->SetStringField(TEXT("returnType"), ReturnType);
			FuncList.Add(MakeShared<FJsonValueObject>(FuncObj));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("className"), FoundClass->GetName());
		Result->SetNumberField(TEXT("count"), FuncList.Num());
		Result->SetArrayField(TEXT("functions"), FuncList);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// list_properties
// ============================================================
class FTool_ListProperties : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_properties");
		Info.Description = TEXT("List properties on a class with type and flag information.");
		Info.Annotations.Category = TEXT("Discovery");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("className"), TEXT("string"), TEXT("Class to inspect"), true});
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Name filter"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString ClassName = Params->GetStringField(TEXT("className"));
		if (ClassName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: className"));

		UClass* FoundClass = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == ClassName || It->GetName() == ClassName + TEXT("_C"))
			{
				FoundClass = *It;
				break;
			}
		}
		if (!FoundClass)
			return FMCPToolResult::Error(FString::Printf(TEXT("Class '%s' not found"), *ClassName));

		FString Filter = Params->GetStringField(TEXT("filter"));
		TArray<TSharedPtr<FJsonValue>> PropList;

		for (TFieldIterator<FProperty> PropIt(FoundClass); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!Prop) continue;
			if (!Filter.IsEmpty() && !Prop->GetName().Contains(Filter, ESearchCase::IgnoreCase)) continue;

			TSharedRef<FJsonObject> PropObj = MakeShared<FJsonObject>();
			PropObj->SetStringField(TEXT("name"), Prop->GetName());
			PropObj->SetStringField(TEXT("type"), Prop->GetCPPType());
			if (Prop->GetOwnerClass()) PropObj->SetStringField(TEXT("definedIn"), Prop->GetOwnerClass()->GetName());

			TArray<TSharedPtr<FJsonValue>> Flags;
			if (Prop->HasAnyPropertyFlags(CPF_BlueprintVisible)) Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintVisible")));
			if (Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly)) Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintReadOnly")));
			if (Prop->HasAnyPropertyFlags(CPF_Edit)) Flags.Add(MakeShared<FJsonValueString>(TEXT("EditAnywhere")));
			if (Flags.Num() > 0) PropObj->SetArrayField(TEXT("flags"), Flags);

			PropList.Add(MakeShared<FJsonValueObject>(PropObj));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("className"), FoundClass->GetName());
		Result->SetNumberField(TEXT("count"), PropList.Num());
		Result->SetArrayField(TEXT("properties"), PropList);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// get_pin_info
// ============================================================
class FTool_GetPinInfo : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_pin_info");
		Info.Description = TEXT("Get detailed information about a specific pin on a node.");
		Info.Annotations.Category = TEXT("Discovery");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		Info.Parameters.Add({TEXT("pinName"), TEXT("string"), TEXT("Pin name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));
		FString PinName = Params->GetStringField(TEXT("pinName"));

		if (BlueprintName.IsEmpty() || NodeId.IsEmpty() || PinName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, nodeId, pinName"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UEdGraphNode* Node = MCPHelpers::FindNodeByGuid(BP, NodeId);
		if (!Node)
			return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

		UEdGraphPin* Pin = Node->FindPin(FName(*PinName));
		if (!Pin)
			return FMCPToolResult::Error(FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *PinName, *NodeId));

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("pinName"), Pin->PinName.ToString());
		Result->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		Result->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		if (Pin->PinType.PinSubCategoryObject.IsValid())
			Result->SetStringField(TEXT("subtype"), Pin->PinType.PinSubCategoryObject->GetName());
		Result->SetBoolField(TEXT("isArray"), Pin->PinType.IsArray());
		if (!Pin->DefaultValue.IsEmpty())
			Result->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);

		if (Pin->LinkedTo.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Conns;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				TSharedRef<FJsonObject> CJ = MakeShared<FJsonObject>();
				CJ->SetStringField(TEXT("nodeId"), Linked->GetOwningNode()->NodeGuid.ToString());
				CJ->SetStringField(TEXT("pinName"), Linked->PinName.ToString());
				Conns.Add(MakeShared<FJsonValueObject>(CJ));
			}
			Result->SetArrayField(TEXT("connectedTo"), Conns);
		}

		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// check_pin_compatibility
// ============================================================
class FTool_CheckPinCompatibility : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("check_pin_compatibility");
		Info.Description = TEXT("Pre-flight check whether two pins can be connected.");
		Info.Annotations.Category = TEXT("Discovery");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
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

		if (BlueprintName.IsEmpty() || SourceNodeId.IsEmpty() || SourcePinName.IsEmpty() ||
			TargetNodeId.IsEmpty() || TargetPinName.IsEmpty())
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

		const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		bool bCompatible = (Response.Response != ECanCreateConnectionResponse::CONNECT_RESPONSE_DISALLOW);
		Result->SetBoolField(TEXT("compatible"), bCompatible);

		FString ResponseType;
		switch (Response.Response)
		{
		case ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE: ResponseType = TEXT("direct"); break;
		case ECanCreateConnectionResponse::CONNECT_RESPONSE_BREAK_OTHERS_A: ResponseType = TEXT("breakSourceConnections"); break;
		case ECanCreateConnectionResponse::CONNECT_RESPONSE_BREAK_OTHERS_B: ResponseType = TEXT("breakTargetConnections"); break;
		case ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE: ResponseType = TEXT("requiresConversion"); break;
		default: ResponseType = TEXT("disallowed"); break;
		}
		Result->SetStringField(TEXT("connectionType"), ResponseType);
		if (!Response.Message.IsEmpty()) Result->SetStringField(TEXT("message"), Response.Message.ToString());

		Result->SetStringField(TEXT("sourcePinType"), SourcePin->PinType.PinCategory.ToString());
		Result->SetStringField(TEXT("targetPinType"), TargetPin->PinType.PinCategory.ToString());
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterDiscoveryTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_ListClasses>());
		R.Register(MakeShared<FTool_ListFunctions>());
		R.Register(MakeShared<FTool_ListProperties>());
		R.Register(MakeShared<FTool_GetPinInfo>());
		R.Register(MakeShared<FTool_CheckPinCompatibility>());
	}
}
