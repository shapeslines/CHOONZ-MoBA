# MOBA engine — top-level build. The CMake link graph IS the architecture (ADR-0006).
cmake_minimum_required(VERSION 3.28)

project(moba LANGUAGES CXX VERSION 0.0.0)

# --- Language: C-style / data-oriented C++17, no extensions (ADR-0002/0009) ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake injects a default /EHsc on MSVC; strip it so moba_options' /EHs-c- (no
# exceptions, ADR-0009) doesn't trigger D9025 "overriding /EHs" on every TU.
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# Generate compile_commands.json for tooling (git-ignored).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Options ---
option(MOBA_WERROR "Treat warnings as errors (/WX) — on in CI/test, off locally" OFF)
option(MOBA_ASAN "Build with AddressSanitizer (/fsanitize=address) — activates arena poison hooks (G10)" OFF)

# --- Project-wide flag/posture targets (must precede module add_subdirectory) ---
include(cmake/CompilerWarnings.cmake)   # moba_warnings
include(cmake/EngineOptions.cmake)      # moba_options
include(cmake/CompileShaders.cmake)     # add_shader_library()

# --- Vulkan (ADR-0004: link HEADERS only; the loader is hand-loaded) ---
# Soft for the M0.2 spine (render is an empty skeleton). Becomes REQUIRED at Phase 2
# (Vulkan bring-up). Install the LunarG Vulkan SDK (sets VULKAN_SDK) before then.
# PIN (G12): project-pinned SDK is 1.4.357.0 (winget KhronosGroup.VulkanSDK); the
# code gate is API 1.3 per ADR-0012 (dynamicRendering + synchronization2).
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    message(STATUS "Vulkan SDK found: ${Vulkan_INCLUDE_DIRS}")
    if(Vulkan_VERSION VERSION_LESS "1.3.0")
        message(WARNING
            "Vulkan headers report API ${Vulkan_VERSION} — older than the ADR-0012 "
            "hard minimum (1.3). Expect device-selection or feature failures.")
    endif()
else()
    message(WARNING
        "Vulkan SDK NOT found (VULKAN_SDK unset). The 'render' module builds as an "
        "empty skeleton for now. Install the LunarG Vulkan SDK before Phase 2 — it "
        "becomes REQUIRED there (find_package(Vulkan REQUIRED)).")
endif()

# --- Engine modules (M0.2 spine: the four that establish the link graph) ---
add_subdirectory(engine/core)
add_subdirectory(engine/math)
add_subdirectory(engine/serialize)
add_subdirectory(engine/platform)
add_subdirectory(engine/sim)
add_subdirectory(engine/render)

# --- Tools & tests ---
enable_testing()                        # register CTest tests (M1.4); must be top-level
add_subdirectory(tools/sandbox)         # M0.3: window + message loop
add_subdirectory(tools/visualize)       # BMP dumps of the fixed-point math vs libm
add_subdirectory(tools/replay)          # M3.0: deterministic replay record/inspect/verify
add_subdirectory(tests)                 # M1.4: self-registering harness + CTest suites
# assets / net + cooker arrive in their phases.

message(STATUS "moba spine configured — modules: eng_core eng_math eng_serialize eng_platform eng_sim eng_render")
