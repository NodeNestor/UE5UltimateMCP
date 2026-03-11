// UserTypes Tools — create struct/enum, add/remove struct properties
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/EnumEditorUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/StructureFactory.h"
#include "Factories/EnumFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// ============================================================
// create_struct
// ============================================================
class FTool_CreateStruct : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_struct");
		Info.Description = TEXT("Create a new UserDefinedStruct asset with optional initial properties.");
		Info.Annotations.Category = TEXT("UserTypes");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("assetPath"), TEXT("string"), TEXT("Full asset path (e.g. /Game/DataTypes/S_MyStruct)"), true});
		Info.Parameters.Add({TEXT("properties"), TEXT("array"), TEXT("Array of {name, type} objects"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString AssetPath = Params->GetStringField(TEXT("assetPath"));
		if (AssetPath.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: assetPath"));

		FString PackagePath, AssetName;
		int32 LastSlash;
		if (!AssetPath.FindLastChar('/', LastSlash))
			return FMCPToolResult::Error(TEXT("assetPath must be a full path (e.g. '/Game/DataTypes/S_MyStruct')"));
		PackagePath = AssetPath.Left(LastSlash);
		AssetName = AssetPath.Mid(LastSlash + 1);
		if (AssetName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Invalid asset name in assetPath"));

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UStructureFactory* Factory = NewObject<UStructureFactory>();
		UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UUserDefinedStruct::StaticClass(), Factory);
		if (!NewAsset)
			return FMCPToolResult::Error(TEXT("Failed to create UserDefinedStruct asset"));

		UUserDefinedStruct* NewStruct = Cast<UUserDefinedStruct>(NewAsset);
		if (!NewStruct)
			return FMCPToolResult::Error(TEXT("Created asset is not a UserDefinedStruct"));

		// Add properties
		int32 PropsAdded = 0;
		const TArray<TSharedPtr<FJsonValue>>* PropsArray = nullptr;
		if (Params->TryGetArrayField(TEXT("properties"), PropsArray) && PropsArray)
		{
			for (const TSharedPtr<FJsonValue>& PropVal : *PropsArray)
			{
				TSharedPtr<FJsonObject> PropObj = PropVal->AsObject();
				if (!PropObj) continue;

				FString PropName = PropObj->GetStringField(TEXT("name"));
				FString PropType = PropObj->GetStringField(TEXT("type"));
				if (PropName.IsEmpty() || PropType.IsEmpty()) continue;

				FEdGraphPinType PinType;
				FString TypeError;
				if (!MCPHelpers::ResolveTypeFromString(PropType, PinType, TypeError)) continue;

				TSet<FGuid> ExistingGuids;
				for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(NewStruct))
					ExistingGuids.Add(Var.VarGuid);

				if (FStructureEditorUtils::AddVariable(NewStruct, PinType))
				{
					for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(NewStruct))
					{
						if (!ExistingGuids.Contains(Var.VarGuid))
						{
							FStructureEditorUtils::RenameVariable(NewStruct, Var.VarGuid, PropName);
							break;
						}
					}
					PropsAdded++;
				}
			}
		}

		// Save
		UPackage* Package = NewStruct->GetPackage();
		FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, NewStruct, *PackageFilename, SaveArgs);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("assetName"), AssetName);
		Result->SetNumberField(TEXT("propertiesAdded"), PropsAdded);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// create_enum
// ============================================================
class FTool_CreateEnum : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_enum");
		Info.Description = TEXT("Create a new UserDefinedEnum asset with specified values.");
		Info.Annotations.Category = TEXT("UserTypes");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("assetPath"), TEXT("string"), TEXT("Full asset path (e.g. /Game/DataTypes/E_MyEnum)"), true});
		Info.Parameters.Add({TEXT("values"), TEXT("array"), TEXT("Array of enum value strings"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString AssetPath = Params->GetStringField(TEXT("assetPath"));
		if (AssetPath.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required field: assetPath"));

		FString PackagePath, AssetName;
		int32 LastSlash;
		if (!AssetPath.FindLastChar('/', LastSlash))
			return FMCPToolResult::Error(TEXT("assetPath must be a full path"));
		PackagePath = AssetPath.Left(LastSlash);
		AssetName = AssetPath.Mid(LastSlash + 1);

		const TArray<TSharedPtr<FJsonValue>>* ValuesArray = nullptr;
		if (!Params->TryGetArrayField(TEXT("values"), ValuesArray) || !ValuesArray || ValuesArray->Num() == 0)
			return FMCPToolResult::Error(TEXT("Missing or empty required field: values"));

		TArray<FString> EnumValues;
		for (const TSharedPtr<FJsonValue>& Val : *ValuesArray)
		{
			FString Str = Val->AsString();
			if (!Str.IsEmpty()) EnumValues.Add(Str);
		}
		if (EnumValues.Num() == 0)
			return FMCPToolResult::Error(TEXT("No valid enum values provided"));

		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UEnumFactory* Factory = NewObject<UEnumFactory>();
		UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UUserDefinedEnum::StaticClass(), Factory);
		if (!NewAsset)
			return FMCPToolResult::Error(TEXT("Failed to create UserDefinedEnum asset"));

		UUserDefinedEnum* NewEnum = Cast<UUserDefinedEnum>(NewAsset);
		if (!NewEnum)
			return FMCPToolResult::Error(TEXT("Created asset is not a UserDefinedEnum"));

		for (int32 i = 0; i < EnumValues.Num(); ++i)
		{
			FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(NewEnum);
			int32 NewIndex = NewEnum->NumEnums() - 2;
			FEnumEditorUtils::SetEnumeratorDisplayName(NewEnum, NewIndex, FText::FromString(EnumValues[i]));
		}

		UPackage* Package = NewEnum->GetPackage();
		FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, NewEnum, *PackageFilename, SaveArgs);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("assetName"), AssetName);
		Result->SetNumberField(TEXT("valueCount"), EnumValues.Num());
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// add_struct_property
// ============================================================
class FTool_AddStructProperty : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_struct_property");
		Info.Description = TEXT("Add a property to an existing UserDefinedStruct.");
		Info.Annotations.Category = TEXT("UserTypes");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("assetPath"), TEXT("string"), TEXT("Struct asset path"), true});
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Property name"), true});
		Info.Parameters.Add({TEXT("type"), TEXT("string"), TEXT("Property type"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString AssetPath = Params->GetStringField(TEXT("assetPath"));
		FString PropName = Params->GetStringField(TEXT("name"));
		FString PropType = Params->GetStringField(TEXT("type"));

		if (AssetPath.IsEmpty() || PropName.IsEmpty() || PropType.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: assetPath, name, type"));

		UUserDefinedStruct* Struct = LoadObject<UUserDefinedStruct>(nullptr, *AssetPath);
		if (!Struct)
		{
			FString FullPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
			Struct = LoadObject<UUserDefinedStruct>(nullptr, *FullPath);
		}
		if (!Struct)
			return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedStruct not found at '%s'"), *AssetPath));

		FEdGraphPinType PinType;
		FString TypeError;
		if (!MCPHelpers::ResolveTypeFromString(PropType, PinType, TypeError))
			return FMCPToolResult::Error(FString::Printf(TEXT("Cannot resolve type '%s': %s"), *PropType, *TypeError));

		TSet<FGuid> ExistingGuids;
		for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
			ExistingGuids.Add(Var.VarGuid);

		if (!FStructureEditorUtils::AddVariable(Struct, PinType))
			return FMCPToolResult::Error(TEXT("Failed to add property to struct"));

		for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
		{
			if (!ExistingGuids.Contains(Var.VarGuid))
			{
				FStructureEditorUtils::RenameVariable(Struct, Var.VarGuid, PropName);
				break;
			}
		}

		UPackage* Package = Struct->GetPackage();
		FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, Struct, *PackageFilename, SaveArgs);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("propertyName"), PropName);
		Result->SetStringField(TEXT("propertyType"), PropType);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// remove_struct_property
// ============================================================
class FTool_RemoveStructProperty : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("remove_struct_property");
		Info.Description = TEXT("Remove a property from a UserDefinedStruct.");
		Info.Annotations.Category = TEXT("UserTypes");
		Info.Annotations.bDestructive = true;
		Info.Parameters.Add({TEXT("assetPath"), TEXT("string"), TEXT("Struct asset path"), true});
		Info.Parameters.Add({TEXT("name"), TEXT("string"), TEXT("Property to remove"), true});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString AssetPath = Params->GetStringField(TEXT("assetPath"));
		FString PropName = Params->GetStringField(TEXT("name"));

		if (AssetPath.IsEmpty() || PropName.IsEmpty())
			return FMCPToolResult::Error(TEXT("Missing required fields: assetPath, name"));

		UUserDefinedStruct* Struct = LoadObject<UUserDefinedStruct>(nullptr, *AssetPath);
		if (!Struct)
		{
			FString FullPath = AssetPath + TEXT(".") + FPackageName::GetShortName(AssetPath);
			Struct = LoadObject<UUserDefinedStruct>(nullptr, *FullPath);
		}
		if (!Struct)
			return FMCPToolResult::Error(FString::Printf(TEXT("UserDefinedStruct not found at '%s'"), *AssetPath));

		FGuid TargetGuid;
		bool bFound = false;
		for (const FStructVariableDescription& Var : FStructureEditorUtils::GetVarDesc(Struct))
		{
			if (Var.FriendlyName == PropName || Var.VarName.ToString() == PropName)
			{
				TargetGuid = Var.VarGuid;
				bFound = true;
				break;
			}
		}
		if (!bFound)
			return FMCPToolResult::Error(FString::Printf(TEXT("Property '%s' not found in struct"), *PropName));

		if (!FStructureEditorUtils::RemoveVariable(Struct, TargetGuid))
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to remove property '%s'"), *PropName));

		UPackage* Package = Struct->GetPackage();
		FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bool bSaved = UPackage::SavePackage(Package, Struct, *PackageFilename, SaveArgs);

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("assetPath"), AssetPath);
		Result->SetStringField(TEXT("removedProperty"), PropName);
		Result->SetBoolField(TEXT("saved"), bSaved);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterUserTypeTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_CreateStruct>());
		R.Register(MakeShared<FTool_CreateEnum>());
		R.Register(MakeShared<FTool_AddStructProperty>());
		R.Register(MakeShared<FTool_RemoveStructProperty>());
	}
}
