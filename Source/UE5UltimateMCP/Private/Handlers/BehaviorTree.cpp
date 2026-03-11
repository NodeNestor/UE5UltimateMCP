// AI Behavior Tree tools for UltimateMCP
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

// ─────────────────────────────────────────────────────────────
// create_behavior_tree
// ─────────────────────────────────────────────────────────────
class FTool_CreateBehaviorTree : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_behavior_tree");
		Info.Description = TEXT("Create a new Behavior Tree asset.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		FMCPParamSchema P;
		P.Name = TEXT("name"); P.Type = TEXT("string");
		P.Description = TEXT("Name for the Behavior Tree asset");
		P.bRequired = true;
		Info.Parameters.Add(P);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("Parameter 'name' is required."));
		}

		FString PackagePath = FString::Printf(TEXT("/Game/AI/BehaviorTrees/%s"), *Name);
		FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package at '%s'."), *PackageName));
		}

		UBehaviorTree* BT = NewObject<UBehaviorTree>(Package, *Name, RF_Public | RF_Standalone);
		if (!BT)
		{
			return FMCPToolResult::Error(TEXT("Failed to create UBehaviorTree."));
		}

		// Create default root node (Selector)
		UBTComposite_Selector* RootSelector = NewObject<UBTComposite_Selector>(BT);
		BT->RootNode = RootSelector;

		FAssetRegistryModule::AssetCreated(BT);
		BT->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		UPackage::SavePackage(Package, BT, *FilePath, SaveArgs);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), PackagePath);
		Result->SetStringField(TEXT("name"), Name);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// create_blackboard
// ─────────────────────────────────────────────────────────────
class FTool_CreateBlackboard : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_blackboard");
		Info.Description = TEXT("Create a new Blackboard Data asset for AI.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		FMCPParamSchema P;
		P.Name = TEXT("name"); P.Type = TEXT("string");
		P.Description = TEXT("Name for the Blackboard asset");
		P.bRequired = true;
		Info.Parameters.Add(P);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("Parameter 'name' is required."));
		}

		FString PackagePath = FString::Printf(TEXT("/Game/AI/Blackboards/%s"), *Name);
		FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package at '%s'."), *PackageName));
		}

		UBlackboardData* BB = NewObject<UBlackboardData>(Package, *Name, RF_Public | RF_Standalone);
		if (!BB)
		{
			return FMCPToolResult::Error(TEXT("Failed to create UBlackboardData."));
		}

		FAssetRegistryModule::AssetCreated(BB);
		BB->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		UPackage::SavePackage(Package, BB, *FilePath, SaveArgs);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), PackagePath);
		Result->SetStringField(TEXT("name"), Name);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_blackboard_key
// ─────────────────────────────────────────────────────────────
class FTool_AddBlackboardKey : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_blackboard_key");
		Info.Description = TEXT("Add a key entry to a Blackboard Data asset.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("blackboard"), TEXT("string"), TEXT("Asset path of the Blackboard Data"), true);
		AddParam(TEXT("key_name"), TEXT("string"), TEXT("Name of the new key"), true);
		AddParam(TEXT("key_type"), TEXT("string"), TEXT("Type: bool, int, float, string, vector, rotator, object, name, class, enum"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BBPath = Params->GetStringField(TEXT("blackboard"));
		FString KeyName = Params->GetStringField(TEXT("key_name"));
		FString KeyType = Params->GetStringField(TEXT("key_type")).ToLower();

		UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BBPath);
		if (!BB)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Blackboard not found at '%s'."), *BBPath));
		}

		// Resolve key type class
		TSubclassOf<UBlackboardKeyType> KeyTypeClass = nullptr;
		if (KeyType == TEXT("bool"))			KeyTypeClass = UBlackboardKeyType_Bool::StaticClass();
		else if (KeyType == TEXT("int"))			KeyTypeClass = UBlackboardKeyType_Int::StaticClass();
		else if (KeyType == TEXT("float"))		KeyTypeClass = UBlackboardKeyType_Float::StaticClass();
		else if (KeyType == TEXT("string"))		KeyTypeClass = UBlackboardKeyType_String::StaticClass();
		else if (KeyType == TEXT("vector"))		KeyTypeClass = UBlackboardKeyType_Vector::StaticClass();
		else if (KeyType == TEXT("rotator"))		KeyTypeClass = UBlackboardKeyType_Rotator::StaticClass();
		else if (KeyType == TEXT("object"))		KeyTypeClass = UBlackboardKeyType_Object::StaticClass();
		else if (KeyType == TEXT("name"))		KeyTypeClass = UBlackboardKeyType_Name::StaticClass();
		else if (KeyType == TEXT("class"))		KeyTypeClass = UBlackboardKeyType_Class::StaticClass();
		else if (KeyType == TEXT("enum"))		KeyTypeClass = UBlackboardKeyType_Enum::StaticClass();
		else
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Unknown key type '%s'. Valid: bool, int, float, string, vector, rotator, object, name, class, enum."), *KeyType));
		}

		// Check if key already exists
		for (const FBlackboardEntry& Entry : BB->Keys)
		{
			if (Entry.EntryName == FName(*KeyName))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Key '%s' already exists in blackboard."), *KeyName));
			}
		}

		FBlackboardEntry NewEntry;
		NewEntry.EntryName = FName(*KeyName);
		NewEntry.KeyType = NewObject<UBlackboardKeyType>(BB, KeyTypeClass);
		BB->Keys.Add(NewEntry);

		BB->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("blackboard"), BBPath);
		Result->SetStringField(TEXT("key_name"), KeyName);
		Result->SetStringField(TEXT("key_type"), KeyType);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_bt_task
// ─────────────────────────────────────────────────────────────
class FTool_AddBTTask : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_bt_task");
		Info.Description = TEXT("Add a task node to a Behavior Tree under a specified parent composite node.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("tree"), TEXT("string"), TEXT("Asset path of the Behavior Tree"), true);
		AddParam(TEXT("task_class"), TEXT("string"), TEXT("Class name of the BTTask (e.g. 'BTTask_MoveTo', 'BTTask_Wait')"), true);
		AddParam(TEXT("parent_index"), TEXT("number"), TEXT("Index of the parent composite node (0 = root). Default: 0"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TreePath = Params->GetStringField(TEXT("tree"));
		FString TaskClassName = Params->GetStringField(TEXT("task_class"));
		int32 ParentIndex = Params->HasField(TEXT("parent_index")) ? (int32)Params->GetNumberField(TEXT("parent_index")) : 0;

		UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
		if (!BT)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Behavior Tree not found at '%s'."), *TreePath));
		}

		// Ensure class name has correct prefix
		FString FullClassName = TaskClassName;
		if (!FullClassName.StartsWith(TEXT("U")))
		{
			FullClassName = TEXT("U") + FullClassName;
		}

		UClass* TaskClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::ExactClass);
		if (!TaskClass)
		{
			// Try without U prefix
			TaskClass = FindFirstObject<UClass>(*TaskClassName, EFindFirstObjectOptions::ExactClass);
		}
		if (!TaskClass || !TaskClass->IsChildOf(UBTTaskNode::StaticClass()))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("BTTask class '%s' not found or is not a UBTTaskNode."), *TaskClassName));
		}

		// Find parent composite node
		UBTCompositeNode* ParentNode = BT->RootNode;
		if (!ParentNode)
		{
			return FMCPToolResult::Error(TEXT("Behavior Tree has no root node."));
		}

		// Navigate to requested parent by index (breadth-first)
		if (ParentIndex > 0)
		{
			TArray<UBTCompositeNode*> Composites;
			TQueue<UBTCompositeNode*> Queue;
			Queue.Enqueue(ParentNode);
			while (!Queue.IsEmpty())
			{
				UBTCompositeNode* Current = nullptr;
				Queue.Dequeue(Current);
				Composites.Add(Current);
				for (int32 i = 0; i < Current->Children.Num(); ++i)
				{
					if (UBTCompositeNode* ChildComposite = Cast<UBTCompositeNode>(Current->Children[i].ChildComposite))
					{
						Queue.Enqueue(ChildComposite);
					}
				}
			}
			if (ParentIndex >= Composites.Num())
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Parent index %d out of range. Tree has %d composite nodes."), ParentIndex, Composites.Num()));
			}
			ParentNode = Composites[ParentIndex];
		}

		UBTTaskNode* NewTask = NewObject<UBTTaskNode>(BT, TaskClass);
		if (!NewTask)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create task of class '%s'."), *TaskClassName));
		}

		FBTCompositeChild NewChild;
		NewChild.ChildTask = NewTask;
		ParentNode->Children.Add(NewChild);

		BT->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("tree"), TreePath);
		Result->SetStringField(TEXT("task_class"), TaskClassName);
		Result->SetNumberField(TEXT("child_index"), ParentNode->Children.Num() - 1);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_bt_decorator
// ─────────────────────────────────────────────────────────────
class FTool_AddBTDecorator : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_bt_decorator");
		Info.Description = TEXT("Add a decorator to a node in a Behavior Tree.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("tree"), TEXT("string"), TEXT("Asset path of the Behavior Tree"), true);
		AddParam(TEXT("decorator_class"), TEXT("string"), TEXT("Class name of the decorator (e.g. 'BTDecorator_Blackboard')"), true);
		AddParam(TEXT("node_index"), TEXT("number"), TEXT("Index of the composite node to attach to (0 = root)"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TreePath = Params->GetStringField(TEXT("tree"));
		FString DecClassName = Params->GetStringField(TEXT("decorator_class"));
		int32 NodeIndex = Params->HasField(TEXT("node_index")) ? (int32)Params->GetNumberField(TEXT("node_index")) : 0;

		UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
		if (!BT || !BT->RootNode)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Behavior Tree not found or has no root at '%s'."), *TreePath));
		}

		FString FullClassName = DecClassName.StartsWith(TEXT("U")) ? DecClassName : TEXT("U") + DecClassName;
		UClass* DecClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::ExactClass);
		if (!DecClass)
		{
			DecClass = FindFirstObject<UClass>(*DecClassName, EFindFirstObjectOptions::ExactClass);
		}
		if (!DecClass || !DecClass->IsChildOf(UBTDecorator::StaticClass()))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Decorator class '%s' not found or invalid."), *DecClassName));
		}

		// Find composite node by index (breadth-first)
		TArray<UBTCompositeNode*> Composites;
		TQueue<UBTCompositeNode*> Queue;
		Queue.Enqueue(BT->RootNode);
		while (!Queue.IsEmpty())
		{
			UBTCompositeNode* Current = nullptr;
			Queue.Dequeue(Current);
			Composites.Add(Current);
			for (int32 i = 0; i < Current->Children.Num(); ++i)
			{
				if (UBTCompositeNode* Child = Cast<UBTCompositeNode>(Current->Children[i].ChildComposite))
				{
					Queue.Enqueue(Child);
				}
			}
		}

		if (NodeIndex >= Composites.Num())
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Node index %d out of range (%d composites)."), NodeIndex, Composites.Num()));
		}

		UBTCompositeNode* TargetNode = Composites[NodeIndex];
		UBTDecorator* NewDec = NewObject<UBTDecorator>(BT, DecClass);
		if (!NewDec)
		{
			return FMCPToolResult::Error(TEXT("Failed to create decorator."));
		}

		// In UE 5.7, decorators live on FBTCompositeChild (parent-child connections),
		// not directly on UBTCompositeNode. Add to the first child's decorator list.
		if (TargetNode->Children.Num() > 0)
		{
			TargetNode->Children[0].Decorators.Add(NewDec);
		}
		else
		{
			return FMCPToolResult::Error(TEXT("Target composite node has no children to attach a decorator to."));
		}
		BT->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("tree"), TreePath);
		Result->SetStringField(TEXT("decorator_class"), DecClassName);
		Result->SetNumberField(TEXT("node_index"), NodeIndex);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_bt_service
// ─────────────────────────────────────────────────────────────
class FTool_AddBTService : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_bt_service");
		Info.Description = TEXT("Add a service to a composite node in a Behavior Tree.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("tree"), TEXT("string"), TEXT("Asset path of the Behavior Tree"), true);
		AddParam(TEXT("service_class"), TEXT("string"), TEXT("Class name of the service (e.g. 'BTService_DefaultFocus')"), true);
		AddParam(TEXT("node_index"), TEXT("number"), TEXT("Index of the composite node to attach to (0 = root)"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TreePath = Params->GetStringField(TEXT("tree"));
		FString SvcClassName = Params->GetStringField(TEXT("service_class"));
		int32 NodeIndex = Params->HasField(TEXT("node_index")) ? (int32)Params->GetNumberField(TEXT("node_index")) : 0;

		UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
		if (!BT || !BT->RootNode)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Behavior Tree not found or has no root at '%s'."), *TreePath));
		}

		FString FullClassName = SvcClassName.StartsWith(TEXT("U")) ? SvcClassName : TEXT("U") + SvcClassName;
		UClass* SvcClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::ExactClass);
		if (!SvcClass)
		{
			SvcClass = FindFirstObject<UClass>(*SvcClassName, EFindFirstObjectOptions::ExactClass);
		}
		if (!SvcClass || !SvcClass->IsChildOf(UBTService::StaticClass()))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Service class '%s' not found or invalid."), *SvcClassName));
		}

		TArray<UBTCompositeNode*> Composites;
		TQueue<UBTCompositeNode*> Queue;
		Queue.Enqueue(BT->RootNode);
		while (!Queue.IsEmpty())
		{
			UBTCompositeNode* Current = nullptr;
			Queue.Dequeue(Current);
			Composites.Add(Current);
			for (int32 i = 0; i < Current->Children.Num(); ++i)
			{
				if (UBTCompositeNode* Child = Cast<UBTCompositeNode>(Current->Children[i].ChildComposite))
				{
					Queue.Enqueue(Child);
				}
			}
		}

		if (NodeIndex >= Composites.Num())
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Node index %d out of range (%d composites)."), NodeIndex, Composites.Num()));
		}

		UBTCompositeNode* TargetNode = Composites[NodeIndex];
		UBTService* NewSvc = NewObject<UBTService>(BT, SvcClass);
		if (!NewSvc)
		{
			return FMCPToolResult::Error(TEXT("Failed to create service."));
		}

		TargetNode->Services.Add(NewSvc);
		BT->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("tree"), TreePath);
		Result->SetStringField(TEXT("service_class"), SvcClassName);
		Result->SetNumberField(TEXT("node_index"), NodeIndex);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_bt_selector
// ─────────────────────────────────────────────────────────────
class FTool_AddBTSelector : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_bt_selector");
		Info.Description = TEXT("Add a Selector composite node to a Behavior Tree.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("tree"), TEXT("string"), TEXT("Asset path of the Behavior Tree"), true);
		AddParam(TEXT("parent_index"), TEXT("number"), TEXT("Index of parent composite (0 = root). Default: 0"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TreePath = Params->GetStringField(TEXT("tree"));
		int32 ParentIndex = Params->HasField(TEXT("parent_index")) ? (int32)Params->GetNumberField(TEXT("parent_index")) : 0;

		UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
		if (!BT || !BT->RootNode)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Behavior Tree not found or has no root at '%s'."), *TreePath));
		}

		TArray<UBTCompositeNode*> Composites;
		TQueue<UBTCompositeNode*> Queue;
		Queue.Enqueue(BT->RootNode);
		while (!Queue.IsEmpty())
		{
			UBTCompositeNode* Current = nullptr;
			Queue.Dequeue(Current);
			Composites.Add(Current);
			for (int32 i = 0; i < Current->Children.Num(); ++i)
			{
				if (UBTCompositeNode* Child = Cast<UBTCompositeNode>(Current->Children[i].ChildComposite))
				{
					Queue.Enqueue(Child);
				}
			}
		}

		if (ParentIndex >= Composites.Num())
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Parent index %d out of range (%d composites)."), ParentIndex, Composites.Num()));
		}

		UBTCompositeNode* ParentNode = Composites[ParentIndex];
		UBTComposite_Selector* NewSelector = NewObject<UBTComposite_Selector>(BT);

		FBTCompositeChild NewChild;
		NewChild.ChildComposite = NewSelector;
		ParentNode->Children.Add(NewChild);

		BT->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("tree"), TreePath);
		Result->SetStringField(TEXT("type"), TEXT("Selector"));
		Result->SetNumberField(TEXT("parent_index"), ParentIndex);
		Result->SetNumberField(TEXT("child_index"), ParentNode->Children.Num() - 1);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_bt_sequence
// ─────────────────────────────────────────────────────────────
class FTool_AddBTSequence : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_bt_sequence");
		Info.Description = TEXT("Add a Sequence composite node to a Behavior Tree.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("tree"), TEXT("string"), TEXT("Asset path of the Behavior Tree"), true);
		AddParam(TEXT("parent_index"), TEXT("number"), TEXT("Index of parent composite (0 = root). Default: 0"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TreePath = Params->GetStringField(TEXT("tree"));
		int32 ParentIndex = Params->HasField(TEXT("parent_index")) ? (int32)Params->GetNumberField(TEXT("parent_index")) : 0;

		UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
		if (!BT || !BT->RootNode)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Behavior Tree not found or has no root at '%s'."), *TreePath));
		}

		TArray<UBTCompositeNode*> Composites;
		TQueue<UBTCompositeNode*> Queue;
		Queue.Enqueue(BT->RootNode);
		while (!Queue.IsEmpty())
		{
			UBTCompositeNode* Current = nullptr;
			Queue.Dequeue(Current);
			Composites.Add(Current);
			for (int32 i = 0; i < Current->Children.Num(); ++i)
			{
				if (UBTCompositeNode* Child = Cast<UBTCompositeNode>(Current->Children[i].ChildComposite))
				{
					Queue.Enqueue(Child);
				}
			}
		}

		if (ParentIndex >= Composites.Num())
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Parent index %d out of range (%d composites)."), ParentIndex, Composites.Num()));
		}

		UBTCompositeNode* ParentNode = Composites[ParentIndex];
		UBTComposite_Sequence* NewSeq = NewObject<UBTComposite_Sequence>(BT);

		FBTCompositeChild NewChild;
		NewChild.ChildComposite = NewSeq;
		ParentNode->Children.Add(NewChild);

		BT->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("tree"), TreePath);
		Result->SetStringField(TEXT("type"), TEXT("Sequence"));
		Result->SetNumberField(TEXT("parent_index"), ParentIndex);
		Result->SetNumberField(TEXT("child_index"), ParentNode->Children.Num() - 1);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// link_blackboard_to_tree
// ─────────────────────────────────────────────────────────────
class FTool_LinkBlackboardToTree : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("link_blackboard_to_tree");
		Info.Description = TEXT("Link a Blackboard Data asset to a Behavior Tree.");
		Info.Annotations.Category = TEXT("AI");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("tree"), TEXT("string"), TEXT("Asset path of the Behavior Tree"), true);
		AddParam(TEXT("blackboard"), TEXT("string"), TEXT("Asset path of the Blackboard Data"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TreePath = Params->GetStringField(TEXT("tree"));
		FString BBPath = Params->GetStringField(TEXT("blackboard"));

		UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *TreePath);
		if (!BT)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Behavior Tree not found at '%s'."), *TreePath));
		}

		UBlackboardData* BB = LoadObject<UBlackboardData>(nullptr, *BBPath);
		if (!BB)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Blackboard not found at '%s'."), *BBPath));
		}

		BT->BlackboardAsset = BB;
		BT->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("tree"), TreePath);
		Result->SetStringField(TEXT("blackboard"), BBPath);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────
namespace UltimateMCPTools
{
	void RegisterBehaviorTreeTools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_CreateBehaviorTree>());
		Registry.Register(MakeShared<FTool_CreateBlackboard>());
		Registry.Register(MakeShared<FTool_AddBlackboardKey>());
		Registry.Register(MakeShared<FTool_AddBTTask>());
		Registry.Register(MakeShared<FTool_AddBTDecorator>());
		Registry.Register(MakeShared<FTool_AddBTService>());
		Registry.Register(MakeShared<FTool_AddBTSelector>());
		Registry.Register(MakeShared<FTool_AddBTSequence>());
		Registry.Register(MakeShared<FTool_LinkBlackboardToTree>());
	}
}
