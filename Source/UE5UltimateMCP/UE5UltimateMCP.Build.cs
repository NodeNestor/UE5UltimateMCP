using UnrealBuildTool;

public class UE5UltimateMCP : ModuleRules
{
	public UE5UltimateMCP(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"UnrealEd",
			"BlueprintGraph",
			"Json",
			"JsonUtilities",
			"HTTPServer",
			"Sockets",
			"Networking"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Asset system
			"AssetRegistry",
			"AssetTools",

			// Blueprint editing
			"Kismet",
			"KismetCompiler",
			"EditorSubsystem",

			// Materials
			"MaterialEditor",
			"RHI",

			// Animation
			"AnimGraph",
			"AnimGraphRuntime",

			// UI / Slate
			"Slate",
			"SlateCore",
			"UMG",
			"UMGEditor",

			// Enhanced Input
			"EnhancedInput",

			// Viewport capture
			"ImageWrapper",

			// Sequencer
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			"Sequencer",

			// AI
			"AIModule",
			"GameplayTasks",
			"NavigationSystem",

			// Niagara
			"NiagaraCore",
			"Niagara",
			"NiagaraEditor",

			// Foliage
			"Foliage",

			// Landscape
			"Landscape",

			// Scripting utilities
			"EditorScriptingUtilities"
		});

		// Windows-only: Live Coding support
		if (Target.Platform == UnrealTargetPlatform.Win64 && Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("LiveCoding");
			PublicDefinitions.Add("WITH_LIVE_CODING=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_LIVE_CODING=0");
		}
	}
}
