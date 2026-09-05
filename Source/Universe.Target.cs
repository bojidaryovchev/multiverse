// Copyright Universe Project. All Rights Reserved.

using UnrealBuildTool;

public class UniverseTarget : TargetRules
{
	public UniverseTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("Universe");
	}
}
