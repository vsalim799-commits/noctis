// Minimal self-contained test framework for the Noctis core (no exceptions, no RTTI, no dependencies).
//
//   NOCTIS_TEST("core.math.wrap_angle") { CHECK_NEAR(wrapAngle(4.0f), 4.0f - kTwoPi, 1e-6); }
//   NOCTIS_TEST_LONG("sim.soak.20_days") { ... }            // only run with --label long|all
//
// CHECK* records a failure and continues; REQUIRE* records a failure and returns from the test
// function (tests are void functions, so REQUIRE is usable anywhere in their body, not in helpers
// returning a value). NOTE() prints a measured value next to the test result: every numeric test
// reports what it measured so CI logs double as a record of the reference values.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace noctistest
{
enum Label : unsigned
{
    kShort = 1u,
    kLong = 2u
};

using TestFn = void (*)();

struct TestInfo
{
    const char* name = "";
    TestFn fn = nullptr;
    const char* file = "";
    int line = 0;
    unsigned labels = kShort;
};

int registerTest(const char* name, TestFn fn, const char* file, int line, unsigned labels);
void fail(const char* file, int line, const std::string& message);
void failf(const char* file, int line, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;
void note(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;
bool currentTestFailed();
// Directory of the repository's Data/ folder (overridable with --data or NOCTIS_DATA_DIR).
const std::string& dataRoot();
// Worker threads used by multi-threaded tests (--threads, default: min(4, hardware)).
int testThreads();

inline std::string toString(bool v) { return v ? "true" : "false"; }
inline std::string toString(const char* v) { return v ? std::string("\"") + v + "\"" : "null"; }
inline std::string toString(const std::string& v) { return "\"" + v + "\""; }
inline std::string toString(double v)
{
    char b[64];
    std::snprintf(b, sizeof(b), "%.9g", v);
    return b;
}
inline std::string toString(float v) { return toString(static_cast<double>(v)); }
inline std::string toString(long long v) { return std::to_string(v); }
inline std::string toString(unsigned long long v) { return std::to_string(v); }
inline std::string toString(long v) { return std::to_string(v); }
inline std::string toString(unsigned long v) { return std::to_string(v); }
inline std::string toString(int v) { return std::to_string(v); }
inline std::string toString(unsigned v) { return std::to_string(v); }
inline std::string toString(short v) { return std::to_string(v); }
inline std::string toString(unsigned short v) { return std::to_string(v); }
inline std::string toString(signed char v) { return std::to_string(static_cast<int>(v)); }
inline std::string toString(unsigned char v) { return std::to_string(static_cast<unsigned>(v)); }
inline std::string toString(char v) { return std::string("'") + v + "'"; }
template <typename T>
std::string toString(const T&)
{
    return "<value>";
}

template <typename A, typename B>
bool compareFailed(const char* file, int line, const char* op, const char* ea, const char* eb, bool ok, const A& a, const B& b)
{
    if (!ok)
    {
        fail(file, line, std::string(ea) + " " + op + " " + eb + "  (" + toString(a) + " vs " + toString(b) + ")");
    }
    return !ok;
}
} // namespace noctistest

#define NOCTIS_TEST_CONCAT2(a, b) a##b
#define NOCTIS_TEST_CONCAT(a, b) NOCTIS_TEST_CONCAT2(a, b)

#define NOCTIS_TEST_WITH_LABELS(testName, testLabels)                                                                                               \
    static void NOCTIS_TEST_CONCAT(noctisTestBody_, __LINE__)();                                                                                     \
    [[maybe_unused]] static const int NOCTIS_TEST_CONCAT(noctisTestReg_, __LINE__) =                                                                \
        ::noctistest::registerTest(testName, &NOCTIS_TEST_CONCAT(noctisTestBody_, __LINE__), __FILE__, __LINE__, testLabels);                        \
    static void NOCTIS_TEST_CONCAT(noctisTestBody_, __LINE__)()

#define NOCTIS_TEST(testName) NOCTIS_TEST_WITH_LABELS(testName, ::noctistest::kShort)
#define NOCTIS_TEST_LONG(testName) NOCTIS_TEST_WITH_LABELS(testName, ::noctistest::kLong)

#define NOTE(...) ::noctistest::note(__VA_ARGS__)

#define CHECK(cond)                                                                                                                                  \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (!(cond))                                                                                                                                 \
        {                                                                                                                                            \
            ::noctistest::fail(__FILE__, __LINE__, "CHECK(" #cond ")");                                                                              \
        }                                                                                                                                            \
    } while (0)

#define REQUIRE(cond)                                                                                                                                \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (!(cond))                                                                                                                                 \
        {                                                                                                                                            \
            ::noctistest::fail(__FILE__, __LINE__, "REQUIRE(" #cond ")");                                                                            \
            return;                                                                                                                                  \
        }                                                                                                                                            \
    } while (0)

#define NOCTIS_TEST_CMP(a, b, op, onFail)                                                                                                            \
    do                                                                                                                                               \
    {                                                                                                                                                \
        const auto& noctisA_ = (a);                                                                                                                  \
        const auto& noctisB_ = (b);                                                                                                                  \
        if (::noctistest::compareFailed(__FILE__, __LINE__, #op, #a, #b, (noctisA_ op noctisB_), noctisA_, noctisB_))                                 \
        {                                                                                                                                            \
            onFail;                                                                                                                                  \
        }                                                                                                                                            \
    } while (0)

#define CHECK_EQ(a, b) NOCTIS_TEST_CMP(a, b, ==, (void)0)
#define CHECK_NE(a, b) NOCTIS_TEST_CMP(a, b, !=, (void)0)
#define CHECK_LT(a, b) NOCTIS_TEST_CMP(a, b, <, (void)0)
#define CHECK_LE(a, b) NOCTIS_TEST_CMP(a, b, <=, (void)0)
#define CHECK_GT(a, b) NOCTIS_TEST_CMP(a, b, >, (void)0)
#define CHECK_GE(a, b) NOCTIS_TEST_CMP(a, b, >=, (void)0)
#define REQUIRE_EQ(a, b) NOCTIS_TEST_CMP(a, b, ==, return)
#define REQUIRE_GT(a, b) NOCTIS_TEST_CMP(a, b, >, return)
#define REQUIRE_GE(a, b) NOCTIS_TEST_CMP(a, b, >=, return)

// |a - b| <= tol (fails on NaN).
#define CHECK_NEAR(a, b, tol)                                                                                                                        \
    do                                                                                                                                               \
    {                                                                                                                                                \
        const double noctisA_ = static_cast<double>(a);                                                                                              \
        const double noctisB_ = static_cast<double>(b);                                                                                              \
        const double noctisT_ = static_cast<double>(tol);                                                                                            \
        if (!(std::fabs(noctisA_ - noctisB_) <= noctisT_))                                                                                           \
        {                                                                                                                                            \
            ::noctistest::failf(__FILE__, __LINE__, "CHECK_NEAR(%s, %s, %s): %.9g vs %.9g, |diff| = %.3g", #a, #b, #tol, noctisA_, noctisB_,          \
                                std::fabs(noctisA_ - noctisB_));                                                                                     \
        }                                                                                                                                            \
    } while (0)

// |a - b| <= rel * |b| (fails on NaN).
#define CHECK_REL(a, b, rel)                                                                                                                         \
    do                                                                                                                                               \
    {                                                                                                                                                \
        const double noctisA_ = static_cast<double>(a);                                                                                              \
        const double noctisB_ = static_cast<double>(b);                                                                                              \
        const double noctisR_ = static_cast<double>(rel);                                                                                            \
        if (!(std::fabs(noctisA_ - noctisB_) <= noctisR_ * std::fabs(noctisB_)))                                                                     \
        {                                                                                                                                            \
            ::noctistest::failf(__FILE__, __LINE__, "CHECK_REL(%s, %s, %s): %.9g vs %.9g, rel. diff = %.3g", #a, #b, #rel, noctisA_, noctisB_,         \
                                noctisB_ != 0.0 ? std::fabs(noctisA_ - noctisB_) / std::fabs(noctisB_) : HUGE_VAL);                                 \
        }                                                                                                                                            \
    } while (0)

#define CHECK_FINITE(x)                                                                                                                              \
    do                                                                                                                                               \
    {                                                                                                                                                \
        const double noctisV_ = static_cast<double>(x);                                                                                              \
        if (!std::isfinite(noctisV_))                                                                                                                \
        {                                                                                                                                            \
            ::noctistest::failf(__FILE__, __LINE__, "CHECK_FINITE(%s): %g", #x, noctisV_);                                                           \
        }                                                                                                                                            \
    } while (0)

// Inclusive range check lo <= x <= hi (fails on NaN).
#define CHECK_RANGE(x, lo, hi)                                                                                                                       \
    do                                                                                                                                               \
    {                                                                                                                                                \
        const double noctisV_ = static_cast<double>(x);                                                                                              \
        const double noctisL_ = static_cast<double>(lo);                                                                                             \
        const double noctisH_ = static_cast<double>(hi);                                                                                             \
        if (!(noctisV_ >= noctisL_ && noctisV_ <= noctisH_))                                                                                         \
        {                                                                                                                                            \
            ::noctistest::failf(__FILE__, __LINE__, "CHECK_RANGE(%s): %.9g not in [%.9g, %.9g]", #x, noctisV_, noctisL_, noctisH_);                   \
        }                                                                                                                                            \
    } while (0)
