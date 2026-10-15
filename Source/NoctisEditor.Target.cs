// Noctis - cible éditeur (UE 5.4 / 5.5).
using UnrealBuildTool;
using System.Collections.Generic;

public class NoctisEditorTarget : TargetRules
{
	public NoctisEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Noctis", "NoctisEditor" });
	}
}
