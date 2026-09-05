// Copyright Universe Project. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// Deterministic astronomical generation: turns a seed and an address into
/// star system data. Pure data production - it never creates Actors and never
/// decides how anything looks.
/// </summary>
public class UniverseGeneration : ModuleRules
{
	public UniverseGeneration(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			// As with UniverseCore: no reflection, therefore no CoreUObject.
			"Core",
			"UniverseCore",
		});
	}
}
