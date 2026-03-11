// Snapshot Tools — snapshot, diff, restore graph state, find disconnected pins, analyze rebuild impact
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectIterator.h"

// ============================================================
// snapshot_graph
// ============================================================
class FTool_SnapshotGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("snapshot_graph");
		Info.Description = TEXT("Capture a snapshot of a Blueprint's graph state (nodes and connections) for later diffing/restoring.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("Specific graph name to snapshot (omit for all)"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		if (BlueprintName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: blueprint"));

		FString GraphFilter;
		Params->TryGetStringField(TEXT("graph"), GraphFilter);

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		FGraphSnapshot Snapshot;
		Snapshot.SnapshotId = MCPHelpers::GenerateSnapshotId(BlueprintName);
		Snapshot.BlueprintName = BP->GetName();
		Snapshot.BlueprintPath = BP->GetPathName();
		Snapshot.CreatedAt = FDateTime::Now();

		TArray<UEdGraph*> GraphsToCapture;
		for (UEdGraph* Graph : BP->UbergraphPages)
		{
			if (!Graph) continue;
			if (!GraphFilter.IsEmpty() && Graph->GetName() != GraphFilter) continue;
			GraphsToCapture.Add(Graph);
		}
		for (UEdGraph* Graph : BP->FunctionGraphs)
		{
			if (!Graph) continue;
			if (!GraphFilter.IsEmpty() && Graph->GetName() != GraphFilter) continue;
			GraphsToCapture.Add(Graph);
		}

		if (GraphsToCapture.Num() == 0 && !GraphFilter.IsEmpty())
			return FMCPToolResult::Error(FString::Printf(TEXT("Graph '%s' not found"), *GraphFilter));

		int32 TotalConnections = 0;
		TArray<TSharedPtr<FJsonValue>> GraphSummaries;

		for (UEdGraph* Graph : GraphsToCapture)
		{
			FGraphSnapshotData GraphData = MCPHelpers::CaptureGraphSnapshot(Graph);

			TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
			Summary->SetStringField(TEXT("name"), Graph->GetName());
			Summary->SetNumberField(TEXT("nodeCount"), GraphData.Nodes.Num());
			Summary->SetNumberField(TEXT("connectionCount"), GraphData.Connections.Num());
			GraphSummaries.Add(MakeShared<FJsonValueObject>(Summary));

			TotalConnections += GraphData.Connections.Num();
			Snapshot.Graphs.Add(Graph->GetName(), MoveTemp(GraphData));
		}

		MCPHelpers::GetSnapshots().Add(Snapshot.SnapshotId, Snapshot);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("snapshotId"), Snapshot.SnapshotId);
		Result->SetStringField(TEXT("blueprint"), BP->GetName());
		Result->SetArrayField(TEXT("graphs"), GraphSummaries);
		Result->SetNumberField(TEXT("totalConnections"), TotalConnections);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// diff_graph
// ============================================================
class FTool_DiffGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("diff_graph");
		Info.Description = TEXT("Compare current Blueprint graph state against a previously captured snapshot.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("snapshotId"), TEXT("string"), TEXT("Snapshot ID to compare against"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("Specific graph to diff"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString SnapshotId = Params->GetStringField(TEXT("snapshotId"));
		if (BlueprintName.IsEmpty() || SnapshotId.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, snapshotId"));

		FString GraphFilter;
		Params->TryGetStringField(TEXT("graph"), GraphFilter);

		FGraphSnapshot* SnapshotPtr = MCPHelpers::GetSnapshots().Find(SnapshotId);
		if (!SnapshotPtr)
			return FMCPToolResult::Error(FString::Printf(TEXT("Snapshot '%s' not found"), *SnapshotId));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		// Build current state
		TMap<FString, FGraphSnapshotData> CurrentGraphs;
		TArray<UEdGraph*> AllGraphs;
		for (UEdGraph* Graph : BP->UbergraphPages) { if (Graph) AllGraphs.Add(Graph); }
		for (UEdGraph* Graph : BP->FunctionGraphs) { if (Graph) AllGraphs.Add(Graph); }
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!GraphFilter.IsEmpty() && Graph->GetName() != GraphFilter) continue;
			CurrentGraphs.Add(Graph->GetName(), MCPHelpers::CaptureGraphSnapshot(Graph));
		}

		auto MakeConnKey = [](const FString& SrcGuid, const FString& SrcPin, const FString& TgtGuid, const FString& TgtPin) -> FString
		{
			return FString::Printf(TEXT("%s|%s|%s|%s"), *SrcGuid, *SrcPin, *TgtGuid, *TgtPin);
		};

		TArray<TSharedPtr<FJsonValue>> SeveredArr, NewConnsArr, MissingNodesArr;

		for (const auto& SnapGraphPair : SnapshotPtr->Graphs)
		{
			const FString& GraphName = SnapGraphPair.Key;
			if (!GraphFilter.IsEmpty() && GraphName != GraphFilter) continue;

			const FGraphSnapshotData& SnapData = SnapGraphPair.Value;
			const FGraphSnapshotData* CurDataPtr = CurrentGraphs.Find(GraphName);

			TMap<FString, const FNodeRecord*> SnapNodeLookup, CurNodeLookup;
			for (const FNodeRecord& NR : SnapData.Nodes) SnapNodeLookup.Add(NR.NodeGuid, &NR);

			TSet<FString> CurrentConnSet, SnapConnSet;
			if (CurDataPtr)
			{
				for (const FNodeRecord& NR : CurDataPtr->Nodes) CurNodeLookup.Add(NR.NodeGuid, &NR);
				for (const FPinConnectionRecord& Conn : CurDataPtr->Connections)
					CurrentConnSet.Add(MakeConnKey(Conn.SourceNodeGuid, Conn.SourcePinName, Conn.TargetNodeGuid, Conn.TargetPinName));
			}
			for (const FPinConnectionRecord& Conn : SnapData.Connections)
				SnapConnSet.Add(MakeConnKey(Conn.SourceNodeGuid, Conn.SourcePinName, Conn.TargetNodeGuid, Conn.TargetPinName));

			// Severed connections
			for (const FPinConnectionRecord& Conn : SnapData.Connections)
			{
				FString Key = MakeConnKey(Conn.SourceNodeGuid, Conn.SourcePinName, Conn.TargetNodeGuid, Conn.TargetPinName);
				if (!CurrentConnSet.Contains(Key))
				{
					TSharedRef<FJsonObject> SJ = MakeShared<FJsonObject>();
					SJ->SetStringField(TEXT("graph"), GraphName);
					SJ->SetStringField(TEXT("sourceNodeGuid"), Conn.SourceNodeGuid);
					SJ->SetStringField(TEXT("sourcePinName"), Conn.SourcePinName);
					SJ->SetStringField(TEXT("targetNodeGuid"), Conn.TargetNodeGuid);
					SJ->SetStringField(TEXT("targetPinName"), Conn.TargetPinName);
					SeveredArr.Add(MakeShared<FJsonValueObject>(SJ));
				}
			}

			// New connections
			if (CurDataPtr)
			{
				for (const FPinConnectionRecord& Conn : CurDataPtr->Connections)
				{
					FString Key = MakeConnKey(Conn.SourceNodeGuid, Conn.SourcePinName, Conn.TargetNodeGuid, Conn.TargetPinName);
					if (!SnapConnSet.Contains(Key))
					{
						TSharedRef<FJsonObject> NJ = MakeShared<FJsonObject>();
						NJ->SetStringField(TEXT("graph"), GraphName);
						NJ->SetStringField(TEXT("sourceNodeGuid"), Conn.SourceNodeGuid);
						NJ->SetStringField(TEXT("sourcePinName"), Conn.SourcePinName);
						NJ->SetStringField(TEXT("targetNodeGuid"), Conn.TargetNodeGuid);
						NJ->SetStringField(TEXT("targetPinName"), Conn.TargetPinName);
						NewConnsArr.Add(MakeShared<FJsonValueObject>(NJ));
					}
				}
			}

			// Missing nodes
			for (const FNodeRecord& SnapNode : SnapData.Nodes)
			{
				if (!CurNodeLookup.Contains(SnapNode.NodeGuid))
				{
					TSharedRef<FJsonObject> MJ = MakeShared<FJsonObject>();
					MJ->SetStringField(TEXT("graph"), GraphName);
					MJ->SetStringField(TEXT("nodeGuid"), SnapNode.NodeGuid);
					MJ->SetStringField(TEXT("nodeClass"), SnapNode.NodeClass);
					MJ->SetStringField(TEXT("nodeTitle"), SnapNode.NodeTitle);
					MissingNodesArr.Add(MakeShared<FJsonValueObject>(MJ));
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetStringField(TEXT("blueprint"), BP->GetName());
		Result->SetStringField(TEXT("snapshotId"), SnapshotId);
		Result->SetArrayField(TEXT("severedConnections"), SeveredArr);
		Result->SetArrayField(TEXT("newConnections"), NewConnsArr);
		Result->SetArrayField(TEXT("missingNodes"), MissingNodesArr);

		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("severedConnections"), SeveredArr.Num());
		Summary->SetNumberField(TEXT("newConnections"), NewConnsArr.Num());
		Summary->SetNumberField(TEXT("missingNodes"), MissingNodesArr.Num());
		Result->SetObjectField(TEXT("summary"), Summary);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// restore_graph
// ============================================================
class FTool_RestoreGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("restore_graph");
		Info.Description = TEXT("Restore severed connections from a snapshot. Reconnects pins that existed in the snapshot but are now disconnected.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("snapshotId"), TEXT("string"), TEXT("Snapshot ID to restore from"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("Specific graph to restore"), false});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Only restore connections for this node"), false});
		Info.Parameters.Add({TEXT("dryRun"), TEXT("boolean"), TEXT("If true, only report what would be restored"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName = Params->GetStringField(TEXT("blueprint"));
		FString SnapshotId = Params->GetStringField(TEXT("snapshotId"));
		if (BlueprintName.IsEmpty() || SnapshotId.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprint, snapshotId"));

		FString GraphFilter, NodeIdFilter;
		Params->TryGetStringField(TEXT("graph"), GraphFilter);
		Params->TryGetStringField(TEXT("nodeId"), NodeIdFilter);
		bool bDryRun = false;
		Params->TryGetBoolField(TEXT("dryRun"), bDryRun);

		FGraphSnapshot* SnapshotPtr = MCPHelpers::GetSnapshots().Find(SnapshotId);
		if (!SnapshotPtr)
			return FMCPToolResult::Error(FString::Printf(TEXT("Snapshot '%s' not found"), *SnapshotId));

		FString LoadError;
		UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BlueprintName, LoadError);
		if (!BP) return FMCPToolResult::Error(LoadError);

		// Build current connection set
		TMap<FString, FGraphSnapshotData> CurrentGraphs;
		TArray<UEdGraph*> AllGraphs;
		for (UEdGraph* Graph : BP->UbergraphPages) { if (Graph) AllGraphs.Add(Graph); }
		for (UEdGraph* Graph : BP->FunctionGraphs) { if (Graph) AllGraphs.Add(Graph); }
		for (UEdGraph* Graph : AllGraphs)
			CurrentGraphs.Add(Graph->GetName(), MCPHelpers::CaptureGraphSnapshot(Graph));

		auto MakeConnKey = [](const FString& SrcGuid, const FString& SrcPin, const FString& TgtGuid, const FString& TgtPin) -> FString
		{
			return FString::Printf(TEXT("%s|%s|%s|%s"), *SrcGuid, *SrcPin, *TgtGuid, *TgtPin);
		};

		int32 Reconnected = 0, Failed = 0;
		TArray<TSharedPtr<FJsonValue>> DetailsArr;

		for (const auto& SnapGraphPair : SnapshotPtr->Graphs)
		{
			const FString& GraphName = SnapGraphPair.Key;
			if (!GraphFilter.IsEmpty() && GraphName != GraphFilter) continue;

			const FGraphSnapshotData& SnapData = SnapGraphPair.Value;
			const FGraphSnapshotData* CurDataPtr = CurrentGraphs.Find(GraphName);

			TSet<FString> CurrentConnSet;
			if (CurDataPtr)
			{
				for (const FPinConnectionRecord& Conn : CurDataPtr->Connections)
					CurrentConnSet.Add(MakeConnKey(Conn.SourceNodeGuid, Conn.SourcePinName, Conn.TargetNodeGuid, Conn.TargetPinName));
			}

			for (const FPinConnectionRecord& Conn : SnapData.Connections)
			{
				FString Key = MakeConnKey(Conn.SourceNodeGuid, Conn.SourcePinName, Conn.TargetNodeGuid, Conn.TargetPinName);
				if (CurrentConnSet.Contains(Key)) continue;

				if (!NodeIdFilter.IsEmpty() && Conn.SourceNodeGuid != NodeIdFilter && Conn.TargetNodeGuid != NodeIdFilter)
					continue;

				TSharedRef<FJsonObject> Detail = MakeShared<FJsonObject>();
				Detail->SetStringField(TEXT("graph"), GraphName);
				Detail->SetStringField(TEXT("sourcePinName"), Conn.SourcePinName);
				Detail->SetStringField(TEXT("targetPinName"), Conn.TargetPinName);

				UEdGraph* SourceGraph = nullptr;
				UEdGraphNode* SourceNode = MCPHelpers::FindNodeByGuid(BP, Conn.SourceNodeGuid, &SourceGraph);
				UEdGraphNode* TargetNode = MCPHelpers::FindNodeByGuid(BP, Conn.TargetNodeGuid);

				if (!SourceNode) { Detail->SetStringField(TEXT("result"), TEXT("failed")); Detail->SetStringField(TEXT("reason"), TEXT("Source node gone")); Failed++; DetailsArr.Add(MakeShared<FJsonValueObject>(Detail)); continue; }
				if (!TargetNode) { Detail->SetStringField(TEXT("result"), TEXT("failed")); Detail->SetStringField(TEXT("reason"), TEXT("Target node gone")); Failed++; DetailsArr.Add(MakeShared<FJsonValueObject>(Detail)); continue; }

				UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*Conn.SourcePinName));
				UEdGraphPin* TargetPin = TargetNode->FindPin(FName(*Conn.TargetPinName));

				if (!SourcePin) { Detail->SetStringField(TEXT("result"), TEXT("failed")); Detail->SetStringField(TEXT("reason"), TEXT("Source pin gone")); Failed++; DetailsArr.Add(MakeShared<FJsonValueObject>(Detail)); continue; }
				if (!TargetPin) { Detail->SetStringField(TEXT("result"), TEXT("failed")); Detail->SetStringField(TEXT("reason"), TEXT("Target pin gone")); Failed++; DetailsArr.Add(MakeShared<FJsonValueObject>(Detail)); continue; }

				if (bDryRun)
				{
					Detail->SetStringField(TEXT("result"), TEXT("would_reconnect"));
					Reconnected++;
				}
				else
				{
					const UEdGraphSchema* Schema = SourceGraph ? SourceGraph->GetSchema() : nullptr;
					if (Schema && Schema->TryCreateConnection(SourcePin, TargetPin))
					{
						Detail->SetStringField(TEXT("result"), TEXT("reconnected"));
						Reconnected++;
					}
					else
					{
						Detail->SetStringField(TEXT("result"), TEXT("failed"));
						Detail->SetStringField(TEXT("reason"), TEXT("TryCreateConnection failed"));
						Failed++;
					}
				}
				DetailsArr.Add(MakeShared<FJsonValueObject>(Detail));
			}
		}

		bool bSaved = false;
		if (!bDryRun && Reconnected > 0)
			bSaved = MCPHelpers::SaveBlueprintPackage(BP);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("ok"));
		Result->SetNumberField(TEXT("reconnected"), Reconnected);
		Result->SetNumberField(TEXT("failed"), Failed);
		Result->SetArrayField(TEXT("details"), DetailsArr);
		Result->SetBoolField(TEXT("saved"), bSaved);
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// find_disconnected_pins
// ============================================================
class FTool_FindDisconnectedPins : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("find_disconnected_pins");
		Info.Description = TEXT("Find Break/Make struct nodes with unresolved types or zero connections, and snapshot-based definite-break detection.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("blueprint"), TEXT("string"), TEXT("Blueprint name"), false});
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Path/name filter for bulk scan"), false});
		Info.Parameters.Add({TEXT("snapshotId"), TEXT("string"), TEXT("Snapshot ID for definite-break detection"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName, PathFilter, SnapshotId;
		Params->TryGetStringField(TEXT("blueprint"), BlueprintName);
		Params->TryGetStringField(TEXT("filter"), PathFilter);
		Params->TryGetStringField(TEXT("snapshotId"), SnapshotId);

		if (BlueprintName.IsEmpty() && PathFilter.IsEmpty() && SnapshotId.IsEmpty())
			return FMCPToolResult::Error(TEXT("Provide at least one of: blueprint, filter, or snapshotId"));

		TArray<FString> BlueprintsToScan;
		if (!BlueprintName.IsEmpty())
		{
			BlueprintsToScan.Add(BlueprintName);
		}
		else if (!SnapshotId.IsEmpty())
		{
			FGraphSnapshot* Snap = MCPHelpers::GetSnapshots().Find(SnapshotId);
			if (Snap) BlueprintsToScan.Add(Snap->BlueprintName);
		}

		TArray<TSharedPtr<FJsonValue>> ResultsArr;
		int32 HighCount = 0, MediumCount = 0;

		for (const FString& BPName : BlueprintsToScan)
		{
			FString LoadError;
			UBlueprint* BP = MCPHelpers::LoadBlueprintByName(BPName, LoadError);
			if (!BP) continue;

			TArray<UEdGraph*> AllGraphs;
			for (UEdGraph* Graph : BP->UbergraphPages) { if (Graph) AllGraphs.Add(Graph); }
			for (UEdGraph* Graph : BP->FunctionGraphs) { if (Graph) AllGraphs.Add(Graph); }

			for (UEdGraph* Graph : AllGraphs)
			{
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!Node) continue;

					UK2Node_BreakStruct* BreakNode = Cast<UK2Node_BreakStruct>(Node);
					UK2Node_MakeStruct* MakeNode = Cast<UK2Node_MakeStruct>(Node);
					if (!BreakNode && !MakeNode) continue;

					FString StructTypeName;
					if (BreakNode)
						StructTypeName = BreakNode->StructType ? BreakNode->StructType->GetName() : TEXT("<unknown>");
					else
						StructTypeName = MakeNode->StructType ? MakeNode->StructType->GetName() : TEXT("<unknown>");

					bool bUnresolved = StructTypeName.Contains(TEXT("unknown")) || StructTypeName == TEXT("None") || StructTypeName.IsEmpty();

					if (bUnresolved)
					{
						TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
						Item->SetStringField(TEXT("blueprint"), BP->GetName());
						Item->SetStringField(TEXT("graph"), Graph->GetName());
						Item->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
						Item->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
						Item->SetStringField(TEXT("structType"), StructTypeName);
						Item->SetStringField(TEXT("confidence"), TEXT("HIGH"));
						Item->SetStringField(TEXT("reason"), TEXT("Unresolved or unknown struct type"));
						ResultsArr.Add(MakeShared<FJsonValueObject>(Item));
						HighCount++;
					}
					else
					{
						bool bHasDataConnection = false;
						for (UEdGraphPin* Pin : Node->Pins)
						{
							if (!Pin || Pin->bHidden || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) continue;
							if (Pin->LinkedTo.Num() > 0) { bHasDataConnection = true; break; }
						}
						if (!bHasDataConnection)
						{
							TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
							Item->SetStringField(TEXT("blueprint"), BP->GetName());
							Item->SetStringField(TEXT("graph"), Graph->GetName());
							Item->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
							Item->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
							Item->SetStringField(TEXT("structType"), StructTypeName);
							Item->SetStringField(TEXT("confidence"), TEXT("MEDIUM"));
							Item->SetStringField(TEXT("reason"), TEXT("Break/Make node with zero data pin connections"));
							ResultsArr.Add(MakeShared<FJsonValueObject>(Item));
							MediumCount++;
						}
					}
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("results"), ResultsArr);
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("high"), HighCount);
		Summary->SetNumberField(TEXT("medium"), MediumCount);
		Summary->SetNumberField(TEXT("total"), ResultsArr.Num());
		Result->SetObjectField(TEXT("summary"), Summary);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// analyze_rebuild_impact
// ============================================================
class FTool_AnalyzeRebuildImpact : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("analyze_rebuild_impact");
		Info.Description = TEXT("Analyze which Blueprints would be affected by rebuilding structs/enums from a given module.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("moduleName"), TEXT("string"), TEXT("Module name to analyze"), true});
		Info.Parameters.Add({TEXT("structNames"), TEXT("array"), TEXT("Optional specific struct/enum names to check"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString ModuleName = Params->GetStringField(TEXT("moduleName"));
		if (ModuleName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: moduleName"));

		TArray<FString> StructNameFilter;
		const TArray<TSharedPtr<FJsonValue>>* StructNamesArr = nullptr;
		if (Params->TryGetArrayField(TEXT("structNames"), StructNamesArr))
		{
			for (const TSharedPtr<FJsonValue>& Val : *StructNamesArr)
			{
				FString Name = Val->AsString();
				if (!Name.IsEmpty()) StructNameFilter.Add(Name);
			}
		}

		// Find types in module
		TSet<FString> TypeNameSet;
		TArray<TSharedPtr<FJsonValue>> TypesFoundArr;

		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			if (!It->GetOutermost()->GetName().Contains(ModuleName)) continue;
			if (StructNameFilter.Num() > 0)
			{
				bool bMatch = false;
				for (const FString& F : StructNameFilter)
				{
					FString Clean = F.StartsWith(TEXT("F")) ? F.Mid(1) : F;
					if (It->GetName() == F || It->GetName() == Clean) { bMatch = true; break; }
				}
				if (!bMatch) continue;
			}
			TypeNameSet.Add(It->GetName());
			TSharedRef<FJsonObject> TJ = MakeShared<FJsonObject>();
			TJ->SetStringField(TEXT("name"), It->GetName());
			TJ->SetStringField(TEXT("kind"), TEXT("struct"));
			TypesFoundArr.Add(MakeShared<FJsonValueObject>(TJ));
		}

		for (TObjectIterator<UEnum> It; It; ++It)
		{
			if (!It->GetOutermost()->GetName().Contains(ModuleName)) continue;
			if (StructNameFilter.Num() > 0)
			{
				bool bMatch = false;
				for (const FString& F : StructNameFilter)
				{
					FString Clean = F.StartsWith(TEXT("E")) ? F.Mid(1) : F;
					if (It->GetName() == F || It->GetName() == Clean) { bMatch = true; break; }
				}
				if (!bMatch) continue;
			}
			TypeNameSet.Add(It->GetName());
			TSharedRef<FJsonObject> TJ = MakeShared<FJsonObject>();
			TJ->SetStringField(TEXT("name"), It->GetName());
			TJ->SetStringField(TEXT("kind"), TEXT("enum"));
			TypesFoundArr.Add(MakeShared<FJsonValueObject>(TJ));
		}

		// Scan blueprints
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> AllBP;
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBP, true);

		TArray<TSharedPtr<FJsonValue>> AffectedArr;
		int32 TotalBreakMake = 0, TotalConnsAtRisk = 0;

		for (const FAssetData& Asset : AllBP)
		{
			UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
			if (!BP) continue;

			int32 BreakNodes = 0, MakeNodes = 0, Variables = 0, ConnsAtRisk = 0;

			TArray<UEdGraph*> AllGraphs;
			BP->GetAllGraphs(AllGraphs);
			for (UEdGraph* Graph : AllGraphs)
			{
				if (!Graph) continue;
				for (UEdGraphNode* Node : Graph->Nodes)
				{
					if (!Node) continue;
					if (UK2Node_BreakStruct* BN = Cast<UK2Node_BreakStruct>(Node))
					{
						if (BN->StructType && TypeNameSet.Contains(BN->StructType->GetName()))
						{
							BreakNodes++;
							for (UEdGraphPin* Pin : Node->Pins)
							{
								if (Pin && !Pin->bHidden && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
									ConnsAtRisk += Pin->LinkedTo.Num();
							}
						}
					}
					else if (UK2Node_MakeStruct* MN = Cast<UK2Node_MakeStruct>(Node))
					{
						if (MN->StructType && TypeNameSet.Contains(MN->StructType->GetName()))
						{
							MakeNodes++;
							for (UEdGraphPin* Pin : Node->Pins)
							{
								if (Pin && !Pin->bHidden && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
									ConnsAtRisk += Pin->LinkedTo.Num();
							}
						}
					}
				}
			}

			for (const FBPVariableDescription& Var : BP->NewVariables)
			{
				if (Var.VarType.PinSubCategoryObject.IsValid() && TypeNameSet.Contains(Var.VarType.PinSubCategoryObject->GetName()))
					Variables++;
			}

			if (BreakNodes > 0 || MakeNodes > 0 || Variables > 0)
			{
				TSharedRef<FJsonObject> BJ = MakeShared<FJsonObject>();
				BJ->SetStringField(TEXT("name"), BP->GetName());
				BJ->SetStringField(TEXT("path"), BP->GetPathName());
				BJ->SetNumberField(TEXT("breakNodes"), BreakNodes);
				BJ->SetNumberField(TEXT("makeNodes"), MakeNodes);
				BJ->SetNumberField(TEXT("variables"), Variables);
				BJ->SetNumberField(TEXT("connectionsAtRisk"), ConnsAtRisk);
				BJ->SetStringField(TEXT("risk"), (BreakNodes > 0 || MakeNodes > 0) ? TEXT("HIGH") : TEXT("MEDIUM"));
				AffectedArr.Add(MakeShared<FJsonValueObject>(BJ));
				TotalBreakMake += BreakNodes + MakeNodes;
				TotalConnsAtRisk += ConnsAtRisk;
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("moduleName"), ModuleName);
		Result->SetArrayField(TEXT("typesFound"), TypesFoundArr);
		Result->SetArrayField(TEXT("affectedBlueprints"), AffectedArr);
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetNumberField(TEXT("totalBlueprints"), AffectedArr.Num());
		Summary->SetNumberField(TEXT("totalBreakMakeNodes"), TotalBreakMake);
		Summary->SetNumberField(TEXT("totalConnectionsAtRisk"), TotalConnsAtRisk);
		Result->SetObjectField(TEXT("summary"), Summary);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterSnapshotTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_SnapshotGraph>());
		R.Register(MakeShared<FTool_DiffGraph>());
		R.Register(MakeShared<FTool_RestoreGraph>());
		R.Register(MakeShared<FTool_FindDisconnectedPins>());
		R.Register(MakeShared<FTool_AnalyzeRebuildImpact>());
	}
}
