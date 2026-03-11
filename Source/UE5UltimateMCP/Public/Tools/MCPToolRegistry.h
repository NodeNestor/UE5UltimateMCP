// Tool registry: dispatches tool calls by name, supports listing all tools.
#pragma once

#include "CoreMinimal.h"
#include "Tools/MCPToolBase.h"

class FMCPToolRegistry
{
public:
	static FMCPToolRegistry& Get();

	/** Register a tool. Takes ownership. */
	void Register(TSharedPtr<FMCPToolBase> Tool);

	/** Find a tool by name. Returns nullptr if not found. */
	FMCPToolBase* FindTool(const FString& Name) const;

	/** Get info for all registered tools. */
	TArray<FMCPToolInfo> GetAllToolInfos() const;

	/** Execute a tool by name. */
	FMCPToolResult ExecuteTool(const FString& Name, const TSharedPtr<FJsonObject>& Params);

	/** Number of registered tools. */
	int32 Num() const { return Tools.Num(); }

private:
	TMap<FString, TSharedPtr<FMCPToolBase>> Tools;
};
