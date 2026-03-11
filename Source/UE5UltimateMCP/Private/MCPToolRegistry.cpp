#include "Tools/MCPToolRegistry.h"

FMCPToolRegistry& FMCPToolRegistry::Get()
{
	static FMCPToolRegistry Instance;
	return Instance;
}

void FMCPToolRegistry::Register(TSharedPtr<FMCPToolBase> Tool)
{
	if (!Tool.IsValid()) return;
	FMCPToolInfo Info = Tool->GetInfo();
	UE_LOG(LogTemp, Log, TEXT("[UltimateMCP] Registered tool: %s"), *Info.Name);
	Tools.Add(Info.Name, Tool);
}

FMCPToolBase* FMCPToolRegistry::FindTool(const FString& Name) const
{
	const TSharedPtr<FMCPToolBase>* Found = Tools.Find(Name);
	return Found ? Found->Get() : nullptr;
}

TArray<FMCPToolInfo> FMCPToolRegistry::GetAllToolInfos() const
{
	TArray<FMCPToolInfo> Result;
	for (const auto& Pair : Tools)
	{
		Result.Add(Pair.Value->GetInfo());
	}
	return Result;
}

FMCPToolResult FMCPToolRegistry::ExecuteTool(const FString& Name, const TSharedPtr<FJsonObject>& Params)
{
	FMCPToolBase* Tool = FindTool(Name);
	if (!Tool)
	{
		return FMCPToolResult::Error(FString::Printf(TEXT("Tool '%s' not found. Use /api/tools to list available tools."), *Name));
	}
	return Tool->Execute(Params);
}
