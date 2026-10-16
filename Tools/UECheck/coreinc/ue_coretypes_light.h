// What a NoctisCore translation unit sees of Unreal in the UBT build (PCHUsage = NoPCHs): CoreTypes.h + AssertionMacros.h.
// Object-like and function-like macros of those headers that could collide with core identifiers.
#pragma once
#include <cstdint>
#include <cstddef>
typedef std::uint8_t uint8; typedef std::uint16_t uint16; typedef std::uint32_t uint32; typedef std::uint64_t uint64;
typedef std::int8_t int8; typedef std::int16_t int16; typedef std::int32_t int32; typedef std::int64_t int64;
typedef wchar_t TCHAR; typedef char ANSICHAR; typedef wchar_t WIDECHAR; typedef std::size_t SIZE_T; typedef std::ptrdiff_t SSIZE_T;
typedef std::uintptr_t UPTRINT; typedef std::intptr_t PTRINT;
#define TEXT(x) L##x
#define IN
#define OUT
#define OPTIONAL
#define FORCEINLINE inline
#define FORCENOINLINE __attribute__((noinline))
#define RESTRICT __restrict
#define ABSTRACT abstract
#define CONSTEXPR constexpr
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#define UE_LIKELY(x) LIKELY(x)
#define UE_UNLIKELY(x) UNLIKELY(x)
#define UE_ASSUME(x) __builtin_assume(x)
#define CA_ASSUME(x) __builtin_assume(x)
#define UE_NODISCARD [[nodiscard]]
#define UE_DEPRECATED(Version, Message) [[deprecated(Message)]]
#define DLLEXPORT __attribute__((visibility("default")))
#define DLLIMPORT __attribute__((visibility("default")))
#define PLATFORM_LINUX 1
#define PLATFORM_WINDOWS 0
#define PLATFORM_MAC 0
#define PLATFORM_LITTLE_ENDIAN 1
#define PLATFORM_64BITS 1
#define PLATFORM_CACHE_LINE_SIZE 64
#define TCHAR_TO_ANSI(x) x
#define DO_CHECK 1
#define DO_GUARD_SLOW 0
#define check(expr) ((void)sizeof(!!(expr)))
#define checkf(expr, format, ...) ((void)sizeof(!!(expr)))
#define checkSlow(expr) ((void)sizeof(!!(expr)))
#define checkfSlow(expr, format, ...) ((void)sizeof(!!(expr)))
#define checkNoEntry() ((void)0)
#define checkNoReentry() ((void)0)
#define checkNoRecursion() ((void)0)
#define verify(expr) ((void)(expr))
#define verifyf(expr, format, ...) ((void)(expr))
#define verifySlow(expr) ((void)(expr))
#define ensure(expr) (!!(expr))
#define ensureMsgf(expr, format, ...) (!!(expr))
#define ensureAlways(expr) (!!(expr))
#define ensureAlwaysMsgf(expr, format, ...) (!!(expr))
#define unimplemented() ((void)0)
#define UE_CHECK_IMPL(expr) ((void)sizeof(!!(expr)))
