// Core HTTP server — based on BlueprintMCP's FHttpServerModule pattern
// with UnrealClaude's tool registry dispatch and queued game-thread execution.
#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "HttpResultCallback.h"

struct FPendingRequest
{
	FString Endpoint;
	TMap<FString, FString> QueryParams;
	FString Body;
	FHttpResultCallback OnComplete;
};

/**
 * The HTTP server that bridges MCP TypeScript wrapper <-> UE5 tool execution.
 * Runs on a configurable port (default 9847). All tool execution happens on the game thread.
 */
class FUltimateMCPServer
{
public:
	FUltimateMCPServer();
	~FUltimateMCPServer();

	/** Start listening. Returns false if port is already in use. */
	bool Start(int32 Port = 9847);

	/** Stop the server and clean up. */
	void Stop();

	/** Process one queued request per tick (called from editor subsystem). */
	void Tick(float DeltaTime);

	/** Is the server currently running? */
	bool IsRunning() const { return bIsRunning; }

	/** Get the port we're listening on. */
	int32 GetPort() const { return ListenPort; }

private:
	// Route handlers
	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleListTools(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleExecuteTool(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleShutdown(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	/** Queue a request for game-thread processing. */
	FHttpRequestHandler QueuedHandler(const FString& Endpoint);

	/** Process a queued request on the game thread. */
	void ProcessOneRequest();

	/** Send a JSON response. */
	static void SendJsonResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, const FString& Json);
	static void SendJsonResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, TSharedPtr<FJsonObject> Obj);

	/** URL decode helper. */
	static FString UrlDecode(const FString& Encoded);

	// State
	TSharedPtr<IHttpRouter> Router;
	TQueue<TSharedPtr<FPendingRequest>> RequestQueue;
	FTSTicker::FDelegateHandle TickHandle;
	int32 ListenPort = 9847;
	bool bIsRunning = false;
};
