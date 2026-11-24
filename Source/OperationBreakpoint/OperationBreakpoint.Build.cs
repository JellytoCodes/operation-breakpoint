// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OperationBreakpoint : ModuleRules
{
	public OperationBreakpoint(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", 
			"CoreUObject", 
			"Engine", 
			"InputCore", 
			"EnhancedInput",

			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",

            "OSC",
			"UMG",
            "MediaAssets"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
            "ApplicationCore",
            "Json",
            "JsonUtilities",
            "Niagara",
            "NavigationSystem",
            "AnimationCore",
            "PhysicsCore",
            "GeometryCollectionEngine",
            "Chaos",
            "AudioMixer",
            "RHI",
            "RenderCore",
            "ImageWrapper"
        });

        if (Target.bBuildEditor)
            PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "LevelEditor", "UMGEditor", "MovieScene", "StateTreeEditorModule", "PropertyBindingUtils", "AssetTools", "AssetRegistry", "RenderCore", "PhysicsUtilities", "AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "PlanarCut" });

        string ReportGenerator = Path.Combine(ModuleDirectory, "../../Binaries/Win64/TrainingReportGenerator.exe");
        if (File.Exists(ReportGenerator))
            RuntimeDependencies.Add("$(ProjectDir)/Binaries/Win64/TrainingReportGenerator.exe", StagedFileType.NonUFS);
	}
}
