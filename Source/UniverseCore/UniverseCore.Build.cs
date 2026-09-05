// Copyright Universe Project. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// The deterministic mathematical core: universe coordinates, stable hashing,
/// the seed hierarchy and the binary format they serialise to.
///
/// The dependency list is just Core, deliberately. Nothing here may
/// reach for Engine, an Actor, a World or a tick, because this code has to be
/// callable from a worker thread, a commandlet, a test, and (in
/// Tools/StandaloneTests) a build with no engine present at all.
/// </summary>
public class UniverseCore : ModuleRules
{
	public UniverseCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			// Core only: UniverseCore contains no UObject, no reflection and no
			// UnrealHeaderTool input whatsoever. That is exactly what lets the
			// same sources compile and run in Tools/StandaloneTests with no
			// engine present.
			"Core",
		});
	}
}
