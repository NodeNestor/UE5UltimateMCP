#include "UltimateMCPSubsystem.h"
#include "UltimateMCPServer.h"
#include "Tools/MCPToolRegistry.h"

// Destructor must be in .cpp where FUltimateMCPServer is fully defined (TUniquePtr needs complete type)
UUltimateMCPSubsystem::~UUltimateMCPSubsystem() = default;

// Forward declare tool registration functions (each handler file provides one)
namespace UltimateMCPTools
{
	void RegisterBlueprintReadTools();
	void RegisterBlueprintMutationTools();
	void RegisterBlueprintGraphTools();
	void RegisterVariableTools();
	void RegisterParamTools();
	void RegisterInterfaceTools();
	void RegisterDispatcherTools();
	void RegisterComponentTools();
	void RegisterSnapshotTools();
	void RegisterValidationTools();
	void RegisterDiscoveryTools();
	void RegisterUserTypeTools();
	void RegisterDiffTools();
	void RegisterMaterialReadTools();
	void RegisterMaterialMutationTools();
	void RegisterAnimationTools();
	void RegisterActorTools();
	void RegisterViewportTools();
	void RegisterSequencerTools();
	void RegisterBehaviorTreeTools();
	void RegisterNavigationTools();
	void RegisterDataTableTools();
	void RegisterFoliageTools();
	void RegisterNiagaraTools();
	void RegisterUITools();
	void RegisterBuildTools();
	void RegisterWorldGenTools();
}

void UUltimateMCPSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTemp, Log, TEXT("[UltimateMCP] Subsystem initializing..."));

	// Register all tool categories
	UltimateMCPTools::RegisterBlueprintReadTools();
	UltimateMCPTools::RegisterBlueprintMutationTools();
	UltimateMCPTools::RegisterBlueprintGraphTools();
	UltimateMCPTools::RegisterVariableTools();
	UltimateMCPTools::RegisterParamTools();
	UltimateMCPTools::RegisterInterfaceTools();
	UltimateMCPTools::RegisterDispatcherTools();
	UltimateMCPTools::RegisterComponentTools();
	UltimateMCPTools::RegisterSnapshotTools();
	UltimateMCPTools::RegisterValidationTools();
	UltimateMCPTools::RegisterDiscoveryTools();
	UltimateMCPTools::RegisterUserTypeTools();
	UltimateMCPTools::RegisterDiffTools();
	UltimateMCPTools::RegisterMaterialReadTools();
	UltimateMCPTools::RegisterMaterialMutationTools();
	UltimateMCPTools::RegisterAnimationTools();
	UltimateMCPTools::RegisterActorTools();
	UltimateMCPTools::RegisterViewportTools();
	UltimateMCPTools::RegisterSequencerTools();
	UltimateMCPTools::RegisterBehaviorTreeTools();
	UltimateMCPTools::RegisterNavigationTools();
	UltimateMCPTools::RegisterDataTableTools();
	UltimateMCPTools::RegisterFoliageTools();
	UltimateMCPTools::RegisterNiagaraTools();
	UltimateMCPTools::RegisterUITools();
	UltimateMCPTools::RegisterBuildTools();
	UltimateMCPTools::RegisterWorldGenTools();

	// Start the HTTP server
	Server = MakeUnique<FUltimateMCPServer>();
	if (Server->Start(9847))
	{
		UE_LOG(LogTemp, Log, TEXT("[UltimateMCP] Server started on port %d with %d tools"),
			Server->GetPort(), FMCPToolRegistry::Get().Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[UltimateMCP] Failed to start server on port 9847"));
	}
}

void UUltimateMCPSubsystem::Deinitialize()
{
	if (Server.IsValid())
	{
		Server->Stop();
		Server.Reset();
	}
	Super::Deinitialize();
}

void UUltimateMCPSubsystem::Tick(float DeltaTime)
{
	if (Server.IsValid())
	{
		Server->Tick(DeltaTime);
	}
}

TStatId UUltimateMCPSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UUltimateMCPSubsystem, STATGROUP_Tickables);
}
