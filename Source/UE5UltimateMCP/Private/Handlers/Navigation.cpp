// Navigation System tools for UltimateMCP
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"

#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavModifierVolume.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Null.h"
#include "NavAreas/NavArea_Obstacle.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/BrushComponent.h"

// ─────────────────────────────────────────────────────────────
// build_navigation
// ─────────────────────────────────────────────────────────────
class FTool_BuildNavigation : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("build_navigation");
		Info.Description = TEXT("Rebuild navigation mesh within the specified bounds (or full rebuild if no bounds given).");
		Info.Annotations.Category = TEXT("Navigation");
		Info.Annotations.bExpensive = true;
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("bounds_min"), TEXT("object"), TEXT("{ x, y, z } minimum bounds (optional, omit for full rebuild)"), false);
		AddParam(TEXT("bounds_max"), TEXT("object"), TEXT("{ x, y, z } maximum bounds (optional, omit for full rebuild)"), false);

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
			return FMCPToolResult::Error(TEXT("Navigation system not available. Ensure your project has navigation enabled."));
		}

		if (Params->HasField(TEXT("bounds_min")) && Params->HasField(TEXT("bounds_max")))
		{
			const TSharedPtr<FJsonObject>& MinObj = Params->GetObjectField(TEXT("bounds_min"));
			const TSharedPtr<FJsonObject>& MaxObj = Params->GetObjectField(TEXT("bounds_max"));
			FVector Min(MinObj->GetNumberField(TEXT("x")), MinObj->GetNumberField(TEXT("y")), MinObj->GetNumberField(TEXT("z")));
			FVector Max(MaxObj->GetNumberField(TEXT("x")), MaxObj->GetNumberField(TEXT("y")), MaxObj->GetNumberField(TEXT("z")));

			FBox DirtyArea(Min, Max);
			NavSys->AddDirtyArea(DirtyArea, ENavigationDirtyFlag::All);
		}

		NavSys->Build();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("status"), TEXT("navigation_build_complete"));
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_nav_mesh_bounds
// ─────────────────────────────────────────────────────────────
class FTool_AddNavMeshBounds : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_nav_mesh_bounds");
		Info.Description = TEXT("Spawn a NavMeshBoundsVolume at the given location and extent.");
		Info.Annotations.Category = TEXT("Navigation");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("location"), TEXT("object"), TEXT("{ x, y, z } center location"), true);
		AddParam(TEXT("extent"), TEXT("object"), TEXT("{ x, y, z } half-extent of the bounds volume"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		const TSharedPtr<FJsonObject>& LocObj = Params->GetObjectField(TEXT("location"));
		const TSharedPtr<FJsonObject>& ExtObj = Params->GetObjectField(TEXT("extent"));

		FVector Location(LocObj->GetNumberField(TEXT("x")), LocObj->GetNumberField(TEXT("y")), LocObj->GetNumberField(TEXT("z")));
		FVector Extent(ExtObj->GetNumberField(TEXT("x")), ExtObj->GetNumberField(TEXT("y")), ExtObj->GetNumberField(TEXT("z")));

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANavMeshBoundsVolume* BoundsVolume = World->SpawnActor<ANavMeshBoundsVolume>(Location, FRotator::ZeroRotator, SpawnParams);

		if (!BoundsVolume)
		{
			return FMCPToolResult::Error(TEXT("Failed to spawn NavMeshBoundsVolume."));
		}

		// Set the brush extent via the component's transform scale
		if (UBrushComponent* BrushComp = BoundsVolume->GetBrushComponent())
		{
			BoundsVolume->SetActorScale3D(Extent / 100.0f); // Brush default is 200 units, extent is half
		}

		BoundsVolume->SetActorLabel(TEXT("MCP_NavBounds"));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actor"), BoundsVolume->GetName());
		Result->SetStringField(TEXT("label"), BoundsVolume->GetActorLabel());

		TSharedPtr<FJsonObject> LocResult = MakeShared<FJsonObject>();
		LocResult->SetNumberField(TEXT("x"), Location.X);
		LocResult->SetNumberField(TEXT("y"), Location.Y);
		LocResult->SetNumberField(TEXT("z"), Location.Z);
		Result->SetObjectField(TEXT("location"), LocResult);

		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// test_path
// ─────────────────────────────────────────────────────────────
class FTool_TestPath : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("test_path");
		Info.Description = TEXT("Test if a navigation path exists between two points and return the path details.");
		Info.Annotations.Category = TEXT("Navigation");
		Info.Annotations.bReadOnly = true;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("start"), TEXT("object"), TEXT("{ x, y, z } start position"), true);
		AddParam(TEXT("end"), TEXT("object"), TEXT("{ x, y, z } end position"), true);

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

		const TSharedPtr<FJsonObject>& StartObj = Params->GetObjectField(TEXT("start"));
		const TSharedPtr<FJsonObject>& EndObj = Params->GetObjectField(TEXT("end"));

		FVector Start(StartObj->GetNumberField(TEXT("x")), StartObj->GetNumberField(TEXT("y")), StartObj->GetNumberField(TEXT("z")));
		FVector End(EndObj->GetNumberField(TEXT("x")), EndObj->GetNumberField(TEXT("y")), EndObj->GetNumberField(TEXT("z")));

		FPathFindingQuery Query;
		Query.StartLocation = Start;
		Query.EndLocation = End;
		Query.NavData = NavSys->GetDefaultNavDataInstance();

		FPathFindingResult PathResult = NavSys->FindPathSync(Query);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("path_found"), PathResult.IsSuccessful());

		if (PathResult.IsSuccessful() && PathResult.Path.IsValid())
		{
			const TArray<FNavPathPoint>& PathPoints = PathResult.Path->GetPathPoints();
			TArray<TSharedPtr<FJsonValue>> PointsArray;

			float TotalLength = 0.0f;
			for (int32 i = 0; i < PathPoints.Num(); ++i)
			{
				TSharedPtr<FJsonObject> Pt = MakeShared<FJsonObject>();
				Pt->SetNumberField(TEXT("x"), PathPoints[i].Location.X);
				Pt->SetNumberField(TEXT("y"), PathPoints[i].Location.Y);
				Pt->SetNumberField(TEXT("z"), PathPoints[i].Location.Z);
				PointsArray.Add(MakeShared<FJsonValueObject>(Pt));

				if (i > 0)
				{
					TotalLength += FVector::Dist(PathPoints[i - 1].Location, PathPoints[i].Location);
				}
			}

			Result->SetArrayField(TEXT("path_points"), PointsArray);
			Result->SetNumberField(TEXT("point_count"), PathPoints.Num());
			Result->SetNumberField(TEXT("path_length"), TotalLength);
		}
		else
		{
			Result->SetStringField(TEXT("reason"), TEXT("No valid path between the two points."));
		}

		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// get_nav_mesh_info
// ─────────────────────────────────────────────────────────────
class FTool_GetNavMeshInfo : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_nav_mesh_info");
		Info.Description = TEXT("Get statistics about the current navigation mesh (tile count, bounds, etc.).");
		Info.Annotations.Category = TEXT("Navigation");
		Info.Annotations.bReadOnly = true;

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

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		ANavigationData* NavData = NavSys->GetDefaultNavDataInstance();
		if (!NavData)
		{
			Result->SetStringField(TEXT("status"), TEXT("no_nav_data"));
			return FMCPToolResult::Ok(Result);
		}

		Result->SetStringField(TEXT("nav_data_class"), NavData->GetClass()->GetName());

		// Get bounds (GetNavMeshBounds doesn't exist in UE 5.7; use GetBounds instead)
		FBox NavBounds = NavData->GetBounds();
		if (NavBounds.IsValid)
		{
			TSharedPtr<FJsonObject> Min = MakeShared<FJsonObject>();
			Min->SetNumberField(TEXT("x"), NavBounds.Min.X);
			Min->SetNumberField(TEXT("y"), NavBounds.Min.Y);
			Min->SetNumberField(TEXT("z"), NavBounds.Min.Z);
			Result->SetObjectField(TEXT("bounds_min"), Min);

			TSharedPtr<FJsonObject> Max = MakeShared<FJsonObject>();
			Max->SetNumberField(TEXT("x"), NavBounds.Max.X);
			Max->SetNumberField(TEXT("y"), NavBounds.Max.Y);
			Max->SetNumberField(TEXT("z"), NavBounds.Max.Z);
			Result->SetObjectField(TEXT("bounds_max"), Max);
		}

		// Count NavMeshBoundsVolumes
		int32 BoundsCount = 0;
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		{
			BoundsCount++;
		}
		Result->SetNumberField(TEXT("bounds_volume_count"), BoundsCount);

		// RecastNavMesh specific info
		ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData);
		if (RecastNavMesh)
		{
			Result->SetNumberField(TEXT("tile_size_uu"), RecastNavMesh->TileSizeUU);
			Result->SetNumberField(TEXT("cell_size"), RecastNavMesh->CellSize);
			Result->SetNumberField(TEXT("cell_height"), RecastNavMesh->CellHeight);
			Result->SetNumberField(TEXT("agent_radius"), RecastNavMesh->AgentRadius);
			Result->SetNumberField(TEXT("agent_height"), RecastNavMesh->AgentHeight);
			Result->SetNumberField(TEXT("agent_max_slope"), RecastNavMesh->AgentMaxSlope);
			Result->SetNumberField(TEXT("agent_max_step_height"), RecastNavMesh->AgentMaxStepHeight);
		}

		Result->SetStringField(TEXT("status"), TEXT("ok"));
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_nav_modifier
// ─────────────────────────────────────────────────────────────
class FTool_AddNavModifier : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_nav_modifier");
		Info.Description = TEXT("Spawn a Nav Modifier Volume that changes the navigation area class within a region.");
		Info.Annotations.Category = TEXT("Navigation");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("location"), TEXT("object"), TEXT("{ x, y, z } center location"), true);
		AddParam(TEXT("extent"), TEXT("object"), TEXT("{ x, y, z } half-extent"), true);
		AddParam(TEXT("area_class"), TEXT("string"), TEXT("Nav area class: 'default', 'null' (blocked), 'obstacle'. Default: 'null'"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		const TSharedPtr<FJsonObject>& LocObj = Params->GetObjectField(TEXT("location"));
		const TSharedPtr<FJsonObject>& ExtObj = Params->GetObjectField(TEXT("extent"));

		FVector Location(LocObj->GetNumberField(TEXT("x")), LocObj->GetNumberField(TEXT("y")), LocObj->GetNumberField(TEXT("z")));
		FVector Extent(ExtObj->GetNumberField(TEXT("x")), ExtObj->GetNumberField(TEXT("y")), ExtObj->GetNumberField(TEXT("z")));

		FString AreaClassName = Params->HasField(TEXT("area_class")) ? Params->GetStringField(TEXT("area_class")).ToLower() : TEXT("null");

		TSubclassOf<UNavArea> AreaClass = UNavArea_Null::StaticClass();
		if (AreaClassName == TEXT("default"))
		{
			AreaClass = UNavArea_Default::StaticClass();
		}
		else if (AreaClassName == TEXT("obstacle"))
		{
			AreaClass = UNavArea_Obstacle::StaticClass();
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ANavModifierVolume* ModVolume = World->SpawnActor<ANavModifierVolume>(Location, FRotator::ZeroRotator, SpawnParams);

		if (!ModVolume)
		{
			return FMCPToolResult::Error(TEXT("Failed to spawn NavModifierVolume."));
		}

		ModVolume->SetActorScale3D(Extent / 100.0f);
		ModVolume->SetAreaClass(AreaClass);
		ModVolume->SetActorLabel(FString::Printf(TEXT("MCP_NavMod_%s"), *AreaClassName));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("actor"), ModVolume->GetName());
		Result->SetStringField(TEXT("area_class"), AreaClassName);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────
namespace UltimateMCPTools
{
	void RegisterNavigationTools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_BuildNavigation>());
		Registry.Register(MakeShared<FTool_AddNavMeshBounds>());
		Registry.Register(MakeShared<FTool_TestPath>());
		Registry.Register(MakeShared<FTool_GetNavMeshInfo>());
		Registry.Register(MakeShared<FTool_AddNavModifier>());
	}
}
