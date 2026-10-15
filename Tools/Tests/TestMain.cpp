// Test runner for noctis_tests.
//
//   noctis_tests [options] [pattern ...]
//     pattern               glob on the test name ('*' and '?'); without wildcard: prefix match
//     --exclude PATTERN     skip matching tests (repeatable)
//     --label short|long|all  which label set to run (default: short)
//     --list                list matching tests and exit
//     --data DIR            Data/ directory (default: NOCTIS_DATA_DIR env, else the source tree)
//     --threads N           worker threads for multi-threaded tests (default: min(4, hardware))
//     --skip-unless-env VAR exit with code 77 (CTest "skipped") unless VAR is set to a non-empty value
//     --quiet               only print failures and the summary
// Exit code: 0 all selected tests passed, 1 at least one failure, 2 usage error / nothing selected,
// 77 skipped (see --skip-unless-env).
#include "TestFramework.h"

#include "Noctis/Core/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef NOCTIS_SOURCE_DIR
#define NOCTIS_SOURCE_DIR "."
#endif

namespace noctistest
{
namespace runnerimpl
{
std::vector<TestInfo>& registry()
{
    static std::vector<TestInfo> tests;
    return tests;
}

struct RunState
{
    const TestInfo* current = nullptr;
    int failuresInTest = 0;
    bool quiet = false;
    std::string dataRoot;
    int threads = 0;
};

RunState& state()
{
    static RunState s;
    return s;
}

bool globMatch(const char* pattern, const char* text)
{
    // Iterative glob with backtracking on the last '*'.
    const char* starP = nullptr;
    const char* starT = nullptr;
    while (*text)
    {
        if (*pattern == '*')
        {
            starP = pattern++;
            starT = text;
        }
        else if (*pattern == '?' || *pattern == *text)
        {
            ++pattern;
            ++text;
        }
        else if (starP)
        {
            pattern = starP + 1;
            text = ++starT;
        }
        else
        {
            return false;
        }
    }
    while (*pattern == '*')
    {
        ++pattern;
    }
    return *pattern == '\0';
}

bool matches(const std::string& pattern, const char* name)
{
    if (pattern.find_first_of("*?") == std::string::npos)
    {
        return std::strncmp(name, pattern.c_str(), pattern.size()) == 0;
    }
    return globMatch(pattern.c_str(), name);
}

void printUsage()
{
    std::printf("usage: noctis_tests [--label short|long|all] [--exclude PATTERN] [--list] [--data DIR] [--threads N]\n"
                "                    [--skip-unless-env VAR] [--quiet] [pattern ...]\n");
}
} // namespace runnerimpl

int registerTest(const char* name, TestFn fn, const char* file, int line, unsigned labels)
{
    TestInfo t;
    t.name = name;
    t.fn = fn;
    t.file = file;
    t.line = line;
    t.labels = labels;
    runnerimpl::registry().push_back(t);
    return static_cast<int>(runnerimpl::registry().size());
}

void fail(const char* file, int line, const std::string& message)
{
    runnerimpl::RunState& s = runnerimpl::state();
    ++s.failuresInTest;
    const char* base = file;
    for (const char* p = file; *p; ++p)
    {
        if (*p == '/' || *p == '\\')
        {
            base = p + 1;
        }
    }
    std::printf("    FAIL %s:%d: %s\n", base, line, message.c_str());
    std::fflush(stdout);
}

void failf(const char* file, int line, const char* fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    fail(file, line, buf);
}

void note(const char* fmt, ...)
{
    if (runnerimpl::state().quiet)
    {
        return;
    }
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    std::printf("    note: %s\n", buf);
    std::fflush(stdout);
}

bool currentTestFailed() { return runnerimpl::state().failuresInTest > 0; }

const std::string& dataRoot() { return runnerimpl::state().dataRoot; }

int testThreads() { return runnerimpl::state().threads; }
} // namespace noctistest

int main(int argc, char** argv)
{
    using namespace noctistest;
    using namespace noctistest::runnerimpl;
    std::vector<std::string> patterns;
    std::vector<std::string> excludes;
    unsigned labelMask = kShort;
    bool list = false;
    RunState& st = state();
    if (const char* env = std::getenv("NOCTIS_DATA_DIR"))
    {
        st.dataRoot = env;
    }
    if (st.dataRoot.empty())
    {
        st.dataRoot = std::string(NOCTIS_SOURCE_DIR) + "/Data";
    }
    st.threads = static_cast<int>(std::min(4u, std::max(2u, std::thread::hardware_concurrency())));
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto value = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--list")
        {
            list = true;
        }
        else if (a == "--quiet")
        {
            st.quiet = true;
        }
        else if (a == "--label")
        {
            const char* v = value();
            const std::string l = v ? v : "";
            if (l == "short")
            {
                labelMask = kShort;
            }
            else if (l == "long")
            {
                labelMask = kLong;
            }
            else if (l == "all")
            {
                labelMask = kShort | kLong;
            }
            else
            {
                printUsage();
                return 2;
            }
        }
        else if (a == "--exclude")
        {
            const char* v = value();
            if (!v)
            {
                printUsage();
                return 2;
            }
            excludes.push_back(v);
        }
        else if (a == "--data")
        {
            const char* v = value();
            if (!v)
            {
                printUsage();
                return 2;
            }
            st.dataRoot = v;
        }
        else if (a == "--threads")
        {
            const char* v = value();
            if (!v)
            {
                printUsage();
                return 2;
            }
            st.threads = std::max(1, std::atoi(v));
        }
        else if (a == "--skip-unless-env")
        {
            const char* v = value();
            if (!v)
            {
                printUsage();
                return 2;
            }
            const char* env = std::getenv(v);
            if (!env || !*env)
            {
                std::printf("skipped: set %s=1 to run these tests\n", v);
                return 77;
            }
        }
        else if (a == "--help" || a == "-h")
        {
            printUsage();
            return 0;
        }
        else if (!a.empty() && a[0] == '-')
        {
            std::printf("unknown option %s\n", a.c_str());
            printUsage();
            return 2;
        }
        else
        {
            patterns.push_back(a);
        }
    }

    std::vector<const TestInfo*> selected;
    for (const TestInfo& t : registry())
    {
        if ((t.labels & labelMask) == 0)
        {
            continue;
        }
        bool include = patterns.empty();
        for (const std::string& p : patterns)
        {
            include = include || matches(p, t.name);
        }
        for (const std::string& p : excludes)
        {
            include = include && !matches(p, t.name);
        }
        if (include)
        {
            selected.push_back(&t);
        }
    }
    std::sort(selected.begin(), selected.end(), [](const TestInfo* a, const TestInfo* b) { return std::strcmp(a->name, b->name) < 0; });
    if (list)
    {
        for (const TestInfo* t : selected)
        {
            std::printf("%s%s\n", t->name, (t->labels & kLong) ? "  [long]" : "");
        }
        return 0;
    }
    if (selected.empty())
    {
        std::printf("no test matches the given patterns/labels\n");
        return 2;
    }
    // Library log output is noise in test logs: only warnings and errors.
    noctis::setLogLevel(noctis::LogLevel::Warning);

    int failedTests = 0;
    std::vector<std::string> failedNames;
    const auto runStart = std::chrono::steady_clock::now();
    std::printf("noctis_tests: %zu test(s), data = %s, threads = %d\n", selected.size(), st.dataRoot.c_str(), st.threads);
    for (const TestInfo* t : selected)
    {
        st.current = t;
        st.failuresInTest = 0;
        if (!st.quiet)
        {
            std::printf("[ RUN  ] %s\n", t->name);
            std::fflush(stdout);
        }
        const auto t0 = std::chrono::steady_clock::now();
        t->fn();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (st.failuresInTest > 0)
        {
            ++failedTests;
            failedNames.push_back(t->name);
            std::printf("[ FAIL ] %s (%d failure(s), %.0f ms)\n", t->name, st.failuresInTest, ms);
        }
        else if (!st.quiet)
        {
            std::printf("[  OK  ] %s (%.0f ms)\n", t->name, ms);
        }
        std::fflush(stdout);
    }
    const double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count();
    std::printf("\n%zu test(s) run in %.1f s: %zu passed, %d failed\n", selected.size(), total, selected.size() - static_cast<size_t>(failedTests),
                failedTests);
    for (const std::string& n : failedNames)
    {
        std::printf("  failed: %s\n", n.c_str());
    }
    return failedTests == 0 ? 0 : 1;
}
