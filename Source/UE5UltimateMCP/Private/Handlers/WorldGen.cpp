// Procedural world generation tools
// Ported from world-builder-mcp's Python algorithms to C++ using UE5 actor spawning

#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EditorAssetLibrary.h"
#include "GameFramework/Actor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Math/UnrealMathUtility.h"

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

namespace
{
	FVector JsonArrayToVec(const TSharedPtr<FJsonObject>& Obj, const FString& Field, const FVector& Default = FVector::ZeroVector)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (Obj->TryGetArrayField(Field, Arr) && Arr->Num() >= 3)
		{
			return FVector((*Arr)[0]->AsNumber(), (*Arr)[1]->AsNumber(), (*Arr)[2]->AsNumber());
		}
		return Default;
	}

	/** Spawn a StaticMeshActor with the given mesh, location, and scale. Returns the actor or nullptr. */
	AStaticMeshActor* SpawnBlock(UWorld* World, const FString& Name, const FString& MeshPath,
		const FVector& Location, const FVector& Scale, const FRotator& Rotation = FRotator::ZeroRotator)
	{
		if (!World) return nullptr;

		FActorSpawnParameters SP;
		SP.Name = *Name;
		SP.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Location, Rotation, SP);
		if (!Actor) return nullptr;

		UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
		if (Mesh)
		{
			Actor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		}

		FTransform T = Actor->GetTransform();
		T.SetScale3D(Scale);
		Actor->SetActorTransform(T);
		return Actor;
	}

	/** Return the editor world. */
	UWorld* GetWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	/** Convert spawned count to result JSON. */
	TSharedPtr<FJsonObject> MakeGenResult(int32 Count, const FString& StructureType)
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("structure"), StructureType);
		R->SetNumberField(TEXT("actors_spawned"), Count);
		return R;
	}

	/** Default cube mesh path. */
	static const FString DefaultMesh = TEXT("/Engine/BasicShapes/Cube.Cube");
	static const FString CylinderMesh = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const FString SphereMesh = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const FString ConeMesh = TEXT("/Engine/BasicShapes/Cone.Cone");
}

// ---------------------------------------------------------------------------
// create_wall
// ---------------------------------------------------------------------------

class FTool_CreateWall : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_wall");
		Info.Description = TEXT("Create a wall from cubes. Specify length (blocks), height (blocks), orientation (x or y), and block_size.");
		Info.Parameters = {
			{TEXT("length"),      TEXT("number"), TEXT("Wall length in blocks (default 5)"), false, nullptr},
			{TEXT("height"),      TEXT("number"), TEXT("Wall height in blocks (default 2)"), false, nullptr},
			{TEXT("block_size"),  TEXT("number"), TEXT("Size of each block in cm (default 100)"), false, nullptr},
			{TEXT("orientation"), TEXT("string"), TEXT("'x' or 'y' axis (default 'x')"), false, nullptr},
			{TEXT("location"),    TEXT("array"),  TEXT("[x, y, z] origin"), false, nullptr},
			{TEXT("material"),    TEXT("string"), TEXT("Mesh asset path (default cube)"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		int32 Length    = Params->HasField(TEXT("length"))     ? static_cast<int32>(Params->GetNumberField(TEXT("length")))    : 5;
		int32 Height    = Params->HasField(TEXT("height"))     ? static_cast<int32>(Params->GetNumberField(TEXT("height")))    : 2;
		float BlockSize = Params->HasField(TEXT("block_size")) ? static_cast<float>(Params->GetNumberField(TEXT("block_size"))): 100.f;
		FString Orient  = TEXT("x");
		Params->TryGetStringField(TEXT("orientation"), Orient);
		FVector Origin  = JsonArrayToVec(Params, TEXT("location"));
		FString Mesh    = DefaultMesh;
		Params->TryGetStringField(TEXT("material"), Mesh);

		float Scale = BlockSize / 100.f;
		FVector ScaleVec(Scale, Scale, Scale);
		int32 Count = 0;

		for (int32 H = 0; H < Height; ++H)
		{
			for (int32 I = 0; I < Length; ++I)
			{
				FVector Loc;
				if (Orient == TEXT("y"))
					Loc = FVector(Origin.X, Origin.Y + I * BlockSize, Origin.Z + H * BlockSize);
				else
					Loc = FVector(Origin.X + I * BlockSize, Origin.Y, Origin.Z + H * BlockSize);

				FString Name = FString::Printf(TEXT("Wall_%d_%d"), H, I);
				if (SpawnBlock(World, Name, Mesh, Loc, ScaleVec))
					Count++;
			}
		}

		return FMCPToolResult::Ok(MakeGenResult(Count, TEXT("wall")));
	}
};

// ---------------------------------------------------------------------------
// create_tower
// ---------------------------------------------------------------------------

class FTool_CreateTower : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_tower");
		Info.Description = TEXT("Create a tower. Styles: cylindrical, square, tapered. Blocks are placed as a shell at each level.");
		Info.Parameters = {
			{TEXT("height"),     TEXT("number"), TEXT("Tower height in levels (default 10)"), false, nullptr},
			{TEXT("base_size"),  TEXT("number"), TEXT("Tower base size in blocks (default 4)"), false, nullptr},
			{TEXT("block_size"), TEXT("number"), TEXT("Block size in cm (default 100)"), false, nullptr},
			{TEXT("location"),   TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
			{TEXT("style"),      TEXT("string"), TEXT("cylindrical, square, or tapered (default cylindrical)"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.bExpensive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		int32 Height    = Params->HasField(TEXT("height"))     ? static_cast<int32>(Params->GetNumberField(TEXT("height")))    : 10;
		int32 BaseSize  = Params->HasField(TEXT("base_size"))  ? static_cast<int32>(Params->GetNumberField(TEXT("base_size"))) : 4;
		float BlockSize = Params->HasField(TEXT("block_size")) ? static_cast<float>(Params->GetNumberField(TEXT("block_size"))): 100.f;
		FVector Origin  = JsonArrayToVec(Params, TEXT("location"));
		FString Style   = TEXT("cylindrical");
		Params->TryGetStringField(TEXT("style"), Style);

		float Scale = BlockSize / 100.f;
		FVector ScaleVec(Scale, Scale, Scale);
		int32 Count = 0;

		for (int32 Level = 0; Level < Height; ++Level)
		{
			float LevelZ = Origin.Z + Level * BlockSize;

			if (Style == TEXT("cylindrical"))
			{
				float Radius = (BaseSize / 2.f) * BlockSize;
				float Circumference = 2.f * PI * Radius;
				int32 NumBlocks = FMath::Max(8, static_cast<int32>(Circumference / BlockSize));

				for (int32 I = 0; I < NumBlocks; ++I)
				{
					float Angle = (2.f * PI * I) / NumBlocks;
					float X = Origin.X + Radius * FMath::Cos(Angle);
					float Y = Origin.Y + Radius * FMath::Sin(Angle);

					FString Name = FString::Printf(TEXT("Tower_%d_%d"), Level, I);
					if (SpawnBlock(World, Name, DefaultMesh, FVector(X, Y, LevelZ), ScaleVec))
						Count++;
				}
			}
			else if (Style == TEXT("tapered"))
			{
				int32 CurSize = FMath::Max(1, BaseSize - (Level / 2));
				float Half = CurSize / 2.f;

				for (int32 Side = 0; Side < 4; ++Side)
				{
					for (int32 I = 0; I < CurSize; ++I)
					{
						float X, Y;
						FString SideName;
						switch (Side)
						{
						case 0: X = Origin.X + (I - Half + 0.5f) * BlockSize; Y = Origin.Y - Half * BlockSize; SideName = TEXT("front"); break;
						case 1: X = Origin.X + Half * BlockSize;              Y = Origin.Y + (I - Half + 0.5f) * BlockSize; SideName = TEXT("right"); break;
						case 2: X = Origin.X + (Half - I - 0.5f) * BlockSize; Y = Origin.Y + Half * BlockSize; SideName = TEXT("back");  break;
						default: X = Origin.X - Half * BlockSize;             Y = Origin.Y + (Half - I - 0.5f) * BlockSize; SideName = TEXT("left");  break;
						}

						FString Name = FString::Printf(TEXT("Tower_%d_%s_%d"), Level, *SideName, I);
						if (SpawnBlock(World, Name, DefaultMesh, FVector(X, Y, LevelZ), ScaleVec))
							Count++;
					}
				}
			}
			else // square
			{
				float Half = BaseSize / 2.f;
				for (int32 Side = 0; Side < 4; ++Side)
				{
					for (int32 I = 0; I < BaseSize; ++I)
					{
						float X, Y;
						FString SideName;
						switch (Side)
						{
						case 0: X = Origin.X + (I - Half + 0.5f) * BlockSize; Y = Origin.Y - Half * BlockSize; SideName = TEXT("front"); break;
						case 1: X = Origin.X + Half * BlockSize;              Y = Origin.Y + (I - Half + 0.5f) * BlockSize; SideName = TEXT("right"); break;
						case 2: X = Origin.X + (Half - I - 0.5f) * BlockSize; Y = Origin.Y + Half * BlockSize; SideName = TEXT("back");  break;
						default: X = Origin.X - Half * BlockSize;             Y = Origin.Y + (Half - I - 0.5f) * BlockSize; SideName = TEXT("left");  break;
						}

						FString Name = FString::Printf(TEXT("Tower_%d_%s_%d"), Level, *SideName, I);
						if (SpawnBlock(World, Name, DefaultMesh, FVector(X, Y, LevelZ), ScaleVec))
							Count++;
					}
				}
			}

			// Corner decorations every 3 levels
			if (Level % 3 == 2 && Level < Height - 1)
			{
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					float Angle = Corner * PI / 2.f;
					float DX = (BaseSize / 2.f + 0.5f) * BlockSize * FMath::Cos(Angle);
					float DY = (BaseSize / 2.f + 0.5f) * BlockSize * FMath::Sin(Angle);

					FString Name = FString::Printf(TEXT("Tower_%d_detail_%d"), Level, Corner);
					if (SpawnBlock(World, Name, CylinderMesh, FVector(Origin.X + DX, Origin.Y + DY, LevelZ), ScaleVec * 0.7f))
						Count++;
				}
			}
		}

		TSharedPtr<FJsonObject> Result = MakeGenResult(Count, TEXT("tower"));
		Result->SetStringField(TEXT("style"), Style);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// create_staircase
// ---------------------------------------------------------------------------

class FTool_CreateStaircase : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_staircase");
		Info.Description = TEXT("Create a staircase from cubes. Each step advances in X and rises in Z.");
		Info.Parameters = {
			{TEXT("steps"),     TEXT("number"), TEXT("Number of steps (default 5)"), false, nullptr},
			{TEXT("step_size"), TEXT("array"),  TEXT("[width, depth, height] per step in cm (default [100,100,50])"), false, nullptr},
			{TEXT("location"),  TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		int32 Steps = Params->HasField(TEXT("steps")) ? static_cast<int32>(Params->GetNumberField(TEXT("steps"))) : 5;
		FVector StepSize = JsonArrayToVec(Params, TEXT("step_size"), FVector(100.f, 100.f, 50.f));
		FVector Origin = JsonArrayToVec(Params, TEXT("location"));

		int32 Count = 0;
		for (int32 I = 0; I < Steps; ++I)
		{
			FVector Loc(Origin.X + I * StepSize.X, Origin.Y, Origin.Z + I * StepSize.Z);
			FVector Scale(StepSize.X / 100.f, StepSize.Y / 100.f, StepSize.Z / 100.f);

			FString Name = FString::Printf(TEXT("Stair_%d"), I);
			if (SpawnBlock(World, Name, DefaultMesh, Loc, Scale))
				Count++;
		}

		return FMCPToolResult::Ok(MakeGenResult(Count, TEXT("staircase")));
	}
};

// ---------------------------------------------------------------------------
// create_arch
// ---------------------------------------------------------------------------

class FTool_CreateArch : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_arch");
		Info.Description = TEXT("Create a semicircular arch from cubes in the XZ plane.");
		Info.Parameters = {
			{TEXT("radius"),   TEXT("number"), TEXT("Arch radius in cm (default 300)"), false, nullptr},
			{TEXT("segments"), TEXT("number"), TEXT("Number of segments (default 6)"), false, nullptr},
			{TEXT("location"), TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		float Radius = Params->HasField(TEXT("radius")) ? static_cast<float>(Params->GetNumberField(TEXT("radius"))) : 300.f;
		int32 Segments = Params->HasField(TEXT("segments")) ? static_cast<int32>(Params->GetNumberField(TEXT("segments"))) : 6;
		FVector Origin = JsonArrayToVec(Params, TEXT("location"));

		float AngleStep = PI / Segments;
		float Scale = Radius / 300.f / 2.f;
		FVector ScaleVec(Scale, Scale, Scale);
		int32 Count = 0;

		for (int32 I = 0; I <= Segments; ++I)
		{
			float Theta = AngleStep * I;
			float X = Radius * FMath::Cos(Theta);
			float Z = Radius * FMath::Sin(Theta);

			FString Name = FString::Printf(TEXT("Arch_%d"), I);
			if (SpawnBlock(World, Name, DefaultMesh, FVector(Origin.X + X, Origin.Y, Origin.Z + Z), ScaleVec))
				Count++;
		}

		return FMCPToolResult::Ok(MakeGenResult(Count, TEXT("arch")));
	}
};

// ---------------------------------------------------------------------------
// create_pyramid
// ---------------------------------------------------------------------------

class FTool_CreatePyramid : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_pyramid");
		Info.Description = TEXT("Create a stepped pyramid from cubes. Each level shrinks by one block in X and Y.");
		Info.Parameters = {
			{TEXT("base_size"),  TEXT("number"), TEXT("Base size in blocks per side (default 3)"), false, nullptr},
			{TEXT("block_size"), TEXT("number"), TEXT("Block size in cm (default 100)"), false, nullptr},
			{TEXT("location"),   TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		int32 BaseSize  = Params->HasField(TEXT("base_size"))  ? static_cast<int32>(Params->GetNumberField(TEXT("base_size")))  : 3;
		float BlockSize = Params->HasField(TEXT("block_size")) ? static_cast<float>(Params->GetNumberField(TEXT("block_size"))) : 100.f;
		FVector Origin  = JsonArrayToVec(Params, TEXT("location"));

		float Scale = BlockSize / 100.f;
		FVector ScaleVec(Scale, Scale, Scale);
		int32 Count = 0;

		for (int32 Level = 0; Level < BaseSize; ++Level)
		{
			int32 LayerSize = BaseSize - Level;
			for (int32 X = 0; X < LayerSize; ++X)
			{
				for (int32 Y = 0; Y < LayerSize; ++Y)
				{
					FVector Loc(
						Origin.X + (X - (LayerSize - 1) / 2.f) * BlockSize,
						Origin.Y + (Y - (LayerSize - 1) / 2.f) * BlockSize,
						Origin.Z + Level * BlockSize
					);

					FString Name = FString::Printf(TEXT("Pyramid_%d_%d_%d"), Level, X, Y);
					if (SpawnBlock(World, Name, DefaultMesh, Loc, ScaleVec))
						Count++;
				}
			}
		}

		return FMCPToolResult::Ok(MakeGenResult(Count, TEXT("pyramid")));
	}
};

// ---------------------------------------------------------------------------
// create_maze  (recursive backtracking)
// ---------------------------------------------------------------------------

class FTool_CreateMaze : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_maze");
		Info.Description = TEXT("Generate a solvable maze using recursive backtracking. Creates walls from cubes with an entrance and exit.");
		Info.Parameters = {
			{TEXT("rows"),        TEXT("number"), TEXT("Rows (default 8)"), false, nullptr},
			{TEXT("cols"),        TEXT("number"), TEXT("Columns (default 8)"), false, nullptr},
			{TEXT("cell_size"),   TEXT("number"), TEXT("Cell size in cm (default 300)"), false, nullptr},
			{TEXT("wall_height"), TEXT("number"), TEXT("Wall height in blocks (default 3)"), false, nullptr},
			{TEXT("location"),    TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.bExpensive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		int32 Rows       = Params->HasField(TEXT("rows"))        ? static_cast<int32>(Params->GetNumberField(TEXT("rows")))        : 8;
		int32 Cols       = Params->HasField(TEXT("cols"))        ? static_cast<int32>(Params->GetNumberField(TEXT("cols")))         : 8;
		float CellSize   = Params->HasField(TEXT("cell_size"))   ? static_cast<float>(Params->GetNumberField(TEXT("cell_size")))   : 300.f;
		int32 WallHeight = Params->HasField(TEXT("wall_height")) ? static_cast<int32>(Params->GetNumberField(TEXT("wall_height"))) : 3;
		FVector Origin   = JsonArrayToVec(Params, TEXT("location"));

		// Clamp for sanity
		Rows = FMath::Clamp(Rows, 2, 30);
		Cols = FMath::Clamp(Cols, 2, 30);
		WallHeight = FMath::Clamp(WallHeight, 1, 10);

		int32 GridH = Rows * 2 + 1;
		int32 GridW = Cols * 2 + 1;

		// Initialize maze grid: true = wall
		TArray<TArray<bool>> Maze;
		Maze.SetNum(GridH);
		for (int32 R = 0; R < GridH; ++R)
		{
			Maze[R].SetNum(GridW);
			for (int32 C = 0; C < GridW; ++C)
				Maze[R][C] = true;
		}

		// Recursive backtracking (iterative stack to avoid stack overflow)
		struct FCell { int32 Row, Col; };
		TArray<FCell> Stack;
		Maze[1][1] = false;
		Stack.Push({0, 0});

		// Direction offsets: up, right, down, left
		static const int32 DR[] = {-1, 0, 1, 0};
		static const int32 DC[] = {0, 1, 0, -1};

		while (Stack.Num() > 0)
		{
			FCell& Current = Stack.Last();

			// Find unvisited neighbors
			TArray<int32> Dirs;
			for (int32 D = 0; D < 4; ++D)
			{
				int32 NR = Current.Row + DR[D];
				int32 NC = Current.Col + DC[D];
				if (NR >= 0 && NR < Rows && NC >= 0 && NC < Cols && Maze[NR * 2 + 1][NC * 2 + 1])
					Dirs.Add(D);
			}

			if (Dirs.Num() == 0)
			{
				Stack.Pop(EAllowShrinking::No);
				continue;
			}

			// Pick random direction
			int32 D = Dirs[FMath::RandRange(0, Dirs.Num() - 1)];
			int32 NR = Current.Row + DR[D];
			int32 NC = Current.Col + DC[D];

			// Carve wall between
			Maze[Current.Row * 2 + 1 + DR[D]][Current.Col * 2 + 1 + DC[D]] = false;
			// Carve destination cell
			Maze[NR * 2 + 1][NC * 2 + 1] = false;

			Stack.Push({NR, NC});
		}

		// Entrance and exit
		Maze[1][0] = false;
		Maze[Rows * 2 - 1][Cols * 2] = false;

		// Spawn wall blocks
		float Scale = CellSize / 100.f;
		FVector ScaleVec(Scale, Scale, Scale);
		int32 Count = 0;

		for (int32 R = 0; R < GridH; ++R)
		{
			for (int32 C = 0; C < GridW; ++C)
			{
				if (!Maze[R][C]) continue;

				for (int32 H = 0; H < WallHeight; ++H)
				{
					float XPos = Origin.X + (C - GridW / 2.f) * CellSize;
					float YPos = Origin.Y + (R - GridH / 2.f) * CellSize;
					float ZPos = Origin.Z + H * CellSize;

					FString Name = FString::Printf(TEXT("Maze_Wall_%d_%d_%d"), R, C, H);
					if (SpawnBlock(World, Name, DefaultMesh, FVector(XPos, YPos, ZPos), ScaleVec))
						Count++;
				}
			}
		}

		// Entrance marker (cylinder)
		{
			FVector Loc(Origin.X - GridW / 2.f * CellSize - CellSize, Origin.Y + (-GridH / 2.f + 1) * CellSize, Origin.Z + CellSize);
			SpawnBlock(World, TEXT("Maze_Entrance"), CylinderMesh, Loc, FVector(0.5f));
			Count++;
		}
		// Exit marker (sphere)
		{
			FVector Loc(Origin.X + GridW / 2.f * CellSize + CellSize, Origin.Y + (-GridH / 2.f + Rows * 2 - 1) * CellSize, Origin.Z + CellSize);
			SpawnBlock(World, TEXT("Maze_Exit"), SphereMesh, Loc, FVector(0.5f));
			Count++;
		}

		TSharedPtr<FJsonObject> Result = MakeGenResult(Count, TEXT("maze"));
		Result->SetStringField(TEXT("maze_size"), FString::Printf(TEXT("%dx%d"), Rows, Cols));
		Result->SetStringField(TEXT("entrance"), TEXT("Left side (cylinder marker)"));
		Result->SetStringField(TEXT("exit"), TEXT("Right side (sphere marker)"));
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// create_castle (modular: walls, towers, keep, gate, moat)
// ---------------------------------------------------------------------------

class FTool_CreateCastle : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_castle");
		Info.Description = TEXT("Create a modular castle with outer walls, corner towers, a central keep, a gatehouse, and a moat ring.");
		Info.Parameters = {
			{TEXT("size"),     TEXT("string"), TEXT("small, medium, large, or epic (default medium)"), false, nullptr},
			{TEXT("location"), TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.bExpensive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		FString Size = TEXT("medium");
		Params->TryGetStringField(TEXT("size"), Size);
		FVector Origin = JsonArrayToVec(Params, TEXT("location"));

		// Size parameters
		struct FSizeParams { float Width; float WallH; float TowerH; int32 TowerCount; };
		TMap<FString, FSizeParams> SizeMap;
		SizeMap.Add(TEXT("small"),  {6000.f,  800.f,  1200.f, 4});
		SizeMap.Add(TEXT("medium"), {8000.f,  1000.f, 1600.f, 4});
		SizeMap.Add(TEXT("large"),  {12000.f, 1200.f, 2000.f, 8});
		SizeMap.Add(TEXT("epic"),   {16000.f, 1400.f, 2400.f, 8});

		const FSizeParams* SP = SizeMap.Find(Size);
		if (!SP) SP = SizeMap.Find(TEXT("medium"));

		float HalfW = SP->Width / 2.f;
		float WallThickness = 200.f;
		int32 Count = 0;

		// --- Outer walls (4 sides) ---
		// North wall
		SpawnBlock(World, TEXT("Castle_Wall_North"), DefaultMesh,
			FVector(Origin.X, Origin.Y + HalfW, Origin.Z + SP->WallH / 2.f),
			FVector(SP->Width / 100.f, WallThickness / 100.f, SP->WallH / 100.f));
		Count++;
		// South wall
		SpawnBlock(World, TEXT("Castle_Wall_South"), DefaultMesh,
			FVector(Origin.X, Origin.Y - HalfW, Origin.Z + SP->WallH / 2.f),
			FVector(SP->Width / 100.f, WallThickness / 100.f, SP->WallH / 100.f));
		Count++;
		// East wall
		SpawnBlock(World, TEXT("Castle_Wall_East"), DefaultMesh,
			FVector(Origin.X + HalfW, Origin.Y, Origin.Z + SP->WallH / 2.f),
			FVector(WallThickness / 100.f, SP->Width / 100.f, SP->WallH / 100.f));
		Count++;
		// West wall (with gate gap - split into two halves)
		float GateWidth = 400.f;
		float WallSegLen = (SP->Width - GateWidth) / 2.f;
		SpawnBlock(World, TEXT("Castle_Wall_West_Top"), DefaultMesh,
			FVector(Origin.X - HalfW, Origin.Y + (WallSegLen / 2.f + GateWidth / 2.f), Origin.Z + SP->WallH / 2.f),
			FVector(WallThickness / 100.f, WallSegLen / 100.f, SP->WallH / 100.f));
		Count++;
		SpawnBlock(World, TEXT("Castle_Wall_West_Bottom"), DefaultMesh,
			FVector(Origin.X - HalfW, Origin.Y - (WallSegLen / 2.f + GateWidth / 2.f), Origin.Z + SP->WallH / 2.f),
			FVector(WallThickness / 100.f, WallSegLen / 100.f, SP->WallH / 100.f));
		Count++;
		// Gate lintel
		SpawnBlock(World, TEXT("Castle_Gate_Lintel"), DefaultMesh,
			FVector(Origin.X - HalfW, Origin.Y, Origin.Z + SP->WallH * 0.8f),
			FVector(WallThickness / 100.f, GateWidth / 100.f, SP->WallH * 0.2f / 100.f));
		Count++;

		// --- Corner towers (cylindrical using stacked cubes) ---
		float CornerOffsets[4][2] = {
			{-HalfW, -HalfW}, {HalfW, -HalfW}, {HalfW, HalfW}, {-HalfW, HalfW}
		};
		const TCHAR* CornerNames[] = {TEXT("SW"), TEXT("SE"), TEXT("NE"), TEXT("NW")};

		for (int32 T = 0; T < 4; ++T)
		{
			float TX = Origin.X + CornerOffsets[T][0];
			float TY = Origin.Y + CornerOffsets[T][1];

			// Tower body
			SpawnBlock(World, FString::Printf(TEXT("Castle_Tower_%s"), CornerNames[T]), CylinderMesh,
				FVector(TX, TY, Origin.Z + SP->TowerH / 2.f),
				FVector(4.f, 4.f, SP->TowerH / 100.f));
			Count++;

			// Tower cap (cone)
			SpawnBlock(World, FString::Printf(TEXT("Castle_TowerCap_%s"), CornerNames[T]), ConeMesh,
				FVector(TX, TY, Origin.Z + SP->TowerH + 150.f),
				FVector(5.f, 5.f, 3.f));
			Count++;
		}

		// --- Additional mid-wall towers for large/epic ---
		if (SP->TowerCount > 4)
		{
			float MidOffsets[4][2] = {
				{0.f, -HalfW}, {HalfW, 0.f}, {0.f, HalfW}, {-HalfW, 0.f}
			};
			const TCHAR* MidNames[] = {TEXT("S"), TEXT("E"), TEXT("N"), TEXT("W")};
			for (int32 M = 0; M < 4; ++M)
			{
				float MX = Origin.X + MidOffsets[M][0];
				float MY = Origin.Y + MidOffsets[M][1];
				SpawnBlock(World, FString::Printf(TEXT("Castle_MidTower_%s"), MidNames[M]), CylinderMesh,
					FVector(MX, MY, Origin.Z + SP->TowerH * 0.8f / 2.f),
					FVector(3.f, 3.f, SP->TowerH * 0.8f / 100.f));
				Count++;
			}
		}

		// --- Central keep ---
		float KeepSize = SP->Width * 0.2f;
		float KeepHeight = SP->TowerH * 1.2f;
		SpawnBlock(World, TEXT("Castle_Keep"), DefaultMesh,
			FVector(Origin.X, Origin.Y, Origin.Z + KeepHeight / 2.f),
			FVector(KeepSize / 100.f, KeepSize / 100.f, KeepHeight / 100.f));
		Count++;

		// Keep tower on top
		SpawnBlock(World, TEXT("Castle_Keep_Tower"), CylinderMesh,
			FVector(Origin.X, Origin.Y, Origin.Z + KeepHeight + 200.f),
			FVector(2.f, 2.f, 4.f));
		Count++;

		// --- Moat (ring of flattened cylinders around the perimeter) ---
		float MoatRadius = HalfW + 400.f;
		int32 MoatSegments = 24;
		for (int32 S = 0; S < MoatSegments; ++S)
		{
			float Angle = (2.f * PI * S) / MoatSegments;
			float MX = Origin.X + MoatRadius * FMath::Cos(Angle);
			float MY = Origin.Y + MoatRadius * FMath::Sin(Angle);

			FString Name = FString::Printf(TEXT("Castle_Moat_%d"), S);
			SpawnBlock(World, Name, CylinderMesh,
				FVector(MX, MY, Origin.Z - 50.f),
				FVector(5.f, 5.f, 0.3f));
			Count++;
		}

		// --- Floor inside walls ---
		SpawnBlock(World, TEXT("Castle_Courtyard"), DefaultMesh,
			FVector(Origin.X, Origin.Y, Origin.Z - 10.f),
			FVector(SP->Width * 0.9f / 100.f, SP->Width * 0.9f / 100.f, 0.2f));
		Count++;

		TSharedPtr<FJsonObject> Result = MakeGenResult(Count, TEXT("castle"));
		Result->SetStringField(TEXT("size"), Size);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// create_town (street grid, buildings, lights, plaza)
// ---------------------------------------------------------------------------

class FTool_CreateTown : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_town");
		Info.Description = TEXT("Generate a town with a street grid, randomized buildings, street lights, and a central plaza.");
		Info.Parameters = {
			{TEXT("size"),     TEXT("string"), TEXT("small, medium, large, or metropolis (default medium)"), false, nullptr},
			{TEXT("style"),    TEXT("string"), TEXT("modern, cottage, mixed, downtown (default mixed)"), false, nullptr},
			{TEXT("location"), TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.bExpensive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		FString Size = TEXT("medium");
		Params->TryGetStringField(TEXT("size"), Size);
		FString Style = TEXT("mixed");
		Params->TryGetStringField(TEXT("style"), Style);
		FVector Origin = JsonArrayToVec(Params, TEXT("location"));

		struct FTownParams { int32 Blocks; float BlockSize; float MaxBuildingH; int32 Population; };
		TMap<FString, FTownParams> ParamMap;
		ParamMap.Add(TEXT("small"),      {3, 1500.f, 500.f,  20});
		ParamMap.Add(TEXT("medium"),     {5, 2000.f, 1000.f, 50});
		ParamMap.Add(TEXT("large"),      {7, 2500.f, 2000.f, 100});
		ParamMap.Add(TEXT("metropolis"), {10, 3000.f, 4000.f, 200});

		const FTownParams* TP = ParamMap.Find(Size);
		if (!TP) TP = ParamMap.Find(TEXT("medium"));

		float StreetW = TP->BlockSize * 0.3f;
		int32 Count = 0;
		int32 BuildingCount = 0;

		// --- Street grid ---
		for (int32 I = 0; I <= TP->Blocks; ++I)
		{
			float Offset = (I - TP->Blocks / 2.f) * TP->BlockSize;

			// East-West streets
			FString EWName = FString::Printf(TEXT("Town_Street_EW_%d"), I);
			SpawnBlock(World, EWName, DefaultMesh,
				FVector(Origin.X, Origin.Y + Offset, Origin.Z - 5.f),
				FVector(TP->Blocks * TP->BlockSize / 100.f, StreetW / 100.f, 0.1f));
			Count++;

			// North-South streets
			FString NSName = FString::Printf(TEXT("Town_Street_NS_%d"), I);
			SpawnBlock(World, NSName, DefaultMesh,
				FVector(Origin.X + Offset, Origin.Y, Origin.Z - 5.f),
				FVector(StreetW / 100.f, TP->Blocks * TP->BlockSize / 100.f, 0.1f));
			Count++;
		}

		// --- Buildings in each block ---
		float BuildArea = TP->BlockSize * 0.6f;

		for (int32 BX = 0; BX < TP->Blocks; ++BX)
		{
			for (int32 BY = 0; BY < TP->Blocks; ++BY)
			{
				if (BuildingCount >= TP->Population) break;

				// Central blocks are taller
				bool bCentral = FMath::Abs(BX - TP->Blocks / 2) <= 1 && FMath::Abs(BY - TP->Blocks / 2) <= 1;

				float CenterX = Origin.X + (BX - TP->Blocks / 2.f + 0.5f) * TP->BlockSize;
				float CenterY = Origin.Y + (BY - TP->Blocks / 2.f + 0.5f) * TP->BlockSize;

				// Random building dimensions
				float W = FMath::RandRange(BuildArea * 0.3f, BuildArea * 0.7f);
				float D = FMath::RandRange(BuildArea * 0.3f, BuildArea * 0.7f);
				float H = bCentral ? FMath::RandRange(TP->MaxBuildingH * 0.5f, TP->MaxBuildingH) : FMath::RandRange(300.f, TP->MaxBuildingH * 0.5f);

				// Offset within block
				float OX = FMath::RandRange(-BuildArea * 0.1f, BuildArea * 0.1f);
				float OY = FMath::RandRange(-BuildArea * 0.1f, BuildArea * 0.1f);

				FString BName = FString::Printf(TEXT("Town_Building_%d_%d"), BX, BY);
				SpawnBlock(World, BName, DefaultMesh,
					FVector(CenterX + OX, CenterY + OY, Origin.Z + H / 2.f),
					FVector(W / 100.f, D / 100.f, H / 100.f));
				Count++;
				BuildingCount++;

				// Roof detail for tall buildings
				if (H > 600.f)
				{
					FString RName = FString::Printf(TEXT("Town_Roof_%d_%d"), BX, BY);
					SpawnBlock(World, RName, CylinderMesh,
						FVector(CenterX + OX, CenterY + OY, Origin.Z + H + 50.f),
						FVector(W * 0.3f / 100.f, D * 0.3f / 100.f, 1.f));
					Count++;
				}
			}
		}

		// --- Street lights at intersections ---
		for (int32 IX = 0; IX <= TP->Blocks; ++IX)
		{
			for (int32 IY = 0; IY <= TP->Blocks; ++IY)
			{
				if (FMath::RandRange(0.f, 1.f) > 0.5f) continue; // Thin out lights

				float LX = Origin.X + (IX - TP->Blocks / 2.f) * TP->BlockSize;
				float LY = Origin.Y + (IY - TP->Blocks / 2.f) * TP->BlockSize;

				// Light pole
				FString PoleName = FString::Printf(TEXT("Town_LightPole_%d_%d"), IX, IY);
				SpawnBlock(World, PoleName, CylinderMesh,
					FVector(LX + StreetW, LY + StreetW, Origin.Z + 250.f),
					FVector(0.15f, 0.15f, 5.f));
				Count++;

				// Point light
				FActorSpawnParameters LSP;
				FString LightName = FString::Printf(TEXT("Town_Light_%d_%d"), IX, IY);
				LSP.Name = *LightName;
				LSP.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
				APointLight* Light = World->SpawnActor<APointLight>(APointLight::StaticClass(),
					FVector(LX + StreetW, LY + StreetW, Origin.Z + 500.f), FRotator::ZeroRotator, LSP);
				if (Light) Count++;
			}
		}

		// --- Central plaza (for large/metropolis) ---
		if (Size == TEXT("large") || Size == TEXT("metropolis"))
		{
			float PlazaSize = TP->BlockSize * 1.5f;
			SpawnBlock(World, TEXT("Town_Plaza"), DefaultMesh,
				FVector(Origin.X, Origin.Y, Origin.Z + 5.f),
				FVector(PlazaSize / 100.f, PlazaSize / 100.f, 0.1f));
			Count++;

			// Fountain in center
			SpawnBlock(World, TEXT("Town_Fountain"), CylinderMesh,
				FVector(Origin.X, Origin.Y, Origin.Z + 100.f),
				FVector(3.f, 3.f, 2.f));
			Count++;

			// Fountain spout
			SpawnBlock(World, TEXT("Town_Fountain_Spout"), ConeMesh,
				FVector(Origin.X, Origin.Y, Origin.Z + 300.f),
				FVector(0.5f, 0.5f, 2.f));
			Count++;
		}

		TSharedPtr<FJsonObject> Result = MakeGenResult(Count, TEXT("town"));
		Result->SetStringField(TEXT("size"), Size);
		Result->SetStringField(TEXT("style"), Style);
		Result->SetNumberField(TEXT("buildings"), BuildingCount);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// create_house
// ---------------------------------------------------------------------------

class FTool_CreateHouse : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_house");
		Info.Description = TEXT("Create a house with walls, floor, roof, and door opening. Styles: modern, cottage.");
		Info.Parameters = {
			{TEXT("width"),    TEXT("number"), TEXT("Width in cm (default 1200)"), false, nullptr},
			{TEXT("depth"),    TEXT("number"), TEXT("Depth in cm (default 1000)"), false, nullptr},
			{TEXT("height"),   TEXT("number"), TEXT("Wall height in cm (default 600)"), false, nullptr},
			{TEXT("style"),    TEXT("string"), TEXT("modern or cottage (default modern)"), false, nullptr},
			{TEXT("location"), TEXT("array"),  TEXT("[x, y, z]"), false, nullptr},
		};
		Info.Annotations.bDestructive = true;
		Info.Annotations.Category = TEXT("WorldGen");
		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		UWorld* World = GetWorld();
		if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

		float Width  = Params->HasField(TEXT("width"))  ? static_cast<float>(Params->GetNumberField(TEXT("width")))  : 1200.f;
		float Depth  = Params->HasField(TEXT("depth"))  ? static_cast<float>(Params->GetNumberField(TEXT("depth")))  : 1000.f;
		float Height = Params->HasField(TEXT("height")) ? static_cast<float>(Params->GetNumberField(TEXT("height"))) : 600.f;
		FString Style = TEXT("modern");
		Params->TryGetStringField(TEXT("style"), Style);
		FVector Origin = JsonArrayToVec(Params, TEXT("location"));

		if (Style == TEXT("cottage"))
		{
			Width  *= 0.8f;
			Depth  *= 0.8f;
			Height *= 0.9f;
		}

		float WallThick = 20.f;
		float FloorThick = 30.f;
		float DoorW = 120.f;
		float DoorH = 240.f;
		int32 Count = 0;

		// Foundation
		SpawnBlock(World, TEXT("House_Foundation"), DefaultMesh,
			FVector(Origin.X, Origin.Y, Origin.Z - FloorThick / 2.f),
			FVector((Width + 200.f) / 100.f, (Depth + 200.f) / 100.f, FloorThick / 100.f));
		Count++;

		// Floor
		SpawnBlock(World, TEXT("House_Floor"), DefaultMesh,
			FVector(Origin.X, Origin.Y, Origin.Z + FloorThick / 2.f),
			FVector(Width / 100.f, Depth / 100.f, FloorThick / 100.f));
		Count++;

		float BaseZ = Origin.Z + FloorThick;

		// Front wall - left of door
		float FrontSideW = (Width / 2.f - DoorW / 2.f);
		SpawnBlock(World, TEXT("House_FrontWall_L"), DefaultMesh,
			FVector(Origin.X - Width / 4.f - DoorW / 4.f, Origin.Y - Depth / 2.f, BaseZ + Height / 2.f),
			FVector(FrontSideW / 100.f, WallThick / 100.f, Height / 100.f));
		Count++;

		// Front wall - right of door
		SpawnBlock(World, TEXT("House_FrontWall_R"), DefaultMesh,
			FVector(Origin.X + Width / 4.f + DoorW / 4.f, Origin.Y - Depth / 2.f, BaseZ + Height / 2.f),
			FVector(FrontSideW / 100.f, WallThick / 100.f, Height / 100.f));
		Count++;

		// Front wall - above door
		SpawnBlock(World, TEXT("House_FrontWall_Top"), DefaultMesh,
			FVector(Origin.X, Origin.Y - Depth / 2.f, BaseZ + DoorH + (Height - DoorH) / 2.f),
			FVector(DoorW / 100.f, WallThick / 100.f, (Height - DoorH) / 100.f));
		Count++;

		// Back wall
		SpawnBlock(World, TEXT("House_BackWall"), DefaultMesh,
			FVector(Origin.X, Origin.Y + Depth / 2.f, BaseZ + Height / 2.f),
			FVector(Width / 100.f, WallThick / 100.f, Height / 100.f));
		Count++;

		// Left wall
		SpawnBlock(World, TEXT("House_LeftWall"), DefaultMesh,
			FVector(Origin.X - Width / 2.f, Origin.Y, BaseZ + Height / 2.f),
			FVector(WallThick / 100.f, Depth / 100.f, Height / 100.f));
		Count++;

		// Right wall
		SpawnBlock(World, TEXT("House_RightWall"), DefaultMesh,
			FVector(Origin.X + Width / 2.f, Origin.Y, BaseZ + Height / 2.f),
			FVector(WallThick / 100.f, Depth / 100.f, Height / 100.f));
		Count++;

		// Roof
		float RoofThick = 30.f;
		float RoofOverhang = 100.f;
		SpawnBlock(World, TEXT("House_Roof"), DefaultMesh,
			FVector(Origin.X, Origin.Y, BaseZ + Height + RoofThick / 2.f),
			FVector((Width + RoofOverhang * 2.f) / 100.f, (Depth + RoofOverhang * 2.f) / 100.f, RoofThick / 100.f));
		Count++;

		// Chimney for cottage
		if (Style == TEXT("cottage"))
		{
			SpawnBlock(World, TEXT("House_Chimney"), CylinderMesh,
				FVector(Origin.X + Width / 3.f, Origin.Y + Depth / 3.f, BaseZ + Height + RoofThick + 150.f),
				FVector(1.f, 1.f, 2.5f));
			Count++;
		}

		// Garage door for modern
		if (Style == TEXT("modern"))
		{
			SpawnBlock(World, TEXT("House_Garage"), DefaultMesh,
				FVector(Origin.X - Width / 3.f, Origin.Y - Depth / 2.f + WallThick / 2.f, BaseZ + 150.f),
				FVector(2.5f, 0.1f, 2.5f));
			Count++;
		}

		TSharedPtr<FJsonObject> Result = MakeGenResult(Count, TEXT("house"));
		Result->SetStringField(TEXT("style"), Style);
		Result->SetNumberField(TEXT("width"), Width);
		Result->SetNumberField(TEXT("depth"), Depth);
		Result->SetNumberField(TEXT("height"), Height);
		return FMCPToolResult::Ok(Result);
	}
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

namespace UltimateMCPTools
{
	void RegisterWorldGenTools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_CreateWall>());
		Registry.Register(MakeShared<FTool_CreateTower>());
		Registry.Register(MakeShared<FTool_CreateStaircase>());
		Registry.Register(MakeShared<FTool_CreateArch>());
		Registry.Register(MakeShared<FTool_CreatePyramid>());
		Registry.Register(MakeShared<FTool_CreateMaze>());
		Registry.Register(MakeShared<FTool_CreateCastle>());
		Registry.Register(MakeShared<FTool_CreateTown>());
		Registry.Register(MakeShared<FTool_CreateHouse>());
	}
}
