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
			"UniversePlanet",

			// The Sprint 002 terrain renderer. Reached only through
			// UPlanetMeshBackend, so replacing it later is a swap rather than a
			// rewrite - see PlanetMeshBackend.h for why that matters in 5.8.
			"ProceduralMeshComponent",
		});

		// SQLite is a *private* dependency on purpose.
		//
		// Nothing outside the persistence backend may see it. The storage
		// abstraction exists so that a server backend can replace the local one
		// without touching gameplay, and that guarantee is worth nothing if a
		// gameplay header can include a SQLite type and quietly form a
		// dependency on it. Keeping it private makes the seam enforced by the
		// build rather than by discipline.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"SQLiteCore",
		});
	}
}
