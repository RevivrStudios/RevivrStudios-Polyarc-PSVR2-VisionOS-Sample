// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class My_Project : ModuleRules
{
	public My_Project(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore" });

		PrivateDependencyModuleNames.AddRange(new string[] { "XRBase", "EnhancedInput" });

		// SpatialAccessoryTracking plugin — needed for Swift bridge thumbstick input on visionOS
		if (Target.Platform == UnrealTargetPlatform.VisionOS)
		{
			PrivateDependencyModuleNames.Add("SpatialAccessoryTracking");
		}

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
