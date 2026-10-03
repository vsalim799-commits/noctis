# Standalone build of the Noctis simulation core, headless runner and tests.
# The same sources are compiled by Unreal Build Tool as the "NoctisCore" module.
cmake_minimum_required(VERSION 3.20)
project(NoctisSim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(NOCTIS_UE_STRICT "Mimic Unreal Engine constraints: no exceptions, no RTTI, warnings as errors" ON)

file(GLOB_RECURSE NOCTIS_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Source/NoctisCore/Private/*.cpp)
# The UE module glue needs Unreal headers.
list(FILTER NOCTIS_CORE_SOURCES EXCLUDE REGEX ".*/NoctisCoreModule\\.cpp$")

add_library(noctis_core STATIC ${NOCTIS_CORE_SOURCES})
target_include_directories(noctis_core PUBLIC ${CMAKE_SOURCE_DIR}/Source/NoctisCore/Public)
target_compile_definitions(noctis_core PUBLIC NOCTIS_STANDALONE=1)

find_package(Threads REQUIRED)
target_link_libraries(noctis_core PUBLIC Threads::Threads)

if(NOCTIS_UE_STRICT)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(noctis_core PUBLIC -fno-exceptions -fno-rtti -Wall -Wextra -Wshadow -Wno-unused-parameter -Werror)
  elseif(MSVC)
    target_compile_options(noctis_core PUBLIC /GR- /W4 /WX /wd4100)
  endif()
endif()

add_executable(noctis_sim ${CMAKE_SOURCE_DIR}/Tools/Cli/noctis_sim.cpp)
target_link_libraries(noctis_sim PRIVATE noctis_core)

file(GLOB NOCTIS_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/Tools/Tests/*.cpp)
add_executable(noctis_tests ${NOCTIS_TEST_SOURCES})
target_link_libraries(noctis_tests PRIVATE noctis_core)
target_compile_definitions(noctis_tests PRIVATE NOCTIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_definitions(noctis_sim PRIVATE NOCTIS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

enable_testing()
add_test(NAME noctis_tests COMMAND noctis_tests)
