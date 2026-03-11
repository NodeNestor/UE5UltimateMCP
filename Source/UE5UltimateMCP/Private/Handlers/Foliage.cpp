// Foliage / Instanced Static Mesh tools for UltimateMCP
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"

#include "InstancedFoliageActor.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "FoliageType.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

// ─────────────────────────────────────────────────────────────
// scatter_foliage
// ─────────────────────────────────────────────────────────────
class FTool_ScatterFoliage : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("scatter_foliage");
		Info.Description = TEXT("Scatter foliage instances of a static mesh randomly within bounds.");
		Info.Annotations.Category = TEXT("Foliage");
		Info.Annotations.bDestructive = false;
		Info.Annotations.bExpensive = true;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("mesh"), TEXT("string"), TEXT("Asset path of the Static Mesh to scatter"), true);
		AddParam(TEXT("count"), TEXT("number"), TEXT("Number of instances to scatter"), true);
		AddParam(TEXT("bounds_min"), TEXT("object"), TEXT("{ x, y, z } minimum bounds corner"), true);
		AddParam(TEXT("bounds_max"), TEXT("object"), TEXT("{ x, y, z } maximum bounds corner"), true);
		AddParam(TEXT("min_scale"), TEXT("number"), TEXT("Minimum random scale (default: 0.8)"), false);
		AddParam(TEXT("max_scale"), TEXT("number"), TEXT("Maximum random scale (default: 1.2)"), false);
		AddParam(TEXT("align_to_normal"), TEXT("boolean"), TEXT("Align instances to surface normal via trace (default: true)"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MeshPath = Params->GetStringField(TEXT("mesh"));
		int32 Count = (int32)Params->GetNumberField(TEXT("count"));

		const TSharedPtr<FJsonObject>& MinObj = Params->GetObjectField(TEXT("bounds_min"));
		const TSharedPtr<FJsonObject>& MaxObj = Params->GetObjectField(TEXT("bounds_max"));
		FVector BoundsMin(MinObj->GetNumberField(TEXT("x")), MinObj->GetNumberField(TEXT("y")), MinObj->GetNumberField(TEXT("z")));
		FVector BoundsMax(MaxObj->GetNumberField(TEXT("x")), MaxObj->GetNumberField(TEXT("y")), MaxObj->GetNumberField(TEXT("z")));

		float MinScale = Params->HasField(TEXT("min_scale")) ? (float)Params->GetNumberField(TEXT("min_scale")) : 0.8f;
		float MaxScale = Params->HasField(TEXT("max_scale")) ? (float)Params->GetNumberField(TEXT("max_scale")) : 1.2f;
		bool bAlignToNormal = Params->HasField(TEXT("align_to_normal")) ? Params->GetBoolField(TEXT("align_to_normal")) : true;

		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Static Mesh not found at '%s'."), *MeshPath));
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		// Get or create the Instanced Foliage Actor for this level
		AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, true);
		if (!IFA)
		{
			return FMCPToolResult::Error(TEXT("Failed to get InstancedFoliageActor."));
		}

		// Create or find foliage type for this mesh
		UFoliageType_InstancedStaticMesh* FoliageType = nullptr;

		// Check existing foliage types
		for (auto& Pair : IFA->GetFoliageInfos())
		{
			UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(Pair.Key);
			if (ISM && ISM->GetStaticMesh() == Mesh)
			{
				FoliageType = ISM;
				break;
			}
		}

		if (!FoliageType)
		{
			// Create a new transient foliage type
			FoliageType = NewObject<UFoliageType_InstancedStaticMesh>(IFA);
			FoliageType->SetStaticMesh(Mesh);
			FoliageType->Scaling = EFoliageScaling::Uniform;
			FoliageType->ScaleX = FFloatInterval(MinScale, MaxScale);
			FoliageType->ScaleY = FFloatInterval(MinScale, MaxScale);
			FoliageType->ScaleZ = FFloatInterval(MinScale, MaxScale);
			FoliageType->AlignToNormal = bAlignToNormal;

			IFA->AddFoliageType(FoliageType);
		}

		// Get the foliage info
		FFoliageInfo* FoliageInfo = IFA->FindInfo(FoliageType);
		if (!FoliageInfo)
		{
			return FMCPToolResult::Error(TEXT("Failed to find foliage info after adding type."));
		}

		// Scatter instances
		int32 PlacedCount = 0;
		FRandomStream RandomStream(FMath::Rand());

		for (int32 i = 0; i < Count; ++i)
		{
			FVector Location;
			Location.X = RandomStream.FRandRange(BoundsMin.X, BoundsMax.X);
			Location.Y = RandomStream.FRandRange(BoundsMin.Y, BoundsMax.Y);
			Location.Z = RandomStream.FRandRange(BoundsMin.Z, BoundsMax.Z);

			FRotator Rotation(0.0f, RandomStream.FRandRange(0.0f, 360.0f), 0.0f);
			float Scale = RandomStream.FRandRange(MinScale, MaxScale);

			// If align to normal, do a line trace down
			if (bAlignToNormal)
			{
				FHitResult Hit;
				FVector TraceStart = FVector(Location.X, Location.Y, BoundsMax.Z + 1000.0f);
				FVector TraceEnd = FVector(Location.X, Location.Y, BoundsMin.Z - 1000.0f);

				if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic))
				{
					Location = Hit.Location;
					if (bAlignToNormal)
					{
						Rotation = Hit.Normal.Rotation();
						Rotation.Pitch -= 90.0f; // Adjust for up vector
					}
				}
			}

			FFoliageInstance NewInstance;
			NewInstance.Location = Location;
			NewInstance.Rotation = Rotation;
			NewInstance.DrawScale3D = FVector(Scale);

			FoliageInfo->AddInstance(IFA, FoliageType, NewInstance);
			PlacedCount++;
		}

		IFA->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("mesh"), MeshPath);
		Result->SetNumberField(TEXT("instances_placed"), PlacedCount);
		Result->SetNumberField(TEXT("requested"), Count);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_foliage_type
// ─────────────────────────────────────────────────────────────
class FTool_AddFoliageType : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_foliage_type");
		Info.Description = TEXT("Create and register a new foliage type asset from a static mesh.");
		Info.Annotations.Category = TEXT("Foliage");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("mesh"), TEXT("string"), TEXT("Asset path of the Static Mesh"), true);
		AddParam(TEXT("min_scale"), TEXT("number"), TEXT("Minimum uniform scale (default: 0.8)"), false);
		AddParam(TEXT("max_scale"), TEXT("number"), TEXT("Maximum uniform scale (default: 1.2)"), false);
		AddParam(TEXT("name"), TEXT("string"), TEXT("Name for the foliage type asset (default: derived from mesh)"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString MeshPath = Params->GetStringField(TEXT("mesh"));
		float MinScale = Params->HasField(TEXT("min_scale")) ? (float)Params->GetNumberField(TEXT("min_scale")) : 0.8f;
		float MaxScale = Params->HasField(TEXT("max_scale")) ? (float)Params->GetNumberField(TEXT("max_scale")) : 1.2f;

		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
		if (!Mesh)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Static Mesh not found at '%s'."), *MeshPath));
		}

		FString AssetName = Params->HasField(TEXT("name")) ? Params->GetStringField(TEXT("name")) : FString::Printf(TEXT("FT_%s"), *Mesh->GetName());

		FString PackagePath = FString::Printf(TEXT("/Game/Foliage/%s"), *AssetName);
		FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return FMCPToolResult::Error(TEXT("Failed to create package."));
		}

		UFoliageType_InstancedStaticMesh* FoliageType = NewObject<UFoliageType_InstancedStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!FoliageType)
		{
			return FMCPToolResult::Error(TEXT("Failed to create UFoliageType_InstancedStaticMesh."));
		}

		FoliageType->SetStaticMesh(Mesh);
		FoliageType->Scaling = EFoliageScaling::Uniform;
		FoliageType->ScaleX = FFloatInterval(MinScale, MaxScale);
		FoliageType->ScaleY = FFloatInterval(MinScale, MaxScale);
		FoliageType->ScaleZ = FFloatInterval(MinScale, MaxScale);
		FoliageType->AlignToNormal = true;
		FoliageType->RandomYaw = true;

		FAssetRegistryModule::AssetCreated(FoliageType);
		FoliageType->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		UPackage::SavePackage(Package, FoliageType, *FilePath, SaveArgs);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), PackagePath);
		Result->SetStringField(TEXT("name"), AssetName);
		Result->SetStringField(TEXT("mesh"), MeshPath);
		Result->SetNumberField(TEXT("min_scale"), MinScale);
		Result->SetNumberField(TEXT("max_scale"), MaxScale);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// clear_foliage
// ─────────────────────────────────────────────────────────────
class FTool_ClearFoliage : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("clear_foliage");
		Info.Description = TEXT("Remove all foliage instances within a bounding region.");
		Info.Annotations.Category = TEXT("Foliage");
		Info.Annotations.bDestructive = true;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("bounds_min"), TEXT("object"), TEXT("{ x, y, z } minimum bounds corner"), true);
		AddParam(TEXT("bounds_max"), TEXT("object"), TEXT("{ x, y, z } maximum bounds corner"), true);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		const TSharedPtr<FJsonObject>& MinObj = Params->GetObjectField(TEXT("bounds_min"));
		const TSharedPtr<FJsonObject>& MaxObj = Params->GetObjectField(TEXT("bounds_max"));
		FVector BoundsMin(MinObj->GetNumberField(TEXT("x")), MinObj->GetNumberField(TEXT("y")), MinObj->GetNumberField(TEXT("z")));
		FVector BoundsMax(MaxObj->GetNumberField(TEXT("x")), MaxObj->GetNumberField(TEXT("y")), MaxObj->GetNumberField(TEXT("z")));

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, false);
		if (!IFA)
		{
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetNumberField(TEXT("removed"), 0);
			Result->SetStringField(TEXT("note"), TEXT("No foliage actor found in current level."));
			return FMCPToolResult::Ok(Result);
		}

		FBox ClearBounds(BoundsMin, BoundsMax);
		int32 TotalRemoved = 0;

		for (auto& Pair : IFA->GetFoliageInfos())
		{
			FFoliageInfo& FoliageInfo = *Pair.Value;
			TArray<int32> InstancesToRemove;

			for (int32 Idx = 0; Idx < FoliageInfo.Instances.Num(); ++Idx)
			{
				const FFoliageInstance& Instance = FoliageInfo.Instances[Idx];
				if (ClearBounds.IsInside(Instance.Location))
				{
					InstancesToRemove.Add(Idx);
				}
			}

			// Remove in reverse order to maintain indices
			for (int32 i = InstancesToRemove.Num() - 1; i >= 0; --i)
			{
				FoliageInfo.RemoveInstances(IFA, { InstancesToRemove[i] }, true);
				TotalRemoved++;
			}
		}

		IFA->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetNumberField(TEXT("removed"), TotalRemoved);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// get_foliage_info
// ─────────────────────────────────────────────────────────────
class FTool_GetFoliageInfo : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("get_foliage_info");
		Info.Description = TEXT("List all foliage types and their instance counts in the current level.");
		Info.Annotations.Category = TEXT("Foliage");
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

		AInstancedFoliageActor* IFA = AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, false);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

		if (!IFA)
		{
			Result->SetNumberField(TEXT("type_count"), 0);
			Result->SetNumberField(TEXT("total_instances"), 0);
			Result->SetStringField(TEXT("note"), TEXT("No foliage actor in current level."));
			return FMCPToolResult::Ok(Result);
		}

		TArray<TSharedPtr<FJsonValue>> TypesArray;
		int32 TotalInstances = 0;

		for (const auto& Pair : IFA->GetFoliageInfos())
		{
			TSharedPtr<FJsonObject> TypeObj = MakeShared<FJsonObject>();

			UFoliageType* FType = Pair.Key;
			const FFoliageInfo& FInfo = *Pair.Value;

			TypeObj->SetStringField(TEXT("name"), FType->GetName());
			TypeObj->SetStringField(TEXT("class"), FType->GetClass()->GetName());

			UFoliageType_InstancedStaticMesh* ISM = Cast<UFoliageType_InstancedStaticMesh>(FType);
			if (ISM && ISM->GetStaticMesh())
			{
				TypeObj->SetStringField(TEXT("mesh"), ISM->GetStaticMesh()->GetPathName());
			}

			int32 InstanceCount = FInfo.Instances.Num();
			TypeObj->SetNumberField(TEXT("instance_count"), InstanceCount);
			TotalInstances += InstanceCount;

			TypesArray.Add(MakeShared<FJsonValueObject>(TypeObj));
		}

		Result->SetNumberField(TEXT("type_count"), TypesArray.Num());
		Result->SetNumberField(TEXT("total_instances"), TotalInstances);
		Result->SetArrayField(TEXT("types"), TypesArray);

		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────
namespace UltimateMCPTools
{
	void RegisterFoliageTools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_ScatterFoliage>());
		Registry.Register(MakeShared<FTool_AddFoliageType>());
		Registry.Register(MakeShared<FTool_ClearFoliage>());
		Registry.Register(MakeShared<FTool_GetFoliageInfo>());
	}
}
