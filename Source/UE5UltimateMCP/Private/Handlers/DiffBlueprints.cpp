// DiffBlueprints Tools — structural diff between two Blueprints
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"

// ============================================================
// diff_blueprints
// ============================================================
class FTool_DiffBlueprints : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("diff_blueprints");
		Info.Description = TEXT("Structural diff between two Blueprints — compares graphs, nodes, connections, and variables.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("blueprintA"), TEXT("string"), TEXT("First Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("blueprintB"), TEXT("string"), TEXT("Second Blueprint name or path"), true});
		Info.Parameters.Add({TEXT("graph"), TEXT("string"), TEXT("Filter to a specific graph name"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintA = Params->GetStringField(TEXT("blueprintA"));
		FString BlueprintB = Params->GetStringField(TEXT("blueprintB"));
		FString GraphFilter;
		Params->TryGetStringField(TEXT("graph"), GraphFilter);

		if (BlueprintA.IsEmpty() || BlueprintB.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: blueprintA, blueprintB"));

		FString LoadErrorA, LoadErrorB;
		UBlueprint* BPA = MCPHelpers::LoadBlueprintByName(BlueprintA, LoadErrorA);
		if (!BPA) return FMCPToolResult::Error(FString::Printf(TEXT("blueprintA: %s"), *LoadErrorA));

		UBlueprint* BPB = MCPHelpers::LoadBlueprintByName(BlueprintB, LoadErrorB);
		if (!BPB) return FMCPToolResult::Error(FString::Printf(TEXT("blueprintB: %s"), *LoadErrorB));

		// Gather graphs
		auto GatherGraphs = [&GraphFilter](UBlueprint* BP) -> TArray<UEdGraph*>
		{
			TArray<UEdGraph*> Graphs;
			for (UEdGraph* G : BP->UbergraphPages)
			{
				if (!G) continue;
				if (!GraphFilter.IsEmpty() && G->GetName() != GraphFilter) continue;
				Graphs.Add(G);
			}
			for (UEdGraph* G : BP->FunctionGraphs)
			{
				if (!G) continue;
				if (!GraphFilter.IsEmpty() && G->GetName() != GraphFilter) continue;
				Graphs.Add(G);
			}
			return Graphs;
		};

		TArray<UEdGraph*> GraphsA = GatherGraphs(BPA);
		TArray<UEdGraph*> GraphsB = GatherGraphs(BPB);

		TMap<FString, UEdGraph*> GraphMapA, GraphMapB;
		for (UEdGraph* G : GraphsA) GraphMapA.Add(G->GetName(), G);
		for (UEdGraph* G : GraphsB) GraphMapB.Add(G->GetName(), G);

		TSet<FString> AllGraphNames;
		for (auto& Pair : GraphMapA) AllGraphNames.Add(Pair.Key);
		for (auto& Pair : GraphMapB) AllGraphNames.Add(Pair.Key);

		TArray<TSharedPtr<FJsonValue>> GraphDiffs;

		for (const FString& GraphName : AllGraphNames)
		{
			UEdGraph** pGA = GraphMapA.Find(GraphName);
			UEdGraph** pGB = GraphMapB.Find(GraphName);

			TSharedRef<FJsonObject> GD = MakeShared<FJsonObject>();
			GD->SetStringField(TEXT("graph"), GraphName);

			if (!pGA)
			{
				GD->SetStringField(TEXT("status"), TEXT("onlyInB"));
				GD->SetNumberField(TEXT("nodeCountB"), (*pGB)->Nodes.Num());
				GraphDiffs.Add(MakeShared<FJsonValueObject>(GD));
				continue;
			}
			if (!pGB)
			{
				GD->SetStringField(TEXT("status"), TEXT("onlyInA"));
				GD->SetNumberField(TEXT("nodeCountA"), (*pGA)->Nodes.Num());
				GraphDiffs.Add(MakeShared<FJsonValueObject>(GD));
				continue;
			}

			UEdGraph* GA = *pGA;
			UEdGraph* GB = *pGB;

			// Build node title maps
			TMap<FString, TArray<UEdGraphNode*>> NodesA, NodesB;
			for (UEdGraphNode* N : GA->Nodes)
			{
				if (!N) continue;
				FString Title = N->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				NodesA.FindOrAdd(Title).Add(N);
			}
			for (UEdGraphNode* N : GB->Nodes)
			{
				if (!N) continue;
				FString Title = N->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				NodesB.FindOrAdd(Title).Add(N);
			}

			TArray<TSharedPtr<FJsonValue>> OnlyInA, OnlyInB;

			for (auto& Pair : NodesA)
			{
				int32 CountA = Pair.Value.Num();
				int32 CountB = 0;
				if (TArray<UEdGraphNode*>* pArr = NodesB.Find(Pair.Key))
					CountB = pArr->Num();
				if (CountA > CountB)
				{
					TSharedRef<FJsonObject> NObj = MakeShared<FJsonObject>();
					NObj->SetStringField(TEXT("title"), Pair.Key);
					NObj->SetStringField(TEXT("class"), Pair.Value[0]->GetClass()->GetName());
					NObj->SetNumberField(TEXT("extraCount"), CountA - CountB);
					OnlyInA.Add(MakeShared<FJsonValueObject>(NObj));
				}
			}

			for (auto& Pair : NodesB)
			{
				int32 CountB = Pair.Value.Num();
				int32 CountA = 0;
				if (TArray<UEdGraphNode*>* pArr = NodesA.Find(Pair.Key))
					CountA = pArr->Num();
				if (CountB > CountA)
				{
					TSharedRef<FJsonObject> NObj = MakeShared<FJsonObject>();
					NObj->SetStringField(TEXT("title"), Pair.Key);
					NObj->SetStringField(TEXT("class"), Pair.Value[0]->GetClass()->GetName());
					NObj->SetNumberField(TEXT("extraCount"), CountB - CountA);
					OnlyInB.Add(MakeShared<FJsonValueObject>(NObj));
				}
			}

			// Connection diff
			auto MakeConnKey = [](UEdGraphPin* SrcPin, UEdGraphPin* TgtPin) -> FString
			{
				FString SrcTitle = SrcPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				FString TgtTitle = TgtPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				return FString::Printf(TEXT("%s|%s|%s|%s"), *SrcTitle, *SrcPin->PinName.ToString(), *TgtTitle, *TgtPin->PinName.ToString());
			};

			TSet<FString> ConnectionsA, ConnectionsB;
			for (UEdGraphNode* N : GA->Nodes)
			{
				if (!N) continue;
				for (UEdGraphPin* Pin : N->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output) continue;
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (!Linked || !Linked->GetOwningNode()) continue;
						ConnectionsA.Add(MakeConnKey(Pin, Linked));
					}
				}
			}
			for (UEdGraphNode* N : GB->Nodes)
			{
				if (!N) continue;
				for (UEdGraphPin* Pin : N->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output) continue;
					for (UEdGraphPin* Linked : Pin->LinkedTo)
					{
						if (!Linked || !Linked->GetOwningNode()) continue;
						ConnectionsB.Add(MakeConnKey(Pin, Linked));
					}
				}
			}

			TArray<TSharedPtr<FJsonValue>> ConnsOnlyInA, ConnsOnlyInB;
			for (const FString& Key : ConnectionsA)
			{
				if (!ConnectionsB.Contains(Key))
					ConnsOnlyInA.Add(MakeShared<FJsonValueString>(Key));
			}
			for (const FString& Key : ConnectionsB)
			{
				if (!ConnectionsA.Contains(Key))
					ConnsOnlyInB.Add(MakeShared<FJsonValueString>(Key));
			}

			bool bIdentical = OnlyInA.Num() == 0 && OnlyInB.Num() == 0 && ConnsOnlyInA.Num() == 0 && ConnsOnlyInB.Num() == 0;
			GD->SetStringField(TEXT("status"), bIdentical ? TEXT("identical") : TEXT("different"));
			GD->SetNumberField(TEXT("nodeCountA"), GA->Nodes.Num());
			GD->SetNumberField(TEXT("nodeCountB"), GB->Nodes.Num());

			if (OnlyInA.Num() > 0) GD->SetArrayField(TEXT("nodesOnlyInA"), OnlyInA);
			if (OnlyInB.Num() > 0) GD->SetArrayField(TEXT("nodesOnlyInB"), OnlyInB);
			if (ConnsOnlyInA.Num() > 0) GD->SetArrayField(TEXT("connectionsOnlyInA"), ConnsOnlyInA);
			if (ConnsOnlyInB.Num() > 0) GD->SetArrayField(TEXT("connectionsOnlyInB"), ConnsOnlyInB);

			GraphDiffs.Add(MakeShared<FJsonValueObject>(GD));
		}

		// Compare variables
		TArray<TSharedPtr<FJsonValue>> VarsOnlyInA, VarsOnlyInB;
		TSet<FString> VarNamesA, VarNamesB;
		for (const FBPVariableDescription& V : BPA->NewVariables) VarNamesA.Add(V.VarName.ToString());
		for (const FBPVariableDescription& V : BPB->NewVariables) VarNamesB.Add(V.VarName.ToString());

		for (const FString& Name : VarNamesA)
		{
			if (!VarNamesB.Contains(Name))
				VarsOnlyInA.Add(MakeShared<FJsonValueString>(Name));
		}
		for (const FString& Name : VarNamesB)
		{
			if (!VarNamesA.Contains(Name))
				VarsOnlyInB.Add(MakeShared<FJsonValueString>(Name));
		}

		// Summary
		int32 TotalDiffs = 0;
		for (auto& GDVal : GraphDiffs)
		{
			auto GDObj = GDVal->AsObject();
			FString Status = GDObj->GetStringField(TEXT("status"));
			if (Status != TEXT("identical")) TotalDiffs++;
		}
		TotalDiffs += VarsOnlyInA.Num() + VarsOnlyInB.Num();

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("blueprintA"), BlueprintA);
		Result->SetStringField(TEXT("blueprintB"), BlueprintB);
		Result->SetArrayField(TEXT("graphs"), GraphDiffs);
		if (VarsOnlyInA.Num() > 0) Result->SetArrayField(TEXT("variablesOnlyInA"), VarsOnlyInA);
		if (VarsOnlyInB.Num() > 0) Result->SetArrayField(TEXT("variablesOnlyInB"), VarsOnlyInB);
		Result->SetNumberField(TEXT("totalDifferences"), TotalDiffs);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterDiffTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_DiffBlueprints>());
	}
}
