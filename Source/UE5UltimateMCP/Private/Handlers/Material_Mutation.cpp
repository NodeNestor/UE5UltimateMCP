// Material Mutation Tools — create, modify, connect, snapshot/diff/restore materials
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"
#include "Misc/PackageName.h"

// SEH wrapper for material expression creation
#if PLATFORM_WINDOWS
extern int32 TryAddMaterialExpressionSEH(
	UObject* Owner, UClass* ExprClass, UMaterial* Material, UMaterialFunction* MatFunc,
	int32 PosX, int32 PosY, UMaterialExpression** OutExpr);
#endif

// Local helpers
namespace
{
	UMaterial* LoadMaterialByName(const FString& Name, FString& OutError)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), Assets, false);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString() == Name || Asset.PackageName.ToString() == Name || Asset.GetObjectPathString() == Name)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
				if (Mat) return Mat;
			}
		}
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
				if (Mat) return Mat;
			}
		}
		OutError = FString::Printf(TEXT("Material '%s' not found."), *Name);
		return nullptr;
	}

	UMaterialFunction* LoadMaterialFunctionByName(const FString& Name, FString& OutError)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UMaterialFunction::StaticClass()->GetClassPathName(), Assets, false);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString() == Name || Asset.PackageName.ToString() == Name || Asset.GetObjectPathString() == Name)
			{
				UMaterialFunction* MF = Cast<UMaterialFunction>(Asset.GetAsset());
				if (MF) return MF;
			}
		}
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				UMaterialFunction* MF = Cast<UMaterialFunction>(Asset.GetAsset());
				if (MF) return MF;
			}
		}
		OutError = FString::Printf(TEXT("MaterialFunction '%s' not found."), *Name);
		return nullptr;
	}

	UMaterialInstanceConstant* LoadMaterialInstanceByName(const FString& Name, FString& OutError)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), Assets, false);
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString() == Name || Asset.PackageName.ToString() == Name || Asset.GetObjectPathString() == Name)
			{
				UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
				if (MI) return MI;
			}
		}
		OutError = FString::Printf(TEXT("MaterialInstance '%s' not found."), *Name);
		return nullptr;
	}

	void EnsureMaterialGraph(UMaterial* Material)
	{
		if (!Material->MaterialGraph)
		{
			Material->MaterialGraph = CastChecked<UMaterialGraph>(
				FBlueprintEditorUtils::CreateNewGraph(Material, NAME_None, UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass()));
			Material->MaterialGraph->Material = Material;
			Material->MaterialGraph->RebuildGraph();
		}
	}

	bool SaveMaterialPackage(UObject* Asset)
	{
		UPackage* Package = Asset->GetPackage();
		FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		return UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
	}
}

// ============================================================
// create_material
// ============================================================
class FTool_CreateMaterial : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_material");
		Info.Description = TEXT("Create a new UMaterial asset with optional domain, blend mode, and two-sided settings.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Material name"), true});
		Info.Parameters.Add({TEXT("packagePath"), TEXT("string"), TEXT("Package path (e.g. /Game/Materials)"), true});
		Info.Parameters.Add({TEXT("domain"), TEXT("string"), TEXT("Material domain (Surface, DeferredDecal, etc.)"), false});
		Info.Parameters.Add({TEXT("blendMode"), TEXT("string"), TEXT("Blend mode (Opaque, Masked, Translucent, etc.)"), false});
		Info.Parameters.Add({TEXT("twoSided"), TEXT("boolean"), TEXT("Enable two-sided rendering"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		FString PackagePath = Params->GetStringField(TEXT("packagePath"));
		if (Name.IsEmpty() || PackagePath.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: name, packagePath"));
		if (!PackagePath.StartsWith(TEXT("/Game")))
			return FMCPToolResult::Error(TEXT("packagePath must start with '/Game'"));

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterial::StaticClass(), Factory);
		if (!NewAsset) return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create Material '%s'"), *Name));

		UMaterial* Material = Cast<UMaterial>(NewAsset);
		if (!Material) return FMCPToolResult::Error(TEXT("Created asset is not a UMaterial"));

		FString DomainStr, BlendModeStr;
		Params->TryGetStringField(TEXT("domain"), DomainStr);
		Params->TryGetStringField(TEXT("blendMode"), BlendModeStr);
		bool bTwoSided = false;
		bool bHasTwoSided = Params->TryGetBoolField(TEXT("twoSided"), bTwoSided);

		Material->PreEditChange(nullptr);

		if (!DomainStr.IsEmpty())
		{
			if (DomainStr == TEXT("Surface")) Material->MaterialDomain = MD_Surface;
			else if (DomainStr == TEXT("DeferredDecal")) Material->MaterialDomain = MD_DeferredDecal;
			else if (DomainStr == TEXT("LightFunction")) Material->MaterialDomain = MD_LightFunction;
			else if (DomainStr == TEXT("Volume")) Material->MaterialDomain = MD_Volume;
			else if (DomainStr == TEXT("PostProcess")) Material->MaterialDomain = MD_PostProcess;
			else if (DomainStr == TEXT("UI")) Material->MaterialDomain = MD_UI;
		}
		if (!BlendModeStr.IsEmpty())
		{
			if (BlendModeStr == TEXT("Opaque")) Material->BlendMode = BLEND_Opaque;
			else if (BlendModeStr == TEXT("Masked")) Material->BlendMode = BLEND_Masked;
			else if (BlendModeStr == TEXT("Translucent")) Material->BlendMode = BLEND_Translucent;
			else if (BlendModeStr == TEXT("Additive")) Material->BlendMode = BLEND_Additive;
			else if (BlendModeStr == TEXT("Modulate")) Material->BlendMode = BLEND_Modulate;
		}
		if (bHasTwoSided) Material->TwoSided = bTwoSided ? 1 : 0;

		Material->PostEditChange();
		bool bSaved = SaveMaterialPackage(Material);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("name"), Name);
		Result->SetStringField(TEXT("path"), Material->GetPathName());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// set_material_property
// ============================================================
class FTool_SetMaterialProperty : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_material_property");
		Info.Description = TEXT("Set a top-level material property (domain, blendMode, twoSided, shadingModel, opacity, usage flags).");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name or path"), true});
		Info.Parameters.Add({TEXT("property"), TEXT("string"), TEXT("Property name"), true});
		Info.Parameters.Add({TEXT("value"), TEXT("string"), TEXT("New value (type depends on property)"), true});
		Info.Parameters.Add({TEXT("dryRun"), TEXT("boolean"), TEXT("Preview without applying"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString Property = Params->GetStringField(TEXT("property"));
		if (MaterialName.IsEmpty() || Property.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: material, property"));
		if (!Params->HasField(TEXT("value")))
			return FMCPToolResult::Error(TEXT("Missing required field: value"));

		bool bDryRun = false;
		Params->TryGetBoolField(TEXT("dryRun"), bDryRun);

		FString LoadError;
		UMaterial* Material = LoadMaterialByName(MaterialName, LoadError);
		if (!Material) return FMCPToolResult::Error(LoadError);

		FString OldValue, NewValue;

		if (Property == TEXT("domain"))
		{
			FString ValueStr = Params->GetStringField(TEXT("value"));
			OldValue = StaticEnum<EMaterialDomain>()->GetNameStringByValue((int64)Material->MaterialDomain);
			EMaterialDomain NewDomain = Material->MaterialDomain;
			if (ValueStr == TEXT("Surface")) NewDomain = MD_Surface;
			else if (ValueStr == TEXT("DeferredDecal")) NewDomain = MD_DeferredDecal;
			else if (ValueStr == TEXT("LightFunction")) NewDomain = MD_LightFunction;
			else if (ValueStr == TEXT("Volume")) NewDomain = MD_Volume;
			else if (ValueStr == TEXT("PostProcess")) NewDomain = MD_PostProcess;
			else if (ValueStr == TEXT("UI")) NewDomain = MD_UI;
			else return FMCPToolResult::Error(FString::Printf(TEXT("Invalid domain '%s'"), *ValueStr));
			NewValue = ValueStr;
			if (!bDryRun) { Material->PreEditChange(nullptr); Material->MaterialDomain = NewDomain; Material->PostEditChange(); }
		}
		else if (Property == TEXT("blendMode"))
		{
			FString ValueStr = Params->GetStringField(TEXT("value"));
			OldValue = StaticEnum<EBlendMode>()->GetNameStringByValue((int64)Material->BlendMode);
			EBlendMode NewBlend = Material->BlendMode;
			if (ValueStr == TEXT("Opaque")) NewBlend = BLEND_Opaque;
			else if (ValueStr == TEXT("Masked")) NewBlend = BLEND_Masked;
			else if (ValueStr == TEXT("Translucent")) NewBlend = BLEND_Translucent;
			else if (ValueStr == TEXT("Additive")) NewBlend = BLEND_Additive;
			else if (ValueStr == TEXT("Modulate")) NewBlend = BLEND_Modulate;
			else return FMCPToolResult::Error(FString::Printf(TEXT("Invalid blendMode '%s'"), *ValueStr));
			NewValue = ValueStr;
			if (!bDryRun) { Material->PreEditChange(nullptr); Material->BlendMode = NewBlend; Material->PostEditChange(); }
		}
		else if (Property == TEXT("twoSided"))
		{
			bool bValue = Params->GetBoolField(TEXT("value"));
			OldValue = Material->TwoSided ? TEXT("true") : TEXT("false");
			NewValue = bValue ? TEXT("true") : TEXT("false");
			if (!bDryRun) { Material->PreEditChange(nullptr); Material->TwoSided = bValue ? 1 : 0; Material->PostEditChange(); }
		}
		else if (Property == TEXT("shadingModel"))
		{
			FString ValueStr = Params->GetStringField(TEXT("value"));
			EMaterialShadingModel NewModel = MSM_DefaultLit;
			if (ValueStr == TEXT("Unlit")) NewModel = MSM_Unlit;
			else if (ValueStr == TEXT("DefaultLit")) NewModel = MSM_DefaultLit;
			else if (ValueStr == TEXT("Subsurface")) NewModel = MSM_Subsurface;
			else if (ValueStr == TEXT("ClearCoat")) NewModel = MSM_ClearCoat;
			else if (ValueStr == TEXT("SubsurfaceProfile")) NewModel = MSM_SubsurfaceProfile;
			else if (ValueStr == TEXT("TwoSidedFoliage")) NewModel = MSM_TwoSidedFoliage;
			else if (ValueStr == TEXT("Hair")) NewModel = MSM_Hair;
			else if (ValueStr == TEXT("Cloth")) NewModel = MSM_Cloth;
			else if (ValueStr == TEXT("Eye")) NewModel = MSM_Eye;
			else return FMCPToolResult::Error(FString::Printf(TEXT("Invalid shadingModel '%s'"), *ValueStr));
			OldValue = TEXT("(current)");
			NewValue = ValueStr;
			if (!bDryRun) { Material->PreEditChange(nullptr); Material->SetShadingModel(NewModel); Material->PostEditChange(); }
		}
		else if (Property == TEXT("opacityMaskClipValue"))
		{
			double Val = Params->GetNumberField(TEXT("value"));
			OldValue = FString::Printf(TEXT("%f"), Material->OpacityMaskClipValue);
			NewValue = FString::Printf(TEXT("%f"), Val);
			if (!bDryRun) { Material->PreEditChange(nullptr); Material->OpacityMaskClipValue = (float)Val; Material->PostEditChange(); }
		}
		else
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Unknown property '%s'. Valid: domain, blendMode, twoSided, shadingModel, opacityMaskClipValue"), *Property));
		}

		bool bSaved = false;
		if (!bDryRun) bSaved = SaveMaterialPackage(Material);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("material"), Material->GetName());
		Result->SetStringField(TEXT("property"), Property);
		Result->SetStringField(TEXT("oldValue"), OldValue);
		Result->SetStringField(TEXT("newValue"), NewValue);
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		if (!bDryRun) Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_material_expression
// ============================================================
class FTool_AddMaterialExpression : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_material_expression");
		Info.Description = TEXT("Add a new expression node to a material or material function.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name (or use materialFunction)"), false});
		Info.Parameters.Add({TEXT("materialFunction"), TEXT("string"), TEXT("MaterialFunction name (or use material)"), false});
		Info.Parameters.Add({TEXT("expressionClass"), TEXT("string"), TEXT("Expression class without prefix (e.g. Constant, ScalarParameter, Add)"), true});
		Info.Parameters.Add({TEXT("posX"), TEXT("number"), TEXT("X position in graph"), false});
		Info.Parameters.Add({TEXT("posY"), TEXT("number"), TEXT("Y position in graph"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString MaterialFunctionName = Params->GetStringField(TEXT("materialFunction"));
		FString ExpressionClassName = Params->GetStringField(TEXT("expressionClass"));

		if (MaterialName.IsEmpty() && MaterialFunctionName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: 'material' or 'materialFunction'"));
		if (ExpressionClassName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: expressionClass"));

		int32 PosX = 0, PosY = 0;
		double D = 0;
		if (Params->TryGetNumberField(TEXT("posX"), D)) PosX = (int32)D;
		if (Params->TryGetNumberField(TEXT("posY"), D)) PosY = (int32)D;

		// Resolve expression class
		static TMap<FString, FString> Aliases = {{TEXT("Lerp"), TEXT("LinearInterpolate")}};
		FString LookupName = ExpressionClassName;
		if (const FString* Alias = Aliases.Find(ExpressionClassName)) LookupName = *Alias;

		UClass* ExprClass = nullptr;
		FString FullClassName = FString::Printf(TEXT("MaterialExpression%s"), *LookupName);
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == FullClassName && It->IsChildOf(UMaterialExpression::StaticClass()))
			{
				ExprClass = *It;
				break;
			}
		}
		if (!ExprClass) return FMCPToolResult::Error(FString::Printf(TEXT("Unknown expression class '%s'"), *ExpressionClassName));
		if (ExprClass->HasAnyClassFlags(CLASS_Abstract))
			return FMCPToolResult::Error(FString::Printf(TEXT("Expression class '%s' is abstract"), *ExpressionClassName));

		UMaterial* Material = nullptr;
		UMaterialFunction* MatFunc = nullptr;
		UObject* Owner = nullptr;
		FString AssetDisplayName;

		if (!MaterialFunctionName.IsEmpty())
		{
			FString LoadError;
			MatFunc = LoadMaterialFunctionByName(MaterialFunctionName, LoadError);
			if (!MatFunc) return FMCPToolResult::Error(LoadError);
			Owner = MatFunc;
			AssetDisplayName = MatFunc->GetName();
		}
		else
		{
			FString LoadError;
			Material = LoadMaterialByName(MaterialName, LoadError);
			if (!Material) return FMCPToolResult::Error(LoadError);
			Owner = Material;
			AssetDisplayName = Material->GetName();
		}

		if (Material) EnsureMaterialGraph(Material);

		UMaterialExpression* NewExpr = nullptr;
#if PLATFORM_WINDOWS
		int32 CreateResult = TryAddMaterialExpressionSEH(Owner, ExprClass, Material, MatFunc, PosX, PosY, &NewExpr);
		if (CreateResult != 0 || !NewExpr)
			return FMCPToolResult::Error(FString::Printf(TEXT("Expression class '%s' cannot be instantiated"), *ExpressionClassName));
#else
		NewExpr = NewObject<UMaterialExpression>(Owner, ExprClass);
		if (!NewExpr) return FMCPToolResult::Error(TEXT("Failed to create material expression"));
		NewExpr->MaterialExpressionEditorX = PosX;
		NewExpr->MaterialExpressionEditorY = PosY;
		if (Material)
		{
			Material->GetExpressionCollection().AddExpression(NewExpr);
			if (Material->MaterialGraph) Material->MaterialGraph->RebuildGraph();
			Material->PreEditChange(nullptr);
			Material->PostEditChange();
			Material->MarkPackageDirty();
		}
		else if (MatFunc)
		{
			MatFunc->GetExpressionCollection().AddExpression(NewExpr);
			MatFunc->PreEditChange(nullptr);
			MatFunc->PostEditChange();
			MatFunc->MarkPackageDirty();
		}
#endif

		bool bSaved = SaveMaterialPackage(Material ? (UObject*)Material : (UObject*)MatFunc);

		FString NodeGuid;
		if (Material && Material->MaterialGraph)
		{
			for (UEdGraphNode* Node : Material->MaterialGraph->Nodes)
			{
				UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
				if (MatNode && MatNode->MaterialExpression == NewExpr)
				{
					NodeGuid = Node->NodeGuid.ToString();
					break;
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("material"), AssetDisplayName);
		Result->SetStringField(TEXT("expressionClass"), ExpressionClassName);
		Result->SetStringField(TEXT("nodeId"), NodeGuid);
		Result->SetNumberField(TEXT("posX"), PosX);
		Result->SetNumberField(TEXT("posY"), PosY);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// delete_material_expression
// ============================================================
class FTool_DeleteMaterialExpression : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("delete_material_expression");
		Info.Description = TEXT("Remove an expression node from a material by nodeId.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name (or use materialFunction)"), false});
		Info.Parameters.Add({TEXT("materialFunction"), TEXT("string"), TEXT("MaterialFunction name"), false});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID to delete"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString MaterialFunctionName = Params->GetStringField(TEXT("materialFunction"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));

		if (MaterialName.IsEmpty() && MaterialFunctionName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: 'material' or 'materialFunction'"));
		if (NodeId.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing: nodeId"));

		UMaterial* Material = nullptr;
		UMaterialFunction* MatFunc = nullptr;
		FString AssetDisplayName;

		if (!MaterialFunctionName.IsEmpty())
		{
			FString E; MatFunc = LoadMaterialFunctionByName(MaterialFunctionName, E);
			if (!MatFunc) return FMCPToolResult::Error(E);
			AssetDisplayName = MatFunc->GetName();
		}
		else
		{
			FString E; Material = LoadMaterialByName(MaterialName, E);
			if (!Material) return FMCPToolResult::Error(E);
			AssetDisplayName = Material->GetName();
		}

		if (Material) EnsureMaterialGraph(Material);
		UEdGraph* Graph = Material ? (UEdGraph*)Material->MaterialGraph : (MatFunc ? MatFunc->MaterialGraph : nullptr);
		if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no material graph"), *AssetDisplayName));

		UMaterialGraphNode* TargetMatNode = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid.ToString() == NodeId)
			{
				TargetMatNode = Cast<UMaterialGraphNode>(Node);
				break;
			}
		}
		if (!TargetMatNode) return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));
		if (!TargetMatNode->MaterialExpression) return FMCPToolResult::Error(TEXT("Node has no material expression"));

		FString DeletedNodeTitle = TargetMatNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		FString DeletedExprClass = TargetMatNode->MaterialExpression->GetClass()->GetName();

		UMaterialExpression* ExprToRemove = TargetMatNode->MaterialExpression;
		if (Material) Material->GetExpressionCollection().RemoveExpression(ExprToRemove);
		else MatFunc->GetExpressionCollection().RemoveExpression(ExprToRemove);
		ExprToRemove->MarkAsGarbage();

		Graph->NotifyGraphChanged();
		UObject* Asset = Material ? (UObject*)Material : (UObject*)MatFunc;
		Asset->PreEditChange(nullptr);
		Asset->PostEditChange();
		Asset->MarkPackageDirty();

		bool bSaved = SaveMaterialPackage(Asset);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("material"), AssetDisplayName);
		Result->SetStringField(TEXT("deletedNode"), NodeId);
		Result->SetStringField(TEXT("deletedNodeTitle"), DeletedNodeTitle);
		Result->SetStringField(TEXT("deletedExpressionClass"), DeletedExprClass);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// connect_material_pins
// ============================================================
class FTool_ConnectMaterialPins : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("connect_material_pins");
		Info.Description = TEXT("Connect two pins in a material graph.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name"), false});
		Info.Parameters.Add({TEXT("materialFunction"), TEXT("string"), TEXT("MaterialFunction name"), false});
		Info.Parameters.Add({TEXT("sourceNodeId"), TEXT("string"), TEXT("Source node GUID"), true});
		Info.Parameters.Add({TEXT("sourcePinName"), TEXT("string"), TEXT("Source pin name"), true});
		Info.Parameters.Add({TEXT("targetNodeId"), TEXT("string"), TEXT("Target node GUID"), true});
		Info.Parameters.Add({TEXT("targetPinName"), TEXT("string"), TEXT("Target pin name"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString MaterialFunctionName = Params->GetStringField(TEXT("materialFunction"));
		FString SourceNodeId = Params->GetStringField(TEXT("sourceNodeId"));
		FString SourcePinName = Params->GetStringField(TEXT("sourcePinName"));
		FString TargetNodeId = Params->GetStringField(TEXT("targetNodeId"));
		FString TargetPinName = Params->GetStringField(TEXT("targetPinName"));

		if (MaterialName.IsEmpty() && MaterialFunctionName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: 'material' or 'materialFunction'"));
		if (SourceNodeId.IsEmpty() || SourcePinName.IsEmpty() || TargetNodeId.IsEmpty() || TargetPinName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: sourceNodeId, sourcePinName, targetNodeId, targetPinName"));

		UMaterial* Material = nullptr;
		UMaterialFunction* MatFunc = nullptr;
		FString AssetDisplayName;

		if (!MaterialFunctionName.IsEmpty())
		{
			FString E; MatFunc = LoadMaterialFunctionByName(MaterialFunctionName, E);
			if (!MatFunc) return FMCPToolResult::Error(E);
			AssetDisplayName = MatFunc->GetName();
		}
		else
		{
			FString E; Material = LoadMaterialByName(MaterialName, E);
			if (!Material) return FMCPToolResult::Error(E);
			AssetDisplayName = Material->GetName();
		}

		if (Material) EnsureMaterialGraph(Material);
		UEdGraph* Graph = Material ? (UEdGraph*)Material->MaterialGraph : (MatFunc ? MatFunc->MaterialGraph : nullptr);
		if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no material graph"), *AssetDisplayName));

		UEdGraphNode* SourceNode = nullptr;
		UEdGraphNode* TargetNode = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (Node->NodeGuid.ToString() == SourceNodeId) SourceNode = Node;
			if (Node->NodeGuid.ToString() == TargetNodeId) TargetNode = Node;
			if (SourceNode && TargetNode) break;
		}
		if (!SourceNode) return FMCPToolResult::Error(FString::Printf(TEXT("Source node '%s' not found"), *SourceNodeId));
		if (!TargetNode) return FMCPToolResult::Error(FString::Printf(TEXT("Target node '%s' not found"), *TargetNodeId));

		UEdGraphPin* SourcePin = SourceNode->FindPin(FName(*SourcePinName));
		if (!SourcePin) return FMCPToolResult::Error(FString::Printf(TEXT("Source pin '%s' not found"), *SourcePinName));
		UEdGraphPin* TargetPin = TargetNode->FindPin(FName(*TargetPinName));
		if (!TargetPin) return FMCPToolResult::Error(FString::Printf(TEXT("Target pin '%s' not found"), *TargetPinName));

		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (!Schema) return FMCPToolResult::Error(TEXT("Material graph schema not found"));

		bool bConnected = Schema->TryCreateConnection(SourcePin, TargetPin);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), bConnected);
		Result->SetBoolField(TEXT("connected"), bConnected);
		Result->SetStringField(TEXT("material"), AssetDisplayName);

		if (bConnected)
		{
			UObject* Asset = Material ? (UObject*)Material : (UObject*)MatFunc;
			Asset->PreEditChange(nullptr);
			Asset->PostEditChange();
			bool bSaved = SaveMaterialPackage(Asset);
			Result->SetBoolField(TEXT("saved"), bSaved);
		}
		else
		{
			Result->SetStringField(TEXT("error"), TEXT("Cannot connect — types may be incompatible"));
		}
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// disconnect_material_pin
// ============================================================
class FTool_DisconnectMaterialPin : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("disconnect_material_pin");
		Info.Description = TEXT("Break all connections on a pin in a material graph.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name"), false});
		Info.Parameters.Add({TEXT("materialFunction"), TEXT("string"), TEXT("MaterialFunction name"), false});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		Info.Parameters.Add({TEXT("pinName"), TEXT("string"), TEXT("Pin name to disconnect"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString MaterialFunctionName = Params->GetStringField(TEXT("materialFunction"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));
		FString PinName = Params->GetStringField(TEXT("pinName"));

		if (MaterialName.IsEmpty() && MaterialFunctionName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: 'material' or 'materialFunction'"));
		if (NodeId.IsEmpty() || PinName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: nodeId, pinName"));

		UMaterial* Material = nullptr;
		UMaterialFunction* MatFunc = nullptr;
		FString AssetDisplayName;
		if (!MaterialFunctionName.IsEmpty())
		{
			FString E; MatFunc = LoadMaterialFunctionByName(MaterialFunctionName, E);
			if (!MatFunc) return FMCPToolResult::Error(E);
			AssetDisplayName = MatFunc->GetName();
		}
		else
		{
			FString E; Material = LoadMaterialByName(MaterialName, E);
			if (!Material) return FMCPToolResult::Error(E);
			AssetDisplayName = Material->GetName();
		}

		if (Material) EnsureMaterialGraph(Material);
		UEdGraph* Graph = Material ? (UEdGraph*)Material->MaterialGraph : (MatFunc ? MatFunc->MaterialGraph : nullptr);
		if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no material graph"), *AssetDisplayName));

		UEdGraphNode* TargetNode = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid.ToString() == NodeId) { TargetNode = Node; break; }
		}
		if (!TargetNode) return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

		UEdGraphPin* Pin = TargetNode->FindPin(FName(*PinName));
		if (!Pin) return FMCPToolResult::Error(FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *PinName, *NodeId));

		int32 BrokenCount = Pin->LinkedTo.Num();
		Pin->BreakAllPinLinks();

		UObject* Asset = Material ? (UObject*)Material : (UObject*)MatFunc;
		Asset->PreEditChange(nullptr);
		Asset->PostEditChange();
		bool bSaved = SaveMaterialPackage(Asset);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("material"), AssetDisplayName);
		Result->SetStringField(TEXT("nodeId"), NodeId);
		Result->SetStringField(TEXT("pinName"), PinName);
		Result->SetNumberField(TEXT("brokenLinkCount"), BrokenCount);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// set_expression_value
// ============================================================
class FTool_SetExpressionValue : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_expression_value");
		Info.Description = TEXT("Set a value on a material expression (Constant, Vector, Parameter, Custom code, etc.).");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name"), false});
		Info.Parameters.Add({TEXT("materialFunction"), TEXT("string"), TEXT("MaterialFunction name"), false});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		Info.Parameters.Add({TEXT("value"), TEXT("string"), TEXT("Value (number, object {r,g,b,a}, or string for Custom code)"), true});
		Info.Parameters.Add({TEXT("parameterName"), TEXT("string"), TEXT("Rename the parameter (for ScalarParameter/VectorParameter)"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString MaterialFunctionName = Params->GetStringField(TEXT("materialFunction"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));

		if (MaterialName.IsEmpty() && MaterialFunctionName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: 'material' or 'materialFunction'"));
		if (NodeId.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing: nodeId"));
		if (!Params->HasField(TEXT("value"))) return FMCPToolResult::Error(TEXT("Missing: value"));

		UMaterial* Material = nullptr;
		UMaterialFunction* MatFunc = nullptr;
		FString AssetDisplayName;
		if (!MaterialFunctionName.IsEmpty())
		{
			FString E; MatFunc = LoadMaterialFunctionByName(MaterialFunctionName, E);
			if (!MatFunc) return FMCPToolResult::Error(E);
			AssetDisplayName = MatFunc->GetName();
		}
		else
		{
			FString E; Material = LoadMaterialByName(MaterialName, E);
			if (!Material) return FMCPToolResult::Error(E);
			AssetDisplayName = Material->GetName();
		}

		if (Material) EnsureMaterialGraph(Material);
		UEdGraph* Graph = Material ? (UEdGraph*)Material->MaterialGraph : (MatFunc ? MatFunc->MaterialGraph : nullptr);
		if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no material graph"), *AssetDisplayName));

		UMaterialGraphNode* TargetMatNode = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid.ToString() == NodeId) { TargetMatNode = Cast<UMaterialGraphNode>(Node); break; }
		}
		if (!TargetMatNode) return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

		UMaterialExpression* Expr = TargetMatNode->MaterialExpression;
		if (!Expr) return FMCPToolResult::Error(TEXT("Node has no material expression"));

		UObject* Asset = Material ? (UObject*)Material : (UObject*)MatFunc;
		Asset->PreEditChange(nullptr);

		FString ExprType, NewValueStr;

		if (UMaterialExpressionConstant* CE = Cast<UMaterialExpressionConstant>(Expr))
		{
			ExprType = TEXT("Constant");
			double V = Params->GetNumberField(TEXT("value"));
			CE->R = (float)V;
			NewValueStr = FString::Printf(TEXT("%f"), V);
		}
		else if (UMaterialExpressionConstant3Vector* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
		{
			ExprType = TEXT("Constant3Vector");
			const TSharedPtr<FJsonObject>* VO = nullptr;
			if (!Params->TryGetObjectField(TEXT("value"), VO) || !VO)
			{
				Asset->PostEditChange();
				return FMCPToolResult::Error(TEXT("Constant3Vector requires value as {r, g, b}"));
			}
			double R = 0, G = 0, B = 0;
			(*VO)->TryGetNumberField(TEXT("r"), R); (*VO)->TryGetNumberField(TEXT("g"), G); (*VO)->TryGetNumberField(TEXT("b"), B);
			C3->Constant = FLinearColor((float)R, (float)G, (float)B);
			NewValueStr = FString::Printf(TEXT("(%f, %f, %f)"), R, G, B);
		}
		else if (UMaterialExpressionConstant4Vector* C4 = Cast<UMaterialExpressionConstant4Vector>(Expr))
		{
			ExprType = TEXT("Constant4Vector");
			const TSharedPtr<FJsonObject>* VO = nullptr;
			if (!Params->TryGetObjectField(TEXT("value"), VO) || !VO)
			{
				Asset->PostEditChange();
				return FMCPToolResult::Error(TEXT("Constant4Vector requires value as {r, g, b, a}"));
			}
			double R = 0, G = 0, B = 0, A = 1;
			(*VO)->TryGetNumberField(TEXT("r"), R); (*VO)->TryGetNumberField(TEXT("g"), G);
			(*VO)->TryGetNumberField(TEXT("b"), B); (*VO)->TryGetNumberField(TEXT("a"), A);
			C4->Constant = FLinearColor((float)R, (float)G, (float)B, (float)A);
			NewValueStr = FString::Printf(TEXT("(%f, %f, %f, %f)"), R, G, B, A);
		}
		else if (UMaterialExpressionScalarParameter* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			ExprType = TEXT("ScalarParameter");
			double V = Params->GetNumberField(TEXT("value"));
			SP->DefaultValue = (float)V;
			NewValueStr = FString::Printf(TEXT("%f"), V);
			FString ParamName;
			if (Params->TryGetStringField(TEXT("parameterName"), ParamName) && !ParamName.IsEmpty())
				SP->ParameterName = FName(*ParamName);
		}
		else if (UMaterialExpressionVectorParameter* VP = Cast<UMaterialExpressionVectorParameter>(Expr))
		{
			ExprType = TEXT("VectorParameter");
			const TSharedPtr<FJsonObject>* VO = nullptr;
			if (!Params->TryGetObjectField(TEXT("value"), VO) || !VO)
			{
				Asset->PostEditChange();
				return FMCPToolResult::Error(TEXT("VectorParameter requires value as {r, g, b, a}"));
			}
			double R = 0, G = 0, B = 0, A = 1;
			(*VO)->TryGetNumberField(TEXT("r"), R); (*VO)->TryGetNumberField(TEXT("g"), G);
			(*VO)->TryGetNumberField(TEXT("b"), B); (*VO)->TryGetNumberField(TEXT("a"), A);
			VP->DefaultValue = FLinearColor((float)R, (float)G, (float)B, (float)A);
			NewValueStr = FString::Printf(TEXT("(%f, %f, %f, %f)"), R, G, B, A);
			FString ParamName;
			if (Params->TryGetStringField(TEXT("parameterName"), ParamName) && !ParamName.IsEmpty())
				VP->ParameterName = FName(*ParamName);
		}
		else if (UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expr))
		{
			ExprType = TEXT("Custom");
			FString Code = Params->GetStringField(TEXT("value"));
			Custom->Code = Code;
			NewValueStr = FString::Printf(TEXT("Code: %d chars"), Code.Len());
		}
		else if (UMaterialExpressionComponentMask* CM = Cast<UMaterialExpressionComponentMask>(Expr))
		{
			ExprType = TEXT("ComponentMask");
			const TSharedPtr<FJsonObject>* VO = nullptr;
			if (!Params->TryGetObjectField(TEXT("value"), VO) || !VO)
			{
				Asset->PostEditChange();
				return FMCPToolResult::Error(TEXT("ComponentMask requires value as {r, g, b, a} (booleans)"));
			}
			bool bR = false, bG = false, bB = false, bA = false;
			(*VO)->TryGetBoolField(TEXT("r"), bR); (*VO)->TryGetBoolField(TEXT("g"), bG);
			(*VO)->TryGetBoolField(TEXT("b"), bB); (*VO)->TryGetBoolField(TEXT("a"), bA);
			CM->R = bR ? 1 : 0; CM->G = bG ? 1 : 0; CM->B = bB ? 1 : 0; CM->A = bA ? 1 : 0;
			NewValueStr = FString::Printf(TEXT("(R=%s, G=%s, B=%s, A=%s)"),
				bR ? TEXT("true") : TEXT("false"), bG ? TEXT("true") : TEXT("false"),
				bB ? TEXT("true") : TEXT("false"), bA ? TEXT("true") : TEXT("false"));
		}
		else
		{
			Asset->PostEditChange();
			return FMCPToolResult::Error(FString::Printf(TEXT("Expression type '%s' does not support direct value setting"), *Expr->GetClass()->GetName()));
		}

		Asset->PostEditChange();
		Asset->MarkPackageDirty();
		bool bSaved = SaveMaterialPackage(Asset);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("material"), AssetDisplayName);
		Result->SetStringField(TEXT("nodeId"), NodeId);
		Result->SetStringField(TEXT("expressionType"), ExprType);
		Result->SetStringField(TEXT("newValue"), NewValueStr);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// move_material_expression
// ============================================================
class FTool_MoveMaterialExpression : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("move_material_expression");
		Info.Description = TEXT("Reposition a material expression node in the graph.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name"), false});
		Info.Parameters.Add({TEXT("materialFunction"), TEXT("string"), TEXT("MaterialFunction name"), false});
		Info.Parameters.Add({TEXT("nodeId"), TEXT("string"), TEXT("Node GUID"), true});
		Info.Parameters.Add({TEXT("posX"), TEXT("number"), TEXT("New X position"), true});
		Info.Parameters.Add({TEXT("posY"), TEXT("number"), TEXT("New Y position"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString MaterialFunctionName = Params->GetStringField(TEXT("materialFunction"));
		FString NodeId = Params->GetStringField(TEXT("nodeId"));

		if (MaterialName.IsEmpty() && MaterialFunctionName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: 'material' or 'materialFunction'"));
		if (NodeId.IsEmpty() || !Params->HasField(TEXT("posX")) || !Params->HasField(TEXT("posY")))
			return FMCPToolResult::Error(TEXT("Missing: nodeId, posX, posY"));

		int32 PosX = (int32)Params->GetNumberField(TEXT("posX"));
		int32 PosY = (int32)Params->GetNumberField(TEXT("posY"));

		UMaterial* Material = nullptr;
		UMaterialFunction* MatFunc = nullptr;
		FString AssetDisplayName;
		if (!MaterialFunctionName.IsEmpty())
		{
			FString E; MatFunc = LoadMaterialFunctionByName(MaterialFunctionName, E);
			if (!MatFunc) return FMCPToolResult::Error(E);
			AssetDisplayName = MatFunc->GetName();
		}
		else
		{
			FString E; Material = LoadMaterialByName(MaterialName, E);
			if (!Material) return FMCPToolResult::Error(E);
			AssetDisplayName = Material->GetName();
		}

		if (Material) EnsureMaterialGraph(Material);
		UEdGraph* Graph = Material ? (UEdGraph*)Material->MaterialGraph : (MatFunc ? MatFunc->MaterialGraph : nullptr);
		if (!Graph) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no material graph"), *AssetDisplayName));

		UMaterialGraphNode* TargetMatNode = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid.ToString() == NodeId) { TargetMatNode = Cast<UMaterialGraphNode>(Node); break; }
		}
		if (!TargetMatNode) return FMCPToolResult::Error(FString::Printf(TEXT("Node '%s' not found"), *NodeId));

		TargetMatNode->NodePosX = PosX;
		TargetMatNode->NodePosY = PosY;
		if (TargetMatNode->MaterialExpression)
		{
			TargetMatNode->MaterialExpression->MaterialExpressionEditorX = PosX;
			TargetMatNode->MaterialExpression->MaterialExpressionEditorY = PosY;
		}

		UObject* Asset = Material ? (UObject*)Material : (UObject*)MatFunc;
		Asset->PreEditChange(nullptr);
		Asset->PostEditChange();
		bool bSaved = SaveMaterialPackage(Asset);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("material"), AssetDisplayName);
		Result->SetStringField(TEXT("nodeId"), NodeId);
		Result->SetNumberField(TEXT("posX"), PosX);
		Result->SetNumberField(TEXT("posY"), PosY);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// create_material_instance
// ============================================================
class FTool_CreateMaterialInstance : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_material_instance");
		Info.Description = TEXT("Create a MaterialInstanceConstant from a parent material.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Instance name"), true});
		Info.Parameters.Add({TEXT("packagePath"), TEXT("string"), TEXT("Package path"), true});
		Info.Parameters.Add({TEXT("parent"), TEXT("string"), TEXT("Parent material name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		FString PackagePath = Params->GetStringField(TEXT("packagePath"));
		FString ParentName = Params->GetStringField(TEXT("parent"));
		if (Name.IsEmpty() || PackagePath.IsEmpty() || ParentName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: name, packagePath, parent"));

		FString LoadError;
		UMaterial* Parent = LoadMaterialByName(ParentName, LoadError);
		if (!Parent) return FMCPToolResult::Error(LoadError);

		FString FullPath = PackagePath / Name;
		UPackage* Package = CreatePackage(*FullPath);
		if (!Package) return FMCPToolResult::Error(TEXT("Failed to create package"));

		UMaterialInstanceConstant* MI = NewObject<UMaterialInstanceConstant>(Package, FName(*Name), RF_Public | RF_Standalone);
		if (!MI) return FMCPToolResult::Error(TEXT("Failed to create MaterialInstanceConstant"));

		MI->SetParentEditorOnly(Parent);
		MI->MarkPackageDirty();
		bool bSaved = SaveMaterialPackage(MI);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("name"), Name);
		Result->SetStringField(TEXT("path"), MI->GetPathName());
		Result->SetStringField(TEXT("parent"), Parent->GetName());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// set_material_instance_parameter
// ============================================================
class FTool_SetMaterialInstanceParameter : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_material_instance_parameter");
		Info.Description = TEXT("Set a parameter override on a MaterialInstanceConstant.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("instance"), TEXT("string"), TEXT("MaterialInstance name or path"), true});
		Info.Parameters.Add({TEXT("parameterName"), TEXT("string"), TEXT("Parameter name"), true});
		Info.Parameters.Add({TEXT("value"), TEXT("string"), TEXT("New value (number for scalar, {r,g,b,a} for vector)"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString InstanceName = Params->GetStringField(TEXT("instance"));
		FString ParamName = Params->GetStringField(TEXT("parameterName"));
		if (InstanceName.IsEmpty() || ParamName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: instance, parameterName"));
		if (!Params->HasField(TEXT("value")))
			return FMCPToolResult::Error(TEXT("Missing: value"));

		FString LoadError;
		UMaterialInstanceConstant* MI = LoadMaterialInstanceByName(InstanceName, LoadError);
		if (!MI) return FMCPToolResult::Error(LoadError);

		FString SetType;

		// Try as scalar
		double ScalarVal = 0;
		if (Params->TryGetNumberField(TEXT("value"), ScalarVal))
		{
			MI->SetScalarParameterValueEditorOnly(FName(*ParamName), (float)ScalarVal);
			SetType = TEXT("Scalar");
		}
		else
		{
			const TSharedPtr<FJsonObject>* VO = nullptr;
			if (Params->TryGetObjectField(TEXT("value"), VO) && VO)
			{
				double R = 0, G = 0, B = 0, A = 1;
				(*VO)->TryGetNumberField(TEXT("r"), R); (*VO)->TryGetNumberField(TEXT("g"), G);
				(*VO)->TryGetNumberField(TEXT("b"), B); (*VO)->TryGetNumberField(TEXT("a"), A);
				MI->SetVectorParameterValueEditorOnly(FName(*ParamName), FLinearColor((float)R, (float)G, (float)B, (float)A));
				SetType = TEXT("Vector");
			}
			else
			{
				return FMCPToolResult::Error(TEXT("value must be a number (scalar) or object {r,g,b,a} (vector)"));
			}
		}

		MI->MarkPackageDirty();
		bool bSaved = SaveMaterialPackage(MI);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("instance"), MI->GetName());
		Result->SetStringField(TEXT("parameterName"), ParamName);
		Result->SetStringField(TEXT("parameterType"), SetType);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// snapshot_material_graph
// ============================================================
class FTool_SnapshotMaterialGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("snapshot_material_graph");
		Info.Description = TEXT("Take a snapshot of the current material graph state for later diff/restore.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		if (MaterialName.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing: material"));

		FString LoadError;
		UMaterial* Material = LoadMaterialByName(MaterialName, LoadError);
		if (!Material) return FMCPToolResult::Error(LoadError);

		EnsureMaterialGraph(Material);
		if (!Material->MaterialGraph) return FMCPToolResult::Error(TEXT("Material has no graph"));

		FGraphSnapshot Snapshot;
		Snapshot.SnapshotId = MCPHelpers::GenerateSnapshotId(MaterialName);
		Snapshot.BlueprintName = Material->GetName();
		Snapshot.BlueprintPath = Material->GetPathName();
		Snapshot.CreatedAt = FDateTime::Now();

		FGraphSnapshotData GraphData = MCPHelpers::CaptureGraphSnapshot(Material->MaterialGraph);
		int32 NodeCount = GraphData.Nodes.Num();
		int32 ConnectionCount = GraphData.Connections.Num();
		Snapshot.Graphs.Add(TEXT("MaterialGraph"), MoveTemp(GraphData));

		MCPHelpers::GetMaterialSnapshots().Add(Snapshot.SnapshotId, Snapshot);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("snapshotId"), Snapshot.SnapshotId);
		Result->SetStringField(TEXT("material"), Material->GetName());
		Result->SetNumberField(TEXT("nodeCount"), NodeCount);
		Result->SetNumberField(TEXT("connectionCount"), ConnectionCount);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// diff_material_graph
// ============================================================
class FTool_DiffMaterialGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("diff_material_graph");
		Info.Description = TEXT("Diff current material graph against a snapshot.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name"), true});
		Info.Parameters.Add({TEXT("snapshotId"), TEXT("string"), TEXT("Snapshot ID to diff against"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString SnapshotId = Params->GetStringField(TEXT("snapshotId"));
		if (MaterialName.IsEmpty() || SnapshotId.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: material, snapshotId"));

		FGraphSnapshot* SnapshotPtr = MCPHelpers::GetMaterialSnapshots().Find(SnapshotId);
		if (!SnapshotPtr) return FMCPToolResult::Error(FString::Printf(TEXT("Snapshot '%s' not found"), *SnapshotId));

		FString LoadError;
		UMaterial* Material = LoadMaterialByName(MaterialName, LoadError);
		if (!Material) return FMCPToolResult::Error(LoadError);

		EnsureMaterialGraph(Material);
		if (!Material->MaterialGraph) return FMCPToolResult::Error(TEXT("Material has no graph"));

		FGraphSnapshotData CurrentData = MCPHelpers::CaptureGraphSnapshot(Material->MaterialGraph);
		const FGraphSnapshotData* SnapData = SnapshotPtr->Graphs.Find(TEXT("MaterialGraph"));
		if (!SnapData) return FMCPToolResult::Error(TEXT("Snapshot has no MaterialGraph"));

		auto MakeConnKey = [](const FString& SG, const FString& SP, const FString& TG, const FString& TP) { return FString::Printf(TEXT("%s|%s|%s|%s"), *SG, *SP, *TG, *TP); };

		TSet<FString> SnapConnSet, CurConnSet;
		for (const FPinConnectionRecord& C : SnapData->Connections) SnapConnSet.Add(MakeConnKey(C.SourceNodeGuid, C.SourcePinName, C.TargetNodeGuid, C.TargetPinName));
		for (const FPinConnectionRecord& C : CurrentData.Connections) CurConnSet.Add(MakeConnKey(C.SourceNodeGuid, C.SourcePinName, C.TargetNodeGuid, C.TargetPinName));

		TMap<FString, const FNodeRecord*> CurNodeLookup;
		for (const FNodeRecord& NR : CurrentData.Nodes) CurNodeLookup.Add(NR.NodeGuid, &NR);

		TArray<TSharedPtr<FJsonValue>> SeveredArr, NewConnsArr, MissingNodesArr;

		for (const FPinConnectionRecord& C : SnapData->Connections)
		{
			if (!CurConnSet.Contains(MakeConnKey(C.SourceNodeGuid, C.SourcePinName, C.TargetNodeGuid, C.TargetPinName)))
			{
				TSharedRef<FJsonObject> SJ = MakeShared<FJsonObject>();
				SJ->SetStringField(TEXT("sourceNodeGuid"), C.SourceNodeGuid);
				SJ->SetStringField(TEXT("sourcePinName"), C.SourcePinName);
				SJ->SetStringField(TEXT("targetNodeGuid"), C.TargetNodeGuid);
				SJ->SetStringField(TEXT("targetPinName"), C.TargetPinName);
				SeveredArr.Add(MakeShared<FJsonValueObject>(SJ));
			}
		}
		for (const FPinConnectionRecord& C : CurrentData.Connections)
		{
			if (!SnapConnSet.Contains(MakeConnKey(C.SourceNodeGuid, C.SourcePinName, C.TargetNodeGuid, C.TargetPinName)))
			{
				TSharedRef<FJsonObject> NJ = MakeShared<FJsonObject>();
				NJ->SetStringField(TEXT("sourceNodeGuid"), C.SourceNodeGuid);
				NJ->SetStringField(TEXT("sourcePinName"), C.SourcePinName);
				NJ->SetStringField(TEXT("targetNodeGuid"), C.TargetNodeGuid);
				NJ->SetStringField(TEXT("targetPinName"), C.TargetPinName);
				NewConnsArr.Add(MakeShared<FJsonValueObject>(NJ));
			}
		}
		for (const FNodeRecord& SN : SnapData->Nodes)
		{
			if (!CurNodeLookup.Contains(SN.NodeGuid))
			{
				TSharedRef<FJsonObject> MJ = MakeShared<FJsonObject>();
				MJ->SetStringField(TEXT("nodeGuid"), SN.NodeGuid);
				MJ->SetStringField(TEXT("nodeClass"), SN.NodeClass);
				MJ->SetStringField(TEXT("nodeTitle"), SN.NodeTitle);
				MissingNodesArr.Add(MakeShared<FJsonValueObject>(MJ));
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("material"), Material->GetName());
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
// restore_material_graph
// ============================================================
class FTool_RestoreMaterialGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("restore_material_graph");
		Info.Description = TEXT("Restore material graph connections from a snapshot.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name"), true});
		Info.Parameters.Add({TEXT("snapshotId"), TEXT("string"), TEXT("Snapshot ID"), true});
		Info.Parameters.Add({TEXT("dryRun"), TEXT("boolean"), TEXT("Preview without applying"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		FString SnapshotId = Params->GetStringField(TEXT("snapshotId"));
		if (MaterialName.IsEmpty() || SnapshotId.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing: material, snapshotId"));

		bool bDryRun = false;
		Params->TryGetBoolField(TEXT("dryRun"), bDryRun);

		FGraphSnapshot* SnapshotPtr = MCPHelpers::GetMaterialSnapshots().Find(SnapshotId);
		if (!SnapshotPtr) return FMCPToolResult::Error(FString::Printf(TEXT("Snapshot '%s' not found"), *SnapshotId));

		FString LoadError;
		UMaterial* Material = LoadMaterialByName(MaterialName, LoadError);
		if (!Material) return FMCPToolResult::Error(LoadError);

		EnsureMaterialGraph(Material);
		if (!Material->MaterialGraph) return FMCPToolResult::Error(TEXT("Material has no graph"));

		FGraphSnapshotData CurrentData = MCPHelpers::CaptureGraphSnapshot(Material->MaterialGraph);
		const FGraphSnapshotData* SnapData = SnapshotPtr->Graphs.Find(TEXT("MaterialGraph"));
		if (!SnapData) return FMCPToolResult::Error(TEXT("Snapshot has no MaterialGraph"));

		auto MakeConnKey = [](const FString& SG, const FString& SP, const FString& TG, const FString& TP) { return FString::Printf(TEXT("%s|%s|%s|%s"), *SG, *SP, *TG, *TP); };

		TSet<FString> CurConnSet;
		for (const FPinConnectionRecord& C : CurrentData.Connections)
			CurConnSet.Add(MakeConnKey(C.SourceNodeGuid, C.SourcePinName, C.TargetNodeGuid, C.TargetPinName));

		TMap<FString, UEdGraphNode*> NodeLookup;
		for (UEdGraphNode* Node : Material->MaterialGraph->Nodes)
		{
			if (Node) NodeLookup.Add(Node->NodeGuid.ToString(), Node);
		}

		int32 Reconnected = 0, Failed = 0;

		for (const FPinConnectionRecord& Conn : SnapData->Connections)
		{
			FString Key = MakeConnKey(Conn.SourceNodeGuid, Conn.SourcePinName, Conn.TargetNodeGuid, Conn.TargetPinName);
			if (CurConnSet.Contains(Key)) continue;

			UEdGraphNode** SrcPtr = NodeLookup.Find(Conn.SourceNodeGuid);
			UEdGraphNode** TgtPtr = NodeLookup.Find(Conn.TargetNodeGuid);
			if (!SrcPtr || !*SrcPtr || !TgtPtr || !*TgtPtr) { Failed++; continue; }

			UEdGraphPin* SrcPin = (*SrcPtr)->FindPin(FName(*Conn.SourcePinName));
			UEdGraphPin* TgtPin = (*TgtPtr)->FindPin(FName(*Conn.TargetPinName));
			if (!SrcPin || !TgtPin) { Failed++; continue; }

			if (bDryRun) { Reconnected++; continue; }

			const UEdGraphSchema* Schema = Material->MaterialGraph->GetSchema();
			if (Schema && Schema->TryCreateConnection(SrcPin, TgtPin)) Reconnected++;
			else Failed++;
		}

		bool bSaved = false;
		if (!bDryRun && Reconnected > 0)
		{
			Material->PreEditChange(nullptr);
			Material->PostEditChange();
			bSaved = SaveMaterialPackage(Material);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("material"), Material->GetName());
		Result->SetStringField(TEXT("snapshotId"), SnapshotId);
		Result->SetNumberField(TEXT("reconnected"), Reconnected);
		Result->SetNumberField(TEXT("failed"), Failed);
		Result->SetBoolField(TEXT("saved"), bSaved);
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// validate_material
// ============================================================
class FTool_ValidateMaterial : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("validate_material");
		Info.Description = TEXT("Force-recompile a material and report errors.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		if (MaterialName.IsEmpty()) return FMCPToolResult::Error(TEXT("Missing: material"));

		FString LoadError;
		UMaterial* Material = LoadMaterialByName(MaterialName, LoadError);
		if (!Material) return FMCPToolResult::Error(LoadError);

		Material->PreEditChange(nullptr);
		Material->PostEditChange();

		TArray<TSharedPtr<FJsonValue>> ErrorArray;
		bool bValid = true;

		FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
		if (Resource)
		{
			const TArray<FString>& CompileErrors = Resource->GetCompileErrors();
			for (const FString& Err : CompileErrors)
			{
				bValid = false;
				ErrorArray.Add(MakeShared<FJsonValueString>(Err));
			}
		}

		auto Expressions = Material->GetExpressions();
		int32 ConnectionCount = 0;
		if (Material->MaterialGraph)
		{
			for (UEdGraphNode* Node : Material->MaterialGraph->Nodes)
			{
				if (!Node) continue;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output) ConnectionCount += Pin->LinkedTo.Num();
				}
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("valid"), bValid);
		Result->SetStringField(TEXT("material"), Material->GetName());
		Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Result->SetNumberField(TEXT("expressionCount"), Expressions.Num());
		Result->SetNumberField(TEXT("connectionCount"), ConnectionCount);
		Result->SetArrayField(TEXT("errors"), ErrorArray);
		Result->SetNumberField(TEXT("errorCount"), ErrorArray.Num());
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterMaterialMutationTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_CreateMaterial>());
		R.Register(MakeShared<FTool_SetMaterialProperty>());
		R.Register(MakeShared<FTool_AddMaterialExpression>());
		R.Register(MakeShared<FTool_DeleteMaterialExpression>());
		R.Register(MakeShared<FTool_ConnectMaterialPins>());
		R.Register(MakeShared<FTool_DisconnectMaterialPin>());
		R.Register(MakeShared<FTool_SetExpressionValue>());
		R.Register(MakeShared<FTool_MoveMaterialExpression>());
		R.Register(MakeShared<FTool_CreateMaterialInstance>());
		R.Register(MakeShared<FTool_SetMaterialInstanceParameter>());
		R.Register(MakeShared<FTool_SnapshotMaterialGraph>());
		R.Register(MakeShared<FTool_DiffMaterialGraph>());
		R.Register(MakeShared<FTool_RestoreMaterialGraph>());
		R.Register(MakeShared<FTool_ValidateMaterial>());
	}
}
