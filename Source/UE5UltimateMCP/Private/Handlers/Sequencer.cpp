// Sequencer / Cinematics tools for UltimateMCP
#include "Tools/MCPToolBase.h"
#include "Tools/MCPToolRegistry.h"

#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "MovieSceneBinding.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "LevelSequenceEditorBlueprintLibrary.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Camera/CameraActor.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"

// ─────────────────────────────────────────────────────────────
// create_level_sequence
// ─────────────────────────────────────────────────────────────
class FTool_CreateLevelSequence : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("create_level_sequence");
		Info.Description = TEXT("Create a new Level Sequence asset for cinematics.");
		Info.Annotations.Category = TEXT("Sequencer");
		Info.Annotations.bDestructive = false;

		FMCPParamSchema P;
		P.Name = TEXT("name"); P.Type = TEXT("string");
		P.Description = TEXT("Asset name for the new Level Sequence (e.g. 'MySequence')");
		P.bRequired = true;
		Info.Parameters.Add(P);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString Name = Params->GetStringField(TEXT("name"));
		if (Name.IsEmpty())
		{
			return FMCPToolResult::Error(TEXT("Parameter 'name' is required."));
		}

		FString PackagePath = FString::Printf(TEXT("/Game/Cinematics/%s"), *Name);
		FString PackageName = FPackageName::ObjectPathToPackageName(PackagePath);

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Failed to create package at '%s'."), *PackageName));
		}

		ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, *Name, RF_Public | RF_Standalone);
		if (!Sequence)
		{
			return FMCPToolResult::Error(TEXT("Failed to create ULevelSequence object."));
		}

		Sequence->Initialize();

		// Set a default playback range of 5 seconds at 30fps
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (MovieScene)
		{
			FFrameRate FrameRate(30, 1);
			MovieScene->SetDisplayRate(FrameRate);
			MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));

			FFrameNumber StartFrame = 0;
			FFrameNumber EndFrame = FrameRate.AsFrameNumber(5.0);
			MovieScene->SetPlaybackRange(TRange<FFrameNumber>(StartFrame, EndFrame + 1));
		}

		FAssetRegistryModule::AssetCreated(Sequence);
		Sequence->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		FString FilePath = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		UPackage::SavePackage(Package, Sequence, *FilePath, SaveArgs);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("path"), PackagePath);
		Result->SetStringField(TEXT("name"), Name);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_actor_to_sequence
// ─────────────────────────────────────────────────────────────
class FTool_AddActorToSequence : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_actor_to_sequence");
		Info.Description = TEXT("Bind an actor from the level to a Level Sequence as a possessable.");
		Info.Annotations.Category = TEXT("Sequencer");
		Info.Annotations.bDestructive = false;

		{
			FMCPParamSchema P;
			P.Name = TEXT("sequence"); P.Type = TEXT("string");
			P.Description = TEXT("Asset path of the Level Sequence (e.g. '/Game/Cinematics/MySeq')");
			P.bRequired = true;
			Info.Parameters.Add(P);
		}
		{
			FMCPParamSchema P;
			P.Name = TEXT("actor"); P.Type = TEXT("string");
			P.Description = TEXT("Name or label of the actor in the current level");
			P.bRequired = true;
			Info.Parameters.Add(P);
		}

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString SeqPath = Params->GetStringField(TEXT("sequence"));
		FString ActorName = Params->GetStringField(TEXT("actor"));

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqPath);
		if (!Sequence)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Level Sequence not found at '%s'."), *SeqPath));
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		AActor* FoundActor = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetActorLabel() == ActorName || It->GetName() == ActorName)
			{
				FoundActor = *It;
				break;
			}
		}
		if (!FoundActor)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' not found in the current level."), *ActorName));
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (!MovieScene)
		{
			return FMCPToolResult::Error(TEXT("Level Sequence has no MovieScene."));
		}

		// Create possessable binding
		FGuid BindingGuid = MovieScene->AddPossessable(FoundActor->GetActorLabel(), FoundActor->GetClass());

		// Bind the actor in the sequence
		Sequence->BindPossessableObject(BindingGuid, *FoundActor, World);

		Sequence->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("binding_id"), BindingGuid.ToString());
		Result->SetStringField(TEXT("actor"), FoundActor->GetActorLabel());
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_transform_keyframe
// ─────────────────────────────────────────────────────────────
class FTool_AddTransformKeyframe : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_transform_keyframe");
		Info.Description = TEXT("Add a transform keyframe to an actor bound in a Level Sequence.");
		Info.Annotations.Category = TEXT("Sequencer");
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("sequence"), TEXT("string"), TEXT("Asset path of the Level Sequence"), true);
		AddParam(TEXT("actor"), TEXT("string"), TEXT("Name or label of the bound actor"), true);
		AddParam(TEXT("time"), TEXT("number"), TEXT("Time in seconds for the keyframe"), true);
		AddParam(TEXT("location"), TEXT("object"), TEXT("{ x, y, z } location"), false);
		AddParam(TEXT("rotation"), TEXT("object"), TEXT("{ pitch, yaw, roll } rotation"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString SeqPath = Params->GetStringField(TEXT("sequence"));
		FString ActorName = Params->GetStringField(TEXT("actor"));
		double Time = Params->GetNumberField(TEXT("time"));

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqPath);
		if (!Sequence)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Level Sequence not found at '%s'."), *SeqPath));
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (!MovieScene)
		{
			return FMCPToolResult::Error(TEXT("Level Sequence has no MovieScene."));
		}

		// Find the binding for this actor
		FGuid BindingGuid;
		bool bFound = false;
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			if (Binding.GetName() == ActorName)
			{
				BindingGuid = Binding.GetObjectGuid();
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' is not bound in sequence '%s'. Use add_actor_to_sequence first."), *ActorName, *SeqPath));
		}

		// Find or create transform track
		UMovieScene3DTransformTrack* TransformTrack = MovieScene->FindTrack<UMovieScene3DTransformTrack>(BindingGuid);
		if (!TransformTrack)
		{
			TransformTrack = MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingGuid);
			if (!TransformTrack)
			{
				return FMCPToolResult::Error(TEXT("Failed to create 3D transform track."));
			}
		}

		// Ensure we have a section
		UMovieSceneSection* Section = nullptr;
		if (TransformTrack->GetAllSections().Num() == 0)
		{
			Section = TransformTrack->CreateNewSection();
			TransformTrack->AddSection(*Section);
			Section->SetRange(TRange<FFrameNumber>::All());
		}
		else
		{
			Section = TransformTrack->GetAllSections()[0];
		}

		UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
		if (!TransformSection)
		{
			return FMCPToolResult::Error(TEXT("Failed to get transform section."));
		}

		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameNumber FrameNumber = (Time * TickResolution).FloorToFrame();

		// Get channels via the channel proxy
		TArrayView<FMovieSceneFloatChannel*> FloatChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

		// Channels order: Location X,Y,Z (0,1,2), Rotation X,Y,Z (3,4,5), Scale X,Y,Z (6,7,8)
		if (FloatChannels.Num() < 9)
		{
			return FMCPToolResult::Error(TEXT("Transform section does not have the expected float channels."));
		}

		// Set location keys
		if (Params->HasField(TEXT("location")))
		{
			const TSharedPtr<FJsonObject>& Loc = Params->GetObjectField(TEXT("location"));
			float X = (float)Loc->GetNumberField(TEXT("x"));
			float Y = (float)Loc->GetNumberField(TEXT("y"));
			float Z = (float)Loc->GetNumberField(TEXT("z"));

			FloatChannels[0]->AddCubicKey(FrameNumber, X);
			FloatChannels[1]->AddCubicKey(FrameNumber, Y);
			FloatChannels[2]->AddCubicKey(FrameNumber, Z);
		}

		// Set rotation keys
		if (Params->HasField(TEXT("rotation")))
		{
			const TSharedPtr<FJsonObject>& Rot = Params->GetObjectField(TEXT("rotation"));
			float Pitch = (float)Rot->GetNumberField(TEXT("pitch"));
			float Yaw = (float)Rot->GetNumberField(TEXT("yaw"));
			float Roll = (float)Rot->GetNumberField(TEXT("roll"));

			FloatChannels[3]->AddCubicKey(FrameNumber, Pitch);
			FloatChannels[4]->AddCubicKey(FrameNumber, Yaw);
			FloatChannels[5]->AddCubicKey(FrameNumber, Roll);
		}

		Sequence->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("sequence"), SeqPath);
		Result->SetStringField(TEXT("actor"), ActorName);
		Result->SetNumberField(TEXT("time"), Time);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// add_camera_cut
// ─────────────────────────────────────────────────────────────
class FTool_AddCameraCut : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("add_camera_cut");
		Info.Description = TEXT("Add a camera cut at a specific time in a Level Sequence.");
		Info.Annotations.Category = TEXT("Sequencer");
		Info.Annotations.bDestructive = false;

		{
			FMCPParamSchema P;
			P.Name = TEXT("sequence"); P.Type = TEXT("string");
			P.Description = TEXT("Asset path of the Level Sequence");
			P.bRequired = true;
			Info.Parameters.Add(P);
		}
		{
			FMCPParamSchema P;
			P.Name = TEXT("camera"); P.Type = TEXT("string");
			P.Description = TEXT("Name of the camera actor to cut to");
			P.bRequired = true;
			Info.Parameters.Add(P);
		}
		{
			FMCPParamSchema P;
			P.Name = TEXT("time"); P.Type = TEXT("number");
			P.Description = TEXT("Time in seconds for the camera cut");
			P.bRequired = true;
			Info.Parameters.Add(P);
		}

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString SeqPath = Params->GetStringField(TEXT("sequence"));
		FString CameraName = Params->GetStringField(TEXT("camera"));
		double Time = Params->GetNumberField(TEXT("time"));

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqPath);
		if (!Sequence)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Level Sequence not found at '%s'."), *SeqPath));
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (!MovieScene)
		{
			return FMCPToolResult::Error(TEXT("Level Sequence has no MovieScene."));
		}

		// Find camera binding
		FGuid CameraGuid;
		bool bFoundCamera = false;
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			if (Binding.GetName() == CameraName)
			{
				CameraGuid = Binding.GetObjectGuid();
				bFoundCamera = true;
				break;
			}
		}
		if (!bFoundCamera)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Camera '%s' not bound in sequence. Use add_actor_to_sequence first."), *CameraName));
		}

		// Find or create camera cut track
		UMovieSceneCameraCutTrack* CameraCutTrack = MovieScene->FindMasterTrack<UMovieSceneCameraCutTrack>();
		if (!CameraCutTrack)
		{
			CameraCutTrack = MovieScene->AddMasterTrack<UMovieSceneCameraCutTrack>();
			if (!CameraCutTrack)
			{
				return FMCPToolResult::Error(TEXT("Failed to create camera cut track."));
			}
		}

		FFrameRate TickResolution = MovieScene->GetTickResolution();
		FFrameNumber FrameNumber = (Time * TickResolution).FloorToFrame();

		// Create a camera cut section
		UMovieSceneCameraCutSection* CutSection = Cast<UMovieSceneCameraCutSection>(CameraCutTrack->CreateNewSection());
		if (!CutSection)
		{
			return FMCPToolResult::Error(TEXT("Failed to create camera cut section."));
		}

		CutSection->SetCameraBindingID(FMovieSceneObjectBindingID(CameraGuid, MovieSceneSequenceID::Root));
		CutSection->SetRange(TRange<FFrameNumber>::AtLeast(FrameNumber));
		CameraCutTrack->AddSection(*CutSection);

		Sequence->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("sequence"), SeqPath);
		Result->SetStringField(TEXT("camera"), CameraName);
		Result->SetNumberField(TEXT("time"), Time);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// set_sequence_length
// ─────────────────────────────────────────────────────────────
class FTool_SetSequenceLength : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("set_sequence_length");
		Info.Description = TEXT("Set the playback duration of a Level Sequence.");
		Info.Annotations.Category = TEXT("Sequencer");
		Info.Annotations.bDestructive = false;

		{
			FMCPParamSchema P;
			P.Name = TEXT("sequence"); P.Type = TEXT("string");
			P.Description = TEXT("Asset path of the Level Sequence");
			P.bRequired = true;
			Info.Parameters.Add(P);
		}
		{
			FMCPParamSchema P;
			P.Name = TEXT("duration"); P.Type = TEXT("number");
			P.Description = TEXT("Duration in seconds");
			P.bRequired = true;
			Info.Parameters.Add(P);
		}

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString SeqPath = Params->GetStringField(TEXT("sequence"));
		double Duration = Params->GetNumberField(TEXT("duration"));

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqPath);
		if (!Sequence)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Level Sequence not found at '%s'."), *SeqPath));
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		if (!MovieScene)
		{
			return FMCPToolResult::Error(TEXT("Level Sequence has no MovieScene."));
		}

		FFrameRate DisplayRate = MovieScene->GetDisplayRate();
		FFrameNumber EndFrame = (Duration * MovieScene->GetTickResolution()).FloorToFrame();
		MovieScene->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), EndFrame + 1));

		Sequence->MarkPackageDirty();

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("sequence"), SeqPath);
		Result->SetNumberField(TEXT("duration"), Duration);
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// render_sequence_to_video
// ─────────────────────────────────────────────────────────────
class FTool_RenderSequenceToVideo : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("render_sequence_to_video");
		Info.Description = TEXT("Render a Level Sequence to a video file using Movie Render Pipeline / Automated Level Sequence Capture.");
		Info.Annotations.Category = TEXT("Sequencer");
		Info.Annotations.bExpensive = true;
		Info.Annotations.bDestructive = false;

		auto AddParam = [&](const TCHAR* N, const TCHAR* T, const TCHAR* D, bool R)
		{
			FMCPParamSchema P;
			P.Name = N; P.Type = T; P.Description = D; P.bRequired = R;
			Info.Parameters.Add(P);
		};

		AddParam(TEXT("sequence"), TEXT("string"), TEXT("Asset path of the Level Sequence"), true);
		AddParam(TEXT("output_path"), TEXT("string"), TEXT("Output directory for rendered video"), true);
		AddParam(TEXT("resolution"), TEXT("string"), TEXT("Resolution like '1920x1080' (default: 1920x1080)"), false);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString SeqPath = Params->GetStringField(TEXT("sequence"));
		FString OutputPath = Params->GetStringField(TEXT("output_path"));
		FString Resolution = Params->HasField(TEXT("resolution")) ? Params->GetStringField(TEXT("resolution")) : TEXT("1920x1080");

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqPath);
		if (!Sequence)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Level Sequence not found at '%s'."), *SeqPath));
		}

		// Parse resolution
		int32 ResX = 1920, ResY = 1080;
		FString XStr, YStr;
		if (Resolution.Split(TEXT("x"), &XStr, &YStr))
		{
			ResX = FCString::Atoi(*XStr);
			ResY = FCString::Atoi(*YStr);
		}

		// Use console command to trigger Movie Scene Capture
		// This launches the automated capture system which is the most reliable editor approach
		FString Command = FString::Printf(
			TEXT("SequencerCapture Sequence=%s Output=%s ResX=%d ResY=%d"),
			*SeqPath, *OutputPath, ResX, ResY);

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		// Set up the capture via the automated level sequence capture settings
		// Store settings to config for the capture system to pick up
		GConfig->SetString(TEXT("LevelSequenceCapture"), TEXT("LevelSequenceAsset"), *SeqPath, GEditorIni);
		GConfig->SetString(TEXT("LevelSequenceCapture"), TEXT("OutputDirectory"), *OutputPath, GEditorIni);
		GConfig->SetInt(TEXT("LevelSequenceCapture"), TEXT("ResolutionX"), ResX, GEditorIni);
		GConfig->SetInt(TEXT("LevelSequenceCapture"), TEXT("ResolutionY"), ResY, GEditorIni);

		// Execute via editor automation
		GEditor->Exec(World, *FString::Printf(
			TEXT("MOVIESCENECAPTURE SEQUENCE=%s OUTPUT=%s RESX=%d RESY=%d"),
			*SeqPath, *OutputPath, ResX, ResY));

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("sequence"), SeqPath);
		Result->SetStringField(TEXT("output_path"), OutputPath);
		Result->SetStringField(TEXT("resolution"), FString::Printf(TEXT("%dx%d"), ResX, ResY));
		Result->SetStringField(TEXT("status"), TEXT("capture_initiated"));
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// play_sequence
// ─────────────────────────────────────────────────────────────
class FTool_PlaySequence : public FMCPToolBase
{
public:
	FMCPToolInfo GetInfo() const override
	{
		FMCPToolInfo Info;
		Info.Name = TEXT("play_sequence");
		Info.Description = TEXT("Play (preview) a Level Sequence in the editor viewport.");
		Info.Annotations.Category = TEXT("Sequencer");
		Info.Annotations.bDestructive = false;

		FMCPParamSchema P;
		P.Name = TEXT("sequence"); P.Type = TEXT("string");
		P.Description = TEXT("Asset path of the Level Sequence to play");
		P.bRequired = true;
		Info.Parameters.Add(P);

		return Info;
	}

	FMCPToolResult Execute(const TSharedPtr<FJsonObject>& Params) override
	{
		FString SeqPath = Params->GetStringField(TEXT("sequence"));

		ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SeqPath);
		if (!Sequence)
		{
			return FMCPToolResult::Error(FString::Printf(TEXT("Level Sequence not found at '%s'."), *SeqPath));
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			return FMCPToolResult::Error(TEXT("No editor world available."));
		}

		// Spawn a temporary LevelSequenceActor to play the sequence
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALevelSequenceActor* SeqActor = World->SpawnActor<ALevelSequenceActor>(SpawnParams);
		if (!SeqActor)
		{
			return FMCPToolResult::Error(TEXT("Failed to spawn LevelSequenceActor."));
		}

		SeqActor->SetSequence(Sequence);

		FMovieSceneSequencePlaybackSettings PlaybackSettings;
		PlaybackSettings.bAutoPlay = true;
		PlaybackSettings.LoopCount.Value = 0;

		SeqActor->InitializePlayer();
		ULevelSequencePlayer* Player = SeqActor->GetSequencePlayer();
		if (Player)
		{
			Player->Play();
		}

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetStringField(TEXT("sequence"), SeqPath);
		Result->SetStringField(TEXT("status"), TEXT("playing"));
		return FMCPToolResult::Ok(Result);
	}
};

// ─────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────
namespace UltimateMCPTools
{
	void RegisterSequencerTools()
	{
		FMCPToolRegistry& Registry = FMCPToolRegistry::Get();
		Registry.Register(MakeShared<FTool_CreateLevelSequence>());
		Registry.Register(MakeShared<FTool_AddActorToSequence>());
		Registry.Register(MakeShared<FTool_AddTransformKeyframe>());
		Registry.Register(MakeShared<FTool_AddCameraCut>());
		Registry.Register(MakeShared<FTool_SetSequenceLength>());
		Registry.Register(MakeShared<FTool_RenderSequenceToVideo>());
		Registry.Register(MakeShared<FTool_PlaySequence>());
	}
}
