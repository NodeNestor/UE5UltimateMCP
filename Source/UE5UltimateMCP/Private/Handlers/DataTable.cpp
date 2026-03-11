// Data Table tools for UltimateMCP
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"

#include "Engine/DataTable.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"

// ─────────────────────────────────────────────────────────────
// create_data_table
// ─────────────────────────────────────────────────────────────
class FTool_CreateDataTable : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_data_table");
		Info.Description = TEXT("Create a new Data Table asset with a specified row struct.");
		Info.Annotations.Category = TEXT("DataTable");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("name"), TEXT("string"), TEXT("Name for the Data Table asset"), true);
		AddParam(TEXT("row_struct"), TEXT("string"), TEXT("Row struct path or name (e.g. '/Script/Engine.DataTableRowHandle' or custom struct path)"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		FString RowStructName = Params->GetStringField(TEXT("row_struct"));

		if (Name.IsEmpty() || RowStructName.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("Both 'name' and 'row_struct' are required."));
		}

		// Find the struct
		UScriptStruct* RowStruct = FindObject<UScriptStruct>(nullptr, *RowStructName);
		if (!RowStruct)
		{
			// Try common short names
			RowStruct = FindFirstObject<UScriptStruct>(*RowStructName, EFindFirstObjectOptions::NativeFirst);
		}
		if (!RowStruct)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Row struct '%s' not found. Use the full path (e.g. '/Script/ModuleName.FMyStruct')."), *RowStructName));
		}

		// Verify it derives from FTableRowBase
		bool bValidRowStruct = false;
		const UScriptStruct* TestStruct = RowStruct;
		while (TestStruct)
		{
			if (TestStruct->GetFName() == FName("TableRowBase"))
			{
				bValidRowStruct = true;
				break;
			}
			TestStruct = Cast<UScriptStruct>(TestStruct->GetSuperStruct());
		}
		if (!bValidRowStruct)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Struct '%s' does not derive from FTableRowBase."), *RowStructName));
		}

		FString PackagePath = FString::Printf(TEXT("/Game/Data/%s"), *Name);
		FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package at '%s'."), *PackageName));
		}

		UDataTable* DataTable = NewObject<UDataTable>(Package, *Name, RF_Public | RF_Standalone);
		if (!DataTable)
		{
			return FMCPToolResult::Error(TEXT("Failed to create UDataTable."));
		}

		DataTable->RowStruct = RowStruct;

		FAssetRegistryModule::AssetCreated(DataTable);
		DataTable->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		UPackage::SavePackage(Package, DataTable, *FilePath, SaveArgs);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), PackagePath);
		Result->SetStringField(TEXT("name"), Name);
		Result->SetStringField(TEXT("row_struct"), RowStruct->GetPathName());
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_data_table_row
// ─────────────────────────────────────────────────────────────
class FTool_AddDataTableRow : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_data_table_row");
		Info.Description = TEXT("Add a row to a Data Table using JSON values.");
		Info.Annotations.Category = TEXT("DataTable");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("table"), TEXT("string"), TEXT("Asset path of the Data Table"), true);
		AddParam(TEXT("row_name"), TEXT("string"), TEXT("Name for the new row"), true);
		AddParam(TEXT("values"), TEXT("object"), TEXT("JSON object with column values matching the struct fields"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TablePath = Params->GetStringField(TEXT("table"));
		FString RowName = Params->GetStringField(TEXT("row_name"));
		const TSharedPtr<FJsonObject>& Values = Params->GetObjectField(TEXT("values"));

		UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
		if (!DataTable)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Data Table not found at '%s'."), *TablePath));
		}

		if (!DataTable->RowStruct)
		{
			return FMCPToolResult::Error(TEXT("Data Table has no row struct defined."));
		}

		// Check if row already exists
		if (DataTable->FindRowUnchecked(FName(*RowName)))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' already exists."), *RowName));
		}

		// Build a JSON string for import
		// Convert the values object to a CSV-like format the data table can import
		FString JsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
		FJsonSerializer::Serialize(Values.ToSharedRef(), Writer);

		// Use AddRow with JSON import
		// Allocate row data
		uint8* RowData = (uint8*)FMemory::Malloc(DataTable->RowStruct->GetStructureSize());
		DataTable->RowStruct->InitializeStruct(RowData);

		// Iterate through the Values JSON and set struct properties
		const UScriptStruct* RowStruct = DataTable->RowStruct;
		for (const auto& Pair : Values->Values)
		{
			FProperty* Prop = RowStruct->FindPropertyByName(FName(*Pair.Key));
			if (!Prop)
			{
				continue;
			}

			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(RowData);

			if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Number)
				{
					if (NumProp->IsFloatingPoint())
					{
						NumProp->SetFloatingPointPropertyValue(PropAddr, Pair.Value->AsNumber());
					}
					else
					{
						NumProp->SetIntPropertyValue(PropAddr, (int64)Pair.Value->AsNumber());
					}
				}
			}
			else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::String)
				{
					StrProp->SetPropertyValue(PropAddr, Pair.Value->AsString());
				}
			}
			else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::String)
				{
					NameProp->SetPropertyValue(PropAddr, FName(*Pair.Value->AsString()));
				}
			}
			else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Boolean)
				{
					BoolProp->SetPropertyValue(PropAddr, Pair.Value->AsBool());
				}
			}
			else if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::String)
				{
					TextProp->SetPropertyValue(PropAddr, FText::FromString(Pair.Value->AsString()));
				}
			}
			else
			{
				// Fallback: try to import from string
				FString ValueStr;
				if (Pair.Value->TryGetString(ValueStr))
				{
					Prop->ImportText_Direct(*ValueStr, PropAddr, nullptr, PPF_None);
				}
			}
		}

		DataTable->AddRow(FName(*RowName), *reinterpret_cast<FTableRowBase*>(RowData));

		DataTable->RowStruct->DestroyStruct(RowData);
		FMemory::Free(RowData);

		DataTable->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("table"), TablePath);
		Result->SetStringField(TEXT("row_name"), RowName);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// get_data_table_rows
// ─────────────────────────────────────────────────────────────
class FTool_GetDataTableRows : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_data_table_rows");
		Info.Description = TEXT("Get all rows from a Data Table as JSON.");
		Info.Annotations.Category = TEXT("DataTable");
		Info.Annotations.bReadOnly = true;

		FMCPParamSchema P;
		P.Name = TEXT("table"); P.Type = TEXT("string");
		P.Description = TEXT("Asset path of the Data Table");
		P.bRequired = true;
		Info.Parameters.Add(P);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TablePath = Params->GetStringField(TEXT("table"));

		UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
		if (!DataTable)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Data Table not found at '%s'."), *TablePath));
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("table"), TablePath);
		Result->SetStringField(TEXT("row_struct"), DataTable->RowStruct ? DataTable->RowStruct->GetName() : TEXT("None"));

		TArray<FName> RowNames = DataTable->GetRowNames();
		Result->SetNumberField(TEXT("row_count"), RowNames.Num());

		TArray<TSharedPtr<FJsonValue>> RowsArray;
		const UScriptStruct* RowStruct = DataTable->RowStruct;

		for (const FName& RowName : RowNames)
		{
			uint8* RowData = DataTable->FindRowUnchecked(RowName);
			if (!RowData || !RowStruct)
			{
				continue;
			}

			TSharedPtr<FJsonObject> RowObj = MakeShared<FJsonObject>();
			RowObj->SetStringField(TEXT("_row_name"), RowName.ToString());

			// Export each property
			for (TFieldIterator<FProperty> PropIt(RowStruct); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				void* PropAddr = Prop->ContainerPtrToValuePtr<void>(RowData);

				FString ValueStr;
				Prop->ExportTextItem_Direct(ValueStr, PropAddr, nullptr, nullptr, PPF_None);
				RowObj->SetStringField(Prop->GetName(), ValueStr);
			}

			RowsArray.Add(MakeShared<FJsonValueObject>(RowObj));
		}

		Result->SetArrayField(TEXT("rows"), RowsArray);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// remove_data_table_row
// ─────────────────────────────────────────────────────────────
class FTool_RemoveDataTableRow : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("remove_data_table_row");
		Info.Description = TEXT("Remove a row from a Data Table by name.");
		Info.Annotations.Category = TEXT("DataTable");
		Info.Annotations.bDestructive = true;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("table"), TEXT("string"), TEXT("Asset path of the Data Table"), true);
		AddParam(TEXT("row_name"), TEXT("string"), TEXT("Name of the row to remove"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TablePath = Params->GetStringField(TEXT("table"));
		FString RowName = Params->GetStringField(TEXT("row_name"));

		UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
		if (!DataTable)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Data Table not found at '%s'."), *TablePath));
		}

		if (!DataTable->FindRowUnchecked(FName(*RowName)))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Row '%s' not found in table."), *RowName));
		}

		DataTable->RemoveRow(FName(*RowName));
		DataTable->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("table"), TablePath);
		Result->SetStringField(TEXT("removed_row"), RowName);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// import_csv_to_data_table
// ─────────────────────────────────────────────────────────────
class FTool_ImportCSVToDataTable : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("import_csv_to_data_table");
		Info.Description = TEXT("Import CSV data into an existing Data Table, replacing all current rows.");
		Info.Annotations.Category = TEXT("DataTable");
		Info.Annotations.bDestructive = true;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("table"), TEXT("string"), TEXT("Asset path of the Data Table"), true);
		AddParam(TEXT("csv_path"), TEXT("string"), TEXT("Absolute file path to the CSV file"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString TablePath = Params->GetStringField(TEXT("table"));
		FString CSVPath = Params->GetStringField(TEXT("csv_path"));

		UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
		if (!DataTable)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Data Table not found at '%s'."), *TablePath));
		}

		FString CSVContent;
		if (!FFileHelper::LoadFileToString(CSVContent, *CSVPath))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to read CSV file at '%s'."), *CSVPath));
		}

		TArray<FString> Errors = DataTable->CreateTableFromCSVString(CSVContent);
		if (Errors.Num() > 0)
		{
			FString ErrorStr;
			for (const FString& Err : Errors)
			{
				ErrorStr += Err + TEXT("\n");
			}
			return FMCPToolResult::Error(FString::Printf(TEXT("CSV import errors:\n%s"), *ErrorStr));
		}

		DataTable->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("table"), TablePath);
		Result->SetNumberField(TEXT("rows_imported"), DataTable->GetRowNames().Num());
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────
namespace UltimateMCPTools
{
	void RegisterDataTableTools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_CreateDataTable>());
		Registry.Register(MakeShared<FTool_AddDataTableRow>());
		Registry.Register(MakeShared<FTool_GetDataTableRows>());
		Registry.Register(MakeShared<FTool_RemoveDataTableRow>());
		Registry.Register(MakeShared<FTool_ImportCSVToDataTable>());
	}
}
