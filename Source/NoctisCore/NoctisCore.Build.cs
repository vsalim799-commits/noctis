// NoctisCore - the engine-agnostic simulation core compiled as an Unreal module.
//
// The same sources are built standalone by CMake (NOCTIS_STANDALONE). Rules that keep both builds identical:
//   * no exceptions, no RTTI (CMake builds with -fno-exceptions -fno-rtti -Werror as well);
//   * no shared PCH: core translation units only see CoreTypes.h + AssertionMacros.h (included by
//     Noctis/Core/Platform.h), never CoreMinimal/Windows.h, so UE/Windows macros (PI, check, near, far,
//     small, ERROR...) cannot leak into the core;
//   * no unity build: the core is also validated file by file in the standalone build.
using System.IO;
using UnrealBuildTool;

public class NoctisCore : ModuleRules
{
	public NoctisCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;
		CppStandard = CppStandardVersion.Cpp20;
		bEnableExceptions = false;
		bUseRTTI = false;

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

		// NOCTIS_API is mapped to NOCTISCORE_API in Noctis/Core/Platform.h; NOCTIS_STANDALONE must stay undefined here.
		PublicDefinitions.Add("NOCTIS_UNREAL=1");

		// Core is the only engine dependency: the core never includes engine headers beyond CoreTypes/AssertionMacros,
		// and NoctisCoreModule.cpp (module boilerplate + log bridge) needs Core only.
		PublicDependencyModuleNames.Add("Core");
	}
}
