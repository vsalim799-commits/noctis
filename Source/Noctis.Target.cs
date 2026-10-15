// Noctis - cible jeu (UE 5.4 / 5.5).
using UnrealBuildTool;
using System.Collections.Generic;

public class NoctisTarget : TargetRules
{
	public NoctisTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		// V5 / Latest exist from UE 5.4 on. For UE 5.3 use BuildSettingsVersion.V4 and EngineIncludeOrderVersion.Unreal5_3.
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		// C++20 is the engine default since 5.3; the modules also request it explicitly (ModuleRules.CppStandard).
		// NoctisCore is pulled in as a dependency of Noctis.
		ExtraModuleNames.Add("Noctis");
	}
}
