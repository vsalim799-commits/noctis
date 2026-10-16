// NoctisEditor - editor-only tooling: scientific data validation (Message Log "Noctis", commandlet for CI), asset
// validation of the per-species visual data, and the "reload scientific data" command for running PIE sessions.
using System.IO;
using UnrealBuildTool;

public class NoctisEditor : ModuleRules
{
	public NoctisEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// Includes NoctisCore headers (std::function, std::vector across the boundary): same settings as the core.
		bEnableExceptions = false;
		bUseRTTI = false;

		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Noctis",
			"NoctisCore",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"ToolMenus",
			"MessageLog",
			"AssetRegistry",
			// Plugin "DataValidation" (enabled in Noctis.uproject): UEditorValidatorBase.
			"DataValidation",
		});
	}
}
