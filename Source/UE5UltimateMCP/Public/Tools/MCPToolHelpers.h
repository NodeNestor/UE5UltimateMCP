// Shared helper utilities used by all MCP tool handler files.
// Provides Blueprint loading, node lookup, serialization, saving, and type resolution.
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Engine/LevelScriptBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "Misc/PackageName.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_EditablePinBase.h"
#include "K2Node_BreakStruct.h"
#include "K2Node_MakeStruct.h"

// Snapshot data structures
struct FPinConnectionRecord
{
	FString SourceNodeGuid;
	FString SourcePinName;
	FString TargetNodeGuid;
	FString TargetPinName;
};

struct FNodeRecord
{
	FString NodeGuid;
	FString NodeClass;
	FString NodeTitle;
	FString StructType;
};

struct FGraphSnapshotData
{
	TArray<FNodeRecord> Nodes;
	TArray<FPinConnectionRecord> Connections;
};

struct FGraphSnapshot
{
	FString SnapshotId;
	FString BlueprintName;
	FString BlueprintPath;
	FDateTime CreatedAt;
	TMap<FString, FGraphSnapshotData> Graphs;
};

/**
 * Static helper namespace used by all MCP tool handlers.
 * These replace the member functions from the reference BlueprintMCPServer class.
 */
namespace MCPHelpers
{
	// ---- Asset lookup ----

	inline FAssetData* FindBlueprintAsset(const FString& NameOrPath)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		static TArray<FAssetData> CachedAssets;
		CachedAssets.Empty();
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), CachedAssets, true);

		for (FAssetData& Asset : CachedAssets)
		{
			if (Asset.AssetName.ToString() == NameOrPath ||
				Asset.PackageName.ToString() == NameOrPath ||
				Asset.GetSoftObjectPath().ToString() == NameOrPath)
			{
				return &Asset;
			}
		}
		return nullptr;
	}

	inline UBlueprint* LoadBlueprintByName(const FString& NameOrPath, FString& OutError)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> AllBP;
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBP, true);

		for (FAssetData& Asset : AllBP)
		{
			if (Asset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase) ||
				Asset.PackageName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase))
			{
				UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
				if (BP) return BP;
			}
		}

		// Try loading as level blueprint from maps
		TArray<FAssetData> AllMaps;
		Registry.GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), AllMaps, false);
		for (FAssetData& MapAsset : AllMaps)
		{
			if (MapAsset.AssetName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase) ||
				MapAsset.PackageName.ToString().Equals(NameOrPath, ESearchCase::IgnoreCase))
			{
				UWorld* World = Cast<UWorld>(MapAsset.GetAsset());
				if (World && World->PersistentLevel)
				{
					ULevelScriptBlueprint* LevelBP = World->PersistentLevel->GetLevelScriptBlueprint(true);
					if (LevelBP) return LevelBP;
				}
			}
		}

		// Try direct load
		UObject* Loaded = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *NameOrPath);
		if (UBlueprint* BP = Cast<UBlueprint>(Loaded))
		{
			return BP;
		}

		OutError = FString::Printf(TEXT("Blueprint '%s' not found. Use list_blueprints to see available assets."), *NameOrPath);
		return nullptr;
	}

	// ---- Node lookup ----

	inline UEdGraphNode* FindNodeByGuid(UBlueprint* BP, const FString& GuidString, UEdGraph** OutGraph = nullptr)
	{
		if (!BP) return nullptr;

		FGuid SearchGuid;
		FGuid::Parse(GuidString, SearchGuid);

		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->NodeGuid == SearchGuid)
				{
					if (OutGraph) *OutGraph = Graph;
					return Node;
				}
			}
		}
		return nullptr;
	}

	// ---- Serialization ----

	inline TSharedPtr<FJsonObject> SerializePin(UEdGraphPin* Pin)
	{
		if (!Pin) return nullptr;
		TSharedRef<FJsonObject> PinJson = MakeShared<FJsonObject>();
		PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
		PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
		PinJson->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
		if (Pin->PinType.PinSubCategoryObject.IsValid())
			PinJson->SetStringField(TEXT("subtype"), Pin->PinType.PinSubCategoryObject->GetName());
		if (!Pin->DefaultValue.IsEmpty())
			PinJson->SetStringField(TEXT("defaultValue"), Pin->DefaultValue);
		PinJson->SetBoolField(TEXT("isArray"), Pin->PinType.IsArray());
		PinJson->SetBoolField(TEXT("hidden"), Pin->bHidden);

		if (Pin->LinkedTo.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Links;
			for (UEdGraphPin* Linked : Pin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				TSharedRef<FJsonObject> LinkJson = MakeShared<FJsonObject>();
				LinkJson->SetStringField(TEXT("nodeId"), Linked->GetOwningNode()->NodeGuid.ToString());
				LinkJson->SetStringField(TEXT("pinName"), Linked->PinName.ToString());
				Links.Add(MakeShared<FJsonValueObject>(LinkJson));
			}
			PinJson->SetArrayField(TEXT("linkedTo"), Links);
		}
		return PinJson;
	}

	inline TSharedPtr<FJsonObject> SerializeNode(UEdGraphNode* Node)
	{
		if (!Node) return nullptr;
		TSharedRef<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		NodeJson->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
		NodeJson->SetStringField(TEXT("nodeClass"), Node->GetClass()->GetName());
		NodeJson->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeJson->SetNumberField(TEXT("posX"), Node->NodePosX);
		NodeJson->SetNumberField(TEXT("posY"), Node->NodePosY);

		if (!Node->NodeComment.IsEmpty())
			NodeJson->SetStringField(TEXT("comment"), Node->NodeComment);

		TArray<TSharedPtr<FJsonValue>> PinsArr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->bHidden) continue;
			TSharedPtr<FJsonObject> PinJson = SerializePin(Pin);
			if (PinJson.IsValid())
				PinsArr.Add(MakeShared<FJsonValueObject>(PinJson.ToSharedRef()));
		}
		NodeJson->SetArrayField(TEXT("pins"), PinsArr);
		return NodeJson;
	}

	inline TSharedPtr<FJsonObject> SerializeGraph(UEdGraph* Graph)
	{
		if (!Graph) return nullptr;
		TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
		GraphJson->SetStringField(TEXT("name"), Graph->GetName());
		GraphJson->SetStringField(TEXT("graphClass"), Graph->GetClass()->GetName());
		GraphJson->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());

		TArray<TSharedPtr<FJsonValue>> NodesArr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			TSharedPtr<FJsonObject> NodeJson = SerializeNode(Node);
			if (NodeJson.IsValid())
				NodesArr.Add(MakeShared<FJsonValueObject>(NodeJson.ToSharedRef()));
		}
		GraphJson->SetArrayField(TEXT("nodes"), NodesArr);
		return GraphJson;
	}

	inline TSharedRef<FJsonObject> SerializeBlueprint(UBlueprint* BP)
	{
		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		if (!BP) return Result;

		Result->SetStringField(TEXT("name"), BP->GetName());
		Result->SetStringField(TEXT("path"), BP->GetPathName());
		Result->SetStringField(TEXT("parentClass"), BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None"));

		// Variables
		TArray<TSharedPtr<FJsonValue>> VarsArr;
		for (const FBPVariableDescription& Var : BP->NewVariables)
		{
			TSharedRef<FJsonObject> VarJson = MakeShared<FJsonObject>();
			VarJson->SetStringField(TEXT("name"), Var.VarName.ToString());
			VarJson->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
			if (Var.VarType.PinSubCategoryObject.IsValid())
				VarJson->SetStringField(TEXT("subtype"), Var.VarType.PinSubCategoryObject->GetName());
			VarJson->SetBoolField(TEXT("isArray"), Var.VarType.IsArray());
			VarsArr.Add(MakeShared<FJsonValueObject>(VarJson));
		}
		Result->SetArrayField(TEXT("variables"), VarsArr);

		// Graphs
		TArray<TSharedPtr<FJsonValue>> GraphsArr;
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			TSharedRef<FJsonObject> GJ = MakeShared<FJsonObject>();
			GJ->SetStringField(TEXT("name"), Graph->GetName());
			GJ->SetStringField(TEXT("class"), Graph->GetClass()->GetName());
			GJ->SetNumberField(TEXT("nodeCount"), Graph->Nodes.Num());
			GraphsArr.Add(MakeShared<FJsonValueObject>(GJ));
		}
		Result->SetArrayField(TEXT("graphs"), GraphsArr);

		return Result;
	}

	// ---- Save ----

	inline bool SaveBlueprintPackage(UBlueprint* BP)
	{
		if (!BP) return false;

		// Compile first
		FKismetEditorUtilities::CompileBlueprint(BP);

		UPackage* Package = BP->GetPackage();
		if (!Package) return false;

		FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		return UPackage::SavePackage(Package, BP, *PackageFilename, SaveArgs);
	}

	inline bool SaveGenericPackage(UObject* Asset)
	{
		if (!Asset) return false;
		UPackage* Package = Asset->GetPackage();
		if (!Package) return false;

		FString PackageFilename = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		return UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
	}

	// ---- JSON helpers ----

	inline FString JsonToString(TSharedRef<FJsonObject> JsonObj)
	{
		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(JsonObj, Writer);
		return Output;
	}

	// ---- Type resolution ----

	inline bool ResolveTypeFromString(const FString& TypeName, FEdGraphPinType& OutPinType, FString& OutError)
	{
		// Handle colon-separated object reference types: "object:ClassName", "softobject:ClassName", etc.
		FString Category, ObjectName;
		if (TypeName.Contains(TEXT(":")))
		{
			TypeName.Split(TEXT(":"), &Category, &ObjectName);
		}
		else
		{
			ObjectName = TypeName;
		}

		// Primitives
		if (ObjectName.Equals(TEXT("bool"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("boolean"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			return true;
		}
		if (ObjectName.Equals(TEXT("byte"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("uint8"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			return true;
		}
		if (ObjectName.Equals(TEXT("int"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("int32"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("integer"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			return true;
		}
		if (ObjectName.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
			return true;
		}
		if (ObjectName.Equals(TEXT("float"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("double"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("real"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		}
		if (ObjectName.Equals(TEXT("string"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("FString"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
			return true;
		}
		if (ObjectName.Equals(TEXT("name"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("FName"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			return true;
		}
		if (ObjectName.Equals(TEXT("text"), ESearchCase::IgnoreCase) || ObjectName.Equals(TEXT("FText"), ESearchCase::IgnoreCase))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
			return true;
		}

		// Object reference categories
		if (!Category.IsEmpty())
		{
			UClass* FoundClass = nullptr;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->GetName().Equals(ObjectName, ESearchCase::IgnoreCase))
				{
					FoundClass = *It;
					break;
				}
			}
			if (!FoundClass)
			{
				OutError = FString::Printf(TEXT("Class '%s' not found for type category '%s'"), *ObjectName, *Category);
				return false;
			}

			if (Category.Equals(TEXT("object"), ESearchCase::IgnoreCase))
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			else if (Category.Equals(TEXT("softobject"), ESearchCase::IgnoreCase))
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
			else if (Category.Equals(TEXT("class"), ESearchCase::IgnoreCase))
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
			else if (Category.Equals(TEXT("softclass"), ESearchCase::IgnoreCase))
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
			else if (Category.Equals(TEXT("interface"), ESearchCase::IgnoreCase))
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Interface;
			else
			{
				OutError = FString::Printf(TEXT("Unknown type category '%s'"), *Category);
				return false;
			}
			OutPinType.PinSubCategoryObject = FoundClass;
			return true;
		}

		// Try struct
		FString StructSearchName = ObjectName;
		if (StructSearchName.StartsWith(TEXT("F")))
			StructSearchName = StructSearchName.Mid(1);

		UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*StructSearchName);
		if (!FoundStruct)
			FoundStruct = FindFirstObject<UScriptStruct>(*ObjectName);
		if (!FoundStruct)
		{
			for (TObjectIterator<UScriptStruct> It; It; ++It)
			{
				if (It->GetName().Equals(StructSearchName, ESearchCase::IgnoreCase) ||
					It->GetName().Equals(ObjectName, ESearchCase::IgnoreCase))
				{
					FoundStruct = *It;
					break;
				}
			}
		}
		if (FoundStruct)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = FoundStruct;
			return true;
		}

		// Try enum
		FString EnumSearchName = ObjectName;
		if (EnumSearchName.StartsWith(TEXT("E")))
			EnumSearchName = EnumSearchName.Mid(1);

		UEnum* FoundEnum = FindFirstObject<UEnum>(*EnumSearchName);
		if (!FoundEnum)
			FoundEnum = FindFirstObject<UEnum>(*ObjectName);
		if (FoundEnum)
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			OutPinType.PinSubCategoryObject = FoundEnum;
			return true;
		}

		// Try class (object reference)
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(ObjectName, ESearchCase::IgnoreCase))
			{
				OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
				OutPinType.PinSubCategoryObject = *It;
				return true;
			}
		}

		OutError = FString::Printf(TEXT("Could not resolve type '%s'. Try: bool, int, float, string, name, text, byte, int64, or a struct/enum/class name."), *TypeName);
		return false;
	}

	// ---- URL decode ----

	inline FString UrlDecode(const FString& Encoded)
	{
		FString Decoded = Encoded;
		Decoded.ReplaceInline(TEXT("+"), TEXT(" "));
		// Simple %XX decode
		FString Result;
		for (int32 i = 0; i < Decoded.Len(); ++i)
		{
			if (Decoded[i] == '%' && i + 2 < Decoded.Len())
			{
				FString Hex = Decoded.Mid(i + 1, 2);
				int32 Val = FParse::HexNumber(*Hex);
				Result += (TCHAR)Val;
				i += 2;
			}
			else
			{
				Result += Decoded[i];
			}
		}
		return Result;
	}

	// ---- Snapshot helpers ----

	inline FString GenerateSnapshotId(const FString& Name)
	{
		FString CleanName = Name;
		CleanName.ReplaceInline(TEXT("/"), TEXT("_"));
		CleanName.ReplaceInline(TEXT(" "), TEXT("_"));
		FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
		return FString::Printf(TEXT("%s_%s_%s"), *CleanName, *Timestamp, *FGuid::NewGuid().ToString().Left(8));
	}

	inline FGraphSnapshotData CaptureGraphSnapshot(UEdGraph* Graph)
	{
		FGraphSnapshotData Data;
		if (!Graph) return Data;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			FNodeRecord Record;
			Record.NodeGuid = Node->NodeGuid.ToString();
			Record.NodeClass = Node->GetClass()->GetName();
			Record.NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
			if (UK2Node_BreakStruct* BreakNode = Cast<UK2Node_BreakStruct>(Node))
				Record.StructType = BreakNode->StructType ? BreakNode->StructType->GetName() : TEXT("");
			else if (UK2Node_MakeStruct* MakeNode = Cast<UK2Node_MakeStruct>(Node))
				Record.StructType = MakeNode->StructType ? MakeNode->StructType->GetName() : TEXT("");
			Data.Nodes.Add(Record);

			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked || !Linked->GetOwningNode()) continue;
					FPinConnectionRecord ConnRecord;
					ConnRecord.SourceNodeGuid = Node->NodeGuid.ToString();
					ConnRecord.SourcePinName = Pin->PinName.ToString();
					ConnRecord.TargetNodeGuid = Linked->GetOwningNode()->NodeGuid.ToString();
					ConnRecord.TargetPinName = Linked->PinName.ToString();
					Data.Connections.Add(ConnRecord);
				}
			}
		}
		return Data;
	}

	// Global snapshot storage
	inline TMap<FString, FGraphSnapshot>& GetSnapshots()
	{
		static TMap<FString, FGraphSnapshot> Snapshots;
		return Snapshots;
	}

	inline TMap<FString, FGraphSnapshot>& GetMaterialSnapshots()
	{
		static TMap<FString, FGraphSnapshot> MaterialSnapshots;
		return MaterialSnapshots;
	}

	static const int32 MaxSnapshots = 50;
}
