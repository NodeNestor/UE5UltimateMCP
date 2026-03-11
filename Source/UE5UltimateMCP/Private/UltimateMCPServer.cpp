// HTTP server implementation — BlueprintMCP's queued model + tool registry dispatch.
#include "UltimateMCPServer.h"
#include "Tools/MCPToolRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogUltimateMCP, Log, All);

// ─── URL Decode (from BlueprintMCP, handles %XX and + as space) ───

FString FUltimateMCPServer::UrlDecode(const FString& Encoded)
{
	FString Result;
	Result.Reserve(Encoded.Len());
	for (int32 i = 0; i < Encoded.Len(); i++)
	{
		TCHAR C = Encoded[i];
		if (C == TEXT('+'))
		{
			Result += TEXT(' ');
		}
		else if (C == TEXT('%') && i + 2 < Encoded.Len())
		{
			FString Hex = Encoded.Mid(i + 1, 2);
			TCHAR* End = nullptr;
			int32 Val = FCString::Strtoi(*Hex, &End, 16);
			if (End && End != *Hex)
			{
				Result += (TCHAR)Val;
				i += 2;
			}
			else
			{
				Result += C;
			}
		}
		else
		{
			Result += C;
		}
	}
	return Result;
}

// ─── JSON helpers ───

void FUltimateMCPServer::SendJsonResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, const FString& Json)
{
	auto Response = FHttpServerResponse::Create(Json, TEXT("application/json"));
	OnComplete(MoveTemp(Response));
}

void FUltimateMCPServer::SendJsonResponse(const FHttpResultCallback& OnComplete, int32 StatusCode, TSharedPtr<FJsonObject> Obj)
{
	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	SendJsonResponse(OnComplete, StatusCode, Json);
}

// ─── Constructor / Destructor ───

FUltimateMCPServer::FUltimateMCPServer() {}

FUltimateMCPServer::~FUltimateMCPServer()
{
	Stop();
}

// ─── Start / Stop ───

bool FUltimateMCPServer::Start(int32 Port)
{
	if (bIsRunning) return true;

	ListenPort = Port;
	FHttpServerModule& HttpModule = FHttpServerModule::Get();
	Router = HttpModule.GetHttpRouter(ListenPort);

	if (!Router.IsValid())
	{
		UE_LOG(LogUltimateMCP, Error, TEXT("Failed to get HTTP router on port %d"), ListenPort);
		return false;
	}

	// Immediate handlers (safe from any thread)
	Router->BindRoute(
		FHttpPath(TEXT("/api/health")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FUltimateMCPServer::HandleHealth));

	Router->BindRoute(
		FHttpPath(TEXT("/api/tools")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FUltimateMCPServer::HandleListTools));

	Router->BindRoute(
		FHttpPath(TEXT("/api/shutdown")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FUltimateMCPServer::HandleShutdown));

	// Tool execution — queued to game thread
	Router->BindRoute(
		FHttpPath(TEXT("/api/tool")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateLambda(
			[this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) -> bool
			{
				return HandleExecuteTool(Request, OnComplete);
			}));

	HttpModule.StartAllListeners();
	bIsRunning = true;

	UE_LOG(LogUltimateMCP, Log, TEXT("HTTP server listening on port %d"), ListenPort);
	return true;
}

void FUltimateMCPServer::Stop()
{
	if (!bIsRunning) return;

	if (Router.IsValid())
	{
		// Unbind all our routes
		Router.Reset();
	}

	FHttpServerModule& HttpModule = FHttpServerModule::Get();
	HttpModule.StopAllListeners();

	bIsRunning = false;
	UE_LOG(LogUltimateMCP, Log, TEXT("HTTP server stopped"));
}

// ─── Tick: process one queued request per frame ───

void FUltimateMCPServer::Tick(float DeltaTime)
{
	ProcessOneRequest();
}

void FUltimateMCPServer::ProcessOneRequest()
{
	TSharedPtr<FPendingRequest> Req;
	if (!RequestQueue.Dequeue(Req) || !Req.IsValid()) return;

	// Parse body JSON
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (!Req->Body.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Req->Body);
		FJsonSerializer::Deserialize(Reader, Params);
	}

	// Add query params to the JSON object
	for (const auto& QP : Req->QueryParams)
	{
		Params->SetStringField(QP.Key, QP.Value);
	}

	// Extract tool name from endpoint: /api/tool?name=xxx or from body
	FString ToolName;
	if (Params->HasField(TEXT("tool")))
	{
		ToolName = Params->GetStringField(TEXT("tool"));
	}
	else if (Req->QueryParams.Contains(TEXT("name")))
	{
		ToolName = Req->QueryParams[TEXT("name")];
	}

	if (ToolName.IsEmpty())
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetBoolField(TEXT("success"), false);
		ErrObj->SetStringField(TEXT("error"), TEXT("Missing 'tool' field or 'name' query parameter"));
		SendJsonResponse(Req->OnComplete, 400, ErrObj);
		return;
	}

	// Execute on game thread
	FMCPToolResult Result = FMCPToolRegistry::Get().ExecuteTool(ToolName, Params);

	// Build response
	TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
	ResponseObj->SetBoolField(TEXT("success"), Result.bSuccess);
	if (Result.bSuccess && Result.Data.IsValid())
	{
		ResponseObj->SetObjectField(TEXT("result"), Result.Data);
	}
	if (!Result.ErrorMessage.IsEmpty())
	{
		ResponseObj->SetStringField(TEXT("error"), Result.ErrorMessage);
	}

	SendJsonResponse(Req->OnComplete, Result.bSuccess ? 200 : 500, ResponseObj);
}

// ─── Route handlers ───

bool FUltimateMCPServer::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("status"), TEXT("ok"));
	Obj->SetStringField(TEXT("plugin"), TEXT("UE5UltimateMCP"));
	Obj->SetStringField(TEXT("version"), TEXT("0.1.0"));
	Obj->SetNumberField(TEXT("tools"), FMCPToolRegistry::Get().Num());
	Obj->SetNumberField(TEXT("port"), ListenPort);
	SendJsonResponse(OnComplete, 200, Obj);
	return true;
}

bool FUltimateMCPServer::HandleListTools(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TArray<FMCPToolInfo> AllTools = FMCPToolRegistry::Get().GetAllToolInfos();

	TArray<TSharedPtr<FJsonValue>> ToolArray;
	for (const FMCPToolInfo& Info : AllTools)
	{
		TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
		ToolObj->SetStringField(TEXT("name"), Info.Name);
		ToolObj->SetStringField(TEXT("description"), Info.Description);
		ToolObj->SetStringField(TEXT("category"), Info.Annotations.Category);
		ToolObj->SetBoolField(TEXT("readOnly"), Info.Annotations.bReadOnly);
		ToolObj->SetBoolField(TEXT("destructive"), Info.Annotations.bDestructive);

		// Parameters
		TArray<TSharedPtr<FJsonValue>> ParamArray;
		for (const FMCPParamSchema& P : Info.Parameters)
		{
			TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			ParamObj->SetStringField(TEXT("name"), P.Name);
			ParamObj->SetStringField(TEXT("type"), P.Type);
			ParamObj->SetStringField(TEXT("description"), P.Description);
			ParamObj->SetBoolField(TEXT("required"), P.bRequired);
			ParamArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
		ToolObj->SetArrayField(TEXT("parameters"), ParamArray);

		ToolArray.Add(MakeShared<FJsonValueObject>(ToolObj));
	}

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetArrayField(TEXT("tools"), ToolArray);
	Response->SetNumberField(TEXT("count"), AllTools.Num());
	SendJsonResponse(OnComplete, 200, Response);
	return true;
}

bool FUltimateMCPServer::HandleExecuteTool(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Queue for game-thread execution
	TSharedPtr<FPendingRequest> Pending = MakeShared<FPendingRequest>();
	Pending->Endpoint = TEXT("tool");
	Pending->OnComplete = OnComplete;

	// Copy body
	if (Request.Body.Num() > 0)
	{
		FUTF8ToTCHAR Converter((const ANSICHAR*)Request.Body.GetData(), Request.Body.Num());
		Pending->Body = FString(Converter.Length(), Converter.Get());
	}

	// Copy query params
	for (const auto& Pair : Request.QueryParams)
	{
		Pending->QueryParams.Add(UrlDecode(Pair.Key), UrlDecode(Pair.Value));
	}

	RequestQueue.Enqueue(Pending);
	return true;
}

bool FUltimateMCPServer::HandleShutdown(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("status"), TEXT("shutting_down"));
	SendJsonResponse(OnComplete, 200, Obj);

	// Defer actual shutdown
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[this](float) -> bool
		{
			Stop();
			return false;
		}), 0.5f);

	return true;
}
