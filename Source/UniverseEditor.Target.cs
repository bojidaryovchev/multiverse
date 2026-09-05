// Copyright Universe Project. All Rights Reserved.

using UnrealBuildTool;

public class UniverseEditorTarget : TargetRules
{
	public UniverseEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("Universe");
	}
}
