// Editor subsystem that owns the MCP server and ticks it on the game thread.
#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Tickable.h"
#include "UltimateMCPServer.h"
#include "UltimateMCPSubsystem.generated.h"

UCLASS()
class UUltimateMCPSubsystem : public UEditorSubsystem, public FTickableEditorObject
{
	GENERATED_BODY()

public:
	virtual ~UUltimateMCPSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableEditorObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return Server.IsValid(); }

	FUltimateMCPServer* GetServer() const { return Server.Get(); }

private:
	TUniquePtr<FUltimateMCPServer> Server;
};
