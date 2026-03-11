// Blueprint Read Tools — list, get, search, describe blueprints (read-only)
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_FunctionEntry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// ============================================================
// list_blueprints
// ============================================================
class FTool_ListBlueprints : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_blueprints");
		Info.Description = TEXT("List all Blueprint assets in the project, optionally filtered by name, parent class, or type.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Name/path substring filter"), false});
		Info.Parameters.Add({TEXT("parentClass"), TEXT("string"), TEXT("Filter by parent class name"), false});
		Info.Parameters.Add({TEXT("type"), TEXT("string"), TEXT("all, regular, or level"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Filter = Params->GetStringField(TEXT("filter"));
		FString ParentClassFilter = Params->GetStringField(TEXT("parentClass"));
		FString TypeFilter = Params->GetStringField(TEXT("type"));

		bool bIncludeRegular = TypeFilter.IsEmpty() || TypeFilter == TEXT("all") || TypeFilter == TEXT("regular");
		bool bIncludeLevel = TypeFilter.IsEmpty() || TypeFilter == TEXT("all") || TypeFilter == TEXT("level");

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

		TArray<TSharedPtr<FJsonValue>> Entries;
		int32 Total = 0;

		if (bIncludeRegular)
		{
			TArray<FAssetData> AllBP;
			Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBP, true);
			Total += AllBP.Num();

			for (const FAssetData& Asset : AllBP)
			{
				FString Name = Asset.AssetName.ToString();
				FString Path = Asset.PackageName.ToString();

				if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !Path.Contains(Filter, ESearchCase::IgnoreCase))
					continue;

				FString ParentClass;
				Asset.GetTagValue(FName(TEXT("ParentClass")), ParentClass);
				int32 DotIndex;
				if (ParentClass.FindLastChar('.', DotIndex))
					ParentClass = ParentClass.Mid(DotIndex + 1);

				if (!ParentClassFilter.IsEmpty() && !ParentClass.Contains(ParentClassFilter, ESearchCase::IgnoreCase))
					continue;

				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Name);
				Entry->SetStringField(TEXT("path"), Path);
				Entry->SetStringField(TEXT("parentClass"), ParentClass);
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}

		if (bIncludeLevel)
		{
			TArray<FAssetData> AllMaps;
			Registry.GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), AllMaps, false);
			Total += AllMaps.Num();

			for (const FAssetData& Asset : AllMaps)
			{
				FString Name = Asset.AssetName.ToString();
				FString Path = Asset.PackageName.ToString();

				if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !Path.Contains(Filter, ESearchCase::IgnoreCase))
					continue;

				if (!ParentClassFilter.IsEmpty() && !FString(TEXT("LevelScriptActor")).Contains(ParentClassFilter, ESearchCase::IgnoreCase))
					continue;

				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Name);
				Entry->SetStringField(TEXT("path"), Path);
				Entry->SetStringField(TEXT("parentClass"), TEXT("LevelScriptActor"));
				Entry->SetBoolField(TEXT("isLevelBlueprint"), true);
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Entries.Num());
		Result->SetNumberField(TEXT("total"), Total);
		Result->SetArrayField(TEXT("blueprints"), Entries);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// get_blueprint
// ============================================================
class FTool_GetBlueprint : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_blueprint");
		Info.Description = TEXT("Get detailed information about a Blueprint asset including variables, graphs, and parent class.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Blueprint name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'name' parameter"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(Name, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		return FMCPToolResult::Ok(MCPHelpers::SerializeBlueprint(BP));
	}
};

// ============================================================
// get_blueprint_graph
// ============================================================
class FTool_GetBlueprintGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_blueprint_graph");
		Info.Description = TEXT("Get the full node graph of a specific graph in a Blueprint, including all nodes and pin connections.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("Graph name (e.g. EventGraph, MyFunction)"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		FString GraphName = Params->GetStringField(TEXT("graph"));
		if (Name.IsEmpty() || GraphName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing 'name' or 'graph' parameter"));

		FString DecodedGraphName = MCPHelpers::UrlDecode(GraphName);

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(Name, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName().Equals(DecodedGraphName, ESearchCase::IgnoreCase))
			{
				TSharedPtr<FJsonObject> GraphJson = MCPHelpers::SerializeGraph(Graph);
				if (GraphJson.IsValid())
					return FMCPToolResult::Ok(GraphJson);
			}
		}

		TArray<TSharedPtr<FJsonValue>> GraphNames;
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph) GraphNames.Add(MakeShared<FJsonValueString>(Graph->GetName()));
		}
		TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph '%s' not found"), *DecodedGraphName));
		E->SetArrayField(TEXT("availableGraphs"), GraphNames);
		return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *DecodedGraphName));
	}
};

// ============================================================
// search_blueprints
// ============================================================
class FTool_SearchBlueprints : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("search_blueprints");
		Info.Description = TEXT("Search across all Blueprint graphs for nodes matching a query string (function names, events, variables).");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("query"), TEXT("string"), TEXT("Search term"), true});
		Info.Parameters.Add({TEXT("path"), TEXT("string"), TEXT("Path filter"), false});
		Info.Parameters.Add({TEXT("maxResults"), TEXT("number"), TEXT("Max results (default 50)"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Query = Params->GetStringField(TEXT("query"));
		if (Query.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'query' parameter"));

		FString PathFilter = Params->GetStringField(TEXT("path"));
		int32 MaxResults = 50;
		if (Params->HasField(TEXT("maxResults")))
			MaxResults = FMath::Clamp((int32)Params->GetNumberField(TEXT("maxResults")), 1, 200);

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> AllBP;
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBP, true);

		TArray<TSharedPtr<FJsonValue>> Results;

		for (const FAssetData& Asset : AllBP)
		{
			if (Results.Num() >= MaxResults) break;
			FString Path = Asset.PackageName.ToString();
			if (!PathFilter.IsEmpty() && !Path.Contains(PathFilter, ESearchCase::IgnoreCase)) continue;

			UBlueprint* BP = Cast<UBlueprint>(const_cast<FAssetData&>(Asset).GetAsset());
			if (!BP) continue;

			TArray<UEdGraph*> Graphs;
			BP->GetAllGraphs(Graphs);

			for (UEdGraph* Graph : Graphs)
			{
				if (!Graph || Results.Num() >= MaxResults) break;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!Node || Results.Num() >= MaxResults) break;
					FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

					FString FuncName, EventName, VarName;
					if (auto* CF = Cast<UK2Node_CallFunction>(Node))
						FuncName = CF->FunctionReference.GetMemberName().ToString();
					else if (auto* Ev = Cast<UK2Node_Event>(Node))
						EventName = Ev->EventReference.GetMemberName().ToString();
					else if (auto* CE = Cast<UK2Node_CustomEvent>(Node))
						EventName = CE->CustomFunctionName.ToString();
					else if (auto* VG = Cast<UK2Node_VariableGet>(Node))
						VarName = VG->GetVarName().ToString();
					else if (auto* VS = Cast<UK2Node_VariableSet>(Node))
						VarName = VS->GetVarName().ToString();

					bool bMatch = Title.Contains(Query, ESearchCase::IgnoreCase) ||
						(!FuncName.IsEmpty() && FuncName.Contains(Query, ESearchCase::IgnoreCase)) ||
						(!EventName.IsEmpty() && EventName.Contains(Query, ESearchCase::IgnoreCase)) ||
						(!VarName.IsEmpty() && VarName.Contains(Query, ESearchCase::IgnoreCase));

					if (bMatch)
					{
						TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
						R->SetStringField(TEXT("blueprint"), Asset.AssetName.ToString());
						R->SetStringField(TEXT("blueprintPath"), Path);
						R->SetStringField(TEXT("graph"), Graph->GetName());
						R->SetStringField(TEXT("nodeTitle"), Title);
						R->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
						if (!FuncName.IsEmpty()) R->SetStringField(TEXT("functionName"), FuncName);
						if (!EventName.IsEmpty()) R->SetStringField(TEXT("eventName"), EventName);
						if (!VarName.IsEmpty()) R->SetStringField(TEXT("variableName"), VarName);
						Results.Add(MakeShared<FJsonValueObject>(R));
					}
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("query"), Query);
		Result->SetNumberField(TEXT("resultCount"), Results.Num());
		Result->SetArrayField(TEXT("results"), Results);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// get_blueprint_summary — brief overview without full graph data
// ============================================================
class FTool_GetBlueprintSummary : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_blueprint_summary");
		Info.Description = TEXT("Get a brief summary of a Blueprint: parent class, variable count, graph names, and node counts.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Blueprint name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'name' parameter"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(Name, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		return FMCPToolResult::Ok(MCPHelpers::SerializeBlueprint(BP));
	}
};

// ============================================================
// describe_graph — human-readable description of graph flow
// ============================================================
class FTool_DescribeGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("describe_graph");
		Info.Description = TEXT("Get a human-readable description of the execution flow in a Blueprint graph.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Blueprint name"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("Graph name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		FString GraphName = Params->GetStringField(TEXT("graph"));
		if (Name.IsEmpty() || GraphName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing 'name' or 'graph' parameter"));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(Name, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
			{
				TSharedPtr<FJsonObject> GraphJson = MCPHelpers::SerializeGraph(Graph);
				if (GraphJson.IsValid())
					return FMCPToolResult::Ok(GraphJson);
			}
		}
		return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
	}
};

// ============================================================
// find_asset_references
// ============================================================
class FTool_FindAssetReferences : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("find_asset_references");
		Info.Description = TEXT("Find all assets that reference a given asset path, categorized as Blueprint vs other references.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("assetPath"), TEXT("string"), TEXT("Package path of the asset to find references to"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString AssetPath = Params->GetStringField(TEXT("assetPath"));
		if (AssetPath.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'assetPath' parameter"));

		IAssetRegistry& Registry = *IAssetRegistry::Get();
		TArray<FName> Referencers;
		Registry.GetReferencers(FName(*AssetPath), Referencers);

		TArray<FAssetData> AllBP;
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBP, true);
		TSet<FString> BlueprintPackages;
		for (const FAssetData& A : AllBP)
			BlueprintPackages.Add(A.PackageName.ToString());

		TArray<TSharedPtr<FJsonValue>> BPRefs, OtherRefs;
		for (const FName& Ref : Referencers)
		{
			FString RefStr = Ref.ToString();
			if (BlueprintPackages.Contains(RefStr))
				BPRefs.Add(MakeShared<FJsonValueString>(RefStr));
			else
				OtherRefs.Add(MakeShared<FJsonValueString>(RefStr));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetNumberField(TEXT("totalReferencers"), Referencers.Num());
		Result->SetNumberField(TEXT("blueprintReferencerCount"), BPRefs.Num());
		Result->SetArrayField(TEXT("blueprintReferencers"), BPRefs);
		Result->SetNumberField(TEXT("otherReferencerCount"), OtherRefs.Num());
		Result->SetArrayField(TEXT("otherReferencers"), OtherRefs);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// search_by_type
// ============================================================
class FTool_SearchByType : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("search_by_type");
		Info.Description = TEXT("Find all usages of a specific type (struct, enum, class) across all Blueprint variables, parameters, and pin connections.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("typeName"), TEXT("string"), TEXT("Type name to search for"), true});
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Blueprint name/path filter"), false});
		Info.Parameters.Add({TEXT("maxResults"), TEXT("number"), TEXT("Max results (default 200)"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TypeName = Params->GetStringField(TEXT("typeName"));
		if (TypeName.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing 'typeName' parameter"));

		FString FilterStr = Params->GetStringField(TEXT("filter"));
		int32 MaxResults = 200;
		if (Params->HasField(TEXT("maxResults")))
			MaxResults = FMath::Clamp((int32)Params->GetNumberField(TEXT("maxResults")), 1, 500);

		FString TypeNameNoPrefix = TypeName;
		if (TypeNameNoPrefix.StartsWith(TEXT("F")) || TypeNameNoPrefix.StartsWith(TEXT("E")) || TypeNameNoPrefix.StartsWith(TEXT("U")))
			TypeNameNoPrefix = TypeNameNoPrefix.Mid(1);

		auto MatchesType = [&TypeName, &TypeNameNoPrefix](const FString& TestType) -> bool
		{
			return TestType.Equals(TypeName, ESearchCase::IgnoreCase) || TestType.Equals(TypeNameNoPrefix, ESearchCase::IgnoreCase);
		};

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> AllBP;
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBP, true);

		TArray<TSharedPtr<FJsonValue>> Results;

		for (const FAssetData& Asset : AllBP)
		{
			if (Results.Num() >= MaxResults) break;

			FString Path = Asset.PackageName.ToString();
			FString BPName = Asset.AssetName.ToString();
			if (!FilterStr.IsEmpty() && !BPName.Contains(FilterStr, ESearchCase::IgnoreCase) && !Path.Contains(FilterStr, ESearchCase::IgnoreCase))
				continue;

			UBlueprint* BP = Cast<UBlueprint>(const_cast<FAssetData&>(Asset).GetAsset());
			if (!BP) continue;

			// Check variables
			for (const FBPVariableDescription& Var : BP->NewVariables)
			{
				if (Results.Num() >= MaxResults) break;
				FString VarSubtype;
				if (Var.VarType.PinSubCategoryObject.IsValid())
					VarSubtype = Var.VarType.PinSubCategoryObject->GetName();

				if (MatchesType(VarSubtype) || MatchesType(Var.VarType.PinCategory.ToString()))
				{
					TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
					R->SetStringField(TEXT("blueprint"), BPName);
					R->SetStringField(TEXT("blueprintPath"), Path);
					R->SetStringField(TEXT("usage"), TEXT("variable"));
					R->SetStringField(TEXT("location"), Var.VarName.ToString());
					R->SetStringField(TEXT("currentType"), Var.VarType.PinCategory.ToString());
					if (!VarSubtype.IsEmpty()) R->SetStringField(TEXT("currentSubtype"), VarSubtype);
					Results.Add(MakeShared<FJsonValueObject>(R));
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("typeName"), TypeName);
		Result->SetNumberField(TEXT("resultCount"), Results.Num());
		Result->SetArrayField(TEXT("results"), Results);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterBlueprintReadTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_ListBlueprints>());
		R.Register(MakeShared<FTool_GetBlueprint>());
		R.Register(MakeShared<FTool_GetBlueprintGraph>());
		R.Register(MakeShared<FTool_SearchBlueprints>());
		R.Register(MakeShared<FTool_GetBlueprintSummary>());
		R.Register(MakeShared<FTool_DescribeGraph>());
		R.Register(MakeShared<FTool_FindAssetReferences>());
		R.Register(MakeShared<FTool_SearchByType>());
	}
}
