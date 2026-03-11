// Actor management tools: spawn, delete, find, transform, properties
// Ported from world-builder-mcp's EpicUnrealMCPEditorCommands

#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{
	/** Parse [x, y, z] JSON array into FVector. Returns ZeroVector on missing/bad data. */
	FVector JsonArrayToVector(const TSharedPtr<FJsonObject>& Obj, const FString& Field)
	{
		FVector V(0.f);
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (Obj->TryGetArrayField(Field, Arr) && Arr->Num() >= 3)
		{
			V.X = (*Arr)[0]->AsNumber();
			V.Y = (*Arr)[1]->AsNumber();
			V.Z = (*Arr)[2]->AsNumber();
		}
		return V;
	}

	/** Parse [pitch, yaw, roll] JSON array into FRotator. */
	FRotator JsonArrayToRotator(const TSharedPtr<FJsonObject>& Obj, const FString& Field)
	{
		FRotator R(0.f);
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (Obj->TryGetArrayField(Field, Arr) && Arr->Num() >= 3)
		{
			R.Pitch = (*Arr)[0]->AsNumber();
			R.Yaw   = (*Arr)[1]->AsNumber();
			R.Roll  = (*Arr)[2]->AsNumber();
		}
		return R;
	}

	/** Convert actor to JSON value (for array inclusion). */
	TSharedPtr<FJsonValue> ActorToJsonValue(AActor* Actor)
	{
		if (!Actor) return MakeShared<FJsonValueNull>();
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Actor->GetName());
		O->SetStringField(TEXT("label"), Actor->GetActorLabel());
		O->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

		auto MakeVec = [](const FVector& V) {
			TArray<TSharedPtr<FJsonValue>> A;
			A.Add(MakeShared<FJsonValueNumber>(V.X));
			A.Add(MakeShared<FJsonValueNumber>(V.Y));
			A.Add(MakeShared<FJsonValueNumber>(V.Z));
			return A;
		};

		O->SetArrayField(TEXT("location"), MakeVec(Actor->GetActorLocation()));
		FRotator Rot = Actor->GetActorRotation();
		TArray<TSharedPtr<FJsonValue>> RotArr;
		RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
		RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
		RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
		O->SetArrayField(TEXT("rotation"), RotArr);
		O->SetArrayField(TEXT("scale"), MakeVec(Actor->GetActorScale3D()));

		return MakeShared<FJsonValueObject>(O);
	}

	/** Convert actor to JSON object (for top-level result). */
	TSharedPtr<FJsonObject> ActorToJsonObject(AActor* Actor)
	{
		if (!Actor) return nullptr;
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), Actor->GetName());
		O->SetStringField(TEXT("label"), Actor->GetActorLabel());
		O->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

		auto MakeVec = [](const FVector& V) {
			TArray<TSharedPtr<FJsonValue>> A;
			A.Add(MakeShared<FJsonValueNumber>(V.X));
			A.Add(MakeShared<FJsonValueNumber>(V.Y));
			A.Add(MakeShared<FJsonValueNumber>(V.Z));
			return A;
		};

		O->SetArrayField(TEXT("location"), MakeVec(Actor->GetActorLocation()));
		FRotator Rot = Actor->GetActorRotation();
		TArray<TSharedPtr<FJsonValue>> RotArr;
		RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Pitch));
		RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Yaw));
		RotArr.Add(MakeShared<FJsonValueNumber>(Rot.Roll));
		O->SetArrayField(TEXT("rotation"), RotArr);
		O->SetArrayField(TEXT("scale"), MakeVec(Actor->GetActorScale3D()));
		return O;
	}

	/** Find actor by name in the editor world. */
	AActor* FindActorByName(const FString& Name)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World) return nullptr;
		TArray<AActor*> All;
		UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), All);
		for (AActor* A : All)
		{
			if (A && (A->GetName() == Name || A->GetActorLabel() == Name))
			{
				return A;
			}
		}
		return nullptr;
	}

	/** Get the editor world, or nullptr. */
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}
}

// ---------------------------------------------------------------------------
// spawn_actor
// ---------------------------------------------------------------------------

class FTool_SpawnActor : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("spawn_actor");
		Info.Description = TEXT("Spawn an actor in the editor level. Supported types: StaticMeshActor, PointLight, SpotLight, DirectionalLight, CameraActor.");
		Info.Parameters = {
			{TEXT("type"),        TEXT("string"), TEXT("Actor type to spawn"), true, nullptr},
			{TEXT("name"),        TEXT("string"), TEXT("Unique actor name"), true, nullptr},
			{TEXT("location"),    TEXT("array"),  TEXT("[x, y, z] world location"), false, nullptr},
			{TEXT("rotation"),    TEXT("array"),  TEXT("[pitch, yaw, roll] rotation"), false, nullptr},
			{TEXT("scale"),       TEXT("array"),  TEXT("[x, y, z] scale"), false, nullptr},
			{TEXT("static_mesh"), TEXT("string"), TEXT("Asset path for StaticMeshActor mesh (e.g. /Engine/BasicShapes/Cube.Cube)"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("Actors");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString ActorType;
		if (!Params->TryGetStringField(TEXT("type"), ActorType))
			return FMCPToolResult::Error(TEXT("Missing 'type' parameter"));

		FString ActorName;
		if (!Params->TryGetStringField(TEXT("name"), ActorName))
			return FMCPToolResult::Error(TEXT("Missing 'name' parameter"));

		UWorld* World = GetEditorWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		// Check for name collision
		if (FindActorByName(ActorName))
			return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' already exists"), *ActorName));

		FVector Location = Params->HasField(TEXT("location")) ? JsonArrayToVector(Params, TEXT("location")) : FVector::ZeroVector;
		FRotator Rotation = Params->HasField(TEXT("rotation")) ? JsonArrayToRotator(Params, TEXT("rotation")) : FRotator::ZeroRotator;
		FVector Scale = Params->HasField(TEXT("scale")) ? JsonArrayToVector(Params, TEXT("scale")) : FVector::OneVector;

		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = *ActorName;

		AActor* NewActor = nullptr;

		if (ActorType == TEXT("StaticMeshActor"))
		{
			AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
			if (MeshActor)
			{
				FString MeshPath;
				if (Params->TryGetStringField(TEXT("static_mesh"), MeshPath))
				{
					UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
					if (Mesh)
						MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
					else
						UE_LOG(LogTemp, Warning, TEXT("[UltimateMCP] Static mesh not found: %s"), *MeshPath);
				}
			}
			NewActor = MeshActor;
		}
		else if (ActorType == TEXT("PointLight"))
		{
			NewActor = World->SpawnActor<APointLight>(APointLight::StaticClass(), Location, Rotation, SpawnParams);
		}
		else if (ActorType == TEXT("SpotLight"))
		{
			NewActor = World->SpawnActor<ASpotLight>(ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
		}
		else if (ActorType == TEXT("DirectionalLight"))
		{
			NewActor = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
		}
		else if (ActorType == TEXT("CameraActor"))
		{
			NewActor = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
		}
		else
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Unknown actor type: %s. Supported: StaticMeshActor, PointLight, SpotLight, DirectionalLight, CameraActor"), *ActorType));
		}

		if (!NewActor) return FMCPToolResult::Error(TEXT("Failed to spawn actor"));

		// Apply scale
		FTransform T = NewActor->GetTransform();
		T.SetScale3D(Scale);
		NewActor->SetActorTransform(T);

		return FMCPToolResult::Ok(ActorToJsonObject(NewActor));
	}
};

// ---------------------------------------------------------------------------
// delete_actor
// ---------------------------------------------------------------------------

class FTool_DeleteActor : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("delete_actor");
		Info.Description = TEXT("Delete an actor from the level by name.");
		Info.Parameters = {
			{TEXT("name"), TEXT("string"), TEXT("Name or label of the actor to delete"), true, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("Actors");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name;
		if (!Params->TryGetStringField(TEXT("name"), Name))
			return FMCPToolResult::Error(TEXT("Missing 'name' parameter"));

		AActor* Actor = FindActorByName(Name);
		if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *Name));

		TSharedPtr<FJsonObject> Info = ActorToJsonObject(Actor);
		Actor->Destroy();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetObjectField(TEXT("deleted_actor"), Info);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// get_actors_in_level
// ---------------------------------------------------------------------------

class FTool_GetActorsInLevel : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_actors_in_level");
		Info.Description = TEXT("List all actors in the current editor level with their transforms.");
		Info.Annotations.bReadOnly = true;
		Info.Annotations.Category = TEXT("Actors");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetEditorWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		TArray<AActor*> All;
		UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), All);

		TArray<TSharedPtr<FJsonValue>> Arr;
		for (AActor* A : All)
		{
			if (A) Arr.Add(ActorToJsonValue(A));
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Arr.Num());
		Result->SetArrayField(TEXT("actors"), Arr);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// find_actors_by_name
// ---------------------------------------------------------------------------

class FTool_FindActorsByName : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("find_actors_by_name");
		Info.Description = TEXT("Find actors whose name or label contains the given pattern (case-insensitive substring match).");
		Info.Parameters = {
			{TEXT("pattern"), TEXT("string"), TEXT("Substring pattern to match"), true, nullptr},
		};
		Info.Annotations.bReadOnly = true;
		Info.Annotations.Category = TEXT("Actors");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Pattern;
		if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
			return FMCPToolResult::Error(TEXT("Missing 'pattern' parameter"));

		UWorld* World = GetEditorWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		TArray<AActor*> All;
		UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), All);

		TArray<TSharedPtr<FJsonValue>> Matches;
		for (AActor* A : All)
		{
			if (A && (A->GetName().Contains(Pattern) || A->GetActorLabel().Contains(Pattern)))
			{
				Matches.Add(ActorToJsonValue(A));
			}
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("count"), Matches.Num());
		Result->SetArrayField(TEXT("actors"), Matches);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// set_actor_transform
// ---------------------------------------------------------------------------

class FTool_SetActorTransform : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_actor_transform");
		Info.Description = TEXT("Set an actor's location, rotation, and/or scale. Only supplied fields are changed.");
		Info.Parameters = {
			{TEXT("name"),     TEXT("string"), TEXT("Actor name or label"), true, nullptr},
			{TEXT("location"), TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
			{TEXT("rotation"), TEXT("array"),  TEXT("[pitch, yaw, roll]"), false, nullptr},
			{TEXT("scale"),    TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("Actors");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name;
		if (!Params->TryGetStringField(TEXT("name"), Name))
			return FMCPToolResult::Error(TEXT("Missing 'name' parameter"));

		AActor* Actor = FindActorByName(Name);
		if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *Name));

		FTransform T = Actor->GetTransform();

		if (Params->HasField(TEXT("location")))
			T.SetLocation(JsonArrayToVector(Params, TEXT("location")));
		if (Params->HasField(TEXT("rotation")))
			T.SetRotation(FQuat(JsonArrayToRotator(Params, TEXT("rotation"))));
		if (Params->HasField(TEXT("scale")))
			T.SetScale3D(JsonArrayToVector(Params, TEXT("scale")));

		Actor->SetActorTransform(T);
		return FMCPToolResult::Ok(ActorToJsonObject(Actor));
	}
};

// ---------------------------------------------------------------------------
// spawn_blueprint_actor
// ---------------------------------------------------------------------------

class FTool_SpawnBlueprintActor : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("spawn_blueprint_actor");
		Info.Description = TEXT("Spawn an actor from a Blueprint asset. Provide the Blueprint name or full path (e.g. /Game/Blueprints/MyBP).");
		Info.Parameters = {
			{TEXT("blueprint_name"), TEXT("string"), TEXT("Blueprint asset name or path"), true, nullptr},
			{TEXT("actor_name"),     TEXT("string"), TEXT("Label for the spawned actor"), true, nullptr},
			{TEXT("location"),       TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
			{TEXT("rotation"),       TEXT("array"),  TEXT("[pitch, yaw, roll]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("Actors");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString BlueprintName;
		if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
			return FMCPToolResult::Error(TEXT("Missing 'blueprint_name' parameter"));

		FString ActorName;
		if (!Params->TryGetStringField(TEXT("actor_name"), ActorName))
			return FMCPToolResult::Error(TEXT("Missing 'actor_name' parameter"));

		// Resolve blueprint path
		FString ObjectPath;
		if (BlueprintName.StartsWith(TEXT("/")))
		{
			FString AssetName = FPaths::GetBaseFilename(BlueprintName);
			ObjectPath = FString::Printf(TEXT("%s.%s"), *BlueprintName, *AssetName);
		}
		else
		{
			ObjectPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s"), *BlueprintName, *BlueprintName);
		}

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *ObjectPath);
		if (!Blueprint)
		{
			// Fallback: asset registry
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
			FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
			if (AD.IsValid())
				Blueprint = Cast<UBlueprint>(AD.GetAsset());
		}
		if (!Blueprint || !Blueprint->GeneratedClass)
			return FMCPToolResult::Error(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));

		UWorld* World = GetEditorWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		FVector Location = Params->HasField(TEXT("location")) ? JsonArrayToVector(Params, TEXT("location")) : FVector::ZeroVector;
		FRotator Rotation = Params->HasField(TEXT("rotation")) ? JsonArrayToRotator(Params, TEXT("rotation")) : FRotator::ZeroRotator;

		FTransform SpawnTransform;
		SpawnTransform.SetLocation(Location);
		SpawnTransform.SetRotation(FQuat(Rotation));

		AActor* NewActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, SpawnTransform);
		if (!NewActor)
			return FMCPToolResult::Error(TEXT("Failed to spawn blueprint actor"));

		NewActor->SetActorLabel(*ActorName);
		return FMCPToolResult::Ok(ActorToJsonObject(NewActor));
	}
};

// ---------------------------------------------------------------------------
// get_actor_properties
// ---------------------------------------------------------------------------

class FTool_GetActorProperties : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_actor_properties");
		Info.Description = TEXT("Get all editable UPROPERTY values for an actor (bools, ints, floats, strings, enums).");
		Info.Parameters = {
			{TEXT("name"), TEXT("string"), TEXT("Actor name or label"), true, nullptr},
		};
		Info.Annotations.bReadOnly = true;
		Info.Annotations.Category = TEXT("Actors");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name;
		if (!Params->TryGetStringField(TEXT("name"), Name))
			return FMCPToolResult::Error(TEXT("Missing 'name' parameter"));

		AActor* Actor = FindActorByName(Name);
		if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *Name));

		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

		for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop->HasAnyPropertyFlags(CPF_Edit)) continue;

			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
			FString PropName = Prop->GetName();

			if (Prop->IsA<FBoolProperty>())
			{
				Props->SetBoolField(PropName, CastField<FBoolProperty>(Prop)->GetPropertyValue(ValuePtr));
			}
			else if (Prop->IsA<FIntProperty>())
			{
				Props->SetNumberField(PropName, CastField<FIntProperty>(Prop)->GetPropertyValue(ValuePtr));
			}
			else if (Prop->IsA<FFloatProperty>())
			{
				Props->SetNumberField(PropName, CastField<FFloatProperty>(Prop)->GetPropertyValue(ValuePtr));
			}
			else if (Prop->IsA<FDoubleProperty>())
			{
				Props->SetNumberField(PropName, CastField<FDoubleProperty>(Prop)->GetPropertyValue(ValuePtr));
			}
			else if (Prop->IsA<FStrProperty>())
			{
				Props->SetStringField(PropName, CastField<FStrProperty>(Prop)->GetPropertyValue(ValuePtr));
			}
			else if (Prop->IsA<FNameProperty>())
			{
				Props->SetStringField(PropName, CastField<FNameProperty>(Prop)->GetPropertyValue(ValuePtr).ToString());
			}
			else if (Prop->IsA<FTextProperty>())
			{
				Props->SetStringField(PropName, CastField<FTextProperty>(Prop)->GetPropertyValue(ValuePtr).ToString());
			}
			// Skip complex types - just note them
			else
			{
				Props->SetStringField(PropName, FString::Printf(TEXT("<%s>"), *Prop->GetCPPType()));
			}
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actor_name"), Actor->GetName());
		Result->SetStringField(TEXT("actor_class"), Actor->GetClass()->GetName());
		Result->SetObjectField(TEXT("properties"), Props);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// set_actor_property
// ---------------------------------------------------------------------------

class FTool_SetActorProperty : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_actor_property");
		Info.Description = TEXT("Set a single UPROPERTY on an actor by name. Supports bool, int, float, string, byte/enum.");
		Info.Parameters = {
			{TEXT("name"),     TEXT("string"), TEXT("Actor name or label"), true, nullptr},
			{TEXT("property"), TEXT("string"), TEXT("Property name"), true, nullptr},
			{TEXT("value"),    TEXT("string"), TEXT("New value (strings, numbers, booleans all accepted)"), true, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("Actors");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name;
		if (!Params->TryGetStringField(TEXT("name"), Name))
			return FMCPToolResult::Error(TEXT("Missing 'name' parameter"));

		FString PropertyName;
		if (!Params->TryGetStringField(TEXT("property"), PropertyName))
			return FMCPToolResult::Error(TEXT("Missing 'property' parameter"));

		AActor* Actor = FindActorByName(Name);
		if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *Name));

		// Get the raw JSON value so we can handle multiple types
		TSharedPtr<FJsonValue> JsonVal = Params->TryGetField(TEXT("value"));
		if (!JsonVal.IsValid())
			return FMCPToolResult::Error(TEXT("Missing 'value' parameter"));

		FProperty* Prop = Actor->GetClass()->FindPropertyByName(*PropertyName);
		if (!Prop)
			return FMCPToolResult::Error(FString::Printf(TEXT("Property not found: %s"), *PropertyName));

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);

		if (Prop->IsA<FBoolProperty>())
		{
			CastField<FBoolProperty>(Prop)->SetPropertyValue(ValuePtr, JsonVal->AsBool());
		}
		else if (Prop->IsA<FIntProperty>())
		{
			CastField<FIntProperty>(Prop)->SetPropertyValue(ValuePtr, static_cast<int32>(JsonVal->AsNumber()));
		}
		else if (Prop->IsA<FFloatProperty>())
		{
			CastField<FFloatProperty>(Prop)->SetPropertyValue(ValuePtr, static_cast<float>(JsonVal->AsNumber()));
		}
		else if (Prop->IsA<FDoubleProperty>())
		{
			CastField<FDoubleProperty>(Prop)->SetPropertyValue(ValuePtr, JsonVal->AsNumber());
		}
		else if (Prop->IsA<FStrProperty>())
		{
			CastField<FStrProperty>(Prop)->SetPropertyValue(ValuePtr, JsonVal->AsString());
		}
		else if (Prop->IsA<FNameProperty>())
		{
			CastField<FNameProperty>(Prop)->SetPropertyValue(ValuePtr, FName(*JsonVal->AsString()));
		}
		else if (Prop->IsA<FByteProperty>())
		{
			FByteProperty* ByteProp = CastField<FByteProperty>(Prop);
			UEnum* EnumDef = ByteProp->GetIntPropertyEnum();
			if (EnumDef && JsonVal->Type == EJson::String)
			{
				FString EnumStr = JsonVal->AsString();
				if (EnumStr.Contains(TEXT("::")))
					EnumStr.Split(TEXT("::"), nullptr, &EnumStr);
				int64 EV = EnumDef->GetValueByNameString(EnumStr);
				if (EV == INDEX_NONE)
					return FMCPToolResult::Error(FString::Printf(TEXT("Enum value not found: %s"), *EnumStr));
				ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(EV));
			}
			else
			{
				ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(JsonVal->AsNumber()));
			}
		}
		else if (Prop->IsA<FEnumProperty>())
		{
			FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop);
			UEnum* EnumDef = EnumProp->GetEnum();
			FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty();
			if (EnumDef && Underlying && JsonVal->Type == EJson::String)
			{
				FString EnumStr = JsonVal->AsString();
				if (EnumStr.Contains(TEXT("::")))
					EnumStr.Split(TEXT("::"), nullptr, &EnumStr);
				int64 EV = EnumDef->GetValueByNameString(EnumStr);
				if (EV == INDEX_NONE)
					return FMCPToolResult::Error(FString::Printf(TEXT("Enum value not found: %s"), *EnumStr));
				Underlying->SetIntPropertyValue(ValuePtr, EV);
			}
			else if (Underlying)
			{
				Underlying->SetIntPropertyValue(ValuePtr, static_cast<int64>(JsonVal->AsNumber()));
			}
		}
		else
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Unsupported property type: %s"), *Prop->GetCPPType()));
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actor"), Actor->GetName());
		Result->SetStringField(TEXT("property"), PropertyName);
		Result->SetBoolField(TEXT("success"), true);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

namespace UltimateMCPTools
{
	void RegisterActorTools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_SpawnActor>());
		Registry.Register(MakeShared<FTool_DeleteActor>());
		Registry.Register(MakeShared<FTool_GetActorsInLevel>());
		Registry.Register(MakeShared<FTool_FindActorsByName>());
		Registry.Register(MakeShared<FTool_SetActorTransform>());
		Registry.Register(MakeShared<FTool_SpawnBlueprintActor>());
		Registry.Register(MakeShared<FTool_GetActorProperties>());
		Registry.Register(MakeShared<FTool_SetActorProperty>());
	}
}
