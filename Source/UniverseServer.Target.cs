// Copyright Universe Project. All Rights Reserved.

using UnrealBuildTool;

/// <summary>
/// The dedicated server target.
///
/// Sprint 007 moves authority for everything that two players must agree on -
/// identity, position, world modifications, persistence - out of the client and
/// into this. What it deliberately does *not* move is the procedural universe
/// itself: the server and every client can already regenerate the same world
/// from the same seed, so the universe is shared mathematical data rather than
/// something to replicate.
///
/// The server builds the same modules as the game. It has to: it needs planet
/// terrain to validate that a structure is being placed on ground that exists,
/// and climate to know whether a region has trees in it. What it does not have
/// is a viewport, so anything that assumes one has to be guarded - see
/// Docs/Architecture/Networking.md for the audit.
/// </summary>
public class UniverseServerTarget : TargetRules
{
	public UniverseServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;

		ExtraModuleNames.Add("Universe");
	}
}
