// Validation Tools — validate blueprint, validate all blueprints with SEH protection
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Tools/MCPToolHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

// SEH wrapper defined externally
#if PLATFORM_WINDOWS
extern int32 TryCompileBlueprintSEH(UBlueprint* BP, EBlueprintCompileOptions Opts);
#endif

// Log capture device for intercepting compilation output
class FCompileLogCapture : public FOutputDevice
{
public:
	TArray<FString> CapturedErrors;
	TArray<FString> CapturedWarnings;

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		FString Msg(V);
		if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Fatal)
		{
			CapturedErrors.Add(Msg);
			return;
		}
		if (Verbosity == ELogVerbosity::Warning)
		{
			CapturedWarnings.Add(Msg);
		}
	}
};

// Helper: validate a single Blueprint
static TSharedRef<FJsonObject> ValidateSingleBlueprint(UBlueprint* BP, const FString& BlueprintName)
{
	FCompileLogCapture LogCapture;
	GLog->AddOutputDevice(&LogCapture);

	EBlueprintCompileOptions CompileOpts =
		EBlueprintCompileOptions::SkipSave |
		EBlueprintCompileOptions::SkipGarbageCollection |
		EBlueprintCompileOptions::SkipFiBSearchMetaUpdate;

	bool bCompileCrashed = false;

#if PLATFORM_WINDOWS
	int32 CompileResult = TryCompileBlueprintSEH(BP, CompileOpts);
	if (CompileResult != 0) bCompileCrashed = true;
#else
	FKismetEditorUtilities::CompileBlueprint(BP, CompileOpts, nullptr);
#endif

	GLog->RemoveOutputDevice(&LogCapture);

	TArray<TSharedPtr<FJsonValue>> ErrorsArr, WarningsArr;

	TArray<UEdGraph*> AllGraphs;
	BP->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || !Node->bHasCompilerMessage) continue;

			TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
			Msg->SetStringField(TEXT("graph"), Graph->GetName());
			Msg->SetStringField(TEXT("nodeId"), Node->NodeGuid.ToString());
			Msg->SetStringField(TEXT("nodeTitle"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			Msg->SetStringField(TEXT("message"), Node->ErrorMsg);

			if (Node->ErrorType == EMessageSeverity::Error)
			{
				Msg->SetStringField(TEXT("severity"), TEXT("error"));
				ErrorsArr.Add(MakeShared<FJsonValueObject>(Msg));
			}
			else
			{
				Msg->SetStringField(TEXT("severity"), TEXT("warning"));
				WarningsArr.Add(MakeShared<FJsonValueObject>(Msg));
			}
		}
	}

	for (const FString& LogErr : LogCapture.CapturedErrors)
	{
		TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("source"), TEXT("log"));
		Msg->SetStringField(TEXT("message"), LogErr);
		Msg->SetStringField(TEXT("severity"), TEXT("error"));
		ErrorsArr.Add(MakeShared<FJsonValueObject>(Msg));
	}
	for (const FString& LogWarn : LogCapture.CapturedWarnings)
	{
		TSharedRef<FJsonObject> Msg = MakeShared<FJsonObject>();
		Msg->SetStringField(TEXT("source"), TEXT("log"));
		Msg->SetStringField(TEXT("message"), LogWarn);
		Msg->SetStringField(TEXT("severity"), TEXT("warning"));
		WarningsArr.Add(MakeShared<FJsonValueObject>(Msg));
	}

	FString StatusStr;
	switch (BP->Status)
	{
		case BS_UpToDate: StatusStr = TEXT("UpToDate"); break;
		case BS_Dirty: StatusStr = TEXT("Dirty"); break;
		case BS_Error: StatusStr = TEXT("Error"); break;
		case BS_Unknown: StatusStr = TEXT("Unknown"); break;
		default: StatusStr = FString::Printf(TEXT("Status_%d"), (int32)BP->Status); break;
	}

	bool bIsValid = (BP->Status == BS_UpToDate) && ErrorsArr.Num() == 0;

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("blueprint"), BlueprintName);
	Result->SetStringField(TEXT("status"), StatusStr);
	Result->SetBoolField(TEXT("isValid"), bIsValid);
	Result->SetNumberField(TEXT("errorCount"), ErrorsArr.Num());
	Result->SetArrayField(TEXT("errors"), ErrorsArr);
	Result->SetNumberField(TEXT("warningCount"), WarningsArr.Num());
	Result->SetArrayField(TEXT("warnings"), WarningsArr);
	if (bCompileCrashed)
		Result->SetStringField(TEXT("compileWarning"), TEXT("Compilation crashed (SEH caught)"));
	return Result;
}

// ============================================================
// validate_blueprint
// ============================================================
class FTool_ValidateBlueprint : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("validate_blueprint");
		Info.Description = TEXT("Compile a Blueprint without saving and report errors and warnings.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
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

		TSharedRef<FJsonObject> Result = ValidateSingleBlueprint(BP, BlueprintName);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// validate_all_blueprints
// ============================================================
class FTool_ValidateAllBlueprints : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("validate_all_blueprints");
		Info.Description = TEXT("Bulk-validate multiple Blueprints by compiling them without saving. Returns only failures.");
		Info.Annotations.Category = TEXT("Blueprint");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.bExpensive = true;
		Info.Parameters.Add({TEXT("filter"), TEXT("string"), TEXT("Name/path filter"), false});
		Info.Parameters.Add({TEXT("countOnly"), TEXT("boolean"), TEXT("Only return count, don't compile"), false});
		Info.Parameters.Add({TEXT("offset"), TEXT("number"), TEXT("Start index for pagination"), false});
		Info.Parameters.Add({TEXT("limit"), TEXT("number"), TEXT("Max blueprints to validate"), false});
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Filter;
		Params->TryGetStringField(TEXT("filter"), Filter);
		bool bCountOnly = false;
		Params->TryGetBoolField(TEXT("countOnly"), bCountOnly);
		int32 Offset = 0, Limit = 0;
		double OffsetD = 0, LimitD = 0;
		if (Params->TryGetNumberField(TEXT("offset"), OffsetD)) Offset = (int32)OffsetD;
		if (Params->TryGetNumberField(TEXT("limit"), LimitD)) Limit = (int32)LimitD;

		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetData> AllBP;
		Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), AllBP, true);

		// Collect matching indices
		TArray<int32> MatchingIndices;
		for (int32 i = 0; i < AllBP.Num(); i++)
		{
			if (!Filter.IsEmpty())
			{
				FString Name = AllBP[i].AssetName.ToString();
				FString Path = AllBP[i].PackageName.ToString();
				if (!Path.Contains(Filter) && !Name.Contains(Filter)) continue;
			}
			MatchingIndices.Add(i);
		}

		int32 TotalMatching = MatchingIndices.Num();

		if (bCountOnly)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("totalMatching"), TotalMatching);
			if (!Filter.IsEmpty()) Result->SetStringField(TEXT("filter"), Filter);
			return FMCPToolResult::Ok(Result);
		}

		int32 StartIdx = FMath::Clamp(Offset, 0, TotalMatching);
		int32 EndIdx = (Limit > 0) ? FMath::Min(StartIdx + Limit, TotalMatching) : TotalMatching;

		TArray<TSharedPtr<FJsonValue>> FailedArr;
		int32 TotalChecked = 0, TotalPassed = 0, TotalFailed = 0;

		for (int32 Idx = StartIdx; Idx < EndIdx; Idx++)
		{
			UBlueprint* BP = Cast<UBlueprint>(AllBP[MatchingIndices[Idx]].GetAsset());
			if (!BP) continue;
			TotalChecked++;

			TSharedRef<FJsonObject> Result = ValidateSingleBlueprint(BP, BP->GetName());
			bool bValid = Result->GetBoolField(TEXT("isValid"));
			if (bValid)
				TotalPassed++;
			else
			{
				TotalFailed++;
				Result->SetStringField(TEXT("path"), AllBP[MatchingIndices[Idx]].PackageName.ToString());
				FailedArr.Add(MakeShared<FJsonValueObject>(Result));
			}
		}

		TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("totalMatching"), TotalMatching);
		Result->SetNumberField(TEXT("totalChecked"), TotalChecked);
		Result->SetNumberField(TEXT("totalPassed"), TotalPassed);
		Result->SetNumberField(TEXT("totalFailed"), TotalFailed);
		Result->SetArrayField(TEXT("failed"), FailedArr);
		if (!Filter.IsEmpty()) Result->SetStringField(TEXT("filter"), Filter);
		return FMCPToolResult::Ok(Result);
	}
};

// ============================================================
// Registration
// ============================================================
namespace UltimateMCPTools
{
	void RegisterValidationTools()
	{
		auto& R = FMCPToolRegistry::Get();
		R.Register(MakeShared<FTool_ValidateBlueprint>());
		R.Register(MakeShared<FTool_ValidateAllBlueprints>());
	}
}
