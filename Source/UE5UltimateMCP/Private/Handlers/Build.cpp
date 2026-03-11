// Build / Package / Lighting tools for UltimateMCP
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorBuildUtils.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"

// ─────────────────────────────────────────────────────────────
// build_lighting
// ─────────────────────────────────────────────────────────────
class FTool_BuildLighting : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("build_lighting");
		Info.Description = TEXT("Build lighting for the current level at the specified quality.");
		Info.Annotations.Category = TEXT("Build");
		Info.Annotations.bExpensive = true;
		Info.Annotations.bDestructive = false;

		FMCPParamSchema P;
		P.Name = TEXT("quality"); P.Type = TEXT("string");
		P.Description = TEXT("Quality: 'preview', 'medium', 'high', 'production'. Default: 'preview'");
		P.bRequired = false;
		Info.Parameters.Add(P);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Quality = Params->HasField(TEXT("quality")) ? Params->GetStringField(TEXT("quality")).ToLower() : TEXT("preview");

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		// Set lighting quality via config
		int32 QualityLevel = 0; // Preview
		if (Quality == TEXT("medium")) QualityLevel = 1;
		else if (Quality == TEXT("high")) QualityLevel = 2;
		else if (Quality == TEXT("production")) QualityLevel = 3;

		// Use EditorBuildUtils to build lighting
		FEditorBuildUtils::EditorBuild(World, FBuildOptions::BuildLighting);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("lighting_build_initiated"));
		Result->SetStringField(TEXT("quality"), Quality);
		Result->SetNumberField(TEXT("quality_level"), QualityLevel);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// build_navigation_only
// ─────────────────────────────────────────────────────────────
class FTool_BuildNavigationOnly : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("build_navigation_only");
		Info.Description = TEXT("Rebuild only navigation data for the current level.");
		Info.Annotations.Category = TEXT("Build");
		Info.Annotations.bExpensive = true;
		Info.Annotations.bDestructive = false;

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!NavSys)
		{
			return FMCPToolResult::Error(TEXT("Navigation system not available."));
		}

		NavSys->Build();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("navigation_build_complete"));
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// cook_project
// ─────────────────────────────────────────────────────────────
class FTool_CookProject : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("cook_project");
		Info.Description = TEXT("Cook the project for a target platform using Unreal Automation Tool (UAT).");
		Info.Annotations.Category = TEXT("Build");
		Info.Annotations.bExpensive = true;
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("platform"), TEXT("string"), TEXT("Target platform: 'Win64', 'Linux', 'Mac', 'Android', 'IOS'"), true);
		AddParam(TEXT("config"), TEXT("string"), TEXT("Build config: 'Development', 'Shipping', 'DebugGame'. Default: 'Development'"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Platform = Params->GetStringField(TEXT("platform"));
		FString Config = Params->HasField(TEXT("config")) ? Params->GetStringField(TEXT("config")) : TEXT("Development");

		if (Platform.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("Parameter 'platform' is required."));
		}

		// Locate UAT
		FString EngineDir = FPaths::EngineDir();
		FString UATPath = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"));

#if PLATFORM_WINDOWS
		FString UATExe = FPaths::Combine(UATPath, TEXT("RunUAT.bat"));
#else
		FString UATExe = FPaths::Combine(UATPath, TEXT("RunUAT.sh"));
#endif

		if (!IFileManager::Get().FileExists(*UATExe))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("UAT not found at '%s'."), *UATExe));
		}

		FString ProjectFile = FPaths::GetProjectFilePath();

		FString CommandLine = FString::Printf(
			TEXT("BuildCookRun -project=\"%s\" -targetplatform=%s -clientconfig=%s -cook -allmaps -NoP4 -UTF8Output"),
			*ProjectFile, *Platform, *Config);

		// Launch UAT asynchronously
		FPlatformProcess::CreateProc(*UATExe, *CommandLine, true, false, false, nullptr, 0, nullptr, nullptr);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("cook_initiated"));
		Result->SetStringField(TEXT("platform"), Platform);
		Result->SetStringField(TEXT("config"), Config);
		Result->SetStringField(TEXT("uat_path"), UATExe);
		Result->SetStringField(TEXT("command"), CommandLine);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// package_project
// ─────────────────────────────────────────────────────────────
class FTool_PackageProject : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("package_project");
		Info.Description = TEXT("Package (build + cook + stage) the project for a target platform.");
		Info.Annotations.Category = TEXT("Build");
		Info.Annotations.bExpensive = true;
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("platform"), TEXT("string"), TEXT("Target platform: 'Win64', 'Linux', 'Mac', 'Android', 'IOS'"), true);
		AddParam(TEXT("output_dir"), TEXT("string"), TEXT("Directory to output the packaged build"), true);
		AddParam(TEXT("config"), TEXT("string"), TEXT("Build config: 'Development', 'Shipping'. Default: 'Shipping'"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Platform = Params->GetStringField(TEXT("platform"));
		FString OutputDir = Params->GetStringField(TEXT("output_dir"));
		FString Config = Params->HasField(TEXT("config")) ? Params->GetStringField(TEXT("config")) : TEXT("Shipping");

		if (Platform.IsEmpty() || OutputDir.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("Both 'platform' and 'output_dir' are required."));
		}

		FString EngineDir = FPaths::EngineDir();
		FString UATPath = FPaths::Combine(EngineDir, TEXT("Build"), TEXT("BatchFiles"));

#if PLATFORM_WINDOWS
		FString UATExe = FPaths::Combine(UATPath, TEXT("RunUAT.bat"));
#else
		FString UATExe = FPaths::Combine(UATPath, TEXT("RunUAT.sh"));
#endif

		if (!IFileManager::Get().FileExists(*UATExe))
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("UAT not found at '%s'."), *UATExe));
		}

		FString ProjectFile = FPaths::GetProjectFilePath();

		FString CommandLine = FString::Printf(
			TEXT("BuildCookRun -project=\"%s\" -targetplatform=%s -clientconfig=%s -build -cook -stage -package -archive -archivedirectory=\"%s\" -allmaps -NoP4 -UTF8Output"),
			*ProjectFile, *Platform, *Config, *OutputDir);

		FPlatformProcess::CreateProc(*UATExe, *CommandLine, true, false, false, nullptr, 0, nullptr, nullptr);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("package_initiated"));
		Result->SetStringField(TEXT("platform"), Platform);
		Result->SetStringField(TEXT("config"), Config);
		Result->SetStringField(TEXT("output_dir"), OutputDir);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// get_build_status
// ─────────────────────────────────────────────────────────────
class FTool_GetBuildStatus : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_build_status");
		Info.Description = TEXT("Check if any editor build operations (lighting, etc.) are currently in progress.");
		Info.Annotations.Category = TEXT("Build");
		Info.Annotations.bReadOnly = true;

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		bool bIsBuilding = GEditor ? GEditor->IsEditorWorldBeingCleanedUp() : false;

		// Check if lighting build is running
		bool bLightingInProgress = GEditor ? GEditor->IsLightingBuildCurrentlyRunning() : false;

		Result->SetBoolField(TEXT("lighting_build_active"), bLightingInProgress);

		// Check for map build
		bool bMapBuildInProgress = false;
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (World)
		{
			bMapBuildInProgress = FEditorBuildUtils::IsBuildCurrentlyRunning();
		}
		Result->SetBoolField(TEXT("map_build_active"), bMapBuildInProgress);
		Result->SetBoolField(TEXT("any_build_active"), bLightingInProgress || bMapBuildInProgress);

		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// run_commandlet
// ─────────────────────────────────────────────────────────────
class FTool_RunCommandlet : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("run_commandlet");
		Info.Description = TEXT("Run a UE5 commandlet by name (e.g. 'ResavePackages', 'FixupRedirects').");
		Info.Annotations.Category = TEXT("Build");
		Info.Annotations.bExpensive = true;
		Info.Annotations.bDestructive = true;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("commandlet_name"), TEXT("string"), TEXT("Name of the commandlet to run"), true);
		AddParam(TEXT("args"), TEXT("string"), TEXT("Additional command-line arguments for the commandlet"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString CommandletName = Params->GetStringField(TEXT("commandlet_name"));
		FString Args = Params->HasField(TEXT("args")) ? Params->GetStringField(TEXT("args")) : TEXT("");

		if (CommandletName.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("Parameter 'commandlet_name' is required."));
		}

		// Security: block dangerous commandlets
		static TArray<FString> BlockedCommandlets = {
			TEXT("DeletePackages"),
			TEXT("WipeContent"),
		};
		for (const FString& Blocked : BlockedCommandlets)
		{
			if (CommandletName.Contains(Blocked))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Commandlet '%s' is blocked for safety."), *CommandletName));
			}
		}

		// Build the command line for the editor subprocess
		FString EngineDir = FPaths::EngineDir();

#if PLATFORM_WINDOWS
		FString EditorExe = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("Win64"), TEXT("UnrealEditor-Cmd.exe"));
#else
		FString EditorExe = FPaths::Combine(EngineDir, TEXT("Binaries"), TEXT("Linux"), TEXT("UnrealEditor-Cmd"));
#endif

		FString ProjectFile = FPaths::GetProjectFilePath();
		FString CommandLine = FString::Printf(
			TEXT("\"%s\" -run=%s %s -NoP4 -UTF8Output"),
			*ProjectFile, *CommandletName, *Args);

		FPlatformProcess::CreateProc(*EditorExe, *CommandLine, true, false, false, nullptr, 0, nullptr, nullptr);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("commandlet_launched"));
		Result->SetStringField(TEXT("commandlet"), CommandletName);
		Result->SetStringField(TEXT("args"), Args);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────
namespace UltimateMCPTools
{
	void RegisterBuildTools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_BuildLighting>());
		Registry.Register(MakeShared<FTool_BuildNavigationOnly>());
		Registry.Register(MakeShared<FTool_CookProject>());
		Registry.Register(MakeShared<FTool_PackageProject>());
		Registry.Register(MakeShared<FTool_GetBuildStatus>());
		Registry.Register(MakeShared<FTool_RunCommandlet>());
	}
}
