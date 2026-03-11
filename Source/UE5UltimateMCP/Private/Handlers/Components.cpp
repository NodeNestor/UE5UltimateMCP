// Component Tools — list, add, remove components in Blueprint SCS
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/UObjectIterator.h"

// ============================================================
// list_components
// ============================================================
class FTool_ListComponents : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_components");
		Info.Description = TEXT("List all components in a Blueprint's Simple Construction Script.");
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

		USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
		if (!SCS)
			return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint '%s' has no SCS (not an Actor Blueprint)"), *BlueprintName));

		const TArray<USCS_Node*>& AllNodes = SCS->GetAllNodes();
		TArray<TSharedPtr<FJsonValue>> ComponentsArr;

		for (USCS_Node* Node : AllNodes)
		{
			if (!Node) continue;
			TSharedRef<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("name"), Node->GetVariableName().ToString());
			CompObj->SetStringField(TEXT("componentClass"), Node->ComponentClass ? Node->ComponentClass->GetName() : TEXT("None"));

			// Parent component
			for (USCS_Node* Candidate : AllNodes)
			{
				if (Candidate && Candidate->GetChildNodes().Contains(Node))
				{
					CompObj->SetStringField(TEXT("parentComponent"), Candidate->GetVariableName().ToString());
					break;
				}
			}

			const TArray<USCS_Node*>& RootNodes = SCS->GetRootNodes();
			bool bIsSceneRoot = RootNodes.Num() > 0 && RootNodes[0] == Node;
			CompObj->SetBoolField(TEXT("isSceneRoot"), bIsSceneRoot);
			CompObj->SetNumberField(TEXT("childCount"), Node->GetChildNodes().Num());
			ComponentsArr.Add(MakeShared<FJsonValueObject>(CompObj));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetNumberField(TEXT("count"), ComponentsArr.Num());
		Result->SetArrayField(TEXT("components"), ComponentsArr);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_component
// ============================================================
class FTool_AddComponent : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_component");
		Info.Description = TEXT("Add a component to a Blueprint's Simple Construction Script.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("componentClass"), TEXT("string"), TEXT("Component class (e.g. StaticMeshComponent)"), true});
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Component name"), true});
		Info.Parameters.Add({TEXT("parentComponent"), TEXT("string"), TEXT("Parent component name to attach to"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString ComponentClassName = Params->GetStringField(TEXT("componentClass"));
		FString ComponentName = Params->GetStringField(TEXT("name"));

		if (BlueprintName.IsEmpty() || ComponentClassName.IsEmpty() || ComponentName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, componentClass, name"));

		FString ParentComponentName;
		Params->TryGetStringField(TEXT("parentComponent"), ParentComponentName);

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
		if (!SCS)
			return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint '%s' has no SCS"), *BlueprintName));

		// Check for duplicate
		const TArray<USCS_Node*>& ExistingNodes = SCS->GetAllNodes();
		for (USCS_Node* Existing : ExistingNodes)
		{
			if (Existing && Existing->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
				return FMCPToolResult::Error(FString::Printf(TEXT("Component '%s' already exists"), *ComponentName));
		}

		// Resolve component class
		UClass* ComponentClass = nullptr;
		TArray<FString> NamesToTry;
		NamesToTry.Add(ComponentClassName);
		if (!ComponentClassName.StartsWith(TEXT("U")))
			NamesToTry.Add(FString::Printf(TEXT("U%s"), *ComponentClassName));
		else
			NamesToTry.Add(ComponentClassName.Mid(1));

		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UActorComponent::StaticClass())) continue;
			FString ClassName = It->GetName();
			for (const FString& NameToTry : NamesToTry)
			{
				if (ClassName.Equals(NameToTry, ESearchCase::IgnoreCase))
				{
					ComponentClass = *It;
					break;
				}
			}
			if (ComponentClass) break;
		}

		if (!ComponentClass)
			return FMCPToolResult::Error(FString::Printf(TEXT("Component class '%s' not found or not a UActorComponent subclass"), *ComponentClassName));

		// Find parent SCS node if specified
		USCS_Node* ParentSCSNode = nullptr;
		if (!ParentComponentName.IsEmpty())
		{
			for (USCS_Node* Node : ExistingNodes)
			{
				if (Node && Node->GetVariableName().ToString().Equals(ParentComponentName, ESearchCase::IgnoreCase))
				{
					ParentSCSNode = Node;
					break;
				}
			}
			if (!ParentSCSNode)
				return FMCPToolResult::Error(FString::Printf(TEXT("Parent component '%s' not found"), *ParentComponentName));
		}

		USCS_Node* NewNode = SCS->CreateNode(ComponentClass, FName(*ComponentName));
		if (!NewNode)
			return FMCPToolResult::Error(TEXT("Failed to create SCS node"));

		if (ParentSCSNode)
			ParentSCSNode->AddChildNode(NewNode);
		else
			SCS->AddNode(NewNode);

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("name"), NewNode->GetVariableName().ToString());
		Result->SetStringField(TEXT("componentClass"), ComponentClass->GetName());
		if (ParentSCSNode) Result->SetStringField(TEXT("parentComponent"), ParentSCSNode->GetVariableName().ToString());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// remove_component
// ============================================================
class FTool_RemoveComponent : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("remove_component");
		Info.Description = TEXT("Remove a component from a Blueprint's Simple Construction Script.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Component to remove"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString ComponentName = Params->GetStringField(TEXT("name"));

		if (BlueprintName.IsEmpty() || ComponentName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, name"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
		if (!SCS)
			return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint '%s' has no SCS"), *BlueprintName));

		USCS_Node* NodeToRemove = nullptr;
		const TArray<USCS_Node*>& AllNodes = SCS->GetAllNodes();
		for (USCS_Node* Node : AllNodes)
		{
			if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				NodeToRemove = Node;
				break;
			}
		}

		if (!NodeToRemove)
			return FMCPToolResult::Error(FString::Printf(TEXT("Component '%s' not found"), *ComponentName));

		const TArray<USCS_Node*>& RootNodes = SCS->GetRootNodes();
		if (RootNodes.Contains(NodeToRemove) && NodeToRemove->GetChildNodes().Num() > 0)
			return FMCPToolResult::Error(FString::Printf(TEXT("Cannot remove root component '%s' with %d children. Remove children first."),
				*ComponentName, NodeToRemove->GetChildNodes().Num()));

		SCS->RemoveNodeAndPromoteChildren(NodeToRemove);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("name"), ComponentName);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterComponentTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_ListComponents>());
		R.Register(MakeShared<FTool_AddComponent>());
		R.Register(MakeShared<FTool_RemoveComponent>());
	}
}
