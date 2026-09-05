// Copyright Universe Project. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// The gameplay module: the only place that knows about Actors, rendering and
/// input. It consumes UniverseCore and UniverseGeneration; the dependency
/// never runs the other way, which is what keeps the deterministic layers
/// testable without an engine.
/// </summary>
public class Universe : ModuleRules
{
	public Universe(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"UniverseCore",
			"UniverseGeneration",
		});
	}
}
