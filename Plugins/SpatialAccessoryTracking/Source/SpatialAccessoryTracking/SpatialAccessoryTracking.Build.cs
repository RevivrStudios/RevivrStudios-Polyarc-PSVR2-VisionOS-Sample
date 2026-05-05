using UnrealBuildTool;
using System.IO;

public class SpatialAccessoryTracking : ModuleRules
{
	public SpatialAccessoryTracking(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"HeadMountedDisplay",
			"XRBase"          // FDefaultXRCamera, FSceneViewExtensions
		});

		if (Target.Platform == UnrealTargetPlatform.VisionOS)
		{
			// Apple frameworks required for ARKit accessory tracking
			PublicFrameworks.AddRange(new string[] {
				"ARKit",
				"GameController",
				"RealityKit"
			});

			// Enable Swift interop: UBT will compile .swift files and generate bridging headers
			// The SwiftInteropHeader declares @_cdecl functions that Swift implements
			SwiftInteropHeader = Path.Combine(ModuleDirectory, "Public", "SpatialAccessoryBridge.h");

			// Enable Swift interop for .swift files in this module
			PrivateDefinitions.Add("WITH_SPATIAL_ACCESSORY_TRACKING=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_SPATIAL_ACCESSORY_TRACKING=0");
		}
	}
}
