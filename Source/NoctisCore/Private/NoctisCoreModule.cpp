// Unreal module glue for the engine-agnostic simulation core.
// Excluded from the standalone CMake build (it is the only core file that includes Unreal headers beyond
// CoreTypes.h / AssertionMacros.h). Installs the logging bridge: noctis::logMessage -> UE_LOG(LogNoctisCore).
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Noctis/Core/Log.h"

#include <string>

DEFINE_LOG_CATEGORY_STATIC(LogNoctisCore, Log, All);

class FNoctisCoreModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		noctis::setLogSink([](noctis::LogLevel Level, const std::string& Message)
		{
			// Messages are UTF-8 (French text, accented characters).
			const FString Text = FString(UTF8_TO_TCHAR(Message.c_str()));
			switch (Level)
			{
			case noctis::LogLevel::Verbose:
				UE_LOG(LogNoctisCore, Verbose, TEXT("%s"), *Text);
				break;
			case noctis::LogLevel::Info:
				UE_LOG(LogNoctisCore, Log, TEXT("%s"), *Text);
				break;
			case noctis::LogLevel::Warning:
				UE_LOG(LogNoctisCore, Warning, TEXT("%s"), *Text);
				break;
			case noctis::LogLevel::Error:
				UE_LOG(LogNoctisCore, Error, TEXT("%s"), *Text);
				break;
			}
		});
		// Verbose core messages are filtered by UE's own verbosity (LogNoctisCore Verbose via -LogCmds or DefaultEngine.ini [Core.Log]).
		noctis::setLogLevel(noctis::LogLevel::Verbose);
	}

	virtual void ShutdownModule() override
	{
		// Back to the core's default stdout/stderr sink: no dangling reference into an unloaded module.
		noctis::setLogSink(noctis::LogSink());
	}
};

IMPLEMENT_MODULE(FNoctisCoreModule, NoctisCore);
