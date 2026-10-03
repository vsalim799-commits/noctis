// Logging with a pluggable sink (the Unreal module routes it to UE_LOG).
#pragma once

#include "Noctis/Core/Platform.h"

#include <functional>
#include <string>

namespace noctis
{
enum class LogLevel : u8
{
    Verbose,
    Info,
    Warning,
    Error
};

using LogSink = std::function<void(LogLevel, const std::string&)>;

NOCTIS_API void setLogSink(LogSink sink);
NOCTIS_API void setLogLevel(LogLevel minimum);
NOCTIS_API void logMessage(LogLevel level, const std::string& message);
NOCTIS_API std::string formatString(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#define NOCTIS_LOG_INFO(...) ::noctis::logMessage(::noctis::LogLevel::Info, ::noctis::formatString(__VA_ARGS__))
#define NOCTIS_LOG_WARN(...) ::noctis::logMessage(::noctis::LogLevel::Warning, ::noctis::formatString(__VA_ARGS__))
#define NOCTIS_LOG_ERROR(...) ::noctis::logMessage(::noctis::LogLevel::Error, ::noctis::formatString(__VA_ARGS__))
#define NOCTIS_LOG_VERBOSE(...) ::noctis::logMessage(::noctis::LogLevel::Verbose, ::noctis::formatString(__VA_ARGS__))
} // namespace noctis
