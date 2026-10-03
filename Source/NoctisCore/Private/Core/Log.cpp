#include "Noctis/Core/Log.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace noctis
{
namespace logimpl
{
std::mutex& sinkMutex()
{
    static std::mutex m;
    return m;
}
LogSink& sinkRef()
{
    static LogSink sink;
    return sink;
}
LogLevel& minLevelRef()
{
    static LogLevel level = LogLevel::Info;
    return level;
}
const char* levelTag(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Verbose: return "VERBOSE";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "?";
}
} // namespace logimpl

void setLogSink(LogSink sink)
{
    std::lock_guard<std::mutex> lock(logimpl::sinkMutex());
    logimpl::sinkRef() = std::move(sink);
}

void setLogLevel(LogLevel minimum)
{
    std::lock_guard<std::mutex> lock(logimpl::sinkMutex());
    logimpl::minLevelRef() = minimum;
}

void logMessage(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(logimpl::sinkMutex());
    if (static_cast<int>(level) < static_cast<int>(logimpl::minLevelRef()))
    {
        return;
    }
    if (logimpl::sinkRef())
    {
        logimpl::sinkRef()(level, message);
    }
    else
    {
        std::fprintf(level >= LogLevel::Warning ? stderr : stdout, "[noctis %s] %s\n", logimpl::levelTag(level), message.c_str());
    }
}

std::string formatString(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    std::string out;
    if (needed > 0)
    {
        std::vector<char> buf(static_cast<size_t>(needed) + 1u);
        std::vsnprintf(buf.data(), buf.size(), fmt, args);
        out.assign(buf.data(), static_cast<size_t>(needed));
    }
    va_end(args);
    return out;
}
} // namespace noctis
