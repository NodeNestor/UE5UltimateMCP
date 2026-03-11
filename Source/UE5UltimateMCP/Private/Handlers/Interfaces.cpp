// Interface Tools — list, add, remove Blueprint interfaces
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UObjectIterator.h"

// ============================================================
// list_interfaces
// ============================================================
class FTool_ListInterfaces : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_interfaces");
		Info.Description = TEXT("List all interfaces implemented by a Blueprint.");
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

		TArray<TSharedPtr<FJsonValue>> InterfacesArr;
		for (const FBPInterfaceDescription& IfaceDesc : BP->ImplementedInterfaces)
		{
			if (!IfaceDesc.Interface) continue;

			TSharedRef<FJsonObject> IfaceObj = MakeShared<FJsonObject>();
			IfaceObj->SetStringField(TEXT("name"), IfaceDesc.Interface->GetName());
			IfaceObj->SetStringField(TEXT("classPath"), IfaceDesc.Interface->GetPathName());

			TArray<TSharedPtr<FJsonValue>> FuncArr;
			for (const UEdGraph* Graph : IfaceDesc.Graphs)
			{
				if (Graph) FuncArr.Add(MakeShared<FJsonValueString>(Graph->GetName()));
			}
			IfaceObj->SetArrayField(TEXT("functions"), FuncArr);
			InterfacesArr.Add(MakeShared<FJsonValueObject>(IfaceObj));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetNumberField(TEXT("count"), InterfacesArr.Num());
		Result->SetArrayField(TEXT("interfaces"), InterfacesArr);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_interface
// ============================================================
class FTool_AddInterface : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_interface");
		Info.Description = TEXT("Add a Blueprint Interface implementation to a Blueprint.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("interfaceName"), TEXT("string"), TEXT("Interface class or asset name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString InterfaceName = Params->GetStringField(TEXT("interfaceName"));

		if (BlueprintName.IsEmpty() || InterfaceName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, interfaceName"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		// Resolve interface class
		UClass* InterfaceClass = nullptr;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (!It->IsChildOf(UInterface::StaticClass())) continue;
			FString ClassName = It->GetName();
			if (ClassName.Equals(InterfaceName, ESearchCase::IgnoreCase))
			{
				InterfaceClass = *It;
				break;
			}
			FString TrimmedName = ClassName;
			if (TrimmedName.EndsWith(TEXT("_C")))
				TrimmedName = TrimmedName.LeftChop(2);
			if (TrimmedName.Equals(InterfaceName, ESearchCase::IgnoreCase))
			{
				InterfaceClass = *It;
				break;
			}
		}

		if (!InterfaceClass)
		{
			FString IfaceLoadError;
			UBlueprint* IfaceBP = MCPHelpers::LoadBlueprintByName(InterfaceName, IfaceLoadError);
			if (IfaceBP && IfaceBP->GeneratedClass && IfaceBP->GeneratedClass->IsChildOf(UInterface::StaticClass()))
				InterfaceClass = IfaceBP->GeneratedClass;
		}

		if (!InterfaceClass)
			return FMCPToolResult::Error(FString::Printf(TEXT("Interface '%s' not found."), *InterfaceName));

		for (const FBPInterfaceDescription& IfaceDesc : BP->ImplementedInterfaces)
		{
			if (IfaceDesc.Interface == InterfaceClass)
				return FMCPToolResult::Error(FString::Printf(TEXT("Interface '%s' is already implemented"), *InterfaceName));
		}

		FTopLevelAssetPath InterfacePath = InterfaceClass->GetClassPathName();
		bool bAdded = FBlueprintEditorUtils::ImplementNewInterface(BP, InterfacePath);
		if (!bAdded)
			return FMCPToolResult::Error(FString::Printf(TEXT("ImplementNewInterface failed for '%s'"), *InterfaceName));

		TArray<FString> AddedFunctions;
		for (const FBPInterfaceDescription& IfaceDesc : BP->ImplementedInterfaces)
		{
			if (IfaceDesc.Interface == InterfaceClass)
			{
				for (const UEdGraph* Graph : IfaceDesc.Graphs)
				{
					if (Graph) AddedFunctions.Add(Graph->GetName());
				}
				break;
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("interfaceName"), InterfaceClass->GetName());
		Result->SetStringField(TEXT("interfacePath"), InterfaceClass->GetPathName());

		TArray<TSharedPtr<FJsonValue>> FuncArr;
		for (const FString& FuncName : AddedFunctions)
			FuncArr.Add(MakeShared<FJsonValueString>(FuncName));
		Result->SetArrayField(TEXT("functionGraphsAdded"), FuncArr);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// remove_interface
// ============================================================
class FTool_RemoveInterface : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("remove_interface");
		Info.Description = TEXT("Remove a Blueprint Interface implementation from a Blueprint.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("interfaceName"), TEXT("string"), TEXT("Interface to remove"), true});
		Info.Parameters.Add({TEXT("preserveFunctions"), TEXT("boolean"), TEXT("Keep function graph stubs"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString InterfaceName = Params->GetStringField(TEXT("interfaceName"));

		if (BlueprintName.IsEmpty() || InterfaceName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, interfaceName"));

		bool bPreserveFunctions = false;
		Params->TryGetBoolField(TEXT("preserveFunctions"), bPreserveFunctions);

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		UClass* FoundInterface = nullptr;
		for (const FBPInterfaceDescription& IfaceDesc : BP->ImplementedInterfaces)
		{
			if (!IfaceDesc.Interface) continue;
			FString ClassName = IfaceDesc.Interface->GetName();
			if (ClassName.Equals(InterfaceName, ESearchCase::IgnoreCase))
			{
				FoundInterface = IfaceDesc.Interface;
				break;
			}
			FString TrimmedName = ClassName;
			if (TrimmedName.EndsWith(TEXT("_C")))
				TrimmedName = TrimmedName.LeftChop(2);
			if (TrimmedName.Equals(InterfaceName, ESearchCase::IgnoreCase))
			{
				FoundInterface = IfaceDesc.Interface;
				break;
			}
		}

		if (!FoundInterface)
			return FMCPToolResult::Error(FString::Printf(TEXT("Interface '%s' is not implemented by Blueprint '%s'"), *InterfaceName, *BlueprintName));

		FTopLevelAssetPath InterfacePath = FoundInterface->GetClassPathName();
		FBlueprintEditorUtils::RemoveInterface(BP, InterfacePath, bPreserveFunctions);
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
		bool bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprint"), BlueprintName);
		Result->SetStringField(TEXT("interfaceName"), FoundInterface->GetName());
		Result->SetBoolField(TEXT("preservedFunctions"), bPreserveFunctions);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterInterfaceTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_ListInterfaces>());
		R.Register(MakeShared<FTool_AddInterface>());
		R.Register(MakeShared<FTool_RemoveInterface>());
	}
}
