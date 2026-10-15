// Noctis - game module: Unreal presentation, input, audio and UI around the NoctisCore simulation.
using System.IO;
using UnrealBuildTool;

public class Noctis : ModuleRules
{
	public Noctis(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		// The core is compiled without exceptions/RTTI; the game module must match (std::function, vectors cross the boundary).
		bEnableExceptions = false;
		bUseRTTI = false;

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"DeveloperSettings",
			"GameplayTags",
			"NoctisCore",
			"ChaosVehicles",
			"PhysicsCore",
			"UMG",
			"Slate",
			"SlateCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
			"AudioMixer",
			"AudioExtensions",
			"Niagara",
			"ProceduralMeshComponent",
			"PCG",
		});

		// Gameplay Debugger category (FGameplayDebuggerCategory_Noctis). This helper (also used by AIModule) adds the
		// GameplayDebugger dependency when the target enables it and defines WITH_GAMEPLAY_DEBUGGER(_CORE/_MENU) either way.
		// VERIFY on 5.5: ModuleRules.SetupGameplayDebuggerSupport(ReadOnlyTargetRules, bool bAddAsPublicDependency = false).
		SetupGameplayDebuggerSupport(Target);

		// Scientific data (Data/**/*.json) staged next to the executable as loose files so that packaged builds read
		// exactly the files the scientists edit. Resolved at runtime from FPaths::ProjectDir() / "Data".
		// VERIFY: recursive "..." wildcard for RuntimeDependencies on the target engine version.
		RuntimeDependencies.Add("$(ProjectDir)/Data/...", StagedFileType.NonUFS);
	}
}
