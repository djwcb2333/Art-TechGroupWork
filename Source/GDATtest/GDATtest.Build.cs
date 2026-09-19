// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GDATtest : ModuleRules
{
	public GDATtest(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"Niagara",
            "Landscape",
			"UMG",
			"Slate",
            "SlateCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "Json", "MoviePlayer", "RenderCore", "RHI" });

        // DoughWorld UMG authoring is editor-only; runtime widgets use UMG.
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "UMGEditor", "Kismet", "KismetCompiler", "AssetRegistry", "AssetTools", "MovieScene", "MovieSceneTracks" });
        }

		PublicIncludePaths.AddRange(new string[] {
			"GDATtest",
            "GDATtest/Gameplay",
			"GDATtest/Variant_Strategy",
			"GDATtest/Variant_Strategy/UI",
			"GDATtest/Variant_TwinStick",
			"GDATtest/Variant_TwinStick/AI",
			"GDATtest/Variant_TwinStick/Gameplay",
			"GDATtest/Variant_TwinStick/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
