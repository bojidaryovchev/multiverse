// Copyright Universe Project. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// Planet mathematics: cube-sphere topology, patch addressing, the quadtree,
/// the terrain function and patch mesh construction.
///
/// Like UniverseCore and UniverseGeneration this module has no UObject, no
/// reflection and no Engine dependency. Terrain generation runs on worker
/// threads and has to be verifiable without an editor, and the cheapest way to
/// guarantee both is to make it structurally impossible to touch an Actor from
/// here.
/// </summary>
public class UniversePlanet : ModuleRules
{
	public UniversePlanet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"UniverseCore",
			"UniverseGeneration",
		});
	}
}
