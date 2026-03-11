// Material Read Tools — list, get, describe, search materials and material functions
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
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"

// Local helpers for loading material assets
namespace
{
	UMaterial* LoadMaterialByName(const FString& Name, FString& OutError)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), Assets, false);

		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString() == Name ||
				Asset.PackageName.ToString() == Name ||
				Asset.GetObjectPathString() == Name)
			{
				UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
				if (Mat) return Mat;
			}
		}
		// Case-insensitive fallback
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase) ||
				Asset.PackageName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				UMaterial* Mat = Cast<UMaterial>(Asset.GetAsset());
				if (Mat) return Mat;
			}
		}
		OutError = FString::Printf(TEXT("Material '%s' not found. Use list_materials to see available assets."), *Name);
		return nullptr;
	}

	UMaterialInstanceConstant* LoadMaterialInstanceByName(const FString& Name, FString& OutError)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), Assets, false);

		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString() == Name ||
				Asset.PackageName.ToString() == Name ||
				Asset.GetObjectPathString() == Name)
			{
				UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
				if (MI) return MI;
			}
		}
		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString().Equals(Name, ESearchCase::IgnoreCase))
			{
				UMaterialInstanceConstant* MI = Cast<UMaterialInstanceConstant>(Asset.GetAsset());
				if (MI) return MI;
			}
		}
		OutError = FString::Printf(TEXT("MaterialInstance '%s' not found."), *Name);
		return nullptr;
	}

	UMaterialFunction* LoadMaterialFunctionByName(const FString& Name, FString& OutError)
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> Assets;
		Registry.GetAssetsByClass(UMaterialFunction::StaticClass()->GetClassPathName(), Assets, false);

		for (const FAssetData& Asset : Assets)
		{
			if (Asset.AssetName.ToString() == Name ||
				Asset.PackageName.ToString() == Name ||
				Asset.GetObjectPathString() == Name)
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
}

// ============================================================
// list_materials
// ============================================================
class FTool_ListMaterials : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_materials");
		Info.Description = TEXT("List Material and MaterialInstance assets in the project.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Name/path filter"), false});
		Info.Parameters.Add({TEXT("type"), TEXT("string"), TEXT("Filter by type: material, instance, or all"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Filter;
		Params->TryGetStringField(TEXT("filter"), Filter);
		FString TypeFilter;
		Params->TryGetStringField(TEXT("type"), TypeFilter);

		bool bIncludeMaterials = TypeFilter.IsEmpty() || TypeFilter == TEXT("all") || TypeFilter == TEXT("material");
		bool bIncludeInstances = TypeFilter.IsEmpty() || TypeFilter == TEXT("all") || TypeFilter == TEXT("instance");

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<TSharedPtr<FJsonValue>> Entries;
		int32 Total = 0;

		if (bIncludeMaterials)
		{
			TArray<FAssetData> MatAssets;
			Registry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MatAssets, false);
			Total += MatAssets.Num();
			for (const FAssetData& Asset : MatAssets)
			{
				FString Name = Asset.AssetName.ToString();
				FString Path = Asset.PackageName.ToString();
				if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !Path.Contains(Filter, ESearchCase::IgnoreCase))
					continue;
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Name);
				Entry->SetStringField(TEXT("path"), Path);
				Entry->SetStringField(TEXT("type"), TEXT("Material"));
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}

		if (bIncludeInstances)
		{
			TArray<FAssetData> MIAssets;
			Registry.GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), MIAssets, false);
			Total += MIAssets.Num();
			for (const FAssetData& Asset : MIAssets)
			{
				FString Name = Asset.AssetName.ToString();
				FString Path = Asset.PackageName.ToString();
				if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !Path.Contains(Filter, ESearchCase::IgnoreCase))
					continue;
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("name"), Name);
				Entry->SetStringField(TEXT("path"), Path);
				Entry->SetStringField(TEXT("type"), TEXT("MaterialInstance"));
				Entries.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Entries.Num());
		Result->SetNumberField(TEXT("total"), Total);
		Result->SetArrayField(TEXT("materials"), Entries);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// get_material
// ============================================================
class FTool_GetMaterial : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_material");
		Info.Description = TEXT("Get detailed info about a Material or MaterialInstance.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Material name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: name"));

		// Try as UMaterial
		FString LoadError;
		UMaterial* Material = LoadMaterialByName(Name, LoadError);
		if (Material)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Material->GetName());
			Result->SetStringField(TEXT("path"), Material->GetPathName());
			Result->SetStringField(TEXT("type"), TEXT("Material"));

			if (const UEnum* DomainEnum = StaticEnum<EMaterialDomain>())
				Result->SetStringField(TEXT("domain"), DomainEnum->GetNameStringByValue((int64)Material->MaterialDomain));
			if (const UEnum* BlendEnum = StaticEnum<EBlendMode>())
				Result->SetStringField(TEXT("blendMode"), BlendEnum->GetNameStringByValue((int64)Material->BlendMode));

			TArray<TSharedPtr<FJsonValue>> ShadingModels;
			FMaterialShadingModelField SMField = Material->GetShadingModels();
			if (const UEnum* SMEnum = StaticEnum<EMaterialShadingModel>())
			{
				for (int32 i = 0; i < SMEnum->NumEnums() - 1; ++i)
				{
					EMaterialShadingModel SM = (EMaterialShadingModel)SMEnum->GetValueByIndex(i);
					if (SMField.HasShadingModel(SM))
						ShadingModels.Add(MakeShared<FJsonValueString>(SMEnum->GetNameStringByIndex(i)));
				}
			}
			Result->SetArrayField(TEXT("shadingModels"), ShadingModels);
			Result->SetBoolField(TEXT("twoSided"), Material->IsTwoSided());

			auto Expressions = Material->GetExpressions();
			Result->SetNumberField(TEXT("expressionCount"), Expressions.Num());

			// Parameters
			TArray<TSharedPtr<FJsonValue>> Parameters;
			for (UMaterialExpression* Expr : Expressions)
			{
				if (!Expr) continue;
				TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
				bool bIsParam = false;

				if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
				{
					bIsParam = true;
					ParamObj->SetStringField(TEXT("name"), SP->ParameterName.ToString());
					ParamObj->SetStringField(TEXT("type"), TEXT("Scalar"));
					ParamObj->SetStringField(TEXT("group"), SP->Group.ToString());
					ParamObj->SetNumberField(TEXT("defaultValue"), SP->DefaultValue);
				}
				else if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr))
				{
					bIsParam = true;
					ParamObj->SetStringField(TEXT("name"), VP->ParameterName.ToString());
					ParamObj->SetStringField(TEXT("type"), TEXT("Vector"));
					ParamObj->SetStringField(TEXT("group"), VP->Group.ToString());
					TSharedRef<FJsonObject> DefVal = MakeShared<FJsonObject>();
					DefVal->SetNumberField(TEXT("r"), VP->DefaultValue.R);
					DefVal->SetNumberField(TEXT("g"), VP->DefaultValue.G);
					DefVal->SetNumberField(TEXT("b"), VP->DefaultValue.B);
					DefVal->SetNumberField(TEXT("a"), VP->DefaultValue.A);
					ParamObj->SetObjectField(TEXT("defaultValue"), DefVal);
				}
				else if (auto* TP = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
				{
					bIsParam = true;
					ParamObj->SetStringField(TEXT("name"), TP->ParameterName.ToString());
					ParamObj->SetStringField(TEXT("type"), TEXT("Texture"));
					ParamObj->SetStringField(TEXT("group"), TP->Group.ToString());
					if (TP->Texture) ParamObj->SetStringField(TEXT("defaultValue"), TP->Texture->GetPathName());
				}
				else if (auto* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
				{
					bIsParam = true;
					ParamObj->SetStringField(TEXT("name"), SSP->ParameterName.ToString());
					ParamObj->SetStringField(TEXT("type"), TEXT("StaticSwitch"));
					ParamObj->SetStringField(TEXT("group"), SSP->Group.ToString());
					ParamObj->SetBoolField(TEXT("defaultValue"), SSP->DefaultValue);
				}

				if (bIsParam)
					Parameters.Add(MakeShared<FJsonValueObject>(ParamObj));
			}
			Result->SetArrayField(TEXT("parameters"), Parameters);

			// Referenced textures
			TArray<TSharedPtr<FJsonValue>> ReferencedTextures;
			for (const TObjectPtr<UObject>& TexObj : Material->GetReferencedTextures())
			{
				if (TexObj)
					ReferencedTextures.Add(MakeShared<FJsonValueString>(TexObj->GetPathName()));
			}
			Result->SetArrayField(TEXT("referencedTextures"), ReferencedTextures);

			int32 GraphNodeCount = Material->MaterialGraph ? Material->MaterialGraph->Nodes.Num() : 0;
			Result->SetNumberField(TEXT("graphNodeCount"), GraphNodeCount);

			// Usage flags
			TSharedRef<FJsonObject> UsageFlags = MakeShared<FJsonObject>();
			UsageFlags->SetBoolField(TEXT("bUsedWithSkeletalMesh"), Material->bUsedWithSkeletalMesh != 0);
			UsageFlags->SetBoolField(TEXT("bUsedWithMorphTargets"), Material->bUsedWithMorphTargets != 0);
			UsageFlags->SetBoolField(TEXT("bUsedWithNiagaraSprites"), Material->bUsedWithNiagaraSprites != 0);
			UsageFlags->SetBoolField(TEXT("bUsedWithParticleSprites"), Material->bUsedWithParticleSprites != 0);
			UsageFlags->SetBoolField(TEXT("bUsedWithStaticLighting"), Material->bUsedWithStaticLighting != 0);
			Result->SetObjectField(TEXT("usageFlags"), UsageFlags);

			Result->SetNumberField(TEXT("opacityMaskClipValue"), Material->OpacityMaskClipValue);
			Result->SetBoolField(TEXT("ditheredLODTransition"), Material->DitheredLODTransition != 0);

			int32 TextureSampleCount = 0;
			for (UMaterialExpression* Expr : Expressions)
			{
				if (Expr && Expr->IsA<UMaterialExpressionTextureSample>()) TextureSampleCount++;
			}
			Result->SetNumberField(TEXT("textureSampleCount"), TextureSampleCount);

			return FMCPToolResult::Ok(Result);
		}

		// Try as MaterialInstance
		FString MILoadError;
		UMaterialInstanceConstant* MI = LoadMaterialInstanceByName(Name, MILoadError);
		if (MI)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), MI->GetName());
			Result->SetStringField(TEXT("path"), MI->GetPathName());
			Result->SetStringField(TEXT("type"), TEXT("MaterialInstance"));

			if (MI->Parent)
			{
				Result->SetStringField(TEXT("parent"), MI->Parent->GetName());
				Result->SetStringField(TEXT("parentPath"), MI->Parent->GetPathName());
			}

			TArray<TSharedPtr<FJsonValue>> OverriddenParams;
			for (const FScalarParameterValue& Param : MI->ScalarParameterValues)
			{
				TSharedRef<FJsonObject> PObj = MakeShared<FJsonObject>();
				PObj->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
				PObj->SetStringField(TEXT("type"), TEXT("Scalar"));
				PObj->SetNumberField(TEXT("value"), Param.ParameterValue);
				OverriddenParams.Add(MakeShared<FJsonValueObject>(PObj));
			}
			for (const FVectorParameterValue& Param : MI->VectorParameterValues)
			{
				TSharedRef<FJsonObject> PObj = MakeShared<FJsonObject>();
				PObj->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
				PObj->SetStringField(TEXT("type"), TEXT("Vector"));
				TSharedRef<FJsonObject> Val = MakeShared<FJsonObject>();
				Val->SetNumberField(TEXT("r"), Param.ParameterValue.R);
				Val->SetNumberField(TEXT("g"), Param.ParameterValue.G);
				Val->SetNumberField(TEXT("b"), Param.ParameterValue.B);
				Val->SetNumberField(TEXT("a"), Param.ParameterValue.A);
				PObj->SetObjectField(TEXT("value"), Val);
				OverriddenParams.Add(MakeShared<FJsonValueObject>(PObj));
			}
			for (const FTextureParameterValue& Param : MI->TextureParameterValues)
			{
				TSharedRef<FJsonObject> PObj = MakeShared<FJsonObject>();
				PObj->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
				PObj->SetStringField(TEXT("type"), TEXT("Texture"));
				PObj->SetStringField(TEXT("value"), Param.ParameterValue ? Param.ParameterValue->GetPathName() : TEXT("None"));
				OverriddenParams.Add(MakeShared<FJsonValueObject>(PObj));
			}
			for (const FStaticSwitchParameter& Param : MI->GetStaticParameters().StaticSwitchParameters)
			{
				TSharedRef<FJsonObject> PObj = MakeShared<FJsonObject>();
				PObj->SetStringField(TEXT("name"), Param.ParameterInfo.Name.ToString());
				PObj->SetStringField(TEXT("type"), TEXT("StaticSwitch"));
				PObj->SetBoolField(TEXT("value"), Param.Value);
				PObj->SetBoolField(TEXT("overridden"), Param.bOverride);
				OverriddenParams.Add(MakeShared<FJsonValueObject>(PObj));
			}
			Result->SetArrayField(TEXT("overriddenParameters"), OverriddenParams);
			return FMCPToolResult::Ok(Result);
		}

		return FMCPToolResult::Error(FString::Printf(TEXT("Material or MaterialInstance '%s' not found. Use list_materials to see available assets."), *Name));
	}
};

// ============================================================
// get_material_graph
// ============================================================
class FTool_GetMaterialGraph : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_material_graph");
		Info.Description = TEXT("Get the serialized node graph for a material.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Material name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: name"));

		FString LoadError;
		UMaterial* Material = LoadMaterialByName(Name, LoadError);
		if (!Material) return FMCPToolResult::Error(LoadError);

		EnsureMaterialGraph(Material);
		if (!Material->MaterialGraph)
			return FMCPToolResult::Error(TEXT("Could not build MaterialGraph for this material"));

		TSharedPtr<FJsonObject> GraphJson = MCPHelpers::SerializeGraph(Material->MaterialGraph);
		if (!GraphJson.IsValid())
			return FMCPToolResult::Error(TEXT("Failed to serialize material graph"));

		GraphJson->SetStringField(TEXT("material"), Material->GetName());
		GraphJson->SetStringField(TEXT("materialPath"), Material->GetPathName());
		return FMCPToolResult::Ok(GraphJson.ToSharedRef());
	}
};

// ============================================================
// describe_material
// ============================================================
class FTool_DescribeMaterial : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("describe_material");
		Info.Description = TEXT("Human-readable description of a material's node chains by tracing from root inputs.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		if (MaterialName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: material"));

		FString LoadError;
		UMaterial* Material = LoadMaterialByName(MaterialName, LoadError);
		if (!Material) return FMCPToolResult::Error(LoadError);

		EnsureMaterialGraph(Material);
		if (!Material->MaterialGraph)
			return FMCPToolResult::Error(TEXT("Could not build MaterialGraph for this material"));

		// Recursive pin tracer
		TFunction<FString(UEdGraphPin*, int32)> TracePin = [&TracePin](UEdGraphPin* Pin, int32 Depth) -> FString
		{
			if (!Pin || Depth > 10) return TEXT("(unknown)");
			if (Pin->LinkedTo.Num() == 0)
			{
				if (!Pin->DefaultValue.IsEmpty())
					return FString::Printf(TEXT("(default: %s)"), *Pin->DefaultValue);
				return TEXT("(unconnected)");
			}

			TArray<FString> Sources;
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
				UEdGraphNode* SourceNode = LinkedPin->GetOwningNode();
				FString NodeDesc;

				if (UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(SourceNode))
				{
					UMaterialExpression* Expr = MatNode->MaterialExpression;
					if (!Expr)
						NodeDesc = TEXT("(null expression)");
					else if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr))
						NodeDesc = FString::Printf(TEXT("ScalarParam \"%s\" (default: %.4f)"), *SP->ParameterName.ToString(), SP->DefaultValue);
					else if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr))
						NodeDesc = FString::Printf(TEXT("VectorParam \"%s\" (default: R=%.2f G=%.2f B=%.2f A=%.2f)"),
							*VP->ParameterName.ToString(), VP->DefaultValue.R, VP->DefaultValue.G, VP->DefaultValue.B, VP->DefaultValue.A);
					else if (auto* TP = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr))
						NodeDesc = FString::Printf(TEXT("TextureParam \"%s\" (%s)"), *TP->ParameterName.ToString(), TP->Texture ? *TP->Texture->GetName() : TEXT("None"));
					else if (auto* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr))
						NodeDesc = FString::Printf(TEXT("StaticSwitchParam \"%s\" (default: %s)"), *SSP->ParameterName.ToString(), SSP->DefaultValue ? TEXT("true") : TEXT("false"));
					else if (auto* SC = Cast<UMaterialExpressionConstant>(Expr))
						NodeDesc = FString::Printf(TEXT("Constant(%.4f)"), SC->R);
					else if (auto* C3 = Cast<UMaterialExpressionConstant3Vector>(Expr))
						NodeDesc = FString::Printf(TEXT("Constant3(R=%.2f G=%.2f B=%.2f)"), C3->Constant.R, C3->Constant.G, C3->Constant.B);
					else if (auto* C4 = Cast<UMaterialExpressionConstant4Vector>(Expr))
						NodeDesc = FString::Printf(TEXT("Constant4(R=%.2f G=%.2f B=%.2f A=%.2f)"), C4->Constant.R, C4->Constant.G, C4->Constant.B, C4->Constant.A);
					else if (auto* TS = Cast<UMaterialExpressionTextureSample>(Expr))
						NodeDesc = FString::Printf(TEXT("TextureSample(%s)"), TS->Texture ? *TS->Texture->GetName() : TEXT("None"));
					else if (auto* MFC = Cast<UMaterialExpressionMaterialFunctionCall>(Expr))
						NodeDesc = FString::Printf(TEXT("FunctionCall(%s)"), MFC->MaterialFunction ? *MFC->MaterialFunction->GetName() : TEXT("None"));
					else
						NodeDesc = Expr->GetClass()->GetName();

					TArray<FString> InputDescs;
					for (UEdGraphPin* InputPin : SourceNode->Pins)
					{
						if (!InputPin || InputPin->Direction != EGPD_Input || InputPin->LinkedTo.Num() == 0) continue;
						InputDescs.Add(TracePin(InputPin, Depth + 1));
					}
					if (InputDescs.Num() > 0)
						NodeDesc += TEXT(" <- (") + FString::Join(InputDescs, TEXT(", ")) + TEXT(")");
				}
				else
				{
					NodeDesc = SourceNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				}
				Sources.Add(NodeDesc);
			}
			return Sources.Num() == 1 ? Sources[0] : TEXT("(") + FString::Join(Sources, TEXT(", ")) + TEXT(")");
		};

		// Find root node
		UMaterialGraphNode_Root* RootNode = nullptr;
		for (UEdGraphNode* Node : Material->MaterialGraph->Nodes)
		{
			RootNode = Cast<UMaterialGraphNode_Root>(Node);
			if (RootNode) break;
		}
		if (!RootNode)
			return FMCPToolResult::Error(TEXT("Could not find root node in material graph"));

		TArray<TSharedPtr<FJsonValue>> InputDescriptions;
		FString TextDesc;

		for (UEdGraphPin* Pin : RootNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input) continue;
			FString PinName = Pin->PinName.ToString();
			FString Description = Pin->LinkedTo.Num() == 0 ? TEXT("(unconnected)") : TracePin(Pin, 0);

			TSharedRef<FJsonObject> InputObj = MakeShared<FJsonObject>();
			InputObj->SetStringField(TEXT("input"), PinName);
			InputObj->SetStringField(TEXT("chain"), Description);
			InputObj->SetBoolField(TEXT("connected"), Pin->LinkedTo.Num() > 0);
			InputDescriptions.Add(MakeShared<FJsonValueObject>(InputObj));

			if (Pin->LinkedTo.Num() > 0)
				TextDesc += FString::Printf(TEXT("%s <- %s\n"), *PinName, *Description);
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("material"), Material->GetName());
		Result->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Result->SetArrayField(TEXT("inputs"), InputDescriptions);
		if (!TextDesc.IsEmpty())
			Result->SetStringField(TEXT("description"), TextDesc);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// search_materials
// ============================================================
class FTool_SearchMaterials : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("search_materials");
		Info.Description = TEXT("Search materials by name, expression class, or parameter name.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("query"), TEXT("string"), TEXT("Search query"), true});
		Info.Parameters.Add({TEXT("maxResults"), TEXT("number"), TEXT("Max results (default 50)"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Query = Params->GetStringField(TEXT("query"));
		if (Query.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: query"));

		int32 MaxResults = 50;
		double MaxD = 0;
		if (Params->TryGetNumberField(TEXT("maxResults"), MaxD)) MaxResults = FMath::Clamp((int32)MaxD, 1, 200);

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> MatAssets;
		Registry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), MatAssets, false);

		TArray<TSharedPtr<FJsonValue>> Results;

		for (const FAssetData& Asset : MatAssets)
		{
			if (Results.Num() >= MaxResults) break;
			FString MatName = Asset.AssetName.ToString();
			bool bNameMatch = MatName.Contains(Query, ESearchCase::IgnoreCase);

			UMaterial* Material = Cast<UMaterial>(const_cast<FAssetData&>(Asset).GetAsset());
			if (!Material) continue;

			if (bNameMatch)
			{
				TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetStringField(TEXT("material"), MatName);
				R->SetStringField(TEXT("materialPath"), Asset.PackageName.ToString());
				R->SetStringField(TEXT("matchType"), TEXT("materialName"));
				Results.Add(MakeShared<FJsonValueObject>(R));
			}

			auto Expressions = Material->GetExpressions();
			for (UMaterialExpression* Expr : Expressions)
			{
				if (!Expr || Results.Num() >= MaxResults) continue;
				FString ExprDesc = Expr->GetDescription();
				FString ExprClass = Expr->GetClass()->GetName();
				FString ParamName;
				if (auto* SP = Cast<UMaterialExpressionScalarParameter>(Expr)) ParamName = SP->ParameterName.ToString();
				else if (auto* VP = Cast<UMaterialExpressionVectorParameter>(Expr)) ParamName = VP->ParameterName.ToString();
				else if (auto* TP = Cast<UMaterialExpressionTextureSampleParameter2D>(Expr)) ParamName = TP->ParameterName.ToString();
				else if (auto* SSP = Cast<UMaterialExpressionStaticSwitchParameter>(Expr)) ParamName = SSP->ParameterName.ToString();

				bool bMatch = ExprDesc.Contains(Query, ESearchCase::IgnoreCase) ||
					ExprClass.Contains(Query, ESearchCase::IgnoreCase) ||
					(!ParamName.IsEmpty() && ParamName.Contains(Query, ESearchCase::IgnoreCase));

				if (bMatch)
				{
					TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
					R->SetStringField(TEXT("material"), MatName);
					R->SetStringField(TEXT("materialPath"), Asset.PackageName.ToString());
					R->SetStringField(TEXT("matchType"), TEXT("expression"));
					R->SetStringField(TEXT("expressionClass"), ExprClass);
					if (!ExprDesc.IsEmpty()) R->SetStringField(TEXT("description"), ExprDesc);
					if (!ParamName.IsEmpty()) R->SetStringField(TEXT("parameterName"), ParamName);
					Results.Add(MakeShared<FJsonValueObject>(R));
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
// find_material_references
// ============================================================
class FTool_FindMaterialReferences : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("find_material_references");
		Info.Description = TEXT("Find assets that reference a given material.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("material"), TEXT("string"), TEXT("Material name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MaterialName = Params->GetStringField(TEXT("material"));
		if (MaterialName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: material"));

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

		// Find package path
		FString PackagePath;
		{
			TArray<FAssetData> Assets;
			Registry.GetAssetsByClass(UMaterial::StaticClass()->GetClassPathName(), Assets, false);
			for (const FAssetData& Asset : Assets)
			{
				if (Asset.AssetName.ToString() == MaterialName || Asset.PackageName.ToString() == MaterialName)
				{
					PackagePath = Asset.PackageName.ToString();
					break;
				}
			}
		}
		if (PackagePath.IsEmpty())
		{
			TArray<FAssetData> Assets;
			Registry.GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), Assets, false);
			for (const FAssetData& Asset : Assets)
			{
				if (Asset.AssetName.ToString() == MaterialName || Asset.PackageName.ToString() == MaterialName)
				{
					PackagePath = Asset.PackageName.ToString();
					break;
				}
			}
		}
		if (PackagePath.IsEmpty())
			return FMCPToolResult::Error(FString::Printf(TEXT("Material '%s' not found."), *MaterialName));

		TArray<FName> Referencers;
		Registry.GetReferencers(FName(*PackagePath), Referencers);

		TArray<TSharedPtr<FJsonValue>> RefArray;
		for (const FName& Ref : Referencers)
		{
			FString RefStr = Ref.ToString();
			if (RefStr == PackagePath) continue;
			RefArray.Add(MakeShared<FJsonValueString>(RefStr));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("material"), MaterialName);
		Result->SetStringField(TEXT("packagePath"), PackagePath);
		Result->SetNumberField(TEXT("totalReferencers"), RefArray.Num());
		Result->SetArrayField(TEXT("referencers"), RefArray);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// list_material_functions
// ============================================================
class FTool_ListMaterialFunctions : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("list_material_functions");
		Info.Description = TEXT("List MaterialFunction assets.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Name/path filter"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Filter;
		Params->TryGetStringField(TEXT("filter"), Filter);

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> MFAssets;
		Registry.GetAssetsByClass(UMaterialFunction::StaticClass()->GetClassPathName(), MFAssets, false);

		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FAssetData& Asset : MFAssets)
		{
			FString Name = Asset.AssetName.ToString();
			FString Path = Asset.PackageName.ToString();
			if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase) && !Path.Contains(Filter, ESearchCase::IgnoreCase))
				continue;
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Name);
			Entry->SetStringField(TEXT("path"), Path);
			Entries.Add(MakeShared<FJsonValueObject>(Entry));
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Entries.Num());
		Result->SetNumberField(TEXT("total"), MFAssets.Num());
		Result->SetArrayField(TEXT("functions"), Entries);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// get_material_function
// ============================================================
class FTool_GetMaterialFunction : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_material_function");
		Info.Description = TEXT("Get detailed info about a MaterialFunction including inputs, outputs, and expressions.");
		Info.Annotations.Category = TEXT("Material");
		Info.Annotations.bReadOnly = true;
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("MaterialFunction name or path"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: name"));

		FString LoadError;
		UMaterialFunction* MF = LoadMaterialFunctionByName(Name, LoadError);
		if (!MF) return FMCPToolResult::Error(LoadError);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("name"), MF->GetName());
		Result->SetStringField(TEXT("path"), MF->GetPathName());
		Result->SetStringField(TEXT("description"), MF->GetDescription());

		auto Expressions = MF->GetExpressions();
		Result->SetNumberField(TEXT("expressionCount"), Expressions.Num());

		TArray<TSharedPtr<FJsonValue>> Inputs, Outputs, ExpressionList;
		for (UMaterialExpression* Expr : Expressions)
		{
			if (!Expr) continue;
			if (auto* FI = Cast<UMaterialExpressionFunctionInput>(Expr))
			{
				TSharedRef<FJsonObject> InputObj = MakeShared<FJsonObject>();
				InputObj->SetStringField(TEXT("name"), FI->InputName.ToString());
				InputObj->SetStringField(TEXT("type"), TEXT("FunctionInput"));
				InputObj->SetNumberField(TEXT("posX"), FI->MaterialExpressionEditorX);
				InputObj->SetNumberField(TEXT("posY"), FI->MaterialExpressionEditorY);
				Inputs.Add(MakeShared<FJsonValueObject>(InputObj));
			}
			else if (auto* FO = Cast<UMaterialExpressionFunctionOutput>(Expr))
			{
				TSharedRef<FJsonObject> OutputObj = MakeShared<FJsonObject>();
				OutputObj->SetStringField(TEXT("name"), FO->OutputName.ToString());
				OutputObj->SetStringField(TEXT("type"), TEXT("FunctionOutput"));
				OutputObj->SetNumberField(TEXT("posX"), FO->MaterialExpressionEditorX);
				OutputObj->SetNumberField(TEXT("posY"), FO->MaterialExpressionEditorY);
				Outputs.Add(MakeShared<FJsonValueObject>(OutputObj));
			}

			// Basic expression info
			TSharedRef<FJsonObject> ExprJson = MakeShared<FJsonObject>();
			ExprJson->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
			ExprJson->SetStringField(TEXT("description"), Expr->GetDescription());
			ExprJson->SetNumberField(TEXT("posX"), Expr->MaterialExpressionEditorX);
			ExprJson->SetNumberField(TEXT("posY"), Expr->MaterialExpressionEditorY);
			ExpressionList.Add(MakeShared<FJsonValueObject>(ExprJson));
		}

		Result->SetArrayField(TEXT("inputs"), Inputs);
		Result->SetArrayField(TEXT("outputs"), Outputs);
		Result->SetArrayField(TEXT("expressions"), ExpressionList);

		if (MF->MaterialGraph)
		{
			TSharedPtr<FJsonObject> GraphJson = MCPHelpers::SerializeGraph(MF->MaterialGraph);
			if (GraphJson.IsValid())
				Result->SetObjectField(TEXT("graph"), GraphJson);
		}

		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterMaterialReadTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_ListMaterials>());
		R.Register(MakeShared<FTool_GetMaterial>());
		R.Register(MakeShared<FTool_GetMaterialGraph>());
		R.Register(MakeShared<FTool_DescribeMaterial>());
		R.Register(MakeShared<FTool_SearchMaterials>());
		R.Register(MakeShared<FTool_FindMaterialReferences>());
		R.Register(MakeShared<FTool_ListMaterialFunctions>());
		R.Register(MakeShared<FTool_GetMaterialFunction>());
	}
}
