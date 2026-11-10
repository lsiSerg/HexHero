// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HexHero : ModuleRules
{
	public HexHero(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"DeveloperSettings",
			"ImageWrapper",
			"Json",
			"JsonUtilities",
			"AssetRegistry",
			"ProceduralMeshComponent",
			"Niagara"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "ImageCore", "RenderCore", "RHI", "MeshDescription", "StaticMeshDescription" });

		PublicIncludePaths.AddRange(new string[] {
			"HexHero",
			"HexHero/Core",
			"HexHero/Grid",
			"HexHero/Map",
			"HexHero/Camera",
			"HexHero/Player",
			"HexHero/UI",
			"HexHero/Planet"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
