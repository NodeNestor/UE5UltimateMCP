// Adapted from UnrealClaude's polymorphic tool pattern + BlueprintMCP's queued model
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Tool annotation hints for the MCP client (read-only vs destructive, etc.)
 */
struct FMCPToolAnnotations
{
	bool bReadOnly = false;
	bool bDestructive = false;
	bool bExpensive = false;
	FString Category;
};

/**
 * Schema definition for a single tool parameter.
 */
struct FMCPParamSchema
{
	FString Name;
	FString Type;        // "string", "number", "boolean", "object", "array"
	FString Description;
	bool bRequired = true;
	TSharedPtr<FJsonObject> DefaultValue;
};

/**
 * Complete tool info returned by GetInfo().
 */
struct FMCPToolInfo
{
	FString Name;
	FString Description;
	TArray<FMCPParamSchema> Parameters;
	FMCPToolAnnotations Annotations;
};

/**
 * Result from executing a tool.
 */
struct FMCPToolResult
{
	bool bSuccess = true;
	TSharedPtr<FJsonObject> Data;
	FString ErrorMessage;

	static FMCPToolResult Ok(TSharedPtr<FJsonObject> InData)
	{
		FMCPToolResult R;
		R.bSuccess = true;
		R.Data = InData;
		return R;
	}

	static FMCPToolResult Error(const FString& Msg)
	{
		FMCPToolResult R;
		R.bSuccess = false;
		R.ErrorMessage = Msg;
		return R;
	}
};

/**
 * Base class for all MCP tools. Each tool category registers concrete subclasses.
 * Pattern: BlueprintMCP's handler-per-file + UnrealClaude's registry dispatch.
 */
class FMCPToolBase
{
public:
	virtual ~FMCPToolBase() = default;

	/** Return tool metadata + parameter schema. */
	virtual FMCPToolInfo GetInfo() const = 0;

	/** Execute the tool on the game thread. Params come from the MCP client. */
	virtual FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) = 0;
};
