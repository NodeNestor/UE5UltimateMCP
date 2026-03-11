#include "UE5UltimateMCPModule.h"

#define LOCTEXT_NAMESPACE "FUE5UltimateMCPModule"

void FUE5UltimateMCPModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("[UltimateMCP] Module started. Server will initialize via EditorSubsystem."));
}

void FUE5UltimateMCPModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("[UltimateMCP] Module shutdown."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FUE5UltimateMCPModule, UE5UltimateMCP)
