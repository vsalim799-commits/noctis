// Noctis simulation core — platform glue.
//
// The simulation core is engine-agnostic C++20. It is compiled two ways:
//   * standalone (CMake, NOCTIS_STANDALONE defined) for the headless runner and tests;
//   * as the Unreal Engine module "NoctisCore" (UBT defines NOCTISCORE_API).
// Rules that keep both builds healthy: no exceptions, no RTTI, no identifiers that collide
// with Unreal macros (PI, check, verify, ensure, TEXT, INDEX_NONE ...) or Windows macros
// (near, far, small, interface, ERROR ...), no file-scope helpers with generic names
// (unity builds concatenate translation units).
//
// Export macro: core declarations use NOCTISCORE_API — the name UBT gives the "NoctisCore" module.
// Never use NOCTIS_API in the core: in the Unreal build that macro belongs to the game module "Noctis"
// (UBT defines it there as DLLEXPORT), so core headers included from the game module would export
// core classes from the wrong DLL. NOCTIS_API survives only as a standalone alias for older code.
#pragma once

#include <cstddef>
#include <cstdint>

#if defined(NOCTIS_STANDALONE)
#define NOCTISCORE_API
#define NOCTIS_API
#include <cassert>
#define NOCTIS_ASSERT(cond) assert(cond)
#elif defined(NOCTISCORE_API)
// Unreal build. UBT defines NOCTISCORE_API as DLLEXPORT/DLLIMPORT (modular builds) or empty (monolithic);
// DLLEXPORT/DLLIMPORT come from CoreTypes.h and checkSlow from AssertionMacros.h. The module is compiled
// without a shared PCH (see NoctisCore.Build.cs), so these two light headers are all of UE a core TU sees.
// Logging is bridged by NoctisCoreModule.cpp, which installs a LogSink that forwards to UE_LOG.
#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#define NOCTIS_ASSERT(cond) checkSlow(cond)
#else
#error "Noctis core: define NOCTIS_STANDALONE (CMake) or build it as the Unreal module NoctisCore (UBT defines NOCTISCORE_API)."
#endif

namespace noctis
{
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using f32 = float;
using f64 = double;
} // namespace noctis
