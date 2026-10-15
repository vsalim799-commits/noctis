# Standalone build of the Noctis simulation core, headless runner, tests and benchmark.
# The same sources are compiled by Unreal Build Tool as the "NoctisCore" module.
#
# Supported toolchains: GCC >= 11, Clang >= 14, AppleClang >= 14, MSVC 2022 (19.30+).
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j
#   ctest --test-dir build                       # quick tests (label "short"), ~1 min
#   NOCTIS_LONG_TESTS=1 ctest --test-dir build -L long   # soak / long determinism runs
cmake_minimum_required(VERSION 3.20)
project(NoctisSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_property(NOCTIS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT NOCTIS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(NOCTIS_UE_STRICT "Mimic Unreal Engine constraints: no exceptions, no RTTI, warnings as errors" ON)
option(NOCTIS_BUILD_TESTS "Build noctis_tests and register them with CTest" ON)
option(NOCTIS_BUILD_BENCH "Build the noctis_bench benchmark" ON)

# ----------------------------------------------------------------------------- compiler flags
# One interface target carries the strict flags so every executable (core, CLI, tests, bench)
# is held to the same rules as the Unreal module.
add_library(noctis_strict INTERFACE)
if(NOCTIS_UE_STRICT)
  if(MSVC)
    # CMake's MSVC defaults carry /GR and /EHsc; drop them so /GR- and /EHs-c- are not overridden
    # (and cl does not warn D9025 about it).
    foreach(flag_var CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
      string(REGEX REPLACE "/EH[a-z]*-?" "" ${flag_var} "${${flag_var}}")
      string(REGEX REPLACE "/GR-?" "" ${flag_var} "${${flag_var}}")
    endforeach()
    # /W4 /WX /permissive- like a strict UE module. The narrowing-conversion warnings C4244, C4267 and
    # C4305 are disabled exactly as Unreal does by default (ModuleRules.UnsafeTypeCastWarningLevel =
    # Off); C4100 (unreferenced parameter) matches -Wno-unused-parameter on GCC/Clang.
    target_compile_options(noctis_strict INTERFACE
      /W4 /WX /permissive- /GR- /EHs-c- /utf-8 /Zc:__cplusplus
      /wd4100 /wd4244 /wd4267 /wd4305)
    target_compile_definitions(noctis_strict INTERFACE _HAS_EXCEPTIONS=0 _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang") # Clang also matches AppleClang
    target_compile_options(noctis_strict INTERFACE -fno-exceptions -fno-rtti -Wall -Wextra -Wshadow -Wno-unused-parameter -Werror)
  endif()
elseif(MSVC)
  target_compile_options(noctis_strict INTERFACE /utf-8 /permissive-)
endif()

# ----------------------------------------------------------------------------- core library
file(GLOB_RECURSE NOCTIS_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/NoctisCore/Private/*.cpp)
# The UE module glue needs Unreal headers.
list(FILTER NOCTIS_CORE_SOURCES EXCLUDE REGEX ".*/NoctisCoreModule\\.cpp$")

add_library(noctis_core STATIC ${NOCTIS_CORE_SOURCES})
target_include_directories(noctis_core PUBLIC ${CMAKE_SOURCE_DIR}/Source/NoctisCore/Public)
target_compile_definitions(noctis_core PUBLIC NOCTIS_STANDALONE=1)

find_package(Threads REQUIRED)
target_link_libraries(noctis_core PUBLIC Threads::Threads noctis_strict)

# ----------------------------------------------------------------------------- headless runner
file(GLOB NOCTIS_SIM_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Tools/Cli/*.cpp)
add_executable(noctis_sim ${NOCTIS_SIM_SOURCES})
target_link_libraries(noctis_sim PRIVATE noctis_core)
target_compile_definitions(noctis_sim PRIVATE NOCTIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# ----------------------------------------------------------------------------- benchmark
if(NOCTIS_BUILD_BENCH)
  add_executable(noctis_bench ${CMAKE_SOURCE_DIR}/Tools/Bench/bench.cpp)
  target_link_libraries(noctis_bench PRIVATE noctis_core)
  target_compile_definitions(noctis_bench PRIVATE NOCTIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(WIN32)
    target_link_libraries(noctis_bench PRIVATE psapi)
  endif()
endif()

# ----------------------------------------------------------------------------- tests
if(NOCTIS_BUILD_TESTS)
  file(GLOB NOCTIS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Tools/Tests/*.cpp)
  add_executable(noctis_tests ${NOCTIS_TEST_SOURCES})
  target_link_libraries(noctis_tests PRIVATE noctis_core)
  target_compile_definitions(noctis_tests PRIVATE NOCTIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

  enable_testing()
  # One CTest entry per test family so failures are reported separately and ctest -j can spread
  # them. Label "short" = default quick suite; label "long" = soak and long determinism runs,
  # skipped (exit code 77) unless NOCTIS_LONG_TESTS is set in the environment.
  set(NOCTIS_DATA_DIR ${CMAKE_SOURCE_DIR}/Data)
  foreach(group core data world creatures research sim)
    add_test(NAME ${group} COMMAND noctis_tests --data ${NOCTIS_DATA_DIR} --label short ${group}.)
    set_tests_properties(${group} PROPERTIES LABELS "short" TIMEOUT 600)
  endforeach()
  set_tests_properties(sim PROPERTIES RUN_SERIAL TRUE) # multi-threaded runs: keep timings stable
  add_test(NAME long COMMAND noctis_tests --data ${NOCTIS_DATA_DIR} --label long --skip-unless-env NOCTIS_LONG_TESTS)
  set_tests_properties(long PROPERTIES LABELS "long" TIMEOUT 7200 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
endif()
